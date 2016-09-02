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

#define MAX_BASS_OP_PAIR 12
#define CONTROL_NUM 9
#define PARAM_NUM 7
#define PARAM_LEN 12 /* ((PARAM_NUM + 1) * 3) / 2 */
#define MSG_LEN 15 /* 3 + PARAM_LEN */
#define MIN_DB (-32)
#define STEP_DB 1
#define MAXV (-MIN_DB / STEP_DB)
#define MINV (-MAX_DB / STEP_DB)

#define BASS_DEFAULT_UCID 0x01
#define BASS_CUST_UCID 0x02

/* the squence number of bass controls */
#define BASS_CNTL_EFFECT_STRENGTH 0
#define BASS_CNTL_AMP_LIMIT 1
#define BASS_CNTL_LP_FC 2
#define BASS_CNTL_HP_FC 3
#define BASS_CNTL_HARM_CONTENT 4
#define BASS_CNTL_XOVER_FC 5
#define BASS_CNTL_MIX_BALANCE 6
#define BASS_CNTL_SWITCH_MODE 7
#define BASS_CNTL_UCID 8

static const DECLARE_TLV_DB_SCALE(bass_db_tlv, MIN_DB*100, STEP_DB*100, 0);
static const int param_min[CONTROL_NUM] = {
	0, 0, 50, 30, 0, 40, 0, 0, 1};	/* min value of control value */
static const int param_max[CONTROL_NUM] = {
	/* max value of control value */
	100, 32, 300, 300, 100, 1000, 100, 2, 2};

struct bass_ctx {
	int effect_strength;
	int amp_limit;
	int lp_fc;
	int hp_fc;
	int harm_content;
	int xover_fc;
	int mix_balance;
	int switch_mode;

	int ucid; /* 0x01: default ucid, 0x02: tier 1 predefined ucid */

	int have_control;
	int pair_idx;
};

struct bass_param_msg {
	u16 block;
	u16 offset;
	u16 param_num;
	u16 params[PARAM_LEN];
};

struct bass_mode_msg {
	u16 block;
	u16 ctrl_id;
	u16 value_h;
	u16 value_l;
};

/* the context of operator without controls */
static u16 no_cntl_op_id[MAX_BASS_OP_PAIR];

static void set_bass_default_value(struct bass_ctx *ctx)
{
	ctx->xover_fc = 200 << 4;
	ctx->mix_balance = 50;
	ctx->effect_strength = 50;
	ctx->amp_limit = 32 << 12;
	ctx->lp_fc = 100 << 4;
	ctx->hp_fc = 100 << 4;
	ctx->harm_content = 50;
	ctx->switch_mode = 1; /* default: process */
	ctx->ucid = BASS_DEFAULT_UCID;
}

static int set_bass_ucid(struct kasobj_op *op, int create_op)
{
	struct bass_ctx *ctx = op->context;
	u16 ucid;
	int ret;

	if (!op->obj.life_cnt)
		return 0;

	ucid = ctx->ucid;
	if (ucid != BASS_DEFAULT_UCID && ucid != BASS_CUST_UCID) {
		pr_err("KASOBJ(%s): invalid UCID(0x%x)!\n", op->obj.name, ucid);
		return -EINVAL;
	}
	ret = kalimba_operator_message(op->op_id, OPERATOR_MSG_SET_UCID,
		1, &ucid, NULL, NULL, __kcm_resp);
	if (ret) {
		pr_err("KASOBJ(%s): set UCID failed(%d)!\n", op->obj.name, ret);
		return ret;
	}
	if (!create_op && no_cntl_op_id[ctx->pair_idx]) {
		ret = kalimba_operator_message(no_cntl_op_id[ctx->pair_idx],
			OPERATOR_MSG_SET_UCID, 1, &ucid, NULL, NULL,
			__kcm_resp);
		if (ret) {
			pr_err("KASOBJ(%s): set UCID failed(%d)!\n",
				op->obj.name, ret);
			return ret;
		}
	}

	return 0;
}

