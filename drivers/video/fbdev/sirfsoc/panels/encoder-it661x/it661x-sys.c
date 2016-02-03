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

#include <linux/fb.h>
#include <linux/delay.h>
#include "hdmitx.h"

/*
 * input and default output color setting.
 * to indicating this selection for color mode.
 */
#define HDMITX_INPUT_SIGNAL_TYPE 0  /* by default */

/* for 16bit sync embedded, color is YUV422 only */
/* #define HDMITX_INPUT_SIGNAL_TYPE T_MODE_SYNCEMB */

/* for 8bit sync embedded, color is YUV422 only */
/* #define HDMITX_INPUT_SIGNAL_TYPE (T_MODE_SYNCEMB | T_MODE_CCIR656) */

/* for DDR input */
/* #define HDMITX_INPUT_SIGNAL_TYPE T_MODE_INDDR */

/* for DE generating by input sync. */
/* #define HDMITX_INPUT_SIGNAL_TYPE T_MODE_DEGEN */

/* for DE and sync regenerating by input sync. */
/* #define HDMITX_INPUT_SIGNAL_TTYP (T_MODE_DEGEN| T_MODE_SYNCGEN) */

int ginput_color_mode;
int goutput_color_mode;
int ginput_signal_type = HDMITX_INPUT_SIGNAL_TYPE;

/*
 * edid
 */
static unsigned char edid_buf[128];
static struct rx_cap rx_capability;
bool gchangemode = false;
union avi_info_frame aviinfo;

/*
 * program utility.
 */
static bool it661x_parse_edid(void);
static bool it661x_parse_cea_edid(unsigned char *pceaedid);
static void it661x_config_avi_info_frame(void);

unsigned char gvideo_mode_select = 0;

unsigned long gvideopixelclock;
unsigned char gvic; /* 480p60 */
unsigned char gpixelrep; /* no pixelrepeating */
enum hdmi_aspec gaspec;
enum hdmi_color_imetry gcolorimetry;

bool ghdmi_mode;

/*
 * Function Body.
 */
static void it661x_set_output(void)
{
	ENTER_FUNC(("%s\n", __func__));

	it661x_enable_video_output(gvideopixelclock, ginput_color_mode,
				ginput_signal_type, goutput_color_mode,
				ghdmi_mode);

	if (ghdmi_mode) {
		it661x_config_avi_info_frame();
		msleep(100);

		it661x_enable_hdcp(true);
	}

	it661x_set_avmute(false);
	gchangemode = false;
}

void it661x_bring_up(void)
{
	unsigned char hpd, hpd_change;

	ENTER_FUNC(("%s\n", __func__));
	it661x_check_hdmitx(&hpd, &hpd_change);

	pr_err("hpd_change = %x, hpd = %x\n", hpd_change, hpd);

	hpd_change = 1;
	hpd = 1;

	if (hpd_change) {
		if (hpd)	{
			it661x_parse_edid();

			/* reset the output color mode into RGB */
			goutput_color_mode &= ~F_MODE_CLRMOD_MASK;

			rx_capability.VALIDHDMI = 1;

			if (rx_capability.VALIDHDMI)
				ghdmi_mode = true;
			else
				ghdmi_mode = false;

			it661x_set_output();
		} else
			ghdmi_mode = false;
	}

	pr_err("hdmi mode is %d(1: enabled; 0: disabled)\n",
				ghdmi_mode);
}


void it661x_change_display_option(enum hdmi_input_video_type timing,
			enum hdmi_pixel_format srcfmt,
			enum hdmi_pixel_format dstfmt)
{
	ENTER_FUNC(("%s\n", __func__));

