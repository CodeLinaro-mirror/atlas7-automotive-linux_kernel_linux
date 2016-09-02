/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include "../kasobj.h"
#include "../kasop.h"
#include "../kcm.h"
#include "../../dsp.h"
#include "utils.h"

#define CVC_SEND_DEFAULT_MODE (1)
#define CVC_SEND_DEFAULT_UCID (0x01)
#define CVC_SEND_CUST_UCID (0x02)

#define CVC_SEND_CTRL_NUM (2)
#define CVC_SEND_CTRL_MODE_IDX (0)
#define CVC_SEND_CTRL_UCID_IDX (1)

#define CVC_SEND_MODE_MAX (2)
#define CVC_SEND_UCID_MAX (2)

#define CVC_SEND_CTRL_ID_MODE (0x1)
#define CVC_SEND_CTRL_ID_MUTE (0x2)

struct cvc_send_ctx {
	u16 mode; /* 0: mute, 1: process, 2: passthrough */
	u16 ucid; /* 0x01: default setting, 0x02: tier1 predefined setting */
};

struct cvc_send_mode_msg {
	u16 block;
	u16 ctrl_id;
	u16 value_h;
	u16 value_l;
};

static int set_cvc_send_ucid(struct kasobj_op *op)
{
	struct cvc_send_ctx *ctx = op->context;
	u16 ucid;
	int ret;

	if (!op->obj.life_cnt)
		return 0;

	ucid = ctx->ucid;
	if (ucid != CVC_SEND_DEFAULT_UCID && ucid != CVC_SEND_CUST_UCID) {
		pr_err("KASOBJ(%s): invalid UCID(0x%x)!\n", op->obj.name, ucid);
		return -EINVAL;
	}

	ret = kalimba_operator_message(op->op_id, OPERATOR_MSG_SET_UCID,
		1, &ucid, NULL, NULL, __kcm_resp);
	if (ret) {
		pr_err("KASOBJ(%s): set UCID failed(%d)!\n", op->obj.name, ret);
		return ret;
	}

	return 0;
}

static inline int send_mode_msg(struct kasobj_op *op,
	struct cvc_send_mode_msg mode_msg)
{
	int ret;

	ret = kalimba_operator_message(op->op_id, OPMSG_COMMON_SET_CONTROL,
		4, (u16 *)&mode_msg, NULL, NULL, __kcm_resp);
	if (ret) {
		pr_err("KASOBJ(%s): Set CVC Send mode failed(%d)!\n",
			op->obj.name, ret);
		return ret;
	}
	return 0;
}

static int set_cvc_send_mode(struct kasobj_op *op)
{
	struct cvc_send_ctx *ctx = op->context;
	struct cvc_send_mode_msg msg = {
		.block = 1,
		.value_h = 0,
	};

	if (!op->obj.life_cnt)
		return 0;

	switch (ctx->mode) {
	case 0:	/* mute */
		msg.ctrl_id = CVC_SEND_CTRL_ID_MUTE;
		msg.value_l = 1; /* Non zero will mute cvc send */
		send_mode_msg(op, msg);
		break;
	case 1: /* process */
		/* First, unmute it */
		msg.ctrl_id = CVC_SEND_CTRL_ID_MUTE;
		msg.value_l = 0;
		send_mode_msg(op, msg);
		/* Set it to full process */
		msg.ctrl_id = CVC_SEND_CTRL_ID_MODE;
		msg.value_l = 2;
		send_mode_msg(op, msg);
		break;
	case 2: /* passthrough */
		/* First, unmute it */
		msg.ctrl_id = CVC_SEND_CTRL_ID_MUTE;
		msg.value_l = 0;
		send_mode_msg(op, msg);
		/* Set it to passthrough */
		msg.ctrl_id = CVC_SEND_CTRL_ID_MODE;
		msg.value_l = 4; /* First mic */
		send_mode_msg(op, msg);
		if (kcm_enable_2mic_cvc) {
			msg.value_l = 5; /* Second mic */
			send_mode_msg(op, msg);
		}
		break;
	default:
		pr_err("KASOBJ(%s): Invalid mode value(%d)!\n",
			op->obj.name, ctx->mode);
		return -EINVAL;
	}

	return 0;
}

static int cvc_send_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ctl_idx, value;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &ctl_idx);
	struct cvc_send_ctx *ctx = op->context;

	BUG_ON(ctl_idx < 0 || ctl_idx >= CVC_SEND_CTRL_NUM);

	switch (ctl_idx) {
	case CVC_SEND_CTRL_MODE_IDX:
		value = ctx->mode;
		break;
	case CVC_SEND_CTRL_UCID_IDX:
		value = ctx->ucid;
		break;
	default:
		pr_err("KASOP(%s): CVC Send get, invalid control number !\n",
			op->obj.name);
		return -EINVAL;
	}
	ucontrol->value.integer.value[0] = value;

	return 0;
}