static int set_bass_params(struct kasobj_op *op, int create_op)
{
	int *ctx, ret, idx, m_idx;
	struct bass_ctx *ctx_op = op->context;
	struct bass_param_msg msg;
	int tmp;

	/* IPC only if operator is instantiated */
	if (!op->obj.life_cnt)
		return 0;

	msg.block = 1;
	msg.offset = 1;
	msg.param_num = PARAM_NUM;

	/* Every time, send all the parameters to DSP */
	ctx = (int *)(op->context);
	for (idx = 0, m_idx = 0; (idx < CONTROL_NUM) && m_idx < PARAM_LEN;) {
		msg.params[m_idx++] = (u16)((ctx[idx] >> 8) & 0x0000ffff);
		tmp = (ctx[idx++] & 0x000000ff) << 8;
		msg.params[m_idx++] = (u16)(tmp |
			((ctx[idx] & 0x00ff0000) >> 16));
		msg.params[m_idx++] = (u16)(ctx[idx++] & 0x0000ffff);
	}

	ret = kalimba_operator_message(op->op_id, OPMSG_COMMON_SET_PARAMS,
		MSG_LEN, (u16 *)&msg, NULL, NULL, __kcm_resp);
	if (ret) {
		pr_err("KASOBJ(%s): set parametor failed(%d)!\n",
			op->obj.name, ret);
		return ret;
	}

	if (!create_op && no_cntl_op_id[ctx_op->pair_idx]) {
		ret = kalimba_operator_message(no_cntl_op_id[ctx_op->pair_idx],
			OPMSG_COMMON_SET_PARAMS, MSG_LEN, (u16 *)&msg, NULL,
			NULL, __kcm_resp);
		if (ret) {
			pr_err("KASOBJ(%s): set parametor failed(%d)!\n",
				op->obj.name, ret);
			return ret;
		}
	}

	return 0;
}

static int set_bass_mode(struct kasobj_op *op, int create_op)
{
	struct bass_ctx *ctx;
	struct bass_mode_msg msg = {
		.block = 1,
		.ctrl_id = 1,
		.value_h = 0,
		.value_l = 0,
	};
	int ret;

	if (!op->obj.life_cnt)
		return 0;

	ctx = op->context;
	msg.value_l = ctx->switch_mode + 1;	/* 0~2 -> 1~3 */
	ret = kalimba_operator_message(op->op_id, OPMSG_COMMON_SET_CONTROL,
		4, (u16 *)&msg, NULL, NULL, __kcm_resp);
	if (ret) {
		pr_err("KASOBJ(%s): set bass mode failed(%d)!\n",
			op->obj.name, ret);
		return ret;
	}

	if (!create_op && no_cntl_op_id[ctx->pair_idx]) {
		ret = kalimba_operator_message(no_cntl_op_id[ctx->pair_idx],
			OPMSG_COMMON_SET_CONTROL, 4, (u16 *)&msg, NULL, NULL,
			__kcm_resp);
		if (ret) {
			pr_err("KASOBJ(%s): set bass mode failed(%d)!\n",
				op->obj.name, ret);
			return ret;
		}
	}

	return 0;
}

static int bass_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ctl_idx, value;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &ctl_idx);
	int *ctx = (int *)(op->context);

	BUG_ON(ctl_idx < 0 || ctl_idx >= CONTROL_NUM);

	value = ctx[ctl_idx];
	switch (ctl_idx) {
	case BASS_CNTL_XOVER_FC:
	case BASS_CNTL_LP_FC:
	case BASS_CNTL_HP_FC:
		value >>= 4;
		break;
	case BASS_CNTL_AMP_LIMIT:
		value >>= 12;
		value -= MIN_DB;
		break;
	default:
		break;
	}
	ucontrol->value.integer.value[0] = value;

	return 0;
}

static int bass_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ctl_idx;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &ctl_idx);
	int *ctx = (int *)(op->context);
	int value = ucontrol->value.integer.value[0];

	BUG_ON(ctl_idx < 0 || ctl_idx >= CONTROL_NUM);

	if (value < param_min[ctl_idx] || value > param_max[ctl_idx])
		return -EINVAL;

	switch (ctl_idx) {
	case BASS_CNTL_XOVER_FC:
	case BASS_CNTL_LP_FC:
	case BASS_CNTL_HP_FC:
		value <<= 4;	/* Q24: 20.N */
		break;
	case BASS_CNTL_AMP_LIMIT:
		value += MIN_DB;	/* 0 ~ 32 -> -32 ~ 0dB */
		value <<= 12;		/* Q24: 12.N */
		break;
	default:
		break;
	}

	kcm_lock();
	if (value != ctx[ctl_idx]) {
		ctx[ctl_idx] = value;
		if (ctl_idx == BASS_CNTL_SWITCH_MODE)
			set_bass_mode(op, 0);
		else if (ctl_idx == BASS_CNTL_UCID)
			set_bass_ucid(op, 0);
		else
			set_bass_params(op, 0);
	}
	kcm_unlock();

	return 0;
}