	switch (timing) {
	case HDMI_640x480p60:
		gvic = 1;
		gvideopixelclock = 25000000;
		gpixelrep = 0;
		gaspec = HDMI_4x3;
		gcolorimetry = HDMI_ITU601;
		break;

	case HDMI_480p60:
		gvic = 2;
		gvideopixelclock = 27000000;
		gpixelrep = 0;
		gaspec = HDMI_4x3;
		gcolorimetry = HDMI_ITU601;
		break;

	case HDMI_480p60_16x9:
		gvic = 3;
		gvideopixelclock = 27000000;
		gpixelrep = 0;
		gaspec = HDMI_16x9;
		gcolorimetry = HDMI_ITU601;
		break;

	case HDMI_720p60:
		gvic = 4;
		gvideopixelclock = 74250000;
		gpixelrep = 0;
		gaspec = HDMI_16x9;
		gcolorimetry = HDMI_ITU709;
		break;

	case HDMI_1080i60:
		gvic = 5;
		gvideopixelclock = 74250000;
		gpixelrep = 0;
		gaspec = HDMI_16x9;
		gcolorimetry = HDMI_ITU709;
		break;

	case HDMI_480i60:
		gvic = 6;
		gvideopixelclock = 13500000;
		gpixelrep = 1;
		gaspec = HDMI_4x3;
		gcolorimetry = HDMI_ITU601;
		break;

	case HDMI_480i60_16x9:
		gvic = 7;
		gvideopixelclock = 13500000;
		gpixelrep = 1;
		gaspec = HDMI_16x9;
		gcolorimetry = HDMI_ITU601;
		break;

	case HDMI_1080p60:
		gvic = 16;
		gvideopixelclock = 148500000;
		gpixelrep = 0;
		gaspec = HDMI_16x9;
		gcolorimetry = HDMI_ITU709;
		break;

	case HDMI_576p50:
		gvic = 17;
		gvideopixelclock = 27000000;
		gpixelrep = 0;
		gaspec = HDMI_4x3;
		gcolorimetry = HDMI_ITU601;
		break;

	case HDMI_576p50_16x9:
		gvic = 18;
		gvideopixelclock = 27000000;
		gpixelrep = 0;
		gaspec = HDMI_16x9;
		gcolorimetry = HDMI_ITU601;
		break;

	case HDMI_720p50:
		gvic = 19;
		gvideopixelclock = 74250000;
		gpixelrep = 0;
		gaspec = HDMI_16x9;
		gcolorimetry = HDMI_ITU709;
		break;

	case HDMI_1080i50:
		gvic = 20;
		gvideopixelclock = 74250000;
		gpixelrep = 0;
		gaspec = HDMI_16x9;
		gcolorimetry = HDMI_ITU709;
		break;

	case HDMI_576i50:
		gvic = 21;
		gvideopixelclock = 13500000;
		gpixelrep = 1;
		gaspec = HDMI_4x3;
		gcolorimetry = HDMI_ITU601;
		break;

	case HDMI_576i50_16x9:
		gvic = 22;
		gvideopixelclock = 13500000;
		gpixelrep = 1;
		gaspec = HDMI_16x9;
		gcolorimetry = HDMI_ITU601;
		break;

	case HDMI_1080p50:
		gvic = 31;
		gvideopixelclock = 148500000;
		gpixelrep = 0;
		gaspec = HDMI_16x9;
		gcolorimetry = HDMI_ITU709;
		break;

	case HDMI_1080p24:
		gvic = 32;
		gvideopixelclock = 74250000;
		gpixelrep = 0;
		gaspec = HDMI_16x9;
		gcolorimetry = HDMI_ITU709;
		break;

	case HDMI_1080p25:
		gvic = 33;
		gvideopixelclock = 74250000;
		gpixelrep = 0;
		gaspec = HDMI_16x9;
		gcolorimetry = HDMI_ITU709;
		break;

	case HDMI_1080p30:
		gvic = 34;
		gvideopixelclock = 74250000;
		gpixelrep = 0;
		gaspec = HDMI_16x9;
		gcolorimetry = HDMI_ITU709;
		break;
	default:
		pr_err("Invalid video timing, return\n");
		gchangemode = false;
		return;
	}

	ginput_color_mode = srcfmt;

	switch (dstfmt) {
	case HDMI_YUV444:
		goutput_color_mode = F_MODE_YUV444;
		break;

	case HDMI_YUV422:
		goutput_color_mode = F_MODE_YUV422;
		break;

	case HDMI_RGB444:
		goutput_color_mode = F_MODE_RGB444;
		break;

	default:
		goutput_color_mode = F_MODE_RGB444;
		break;
	}

