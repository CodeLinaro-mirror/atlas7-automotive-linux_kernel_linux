/*
 * kalimba debug & development interface
 *
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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

#include <linux/cdev.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "buffer.h"
#include "debug.h"
#include "dsp.h"
#include "firmware.h"
#include "i2s.h"
#include "iacc.h"
#include "ipc.h"
#include "regs.h"
#include "usp-pcm.h"

struct audio_unit {
	struct list_head node;
	unsigned long id;
	dma_addr_t buff_phy_addr;
	unsigned long buff_length;
	int pchannels;
	int rchannels;
	u32 type;
	int usp_port;
};

struct kalimba_debug_data {
	struct cdev debug_cdev;
	int devid;
	struct class *class;
	struct device *dev;
	unsigned long audio_unit_id;
	struct list_head audio_unit_list;
};

static struct kalimba_debug_data *debug_data;

static int insert_audio_unit_into_list(unsigned long addr, u32 type,
		unsigned long buff_length, int pchannels, int rchannels,
		int usp_port)
{
	struct audio_unit *audio_unit;

	audio_unit = kmalloc(sizeof(struct audio_unit),
			GFP_KERNEL);
	if (audio_unit == NULL)
		return -ENOMEM;

	debug_data->audio_unit_id++;
	audio_unit->buff_phy_addr = addr;
	audio_unit->id = debug_data->audio_unit_id;
	audio_unit->type = type;
	audio_unit->buff_length = buff_length;
	audio_unit->pchannels = pchannels;
	audio_unit->rchannels = rchannels;
	audio_unit->usp_port = usp_port;
	list_add(&audio_unit->node, &debug_data->audio_unit_list);
	return debug_data->audio_unit_id;
}

static int setup_audio_unit(unsigned long arg)
{
	u32 Type;
	u32 TypeConf;
	u32 BufferLength;
	u32 SampleFormat;
	u32 SampleRate;
	u32 Volume;
	unsigned long buff_addr;
	int ret;
	int pchannels = 2;
	int rchannels = 1;
	int i2s_slave_mode = 0;
	int usp_port = 0;
	enum iacc_input_path path = NO_USED;

	get_user(Type, (u32 __user *)arg);
	get_user(TypeConf, (u32 __user *)(arg + 4));
	get_user(BufferLength, (u32 __user *)(arg + 8));
	get_user(SampleFormat, (u32 __user *)(arg + 12));
	get_user(SampleRate, (u32 __user *)(arg + 16));
	get_user(Volume, (u32 __user *)(arg + 20));

	if ((Type == CTRL_DEVICE_TYPE_I2S) && (TypeConf & 0x1))
		pchannels = 6;

	if (Type == CTRL_DEVICE_TYPE_USP) {
		/* The bit4 to bit6 indicate the channels of USP */
		u32 channels = (TypeConf >> 4) & 0x7;

		if (channels == 0)
			pchannels = 1;
		else
			pchannels = channels * 2;
		/* The record channels must same as playback channels */
		rchannels = pchannels;

		if (TypeConf & 1)
			usp_port = 0;
		else if (TypeConf & 2)
			usp_port = 1;
		else if (TypeConf & 4)
			usp_port = 2;
		else if (TypeConf & 8)
			usp_port = 3;
	}

	if (Type == CTRL_DEVICE_TYPE_IACC) {
		if ((TypeConf & 0xf) == 0xf)
			pchannels = 4;
		else if ((TypeConf & 0xf) == 1)
			pchannels = 1;
		else {
			dev_err(debug_data->dev,
					"IACC supports mono and 4 channels only\n");
			ret = -EINVAL;
			goto out;
		}

		switch (TypeConf >> 16) {
		case 0:
			rchannels = 0;
			break;
		case 1:
			rchannels = 1;
			path = MONO_DIFF;
			break;
		case 2:
			rchannels = 2;
			path = STEREO_SINGLE;
			break;
		case 3:
			rchannels = 2;
			path = STEREO_DIGITAL;
			break;
		case 4:
			rchannels = 2;
			path = STEREO_LINEIN;
			break;
		case 5:
			rchannels = 1;
			path = MONO_LINEIN;
			break;
		default:
			dev_err(debug_data->dev,
				"IACC input path set error\n");
			ret = -EINVAL;
			goto out;
		}
	}

	buff_addr = buff_alloc(debug_data->dev, BufferLength);
	if (buff_addr < 0) {
		ret = buff_addr;
		goto out;
	}


	switch (Type) {
	case CTRL_DEVICE_TYPE_I2S:
		if (TypeConf & (1 << 5))
			i2s_slave_mode = 1;
		sirf_i2s_params(pchannels, SampleRate, i2s_slave_mode);
		break;
	case CTRL_DEVICE_TYPE_IACC:
		ret = iacc_setup(pchannels, rchannels, path, SampleRate,
				SampleFormat);
		if (ret < 0)
			goto out;
		break;
	case CTRL_DEVICE_TYPE_USP:
		sirf_usp_pcm_params(usp_port, 0, pchannels, SampleRate);
		sirf_usp_pcm_params(usp_port, 1, pchannels, SampleRate);
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	ret = insert_audio_unit_into_list(buff_addr, Type,
			BufferLength, pchannels, rchannels, usp_port);
	if (ret < 0)
		buff_free(debug_data->dev, (u32)buff_addr);
out:
	if (ret < 0) {
		ret = 0x200A;
		put_user(ret, (u32 __user *)arg);
	} else {
		u32 status = 0;

		put_user(status, (u32 __user *)arg);
		put_user((u32)ret, (u32 __user *)arg + 1);
		put_user(buff_addr, (u32 __user *)arg + 2);
	}
	return ret;
}

