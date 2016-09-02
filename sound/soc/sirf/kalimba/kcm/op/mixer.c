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

#define MIXER_CTRL_CH1	0
#define MIXER_CTRL_CH2	1
#define MIXER_CTRL_CH3	2
#define MIXER_CTRL_CH4	3
#define MIXER_CTRL_CH5	4
#define MIXER_CTRL_CH6	5
#define MIXER_CTRL_GAIN 6
#define MIXER_CTRL_MUTE 7
#define MIXER_CTRL_RAMP 8

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
	int gain[MAX_STREAMS];
	int muted[MAX_STREAMS];
	int ramp[2][MAX_STREAMS];	/* 0: for volume, 1: for mute/unmute */
	int ch_gain[MAX_STREAMS][MAX_CHANNELS];
	u16 streams;
	u16 channels[MAX_STREAMS];
	u16 primary_stream;	/* Starts from 1 */
};

static int set_stream_gain(struct kasobj_op *op, int samples)
{
	int i, ret;
	struct mixer_ctx *ctx = op->context;
	u16 db[MAX_STREAMS];
	u16 msg_ramp[2];

	if (!op->obj.life_cnt)
		return 0;

	/* <MS_8bits> <LS_16bits> */
	msg_ramp[0] = samples >> 16;
	msg_ramp[1] = samples & 0xffff;

	for (i = 0; i < ctx->streams; i++) {
		if (ctx->muted[i])
			db[i] = -32768;
		else
			db[i] = (ctx->gain[i] - MAXV) * 60;
	}
	ret = kalimba_operator_message(op->op_id,
			OPERATOR_MSG_SET_RAMP_NUM_SAMPLES,
			2, msg_ramp, NULL, NULL, __kcm_resp);
	if (ret) {
		pr_err("KASOP(%s): set stream ramp failed !\n",
			op->obj.name);
		return ret;
	}
	ret = kalimba_operator_message(op->op_id, OPERATOR_MSG_SET_GAINS,
			ctx->streams, db, NULL, NULL, __kcm_resp);
	if (ret) {
		pr_err("KASOP(%s): set stream gain failed !\n",
			op->obj.name);
		return ret;
	}

	return 0;
}

