/*
 * kailimba dsp driver for CSR SiRFAtlas7
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

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include "buffer.h"
#ifdef CONFIG_SND_SOC_SIRF_KALIMBA_DEBUG
#include "debug.h"
#endif
#include "license.h"
#include "dsp.h"
#include "ipc.h"
#include "kcm.h"
#include "kcm/kcm.h"
#include "ps.h"
#include "kerror.h"
#include "regs.h"
#include "firmware.h"

struct kalimba *kalimba;

void kalimba_msg_send_lock(void)
{
	mutex_lock(&kalimba->msg_send_mutex);
}

void kalimba_msg_send_unlock(void)
{
	mutex_unlock(&kalimba->msg_send_mutex);
}

int kalimba_create_operator(u16 capability_id, u16 *operator_id, u16 *resp)
{
	u16 msg[3] = {CREATE_OPERATOR_REQ, 1, capability_id};
	int ret;

	ret = ipc_send_msg(msg, 3, MSG_NEED_ACK | MSG_NEED_RSP, resp);
	if (ret < 0)
		return ret;
	*operator_id = resp[3];

	return 0;
}

int kalimba_create_operator_extended(u16 capability_id, u16 num_of_keys,
	u16 *msg_data, u16 *operator_id, u16 *resp)
{

	int vec_size = 3 * num_of_keys;
	int msg_size = 2 + 2 + vec_size;
	u16 *msg;
	int i, j, ret;

	msg = kmalloc_array(msg_size, sizeof(u16), GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = CREATE_OPERATOR_EXTENDED_REQ;
	msg[1] = 2 + vec_size;
	msg[2] = capability_id;
	msg[3] = num_of_keys;

	/*
	 * The keys are 16bit size and their corresponding
	 * values are 32bit size. KAS needs the 32bit value swapped.
	 */
	for (i = 0; i < num_of_keys; i++) {
		j = i * 3;
		msg[4 + j] = msg_data[j];
		msg[5 + j] = msg_data[2 + j];
		msg[6 + j] = msg_data[1 + j];
	}

	ret = ipc_send_msg(msg, msg_size,
			MSG_NEED_ACK | MSG_NEED_RSP, resp);
	kfree(msg);
	if (ret < 0)
		return ret;

	*operator_id = resp[3];

	return 0;
}

