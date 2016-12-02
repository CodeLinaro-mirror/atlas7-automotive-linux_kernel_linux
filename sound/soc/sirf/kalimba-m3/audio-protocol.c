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

#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rpmsg.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>

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
#define MSG_CREATE_STREAM		0x00000016
#define MSG_DESTROY_STREAM		0x00000017
#define MSG_COREDUMP			0x00000018
#define MSG_CREATE_STREAM_RESP		0x00000019
#define MSG_DESTROY_STREAM_RESP		0x0000001A
#define MSG_START_STREAM_RESP		0x0000001B
#define MSG_STOP_STREAM_RESP		0x0000001C

#define MSG_NEED_ACK			0x1
#define MSG_NEED_RSP			0x2

#define MSG_RESP_SUCCESS		0x00
#define MSG_RESP_ERROR			(~0x00)

#define KAS_POINTER_UPDATE_PHYADDR	0x4FF00000
#define KAS_POINTER_UPDATE_SIZE		256

#define CHECK_POINTER_INTERVAL_NS	5000000

static wait_queue_head_t waitq_dsp_rsp;
static bool msg_dsp_rsp;
static u16 resp_payload[64];
static u32 *kas_pointer_update;
static u32 local_kas_data_pointer[32];
static u32 running_stream;
static struct hrtimer hrt;
static struct work_struct audio_wq;

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
		pr_err("Audio IPC(%s): rpdev is NULL\n", __func__);
		return NULL;
	}
	msg_dsp_rsp = false;
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
		pr_err("Audio IPC(%s): rpdev is NULL\n", __func__);
		return -EINVAL;
	}
	msg_dsp_rsp = false;
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
	if (!audio_rpdev) {
		pr_err("Audio IPC(%s): rpdev is NULL\n", __func__);
		return -EINVAL;
	}
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
	if (!audio_rpdev) {
		pr_err("Audio IPC(%s): rpdev is NULL\n", __func__);
		return;
	}
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
	if (!audio_rpdev) {
		pr_err("Audio IPC(%s): rpdev is NULL\n", __func__);
		return;
	}
	rpmsg_send(audio_rpdev, msg, 2 * sizeof(u32) + resp_len);
	kfree(msg);
}

int kas_create_stream(u32 stream, u32 sample_rate, u32 channles, u32 buff_addr,
		u32 buff_size, u32 period_size)
{
	u32 *resp = (u32 *)resp_payload;
	u32 msg[7];

	msg[0] = MSG_CREATE_STREAM;
	msg[1] = stream;
	msg[2] = sample_rate;
	msg[3] = channles;
	msg[4] = buff_addr;
	msg[5] = buff_size;
	msg[6] = period_size;
	if (!audio_rpdev) {
		pr_err("Audio IPC(%s): rpdev is NULL\n", __func__);
		return -EINVAL;
	}
	msg_dsp_rsp = false;
	local_kas_data_pointer[stream] = 0;
	rpmsg_send(audio_rpdev, msg, 7 * sizeof(u32));
	wait_event(waitq_dsp_rsp, msg_dsp_rsp == true);
	if (resp[0]) {
		pr_err("Audio IPC: create stream (dev: %d) failed!\n", stream);
		return -EINVAL;
	}
	if (!running_stream)
		hrtimer_start(&hrt, ktime_set(0, 0), HRTIMER_MODE_REL);
	running_stream |= (1 << stream);

	return 0;
}

int kas_destroy_stream(u32 stream, u32 channels)
{
	u32 *resp = (u32 *)resp_payload;
	u32 msg[3];

	msg[0] = MSG_DESTROY_STREAM;
	msg[1] = stream;
	msg[2] = channels;
	if (!audio_rpdev) {
		pr_err("Audio IPC(%s): rpdev is NULL\n", __func__);
		return -EINVAL;
	}
	msg_dsp_rsp = false;
	rpmsg_send(audio_rpdev, msg, 3 * sizeof(u32));
	wait_event(waitq_dsp_rsp, msg_dsp_rsp == true);

	running_stream &= ~(1 << stream);
	if (!running_stream)
		hrtimer_cancel(&hrt);

	if (resp[0]) {
		pr_err("Audio IPC: destroy stream (dev: %d) failed!\n", stream);
		return -EINVAL;
	}

	return 0;
}

