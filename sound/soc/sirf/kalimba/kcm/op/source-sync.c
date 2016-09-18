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

#define SOURCE_SYNC_CTRL_NUM (2)
#define SOURCE_SYNC_CTRL_IDX_ACTIVE_STREAM (0)
#define SOURCE_SYNC_CTRL_IDX_TRANS_SAMPLES (1)

#define SOURCE_SYNC_GROUPS_MAX (24)
#define SOURCE_SYNC_CHANNELS_MAX (24)
#define SOURCE_SYNC_TRANS_SAMPLES_MAX (65536)

struct source_sync_ctx {
	u16 streams;
	u16 channels[SOURCE_SYNC_GROUPS_MAX];
	u16 input_map[SOURCE_SYNC_CHANNELS_MAX];	/* starts from 1 */
	u16 output_map[SOURCE_SYNC_CHANNELS_MAX];	/* starts from 1 */
	u16 active_stream;	/* starts from 1 */
	u16 trans_samples;
	u16 sample_rate;
};

struct sink_groups_msg {
	u16 group_num;
	u32 sync_group[SOURCE_SYNC_GROUPS_MAX];
};

struct route_item {
	u16 source_idx;
	u16 sink_idx;
	u16 rate;
	u16 gain;
	u16 trans_samples;
};

struct switch_route_msg {
	u16 route_num;
	struct route_item route[SOURCE_SYNC_CHANNELS_MAX];
};

static int set_sink_groups(struct kasobj_op *op)
{
	struct source_sync_ctx *ctx = op->context;
	struct sink_groups_msg msg;
	int idx, shift, ret, msg_len;
	u32 mask;

	if (!op->obj.life_cnt)
		return 0;

	msg.group_num = ctx->streams;
	for (idx = 0, shift = 0; idx < ctx->streams; idx++) {
		mask = ~0;			/* 0xFFFFFFFF */
		mask <<= ctx->channels[idx];	/* 0xFFFFFFF0 (if ch is 4) */
		mask = ~mask;			/* 0x0000000F */
		mask <<= shift;			/* 0x00000F00 (if shift is 8) */
		msg.sync_group[idx] = mask;
		shift += ctx->channels[idx];
	}
	msg_len = 2 * ctx->streams + 1;

	ret = kalimba_operator_message(op->op_id, SOURCESYNC_SET_SINK_GROUPS,
		msg_len, (u16 *)&msg, NULL, NULL, __kcm_resp);
	if (ret) {
		pr_err("KASOBJ(%s): set sink groups failed(%d)!\n",
			op->obj.name, ret);
		return ret;
	}

	return 0;
}

static int set_routes(struct kasobj_op *op)
{
	struct source_sync_ctx *ctx = op->context;
	struct switch_route_msg msg;
	int idx, cnt, ret, msg_len, tmp;

	if (!op->obj.life_cnt)
		return 0;

	for (idx = 0, cnt = 0; idx < SOURCE_SYNC_CHANNELS_MAX; idx++) {
		tmp = ctx->output_map[idx];
		if (tmp == 0)
			continue;
		msg.route[cnt].source_idx = idx;
		msg.route[cnt].sink_idx = tmp - 1;
		msg.route[cnt].gain = 0;	/* 0dB */
		msg.route[cnt].trans_samples = ctx->trans_samples;
		msg.route[cnt++].rate = ctx->sample_rate / 25;
	}
	msg.route_num = cnt;
	msg_len = 5 * cnt + 1;
	ret = kalimba_operator_message(op->op_id, SOURCESYNC_SET_ROUTE,
		msg_len, (u16 *)&msg, NULL, NULL, __kcm_resp);
	if (ret) {
		pr_err("KASOBJ(%s): set route failed(%d)!\n",
			op->obj.name, ret);
		return ret;
	}

	return 0;
}

static int find_active_stream(struct kasobj_op *op)
{
	int i, ch;
	struct source_sync_ctx *ctx = op->context;

	/*
	 * Check first channel pin of each stream
	 * eg. 3streams, 4channels: 0,4,8
	 */
	for (i = 0, ch = 0; i < ctx->streams; ch += ctx->channels[i++])
		if (op->active_sink_pins & BIT(ch))
			return i;

	return -EINVAL;
}

static void change_active_stream(struct kasobj_op *op, int active_st)
{
		struct source_sync_ctx *ctx = op->context;
		int ch, shift, idx, out_idx;

		ctx->active_stream = active_st;
		active_st--;
		for (idx = 0, shift = 0; idx < active_st; idx++)
			shift += ctx->channels[idx];
		ch = ctx->channels[active_st];
		for (idx = 0; idx < ch; idx++) {
			out_idx = ctx->input_map[idx + shift];
			ctx->output_map[out_idx - 1] = idx + shift + 1;
		}
}

/* Endpoint activity changed, we may have to pick new primary stream */
static void pin_changed(struct kasobj_op *op, int is_sink)
{
	if (is_sink && (op->obj.life_cnt == 1)) {
		/* Pick active stream based on current input pins activity */
		int active_stream = find_active_stream(op);
		struct source_sync_ctx *ctx = op->context;

		if (active_stream == -EINVAL) {
			pr_err("KASOBJ(%s): find active stream failed!\n",
				op->obj.name);
			return;
		}
		change_active_stream(op, active_stream + 1);
		set_routes(op);
		kcm_debug("KASOP(%s): set active stream to %d\n",
				op->obj.name, ctx->active_stream);
	}
}

