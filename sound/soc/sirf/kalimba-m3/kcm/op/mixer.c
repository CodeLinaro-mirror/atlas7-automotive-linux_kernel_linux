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
#include "utils.h"

#define MIXER_CTRLS_PER_STREAM 9
#define MIXER_MIN_RAMP_SAMPLES 240
#define MIXER_MAX_RAMP_SAMPLES 480000

#define MAX_STREAMS	3
#define MIN_STREAMS	2
#define MAX_CHANNELS	6
#define MAX_CH_3STREAMS	4
#define MAX_CH_2STREAMS	6
#define MIN_DB		(-120)
#define STEP_DB		1
#define MAXV		(-MIN_DB / STEP_DB)
#define DEFV		MAXV   /* May big noise if all streams are 0dB */

static const DECLARE_TLV_DB_SCALE(vol_tlv, MIN_DB*100, STEP_DB*100, 0);

/* Supported pattern upto: 3streams*4channels or 2streams*6channels */
struct mixer_ctx {
	u16 streams;
	u16 channels[MAX_STREAMS];
};

#define NAME_BUF_LEN    768
static int mixer_init(struct kasobj_op *op)
{
	u16 st, ch, ctrl_idx = 0, max, st_config;
	const int *tlv = NULL;
	char names_buf[NAME_BUF_LEN], *names = names_buf, *name;
	struct mixer_ctx *ctx = kzalloc(sizeof(struct mixer_ctx), GFP_KERNEL);
	struct snd_kcontrol_new *ctrl;

	st_config = (u16)op->db->param.mixer_streams;

	/* Analysis the number of stream and channel
	 *   XXX--> channel number of stream3
	 *   ||
	 *   |+---> channel number of stream2
	 *   +----> channel number of stream1
	 * Example: 124 means 1 channel for stream1, 2 channels for stream2,
	 *   4 channels for stream3.
	 */
	if ((st_config & (~0x0fff)) || !(st_config & (~0x000f))) {
		pr_err("KASOP(%s): Too many or too few streams!\n",
			op->obj.name);
		return -EINVAL;
	}
	if (st_config & 0xf00) {
		/* 3 streams */
		ctx->streams = 3;
		max = MAX_CH_3STREAMS;
	} else {
		/* 2 streams */
		ctx->streams = 2;
		max = MAX_CH_2STREAMS;
	}

	for (st = 0; st < ctx->streams; st++) {
		ch = st_config & 0x00f;
		if (ch > max) {
			pr_err("KASOP(%s): Invalid channels of stream(%d)!\n",
				op->obj.name, st);
			return -EINVAL;
		}
		ctx->channels[ctx->streams - st - 1] = ch;
		st_config >>= 4;
	}

	if (op->db->rate == 0) {
		pr_err("KASOP(%s): invalid sample rate!\n", op->obj.name);
		return -EINVAL;
	}

	if (!op->db->ctrl_names.s)
		return 0;

	if (snprintf(names_buf, NAME_BUF_LEN, "%s", op->db->ctrl_names.s)
			>= NAME_BUF_LEN) {
		pr_err("KASOP(%s): control names too long!\n", op->obj.name);
		return -EINVAL;
	}

	st = ch = 0;
	while ((name = strsep(&names, ":;"))) {
		if (ctrl_idx / MIXER_CTRLS_PER_STREAM >= ctx->streams) {
			pr_err("KASOP(%s): too many Mixer controls!\n",
				op->obj.name);
			return -EINVAL;
		}
		if (kcm_strcasestr(name, "NOCTRL")) {
			/* the stream without ctrls */
			ctrl_idx++;
			if (kcm_strcasestr(name, "Vol"))
				st++;
			continue;
		} else if (kcm_strcasestr(name, "Vol")) {
			max = MAXV;
			tlv = vol_tlv;
			if (st >= ctx->streams) {
				pr_err("KASOP(%s): Too many streams '%s'!\n",
					op->obj.name, name);
				return -EINVAL;
			}
			st++;
		} else if (kcm_strcasestr(name, "Gain")) {
			ch = ch % MAX_CHANNELS;
			if (ch < ctx->channels[st]) {
				max = MAXV;
				tlv = vol_tlv;
				ch++;
			} else {
				ctrl_idx++;
				ch++;
				continue;
			}
		} else if (kcm_strcasestr(name, "Mute")) {
			max = 1;
			tlv = NULL;
		} else if (kcm_strcasestr(name, "Ramp")) {
			ctrl = kasop_ctrl_double_ext_tlv(name, op,
				MIXER_MAX_RAMP_SAMPLES,	NULL, ctrl_idx);
			kcm_register_ctrl(ctrl);
			ctrl_idx++;
			continue;
		} else {
			pr_err("KASOP(%s): unknown control '%s'!\n",
					op->obj.name, name);
			return -EINVAL;
		}
		if (max <= 0) {
			pr_err("KASOP(%s): invalid control max value, %d!\n",
				op->obj.name, max);
			return -EINVAL;
		}
		ctrl = kasop_ctrl_single_ext_tlv(name, op, max, tlv, ctrl_idx);
		kcm_register_ctrl(ctrl);
		ctrl_idx++;
	}
	kfree(ctx);
	op->ctrl_value = kcalloc(ctrl_idx, sizeof(int), GFP_KERNEL);
	op->ctrl_flag  = kcalloc(ctrl_idx, sizeof(int), GFP_KERNEL);

	return 0;
}

static struct kasop_impl mixer_impl = {
	.init = mixer_init,
};

static int __init kasop_init_mixer(void)
{
	return kcm_register_cap(CAPABILITY_ID_MIXER, &mixer_impl);
}

subsys_initcall(kasop_init_mixer);