static void start_audio_unit(unsigned long arg)
{
	struct audio_unit *audio_unit;
	u32 audio_unit_id;
	u32 playback;
	u32 channels;
	int ret = -1;

	get_user(audio_unit_id, (u32 __user *)arg);
	get_user(playback, (u32 __user *)(arg + 4));
	list_for_each_entry(audio_unit, &debug_data->audio_unit_list, node) {
		if (audio_unit->id == audio_unit_id) {
			switch (audio_unit->type) {
			case CTRL_DEVICE_TYPE_I2S:
				sirf_i2s_start(playback);
				ret = 0;
				break;
			case CTRL_DEVICE_TYPE_IACC:
				channels = playback ? audio_unit->pchannels
					: audio_unit->rchannels;
				iacc_start(playback, channels);
				ret = 0;
				break;
			case CTRL_DEVICE_TYPE_USP:
				sirf_usp_pcm_start(audio_unit->usp_port,
					playback);
				ret = 0;
				break;
			default:
				break;
			}
		}
	}

	put_user((u32)ret, (u32 __user *)arg);
}

static void stop_audio_unit(unsigned long arg)
{
	struct audio_unit *audio_unit;
	u32 audio_unit_id;
	u32 playback;
	int ret = -1;

	get_user(audio_unit_id, (u32 __user *)arg);
	get_user(playback, (u32 __user *)(arg + 4));
	list_for_each_entry(audio_unit, &debug_data->audio_unit_list, node) {
		if (audio_unit->id == audio_unit_id) {
			switch (audio_unit->type) {
			case CTRL_DEVICE_TYPE_I2S:
				sirf_i2s_stop(playback);
				ret = 0;
				break;
			case CTRL_DEVICE_TYPE_IACC:
				iacc_stop(playback);
				ret = 0;
				break;
			case CTRL_DEVICE_TYPE_USP:
				sirf_usp_pcm_stop(audio_unit->usp_port,
					playback);
				ret = 0;
				break;
			default:
				break;
			}
		}
	}

	put_user((u32)ret, (u32 __user *)arg);
}

static void release_audio_unit(unsigned long arg)
{
	struct audio_unit *audio_unit;
	u32 audio_unit_id;
	int ret = -1;

	get_user(audio_unit_id, (u32 __user *)arg);

	list_for_each_entry(audio_unit, &debug_data->audio_unit_list, node) {
		if (audio_unit->id == audio_unit_id) {
			switch (audio_unit->type) {
			case CTRL_DEVICE_TYPE_I2S:
			case CTRL_DEVICE_TYPE_USP:
				ret = 0;
				break;
			case CTRL_DEVICE_TYPE_IACC:
				atlas7_codec_release();
				ret = 0;
				break;
			default:
				break;
			}
			buff_free(debug_data->dev, audio_unit->buff_phy_addr);
		}
	}
	put_user((u32)ret, (u32 __user *)arg);
}

static void create_operator(u16 *cmd, u16 *resp)
{
	u16 operator_id;

	kalimba_create_operator(cmd[2], &operator_id, resp);
}

