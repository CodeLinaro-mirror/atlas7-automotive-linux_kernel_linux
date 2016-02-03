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

#ifndef _VIDEO_TYPE_H_
#define _VIDEO_TYPE_H_

/*
 * video data type
 */
#define F_MODE_RGB444		0
#define F_MODE_YUV422		1
#define F_MODE_YUV444		2
#define F_MODE_CLRMOD_MASK	3


#define F_MODE_INTERLACE	1

#define F_VIDMODE_ITU709	BIT(4)
#define F_VIDMODE_ITU601	0

#define F_VIDMODE_0_255		0
#define F_VIDMODE_16_235	BIT(5)

/* output mode only, and loaded from EEPROM */
#define F_VIDMODE_EN_UDFILT	BIT(6)

/* output mode only, and loaded from EEPROM */
#define F_VIDMODE_EN_DITHER	BIT(7)

struct _vfc_code {
	unsigned char colorfmt:2;
	unsigned char interlace:1;
	unsigned char Colorimetry:1;
	unsigned char Quantization:1;
	unsigned char UpDownFilter:1;
	unsigned char Dither:1;
};

union video_format_code {
	struct _vfc_code vfccode;
	unsigned char VFCByte;
};

#ifdef SUPPORT_EDID
struct rx_cap {
	unsigned char VALIDCEA;
	unsigned char VALIDHDMI;
	unsigned char VIDEOMODE;
	unsigned char VDOMODECOUNT;
	unsigned char IDXNATIVEVDOMODE;
	unsigned char VDOMODE[32];
	unsigned char AUDDESCOUNT;
	unsigned long IEEEOUI;
};
#endif

#define T_MODE_CCIR656		BIT(0)
#define T_MODE_SYNCEMB		BIT(1)
#define T_MODE_INDDR		BIT(2)
#define T_MODE_DEGEN		BIT(3)
#define T_MODE_SYNCGEN		BIT(4)

/*
 * Audio relate definition and macro.
 */

/* for sample clock */
#define FS_22K05		4
#define FS_44K1			0
#define FS_88K2			8
#define FS_176K4		12

#define FS_24K			6
#define FS_48K			2
#define FS_96K			10
#define FS_192K			14

#define FS_32K			3
#define FS_OTHER		1

/*Audio Enable */
#define ENABLE_SPDIF		BIT(4)
#define ENABLE_I2S_SRC3		BIT(3)
#define ENABLE_I2S_SRC2		BIT(2)
#define ENABLE_I2S_SRC1		BIT(1)
#define ENABLE_I2S_SRC0		BIT(0)

#define AUD_SWL_NOINDICATE	0x0
#define AUD_SWL_16		0x2
#define AUD_SWL_17		0xC
#define AUD_SWL_18		0x4
#define AUD_SWL_20		0xA /* for maximum 20 bit*/
#define AUD_SWL_21		0xD
#define AUD_SWL_22		0x5
#define AUD_SWL_23		0x9
#define AUD_SWL_24		0xB

/*
 * Packet and Info Frame definition and datastructure.
 */

#define VENDORSPEC_INFOFRAME_TYPE	0x01
#define AVI_INFOFRAME_TYPE		0x02
#define SPD_INFOFRAME_TYPE		0x03
#define AUDIO_INFOFRAME_TYPE		0x04
#define MPEG_INFOFRAME_TYPE		0x05

#define VENDORSPEC_INFOFRAME_VER	0x01
#define AVI_INFOFRAME_VER		0x02
#define SPD_INFOFRAME_VER		0x01
#define AUDIO_INFOFRAME_VER		0x01
#define MPEG_INFOFRAME_VER		0x01

#define VENDORSPEC_INFOFRAME_LEN	8
#define AVI_INFOFRAME_LEN		13
#define SPD_INFOFRAME_LEN		25
#define AUDIO_INFOFRAME_LEN		10
#define MPEG_INFOFRAME_LEN		10

#define ACP_PKT_LEN			9
#define ISRC1_PKT_LEN			16
#define ISRC2_PKT_LEN			16

struct video_timing {
	unsigned long VIDEOPIXELCLOCK;
	unsigned char VIC;
	unsigned char PIXELREP;
};

struct avi_info {
	unsigned char TYPE;
	unsigned char VER;
	unsigned char LEN;

	unsigned char SCAN:2;
	unsigned char BARINFO:2;
	unsigned char ACTIVE_FMT_Info_PRESENT:1;
	unsigned char COLORMODE:2;
	unsigned char FU1:1;

	unsigned char ACTIVE_FORMAT_ASPECT_RATIO:4;
	unsigned char PICTURE_ASPECT_RATIO:2;
	unsigned char COLORIMETRY:2;

	unsigned char SCALING:2;
	unsigned char FU2:6;

	unsigned char VIC:7;
	unsigned char FU3:1;

	unsigned char PIXELREPETITION:4;
	unsigned char FU4:4;

	short LN_END_TOP;
	short LN_START_BOTTOM;
	short PIX_END_LEFT;
	short PIX_START_RIGHT;
};

struct avi_pkt_byte {
	unsigned char AVI_HB[3];
	unsigned char AVI_DB[AVI_INFOFRAME_LEN];
};

union avi_info_frame {
	struct avi_info info;
	struct avi_pkt_byte pktbyte;
};

#endif