static int cvc_send_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ctl_idx;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &ctl_idx);
	struct cvc_send_ctx *ctx = op->context;
	u16 value = ucontrol->value.integer.value[0];

	BUG_ON(ctl_idx < 0 || ctl_idx >= CVC_SEND_CTRL_NUM);

	switch (ctl_idx) {
	case CVC_SEND_CTRL_MODE_IDX:
		if (value != ctx->mode) {
			kcm_lock();
			ctx->mode = value;
			set_cvc_send_mode(op);
			kcm_unlock();
		}
		break;
	case CVC_SEND_CTRL_UCID_IDX:
		if (value != ctx->ucid) {
			/* UCID: 1 ~ 2 */
			if (value != CVC_SEND_DEFAULT_UCID &&
				value != CVC_SEND_CUST_UCID)
				return -EINVAL;
			kcm_lock();
			ctx->ucid = value;
			set_cvc_send_ucid(op);
			kcm_unlock();
		}
		break;
	default:
		pr_err("KASOP(%s): CVC Send put, invalid control number !\n",
			op->obj.name);
		return -EINVAL;
	}

	return 0;
}

/* Create control interfaces */
static int cvc_send_init(struct kasobj_op *op)
{
	struct cvc_send_ctx *ctx = kzalloc(
		sizeof(struct cvc_send_ctx), GFP_KERNEL);
	struct snd_kcontrol_new *ctrl;
	char names_buf[256], *names = names_buf, *name;
	int ctrl_idx = 0; /* control interface index */
	int max;

	ctx->mode = CVC_SEND_DEFAULT_MODE;
	ctx->ucid = CVC_SEND_DEFAULT_UCID;
	op->context = ctx;
	if (!op->db->ctrl_names.s)
		return 0;

	if (snprintf(names_buf, 256, "%s", op->db->ctrl_names.s) >= 256) {
		pr_err("KASOP(%s): control names too long!\n", op->obj.name);
		return -EINVAL;
	}

	while ((name = strsep(&names, ":;"))) {
		if (ctrl_idx >= CVC_SEND_CTRL_NUM) {
			pr_err("KASOP(%s): too many controls!\n",
				op->obj.name);
			return -EINVAL;
		}
		if (kcm_strcasestr(name, "Mode"))
			max = CVC_SEND_MODE_MAX;
		else if (kcm_strcasestr(name, "UCID"))
			max = CVC_SEND_UCID_MAX;
		else {
			pr_err("KASOP(%s): unknown control '%s'!\n",
				op->obj.name, name);
			return -EINVAL;
		}

		ctrl = kasop_ctrl_single_ext_tlv(name, op, max,
			cvc_send_get, cvc_send_put, NULL, ctrl_idx);
		kcm_register_ctrl(ctrl);
		ctrl_idx++;
	}

	return 0;
}

/* Called before the operator is created */
static int cvc_send_prepare(struct kasobj_op *op,
	const struct kasobj_param *param)
{
	if (kcm_enable_2mic_cvc) {
		switch (param->rate) {
		case 8000:
			op->cap_id = CAPABILITY_ID_CVCHF2MIC_SEND_NB;
			break;
		case 16000:
			op->cap_id = CAPABILITY_ID_CVCHF2MIC_SEND_WB;
			break;
		case 24000:
			op->cap_id = CAPABILITY_ID_CVCHF2MIC_SEND_UWB;
			break;
		default:
			pr_err("KASOBJ(%s): Unsupported sample rate(%d) for 2Mic!\n",
				op->obj.name, param->rate);
			return -EINVAL;
		}
	} else {
		switch (param->rate) {
		case 8000:
			op->cap_id = CAPABILITY_ID_CVCHF1MIC_SEND_NB;
			break;
		case 16000:
			op->cap_id = CAPABILITY_ID_CVCHF1MIC_SEND_WB;
			break;
		case 24000:
			op->cap_id = CAPABILITY_ID_CVCHF1MIC_SEND_UWB;
			break;
		default:
			pr_err("KASOBJ(%s): Unsupported sample rate(%d) for 1Mic!\n",
				op->obj.name, param->rate);
			return -EINVAL;
		}

	}

	return 0;
}

/* Called after the operator is created */
static int cvc_send_create(struct kasobj_op *op,
	const struct kasobj_param *param)
{
	struct cvc_send_ctx *ctx = op->context;
	int ret;

	/* Reset to default when HF call started */
	ctx->mode = CVC_SEND_DEFAULT_MODE;
	ctx->ucid = CVC_SEND_DEFAULT_UCID;

	ret = set_cvc_send_ucid(op);
	if (ret)
		return ret;

	ret = set_cvc_send_mode(op);

	return ret;
}

static const struct kasop_impl cvc_send_impl = {
	.init = cvc_send_init,
	.prepare = cvc_send_prepare,
	.create = cvc_send_create,
};

/* registe cvc send operator */
static int __init kasop_init_cvc_send(void)
{
	return kcm_register_cap(CAPABILITY_ID_CVCHF_SEND_DUMMY, &cvc_send_impl);
}

subsys_initcall(kasop_init_cvc_send);