static void create_operator_extended(u16 *cmd, u16 *resp)
{
	u16 vlength = cmd[3];
	u16 operator_id;

	kalimba_create_operator_extended(cmd[2], vlength, &cmd[4],
		&operator_id, resp);
}

static void start_operator(u16 *cmd, u16 *resp)
{
	u16 api_length = cmd[1];

	kalimba_start_operator(&cmd[2], api_length, resp);
}

static void stop_operator(u16 *cmd, u16 *resp)
{
	u16 api_length = cmd[1];

	kalimba_stop_operator(&cmd[2], api_length, resp);
}

static void reset_operator(u16 *cmd, u16 *resp)
{
	u16 api_length = cmd[1];

	kalimba_reset_operator(&cmd[2], api_length, resp);
}

static void destroy_operator(u16 *cmd, u16 *resp)
{
	u16 api_length = cmd[1];

	kalimba_destroy_operator(&cmd[2], api_length, resp);
}

static void operator_message(u16 *cmd, u16 *resp)
{
	u16 api_length = cmd[1];

	kalimba_operator_message(cmd[2], cmd[3], api_length - 2,
		&cmd[4], NULL, NULL, resp);
}

static void capability_code_dram_addr_set(u16 *cmd, u16 *resp)
{
	kalimba_capability_code_dram_addr_set(cmd[2], cmd[3], NULL, resp);
}

static void capability_code_dram_addr_clear(u16 *cmd, u16 *resp)
{
	kalimba_capability_code_dram_addr_clear(cmd[2], cmd[3], resp);
}

static void get_version_id(u16 *cmd, u16 *resp)
{
	kalimba_get_version_id(NULL, resp);
}

static void get_capid_list(u16 *cmd, u16 *resp)
{
	kalimba_get_capid_list(NULL, resp);
}

static void get_opid_list(u16 *cmd, u16 *resp)
{
	u16 filter = cmd[2];

	kalimba_get_opid_list(filter, NULL, NULL, resp);
}

static void get_connection_list(u16 *cmd, u16 *resp)
{
	u16 source_filter = cmd[2];
	u16 sink_filter = cmd[3];

	kalimba_get_connection_list(source_filter, sink_filter,
			NULL, NULL, NULL, resp);
}

static void endpoint_get_info(u16 *cmd, u16 *resp)
{
	u16 endpoint_id = cmd[2];
	u16 configure_key = cmd[3];

	kalimba_get_endpoint_info(endpoint_id, configure_key, resp);
}

static void get_source(u16 *cmd, u16 *resp)
{
	u16 endpoint_type, instance_id, channels;
	u32 handle_addr;

	endpoint_type = cmd[2];
	instance_id = cmd[3];
	channels = cmd[4];
	handle_addr = (u32)(cmd[5]) | (cmd[6] << 16);

	kalimba_get_source(endpoint_type, instance_id,
			channels, handle_addr, NULL, resp);
}

static void get_sink(u16 *cmd, u16 *resp)
{
	u16 endpoint_type, instance_id, channels;
	u32 handle_addr;

	endpoint_type = cmd[2];
	instance_id = cmd[3];
	channels = cmd[4];
	handle_addr = (u32)(cmd[5]) | (cmd[6] << 16);

	kalimba_get_sink(endpoint_type, instance_id,
			channels, handle_addr, NULL, resp);
}

static void close_source(u16 *cmd, u16 *resp)
{
	kalimba_close_source(cmd[1], &cmd[2], resp);
}

static void close_sink(u16 *cmd, u16 *resp)
{
	kalimba_close_sink(cmd[1], &cmd[2], resp);
}

static void sync_endpoints(u16 *cmd, u16 *resp)
{
	kalimba_sync_endpoint(cmd[2], cmd[3], resp);
}

static void endpoint_configure(u16 *cmd, u16 *resp)
{
	kalimba_config_endpoint(cmd[2], cmd[3], (u32)cmd[4] | cmd[5] << 16,
		resp);
}

static void connnect_endpoint(u16 *cmd, u16 *resp)
{
	kalimba_connect_endpoints(cmd[2], cmd[3], NULL, resp);
}

static void disconnect_endpoint(u16 *cmd, u16 *resp)
{
	kalimba_disconnect_endpoints(cmd[1], &cmd[2], resp);
}

static void data_produced(u16 *cmd)
{
	kalimba_data_produced(cmd[2]);
}

static void data_consumed(u16 *cmd)
{
	kalimba_data_consumed(cmd[2]);
}

