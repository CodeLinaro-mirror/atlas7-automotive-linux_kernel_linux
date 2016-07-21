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

/* Any case will not use 48k output sample rate for Mic stream */
#define AEC_REF_DEFAULT_OUT_RATE (48000)
#define AEC_REF_MIC_PIN (2) /* The first Mic in pin */

struct aec_ref_ctx {
	int mic_out_rate; /* current value of output sample rate(Mic stream) */
	int sample_rate; /* sample rate from stream parameter */
};

static void set_sample_rate(struct kasobj_op *op)
{
	struct aec_ref_ctx *ctx = op->context;
	u16 sample_rate[2];
	int ret;

	sample_rate[0] = op->db->rate;
	sample_rate[1] = ctx->mic_out_rate; /* NW/WB/UWB CVC: 8/16/24KHz */
	ret = kalimba_operator_message(op->op_id, AEC_REF_SET_SAMPLE_RATES,
		2, sample_rate, NULL, NULL, __kcm_resp);
	if (ret)
		pr_err("KASOBJ(%s): set sample rate failed(%d)!\n",
			op->obj.name, ret);
}

static int aec_ref_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ctrl_idx;

	kasobj_ctrl_get_op(kcontrol, &ctrl_idx);
	BUG_ON(ctrl_idx != 0);
	ucontrol->value.integer.value[0] = kcm_enable_2mic_cvc;

	return 0;
}

static int aec_ref_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ctrl_idx;

	kasobj_ctrl_get_op(kcontrol, &ctrl_idx);
	BUG_ON(ctrl_idx != 0);
	kcm_enable_2mic_cvc = (bool)ucontrol->value.integer.value[0];

	return 0;
}

/* Create control interfaces */
static int aec_ref_init(struct kasobj_op *op)
{
	struct aec_ref_ctx *ctx = kzalloc(
		sizeof(struct aec_ref_ctx), GFP_KERNEL);
	struct snd_kcontrol_new *ctrl;
	char names_buf[256], *names = names_buf, *name;
	int ctrl_idx = 0; /* control interface index */

	ctx->mic_out_rate = AEC_REF_DEFAULT_OUT_RATE;
	op->context = ctx;
	if (!op->db->ctrl_names.s)
		return 0;

	if (snprintf(names_buf, 256, "%s", op->db->ctrl_names.s) >= 256) {
		pr_err("KASOP(%s): control names too long!\n", op->obj.name);
		return -EINVAL;
	}

	while ((name = strsep(&names, ":;"))) {
		if (kcm_strcasestr(name, "Switch")) {
			/* For AEC-Ref, only one ctrl */
			if (ctrl_idx > 0) {
				pr_err("KASOP(%s): too many controls!\n",
					op->obj.name);
				continue;
			}
			ctrl = kasop_ctrl_single_ext_tlv(name, op, 1,
				aec_ref_get, aec_ref_put, NULL, ctrl_idx);
			kcm_register_ctrl(ctrl);
			ctrl_idx++;
		} else {
			pr_err("KASOP(%s): unknown control '%s'!\n",
					op->obj.name, name);
		}
	}

	return 0;
}

/* Called before the operator is created */
static int aec_ref_prepare(struct kasobj_op *op,
	const struct kasobj_param *param)
{
	if (kcm_enable_2mic_cvc)
		op->cap_id = CAPABILITY_ID_AEC_REF_2MIC;
	else
		op->cap_id = CAPABILITY_ID_AEC_REF_1MIC;

	return 0;
}

/* Called after the operator is created */
static int aec_ref_create(struct kasobj_op *op,
	const struct kasobj_param *param)
{
	struct aec_ref_ctx *ctx = op->context;
	u16 aec_ref_ucid = 4; /* stable user case ID */
	int ret;

	if (!op->db->rate)
		pr_err("KASOBJ(%s): sample rate invalid(%d)!\n",
			op->obj.name, op->db->rate);

	ctx->sample_rate = param->rate;
	set_sample_rate(op);
	ret = kalimba_operator_message(op->op_id, OPERATOR_MSG_SET_UCID,
		1, &aec_ref_ucid, NULL, NULL, __kcm_resp);
	if (ret) {
		pr_err("KASOBJ(%s): set UCID failed(%d)!\n", op->obj.name, ret);
		return ret;
	}

	return 0;
}

static int aec_ref_trigger(struct kasobj_op *op, int event)
{
	struct aec_ref_ctx *ctx = op->context;
	const int is_sink = KASOP_GET_PARAM(event);

	if (!is_sink)
		return 0;

	switch (KASOP_GET_EVENT(event)) {
	case kasop_event_start_ep:
		if ((ctx->mic_out_rate == AEC_REF_DEFAULT_OUT_RATE) &&
			(op->active_sink_pins & BIT(AEC_REF_MIC_PIN))) {
			ctx->mic_out_rate = ctx->sample_rate;
			set_sample_rate(op);
		}
		break;
	case kasop_event_stop_ep:
		if (!(op->active_sink_pins & BIT(AEC_REF_MIC_PIN)) &&
			(ctx->mic_out_rate != AEC_REF_DEFAULT_OUT_RATE))
			ctx->mic_out_rate = AEC_REF_DEFAULT_OUT_RATE;
		break;
	default:
		break;
	}

	return 0;
}

static int aec_ref_reconfig(struct kasobj_op *op,
	const struct kasobj_param *param)
{
	struct aec_ref_ctx *ctx = op->context;

	ctx->sample_rate = param->rate;

	return 0;
}

static const struct kasop_impl aec_ref_impl = {
	.init = aec_ref_init,
	.prepare = aec_ref_prepare,
	.create = aec_ref_create,
	.trigger = aec_ref_trigger,
	.reconfig = aec_ref_reconfig,
};

/* registe AEC-Ref operator */
static int __init kasop_init_aec_ref(void)
{
	return kcm_register_cap(CAPABILITY_ID_AEC_REF_DUMMY, &aec_ref_impl);
}

subsys_initcall(kasop_init_aec_ref);
