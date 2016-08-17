/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
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
#include <sound/pcm_params.h>
#include "kasobj.h"
#include "../dsp.h"

#if SNDRV_PCM_RATE_5512 != 1 << 0 || SNDRV_PCM_RATE_192000 != 1 << 12
#error "Rate definition changed in kernel!"
#endif

#define KCM_RATES (SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000)
#define KCM_FORMATS (SNDRV_PCM_FMTBIT_S16_LE)

#ifdef CONFIG_SND_SOC_SIRF_KALIMBA_KCM_CODEC_I2S
#define I2S_CODEC_ENABLE 1
#define IACC_CODEC_ENABLE 0
#define CODEC_TYPE ("i2s")
#define SI_CODEC ("si_i2s")
#define SO_CODEC ("so_i2s")
#define SO_CODEC_2MIC ("so_i2s_2mic")
#define SO_CODEC_CH_MIN 2
#define SO_CODEC_CH_MAX 2
#else
#define I2S_CODEC_ENABLE 0
#define IACC_CODEC_ENABLE 1
#define CODEC_TYPE ("iacc")
#define SI_CODEC ("si_iacc")
#define SO_CODEC ("so_iacc")
#define SO_CODEC_2MIC ("so_iacc_2mic")
#define SO_CODEC_CH_MIN 1
#define SO_CODEC_CH_MAX 2
#endif

#define __S(str)	{ .s = str }

#include "kasdb-ctrls.h"
#include "kasdb-hw.c"
#include "fe.c"
#include "op.c"
#include "link.c"
#include "chain.c"

void __init kasdb_load_database(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(codec); i++)
		kasobj_add(&codec[i], kasdb_elm_codec);
	for (i = 0; i < ARRAY_SIZE(hw); i++)
		kasobj_add(&hw[i], kasdb_elm_hw);
	for (i = 0; i < ARRAY_SIZE(fe); i++)
		kasobj_add(&fe[i], kasdb_elm_fe);
	for (i = 0; i < ARRAY_SIZE(op); i++)
		kasobj_add(&op[i], kasdb_elm_op);
	for (i = 0; i < ARRAY_SIZE(link); i++)
		kasobj_add(&link[i], kasdb_elm_link);
	for (i = 0; i < ARRAY_SIZE(chain); i++)
		kasobj_add(&chain[i], kasdb_elm_chain);
}
