/*
 * CSRatlas7 USP-PCM controllers define
 *
 * Copyright (c) 2015, 2016 The Linux Foundation. All rights reserved.
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

#ifndef _KAS_USP_PCM_H
#define _KAS_USP_PCM_H

#define USP_PORTS		4

void sirf_usp_pcm_start(int port, int playback);
void sirf_usp_pcm_stop(int port, int playback);
void sirf_usp_pcm_params(int port, int playback, int channels, int rate);

#endif /*_KAS_USP_PCM_H*/
