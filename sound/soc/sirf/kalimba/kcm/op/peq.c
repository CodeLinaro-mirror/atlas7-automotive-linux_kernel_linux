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

/*
 * The squence number of peq controls
 * defined in db-default/op.c
 * control names	   control index
 * band 1~10 gain	   0 ~ 9
 * band 1~10 FC		   10~ 19
 * band num		   20
 * core type		   21
 * master gain		   22
 * switch mode		   23
 * ucid			   24
 */

#define PEQ_CNTL_BAND1_GAIN 0
#define PEQ_CNTL_BAND10_GAIN 9
#define PEQ_CNTL_BAND1_FC 10
#define PEQ_CNTL_BAND10_FC 19
#define PEQ_CNTL_BANDS_NUM 20
#define PEQ_CNTL_CORE_TYPE 21
#define PEQ_CNTL_MASTER_GAIN 22
#define PEQ_CNTL_SWITCH_MODE 23
#define PEQ_CNTL_UCID 24

#define PEQ_CONTROL_NUM 25
#define PEQ_DEFAULT_MSG_LEN 69
#define PEQ_MIN_DB (-60)
#define PEQ_MAX_DB 20
#define PEQ_STEP_DB 1
#define PEQ_MAX_GAIN (PEQ_MAX_DB - PEQ_MIN_DB)
#define PEQ_BANDS 10

#define PEQ_DEFAULT_UCID 0x01	/* default peq UCID */
#define PEQ_CUST_UCID_MAX 0x0a
#define PEQ_MAX_UCID PEQ_CUST_UCID_MAX

static const DECLARE_TLV_DB_SCALE(peq_db_tlv,
	PEQ_MIN_DB*100, PEQ_STEP_DB*100, 0);

/* The default FC for each band */
static const int peq_default_fc[] = {
	32,   64,   125,  250,	500,
	1000, 2000, 4000, 8000, 16000
};

struct peq_ctx {
	int switch_mode;
	int core_type;
	int bands_num;
	int master_gain;
	int ucid; /* 0x00: default ucid, 0x01~0x09: tier1 predefined ucid */
	int band_fc[PEQ_BANDS];
	int band_gain[PEQ_BANDS];
};

struct peq_mode_msg {
	u16 block;
	u16 ctrl_id;
	u16 value_h;
	u16 value_l;
};

struct peq_param_msg {
	u16 block;
	u16 offset;
	u16 param_num;
	u16 value_h;
	u16 value_l;
	u16 pad;
};

static int set_peq_params(struct kasobj_op *op, int ctl_idx)
{
	struct peq_ctx *ctx = op->context;
	int ret, offset, value;
	struct peq_param_msg msg = {
		.block = 1,
		.param_num = 1,
		.pad = 0,
	};

	/* IPC only if operator is instantiated */
	if (!op->obj.life_cnt)
		return 0;

	switch (ctl_idx) {
	case PEQ_CNTL_BANDS_NUM:
		value = ctx->bands_num;
		offset = 2;
		break;
	case PEQ_CNTL_CORE_TYPE:
		value = ctx->core_type;
		offset = 1;
		break;
	case PEQ_CNTL_MASTER_GAIN:
		value = ctx->master_gain;
		offset = 3;
		break;
	default:
		if (ctl_idx >= PEQ_CNTL_BAND1_GAIN &&
			ctl_idx <= PEQ_CNTL_BAND10_GAIN) {
			value = ctx->band_gain[ctl_idx];
			offset = 6 + ctl_idx * 4;
		} else if (ctl_idx >= PEQ_CNTL_BAND1_FC &&
			ctl_idx <= PEQ_CNTL_BAND10_FC) {
			value = ctx->band_fc[ctl_idx - 10];
			offset = 5 + (ctl_idx - 10) * 4;
		} else
			pr_err("KASOP(%s): peq set parameter exception !\n",
				 op->obj.name);
	}
	msg.offset = (u16)(offset & 0x0000ffff);
	msg.value_h = (u16)((value >> 8) & 0x0000ffff);
	msg.value_l = (u16)((value & 0x000000ff) << 8);

	ret = kalimba_operator_message(op->op_id, OPMSG_COMMON_SET_PARAMS,
		6, (u16 *)&msg, NULL, NULL, __kcm_resp);
	if (ret) {
		pr_err("KASOBJ(%s): set parametor failed(%d)!\n",
			op->obj.name, ret);
		return ret;
	}

	return 0;
}