	if (HDMI_ITU709 == gcolorimetry)
		ginput_color_mode |= F_VIDMODE_ITU709;
	else
		ginput_color_mode &= ~F_VIDMODE_ITU709;

	if (HDMI_640x480p60 != timing)
		ginput_color_mode |= F_VIDMODE_16_235;
	else
		ginput_color_mode &= ~F_VIDMODE_16_235;

	gchangemode = true;
}


void it661x_config_avi_info_frame(void)
{
	ENTER_FUNC(("%s\n", __func__));

	aviinfo.pktbyte.AVI_HB[0] = AVI_INFOFRAME_TYPE | 0x80;
	aviinfo.pktbyte.AVI_HB[1] = AVI_INFOFRAME_VER;
	aviinfo.pktbyte.AVI_HB[2] = AVI_INFOFRAME_LEN;

	switch (goutput_color_mode) {
	case F_MODE_YUV444:
		/* aviinfo.info.ColorMode = 2; */
		aviinfo.pktbyte.AVI_DB[0] = (2 << 5) | (1 << 4);
		break;
	case F_MODE_YUV422:
		/* aviinfo.info.ColorMode = 1; */
		aviinfo.pktbyte.AVI_DB[0] = (1 << 5) | (1 << 4);
		pr_err("%s set F_MODE_YUV422.\r\n", __func__);
		break;
	case F_MODE_RGB444:
	default:
		/* aviinfo.info.ColorMode = 0; */
		aviinfo.pktbyte.AVI_DB[0] = (0 << 5) | (1 << 4);
		break;
	}

	/*same as picture aspect ratio */
	aviinfo.pktbyte.AVI_DB[1] = 8;

	/* 4:3 or 16:9 */
	aviinfo.pktbyte.AVI_DB[1] |=
			(gaspec != HDMI_16x9) ? (1 << 4) : (2 << 4);

	/* ITU601 */
	aviinfo.pktbyte.AVI_DB[1] |= (gcolorimetry != HDMI_ITU709) ?
						(1 << 6) : (2 << 6);

	aviinfo.pktbyte.AVI_DB[2] = 0;
	aviinfo.pktbyte.AVI_DB[3] = gvic;
	aviinfo.pktbyte.AVI_DB[4] = gpixelrep & 3;
	aviinfo.pktbyte.AVI_DB[5] = 0;
	aviinfo.pktbyte.AVI_DB[6] = 0;
	aviinfo.pktbyte.AVI_DB[7] = 0;
	aviinfo.pktbyte.AVI_DB[8] = 0;
	aviinfo.pktbyte.AVI_DB[9] = 0;
	aviinfo.pktbyte.AVI_DB[10] = 0;
	aviinfo.pktbyte.AVI_DB[11] = 0;
	aviinfo.pktbyte.AVI_DB[12] = 0;

	it661x_enable_avi_info_frame(true, (unsigned char *)&aviinfo);
}

/*
 * it661x_parse_edid()
 * check edid check sum and edid 1.3 extended segment.
 */