int kas_start_stream(u32 stream)
{
	u32 *resp = (u32 *)resp_payload;
	u32 msg[2];

	msg[0] = MSG_START_STREAM;
	msg[1] = stream;

	if (!audio_rpdev) {
		pr_err("Audio IPC(%s): rpdev is NULL\n", __func__);
		return -EINVAL;
	}
	msg_dsp_rsp = false;
	kas_pointer_update[stream] = 0;
	rpmsg_send(audio_rpdev, msg, 2 * sizeof(u32));
	wait_event(waitq_dsp_rsp, msg_dsp_rsp == true);
	if (resp[0]) {
		pr_err("Audio IPC: start stream (dev: %d) failed!\n", stream);
		return -EINVAL;
	}

	return 0;
}

int kas_stop_stream(u32 stream)
{
	u32 *resp = (u32 *)resp_payload;
	u32 msg[2];

	msg[0] = MSG_STOP_STREAM;
	msg[1] = stream;

	msg_dsp_rsp = false;
	if (!audio_rpdev) {
		pr_err("Audio IPC(%s): rpdev is NULL\n", __func__);
		return -EINVAL;
	}
	msg_dsp_rsp = false;
	rpmsg_send(audio_rpdev, msg, 2 * sizeof(u32));
	wait_event(waitq_dsp_rsp, msg_dsp_rsp == true);
	if (resp[0]) {
		pr_err("Audio IPC: stop stream (dev: %d) failed!\n", stream);
		return -EINVAL;
	}

	return 0;
}

void kas_ps_region_addr_update(u32 addr)
{
	u32 msg[2];

	msg[0] = MSG_PS_ADDR_SET;
	msg[1] = addr;
	if (!audio_rpdev) {
		pr_err("Audio IPC(%s): rpdev is NULL\n", __func__);
		return;
	}
	rpmsg_send(audio_rpdev, msg, 2 * sizeof(u32));
}

static void kas_dram_allocation_req(u32 length)
{
	u32 msg[2];
	unsigned long dram_allocation_addr;

	dram_allocation_addr = buff_alloc(NULL, length);
	msg[0] = MSG_DRAM_ALLOCATION_RESP;
	msg[1] = dram_allocation_addr;
	if (!audio_rpdev) {
		pr_err("Audio IPC(%s): rpdev is NULL\n", __func__);
		return;
	}
	rpmsg_send(audio_rpdev, msg, 2 * sizeof(u32));
}

static void kas_dram_free_req(u32 address)
{
	u32 msg;

	buff_free(NULL, address);
	msg = MSG_DRAM_FREE_RESP;
	if (!audio_rpdev) {
		pr_err("Audio IPC(%s): rpdev is NULL\n", __func__);
		return;
	}
	rpmsg_send(audio_rpdev, &msg, sizeof(u32));
}

#define KAS_DM1_SRAM_START_ADDR			0
#define KAS_DM2_SRAM_START_ADDR			0xFF3000
#define KAS_PM_SRAM_START_ADDR			0
#define HEADER_SIZE 1024
enum mtype {KAS_PM, KAS_DM1, KAS_DM2, KAS_RM};

static struct kas_mem {
	enum mtype mem_type;
	char *mem_name;
	u32 mem_start;
	u32 mem_size;
} kas_memory[] = {
	{ KAS_PM,  "DC", KAS_PM_SRAM_START_ADDR,  0x10000},
	{ KAS_DM1, "DD", KAS_DM1_SRAM_START_ADDR, 0x8000},
	{ KAS_DM2, "DD", KAS_DM2_SRAM_START_ADDR, 0x8000},
	{ KAS_RM,  "DR", 0x00FFFE00, 0x00200},
};