static int set_peq_mode(struct kasobj_op *op)
{
	struct peq_ctx *ctx = op->context;
	int ret;
	struct peq_mode_msg msg = {
		.block = 1,
		.ctrl_id = 1,
		.value_h = 0,
	};

	if (!op->obj.life_cnt)
		return 0;

	msg.value_l = ctx->switch_mode + 1;	/* 0~2 -> 1~3 */
	ret = kalimba_operator_message(op->op_id, OPMSG_COMMON_SET_CONTROL,
		4, (u16 *)&msg, NULL, NULL, __kcm_resp);
	if (ret)
		pr_err("KASOBJ(%s): set PEQ mode failed(%d)!\n",
			op->obj.name, ret);

	return 0;
}

static int set_peq_ucid(struct kasobj_op *op)
{
	struct peq_ctx *ctx = op->context;
	u16 ucid;
	int ret;

	if (!op->obj.life_cnt)
		return 0;

	ucid = ctx->ucid;
	if (ucid < PEQ_DEFAULT_UCID || ucid > PEQ_CUST_UCID_MAX) {
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

static int peq_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ctl_idx, value;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &ctl_idx);
	struct peq_ctx *ctx = op->context;

	BUG_ON(ctl_idx < 0 || ctl_idx >= PEQ_CONTROL_NUM);

	switch (ctl_idx) {
	case PEQ_CNTL_BANDS_NUM:
		value = ctx->bands_num;
		break;
	case PEQ_CNTL_CORE_TYPE:
		value = ctx->core_type;
		break;
	case PEQ_CNTL_MASTER_GAIN:
		value = ctx->master_gain >> 12;
		value += 60;
		break;
	case PEQ_CNTL_SWITCH_MODE:
		value = ctx->switch_mode;
		break;
	case PEQ_CNTL_UCID:
		value = ctx->ucid;
		break;
	default: {
		if (ctl_idx >= PEQ_CNTL_BAND1_GAIN &&
			ctl_idx <= PEQ_CNTL_BAND10_GAIN) {
			value = ctx->band_gain[ctl_idx] >> 12;
			value += 60;
		} else if (ctl_idx >= PEQ_CNTL_BAND1_FC &&
			ctl_idx <= PEQ_CNTL_BAND10_FC)
			value = ctx->band_fc[ctl_idx - 10] >> 4;
		else
			pr_err("KASOP(%s): peq get, invalid control number !\n",
				 op->obj.name);
		}
	}
	ucontrol->value.integer.value[0] = value;

	return 0;
}

static int peq_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ctl_idx, diff = 0, ret;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &ctl_idx);
	struct peq_ctx *ctx = op->context;
	int value = ucontrol->value.integer.value[0];

	BUG_ON(ctl_idx < 0 || ctl_idx >= PEQ_CONTROL_NUM);

	switch (ctl_idx) {
	case PEQ_CNTL_BANDS_NUM:
		if (ctx->bands_num != value) {
			ctx->bands_num = value;
			diff = 1;
		}
		break;
	case PEQ_CNTL_CORE_TYPE:
		if (ctx->core_type != value) {
			ctx->core_type = value;
			diff = 1;
		}
		break;
	case PEQ_CNTL_MASTER_GAIN:
		value -= 60;	/* 0 ~ 80 -> -60 ~ 20 dB */
		value <<= 12;	/* Q24: 12.N */
		if (ctx->master_gain != value) {
			ctx->master_gain = value;
			diff = 1;
		}
		break;
	case PEQ_CNTL_SWITCH_MODE:
		if (ctx->switch_mode != value) {
			ctx->switch_mode = value;
			diff = 1;
		}
		break;
	case PEQ_CNTL_UCID:
		if (ctx->ucid != value) {
			if (value < PEQ_DEFAULT_UCID ||
				value > PEQ_CUST_UCID_MAX)
				return -EINVAL;
			ctx->ucid = value;
			diff = 1;
		}
		break;
	default:
		if (ctl_idx >= PEQ_CNTL_BAND1_GAIN &&
			ctl_idx <= PEQ_CNTL_BAND10_GAIN) {
			value -= 60;	/* 0 ~ 80 -> -60 ~ 20 dB */
			value <<= 12;
			if (ctx->band_gain[ctl_idx] != value) {
				ctx->band_gain[ctl_idx] = value;
				diff = 1;
			}
		} else if (ctl_idx >= PEQ_CNTL_BAND1_FC &&
			ctl_idx <= PEQ_CNTL_BAND10_FC) {
			if (value < 20)	/* FC: 20 ~ 2400 */
				return -EINVAL;
			value <<= 4;	/* Q24: 20.N */
			if (ctx->band_fc[ctl_idx - 10] != value) {
				ctx->band_fc[ctl_idx - 10] = value;
				diff = 1;
			}
		} else {
			pr_err("KASOP(%s): peq put, invalid control number !\n",
				 op->obj.name);
			return -EINVAL;
		}
	}
	kcm_lock();
	if (diff) {
		if (ctl_idx == PEQ_CNTL_SWITCH_MODE)
			ret = set_peq_mode(op);
		else if (ctl_idx == PEQ_CNTL_UCID)
			ret = set_peq_ucid(op);
		else
			ret = set_peq_params(op, ctl_idx);
	}
	kcm_unlock();

	return ret;
}

