/*
 * kalimba debug & development interface
 * TODO: This module is for temporary debugging purpose, will be removed.
 *
 * Copyright (c) 2015 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/cdev.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "debug.h"
#include "dsp.h"
#include "firmware.h"
#include "i2s.h"
#include "iacc.h"
#include "ipc.h"
#include "regs.h"

struct audio_unit {
	struct list_head node;
	unsigned long id;
	dma_addr_t buff_phy_addr;
	unsigned long buff_length;
	int pchannels;
	int rchannels;
	u32 type;
};

struct buff_node {
	struct list_head node;
	dma_addr_t phy_addr;
	void *virt_addr;
	unsigned long size;
};

struct kalimba_debug_data {
	struct regmap *regmap;
	struct cdev kalimba_cdev;
	int devid;
	struct class *class;
	struct device *dev;
	struct ipc_data *ipc_data;
	unsigned long audio_unit_id;
	struct list_head audio_unit_list;
	struct list_head buff_list;
};

static unsigned long buff_alloc(struct kalimba_debug_data *debug_data,
		unsigned long size)
{
	struct buff_node *buff;

	buff = kmalloc(sizeof(struct buff_node), GFP_KERNEL);
	if (buff == NULL)
		return -ENOMEM;
	buff->virt_addr = dma_alloc_coherent(debug_data->dev, size * 4,
			&buff->phy_addr, GFP_KERNEL);
	if (buff->virt_addr == NULL) {
		kfree(buff);
		dev_err(debug_data->dev, "Alloc dram failed.\n");
		return -ENOMEM;
	}
	memset(buff->virt_addr, 0, size * 4);
	buff->size = size * 4;
	list_add(&buff->node, &debug_data->buff_list);
	dev_info(debug_data->dev,
			"Alloc dram success, phy addr: %p, virt addr: %p\n",
			(void *)(buff->phy_addr), buff->virt_addr);
	return (unsigned long)(buff->phy_addr);
}

static int buff_free(struct kalimba_debug_data *debug_data,
		unsigned long phy_addr)
{
	struct buff_node *buff;

	list_for_each_entry(buff, &debug_data->buff_list, node) {
		if (buff->phy_addr <= phy_addr
				&& (buff->phy_addr + buff->size) > phy_addr) {
			dma_free_coherent(debug_data->dev, buff->size,
					buff->virt_addr, buff->phy_addr);
			list_del(&buff->node);
			dev_info(debug_data->dev, "Free dram success\n");
			return 0;
		}
	}
	dev_err(debug_data->dev, "Free dram failed. phy addr: %lu\n", phy_addr);
	return -EINVAL;
}

static int buff_fill(struct kalimba_debug_data *debug_data,
		unsigned long start_addr,
		unsigned long size, void *data)
{
	struct buff_node *buff;
	unsigned long virt_start_addr;

	list_for_each_entry(buff, &debug_data->buff_list, node) {
		if (buff->phy_addr <= start_addr &&
			(start_addr + size) <= (buff->phy_addr + buff->size)) {
			virt_start_addr = (unsigned long)buff->virt_addr +
				(start_addr - buff->phy_addr);
			memcpy((void *)virt_start_addr, data, size);
			dev_info(debug_data->dev, "Write dram success\n");
			return 0;
		}
	}
	dev_err(debug_data->dev,
		"The address and size is over range of buffer\n");
	return -EINVAL;
}

static int buff_read(struct kalimba_debug_data *debug_data,
		unsigned long start_addr,
		unsigned long size, void *data)
{
	struct buff_node *buff;
	unsigned long virt_start_addr;

	if (data == NULL)
		return -EINVAL;

	list_for_each_entry(buff, &debug_data->buff_list, node) {
		if (buff->phy_addr <= start_addr &&
			(start_addr + size) <= (buff->phy_addr + buff->size)) {
			virt_start_addr = (unsigned long)buff->virt_addr +
				(start_addr - buff->phy_addr);
			memcpy(data, (void *)virt_start_addr, size);
			dev_info(debug_data->dev, "Read dram success\n");
			return 0;
		}
	}
	dev_err(debug_data->dev,
		"The address and size is over range of buffer\n");
	return -EINVAL;
}

static int insert_audio_unit_into_list(struct kalimba_debug_data *debug_data,
		unsigned long addr, u32 type,
		unsigned long buff_length, int pchannels, int rchannels)
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
	list_add(&audio_unit->node, &debug_data->audio_unit_list);
	return debug_data->audio_unit_id;
}

static int setup_audio_unit(struct kalimba_debug_data *debug_data,
		unsigned long arg)
{
	u32 Type;
	u32 TypeConf;
	u32 BufferLength;
	u32 SampleFormat;
	u32 SampleRate;
	u32 Volume;
	void *buff_addr;
	int ret;
	int pchannels = 2;
	int rchannels = 1;
	int i2s_slave_mode = 0;
	enum iacc_input_path path = NO_USED;

	get_user(Type, (u32 __user *)arg);
	get_user(TypeConf, (u32 __user *)(arg + 4));
	get_user(BufferLength, (u32 __user *)(arg + 8));
	get_user(SampleFormat, (u32 __user *)(arg + 12));
	get_user(SampleRate, (u32 __user *)(arg + 16));
	get_user(Volume, (u32 __user *)(arg + 20));

	if ((Type == CTRL_DEVICE_TYPE_I2S) && (TypeConf & 0x1))
		pchannels = 6;

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

	buff_addr = (void *)buff_alloc(debug_data, BufferLength / 4);
	if (IS_ERR(buff_addr)) {
		ret = PTR_ERR(buff_addr);
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
	default:
		ret = -EINVAL;
		goto out;
	}

	ret = insert_audio_unit_into_list(debug_data, (u32)buff_addr, Type,
			BufferLength, pchannels, rchannels);
	if (ret < 0)
		buff_free(debug_data, (u32)buff_addr);
out:
	if (ret < 0) {
		ret = 0x200A;
		put_user(ret, (u32 __user *)arg);
	} else {
		u32 status = 0;

		put_user(status, (u32 __user *)arg);
		put_user((u32)ret, (u32 __user *)arg + 1);
		put_user((u32)buff_addr, (u32 __user *)arg + 2);
	}
	return ret;
}

static void start_audio_unit(struct kalimba_debug_data *debug_data,
		unsigned long arg)
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
				sirf_i2s_start(playback,
					(u32)(audio_unit->buff_phy_addr),
					audio_unit->buff_length);
				ret = 0;
				break;
			case CTRL_DEVICE_TYPE_IACC:
				channels = playback ? audio_unit->pchannels
					: audio_unit->rchannels;
				debug_iacc_start(playback, channels,
					(u32)(audio_unit->buff_phy_addr),
					audio_unit->buff_length);
				ret = 0;
				break;
			default:
				break;
			}
		}
	}

	put_user((u32)ret, (u32 __user *)arg);
}

static void stop_audio_unit(struct kalimba_debug_data *debug_data,
		unsigned long arg)
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
			default:
				break;
			}
		}
	}

	put_user((u32)ret, (u32 __user *)arg);
}

static void release_audio_unit(struct kalimba_debug_data *debug_data,
		unsigned long arg)
{
	struct audio_unit *audio_unit;
	u32 audio_unit_id;
	int ret = -1;

	get_user(audio_unit_id, (u32 __user *)arg);

	list_for_each_entry(audio_unit, &debug_data->audio_unit_list, node) {
		if (audio_unit->id == audio_unit_id) {
			switch (audio_unit->type) {
			case CTRL_DEVICE_TYPE_I2S:
				ret = 0;
				break;
			case CTRL_DEVICE_TYPE_IACC:
				atlas7_codec_release();
				ret = 0;
				break;
			default:
				break;
			}
			buff_free(debug_data, audio_unit->buff_phy_addr);
		}
	}
	put_user((u32)ret, (u32 __user *)arg);
}

static void create_operator(struct ipc_data *ipc_data, u16 *cmd)
{
	u16 operator_id;
	int i;
	u16 resp;

	ipc_create_operator(ipc_data, cmd[2], &operator_id);
	for (i = 0; i < 64; i++) {
		resp = ipc_data->payload[i];
		cmd[i] = resp;
		put_user(resp, &cmd[i]);
	}
}

static void start_operator(struct ipc_data *ipc_data, u16 *cmd)
{
	u16 api_length = cmd[1];
	int i;
	u16 resp;

	ipc_start_operator(ipc_data, &cmd[2], api_length);
	for (i = 0; i < 64; i++) {
		resp = ipc_data->payload[i];
		cmd[i] = resp;
	}
}

static void stop_operator(struct ipc_data *ipc_data, u16 *cmd)
{
	u16 api_length = cmd[1];
	int i;
	u16 resp;

	ipc_stop_operator(ipc_data, &cmd[2], api_length);
	for (i = 0; i < 64; i++) {
		resp = ipc_data->payload[i];
		cmd[i] = resp;
	}
}

static void reset_operator(struct ipc_data *ipc_data, u16 *cmd)
{
	u16 api_length = cmd[1];
	int i;
	u16 resp;

	ipc_reset_operator(ipc_data, &cmd[2], api_length);
	for (i = 0; i < 64; i++) {
		resp = ipc_data->payload[i];
		cmd[i] = resp;
	}
}

static void destroy_operator(struct ipc_data *ipc_data, u16 *cmd)
{
	u16 api_length = cmd[1];
	int i;
	u16 resp;

	ipc_destroy_operator(ipc_data, &cmd[2], api_length);
	for (i = 0; i < 64; i++) {
		resp = ipc_data->payload[i];
		cmd[i] = resp;
	}
}

static void operator_message(struct ipc_data *ipc_data, u16 *cmd)
{
	u16 api_length = cmd[1];
	int i;
	u16 resp;

	ipc_operator_message(ipc_data, cmd[2], cmd[3],
			api_length - 2, &cmd[4], NULL, NULL);
	for (i = 0; i < 64; i++) {
		resp = ipc_data->payload[i];
		cmd[i] = resp;
	}
}

static void get_version_id(struct ipc_data *ipc_data, u16 *cmd)
{
	int i;
	u16 resp;

	ipc_get_version_id(ipc_data, NULL);
	for (i = 0; i < 64; i++) {
		resp = ipc_data->payload[i];
		cmd[i] = resp;
	}
}

static void get_capid_list(struct ipc_data *ipc_data, u16 *cmd)
{
	int i;
	u16 resp;

	ipc_get_capid_list(ipc_data, NULL);
	for (i = 0; i < 64; i++) {
		resp = ipc_data->payload[i];
		cmd[i] = resp;
	}
}

static void get_opid_list(struct ipc_data *ipc_data, u16 *cmd)
{
	int i;
	u16 resp;
	u16 filter = cmd[2];

	ipc_get_opid_list(ipc_data, filter, NULL, NULL);
	for (i = 0; i < 64; i++) {
		resp = ipc_data->payload[i];
		cmd[i] = resp;
	}
}

static void get_connection_list(struct ipc_data *ipc_data, u16 *cmd)
{
	int i;
	u16 resp;
	u16 source_filter = cmd[2];
	u16 sink_filter = cmd[3];

	ipc_get_connection_list(ipc_data, source_filter,
			sink_filter, NULL, NULL, NULL);
	for (i = 0; i < 64; i++) {
		resp = ipc_data->payload[i];
		cmd[i] = resp;
	}
}

static void endpoint_get_info(struct ipc_data *ipc_data, u16 *cmd)
{
	int i;
	u16 resp;
	u16 endpoint_id = cmd[2];
	u16 configure_key = cmd[3];

	ipc_get_endpoint_info(ipc_data, endpoint_id, configure_key);
	for (i = 0; i < 64; i++) {
		resp = ipc_data->payload[i];
		cmd[i] = resp;
	}
}

static void get_source(struct ipc_data *ipc_data, u16 *cmd)
{
	int i;
	u16 resp;
	u16 endpoint_type, instance_id, channels;
	u32 handle_addr;

	endpoint_type = cmd[2];
	instance_id = cmd[3];
	channels = cmd[4];
	handle_addr = (u32)(cmd[5]) | (cmd[6] << 16);

	ipc_get_source(ipc_data, endpoint_type, instance_id,
			channels, handle_addr, NULL);
	for (i = 0; i < 64; i++) {
		resp = ipc_data->payload[i];
		cmd[i] = resp;
	}
}

static void get_sink(struct ipc_data *ipc_data, u16 *cmd)
{
	int i;
	u16 resp;
	u16 endpoint_type, instance_id, channels;
	u32 handle_addr;

	endpoint_type = cmd[2];
	instance_id = cmd[3];
	channels = cmd[4];
	handle_addr = (u32)(cmd[5]) | (cmd[6] << 16);

	ipc_get_sink(ipc_data, endpoint_type, instance_id,
			channels, handle_addr, NULL);
	for (i = 0; i < 64; i++) {
		resp = ipc_data->payload[i];
		cmd[i] = resp;
	}
}

static void close_source(struct ipc_data *ipc_data, u16 *cmd)
{
	int i;
	u16 resp;

	ipc_close_source(ipc_data, cmd[1], &cmd[2]);
	for (i = 0; i < 64; i++) {
		resp = ipc_data->payload[i];
		cmd[i] = resp;
	}
}

static void close_sink(struct ipc_data *ipc_data, u16 *cmd)
{
	int i;
	u16 resp;

	ipc_close_sink(ipc_data, cmd[1], &cmd[2]);
	for (i = 0; i < 64; i++) {
		resp = ipc_data->payload[i];
		cmd[i] = resp;
	}
}

static void sync_endpoints(struct ipc_data *ipc_data, u16 *cmd)
{
	int i;
	u16 resp;

	ipc_sync_endpoint(ipc_data, cmd[2], cmd[3]);
	for (i = 0; i < 64; i++) {
		resp = ipc_data->payload[i];
		cmd[i] = resp;
	}
}

static void endpoint_configure(struct ipc_data *ipc_data, u16 *cmd)
{
	int i;
	u16 resp;

	ipc_config_endpoint(ipc_data, cmd[2], cmd[3],
			(u32)cmd[4] | cmd[5] << 16);
	for (i = 0; i < 64; i++) {
		resp = ipc_data->payload[i];
		cmd[i] = resp;
	}
}

static void connnect_endpoint(struct ipc_data *ipc_data, u16 *cmd)
{
	int i;
	u16 resp;

	ipc_connect_endpoints(ipc_data, cmd[2], cmd[3], NULL);
	for (i = 0; i < 64; i++) {
		resp = ipc_data->payload[i];
		cmd[i] = resp;
	}
}

static void disconnect_endpoint(struct ipc_data *ipc_data, u16 *cmd)
{
	int i;
	u16 resp;

	ipc_disconnect_endpoints(ipc_data, cmd[1], &cmd[2]);
	for (i = 0; i < 64; i++) {
		resp = ipc_data->payload[i];
		cmd[i] = resp;
	}
}

static void data_produced(struct ipc_data *ipc_data, u16 *cmd)
{
	ipc_data_produced(ipc_data, cmd[2]);
}

static void data_consumed(struct ipc_data *ipc_data, u16 *cmd)
{
	ipc_data_consumed(ipc_data, cmd[2]);
}

int kalimba_api(struct ipc_data *ipc_data, u16 *cmd)
{
	u16 api_id, api_length;

	api_id = cmd[0];
	api_length = cmd[1];

	switch (api_id) {
	case CREATE_OPERATOR_REQ:
		create_operator(ipc_data, cmd);
		break;
	case START_OPERATOR_REQ:
		start_operator(ipc_data, cmd);
		break;
	case STOP_OPERATOR_REQ:
		stop_operator(ipc_data, cmd);
		break;
	case RESET_OPERATOR_REQ:
		reset_operator(ipc_data, cmd);
		break;
	case DESTROY_OPERATOR_REQ:
		destroy_operator(ipc_data, cmd);
		break;
	case OPERATOR_MESSAGE_REQ:
		operator_message(ipc_data, cmd);
		break;
	case GET_SOURCE_REQ:
		get_source(ipc_data, cmd);
		break;
	case GET_SINK_REQ:
		get_sink(ipc_data, cmd);
		break;
	case CLOSE_SOURCE_REQ:
		close_source(ipc_data, cmd);
		break;
	case CLOSE_SINK_REQ:
		close_sink(ipc_data, cmd);
		break;
	case SYNC_ENDPOINTS_REQ:
		sync_endpoints(ipc_data, cmd);
		break;
	case ENDPOINT_CONFIGURE_REQ:
		endpoint_configure(ipc_data, cmd);
		break;
	case ENDPOINT_GET_INFO_REQ:
		endpoint_get_info(ipc_data, cmd);
		break;
	case CONNECT_REQ:
		connnect_endpoint(ipc_data, cmd);
		break;
	case DISCONNECT_REQ:
		disconnect_endpoint(ipc_data, cmd);
		break;
	case GET_VERSION_ID_REQ:
		get_version_id(ipc_data, cmd);
		break;
	case GET_CAPID_LIST_REQ:
		get_capid_list(ipc_data, cmd);
		break;
	case GET_OPID_LIST_REQ:
		get_opid_list(ipc_data, cmd);
		break;
	case GET_CONNECTION_LIST_REQ:
		get_connection_list(ipc_data, cmd);
		break;
	case DATA_PRODUCED:
		data_produced(ipc_data, cmd);
		break;
	case DATA_CONSUMED:
		data_consumed(ipc_data, cmd);
		break;
	}
	return 0;
}

static int debug_open(struct inode *inode, struct file *file)
{
	struct kalimba_debug_data *debug_data = container_of(inode->i_cdev,
			struct kalimba_debug_data, kalimba_cdev);
	file->private_data = debug_data;
	return 0;
}

static long debug_ioctl(struct file *filp,
		unsigned int cmd, unsigned long arg)
{
	struct kalimba_debug_data *debug_data = filp->private_data;
	u16 *api_cmd;
	u16 api_cmd_length;
	u16 api_resp_length;
	long ret;
	u32 start_addr;
	u32 size;
	void *data;

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
		return firmware_ioctl(debug_data->regmap, debug_data->dev,
			cmd, arg);
	case IOCTL_KALIMBA_API:
		api_cmd = kmalloc(256, GFP_KERNEL);
		get_user(api_cmd_length, (u16 __user *)(arg + 2));
		if (copy_from_user(api_cmd, (u32 __user *)arg,
			api_cmd_length * 2 + 4))
			ret = -EINVAL;
		else {
			ret = kalimba_api(debug_data->ipc_data, api_cmd);
			api_resp_length = api_cmd[1] * 2 + 4;
			if (copy_to_user((u32 __user *)arg, api_cmd,
				api_resp_length))
				ret = -EINVAL;
		}
		kfree(api_cmd);
		return ret;
	case IOCTL_KALIMBA_SETUP_AUDIO_UNIT:
		return setup_audio_unit(debug_data, arg);
	case IOCTL_KALIMBA_START_AUDIO_UNIT:
		start_audio_unit(debug_data, arg);
		return 0;
	case IOCTL_KALIMBA_STOP_AUDIO_UNIT:
		stop_audio_unit(debug_data, arg);
		return 0;
	case IOCTL_KALIMBA_RELEASE_AUDIO_UNIT:
		release_audio_unit(debug_data, arg);
		return 0;
	case IOCTL_KALIMBA_DRAM_ALLOC:
		ret = buff_alloc(debug_data, *((u32 __user *)arg));
		put_user(ret, (u32 __user *)arg);
		break;
	case IOCTL_KALIMBA_DRAM_FREE:
		ret = buff_free(debug_data, *((u32 __user *)arg));
		put_user(ret, (u32 __user *)arg);
		break;
	case IOCTL_KALIMBA_WRITE_DRAM:
	case IOCTL_KALIMBA_DRAM_FILL:
		get_user(start_addr, (u32 __user *)arg);
		get_user(size, (u32 __user *)(arg + 4));
		data = kmalloc(size, GFP_KERNEL);
		if (!copy_from_user(data, (u32 __user *)(arg + 8),
				size)) {
			ret = buff_fill(debug_data,
			start_addr, size, data);
		} else
			ret = -EINVAL;
		kfree(data);
		put_user(ret, (u32 __user *)arg);
		return ret;
	case IOCTL_KALIMBA_READ_DRAM:
		get_user(start_addr, (u32 __user *)arg);
		get_user(size, (u32 __user *)(arg + 4));
		data = kmalloc(size, GFP_KERNEL);
		ret = buff_read(debug_data, start_addr, size, data);
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
	.open           = debug_open,
	.unlocked_ioctl = debug_ioctl,
};

int debug_init(struct platform_device *pdev)
{
	struct kalimba_debug_data *debug_data;
	struct kalimba *kalimba = platform_get_drvdata(pdev);
	int ret;

	debug_data = devm_kzalloc(&pdev->dev, sizeof(*debug_data), GFP_KERNEL);
	if (debug_data == NULL)
		return -ENOMEM;

	debug_data->class = class_create(THIS_MODULE, "kalimba-dev");
	if (IS_ERR(debug_data->class)) {
		dev_err(&pdev->dev, "Create device class failed.\n");
		return PTR_ERR(debug_data->class);
	}

	ret = alloc_chrdev_region(&debug_data->devid, 0, 1, "kalimba");
	if (ret < 0) {
		dev_err(&pdev->dev, "Alloc device id failed: %d\n", ret);
		goto alloc_chrdev_region_failed;
	}

	debug_data->dev = device_create(debug_data->class, NULL,
			debug_data->devid, NULL, "kalimba");
	if (IS_ERR(debug_data->dev)) {
		dev_err(&pdev->dev, "Create device failed.\n");
		ret = PTR_ERR(debug_data->dev);
		goto device_create_failed;
	}
	debug_data->dev->coherent_dma_mask = DMA_BIT_MASK(32);
	cdev_init(&debug_data->kalimba_cdev, &kalimba_debug_fops);
	cdev_add(&debug_data->kalimba_cdev, debug_data->devid, 1);

	debug_data->regmap = kalimba->regmap;
	dev_set_drvdata(debug_data->dev, debug_data);
	kalimba->debug_dev = debug_data->dev;
	debug_data->ipc_data = kalimba->ipc_data;
	INIT_LIST_HEAD(&debug_data->audio_unit_list);
	INIT_LIST_HEAD(&debug_data->buff_list);
	return 0;

device_create_failed:
	unregister_chrdev_region(debug_data->devid, 1);
alloc_chrdev_region_failed:
	class_destroy(debug_data->class);
	return ret;
}

void debug_deinit(struct platform_device *pdev)
{
	struct kalimba *kalimba = platform_get_drvdata(pdev);
	struct kalimba_debug_data *debug_data = dev_get_drvdata(
			kalimba->debug_dev);

	cdev_del(&debug_data->kalimba_cdev);
	device_destroy(debug_data->class, debug_data->devid);
	unregister_chrdev_region(debug_data->devid, 1);
	class_destroy(debug_data->class);
}
