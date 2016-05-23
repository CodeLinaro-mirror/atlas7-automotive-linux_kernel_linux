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

#define MIXER_CTRL_GAIN 0
#define MIXER_CTRL_MUTE 1
#define MIXER_CTRL_RAMP 2
#define MIXER_CTRL_FR_L 3
#define MIXER_CTRL_FR_R 4
#define MIXER_CTRL_RER_L 5
#define MIXER_CTRL_RER_R 6

#define MIXER_CTRLS_PER_STREAM 7
#define MIXER_MAX_RAMP_SAMPLES 0x00ffffff

#define MAX_STREAMS	3
#define MAX_CHANNELS 12
#define MIN_DB		(-96)
#define STEP_DB		1
#define MAXV		(-MIN_DB / STEP_DB)
#define DEFV		MAXV   /* May big noise if all streams are 0dB */

static const DECLARE_TLV_DB_SCALE(vol_tlv, MIN_DB*100, STEP_DB*100, 0);
static u16 mixer_default_channel_gain[1 + MAX_CHANNELS * 2] = {
	MAX_CHANNELS, /* total channels */
	0, 0, 1, 0, 2, 0, 3, 0,  /* stream1 channels */
	4, 0, 5, 0, 6, 0, 7, 0,  /* stream2 channels */
	8, 0, 9, 0, 10, 0, 11, 0 /* stream3 channels */
};

struct mixer_ctx {
	int gain[MAX_STREAMS];
	int muted[MAX_STREAMS];
	int ramp[2][MAX_STREAMS];	/* 0: for volume, 1: for mute/unmute */
	int fr_l_gain[MAX_STREAMS];
	int fr_r_gain[MAX_STREAMS];
	int rer_l_gain[MAX_STREAMS];
	int rer_r_gain[MAX_STREAMS];
	int streams;
	u16 primary_stream;	/* Starts from 1 */
};

static void set_stream_gain(struct kasobj_op *op, int samples)
{
	int i;
	struct mixer_ctx *ctx = op->context;
	u16 db[MAX_STREAMS];
	u16 msg_ramp[2];

	if (!op->obj.life_cnt)
		return;

	/* <MS_8bits> <LS_16bits> */
	msg_ramp[0] = samples >> 16;
	msg_ramp[1] = samples & 0xffff;

	for (i = 0; i < ctx->streams; i++) {
		if (ctx->muted[i])
			db[i] = -32768;
		else
			db[i] = (ctx->gain[i] - MAXV) * 60;
	}
	kalimba_operator_message(op->op_id, OPERATOR_MSG_SET_RAMP_NUM_SAMPLES,
			2, msg_ramp, NULL, NULL, __kcm_resp);
	kalimba_operator_message(op->op_id, OPERATOR_MSG_SET_GAINS,
			ctx->streams, db, NULL, NULL, __kcm_resp);
}

static void set_channel_gain(struct kasobj_op *op, int stream,
			int channel, int gain)
{
	struct mixer_ctx *ctx = op->context;
	u16 msg[3] = {1, 0, 0};
	u16 msg_ramp[2];

	/* if muted, do not send ipc msg */
	if (!op->obj.life_cnt || ctx->muted[stream])
		return;

	/* <MS_8bits> <LS_16bits> */
	msg_ramp[0] = ctx->ramp[0][stream] >> 16;
	msg_ramp[1] = ctx->ramp[0][stream] & 0xffff;

	msg[1] = stream * 4 + channel;
	msg[2] = (gain - MAXV) * 60;

	kalimba_operator_message(op->op_id, OPERATOR_MSG_SET_RAMP_NUM_SAMPLES,
			2, msg_ramp, NULL, NULL, __kcm_resp);
	kalimba_operator_message(op->op_id, OPERATOR_MSG_SET_CHANNEL_GAINS,
			3, msg, NULL, NULL, __kcm_resp);
}

static void set_primary_stream(struct kasobj_op *op)
{
	struct mixer_ctx *ctx = op->context;

	if (op->obj.life_cnt)
		kalimba_operator_message(op->op_id,
				OPERATOR_MSG_SET_PRIMARY_STREAM, 1,
				&ctx->primary_stream, NULL, NULL, __kcm_resp);
}

/* Find primary stream (last active stream) */
static int select_primary_stream(struct kasobj_op *op)
{
	int i;
	struct mixer_ctx *ctx = op->context;

	/* Check pin (0,4,8) if 3x4 or (0,6) if 2x6 */
	for (i = ctx->streams - 1; i >= 0; i--)
		if (op->active_sink_pins & BIT(i * 12 / ctx->streams))
			return i + 1;

	return ctx->primary_stream;
}

/* Endpoint activity changed, we may have to pick new primary stream */
static void pin_changed(struct kasobj_op *op, int is_sink)
{
	if (is_sink) {
		/* Pick primary stream based on current input pins activity */
		int primary_stream = select_primary_stream(op);
		struct mixer_ctx *ctx = op->context;

		if (primary_stream != ctx->primary_stream) {
			ctx->primary_stream = primary_stream;
			set_primary_stream(op);
			kcm_debug("KASOP(%s): set primary stream to %d\n",
					op->obj.name, primary_stream);
		}
	}
}