/* Create control interfaces */
static int peq_init(struct kasobj_op *op)
{
	struct peq_ctx *ctx = kzalloc(sizeof(struct peq_ctx), GFP_KERNEL);
	struct snd_kcontrol_new *ctrl;
	char names_buf[512], *names = names_buf, *name;
	int ctl_idx = 0; /* control interface index */
	int max, idx;
	const int *tlv = NULL;

	if (!op->db->ctrl_names.s)
		return 0;

	if (snprintf(names_buf, 512, "%s", op->db->ctrl_names.s) >= 512) {
		pr_err("KASOP(%s): control names too long!\n", op->obj.name);
		return -EINVAL;
	}

	ctx->switch_mode = 1;
	ctx->core_type = 0;
	ctx->bands_num = 10;
	ctx->master_gain = 0; /* default 0dB */
	ctx->ucid = PEQ_DEFAULT_UCID; /* use default ucid */
	for (idx = 0; idx < PEQ_BANDS; idx++) {
		ctx->band_fc[idx] = peq_default_fc[idx] << 4;
		ctx->band_gain[idx] = 0; /* default 0dB */
	}

	op->context = ctx;
	while ((name = strsep(&names, ":;"))) {
			/* PEQ controls */
		if (ctl_idx >= PEQ_CONTROL_NUM) {
			pr_err("KASOP(%s): too many PEQ controls!\n",
				op->obj.name);
			break;
		}
		if (kcm_strcasestr(name, "Gain")) {
			max = PEQ_MAX_GAIN;
			tlv = peq_db_tlv;
		} else if (kcm_strcasestr(name, "FC"))
			max = 24000;
		else if (kcm_strcasestr(name, "Mode"))
			max = 2;
		else if (kcm_strcasestr(name, "Type"))
			max = 2;
		else if (kcm_strcasestr(name, "Num"))
			max = 10;
		else if (kcm_strcasestr(name, "UCID"))
			max = PEQ_MAX_UCID;
		else {
			pr_err("KASOP(%s): Invalid control !\n", op->obj.name);
			continue;
		}

		if (max <= 0) {
			pr_err("KASOP(%s): invalid control max value, %d!\n",
				op->obj.name, max);
			return -EINVAL;
		}
		ctrl = kasop_ctrl_single_ext_tlv(name, op, max,
			peq_get, peq_put, tlv, ctl_idx);
		kcm_register_ctrl(ctrl);
		ctl_idx++;
	}

	return 0;
}

/* Called after the operator is created */
static int peq_create(struct kasobj_op *op, const struct kasobj_param *param)
{
	u16 sample_rate;
	int ret, idx;

	ret = set_peq_ucid(op);
	if (ret)
		return ret;
	/*
	 * The db->rate has two function:
	 * First, it is to decide which rate value will be used (db->rate
	 * or param->rate), which can be implemented by setting it with
	 * zero or non-zero value.
	 * Second, it is used to config sample rate with a non-zero value.
	 * For operator, there are two position within the audio pipeling:
	 * 1. Ahead of resampler, db->rate should be 0. Rate value from
	 *    app should be send to kalimba, and resampler will convert the
	 *    rate to the rate of codec.
	 * 2. Behind resampler, db->rate should not be 0 and should be
	 *    equal to the rate of codec.
	 */
	if (op->db->rate)
		sample_rate = op->db->rate / 25; /* sample rate / 25 */
	else
		sample_rate = param->rate / 25;
	ret = kalimba_operator_message(op->op_id, OPMSG_COMMON_SET_SAMPLE_RATE,
		1, &sample_rate, NULL, NULL, __kcm_resp);
	if (ret) {
		pr_err("KASOBJ(%s): set sample rate failed(%d)!\n",
			op->obj.name, ret);
		return ret;
	}
	for (idx = 0; idx < PEQ_CNTL_SWITCH_MODE; idx++) {
		ret = set_peq_params(op, idx);
		if (ret)
			return ret;
	}
	ret = set_peq_mode(op);

	return ret;
}

static const struct kasop_impl peq_impl = {
	.init = peq_init,
	.create = peq_create,
};

/* registe Bass operator */
static int __init kasop_init_peq(void)
{
	return kcm_register_cap(CAPABILITY_ID_PEQ, &peq_impl);
}

subsys_initcall(kasop_init_peq);