char *kregs[] = {
	"R PC",
	"R rMAC2",
	"R rMAC1",
	"R rMAC0",
	"R rMAC24",
	"R R0",
	"R R1",
	"R R2",
	"R R3",
	"R R4",
	"R R5",
	"R R6",
	"R R7",
	"R R8",
	"R R9",
	"R R10",
	"R RLINK",
	"R FLAGS",
	"R RMACB24",
	"R I0",
	"R I1",
	"R I2",
	"R I3",
	"R I4",
	"R I5",
	"R I6",
	"R I7",
	"R M0",
	"R M1",
	"R M2",
	"R M3",
	"R L0",
	"R L1",
	"R L3",
	"R L4",
	"R RUNCLKS",
	"R NUMINSTRS",
	"R NUMSTALLS",
	"R rMACB2",
	"R rMACB1",
	"R rMACB0",
	"R B0",
	"R B1",
	"R B4",
	"R B5",
	"R FP",
	"R SP"
};

struct coredump_desc_t {
	u32 base_addr;

	u32 pm_addr;
	u32 pm_len;

	u32 dm1_addr;
	u32 dm1_len;

	u32 dm2_addr;
	u32 dm2_len;

	u32 rm_addr;
	u32 rm_len;

	u32 kregs_addr;
	u32 kregs_len;
};

/*
 * kas generate core dump header
 */
static u32 kerror_coredump_header(char *pos)
{
	u32 dsp_ver = 0x00600019;
	char *p = pos;

	p += sprintf(p, "XCD2\n");
	p += sprintf(p, "AV %08x\n", dsp_ver);
	p += sprintf(p, "P DSP\n");
	p += sprintf(p, "AT KALIMBA5\n");

	return (u32)(p - pos);
}

/*
 * calculate the buffer needed for
 * coredump file
 *
 */
static u32 kerror_buf_size_calc(void)
{
	u32 i, k;
	u32 size;
	char buf[256];

	/* Frist, the size of the header */
	size  = HEADER_SIZE;

	/* Calculate buffer size for the coredump of the memory regions */
	for (i = 0; i < ARRAY_SIZE(kas_memory); i++) {
		size += sprintf(buf, "%s %08x %08x\n",
				kas_memory[i].mem_name,
				kas_memory[i].mem_start,
				kas_memory[i].mem_size);

		for (k = 0; k < kas_memory[i].mem_size; k++)
			size += sprintf(buf, "%08x%s", 0,
					(k + 1) % 8 ? " ":"\n");
	}

	/* Calculate the buffer size for the registers */
	for (i = 0; i < ARRAY_SIZE(kregs); i++)
		size += sprintf(buf, "%s %06x\n",
				kregs[i], 0);

	return size;
}