static int mixer_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int stream_idx, ctrl_idx, param_idx, value;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &ctrl_idx);
	struct mixer_ctx *ctx = op->context;

	BUG_ON(ctrl_idx < 0 || ctrl_idx >= ctx->streams *
		MIXER_CTRLS_PER_STREAM);

	stream_idx = ctrl_idx / MIXER_CTRLS_PER_STREAM;
	param_idx = ctrl_idx % MIXER_CTRLS_PER_STREAM;
	switch (param_idx) {
	case MIXER_CTRL_GAIN:
		value = ctx->gain[stream_idx];
		break;
	case MIXER_CTRL_MUTE:
		value = ctx->muted[stream_idx];
		break;
	case MIXER_CTRL_RAMP:
		value = ctx->ramp[0][stream_idx];
		ucontrol->value.integer.value[1] = ctx->ramp[1][stream_idx];
		break;
	case MIXER_CTRL_FR_L:
		value = ctx->fr_l_gain[stream_idx];
		break;
	case MIXER_CTRL_FR_R:
		value = ctx->fr_r_gain[stream_idx];
		break;
	case MIXER_CTRL_RER_L:
		value = ctx->rer_l_gain[stream_idx];
		break;
	case MIXER_CTRL_RER_R:
		value = ctx->rer_r_gain[stream_idx];
		break;
	default:
		pr_err("KASOP(%s): mixer get, invalid control number !\n",
			op->obj.name);
	}
	ucontrol->value.integer.value[0] = value;

	return 0;
}

static int mixer_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int stream_idx, param_idx, ctrl_idx;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &ctrl_idx);
	struct mixer_ctx *ctx = op->context;
	int value = ucontrol->value.integer.value[0];

	BUG_ON(ctrl_idx < 0 || ctrl_idx >= ctx->streams *
		MIXER_CTRLS_PER_STREAM);

	stream_idx = ctrl_idx / MIXER_CTRLS_PER_STREAM;
	param_idx = ctrl_idx % MIXER_CTRLS_PER_STREAM;
	switch (param_idx) {
	case MIXER_CTRL_GAIN:
		if (ctx->gain[stream_idx] != value) {
			ctx->gain[stream_idx] = value;
			/* if muted, just save the vlaue */
			if (!ctx->muted[stream_idx]) {
				kcm_lock();
				set_stream_gain(op, ctx->ramp[0][stream_idx]);
				kcm_unlock();
			}
		}
		break;
	case MIXER_CTRL_MUTE:
		if (ctx->muted[stream_idx] != value) {
			ctx->muted[stream_idx] = value;
			/* when unmute, send all the saved volume vlaue */
			kcm_lock();
			set_channel_gain(op, stream_idx, 0,
				ctx->fr_l_gain[stream_idx]);
			set_channel_gain(op, stream_idx, 1,
				ctx->fr_r_gain[stream_idx]);
			set_channel_gain(op, stream_idx, 2,
				ctx->rer_l_gain[stream_idx]);
			set_channel_gain(op, stream_idx, 3,
				ctx->rer_r_gain[stream_idx]);
			set_stream_gain(op, ctx->ramp[1][stream_idx]);
			kcm_unlock();
		}
		break;
	case MIXER_CTRL_RAMP:
		if (ctx->ramp[0][stream_idx] != value ||
			ctx->ramp[1][stream_idx] !=
				ucontrol->value.integer.value[1])
			ctx->ramp[0][stream_idx] = value;
			ctx->ramp[1][stream_idx] =
				ucontrol->value.integer.value[1];
		break;
	case MIXER_CTRL_FR_L:
		if (ctx->fr_l_gain[stream_idx] != value) {
			ctx->fr_l_gain[stream_idx] = value;
			kcm_lock();
			set_channel_gain(op, stream_idx, 0, value);
			kcm_unlock();
		}
		break;
	case MIXER_CTRL_FR_R:
		if (ctx->fr_r_gain[stream_idx] != value) {
			ctx->fr_r_gain[stream_idx] = value;
			kcm_lock();
			set_channel_gain(op, stream_idx, 1, value);
			kcm_unlock();
		}
		break;
	case MIXER_CTRL_RER_L:
		if (ctx->rer_l_gain[stream_idx] != value) {
			ctx->rer_l_gain[stream_idx] = value;
			kcm_lock();
			set_channel_gain(op, stream_idx, 2, value);
			kcm_unlock();
		}
		break;
	case MIXER_CTRL_RER_R:
		if (ctx->rer_r_gain[stream_idx] != value) {
			ctx->rer_r_gain[stream_idx] = value;
			kcm_lock();
			set_channel_gain(op, stream_idx, 3, value);
			kcm_unlock();
		}
		break;
	default:
		pr_err("KASOP(%s): mixer put, invalid control number !\n",
			op->obj.name);
	}

	return 0;
}

