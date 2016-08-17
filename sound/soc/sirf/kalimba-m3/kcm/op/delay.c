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

#define MAX_CHANNELS 8
#define PARAM_LEN 12 /* (MAX_CHANNELS * 3) / 2 */
#define MSG_LEN 15 /* (3 + (MAX_CHANNELS * 3) / 2) */
#define MIN_SAMPLES 0
#define MAX_SAMPLES 768

struct delay_ctx {
	int samples[MAX_CHANNELS]; /* delay: 0 ~ 768 samples */
	int channels;
};

struct delay_msg {
	u16 block;
	u16 offset;
	u16 channels;
	u16 params[PARAM_LEN];
};

static int set_delay_samples(struct kasobj_op *op)
{
	struct delay_ctx *ctx;
	int ret, idx, m_idx, *sp, tmp;
	struct delay_msg msg = {
		.block = 1,
		.offset = 0,
		.channels = MAX_CHANNELS,
	};

	/* IPC only if operator is instantiated */
	if (!op->obj.life_cnt)
		return 0;

	ctx = op->context;
	sp = ctx->samples;

	/* Every time, set the parameters of all channels */
	for (idx = 0, m_idx = 0; (idx < MAX_CHANNELS) && m_idx < PARAM_LEN;) {
		msg.params[m_idx++] = (u16)((sp[idx] >> 8) & 0x0000ffff);
		tmp = (sp[idx++] & 0x000000ff) << 8;
		msg.params[m_idx++] = (u16)(tmp |
			((sp[idx] & 0x00ff0000) >> 16));
		msg.params[m_idx++] = (u16)(sp[idx++] & 0x0000ffff);
	}

	ret = kalimba_operator_message(op->op_id, OPMSG_COMMON_SET_PARAMS,
		 MSG_LEN, (u16 *)&msg, NULL, NULL, __kcm_resp);
	if (ret) {
		pr_err("KASOBJ(%s): set parametor failed(%d)!\n",
			op->obj.name, ret);
		return ret;
	}

	return ret;
}

static int samples_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int sample_idx;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &sample_idx);
	struct delay_ctx *ctx = op->context;

	BUG_ON(sample_idx < 0 || sample_idx >= ctx->channels);

	ucontrol->value.integer.value[0] = ctx->samples[sample_idx];

	return 0;
}

static int samples_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int sample_idx;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &sample_idx);
	struct delay_ctx *ctx = op->context;
	int samples = ucontrol->value.integer.value[0];

	BUG_ON(sample_idx < 0 || sample_idx >= ctx->channels);

	if (samples < MIN_SAMPLES || samples > MAX_SAMPLES)
		return -EINVAL;

	kcm_lock();
	if (samples != ctx->samples[sample_idx]) {
		ctx->samples[sample_idx] = samples;
		set_delay_samples(op);
	}
	kcm_unlock();

	return 0;
}
/* Create control interfaces */
static int delay_init(struct kasobj_op *op)
{
	struct delay_ctx *ctx = kzalloc(sizeof(struct delay_ctx), GFP_KERNEL);
	struct snd_kcontrol_new *ctrl;
	char names_buf[256], *names = names_buf, *name;
	int sample_idx = 0; /* control interface index */
	int idx;

	op->context = ctx;
	/* Current supported channels */
	ctx->channels = op->db->param.delay_channels;
	if (ctx->channels > MAX_CHANNELS) {
		pr_err("KASOBJ(%s): channel count > %d!\n",
			op->obj.name, MAX_CHANNELS);
		ctx->channels = MAX_CHANNELS;
	}
	/* Set the default parameters */
	for (idx = 0; idx < MAX_CHANNELS; idx++)
		ctx->samples[idx] = MIN_SAMPLES;

	if (!op->db->ctrl_names.s)
		return 0;

	if (snprintf(names_buf, 256, "%s", op->db->ctrl_names.s) >= 256) {
		pr_err("KASOP(%s): control names too long!\n", op->obj.name);
		return -EINVAL;
	}

	while ((name = strsep(&names, ":;"))) {
		if (kcm_strcasestr(name, "Delay")) {
			/* Samples control */
			if (sample_idx >= ctx->channels) {
				pr_err("KASOP(%s): too many Sample controls!\n",
					op->obj.name);
				return -EINVAL;
			}
			ctrl = kasop_ctrl_single_ext_tlv(name, op, MAX_SAMPLES,
				samples_get, samples_put, NULL, sample_idx);
			kcm_register_ctrl(ctrl);
			sample_idx++;
		} else {
			pr_err("KASOP(%s): unknown control '%s'!\n",
				op->obj.name, name);
			return -EINVAL;
		}
	}

	return 0;
}
/* Called after the operator is created */
static int delay_create(struct kasobj_op *op, const struct kasobj_param *param)
{
	return set_delay_samples(op);
}

static const struct kasop_impl delay_impl = {
	.init = delay_init,
	.create = delay_create,
};

/* registe Delay operator */
static int __init kasop_init_delay(void)
{
	return kcm_register_cap(CAPABILITY_ID_DELAY, &delay_impl);
}

subsys_initcall(kasop_init_delay);
