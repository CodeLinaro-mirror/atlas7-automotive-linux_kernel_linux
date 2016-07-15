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

/* Volume control ID */
#define VOLCTRL_FRONT_LEFT_ID 0x0010
#define VOLCTRL_FRONT_RIGHT_ID 0x0011
#define VOLCTRL_REAR_LEFT_ID 0x0012
#define VOLCTRL_REAR_RIGHT_ID 0x0013
#define VOLCTRL_MASTER_GAIN_ID 0x0021

/* the control sequence */
#define VOLCTRL_CTRL_FRONT_LEFT 0
#define VOLCTRL_CTRL_FRONT_RIGHT 1
#define VOLCTRL_CTRL_REAR_LEFT 2
#define VOLCTRL_CTRL_REAR_RIGHT 3
#define VOLCTRL_CTRL_MASTER_GAIN 4
#define VOLCTRL_CTRL_MASTER_MUTE 5
#define VOLCTRL_CONTROL_NUM 6

#define VOLCTRL_MIN_DB (-120)
#define VOLCTRL_MAX_DB 9
#define VOLCTRL_STEP_DB 1
#define VOLCTRL_MAX_GAIN (VOLCTRL_MAX_DB - VOLCTRL_MIN_DB)


static const DECLARE_TLV_DB_SCALE(volctrl_db_tlv,
	VOLCTRL_MIN_DB*100, VOLCTRL_STEP_DB*100, 0);

struct volctrl_ctx {
	int front_left;
	int front_right;
	int rear_left;
	int rear_right;
	int master_gain;
	int master_mute;
};

struct volctrl_msg {
	u16 block;
	u16 ctrl_id;
	u16 value_h;
	u16 value_l;
};

static int set_volctrl_params(struct kasobj_op *op, int ctl_idx)
{
	struct volctrl_ctx *ctx = op->context;
	int ret;
	u32 volume;
	struct volctrl_msg msg = {
		.block = 1,
	};

	/* IPC only if operator is instantiated */
	if (!op->obj.life_cnt)
		return 0;

	switch (ctl_idx) {
	case VOLCTRL_CTRL_FRONT_LEFT:
		msg.ctrl_id = VOLCTRL_FRONT_LEFT_ID;
		/* 1/60th of one dB resolution,( <dB gain> * 60) */
		volume = ctx->front_left * 60;
		break;
	case VOLCTRL_CTRL_FRONT_RIGHT:
		msg.ctrl_id = VOLCTRL_FRONT_RIGHT_ID;
		volume = ctx->front_right * 60;
		break;
	case VOLCTRL_CTRL_REAR_LEFT:
		msg.ctrl_id = VOLCTRL_REAR_LEFT_ID;
		volume = ctx->rear_left * 60;
		break;
	case VOLCTRL_CTRL_REAR_RIGHT:
		msg.ctrl_id = VOLCTRL_REAR_RIGHT_ID;
		volume = ctx->rear_right * 60;
		break;
	case VOLCTRL_CTRL_MASTER_GAIN:
		msg.ctrl_id = VOLCTRL_MASTER_GAIN_ID;
		volume = ctx->master_gain * 60;
		break;
	case VOLCTRL_CTRL_MASTER_MUTE:
		msg.ctrl_id = VOLCTRL_MASTER_GAIN_ID;
		/* mute equals minamal master gain */
		if (ctx->master_mute)
			volume = VOLCTRL_MIN_DB * 60;
		else
			volume = ctx->master_gain * 60;
		break;
	default:
		pr_err("KASOP(%s): volume control, set parameter exception !\n",
			 op->obj.name);
		return -EINVAL;
	}
	msg.value_h = (u16)(volume >> 16);
	msg.value_l = (u16)(volume & 0xffff);
	ret = kalimba_operator_message(op->op_id,
		OPERATOR_MSG_VOLUME_CTRL_SET_CONTROL,
		4, (u16 *)&msg, NULL, NULL, __kcm_resp);
	if (ret) {
		pr_err("KASOBJ(%s): set parametor failed(%d)!\n",
			op->obj.name, ret);
		return ret;
	}

	return 0;
}

static void set_volctrl_master_gain(struct kasobj_op *op, int vol)
{
	struct volctrl_ctx *ctx = op->context;

	if (vol < -120 || vol > 9) {
		pr_err("KASOP(%s): invalid master gain(%d) !\n",
			 op->obj.name, vol);
		return;
	}

	kcm_lock();
	((int *)ctx)[VOLCTRL_CTRL_MASTER_GAIN] = vol;
	if (!(ctx->master_mute))
		set_volctrl_params(op, VOLCTRL_CTRL_MASTER_GAIN);
	kcm_unlock();
}