static int mixer_init(struct kasobj_op *op)
{
	int i, ctrl_idx = 0, max;
	const int *tlv = NULL;
	char names_buf[1024], *names = names_buf, *name;
	struct mixer_ctx *ctx = kzalloc(sizeof(struct mixer_ctx), GFP_KERNEL);
	struct snd_kcontrol_new *ctrl;

	op->context = ctx;
	ctx->streams = op->db->param.mixer_streams;

	if (ctx->streams > MAX_STREAMS) {
		pr_err("KASOBJ(%s): stream count > %d!\n", op->obj.name,
			MAX_STREAMS);
		ctx->streams = MAX_STREAMS;
	}
	for (i = 0; i < MAX_STREAMS; i++) {
		ctx->gain[i] = DEFV;
		ctx->muted[i] = 0;
		ctx->ramp[0][i] = 96000;
		ctx->ramp[1][i] = 240;
		ctx->fr_l_gain[i] = DEFV;
		ctx->fr_r_gain[i] = DEFV;
		ctx->rer_l_gain[i] = DEFV;
		ctx->rer_r_gain[i] = DEFV;
	}
	if (op->db->rate == 0) {
		pr_err("KASOP(%s): invalid sample rate!\n", op->obj.name);
		return -EINVAL;
	}

	if (!op->db->ctrl_names.s)
		return 0;

	if (snprintf(names_buf, 1024, "%s", op->db->ctrl_names.s) >= 1024) {
		pr_err("KASOP(%s): control names too long!\n", op->obj.name);
		return -EINVAL;
	}

	while ((name = strsep(&names, ":;"))) {
		if (ctrl_idx / MIXER_CTRLS_PER_STREAM >= ctx->streams) {
			pr_err("KASOP(%s): too many Mixer controls!\n",
				op->obj.name);
			break;
		}
		if (kcm_strcasestr(name, "NOCTRL")) {
			/* the stream without ctrls */
			ctrl_idx++;
			continue;
		} else if (kcm_strcasestr(name, "Volume") ||
			kcm_strcasestr(name, "Left") ||
			kcm_strcasestr(name, "Right")) {
			max = MAXV;
			tlv = vol_tlv;
		} else if (kcm_strcasestr(name, "Mute")) {
			max = 1;
			tlv = NULL;
		} else if (kcm_strcasestr(name, "Ramp")) {
			ctrl = kasop_ctrl_double_ext_tlv(name, op,
				MIXER_MAX_RAMP_SAMPLES,	mixer_get, mixer_put,
				NULL, ctrl_idx);
			kcm_register_ctrl(ctrl);
			ctrl_idx++;
			continue;
		} else {
			pr_err("KASOP(%s): unknown control '%s'!\n",
					op->obj.name, name);
		}
		if (max <= 0) {
			pr_err("KASOP(%s): invalid control max value, %d!\n",
				op->obj.name, max);
			return -EINVAL;
		}
		ctrl = kasop_ctrl_single_ext_tlv(name, op, max,
			mixer_get, mixer_put, tlv, ctrl_idx);
		kcm_register_ctrl(ctrl);
		ctrl_idx++;
	}

	return 0;
}

static int mixer_create(struct kasobj_op *op, const struct kasobj_param *param)
{
	struct mixer_ctx *ctx = op->context;
	static short stream3x4_cfg[] = { 4, 4, 4 };
	static short stream2x6_cfg[] = { 6, 6 };
	short *stream_cfg;
	short rate = op->db->rate / 25;

	if (ctx->streams == 2) {
		stream_cfg = stream2x6_cfg;
	} else {
		stream_cfg = stream3x4_cfg;
		if (ctx->streams != 3)
			pr_err("KASOBJ(%s): unsupported streams %d!\n",
					op->obj.name, ctx->streams);
	}
	kalimba_operator_message(op->op_id, OPERATOR_MSG_SET_CHANNELS,
			ctx->streams, stream_cfg, NULL, NULL, __kcm_resp);

	kalimba_operator_message(op->op_id, OPMSG_COMMON_SET_SAMPLE_RATE,
			1, &rate, NULL, NULL, __kcm_resp);
	kalimba_operator_message(op->op_id, OPERATOR_MSG_SET_CHANNEL_GAINS,
			(1 + MAX_CHANNELS * 2), mixer_default_channel_gain,
			NULL, NULL, __kcm_resp);
	set_stream_gain(op, 96000);

	ctx->primary_stream = 1;
	set_primary_stream(op);

	return 0;
}

static int mixer_trigger(struct kasobj_op *op, int event)
{
	const int param = KASOP_GET_PARAM(event);

	switch (KASOP_GET_EVENT(event)) {
	case kasop_event_start_ep:
	case kasop_event_stop_ep:
		pin_changed(op, param);
		break;
	default:
		break;
	}

	return 0;
}

static struct kasop_impl mixer_impl = {
	.init = mixer_init,
	.create = mixer_create,
	.trigger = mixer_trigger,
};

static int __init kasop_init_mixer(void)
{
	return kcm_register_cap(CAPABILITY_ID_MIXER, &mixer_impl);
}

subsys_initcall(kasop_init_mixer);