int kalimba_destroy_operator(u16 *operators_id, u16 operator_count, u16 *resp)
{
	int msg_size = 2 + operator_count;
	u16 *msg;
	int i;
	int ret;

	if (WARN_ON(operator_count < 1)) {
		pr_err("%s: The operator numbers must great than zero: %d\n",
			__func__, operator_count);
		return -EINVAL;
	}

	msg = kmalloc_array(msg_size, sizeof(u16), GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = DESTROY_OPERATOR_REQ;
	msg[1] = operator_count;
	for (i = 0; i < operator_count; i++)
		msg[2 + i] = operators_id[i];

	ret = ipc_send_msg(msg, msg_size,
			MSG_NEED_ACK | MSG_NEED_RSP, resp);
	kfree(msg);

	if (WARN_ON(resp[3] != operator_count || ret < 0)) {
		pr_err("Operator destroy failed: %d %d\n", operator_count,
			resp[3]);
		pr_err("First failure reason: %x\n", resp[4]);
		return -EINVAL;
	}
	return 0;
}

int kalimba_operator_message(u16 operator_id, u16 msg_id, int message_data_len,
	u16 *msg_data, u16 **res_msg_data, u16 *rsp_msg_len, u16 *resp)
{
	int msg_size = 2 + 2 + message_data_len;
	u16 *msg;
	int i;
	int ret;

	msg = kmalloc_array(msg_size, sizeof(u16), GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = OPERATOR_MESSAGE_REQ;
	msg[1] = 2 + message_data_len;
	msg[2] = operator_id;
	msg[3] = msg_id;

	for (i = 0; i < message_data_len; i++)
		msg[4 + i] = msg_data[i];

	ret = ipc_send_msg(msg, msg_size,
			MSG_NEED_ACK | MSG_NEED_RSP, resp);
	kfree(msg);

	if (res_msg_data != NULL && ret == 0) {
		*rsp_msg_len = resp[1] - 3;
		*res_msg_data = kmalloc_array(*rsp_msg_len,
			sizeof(u16), GFP_KERNEL);
		for (i = 0; i < *rsp_msg_len; i++)
			*res_msg_data[i] = resp[5 + i];
	}

	return ret;
}

#ifdef CONFIG_SND_SOC_SIRF_KALIMBA_KCM
void kalimba_set_master_gain(int vol)
{
	/* It's used by the "anti-clipping" driver */
	kcm_set_vol_ctrl_gain(vol);
}
#else
void kalimba_set_dbe_control(u16 mode)
{
	u16 msg[4] = {1, 1, 0, mode};
	u16 dbe_op_id;

	set_default_music_dbe_control(mode);
	kalimba_msg_send_lock();
	dbe_op_id = get_dbe_op_id(0);
	if (dbe_op_id)
		kalimba_operator_message(dbe_op_id,
			OPMSG_COMMON_SET_CONTROL, 4, msg,
			NULL, NULL, NULL);

	dbe_op_id = get_dbe_op_id(1);
	if (dbe_op_id)
		kalimba_operator_message(dbe_op_id,
			OPMSG_COMMON_SET_CONTROL, 4, msg,
			NULL, NULL, NULL);
	kalimba_msg_send_unlock();
}

void kalimba_set_dbe_params(u16 offset, int val)
{
	u16 msg[6] = {1, offset, 1, (u16)((val >> 8) & 0x0000ffff),
		(u16)((val & 0x000000ff) << 8), 0};
	u16 dbe_op_id;

	set_default_music_dbe_params(offset, val);
	kalimba_msg_send_lock();
	dbe_op_id = get_dbe_op_id(0);
	if (dbe_op_id)
		kalimba_operator_message(dbe_op_id,
			OPMSG_COMMON_SET_PARAMS, 6, msg,
			NULL, NULL, NULL);

	dbe_op_id = get_dbe_op_id(1);
	if (dbe_op_id)
		kalimba_operator_message(dbe_op_id,
			OPMSG_COMMON_SET_PARAMS, 6, msg,
			NULL, NULL, NULL);
	kalimba_msg_send_unlock();
}

void kalimba_set_delay_params(u16 offset, int val)
{
	u16 msg[6] = {1, offset, 1, (u16)((val >> 8) & 0x0000ffff),
		(u16)((val & 0x000000ff) << 8), 0};
	u16 delay_op_id;

	set_default_music_delay_params(offset, val);
	kalimba_msg_send_lock();
	delay_op_id = get_delay_op_id();
	if (delay_op_id)
		kalimba_operator_message(delay_op_id,
			OPMSG_COMMON_SET_PARAMS, 6, msg,
			NULL, NULL, NULL);
	kalimba_msg_send_unlock();
}

void kalimba_set_peq_control(u16 index, u16 mode)
{
	u16 msg[4] = {1, 1, 0, mode};
	u16 peq_op_id;

	set_default_music_peq_control(index, mode);
	kalimba_msg_send_lock();
	peq_op_id = get_peq_op_id(index);
	if (peq_op_id)
		kalimba_operator_message(peq_op_id,
			OPMSG_COMMON_SET_CONTROL, 4, msg,
			NULL, NULL, NULL);
	kalimba_msg_send_unlock();
}

void kalimba_set_peq_params(u16 index, u16 offset, int val)
{
	u16 msg[6] = {1, offset, 1, (u16)((val >> 8) & 0x0000ffff),
		(u16)((val & 0x000000ff) << 8), 0};
	u16 peq_op_id;

	set_default_music_peq_params(index, offset, val);
	kalimba_msg_send_lock();
	peq_op_id = get_peq_op_id(index);
	if (peq_op_id)
		kalimba_operator_message(peq_op_id,
			OPMSG_COMMON_SET_PARAMS, 6, msg,
			NULL, NULL, NULL);
	kalimba_msg_send_unlock();
}

void kalimba_set_channel_volume(int channel, int vol)
{
	u16 channels_id[4] = {0x10, 0x11, 0x12, 0x13};
	u32 volume_setting = (u32)(vol * 60);
	u16 msg[4] = {1, channels_id[channel], (u16)(volume_setting >> 16),
		(u16)(volume_setting & 0xffff)};
	u16 volume_control_op_id;

	set_default_volume_ctrl_volume(channel, volume_setting);
	kalimba_msg_send_lock();
	volume_control_op_id = get_volume_control_op_id();
	if (volume_control_op_id)
		kalimba_operator_message(volume_control_op_id,
			OPERATOR_MSG_VOLUME_CTRL_SET_CONTROL, 4, msg,
			NULL, NULL, NULL);
	kalimba_msg_send_unlock();
}

void kalimba_set_music_passthrough_volume(int vol)
{
	u16 passthrough_op_id;
	u16 passthrough_volume_setting = (u16)(vol * 60);

	set_default_music_passthrough_volume(passthrough_volume_setting);
	kalimba_msg_send_lock();
	passthrough_op_id = get_music_passthrough_op_id();
	if (passthrough_op_id)
		kalimba_operator_message(passthrough_op_id,
			OPERATOR_MSG_SET_PASSTHROUGH_GAIN, 1,
			&passthrough_volume_setting, NULL, NULL, NULL);
	kalimba_msg_send_unlock();
}

void kalimba_set_master_gain(int vol)
{
	u32 volume_setting = (u32)(vol * 60);
	u16 msg[4] = {1, 0x21, (u16)(volume_setting >> 16),
		(u16)(volume_setting & 0xffff)};
	u16 volume_control_op_id;

	set_default_master_volume(volume_setting);
	kalimba_msg_send_lock();
	volume_control_op_id = get_volume_control_op_id();
	if (volume_control_op_id)
		kalimba_operator_message(volume_control_op_id,
			OPERATOR_MSG_VOLUME_CTRL_SET_CONTROL, 4, msg,
			NULL, NULL, NULL);
	kalimba_msg_send_unlock();
}
EXPORT_SYMBOL(kalimba_set_master_gain);
void kalimba_set_stream_volume(int stream, int vol, int samples)
{
	int i;
	u16 mixer_op_id;
	static u16 streams_volume[MIXER_SUPPORT_STREAMS * 2];
	u16 msg[MIXER_SUPPORT_STREAMS];
	u16 msg_ramp[2];

	/* <MS_8bits> <LS_16bits> */
	msg_ramp[0] = samples >> 16;
	msg_ramp[1] = samples & 0xffff;

	streams_volume[stream] = (u16)(vol * 60);
	set_default_mixer_stream_volume(stream, streams_volume[stream]);

	for (i = 0; i < MIXER_SUPPORT_STREAMS; i++) {
		if (stream < MIXER_SUPPORT_STREAMS)
			msg[i] = streams_volume[i];
		else
			msg[i] = streams_volume[i + 3];
	}

	kalimba_msg_send_lock();
	if (stream < MIXER_SUPPORT_STREAMS)
		mixer_op_id = get_mixer_op_id(1);
	else
		mixer_op_id = get_mixer_op_id(2);
	if (mixer_op_id) {
		kalimba_operator_message(mixer_op_id,
			OPERATOR_MSG_SET_RAMP_NUM_SAMPLES,
			2, msg_ramp, NULL, NULL, NULL);
		kalimba_operator_message(mixer_op_id, OPERATOR_MSG_SET_GAINS,
			MIXER_SUPPORT_STREAMS, msg, NULL, NULL, NULL);
	}
	kalimba_msg_send_unlock();
}

void kalimba_set_stream_channel_volume(int stream, int channel, int vol,
	int samples)
{
	u16 mixer_op_id;
	u16 msg[3] = {1, 0, (u16)(vol * 60)};
	u16 msg_ramp[2];

	/* <MS_8bits> <LS_16bits> */
	msg_ramp[0] = samples >> 16;
	msg_ramp[1] = samples & 0xffff;

	set_default_mixer_stream_channel_volume(stream, channel, msg[2]);

	if (stream < MIXER_SUPPORT_STREAMS)
		msg[1] = stream * 4 + channel;
	else
		msg[1] = (stream - MIXER_SUPPORT_STREAMS) * 4 + channel;

	kalimba_msg_send_lock();
	if (stream < MIXER_SUPPORT_STREAMS)
		mixer_op_id = get_mixer_op_id(1);
	else
		mixer_op_id = get_mixer_op_id(2);
	if (mixer_op_id) {
		kalimba_operator_message(mixer_op_id,
			OPERATOR_MSG_SET_RAMP_NUM_SAMPLES,
			2, msg_ramp, NULL, NULL, NULL);
		kalimba_operator_message(mixer_op_id,
			OPERATOR_MSG_SET_CHANNEL_GAINS,
			3, msg, NULL, NULL, NULL);
	}
	kalimba_msg_send_unlock();
}
#endif

int kalimba_start_operator(u16 *operators_id, u16 operator_count, u16 *resp)
{
	int msg_size = 2 + operator_count;
	u16 *msg;
	int i, ret;

	if (WARN_ON(operator_count < 1)) {
		pr_err("%s: The operator numbers must great than zero: %d\n",
			__func__, operator_count);
		return -EINVAL;
	}

	msg = kmalloc_array(msg_size, sizeof(u16), GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = START_OPERATOR_REQ;
	msg[1] = operator_count;
	for (i = 0; i < msg[1]; i++)
		msg[2 + i] = operators_id[i];

	ret = ipc_send_msg(msg, msg_size, MSG_NEED_ACK | MSG_NEED_RSP, resp);
	kfree(msg);
	if (WARN_ON(resp[3] != operator_count || ret < 0)) {
		pr_err("Operator start failed: %d %d\n",
				operator_count, resp[3]);
		pr_err("First failure reason: %x\n", resp[4]);
		return -EINVAL;
	}

	return ret;
}

int kalimba_stop_operator(u16 *operators_id, u16 operator_count, u16 *resp)
{
	int msg_size = 2 + operator_count;
	u16 *msg;
	int i, ret;

	if (WARN_ON(operator_count < 1)) {
		pr_err("%s: The operator numbers must great than zero: %d\n",
			__func__, operator_count);
		return -EINVAL;
	}

	msg = kmalloc_array(msg_size, sizeof(u16), GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = STOP_OPERATOR_REQ;
	msg[1] = operator_count;
	for (i = 0; i < msg[1]; i++)
		msg[2 + i] = operators_id[i];

	ret = ipc_send_msg(msg, msg_size, MSG_NEED_ACK | MSG_NEED_RSP, resp);
	kfree(msg);

	if (WARN_ON(resp[3] != operator_count || ret < 0)) {
		pr_err("Operator stop failed: %d %d\n",
				operator_count, resp[3]);
		pr_err("First failure reason: %x\n", resp[4]);
		return -EINVAL;
	}
	return ret;
}

int kalimba_reset_operator(u16 *operators_id, u16 operator_count, u16 *resp)
{
	int msg_size = 2 + operator_count;
	u16 *msg;
	int i, ret;

	if (WARN_ON(operator_count < 1)) {
		pr_err("%s: The operator numbers must great than zero: %d\n",
			__func__, operator_count);
		return -EINVAL;
	}

	msg = kmalloc_array(msg_size, sizeof(u16), GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = RESET_OPERATOR_REQ;
	msg[1] = operator_count;
	for (i = 0; i < msg[1]; i++)
		msg[2 + i] = operators_id[i];

	ret = ipc_send_msg(msg, msg_size, MSG_NEED_ACK | MSG_NEED_RSP, resp);
	kfree(msg);

	if (WARN_ON(resp[3] != operator_count || ret < 0)) {
		pr_err("Operator reset failed: %d %d\n",
				operator_count, resp[3]);
		pr_err("First failure reason: %x\n", resp[4]);
		return -EINVAL;
	}
	return ret;
}

int kalimba_get_source(u16 endpoint_type, u16 instance_id, u16 channels,
	u32 handle_addr, u16 *endpoint_id, u16 *resp)
{
	int i, ret;
	u16 msg[7] = {GET_SOURCE_REQ, 5, endpoint_type,	instance_id, channels,
		handle_addr & 0xffff, handle_addr >> 16};

	ret = ipc_send_msg(msg, 7, MSG_NEED_ACK | MSG_NEED_RSP, resp);

	if (endpoint_id && ret == 0) {
		for (i = 0; i < channels; i++)
			endpoint_id[i] = resp[3 + i];
	}
	return ret;
}

int kalimba_get_sink(u16 endpoint_type, u16 instance_id, u16 channels,
		u32 handle_addr, u16 *endpoint_id, u16 *resp)
{
	int i, ret;
	u16 msg[7] = {GET_SINK_REQ, 5, endpoint_type,
		instance_id, channels, handle_addr & 0xffff, handle_addr >> 16};

	ret = ipc_send_msg(msg, 7, MSG_NEED_ACK | MSG_NEED_RSP, resp);

	if (endpoint_id && ret == 0) {
		for (i = 0; i < channels; i++)
			endpoint_id[i] = resp[3 + i];
	}
	return ret;
}

int kalimba_config_endpoint(u16 endpoint_id, u16 config_key,
		u32 config_value, u16 *resp)
{
	u16 msg[6] = {ENDPOINT_CONFIGURE_REQ, 4, endpoint_id,
		config_key, config_value & 0xffff, config_value >> 16};

	return ipc_send_msg(msg, 6, MSG_NEED_ACK | MSG_NEED_RSP, resp);
}

int kalimba_connect_endpoints(u16 source_endpoint_id, u16 sink_endpoint_id,
		u16 *connect_id, u16 *resp)
{
	u16 msg[4] = {CONNECT_REQ, 2, source_endpoint_id, sink_endpoint_id};
	int ret;

	ret = ipc_send_msg(msg, 4, MSG_NEED_ACK | MSG_NEED_RSP, resp);

	if (connect_id && ret == 0)
		*connect_id = resp[3];
	return ret;
}

int kalimba_close_source(u16 endpoint_count, u16 *endpoint_id, u16 *resp)
{
	u16 *msg;
	int ret;

	msg = kmalloc(4 + endpoint_count * 2, GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = CLOSE_SOURCE_REQ;
	msg[1] = endpoint_count;
	memcpy(&msg[2], endpoint_id, endpoint_count * 2);

	ret = ipc_send_msg(msg, 2 + endpoint_count, MSG_NEED_ACK | MSG_NEED_RSP,
		resp);
	kfree(msg);

	return ret;
}

int kalimba_close_sink(u16 endpoint_count, u16 *endpoint_id, u16 *resp)
{
	u16 *msg;
	int ret;

	msg = kmalloc(4 + endpoint_count * 2, GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = CLOSE_SINK_REQ;
	msg[1] = endpoint_count;
	memcpy(&msg[2], endpoint_id, endpoint_count * 2);

	ret = ipc_send_msg(msg, 2 + endpoint_count,
			MSG_NEED_ACK | MSG_NEED_RSP,
			resp);
	kfree(msg);

	return ret;
}

int kalimba_disconnect_endpoints(u16 connect_count, u16 *connect_id, u16 *resp)
{
	u16 *msg;
	int ret;

	msg = kmalloc(4 + connect_count * 2, GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = DISCONNECT_REQ;
	msg[1] = connect_count;
	memcpy(&msg[2], connect_id, connect_count * 2);

	ret = ipc_send_msg(msg, 2 + connect_count,
			MSG_NEED_ACK | MSG_NEED_RSP, resp);
	kfree(msg);

	return ret;
}

int kalimba_data_produced(u16 endpoint_id)
{
	u16 msg[3] = {DATA_PRODUCED, 1, endpoint_id};
	int ret;

	ret = ipc_send_msg(msg, 3, MSG_NEED_ACK, NULL);
	if (ret < 0)
		return -EKASIPC;

	return ret;
}

int kalimba_data_consumed(u16 endpoint_id)
{
	u16 msg[3] = {DATA_CONSUMED, 1, endpoint_id};
	int ret;

	ret = ipc_send_msg(msg, 3, MSG_NEED_ACK, NULL);
	if (ret < 0)
		return -EKASIPC;

	return ret;
}

int kalimba_get_version_id(u32 *version_id, u16 *resp)
{
	u16 msg[2] = {GET_VERSION_ID_REQ, 0};
	int ret;

	ret = ipc_send_msg(msg, 2,
		MSG_NEED_ACK | MSG_NEED_RSP, resp);

	if (version_id != NULL) {
		if (ret < 0)
			*version_id = -1;
		else
			*version_id = resp[3] | (resp[4] << 16);
	}

	return ret;
}

int kalimba_get_capid_list(u16 *capids, u16 *resp)
{
	int capid_num = 0;
	int i;
	u16 msg[2] = {GET_CAPID_LIST_REQ, 0};
	int ret;

	ret = ipc_send_msg(msg, 2, MSG_NEED_ACK | MSG_NEED_RSP, resp);

	capid_num = resp[1] - 1;
	if (capids && ret == 0) {
		for (i = 0; i < capid_num; i++)
			capids[i] = resp[3 + i];
	}
	return ret;
}

int kalimba_get_opid_list(u16 filter, u16 *opids, u16 *capids, u16 *resp)
{
	int num = 0;
	int i, ret;
	u16 msg[3] = {GET_OPID_LIST_REQ, 1, filter};

	ret = ipc_send_msg(msg, 3, MSG_NEED_ACK | MSG_NEED_RSP, resp);

	num = resp[1] - 1;
	if (opids && capids && ret == 0) {
		for (i = 0; i < num / 2; i++) {
			opids[i] = resp[3 + i * 2];
			capids[i] = resp[3 + i * 2 + 1];
		}
	}
	return ret;
}

int kalimba_get_connection_list(u16 source_filter, u16 sink_filter,
		u16 *connection_ids, u16 *source_ids, u16 *sink_ids, u16 *resp)
{
	int num = 0;
	int i, ret;
	u16 msg[4] = {GET_CONNECTION_LIST_REQ, 2, source_filter, sink_filter};

	ret = ipc_send_msg(msg, 4, MSG_NEED_ACK | MSG_NEED_RSP, resp);

	num = resp[1] - 1;
	if (connection_ids && source_ids && sink_ids && ret == 0) {
		for (i = 0; i < num / 3; i++) {
			connection_ids[i] = resp[3 + i * 3];
			source_ids[i] = resp[3 + i * 3 + 1];
			sink_ids[i] = resp[3 + i * 3 + 2];
		}
	}
	return ret;
}

int kalimba_sync_endpoint(u16 endpoint1, u16 endpoint2, u16 *resp)
{
	u16 msg[4] = {SYNC_ENDPOINTS_REQ, 2, endpoint1, endpoint2};

	return ipc_send_msg(msg, 4, MSG_NEED_ACK | MSG_NEED_RSP, resp);
}

int kalimba_get_endpoint_info(u16 endpoint_id, u16 configure_key, u16 *resp)
{
	u16 msg[4] = {ENDPOINT_GET_INFO_REQ, 2, endpoint_id, configure_key};

	return ipc_send_msg(msg, 4, MSG_NEED_ACK | MSG_NEED_RSP, resp);
}

int kalimba_capability_code_dram_addr_clear(u16 addr_low, u16 addr_high,
	u16 *resp)
{
	u16 msg[4] = {CAPABILITY_CODE_DRAM_ADDR_CLEAR_REQ, 2,
		addr_low, addr_high};

	return ipc_send_msg(msg, 4, MSG_NEED_ACK | MSG_NEED_RSP, resp);
}

int kalimba_capability_code_dram_addr_set(u16 addr_low, u16 addr_high,
	u16 *capids, u16 *resp)
{
	int i;
	int capid_num;
	int ret;
	u16 msg[4] = {CAPABILITY_CODE_DRAM_ADDR_CLEAR_REQ, 2,
		addr_low, addr_high};

	ret = ipc_send_msg(msg, 4, MSG_NEED_ACK | MSG_NEED_RSP, resp);

	capid_num = resp[1] - 1;
	if (capids && ret == 0) {
		for (i = 0; i < capid_num; i++)
			capids[i] = resp[3 + i];
	}
	return ret;
}

static int kalimba_dram_allocation_rsp_send(u16 addr_low, u16 addr_high)
{
	u16 msg[5] = {DRAM_ALLOCATION_RSP, 3, 0, addr_low, addr_high};

	return ipc_send_msg(msg, 5, MSG_NEED_ACK, NULL);
}

static int kalimba_dram_free_rsp_send(void)
{
	u16 msg[3] = {DRAM_FREE_RSP, 1, 0};

	return ipc_send_msg(msg, 3, MSG_NEED_ACK, NULL);
}

static int dram_allocation_req_actions(u16 message, void *priv_data, u16 *data)
{
	struct device *dev = (struct device *)priv_data;
	unsigned long dram_allocation_addr;
	int ret;

	dram_allocation_addr = buff_alloc(dev, data[0] * sizeof(u32));
	ret = kalimba_dram_allocation_rsp_send(
		(u16)(dram_allocation_addr & 0xffff),
		(u16)((dram_allocation_addr >> 16) & 0xffff));
	return ACTION_HANDLED;
}

static int dram_free_req_actions(u16 message, void *priv_data, u16 *data)
{
	struct device *dev = (struct device *)priv_data;
	unsigned long dram_allocation_addr;

	dram_allocation_addr = (data[0] & 0xffff) | (data[1] << 16);
	buff_free(dev, dram_allocation_addr);
	kalimba_dram_free_rsp_send();
	return ACTION_HANDLED;
}

void *register_kalimba_msg_action(u16 message,
		int (*handler)(u16, void *, u16 *), void *priv_data)
{
	struct kalimba_msg_action *action;

	mutex_lock(&kalimba->action_mutex);
	action = kmalloc(sizeof(struct kalimba_msg_action), GFP_KERNEL);
	action->message = message;
	action->handler = handler;
	action->priv_data = priv_data;

	list_add(&action->node, &kalimba->kalimba_msg_action_list);
	mutex_unlock(&kalimba->action_mutex);
	return action;
}

void unregister_kalimba_msg_action(void *action_id)
{
	struct kalimba_msg_action *action;

	mutex_lock(&kalimba->action_mutex);
	list_for_each_entry(action, &kalimba->kalimba_msg_action_list, node) {
		if (action == action_id) {
			list_del(&action->node);
			kfree(action);
			break;
		}
	}
	mutex_unlock(&kalimba->action_mutex);
}

static void unregister_kalimba_msg_all_actions(void)
{
	struct kalimba_msg_action *action;

	mutex_lock(&kalimba->action_mutex);
	list_for_each_entry(action, &kalimba->kalimba_msg_action_list, node) {
		list_del(&action->node);
		kfree(action);
	}
	mutex_unlock(&kalimba->action_mutex);
}

void kalimba_do_actions(u16 message, u16 *data)
{
	struct kalimba_msg_action *action;
	int ret;

	mutex_lock(&kalimba->action_mutex);
	list_for_each_entry(action, &kalimba->kalimba_msg_action_list, node) {
		if (action->message == message)
			ret = action->handler(message, action->priv_data, data);
			if (ret == ACTION_HANDLED)
				break;
	}
	mutex_unlock(&kalimba->action_mutex);
}

static ssize_t firmware_version_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	u32 fw_version;
	u16 resp[64];

	kalimba_get_version_id(&fw_version, resp);
	return sprintf(buf, "%d\n", fw_version);
}
static DEVICE_ATTR_RO(firmware_version);

static int kalimba_probe(struct platform_device *pdev)
{
	int ret;
	void *action_id;

	kalimba = devm_kzalloc(&pdev->dev, sizeof(struct kalimba),
			GFP_KERNEL);
	if (kalimba == NULL)
		return -ENOMEM;

	kalimba->clk_kas = devm_clk_get(&pdev->dev, "kas_kas");
	if (IS_ERR(kalimba->clk_kas)) {
		dev_err(&pdev->dev, "Get clock(kas) failed.\n");
		return PTR_ERR(kalimba->clk_kas);
	}
	ret = clk_prepare_enable(kalimba->clk_kas);
	if (ret) {
		dev_err(&pdev->dev, "Enable clock(kas) failed.\n");
		return ret;
	}

	kalimba->clk_audmscm = devm_clk_get(&pdev->dev, "audmscm_nocd");
	if (IS_ERR(kalimba->clk_audmscm)) {
		dev_err(&pdev->dev, "Get clock(audmscm) failed.\n");
		ret = PTR_ERR(kalimba->clk_audmscm);
		goto clk_get_audmscm_failed;
	}
	ret = clk_prepare_enable(kalimba->clk_audmscm);
	if (ret) {
		dev_err(&pdev->dev, "Enable clock(audmscm failed.\n");
		goto clk_get_audmscm_failed;
	}

	kalimba->clk_gpum = devm_clk_get(&pdev->dev, "gpum_nocd");
	if (IS_ERR(kalimba->clk_gpum)) {
		dev_err(&pdev->dev, "Get clock(gpum) failed.\n");
		ret = PTR_ERR(kalimba->clk_gpum);
		goto clk_get_gpum_failed;
	}
	ret = clk_prepare_enable(kalimba->clk_gpum);
	if (ret) {
		dev_err(&pdev->dev, "Enable clock(gpum failed.\n");
		goto clk_get_gpum_failed;
	}

	kalimba->clk_dmac2 = devm_clk_get(&pdev->dev, "dmac2");
	if (IS_ERR(kalimba->clk_dmac2)) {
		dev_err(&pdev->dev, "Get clock(dmac2) failed.\n");
		ret = PTR_ERR(kalimba->clk_dmac2);
		goto clk_get_dmac2_failed;
	}
	ret = clk_prepare_enable(kalimba->clk_dmac2);
	if (ret) {
		dev_err(&pdev->dev, "Enable clock(dmac2) failed.\n");
		goto clk_get_dmac2_failed;
	}

	ret = device_reset(&pdev->dev);
	if (ret != 0) {
		dev_err(&pdev->dev, "Reset kalimba failed: %d\n", ret);
		goto kalimba_reset_failed;
	}

	INIT_LIST_HEAD(&kalimba->kalimba_msg_action_list);
	mutex_init(&kalimba->msg_send_mutex);
	mutex_init(&kalimba->action_mutex);
	platform_set_drvdata(pdev, kalimba);

	ps_init();

	kcoredump_init();
#ifdef CONFIG_SND_SOC_SIRF_KALIMBA_DEBUG
	ret = debug_init();
	if (ret != 0) {
		dev_err(&pdev->dev, "Initialize debug interface failed.\n");
		goto kalimba_reset_failed;
	}
#endif

	ret = license_init();
	if (ret != 0) {
		dev_err(&pdev->dev, "Initialzie license interface failed.\n");
		goto kalimba_reset_failed;
	}

	action_id = register_kalimba_msg_action(DRAM_ALLOCATION_REQ,
		dram_allocation_req_actions, &pdev->dev);
	if (IS_ERR(action_id)) {
		ret = PTR_ERR(action_id);
		goto kalimba_reset_failed;
	}
	action_id = register_kalimba_msg_action(DRAM_FREE_REQ,
		dram_free_req_actions, &pdev->dev);
	if (IS_ERR(action_id)) {
		ret = PTR_ERR(action_id);
		goto register_dma_free_req_action_failed;
	}

	device_create_file(&pdev->dev, &dev_attr_firmware_version);
	return 0;

register_dma_free_req_action_failed:
	unregister_kalimba_msg_all_actions();
kalimba_reset_failed:
	clk_disable_unprepare(kalimba->clk_dmac2);
clk_get_dmac2_failed:
	clk_disable_unprepare(kalimba->clk_gpum);
clk_get_gpum_failed:
	clk_disable_unprepare(kalimba->clk_audmscm);
clk_get_audmscm_failed:
	clk_disable_unprepare(kalimba->clk_kas);
	return ret;
}

static int kalimba_remove(struct platform_device *pdev)
{
	struct kalimba *kalimba = platform_get_drvdata(pdev);

	device_remove_file(&pdev->dev, &dev_attr_firmware_version);
	unregister_kalimba_msg_all_actions();
	clk_disable_unprepare(kalimba->clk_dmac2);
	clk_disable_unprepare(kalimba->clk_gpum);
	clk_disable_unprepare(kalimba->clk_audmscm);
	clk_disable_unprepare(kalimba->clk_kas);

#ifdef CONFIG_SND_SOC_SIRF_KALIMBA_DEBUG
	debug_deinit();
#endif
	license_deinit();
	return 0;
}

static const struct of_device_id kalimba_of_match[] = {
	{ .compatible = "csr,kalimba", },
	{}
};
MODULE_DEVICE_TABLE(of, kalimba_of_match);

static struct platform_driver kalimba_driver = {
	.driver = {
		.name = "kalimba",
		.of_match_table = kalimba_of_match,
	},
	.probe = kalimba_probe,
	.remove = kalimba_remove,
};

module_platform_driver(kalimba_driver);

MODULE_DESCRIPTION("SiRF SoC Kalimba DSP driver");
MODULE_LICENSE("GPL v2");
