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

#define MIN_DB		(-120)
#define STEP_DB		1
#define MAXV		(-MIN_DB / STEP_DB)
#define CHANNEL_MAX	8

static const DECLARE_TLV_DB_SCALE(vol_tlv, MIN_DB*100, STEP_DB*100, 0);
static const int chmixer_gain_max[CHANNEL_MAX] =
		/* -20 * log(input_channels) */
		{ 0, -7, -10, -13, -14, -16, -17, -19 };

struct chmixer_ctx {
	int input_ch;
	int output_ch;
	int gain_max;
	int *gain; /* gain[in_idx][out_idx]: the weighting of each route */
};

struct chmixer_msg {
	u16 in_ch;
	u16 out_ch;

	/* 64 is the maximum number of gain
	 * sequence of gain: gain00, gain01, gain02, gain03,
	 *		     gain10, gain11, gain12, gain13
	 */
	u16 gain[64];
};

static int set_chmixer_param(struct kasobj_op *op)
{
	struct chmixer_ctx *ctx = op->context;
	struct chmixer_msg msg;
	int ret, gain_num, idx, msg_len;

	if (!op->obj.life_cnt)
		return 0;

	msg.in_ch = ctx->input_ch;
	msg.out_ch = ctx->output_ch;
	gain_num = ctx->input_ch * ctx->output_ch;
	msg_len = gain_num + 2;
	for (idx = 0; idx < gain_num; idx++)
		msg.gain[idx] = ctx->gain[idx] * 60;

	ret = kalimba_operator_message(op->op_id, CHANNEL_MIXER_SET_PARAMETERS,
		msg_len, (u16 *)&msg, NULL, NULL, __kcm_resp);
	if (ret) {
		pr_err("KASOBJ(%s): set channel mixer parameters failed(%d)!\n",
			op->obj.name, ret);
		return ret;
	}

	return 0;
}

static int chmixer_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ctl_idx, ctrl_num;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &ctl_idx);
	struct chmixer_ctx *ctx = op->context;

	ctrl_num = ctx->input_ch * ctx->output_ch;
	BUG_ON(ctl_idx < 0 || ctl_idx >= ctrl_num);

	ucontrol->value.integer.value[0] =
		ctx->gain[ctl_idx] + MAXV - ctx->gain_max;

	return 0;
}

static int chmixer_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ctl_idx, ctrl_num;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &ctl_idx);
	struct chmixer_ctx *ctx = op->context;
	u16 value = ucontrol->value.integer.value[0];

	ctrl_num = ctx->input_ch * ctx->output_ch;
	BUG_ON(ctl_idx < 0 || ctl_idx >= ctrl_num);

	if (value > MAXV) {
		pr_err("KASOP(%s): channel mixer put, invalid gain value !\n",
			op->obj.name);
		return -EINVAL;
	}
	ctx->gain[ctl_idx] = value - MAXV + ctx->gain_max;

	return 0;
}

/* Create control interfaces */
static int chmixer_init(struct kasobj_op *op)
{
	struct chmixer_ctx *ctx = kzalloc(
		sizeof(struct chmixer_ctx), GFP_KERNEL);
	struct snd_kcontrol_new *ctrl;
	char names_buf[256], name_tmp[52], *names = names_buf, *name;
	int ctrl_idx = 0; /* control interface index */
	int io_num, gain_num, in, out, len, idx, max;

	op->context = ctx;
	/* 0x00XY -> X: input channel num, Y: output channel num */
	io_num = op->db->param.chmixer_io;
	ctx->output_ch = io_num & 0x000f;
	ctx->input_ch = (io_num >> 4) & 0x000f;
	if (ctx->input_ch < 1 || ctx->output_ch < 1) {
		pr_err("KASOP(%s): invalid input(%d)/output(%d) channels !\n",
			op->obj.name, ctx->input_ch, ctx->output_ch);
		return -EINVAL;
	}
	gain_num = ctx->input_ch * ctx->output_ch;
	ctx->gain = kcalloc(gain_num, sizeof(int), GFP_KERNEL);
	ctx->gain_max = chmixer_gain_max[ctx->input_ch - 1];
	max = MAXV;
	for (idx = 0; idx < gain_num; idx++)
		ctx->gain[idx] = ctx->gain_max;

	if (!op->db->ctrl_names.s)
		return 0;

	if (snprintf(names_buf, 256, "%s", op->db->ctrl_names.s) >= 256) {
		pr_err("KASOP(%s): control names too long!\n", op->obj.name);
		return -EINVAL;
	}

	while ((name = strsep(&names, ":;"))) {
		if (ctrl_idx >= gain_num) {
			pr_err("KASOP(%s): too many controls!\n",
				op->obj.name);
			return -EINVAL;
		}
		if (kcm_strcasestr(name, "Gain")) {
			len = snprintf(name_tmp, 50, "%s", name);
			if (len >= 50) {
				pr_err("KASOP(%s): single ctrl too long!\n",
					op->obj.name);
				return -EINVAL;
			}
			for (out = 0; out < ctx->output_ch; out++) {
				for (in = 0; in < ctx->input_ch; in++) {
					name_tmp[len] = '0' + out;
					name_tmp[len + 1] = '0' + in;
					name_tmp[len + 2] = '\0';
					ctrl = kasop_ctrl_single_ext_tlv(
						name_tmp, op, max, chmixer_get,
						chmixer_put, vol_tlv, ctrl_idx);
					kcm_register_ctrl(ctrl);
					ctrl_idx++;
				}
			}
		} else {
			pr_err("KASOP(%s): unknown control '%s'!\n",
				op->obj.name, name);
			return -EINVAL;
		}
	}

	return 0;
}

/* Called after the operator is created */
static int chmixer_create(struct kasobj_op *op,
	const struct kasobj_param *param)
{
	return set_chmixer_param(op);
}

static const struct kasop_impl chmixer_impl = {
	.init = chmixer_init,
	.create = chmixer_create,
};

/* registe channel mixer operator */
static int __init kasop_init_chmixer(void)
{
	return kcm_register_cap(CAPABILITY_ID_CHANNEL_MIXER, &chmixer_impl);
}

subsys_initcall(kasop_init_chmixer);