static void kas_coredump(u32 address, u32 len)
{
	struct file *cdfile;
	int ret;
	char *dp, *p;
	u32 *addr;
	u32 phy_addr;
	u32 fsize, pos, i;
	size_t count;
	struct coredump_desc_t *desc;

	fsize = kerror_buf_size_calc();

	/* allocate buffers */
	dp = dma_alloc_coherent(NULL, fsize, &phy_addr,
				GFP_KERNEL);
	if (dp == NULL) {
		pr_err("Alloc dram failed.\n");
		return;
	}

	cdfile = filp_open("/var/lib/kalimba/coredump.xcd",
			O_RDWR | O_CREAT | O_TRUNC | O_DSYNC, 0600);
	if (IS_ERR(cdfile)) {
		ret = PTR_ERR(cdfile);
		pr_err("create coredump file failure:\n");
		goto open_err;
	}

	desc = (struct coredump_desc_t *)ioremap_nocache(address, len);

	/* generate the header */
	pos = kerror_coredump_header(dp);
	p = dp+pos;

	/*dump pm*/
	p += sprintf(p, "%s %08x %08x\n",
			kas_memory[0].mem_name,
			kas_memory[0].mem_start,
			kas_memory[0].mem_size);
	addr = (u32 *)ioremap_nocache(desc->pm_addr, desc->pm_len);
	for (i = 0; i < desc->pm_len / 4; i++)
			p += sprintf(p, "%08x%s",
				(u32)*(addr + i),
				(i + 1) % 8 ? " ":"\n");

	/*dump dm1*/
	p += sprintf(p, "%s %08x %08x\n",
			kas_memory[1].mem_name,
			kas_memory[1].mem_start,
			kas_memory[1].mem_size);

	addr = (u32 *)ioremap_nocache(desc->dm1_addr, desc->dm1_len);
	for (i = 0; i < desc->dm1_len / 4; i++)
			p += sprintf(p, "%08x%s",
				(u32)*(addr + i),
				(i + 1) % 8 ? " ":"\n");

	/*dump dm2*/
	p += sprintf(p, "%s %08x %08x\n",
			kas_memory[2].mem_name,
			kas_memory[2].mem_start,
			kas_memory[2].mem_size);

	addr = (u32 *)ioremap_nocache(desc->dm2_addr, desc->dm2_len);
	for (i = 0; i < desc->dm2_len / 4; i++)
			p += sprintf(p, "%08x%s",
				(u32)*(addr + i),
				(i + 1) % 8 ? " ":"\n");

	/*dump rm*/
	p += sprintf(p, "%s %08x %08x\n",
			kas_memory[3].mem_name,
			kas_memory[3].mem_start,
			kas_memory[3].mem_size);
	addr = (u32 *)ioremap_nocache(desc->rm_addr, desc->rm_len);
	for (i = 0; i < desc->rm_len / 4; i++)
			p += sprintf(p, "%08x%s",
				(u32)*(addr + i),
				(i + 1) % 8 ? " ":"\n");

	/*dump kregs*/
	addr = (u32 *)ioremap_nocache(desc->kregs_addr, desc->kregs_len);
	for (i = 0; i < ARRAY_SIZE(kregs); i++)
		p += sprintf(p, "%s %06x\n",
				kregs[i], *(addr  + i));

	count = (size_t)(p - dp);
	/* write all the core dump data into a file */
	ret = kernel_write(cdfile, dp, count, 0);
	if (ret < 0) {
		pr_err("Error writing coredump file:\n");
		ret = -EIO;
	}

	vfs_fsync(cdfile, 0);
	filp_close(cdfile, NULL);
	pr_info("kcoredump completed:\n");
open_err:
	return;
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
	case MSG_CREATE_STREAM_RESP:
	case MSG_DESTROY_STREAM_RESP:
	case MSG_START_STREAM_RESP:
	case MSG_STOP_STREAM_RESP:
		memcpy(resp_payload, &msg[1], sizeof(u32));
		msg_dsp_rsp = true;
		wake_up(&waitq_dsp_rsp);
		break;
	case MSG_COREDUMP:
		kas_coredump(msg[1], msg[2]);
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

int audio_rpmsg_check(void)
{
	if (audio_rpdev)
		return 0;
	else
		return -EINVAL;
}


void wq_do_work(struct work_struct *work)
{
	int i;

	for (i = 0; i < 32; i++) {
		if ((running_stream & (1 << i)) &&
			(local_kas_data_pointer[i] != kas_pointer_update[i])) {
			kas_pcm_notify(i, kas_pointer_update[i]);
			local_kas_data_pointer[i] = kas_pointer_update[i];
		}
	}
}

static enum hrtimer_restart audio_hrtimer_callback(struct hrtimer *timer)
{
	schedule_work(&audio_wq);
	hrtimer_forward_now(timer, ns_to_ktime(CHECK_POINTER_INTERVAL_NS));

	return HRTIMER_RESTART;
}

int audio_protocol_init(void)
{
	int ret = 0;

	/* Use shared memory to update the audio data pointer */
	kas_pointer_update = (u32 *)ioremap_nocache(KAS_POINTER_UPDATE_PHYADDR,
		KAS_POINTER_UPDATE_SIZE);
	if (!kas_pointer_update) {
		pr_err("Remap kas pointer update share memory failed.");
		return -ENXIO;
	}

	/*
	 * Create a hrtimer to loop check the audio data pointer.
	 * The interval time is 5ms.
	 */
	hrtimer_init(&hrt, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	hrt.function = audio_hrtimer_callback;
	INIT_WORK(&audio_wq, wq_do_work);
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
