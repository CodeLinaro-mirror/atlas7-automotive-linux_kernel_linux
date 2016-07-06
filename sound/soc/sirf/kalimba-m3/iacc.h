/*
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
#ifndef _KAS_IACC_H
#define _KAS_IACC_H

#include <linux/regmap.h>

enum iacc_input_path {
	NO_USED,
	MONO_DIFF,
	STEREO_SINGLE,
	STEREO_DIGITAL,
	STEREO_LINEIN,
	MONO_LINEIN
};

int iacc_setup(int pchannels, int rchannels,
	enum iacc_input_path path, u32 SampleRate, u32 format);
void iacc_start(int playback, int channels);
void iacc_stop(int playback);
void atlas7_codec_release(void);

#ifdef CONFIG_SND_SOC_SIRF_KALIMBA_DEBUG
void debug_setup_codec_regmap(struct regmap *regmap);
#endif

#endif /* _KAS_IACC_H */