static bool it661x_parse_edid(void)
{
	unsigned char checksum;
	unsigned char blockcount;
	bool err = true;
	bool bvalid_cea = false;
	int i, j;

	ENTER_FUNC(("%s\n", __func__));

	rx_capability.VALIDCEA = false;

	it661x_get_edid_data(0, edid_buf);

	for (i = 0; i < 0x80; i += 16) {
		unsigned char ucdata;

		for (j = 0; j < 16; j++)
			ucdata = edid_buf[i + j];
	}

	for (i = 0, checksum = 0; i < 128; i++) {
		checksum += edid_buf[i];
		checksum &= 0xFF;
	}

	if (checksum != 0)
		return false;

	if (edid_buf[0] != 0x00 ||
		edid_buf[1] != 0xFF ||
		edid_buf[2] != 0xFF ||
		edid_buf[3] != 0xFF ||
		edid_buf[4] != 0xFF ||
		edid_buf[5] != 0xFF ||
		edid_buf[6] != 0xFF ||
		edid_buf[7] != 0x00)
		return false;


	blockcount = edid_buf[0x7E];

	if (blockcount == 0)
		return true; /* do nothing. */
	else if (blockcount > 4)
		blockcount = 4;

	/* read all segment */
	for (i = 1; i <= blockcount; i++) {
		err = it661x_get_edid_data(i, edid_buf);

		if (err) {
			if (!bvalid_cea &&
			edid_buf[0] == 0x2 && edid_buf[1] == 0x3) {
				err = it661x_parse_cea_edid(edid_buf);
				if (err) {
					if (rx_capability.IEEEOUI == 0x0c03) {
						rx_capability.VALIDHDMI = true;
						bvalid_cea = true;
					} else
						rx_capability.VALIDHDMI = false;

				}
			}
		}
	}

	return err;
}

static bool it661x_parse_cea_edid(unsigned char *pcea_edid)
{
	unsigned char offset, end;
	unsigned char count;
	unsigned char tag;
	int i;

	ENTER_FUNC(("%s\n", __func__));

	if (pcea_edid[0] != 0x02 || pcea_edid[1] != 0x03)
		return false; /* not a CEA BLOCK. */

	end = pcea_edid[2]; /* CEA description. */

	rx_capability.VIDEOMODE = pcea_edid[3];
	rx_capability.VDOMODECOUNT = 0;
	rx_capability.IDXNATIVEVDOMODE = 0xff;

	for (offset = 4; offset < end;) {
		tag = pcea_edid[offset] >> 5;
		count = pcea_edid[offset] & 0x1f;

		switch (tag) {
		case 0x02:
			/* video Data Block;*/
			offset++;
			for (i = 0, rx_capability.IDXNATIVEVDOMODE = 0xff;
						i < count; i++, offset++) {
				unsigned char localvic;

				localvic = pcea_edid[offset] & (~0x80);

				rx_capability.VDOMODE[rx_capability.
					VDOMODECOUNT] = localvic;

				if (pcea_edid[offset] & 0x80) {
					rx_capability.IDXNATIVEVDOMODE =
					(unsigned char)rx_capability.
							VDOMODECOUNT;

					gvideo_mode_select =
						rx_capability.VDOMODECOUNT;
				}

				rx_capability.VDOMODECOUNT++;
			}
			break;

		case 0x03:
			/* Vendor Specific Data Block; */
			offset++;
			rx_capability.IEEEOUI =
					(unsigned long)pcea_edid[offset + 2];
			rx_capability.IEEEOUI <<= 8;
			rx_capability.IEEEOUI +=
					(unsigned long)pcea_edid[offset + 1];
			rx_capability.IEEEOUI <<= 8;
			rx_capability.IEEEOUI +=
					(unsigned long)pcea_edid[offset];
			offset += count; /* ignore the remaining. */

			break;

#ifdef PARSE_AUDIO_BLOCK
		case 0x01:
			/* audio Data Block; */
			rx_capability.AUDDDSCOUNT = count/3;
			offset++;
			for (i = 0; i < rx_capability.AUDDESCOUNT; i++) {
					rx_capability.AUDDES[i].uc[0] =
							pcea_edid[offset++];
					rx_capability.AUDDES[i].uc[1] =
							pcea_edid[offset++];
					rx_capability.AUDDES[i].uc[2] =
							pcea_edid[offset++];
			}

			break;

		case 0x04:
			/* Speaker Data Block; */
			offset++;
			rx_capability.SPEAKERALLOCBLK.uc[0] =
							pcea_edid[offset];
			rx_capability.SPEAKERALLOCBLK.uc[1] =
							pcea_edid[offset + 1];
			rx_capability.SPEAKERALLOCBLK.uc[2] =
							pcea_edid[offset + 2];
			offset += 3;

			break;
#endif

		default:
			offset += count + 1;
		}
	}

	rx_capability.VALIDCEA = true;

	return true;
}