void kcm_set_vol_ctrl_gain(int vol)
{
	struct kasobj_op *op;
	int idx = 0;

	while (1) {
		op = kasobj_find_op_by_capid(
			CAPABILITY_ID_VOLUME_CONTROL, idx++);
		if (op)
			set_volctrl_master_gain(op, vol);
		else
			break;
	}
}
EXPORT_SYMBOL(kcm_set_vol_ctrl_gain);

static int volctrl_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ctl_idx, value;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &ctl_idx);
	struct volctrl_ctx *ctx = op->context;

	BUG_ON(ctl_idx < 0 || ctl_idx >= VOLCTRL_CONTROL_NUM);

	if (ctl_idx == VOLCTRL_CTRL_MASTER_MUTE)
		value = ctx->master_mute;
	else
		value = ((int *)ctx)[ctl_idx] + 120; /* 0~129 => -120~9 dB*/

	ucontrol->value.integer.value[0] = value;

	return 0;
}

static int volctrl_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ctl_idx;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &ctl_idx);
	struct volctrl_ctx *ctx = op->context;
	int value = ucontrol->value.integer.value[0];

	BUG_ON(ctl_idx < 0 || ctl_idx >= VOLCTRL_CONTROL_NUM);

	if (ctl_idx != VOLCTRL_CTRL_MASTER_MUTE)
		value -= 120;

	kcm_lock();
	if (((int *)ctx)[ctl_idx] != value) {
		((int *)ctx)[ctl_idx] = value;
		if (!((ctl_idx == VOLCTRL_CTRL_MASTER_GAIN) &&
			ctx->master_mute))
			set_volctrl_params(op, ctl_idx);
	}
	kcm_unlock();

	return 0;
}

/* Create control interfaces */
static int volctrl_init(struct kasobj_op *op)
{
	struct volctrl_ctx *ctx;
	struct snd_kcontrol_new *ctrl;
	char names_buf[256], *names = names_buf, *name;
	int ctl_idx = 0; /* control interface index */
	int max, idx;
	const int *tlv = NULL;

	ctx = kzalloc(sizeof(struct volctrl_ctx), GFP_KERNEL);
	op->context = ctx;
	for (idx = 0; idx < VOLCTRL_CONTROL_NUM - 1; idx++)
		((int *)ctx)[idx] = 0;
	ctx->master_mute = 0;

	if (!op->db->ctrl_names.s)
		return 0;

	if (snprintf(names_buf, 256, "%s", op->db->ctrl_names.s) >= 256) {
		pr_err("KASOP(%s): control names too long!\n", op->obj.name);
		return -EINVAL;
	}

	while ((name = strsep(&names, ":;"))) {
			/* volume controls */
		if (ctl_idx >= VOLCTRL_CONTROL_NUM) {
			pr_err("KASOP(%s): too many volume controls!\n",
				op->obj.name);
			break;
		}
		if (kcm_strcasestr(name, "Mute")) {
			max = 1;
		} else {
			max = VOLCTRL_MAX_GAIN;
			tlv = volctrl_db_tlv;
		}
		if (max <= 0) {
			pr_err("KASOP(%s): invalid control max value, %d!\n",
				op->obj.name, max);
			return -EINVAL;
		}
		ctrl = kasop_ctrl_single_ext_tlv(name, op, max,
			volctrl_get, volctrl_put, tlv, ctl_idx);
		kcm_register_ctrl(ctrl);
		ctl_idx++;
	}

	return 0;
}

/* Called after the operator is created */
static int volctrl_create(struct kasobj_op *op,
	const struct kasobj_param *param)
{
	u16 sample_rate;  /* sample rate / 25 */
	int ret, idx;

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
	for (idx = 0; idx < VOLCTRL_CONTROL_NUM; idx++) {
		ret = set_volctrl_params(op, idx);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct kasop_impl volctrl_impl = {
	.init = volctrl_init,
	.create = volctrl_create,
};

/* registe volume control operator */
static int __init kasop_init_volctrl(void)
{
	return kcm_register_cap(CAPABILITY_ID_VOLUME_CONTROL, &volctrl_impl);
}

subsys_initcall(kasop_init_volctrl);
