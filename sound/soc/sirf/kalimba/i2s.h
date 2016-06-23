/*
 * SiRF I2S controllers define
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

#ifndef _KAS_I2S_H
#define _KAS_I2S_H

struct i2s_params {
	int channels;
	int rate;
	char slave;
	char playback;
};

void sirf_i2s_start(int playback);
void sirf_i2s_stop(int playback);
void sirf_i2s_params(int channels, int rate, int slave);
int sirf_i2s_params_adv(struct i2s_params *param);
void sirf_i2s_set_sysclk(int freq);

#endif /*_KAS_I2S_H*/