static int source_sync_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ctl_idx, value;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &ctl_idx);
	struct source_sync_ctx *ctx = op->context;

	BUG_ON(ctl_idx < 0 || ctl_idx >= SOURCE_SYNC_CTRL_NUM);

	switch (ctl_idx) {
	case SOURCE_SYNC_CTRL_IDX_ACTIVE_STREAM:
		value = ctx->active_stream;
		break;
	case SOURCE_SYNC_CTRL_IDX_TRANS_SAMPLES:
		value = ctx->trans_samples;
		break;
	default:
		pr_err("KASOP(%s): source sync get, invalid control number !\n",
			op->obj.name);
		return -EINVAL;
	}
	ucontrol->value.integer.value[0] = value;

	return 0;
}

static int source_sync_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ctl_idx;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &ctl_idx);
	struct source_sync_ctx *ctx = op->context;
	int value = ucontrol->value.integer.value[0];

	BUG_ON(ctl_idx < 0 || ctl_idx >= SOURCE_SYNC_CTRL_NUM);

	switch (ctl_idx) {
	case SOURCE_SYNC_CTRL_IDX_ACTIVE_STREAM:
		if (value < 1)
			return -EINVAL;
		kcm_lock();
		change_active_stream(op, value);
		set_routes(op);
		kcm_unlock();
		break;
	case SOURCE_SYNC_CTRL_IDX_TRANS_SAMPLES:
		if (value != ctx->trans_samples)
			ctx->trans_samples = value;
		break;
	default:
		pr_err("KASOP(%s): source sync put, invalid control number !\n",
			op->obj.name);
		return -EINVAL;
	}

	return 0;
}

/* Create control interfaces */
static int source_sync_init(struct kasobj_op *op)
{
	struct source_sync_ctx *ctx = kzalloc(
		sizeof(struct source_sync_ctx), GFP_KERNEL);
	struct snd_kcontrol_new *ctrl;
	char names_buf[256], *names = names_buf, *name;
	int ctrl_idx = 0; /* control interface index */
	int max, ch, idx, cnt, tmp;

	op->context = ctx;
	ctx->trans_samples = 0;
	ctx->sample_rate = op->db->rate;
	if (op->db->rate == 0) {
		pr_err("KASOP(%s): invalid sample rate!\n", op->obj.name);
		return -EINVAL;
	}

	for (idx = 0, ch = 0, cnt = 0; idx < SOURCE_SYNC_CHANNELS_MAX; idx++) {
		tmp = op->db->param.srcsync_cfg.stream_ch[idx];
		if (tmp == 0)
			break;
		if (tmp > SOURCE_SYNC_CHANNELS_MAX) {
			pr_err("KASOP(%s): invalid channel group config!\n",
				op->obj.name);
			return -EINVAL;
		}
		ch += tmp;
		if (ch > SOURCE_SYNC_CHANNELS_MAX) {
			pr_err("KASOP(%s): too many total channels(%d)!\n",
				op->obj.name, ch);
			return -EINVAL;
		}
		ctx->channels[idx] = tmp;
		cnt += tmp;
	}
	ctx->streams = idx;
	for (idx = 0; idx < SOURCE_SYNC_CHANNELS_MAX; idx++) {
		tmp = op->db->param.srcsync_cfg.input_map[idx];
		if (tmp > SOURCE_SYNC_CHANNELS_MAX) {
			pr_err("KASOP(%s): invalid input-output map!\n",
				op->obj.name);
			return -EINVAL;
		}
		ctx->input_map[idx] = tmp;
	}

	if (!op->db->ctrl_names.s)
		return 0;

	if (snprintf(names_buf, 256, "%s", op->db->ctrl_names.s) >= 256) {
		pr_err("KASOP(%s): control names too long!\n", op->obj.name);
		return -EINVAL;
	}

	while ((name = strsep(&names, ":;"))) {
		if (ctrl_idx >= SOURCE_SYNC_CTRL_NUM) {
			pr_err("KASOP(%s): too many controls!\n",
				op->obj.name);
			return -EINVAL;
		}
		if (kcm_strcasestr(name, "Stream"))
			max = ctx->streams;
		else if (kcm_strcasestr(name, "Samples"))
			max = SOURCE_SYNC_TRANS_SAMPLES_MAX;
		else {
			pr_err("KASOP(%s): unknown control '%s'!\n",
				op->obj.name, name);
			return -EINVAL;
		}

		ctrl = kasop_ctrl_single_ext_tlv(name, op, max,
			source_sync_get, source_sync_put, NULL, ctrl_idx);
		kcm_register_ctrl(ctrl);
		ctrl_idx++;
	}

	return 0;
}

/* Called after the operator is created */
static int source_sync_create(struct kasobj_op *op,
	const struct kasobj_param *param)
{
	struct source_sync_ctx *ctx = op->context;
	int idx, out_ch;

	for (idx = 0; idx < SOURCE_SYNC_CHANNELS_MAX; idx++)
		ctx->output_map[idx] = 0;

	for (idx = 0; idx < SOURCE_SYNC_CHANNELS_MAX; idx++) {
		out_ch = ctx->input_map[idx];
		if (out_ch == 0)
			continue;
		ctx->output_map[out_ch - 1] = idx + 1;
	}

	return set_sink_groups(op);
}

static int source_sync_trigger(struct kasobj_op *op, int event)
{
	const int param = KASOP_GET_PARAM(event);

	switch (KASOP_GET_EVENT(event)) {
	case kasop_event_start_ep:
		pin_changed(op, param);
		break;
	default:
		break;
	}

	return 0;
}

static const struct kasop_impl source_sync_impl = {
	.init = source_sync_init,
	.create = source_sync_create,
	.trigger = source_sync_trigger,
};

/* register source sync operator */
static int __init kasop_init_source_sync(void)
{
	return kcm_register_cap(CAPABILITY_ID_SOURCE_SYNC, &source_sync_impl);
}

subsys_initcall(kasop_init_source_sync);