static int kalimba_api(u16 *cmd, u16 *resp)
{
	u16 api_id, api_length;

	api_id = cmd[0];
	api_length = cmd[1];

	switch (api_id) {
	case CREATE_OPERATOR_REQ:
		create_operator(cmd, resp);
		break;
	case CREATE_OPERATOR_EXTENDED_REQ:
		create_operator_extended(cmd, resp);
		break;
	case START_OPERATOR_REQ:
		start_operator(cmd, resp);
		break;
	case STOP_OPERATOR_REQ:
		stop_operator(cmd, resp);
		break;
	case RESET_OPERATOR_REQ:
		reset_operator(cmd, resp);
		break;
	case DESTROY_OPERATOR_REQ:
		destroy_operator(cmd, resp);
		break;
	case OPERATOR_MESSAGE_REQ:
		operator_message(cmd, resp);
		break;
	case GET_SOURCE_REQ:
		get_source(cmd, resp);
		break;
	case GET_SINK_REQ:
		get_sink(cmd, resp);
		break;
	case CLOSE_SOURCE_REQ:
		close_source(cmd, resp);
		break;
	case CLOSE_SINK_REQ:
		close_sink(cmd, resp);
		break;
	case SYNC_ENDPOINTS_REQ:
		sync_endpoints(cmd, resp);
		break;
	case ENDPOINT_CONFIGURE_REQ:
		endpoint_configure(cmd, resp);
		break;
	case ENDPOINT_GET_INFO_REQ:
		endpoint_get_info(cmd, resp);
		break;
	case CONNECT_REQ:
		connnect_endpoint(cmd, resp);
		break;
	case DISCONNECT_REQ:
		disconnect_endpoint(cmd, resp);
		break;
	case GET_VERSION_ID_REQ:
		get_version_id(cmd, resp);
		break;
	case GET_CAPID_LIST_REQ:
		get_capid_list(cmd, resp);
		break;
	case GET_OPID_LIST_REQ:
		get_opid_list(cmd, resp);
		break;
	case GET_CONNECTION_LIST_REQ:
		get_connection_list(cmd, resp);
		break;
	case CAPABILITY_CODE_DRAM_ADDR_SET_REQ:
		capability_code_dram_addr_set(cmd, resp);
		break;
	case CAPABILITY_CODE_DRAM_ADDR_CLEAR_REQ:
		capability_code_dram_addr_clear(cmd, resp);
		break;
	case DATA_PRODUCED:
		data_produced(cmd);
		break;
	case DATA_CONSUMED:
		data_consumed(cmd);
		break;
	}
	return 0;
}