static int set_channel_gain(struct kasobj_op *op, int stream,
			int channel, int gain)
{
	struct mixer_ctx *ctx = op->context;
	u16 msg[3] = {1, 0, 0};
	u16 msg_ramp[2];
	int idx, ret;

	/* if muted, do not send ipc msg */
	if (!op->obj.life_cnt || ctx->muted[stream])
		return 0;

	/* <MS_8bits> <LS_16bits> */
	msg_ramp[0] = ctx->ramp[0][stream] >> 16;
	msg_ramp[1] = ctx->ramp[0][stream] & 0xffff;

	for (idx = 0; idx < stream; idx++)
		msg[1] += ctx->channels[idx];
	msg[1] += channel;
	msg[2] = (gain - MAXV) * 60;

	ret = kalimba_operator_message(op->op_id,
		OPERATOR_MSG_SET_RAMP_NUM_SAMPLES,
		2, msg_ramp, NULL, NULL, __kcm_resp);
	if (ret) {
		pr_err("KASOP(%s): set channel ramp failed !\n",
			op->obj.name);
		return ret;
	}
	ret = kalimba_operator_message(op->op_id,
		OPERATOR_MSG_SET_CHANNEL_GAINS,
		3, msg, NULL, NULL, __kcm_resp);
	if (ret) {
		pr_err("KASOP(%s): set channel gain failed !\n",
			op->obj.name);
		return ret;
	}

	return 0;
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
	int i, ch;
	struct mixer_ctx *ctx = op->context;

	/*
	 * Check first channel pin of each stream
	 * eg. 3streams, 4channels: 0,4,8
	 */
	for (i = 0, ch = 0; i < ctx->streams; ch += ctx->channels[i++])
		if (op->active_sink_pins & BIT(ch))
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
	if (stream_idx >= ctx->streams) {
		pr_err("KASOP(%s): mixer get, invalid stream !\n",
			op->obj.name);
		ucontrol->value.integer.value[0] = 0;
		return 0;
	}
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
	case MIXER_CTRL_CH1:
	case MIXER_CTRL_CH2:
	case MIXER_CTRL_CH3:
	case MIXER_CTRL_CH4:
	case MIXER_CTRL_CH5:
	case MIXER_CTRL_CH6:
		value = ctx->ch_gain[stream_idx][param_idx];
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
	int stream_idx, param_idx, ctrl_idx, cnt;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &ctrl_idx);
	struct mixer_ctx *ctx = op->context;
	int value = ucontrol->value.integer.value[0];
	int ramp0, ramp1;

	BUG_ON(ctrl_idx < 0 || ctrl_idx >= ctx->streams *
		MIXER_CTRLS_PER_STREAM);

	stream_idx = ctrl_idx / MIXER_CTRLS_PER_STREAM;
	param_idx = ctrl_idx % MIXER_CTRLS_PER_STREAM;
	if (stream_idx >= ctx->streams) {
		pr_err("KASOP(%s): mixer put, invalid stream !\n",
			op->obj.name);
		return 0;
	}
	switch (param_idx) {
	case MIXER_CTRL_GAIN:
		ctx->gain[stream_idx] = value;
		/* overwrite channel gains of the stream */
		for (cnt = 0; cnt < ctx->channels[stream_idx]; cnt++)
			ctx->ch_gain[stream_idx][cnt] = value;
		/* if muted, just save the vlaue */
		if (!ctx->muted[stream_idx]) {
			kcm_lock();
			set_stream_gain(op, ctx->ramp[0][stream_idx]);
			kcm_unlock();
		}
		break;
	case MIXER_CTRL_MUTE:
		if (ctx->muted[stream_idx] != value) {
			ctx->muted[stream_idx] = value;
			/* when unmute, send all the saved channel gain */
			kcm_lock();
			if (!value) {
				cnt = 0;
				while (cnt < ctx->channels[stream_idx]) {
					set_channel_gain(op, stream_idx, cnt,
						ctx->ch_gain[stream_idx][cnt]);
					cnt++;
				}
			} else {
				set_stream_gain(op, ctx->ramp[1][stream_idx]);
			}
			kcm_unlock();
		}
		break;
	case MIXER_CTRL_RAMP:
		ramp0 = value;
		ramp1 = ucontrol->value.integer.value[1];
		if (ramp0 < MIXER_MIN_RAMP_SAMPLES || /* RAMP: 240 ~ 480000 */
			ramp0 > MIXER_MAX_RAMP_SAMPLES ||
			ramp1 < MIXER_MIN_RAMP_SAMPLES ||
			ramp1 > MIXER_MAX_RAMP_SAMPLES)
			return -EINVAL;
		if (ctx->ramp[0][stream_idx] != ramp0 ||
			ctx->ramp[1][stream_idx] != ramp1) {
			ctx->ramp[0][stream_idx] = ramp0;
			ctx->ramp[1][stream_idx] = ramp1;
		}
		break;
	case MIXER_CTRL_CH1:
	case MIXER_CTRL_CH2:
	case MIXER_CTRL_CH3:
	case MIXER_CTRL_CH4:
	case MIXER_CTRL_CH5:
	case MIXER_CTRL_CH6:
		if (ctx->ch_gain[stream_idx][param_idx] != value) {
			ctx->ch_gain[stream_idx][param_idx] = value;
			kcm_lock();
			for (cnt = 0; cnt < ctx->channels[stream_idx]; cnt++)
				set_channel_gain(op, stream_idx, cnt,
					ctx->ch_gain[stream_idx][cnt]);
			kcm_unlock();
		}
		break;
	default:
		pr_err("KASOP(%s): mixer put, invalid control number !\n",
			op->obj.name);
	}

	return 0;
}

#define NAME_BUF_LEN	768
static int mixer_init(struct kasobj_op *op)
{
	u16 st, ch, ctrl_idx = 0, max, st_config;
	const int *tlv = NULL;
	char names_buf[NAME_BUF_LEN], *names = names_buf, *name;
	struct mixer_ctx *ctx = kzalloc(sizeof(struct mixer_ctx), GFP_KERNEL);
	struct snd_kcontrol_new *ctrl;

	op->context = ctx;
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

	for (st = 0; st < MAX_STREAMS; st++) {
		ctx->gain[st] = DEFV;
		ctx->muted[st] = 0;
		ctx->ramp[0][st] = 96000;
		ctx->ramp[1][st] = 240;
		for (ch = 0; ch < MAX_CHANNELS; ch++)
			ctx->ch_gain[st][ch] = DEFV;
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
				MIXER_MAX_RAMP_SAMPLES,	mixer_get, mixer_put,
				NULL, ctrl_idx);
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
	u16 *stream_cfg;
	short rate = op->db->rate / 25;
	int st, ch;

	stream_cfg = ctx->channels;
	kalimba_operator_message(op->op_id, OPERATOR_MSG_SET_CHANNELS,
			ctx->streams, stream_cfg, NULL, NULL, __kcm_resp);
	if (rate <= 0) {
		pr_err("KASOBJ(%s): Invalid sample rate (%d)\n",
			op->obj.name, rate);
		return -EINVAL;
	}
	kalimba_operator_message(op->op_id, OPMSG_COMMON_SET_SAMPLE_RATE,
			1, &rate, NULL, NULL, __kcm_resp);
	for (st = 0; st < ctx->streams; st++) {
		if (ctx->muted[st]) {
			set_stream_gain(op, ctx->ramp[1][st]);
			continue;
		}
		for (ch = 0; ch < ctx->channels[st]; ch++)
			set_channel_gain(op, st, ch, ctx->ch_gain[st][ch]);
	}

	ctx->primary_stream = 0;

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
