/*
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

#ifndef _KAS_DSP_H
#define _KAS_DSP_H

#define CAPABILITY_ID_BASIC_PASSTHROUGH		0x0001
#define CAPABILITY_ID_RESAMPLER			0x0009
#define CAPABILITY_ID_MIXER			0x000A
#define CAPABILITY_ID_SPLITTER			0x0013
#define CAPABILITY_ID_CVC_RCV_NB		0x001D
#define CAPABILITY_ID_CVCHF1MIC_SEND_NB		0x001C
#define CAPABILITY_ID_CVCHF2MIC_SEND_NB		0x0020
#define CAPABILITY_ID_CVC_RCV_WB		0x001F
#define CAPABILITY_ID_CVCHF1MIC_SEND_WB		0x001E
#define CAPABILITY_ID_CVCHF2MIC_SEND_WB		0x0021
#define CAPABILITY_ID_CVC_RCV_UWB		0x0053
#define CAPABILITY_ID_CVCHF1MIC_SEND_UWB	0x0056
#define CAPABILITY_ID_CVCHF2MIC_SEND_UWB	0x0059
#define CAPABILITY_ID_DBE			0x002F
#define CAPABILITY_ID_DELAY			0x0035
#define CAPABILITY_ID_AEC_REF_1MIC		0x0040
#define CAPABILITY_ID_AEC_REF_2MIC		0x0041
#define CAPABILITY_ID_VOLUME_CONTROL		0x0048
#define CAPABILITY_ID_PEQ			0x0049
#define CAPABILITY_ID_DBE_FULLBAND_IN_OUT	0x0090
#define CAPABILITY_ID_DBE_FULLBAND_IN		0x0091
#define CAPABILITY_ID_CHANNEL_MIXER		0x0097
#define CAPABILITY_ID_SOURCE_SYNC		0x0099

/*
 * These dummy capability is used to register operator operations.
 * Also, user can use it to define their CVC related operators and ignore
 * the differences of sample rate and the number of mics. Wthin kasop_impl
 * "prepare" function will chose the right CVC capability ID according to
 * the parameters pass from user mode.
 */
#define CAPABILITY_ID_CVC_RCV_DUMMY CAPABILITY_ID_CVC_RCV_WB
#define CAPABILITY_ID_CVCHF_SEND_DUMMY CAPABILITY_ID_CVCHF1MIC_SEND_WB
#define CAPABILITY_ID_AEC_REF_DUMMY CAPABILITY_ID_AEC_REF_1MIC

#endif /* _KAS_DSP_H */
