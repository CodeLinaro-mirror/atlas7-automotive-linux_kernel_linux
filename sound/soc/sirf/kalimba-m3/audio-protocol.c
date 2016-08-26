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

#include "buffer.h"
#include "debug.h"
#include "license.h"
#include "pcm.h"
#include "ps.h"

static struct rpmsg_channel *audio_rpdev;

#define MSG_START_STREAM		0x00000001
#define MSG_STOP_STREAM			0x00000002
#define MSG_OPERATOR_CFG		0x00000003
#define MSG_PS_ADDR_SET			0x00000004
#define MSG_PS_UPDATE			0x00000005
#define MSG_DATA_PRODUCED		0x00000006
#define MSG_DATA_CONSUMED		0x00000007
#define MSG_AUDIO_CODEC_SET		0x00000008
#define MSG_GET_AUDIO_CODEC_VOL_RANGE	0x00000009
#define MSG_AUDIO_CODEC_VOL_SET		0x0000000A
#define MSG_DSP_COMMAND			0x0000000B
#define MSG_LICENSE_REQ			0x0000000C
#define MSG_LICENSE_RESP		0x0000000D
#define MSG_DRAM_ALLOCATION_REQ		0x0000000E
#define MSG_DRAM_ALLOCATION_RESP	0x0000000F
#define MSG_DRAM_FREE_REQ		0x00000010
#define MSG_DRAM_FREE_RESP		0x00000011
#define MSG_OP_OBJ_REQ			0x00000012
#define MSG_OP_OBJ_RESP			0x00000013
#define MSG_CTRL_REQ			0x00000014
#define MSG_CTRL_RESP			0x00000015

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

u32 *kas_get_m3_op_obj(u8 *op_name, int len)
{
	u32 msg[10];
	u32 *resp = (u32 *)resp_payload;

	if (len > 32) {
		pr_err("Audio IPC: OP name too long (%d)\n", len);
		return NULL;
	}

	msg[0] = MSG_OP_OBJ_REQ;
	msg[1] = len;
	memcpy(&msg[2], op_name, len);
	if (!audio_rpdev) {
		pr_err("Audio IPC: rpdev 0x%x\n", (u32)audio_rpdev);
		return NULL;
	}
	rpmsg_send(audio_rpdev, msg, len + 2 * sizeof(u32));
	wait_event(waitq_dsp_rsp, msg_dsp_rsp == true);

	return (u32 *)resp[0];
}
int kas_ctrl_msg(int put, u32 *op_m3, int ctrl_id, int value_idx,
		u32 value, u32 *ret)
{
	u32 *resp = (u32 *)resp_payload;
	u32 msg[6];

	msg[0] = MSG_CTRL_REQ;
	msg[1] = put;
	msg[2] = (u32)op_m3;
	msg[3] = ctrl_id;
	msg[4] = value_idx;
	msg[5] = value;
	if (!audio_rpdev) {
		pr_err("Audio IPC: rpdev 0x%x\n", (u32)audio_rpdev);
		return -EINVAL;
	}
	rpmsg_send(audio_rpdev, msg, 6 * sizeof(u32));
	wait_event(waitq_dsp_rsp, msg_dsp_rsp == true);
	if (!ret) {
		if (put)
			return 0;
		pr_err("Audio IPC: return value addr is NULL!\n");
		return -EINVAL;
	}
	*ret = resp[0];

	return	0;
}

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

void kas_send_data_produced(u32 stream, u32 pos)
{
	u32 msg[3];

	msg[0] = MSG_DATA_PRODUCED;
	msg[1] = stream;
	msg[2] = pos;

	rpmsg_send(audio_rpdev, msg, 3 * sizeof(u32));
}

void kas_send_license_ctrl_resp(u32 resp_len, void *data)
{
	void *__msg;
	u32 *msg;

	__msg = kmalloc(2 * sizeof(u32) + resp_len, GFP_KERNEL);
	if (__msg == NULL)
		return;

	msg = (u32 *)__msg;

	msg[0] = MSG_LICENSE_RESP;
	msg[1] = resp_len / sizeof(u16);
	memcpy(&msg[2], data, resp_len);
	rpmsg_send(audio_rpdev, msg, 2 * sizeof(u32) + resp_len);
	kfree(msg);
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

void kas_ps_region_addr_update(u32 addr)
{
	u32 msg[2];

	msg[0] = MSG_PS_ADDR_SET;
	msg[1] = addr;
	rpmsg_send(audio_rpdev, msg, 2 * sizeof(u32));
}

static void kas_dram_allocation_req(u32 length)
{
	u32 msg[2];
	unsigned long dram_allocation_addr;

	dram_allocation_addr = buff_alloc(NULL, length);
	msg[0] = MSG_DRAM_ALLOCATION_RESP;
	msg[1] = dram_allocation_addr;
	rpmsg_send(audio_rpdev, msg, 2 * sizeof(u32));
}

static void kas_dram_free_req(u32 address)
{
	u32 msg;

	buff_free(NULL, address);
	msg = MSG_DRAM_FREE_RESP;
	rpmsg_send(audio_rpdev, &msg, sizeof(u32));
}

static struct rpmsg_device_id rpmsg_driver_audio_id_table[] = {
	{ .name = "rpmsg-audio" },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_driver_sample_id_table);

#define AUDIO_PROTOCOL_RESP_ID(msg)		(0x10000000 | msg)

static void rpmsg_audio_cb(struct rpmsg_channel *rpdev, void *data, int len,
		void *priv, u32 src)
{
	u32 *msg = (u32 *)data;

	switch (msg[0]) {
	case MSG_DATA_PRODUCED:
	case MSG_DATA_CONSUMED:
		kas_pcm_notify(msg[1], msg[2]);
		break;
	case AUDIO_PROTOCOL_RESP_ID(MSG_DSP_COMMAND):
		memcpy(resp_payload, &msg[2], msg[1]);
		msg_dsp_rsp = true;
		wake_up(&waitq_dsp_rsp);
		break;
	case MSG_PS_UPDATE:
		kas_ps_update();
		break;
	case MSG_LICENSE_REQ:
		kalimba_license_req(msg[1], &msg[2]);
		break;
	case MSG_DRAM_ALLOCATION_REQ:
		kas_dram_allocation_req(msg[1]);
		break;
	case MSG_DRAM_FREE_REQ:
		kas_dram_free_req(msg[1]);
		break;
	case MSG_OP_OBJ_RESP:
	case MSG_CTRL_RESP:
		memcpy(resp_payload, &msg[1], sizeof(u32));
		msg_dsp_rsp = true;
		wake_up(&waitq_dsp_rsp);
		break;
	default:
		break;
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
	ps_init();
	license_init();
	return ret;
}
