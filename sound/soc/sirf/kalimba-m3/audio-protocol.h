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

#ifndef __AUDIO_PROTOCOL
#define __AUDIO_PROTOCOL

int audio_protocol_init(void);
int kas_create_stream(u32 stream, u32 sample_rate, u32 channles, u32 buff_addr,
	u32 buff_size, u32 period_size);
int kas_destroy_stream(u32 stream, u32 channels);
int kas_start_stream(u32 stream);
int kas_stop_stream(u32 stream);
int kas_send_raw_msg(u8 *data, u32 data_bytes, u16 *resp);
void kas_ps_region_addr_update(u32 addr);
void kas_send_data_produced(u32 stream, u32 pos);
void kas_send_license_ctrl_resp(u32 resp_len, void *data);
u32 *kas_get_m3_op_obj(const u8 *op_name, int len);
int kas_ctrl_msg(int put, u32 *op_m3, int ctrl_id, int value_idx,
	u32 value, u32 *rsp);
int audio_rpmsg_check(void);

#endif
