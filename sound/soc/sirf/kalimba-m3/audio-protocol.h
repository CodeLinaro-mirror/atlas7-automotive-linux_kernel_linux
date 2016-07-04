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
void kas_start_stream(u32 stream, u32 sample_rate, u32 channles, u32 buff_addr,
	u32 buff_size, u32 period_size);
void kas_stop_stream(u32 stream);

#endif
