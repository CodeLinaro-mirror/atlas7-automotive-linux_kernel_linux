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

#define CVC_RECV_DEFAULT_MODE (1)
#define CVC_RECV_DEFAULT_UCID (0x01)
#define CVC_RECV_CUST_UCID (0x02)

#define CVC_RECV_CTRL_NUM (2)
#define CVC_RECV_CTRL_MODE_IDX (0)
#define CVC_RECV_CTRL_UCID_IDX (1)

#define CVC_RECV_MODE_MAX (2)
#define CVC_RECV_UCID_MAX (2)

struct cvc_recv_ctx {
	u16 mode; /* 0: mute, 1: process, 2: passthrough */
	u16 ucid; /* 0x01: default UCID, 0x02: tier 1 predefined UCID */
};

struct cvc_recv_mode_msg {
	u16 block;
	u16 ctrl_id;
	u16 value_h;
	u16 value_l;
};

static int set_cvc_recv_ucid(struct kasobj_op *op)
{
	struct cvc_recv_ctx *ctx = op->context;
	u16 ucid;
	int ret;

	if (!op->obj.life_cnt)
		return 0;

	ucid = ctx->ucid;
	if (ucid != CVC_RECV_DEFAULT_UCID && ucid != CVC_RECV_CUST_UCID) {
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

static int set_cvc_recv_mode(struct kasobj_op *op)
{
	struct cvc_recv_ctx *ctx = op->context;
	struct cvc_recv_mode_msg msg = {
		.block = 1,
		.ctrl_id = 1,
		.value_h = 0,
	};
	int ret;

	if (!op->obj.life_cnt)
		return 0;

	msg.value_l = ctx->mode + 1; /* 0~2 -> 1~3 */
	ret = kalimba_operator_message(op->op_id, OPMSG_COMMON_SET_CONTROL,
		4, (u16 *)&msg, NULL, NULL, __kcm_resp);
	if (ret) {
		pr_err("KASOBJ(%s): Set CVC Recv mode failed(%d)!\n",
			op->obj.name, ret);
		return ret;
	}

	return 0;
}

static int cvc_recv_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ctl_idx, value;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &ctl_idx);
	struct cvc_recv_ctx *ctx = op->context;

	BUG_ON(ctl_idx < 0 || ctl_idx >= CVC_RECV_CTRL_NUM);

	switch (ctl_idx) {
	case CVC_RECV_CTRL_MODE_IDX:
		value = ctx->mode;
		break;
	case CVC_RECV_CTRL_UCID_IDX:
		value = ctx->ucid;
		break;
	default:
		pr_err("KASOP(%s): CVC Recv get, invalid control number !\n",
			op->obj.name);
		return -EINVAL;
	}
	ucontrol->value.integer.value[0] = value;

	return 0;
}

static int cvc_recv_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ctl_idx;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &ctl_idx);
	struct cvc_recv_ctx *ctx = op->context;
	u16 value = ucontrol->value.integer.value[0];

	BUG_ON(ctl_idx < 0 || ctl_idx >= CVC_RECV_CTRL_NUM);

	switch (ctl_idx) {
	case CVC_RECV_CTRL_MODE_IDX:
		if (value != ctx->mode) {
			kcm_lock();
			ctx->mode = value;
			set_cvc_recv_mode(op);
			kcm_unlock();
		}
		break;
	case CVC_RECV_CTRL_UCID_IDX:
		if (value != ctx->ucid) {
			/* UCID: 1 ~ 2 */
			if (value != CVC_RECV_DEFAULT_UCID &&
				value != CVC_RECV_CUST_UCID)
				return -EINVAL;
			kcm_lock();
			ctx->ucid = value;
			set_cvc_recv_ucid(op);
			kcm_unlock();
		}
		break;
	default:
		pr_err("KASOP(%s): CVC Recv put, invalid control number !\n",
			op->obj.name);
		return -EINVAL;
	}

	return 0;
}

/* Create control interfaces */
static int cvc_recv_init(struct kasobj_op *op)
{
	struct cvc_recv_ctx *ctx = kzalloc(
		sizeof(struct cvc_recv_ctx), GFP_KERNEL);
	struct snd_kcontrol_new *ctrl;
	char names_buf[256], *names = names_buf, *name;
	int ctrl_idx = 0; /* control interface index */
	int max;

	ctx->mode = CVC_RECV_DEFAULT_MODE;
	ctx->ucid = CVC_RECV_DEFAULT_UCID;
	op->context = ctx;
	if (!op->db->ctrl_names.s)
		return 0;

	if (snprintf(names_buf, 256, "%s", op->db->ctrl_names.s) >= 256) {
		pr_err("KASOP(%s): control names too long!\n", op->obj.name);
		return -EINVAL;
	}

	while ((name = strsep(&names, ":;"))) {
		if (ctrl_idx >= CVC_RECV_CTRL_NUM) {
			pr_err("KASOP(%s): too many controls!\n",
				op->obj.name);
			return -EINVAL;
		}
		if (kcm_strcasestr(name, "Mode"))
			max = CVC_RECV_MODE_MAX;
		else if (kcm_strcasestr(name, "UCID"))
			max = CVC_RECV_UCID_MAX;
		else {
			pr_err("KASOP(%s): unknown control '%s'!\n",
				op->obj.name, name);
			return -EINVAL;
		}

		ctrl = kasop_ctrl_single_ext_tlv(name, op, max,
			cvc_recv_get, cvc_recv_put, NULL, ctrl_idx);
		kcm_register_ctrl(ctrl);
		ctrl_idx++;
	}

	return 0;
}

/* Called before the operator is created */
static int cvc_recv_prepare(struct kasobj_op *op,
	const struct kasobj_param *param)
{
	switch (param->rate) {
	case 8000:
		op->cap_id = CAPABILITY_ID_CVC_RCV_NB;
		break;
	case 16000:
		op->cap_id = CAPABILITY_ID_CVC_RCV_WB;
		break;
	case 24000:
		op->cap_id = CAPABILITY_ID_CVC_RCV_UWB;
		break;
	default:
		pr_err("KASOBJ(%s): Unsupported sample rate(%d) !\n",
			op->obj.name, param->rate);
		return -EINVAL;
	}

	return 0;
}

/* Called after the operator is created */
static int cvc_recv_create(struct kasobj_op *op,
	const struct kasobj_param *param)
{
	struct cvc_recv_ctx *ctx = op->context;
	int ret;

	/* Reset to default when HF call started */
	ctx->mode = CVC_RECV_DEFAULT_MODE;
	ctx->ucid = CVC_RECV_DEFAULT_UCID;

	ret = set_cvc_recv_ucid(op);
	if (ret)
		return ret;

	ret = set_cvc_recv_mode(op);

	return ret;
}

static const struct kasop_impl cvc_recv_impl = {
	.init = cvc_recv_init,
	.prepare = cvc_recv_prepare,
	.create = cvc_recv_create,
};

/* registe cvc recv operator */
static int __init kasop_init_cvc_recv(void)
{
	return kcm_register_cap(CAPABILITY_ID_CVC_RCV_DUMMY, &cvc_recv_impl);
}

subsys_initcall(kasop_init_cvc_recv);
