/*
 * Copyright (c) [2016] The Linux Foundation. All rights reserved.
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
#include <linux/platform_device.h>
#include <linux/rpmsg.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/wait.h>

#include "debug.h"
#include "pcm.h"

static struct rpmsg_channel *audio_rpdev;

#define MSG_START_STREAM		0x00000001
#define MSG_STOP_STREAM			0x00000002
#define MSG_OPERATOR_CFG		0x00000003
#define MSG_PS_ADDR_SET			0x00000004
#define MSG_DATA_PRODUCED		0x00000005
#define MSG_DATA_CONSUMED		0x00000006
#define MSG_AUDIO_CODEC_SET		0x00000007
#define MSG_GET_AUDIO_CODEC_VOL_RANGE	0x00000008
#define MSG_AUDIO_CODEC_VOL_SET		0x00000009
#define MSG_DSP_COMMAND			0x0000000A

#define MSG_NEED_ACK			0x1
#define MSG_NEED_RSP			0x2

static wait_queue_head_t waitq_dsp_rsp;
static bool msg_dsp_rsp;
static u16 resp_payload[64];

struct audio_msg {
	u32 msg_type;
	u32 need_ack_rsp;
	u32 size;
	u8 msg[];
};

int kas_send_raw_msg(u8 *data, u32 data_bytes, u16 *resp)
{
	struct audio_msg *audio_msg;
	u16 msg_id = ((u16 *)data)[0];

	audio_msg = kmalloc(data_bytes + sizeof(struct audio_msg), GFP_KERNEL);

	if (audio_msg == NULL)
		return -ENOMEM;

	if (msg_id != DATA_PRODUCED && msg_id != DATA_CONSUMED)
		audio_msg->need_ack_rsp = MSG_NEED_ACK | MSG_NEED_RSP;

	audio_msg->msg_type = MSG_DSP_COMMAND;
	audio_msg->size = data_bytes;
	memcpy(audio_msg->msg, data, data_bytes);
	msg_dsp_rsp = false;
	rpmsg_send(audio_rpdev, audio_msg,
		data_bytes + sizeof(struct audio_msg));
	if (audio_msg->need_ack_rsp & MSG_NEED_RSP) {
		wait_event(waitq_dsp_rsp, msg_dsp_rsp == true);
		if (resp)
			memcpy(resp, resp_payload, 64 * sizeof(u16));
	}
	kfree(audio_msg);
	return 0;
}

void kas_start_stream(u32 stream, u32 sample_rate, u32 channles, u32 buff_addr,
		u32 buff_size, u32 period_size)
{
	u32 msg[7];

	msg[0] = MSG_START_STREAM;
	msg[1] = stream;
	msg[2] = sample_rate;
	msg[3] = channles;
	msg[4] = buff_addr;
	msg[5] = buff_size;
	msg[6] = period_size;

	rpmsg_send(audio_rpdev, msg, 7 * sizeof(u32));
}

void kas_stop_stream(u32 stream)
{
	u32 msg[2];

	msg[0] = MSG_STOP_STREAM;
	msg[1] = stream;

	rpmsg_send(audio_rpdev, msg, 2 * sizeof(u32));
}

static struct rpmsg_device_id rpmsg_driver_audio_id_table[] = {
	{ .name = "rpmsg-audio" },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_driver_sample_id_table);

static void rpmsg_audio_cb(struct rpmsg_channel *rpdev, void *data, int len,
		void *priv, u32 src)
{
	u32 *msg = (u32 *)data;

	if (msg[0] == MSG_DATA_PRODUCED || msg[0] == MSG_DATA_CONSUMED)
		kas_pcm_notify(msg[1], msg[2]);
	else if (msg[0] == (0x10000000 | MSG_DSP_COMMAND)) {
		memcpy(resp_payload, &msg[2], msg[1]);
		msg_dsp_rsp = true;
		wake_up(&waitq_dsp_rsp);
	}
}

static int rpmsg_audio_probe(struct rpmsg_channel *rpdev)
{
	u32 sync_signal = 0xffffffff;

	audio_rpdev = rpdev;
	rpmsg_send(rpdev, &sync_signal, sizeof(u32));
	return 0;
}

static struct rpmsg_driver rpmsg_audio_client = {
	.drv.name = KBUILD_MODNAME,
	.drv.owner = THIS_MODULE,
	.id_table = rpmsg_driver_audio_id_table,
	.probe = rpmsg_audio_probe,
	.callback = rpmsg_audio_cb,
};

int audio_protocol_init(void)
{
	int ret = 0;

	ret = register_rpmsg_driver(&rpmsg_audio_client);
	if (ret)
		pr_err("Register audio rpmsg failed: %d\n", ret);

	init_waitqueue_head(&waitq_dsp_rsp);
#ifdef CONFIG_SND_SOC_SIRF_KALIMBA_DEBUG
	debug_init();
#endif
	return ret;
}