static long debug_ioctl(struct file *filp,
		unsigned int cmd, unsigned long arg)
{
	u16 *api_cmd;
	u16 api_cmd_length;
	u16 api_resp_length;
	long ret;
	u32 start_addr;
	u32 size;
	void *data;
	u16 resp[64];

	if (_IOC_TYPE(cmd) != KALIMBA_IOC_MAGIC)
		return -EINVAL;
	if (_IOC_NR(cmd) > KALIMBA_IOC_MAXNR)
		return -EINVAL;

	switch (cmd) {
	case IOCTL_KALIMBA_WRITE_PM:
	case IOCTL_KALIMBA_READ_PM:
	case IOCTL_KALIMBA_WRITE_DM:
	case IOCTL_KALIMBA_READ_DM:
	case IOCTL_KALIMBA_RUN_PM:
	case IOCTL_KALIMBA_STOP_PM:
	case IOCTL_KALIMBA_RESUME_PM:
	case IOCTL_KALIMBA_DUMP_BOOTCODE:
	case IOCTL_KALIMBA_DOWNLOAD_BOOTCODE:
		return firmware_ioctl(debug_data->dev, cmd, arg);
	case IOCTL_KALIMBA_API:
		get_user(api_cmd_length, (u16 __user *)(arg + 2));
		api_cmd = kmalloc(api_cmd_length * 2 + 4, GFP_KERNEL);
		if (!api_cmd)
			return -ENOMEM;
		if (copy_from_user(api_cmd, (u32 __user *)arg,
			api_cmd_length * 2 + 4)) {
			kfree(api_cmd);
			return -EINVAL;
		}
		ret = kalimba_api(api_cmd, resp);
		if (api_cmd[0] != DATA_PRODUCED &&
			api_cmd[0] != DATA_CONSUMED) {
			api_resp_length = resp[1] * 2 + 4;
			if (copy_to_user((u32 __user *)arg, resp,
				api_resp_length))
				ret = -EINVAL;
		}
		kfree(api_cmd);
		return ret;
	case IOCTL_KALIMBA_SETUP_AUDIO_UNIT:
		return setup_audio_unit(arg);
	case IOCTL_KALIMBA_START_AUDIO_UNIT:
		start_audio_unit(arg);
		return 0;
	case IOCTL_KALIMBA_STOP_AUDIO_UNIT:
		stop_audio_unit(arg);
		return 0;
	case IOCTL_KALIMBA_RELEASE_AUDIO_UNIT:
		release_audio_unit(arg);
		return 0;
	case IOCTL_KALIMBA_DRAM_ALLOC:
		/* Length in 32-bit words */
		ret = buff_alloc(debug_data->dev,
			*((u32 __user *)arg) * sizeof(u32));
		put_user(ret, (u32 __user *)arg);
		break;
	case IOCTL_KALIMBA_DRAM_FREE:
		ret = buff_free(debug_data->dev, *((u32 __user *)arg));
		put_user(ret, (u32 __user *)arg);
		break;
	case IOCTL_KALIMBA_WRITE_DRAM:
	case IOCTL_KALIMBA_DRAM_FILL:
		get_user(start_addr, (u32 __user *)arg);
		get_user(size, (u32 __user *)(arg + 4));
		data = kmalloc(size, GFP_KERNEL);
		if (!data)
			return -ENOMEM;
		if (!copy_from_user(data, (u32 __user *)(arg + 8),
				size)) {
			ret = buff_fill(debug_data->dev, start_addr,
				size, data);
		} else
			ret = -EINVAL;
		kfree(data);
		put_user(ret, (u32 __user *)arg);
		return ret;
	case IOCTL_KALIMBA_READ_DRAM:
		get_user(start_addr, (u32 __user *)arg);
		get_user(size, (u32 __user *)(arg + 4));
		data = kmalloc(size, GFP_KERNEL);
		if (!data)
			return -ENOMEM;
		ret = buff_read(debug_data->dev, start_addr, size, data);
		if (!ret) {
			if (copy_to_user((u32 __user *)(arg + 8), data,
					size))
				ret = -EINVAL;
		}
		kfree(data);
		put_user(ret, (u32 __user *)arg);
		return ret;
	default:
		dev_err(debug_data->dev, "Unknown ioctl cmd(%d).\n", cmd);
		return -EINVAL;
	}
	return 0;
}

static const struct file_operations kalimba_debug_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = debug_ioctl,
};

int debug_init(void)
{
	int ret;
	int devid;
	struct class *class;
	struct device *dev;

	class = class_create(THIS_MODULE, "kalimba-dev");
	if (IS_ERR(class)) {
		pr_err("Create device class failed.\n");
		return PTR_ERR(class);
	}

	ret = alloc_chrdev_region(&devid, 0, 1, "kalimba");
	if (ret < 0) {
		pr_err("Alloc device id failed: %d\n", ret);
		goto alloc_chrdev_region_failed;
	}

	dev = device_create(class, NULL, devid, NULL, "kalimba");
	if (IS_ERR(dev)) {
		pr_err("Create device failed.\n");
		ret = PTR_ERR(dev);
		goto device_create_failed;
	}
	debug_data = devm_kzalloc(dev, sizeof(*debug_data), GFP_KERNEL);
	if (debug_data == NULL) {
		ret = -ENOMEM;
		goto debug_data_alloc_failed;
	}

	debug_data->dev = dev;
	debug_data->dev->coherent_dma_mask = DMA_BIT_MASK(32);
	debug_data->class = class;
	debug_data->devid = devid;
	cdev_init(&debug_data->debug_cdev, &kalimba_debug_fops);
	cdev_add(&debug_data->debug_cdev, debug_data->devid, 1);

	INIT_LIST_HEAD(&debug_data->audio_unit_list);
	return 0;

debug_data_alloc_failed:
	device_destroy(class, devid);
device_create_failed:
	unregister_chrdev_region(devid, 1);
alloc_chrdev_region_failed:
	class_destroy(class);
	return ret;
}

void debug_deinit(void)
{
	cdev_del(&debug_data->debug_cdev);
	device_destroy(debug_data->class, debug_data->devid);
	unregister_chrdev_region(debug_data->devid, 1);
	class_destroy(debug_data->class);
}