/* Create control interfaces */
static int bass_init(struct kasobj_op *op)
{
	struct bass_ctx *ctx = kzalloc(sizeof(struct bass_ctx), GFP_KERNEL);
	struct snd_kcontrol_new *ctrl;
	char names_buf[256], *names = names_buf, *name;
	int ctl_idx = 0; /* control interface index */
	int max;
	const int *tlv;

	op->context = ctx;
	set_bass_default_value(ctx);
	if (!(op->db->param.bass_pair_idx < MAX_BASS_OP_PAIR)) {
		pr_err("KASOP(%s): pair indx is %d, only support %d pair!\n",
			op->obj.name, op->db->param.bass_pair_idx,
			MAX_BASS_OP_PAIR);
		return -EINVAL;
	}
	ctx->pair_idx = op->db->param.bass_pair_idx;
	if (!op->db->ctrl_names.s) {
		ctx->have_control = 0;
		return 0;
	}
	ctx->have_control = 1;

	if (!op->db->ctrl_names.s)
		return 0;

	if (snprintf(names_buf, 256, "%s", op->db->ctrl_names.s) >= 256) {
		pr_err("KASOP(%s): control names too long!\n", op->obj.name);
		return -EINVAL;
	}

	while ((name = strsep(&names, ":;"))) {
		/* Bass+ controls */
		if (ctl_idx >= CONTROL_NUM) {
			pr_err("KASOP(%s): too many bass controls!\n",
				op->obj.name);
			return -EINVAL;
		}
		if (kcm_strcasestr(name, "Limit"))
			tlv = bass_db_tlv;
		else
			tlv = NULL;
		max = param_max[ctl_idx];
		if (max <= 0) {
			pr_err("KASOP(%s): invalid control max value, %d!\n",
				 op->obj.name, max);
			return -EINVAL;
		}
		ctrl = kasop_ctrl_single_ext_tlv(name, op, max,
			bass_get, bass_put, tlv, ctl_idx);
		kcm_register_ctrl(ctrl);
		ctl_idx++;
	}

	return 0;
}

/* Called after the operator is created */
static int bass_create(struct kasobj_op *op,
	const struct kasobj_param *param)
{
	struct bass_ctx *ctx = op->context;
	u16 sample_rate;
	int ret;

	if (!ctx->have_control)
		no_cntl_op_id[ctx->pair_idx] = op->op_id;

	ret = set_bass_ucid(op, 1);
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
	if (op->db->rate > 0 && op->db->rate <= KASOP_MAX_SAMPLE_RATE)
		sample_rate = op->db->rate / 25;
	else if (op->db->rate == 0)
		sample_rate = param->rate / 25; /* sample rate / 25 */
	else {
		pr_err("KASOBJ(%s): Invalid sample rate (%d)!\n",
			op->obj.name, op->db->rate);
		return -EINVAL;
	}
	ret = kalimba_operator_message(op->op_id, OPMSG_COMMON_SET_SAMPLE_RATE,
		1, &sample_rate, NULL, NULL, __kcm_resp);
	if (ret) {
		pr_err("KASOBJ(%s): set sample rate failed(%d)!\n",
			op->obj.name, ret);
		return ret;
	}
	ret = set_bass_params(op, 1);
	if (ret)
		return ret;

	ret = set_bass_mode(op, 1);

	return ret;
}

static const struct kasop_impl bass_impl = {
	.init = bass_init,
	.create = bass_create,
};

/* registe Bass operator */
static int __init kasop_init_bass(void)
{
	return kcm_register_cap(CAPABILITY_ID_DBE_FULLBAND_IN_OUT, &bass_impl);
}

subsys_initcall(kasop_init_bass);
