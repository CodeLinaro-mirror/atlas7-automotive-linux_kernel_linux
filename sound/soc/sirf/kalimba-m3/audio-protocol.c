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

#include "pcm.h"

static struct rpmsg_channel *audio_rpdev;

#define START_STREAM_CMD		0x1
#define STOP_STREAM_CMD			0x2

void kas_start_stream(u32 stream, u32 sample_rate, u32 channles, u32 buff_addr,
		u32 buff_size, u32 period_size)
{
	u32 msg[7];

	msg[0] = START_STREAM_CMD;
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

	msg[0] = STOP_STREAM_CMD;
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

	kas_pcm_notify(msg[1], msg[2]);
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

	return ret;
}
