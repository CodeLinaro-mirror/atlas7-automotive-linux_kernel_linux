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

#include "hdmitx.h"

/*
 * General global variables
 */
unsigned char i2cdev = 0;
bool gauthenticated = false;
bool gmanualcheckri = false;
unsigned long currvideopixelclock;
unsigned short gmanualcheckricounter = 0;

struct tx_dev_info txdev_info[TXDEVCOUNT];

/* Y,C,RGB offset
 * for register 73~75
 */
unsigned char bCSCOffset_16_235[] = {0x00, 0x80, 0x00};

unsigned char bCSCOffset_0_255[] = {0x10, 0x80, 0x10};

#ifdef SUPPORT_INPUTRGB
unsigned char bCSCMtx_RGB2YUV_ITU601_16_235[] = {
	0xB2, 0x04, 0x64, 0x02, 0xE9, 0x00,
	0x93, 0x3C, 0x18, 0x04, 0x56, 0x3F,
	0x49, 0x3D, 0x9F, 0x3E, 0x18, 0x04,
};

unsigned char bCSCMtx_RGB2YUV_ITU601_0_255[] = {
	0x09, 0x04, 0x0E, 0x02, 0xC8, 0x00,
	0x0E, 0x3D, 0x84, 0x03, 0x6E, 0x3F,
	0xAC, 0x3D, 0xD0, 0x3E, 0x84, 0x03,
};

unsigned char bCSCMtx_RGB2YUV_ITU709_16_235[] = {
	0xB8, 0x05, 0xB4, 0x01, 0x93, 0x00,
	0x49, 0x3C, 0x18, 0x04, 0x9F, 0x3F,
	0xD9, 0x3C, 0x10, 0x3F, 0x18, 0x04,
};

unsigned char bCSCMtx_RGB2YUV_ITU709_0_255[] = {
	0xE5, 0x04, 0x78, 0x01, 0x81, 0x00,
	0xCE, 0x3C, 0x84, 0x03, 0xAE, 0x3F,
	0x49, 0x3D, 0x33, 0x3F, 0x84, 0x03,
};
#endif

#ifdef SUPPORT_INPUTYUV
unsigned char bCSCMtx_YUV2RGB_ITU601_16_235[] = {
	0x00, 0x08, 0x6A, 0x3A, 0x4F, 0x3D,
	0x00, 0x08, 0xF7, 0x0A, 0x00, 0x00,
	0x00, 0x08, 0x00, 0x00, 0xDB, 0x0D,
};

unsigned char bCSCMtx_YUV2RGB_ITU601_0_255[] = {
	0x4F, 0x09, 0x81, 0x39, 0xDF, 0x3C,
	0x4F, 0x09, 0xC2, 0x0C, 0x00, 0x00,
	0x4F, 0x09, 0x00, 0x00, 0x1E, 0x10,
};

unsigned char bCSCMtx_YUV2RGB_ITU709_16_235[] = {
	0x00, 0x08, 0x53, 0x3C, 0x89, 0x3E,
	0x00, 0x08, 0x51, 0x0C, 0x00, 0x00,
	0x00, 0x08, 0x00, 0x00, 0x87, 0x0E,
};

unsigned char bCSCMtx_YUV2RGB_ITU709_0_255[] = {
	0x4F, 0x09, 0xBA, 0x3B, 0x4B, 0x3E,
	0x4F, 0x09, 0x56, 0x0E, 0x00, 0x00,
	0x4F, 0x09, 0x00, 0x00, 0xE7, 0x10,
};
#endif

/*
 * video function
 */
static void it661x_set_input_mode(unsigned char inputcolormode,
				unsigned char inputsignaltype);
static void it661x_set_csc_scale(unsigned char binputmode,
				unsigned char boutputmode);
static void it661x_setup_afe(unsigned long plkfreq);
static void it661x_fire_afe(void);

/*
 *ddc
 */
static void it661x_ddc_clear_fifo(void);
static void it661x_ddc_generate_sclk(void);
static void it661x_ddc_abort(void);

/*
 *edid
 */
static bool it661x_read_edid(unsigned char *pdata, unsigned char bsegment,
				unsigned char offset, short count);

/*
 *infoframe
 */
static bool it661x_set_avi_info_frame(union avi_info_frame *pavi_info_frame);
/*
 * hdcp authentication
 */
static bool it661x_hdcp_enable_encryption(void);
static void it661x_hdcp_auth_fire(void);
static void it661x_hdcp_start_ancipher(void);
static void it661x_hdcp_generate_ancipher(void);
static bool it661x_hdcp_get_bcaps(unsigned char *pbcaps,
				unsigned short *pbstatus);
static bool it661x_hdcp_get_bksv(unsigned char *pbksv);
static bool it661x_hdcp_authenticate(void);
static void it661x_hdcp_resume_authentication(void);
static void it661x_hdcp_read_ri(void);

static void it661x_hdcp_start_ancipher(void);
static void it661x_hdcp_stop_ancipher(void);
static bool it661x_hdcp_get_vr(unsigned char *pvr);
static bool it661x_hdcp_authenticate_repeater(void);
static bool it661x_hdcp_check_sha(unsigned char pm0[], unsigned short bstatus,
		unsigned char pksvlist[], int cdownstream, unsigned char vr[]);
static void it661x_hdcp_reset(void);

/*
 * function body
 */
void it661x_init(void)
{
	ENTER_FUNC(("%s\n", __func__));

	SWITCH_HDMITX_BANK(0);

	/* depends on the interrupt type */
	it661x_write_i2c_byte(REG_INT_CTRL, B_INTPOL_ACTL | B_INTPOL_ACTH);

	it661x_write_i2c_byte(REG_SW_RST, B_REF_RST | B_VID_RST |
				B_AUD_RST | B_AREF_RST | B_HDCP_RST);

	it661x_write_i2c_byte(REG_SW_RST, B_VID_RST | B_AUD_RST |
				B_AREF_RST | B_HDCP_RST);

	it661x_read_i2c_byte(REG_SW_RST);

	/* Avoid power loading in un play status.*/
	it661x_write_i2c_byte(REG_AFE_DRV_CTRL, B_AFE_DRV_RST);

	/* set interrupt mask, mask value 0 is interrupt available */
	it661x_write_i2c_byte(REG_INT_MASK1, 0xF8);
	it661x_write_i2c_byte(REG_INT_MASK2, 0xF8);
	it661x_write_i2c_byte(REG_INT_MASK3, 0xF7);

	DISABLE_NULL_PKT();
	DISABLE_ACP_PKT();
	DISABLE_ISRC1_PKT();
	DISABLE_ISRC2_PKT();
	DISABLE_AVI_INFO_FRM_PKT();
}

static void it661x_set_hdmi_mode(bool bhdmimode)
{
	ENTER_FUNC(("%s\n", __func__));
	SWITCH_HDMITX_BANK(0);

	if (bhdmimode)
		it661x_write_i2c_byte(REG_HDMI_MODE, B_HDMI_MODE);
	else {
		it661x_write_i2c_byte(REG_HDMI_MODE, B_DVI_MODE);

		DISABLE_NULL_PKT();
		DISABLE_ACP_PKT();
		DISABLE_ISRC1_PKT();
		DISABLE_ISRC2_PKT();
		DISABLE_AVI_INFO_FRM_PKT();

		SWITCH_HDMITX_BANK(1);

		it661x_write_i2c_byte(REG_AVIINFO_DB1, 0);
	}

	SWITCH_HDMITX_BANK(0);
}

bool it661x_enable_video_output(unsigned long plkfreq,
				unsigned char inputcolormode,
				unsigned char inputsignaltype,
				unsigned char outputcolormode,
				bool bhdmi)
{
	int i;

	ENTER_FUNC(("%s\n", __func__));

	it661x_write_i2c_byte(REG_SW_RST, B_VID_RST | B_AUD_RST |
				B_AREF_RST | B_HDCP_RST);

	txdev_info[i2cdev].bHDMIMode = bhdmi;

	if (bhdmi)
		it661x_set_avmute(true);

	it661x_set_input_mode(inputcolormode, inputsignaltype);

	it661x_set_csc_scale(inputcolormode, outputcolormode);

	it661x_set_hdmi_mode(bhdmi);

	it661x_setup_afe(plkfreq); /* pass if High Freq request */

	for (i = 0; i < 100; i++) {
		if (it661x_read_i2c_byte(REG_SYS_STATUS) & B_TXVIDSTABLE)
			break;
	}

	it661x_write_i2c_byte(REG_SW_RST, B_VID_RST | B_AUD_RST |
			B_AREF_RST | B_HDCP_RST);

	it661x_write_i2c_byte(REG_SW_RST, B_AUD_RST | B_AREF_RST | B_HDCP_RST);

	for (i = 0; i < 40; i++) {
		if ((it661x_read_i2c_byte(REG_SYS_STATUS) & B_TXVIDSTABLE) == 0)
			continue;

		if ((it661x_read_i2c_byte(REG_SYS_STATUS) & B_TXVIDSTABLE) == 0)
			continue;

		if ((it661x_read_i2c_byte(REG_SYS_STATUS) & B_TXVIDSTABLE) == 0)
			continue;

		break;
	}

	it661x_fire_afe();

	it661x_and_reg_byte(REG_INT_MASK3, ~B_VIDSTABLE_MASK);

	for (i = 0; i < 10; i++)	{
		unsigned char uc;

		it661x_write_i2c_byte(REG_INT_CLR0, 0x0);
		it661x_write_i2c_byte(REG_INT_CLR1, B_CLR_VIDSTABLE);

		uc = it661x_read_i2c_byte(REG_SYS_STATUS);

		uc &= ~B_CLR_AUD_CTS;
		uc |= B_INTACTDONE;

		/* clear interrupt. */
		it661x_write_i2c_byte(REG_SYS_STATUS, uc);

		if (B_INT_VIDSTABLE ==
		(it661x_read_i2c_byte(REG_INT_STAT3) & B_INT_VIDSTABLE)) {
			if ((it661x_read_i2c_byte(REG_SYS_STATUS) &
				B_TXVIDSTABLE) == B_TXVIDSTABLE)
				break;
		}
	}

	return true;
}

void it661x_disable_video_output(void)
{
	ENTER_FUNC(("%s\n", __func__));
	it661x_write_i2c_byte(REG_SW_RST, B_VID_RST | B_AUD_RST |
				B_AREF_RST | B_HDCP_RST);

	it661x_write_i2c_byte(REG_AFE_DRV_CTRL,
				B_AFE_DRV_PWD | B_AFE_DRV_ENBIST);
}
EXPORT_SYMBOL(it661x_disable_video_output);

bool it661x_get_edid_data(int edid_block_id, unsigned char *pedid_data)
{
	ENTER_FUNC(("%s\n", __func__));

	if (it661x_read_edid(pedid_data, edid_block_id/2,
			(edid_block_id % 2) * 128, 128) == false)
		return false;

	return true;
}

bool it661x_enable_hdcp(bool benable)
{
	ENTER_FUNC(("%s\n", __func__));
	if (benable) {
		if (it661x_hdcp_authenticate() == false) {
			it661x_hdcp_reset();
			return false;
		}
	} else
		it661x_hdcp_reset();

	return true;
}
EXPORT_SYMBOL(it661x_enable_hdcp);

unsigned char it661x_check_hdmitx(unsigned char *phpd,
			unsigned char *phpd_change)
{
	unsigned char intdata1, intdata2, intdata3, sysstat;
	unsigned char intclr3 = 0;
	bool prevhpd = false;
	bool hpd;

	ENTER_FUNC(("%s\n", __func__));
	sysstat = it661x_read_i2c_byte(REG_SYS_STATUS);

	hpd = ((sysstat & B_HPDETECT) == B_HPDETECT) ? true : false;

	if (phpd_change)
		*phpd_change = false;

	if (!hpd)
		gauthenticated = false;

	if (sysstat & B_INT_ACTIVE) {
		pr_err("sysstat = 0x%02X\n", sysstat);
		intdata1 = it661x_read_i2c_byte(REG_INT_STAT1);

		if (intdata1 & B_INT_DDCFIFO_ERR) {
			pr_err("DDC FIFO Error.\n");
			it661x_ddc_clear_fifo();
		}

		if (intdata1 & B_INT_DDC_BUS_HANG) {
			pr_err("DDC BUS HANG.\n");
			it661x_ddc_abort();

			/* when DDC hang, and aborted DDC,
			 * the HDCP authentication need to restart.
			 */
			if (gauthenticated)
				it661x_hdcp_resume_authentication();
		}

		if (intdata1 & B_INT_HPD_PLUG) {
			if (phpd_change)
				*phpd_change = true;

			if (!hpd) {
				it661x_write_i2c_byte(REG_SW_RST, B_AREF_RST |
					B_VID_RST | B_AUD_RST | B_HDCP_RST);

				it661x_write_i2c_byte(REG_AFE_DRV_CTRL,
						B_AFE_DRV_RST | B_AFE_DRV_PWD);

				pr_err("Unplug1, %x %x\n",
					it661x_read_i2c_byte(REG_SW_RST),
					it661x_read_i2c_byte(REG_AFE_DRV_CTRL));
			}
		}

		intdata2 = it661x_read_i2c_byte(REG_INT_STAT2);

#ifdef SUPPORT_HDCP
		if (intdata2 & B_INT_AUTH_DONE) {
			pr_err("interrupt Authenticate Done.\n");
			it661x_or_reg_byte(REG_INT_MASK2, B_AUTH_DONE_MASK);
			gauthenticated = true;
			it661x_set_avmute(false);
		}

		if (intdata2 & B_INT_AUTH_FAIL) {
			pr_err("interrupt Authenticate Fail.\n");
			it661x_ddc_abort();
			it661x_hdcp_resume_authentication();
		}
#endif /* SUPPORT_HDCP */

		intdata3 = it661x_read_i2c_byte(REG_INT_STAT3);
		if (intdata3 & B_INT_VIDSTABLE) {
			if (sysstat & B_TXVIDSTABLE)
				it661x_fire_afe();
		}

		it661x_write_i2c_byte(REG_INT_CLR0, 0xFF);
		it661x_write_i2c_byte(REG_INT_CLR1, 0xFF);
		intclr3 = (it661x_read_i2c_byte(REG_SYS_STATUS)) |
				B_CLR_AUD_CTS | B_INTACTDONE;

		/* clear interrupt. */
		it661x_write_i2c_byte(REG_SYS_STATUS, intclr3);
		intclr3 &= ~(B_INTACTDONE | B_CLR_AUD_CTS);

		/* INTACTDONE reset to zero */
		it661x_write_i2c_byte(REG_SYS_STATUS, intclr3);
	} else {
		if (phpd_change) {
			*phpd_change = (hpd != prevhpd) ? true : false;

			if (*phpd_change && (!hpd)) {
				it661x_write_i2c_byte(REG_SW_RST, B_AREF_RST |
					B_VID_RST | B_AUD_RST | B_HDCP_RST);

				it661x_write_i2c_byte(REG_AFE_DRV_CTRL,
						B_AFE_DRV_RST | B_AFE_DRV_PWD);

				pr_err("Unplug2, %x %x\n",
					it661x_read_i2c_byte(REG_SW_RST),
					it661x_read_i2c_byte(REG_AFE_DRV_CTRL));
			}
		}
	}

	if (gmanualcheckri) {
		gmanualcheckricounter++;
		if (gmanualcheckricounter == 1000) {
			gmanualcheckricounter = 0;
			it661x_hdcp_read_ri();
		}
	}

	if (phpd)
		*phpd = hpd ? true : false;

	prevhpd = hpd;
	return hpd;
}

bool it661x_enable_avi_info_frame(bool benable, unsigned char *pavi_info_frame)
{
	ENTER_FUNC(("%s\n", __func__));
	if (!benable) {
		DISABLE_AVI_INFO_FRM_PKT();
		return true;
	}

	if (it661x_set_avi_info_frame((union avi_info_frame *)pavi_info_frame))
		return true;

	return false;
}

void it661x_set_avmute(bool benable)
{
	ENTER_FUNC(("%s\n", __func__));
	SWITCH_HDMITX_BANK(0);

	it661x_write_i2c_byte(REG_AV_MUTE,
				benable ? B_SET_AVMUTE : B_CLR_AVMUTE);

	it661x_write_i2c_byte(REG_PKT_GENERAL_CTRL,
				B_ENABLE_PKT | B_REPEAT_PKT);
}

/*
 * Initialization process
 *
 * Function: it661x_set_input_mode
 * Parameter: inputcolormode, inputsignaltype
 * InputMode - use [1:0] to identify the color space for reg70[7:6],
 * definition:
 *         #define F_MODE_RGB444  0
 *         #define F_MODE_YUV422 1
 *         #define F_MODE_YUV444 2
 *         #define F_MODE_CLRMOD_MASK 3
 * VideoInputType - defined the CCIR656 D[0], SYNC Embedded D[1], and
 *                     DDR input in D[2].
 * return: N/A
 * Remark: program Reg70 with the input value.
 * Side-Effect: Reg70.
 */

static void it661x_set_input_mode(unsigned char inputcolormode,
				unsigned char inputsignaltype)
{
	unsigned char ucdata;

	ENTER_FUNC(("%s\n", __func__));

	ucdata = it661x_read_i2c_byte(REG_INPUT_MODE);

	ucdata &= ~(M_INCOLMOD | B_2X656CLK | B_SYNCEMB | B_INDDR);

	switch (inputcolormode & F_MODE_CLRMOD_MASK) {
	case F_MODE_YUV422:
		ucdata |= B_IN_YUV422;
		break;
	case F_MODE_YUV444:
		ucdata |= B_IN_YUV444;
		break;
	case F_MODE_RGB444:
	default:
		ucdata |= B_IN_RGB;
		break;
	}

	if (inputsignaltype & T_MODE_CCIR656) {
		ucdata |= B_2X656CLK;
		pr_err("CCIR656 mode\n");
	}

	if (inputsignaltype & T_MODE_SYNCEMB) {
		ucdata |= B_SYNCEMB;
		pr_err("Sync Embedded mode\n");
	}

	if (inputsignaltype & T_MODE_INDDR) {
		ucdata |= B_INDDR;
		pr_err("input DDR mode\n");
	}

	it661x_write_i2c_byte(REG_INPUT_MODE, ucdata);
}

/*
 * Function: it661x_set_csc_scale
 * Parameter: binputmode -
 *    D[1:0] - Color Mode
 *    D[4] - Colorimetry 0: ITU_BT601 1: ITU_BT709
 *    D[5] - Quantization 0: 0_255 1: 16_235
 *    D[6] - Up/Dn Filter 'Required'
 *    0: no up/down filter
 *    1: enable up/down filter when csc need.
 *    D[7] - Dither Filter 'Required'
 *    0: no dither enabled.
 *    1: enable dither and dither free go "when required".
 *    boutputmode -
 *    D[1:0] - Color mode.
 *  return: N/A
 *  Remark: reg72~reg8D will be programmed depended the input with table.
 * Side-Effect:
 */
static void it661x_set_csc_scale(unsigned char binputmode,
				unsigned char boutputmode)
{
	unsigned char ucdata, csc = B_CSC_BYPASS;

	/* filter is for Video CTRL DN_FREE_GO, EN_DITHER, and ENUDFILT */
	unsigned char filter = 0;
	unsigned char i;

	/* (1) YUV422 in, RGB/YUV444 output
	 *     Output is 8-bit, input is 12-bit
	 * (2) YUV444/422  in, RGB output
	 *     (CSC enable, and output is not YUV422)
	 * (3) RGB in, YUV444 output
	 *     (CSC enable, and output is not YUV422)
	 *
	 *  YUV444/RGB444 <-> YUV422 need set up/down filter.
	 */

	ENTER_FUNC(("%s\n", __func__));

	switch (binputmode & F_MODE_CLRMOD_MASK) {
#ifdef SUPPORT_INPUTYUV444
	case F_MODE_YUV444:
		pr_err("Input mode is YUV444\n");
		switch (boutputmode & F_MODE_CLRMOD_MASK) {
		case F_MODE_YUV444:
			pr_err("Output mode is YUV444\n");
			csc = B_CSC_BYPASS;
			break;

		case F_MODE_YUV422:
			pr_err("Output mode is YUV422\n");
			/* YUV444 to YUV422 need up/down filter
			 * for processing
			 */
			if (binputmode & F_VIDMODE_EN_UDFILT)
				filter |= B_EN_UDFILTER;

			csc = B_CSC_BYPASS;

			break;

		case F_MODE_RGB444:
			pr_err("Output mode is RGB444\n");
			csc = B_CSC_YUV2RGB;

			/* YUV444 to RGB444 need dither */
			if (binputmode & F_VIDMODE_EN_DITHER)
				filter |= B_EN_DITHER | B_DNFREE_GO;
			break;
		}

		break;
#endif

#ifdef SUPPORT_INPUTYUV422
	case F_MODE_YUV422:
		pr_err("Input mode is YUV422\n");
		switch (boutputmode & F_MODE_CLRMOD_MASK) {
		case F_MODE_YUV444:
			pr_err("Output mode is YUV444\n");
			csc = B_CSC_BYPASS;

			/* YUV422 to YUV444 need up filter */
			if (binputmode & F_VIDMODE_EN_UDFILT)
				filter |= B_EN_UDFILTER;

			/* YUV422 to YUV444 need dither */
			if (binputmode & F_VIDMODE_EN_DITHER)
				filter |= B_EN_DITHER | B_DNFREE_GO;

			break;

		case F_MODE_YUV422:
			pr_err("Output mode is YUV422\n");
			csc = B_CSC_BYPASS;
			break;

		case F_MODE_RGB444:
			pr_err("Output mode is RGB444\n");
			csc = B_CSC_YUV2RGB;

			/* YUV422 to RGB444 need up/dn filter. */
			if (binputmode & F_VIDMODE_EN_UDFILT)
				filter |= B_EN_UDFILTER;

			/* YUV422 to RGB444 need dither */
			if (binputmode & F_VIDMODE_EN_DITHER)
				filter |= B_EN_DITHER | B_DNFREE_GO;

			break;
		}

		break;
#endif

#ifdef SUPPORT_INPUTRGB
	case F_MODE_RGB444:
		pr_err("Input mode is RGB444\n");
		switch (boutputmode & F_MODE_CLRMOD_MASK) {
		case F_MODE_YUV444:
			pr_err("Output mode is YUV444\n");
			csc = B_CSC_RGB2YUV;

			/*RGB444 to YUV444 need dither */
			if (binputmode & F_VIDMODE_EN_DITHER)
				filter |= B_EN_DITHER | B_DNFREE_GO;

			break;

		case F_MODE_YUV422:
			pr_err("Output mode is YUV422\n");

			/* RGB444 to YUV422 need down filter. */
			if (binputmode & F_VIDMODE_EN_UDFILT)
				filter |= B_EN_UDFILTER;

			/* RGB444 to YUV422 need dither */
			if (binputmode & F_VIDMODE_EN_DITHER)
				filter |= B_EN_DITHER | B_DNFREE_GO;

			csc = B_CSC_RGB2YUV;
			break;

		case F_MODE_RGB444:
			pr_err("Output mode is RGB444\n");
			csc = B_CSC_BYPASS;
			break;
		}

		break;
#endif
	} /* end of switch(bOutputMode&F_MODE_CLRMOD_MASK) */

#ifdef SUPPORT_INPUTRGB
	/* set the CSC metrix registers by colorimetry and quantization */
	if (csc == B_CSC_RGB2YUV) {
		pr_err("CSC = RGB2YUV %x ", csc);

		switch (binputmode &
			(F_VIDMODE_ITU709 | F_VIDMODE_16_235)) {
		case F_VIDMODE_ITU709 | F_VIDMODE_16_235:
			pr_err("ITU709 16-235\n");

			for (i = 0; i <  SIZEOF_CSCOFFSET; i++) {
				it661x_write_i2c_byte(REG_CSC_YOFF + i,
					bCSCOffset_16_235[i]);
			}

			for (i = 0; i < SIZEOF_CSCMTX; i++) {
				it661x_write_i2c_byte(REG_CSC_MTX11_L + i,
					bCSCMtx_RGB2YUV_ITU709_16_235[i]);
			}

			break;

		case F_VIDMODE_ITU709 | F_VIDMODE_0_255:
			pr_err("ITU709 0-255\n");

			for (i = 0; i <  SIZEOF_CSCOFFSET; i++) {
				it661x_write_i2c_byte(REG_CSC_YOFF+i,
					bCSCOffset_0_255[i]);
			}

			for (i = 0; i < SIZEOF_CSCMTX; i++) {
				it661x_write_i2c_byte(REG_CSC_MTX11_L + i,
					bCSCMtx_RGB2YUV_ITU709_0_255[i]);
			}

			break;

		case F_VIDMODE_ITU601 | F_VIDMODE_16_235:
			pr_err("ITU601 16-235\n");

			for (i = 0; i <  SIZEOF_CSCOFFSET; i++) {
				it661x_write_i2c_byte(REG_CSC_YOFF + i,
					bCSCOffset_16_235[i]);
			}

			for (i = 0; i < SIZEOF_CSCMTX; i++) {
				it661x_write_i2c_byte(REG_CSC_MTX11_L + i,
					bCSCMtx_RGB2YUV_ITU601_16_235[i]);
			}

			break;

		case F_VIDMODE_ITU601 | F_VIDMODE_0_255:
		default:
			pr_err("ITU601 0-255\n");

			for (i = 0; i < SIZEOF_CSCOFFSET; i++) {
				it661x_write_i2c_byte(REG_CSC_YOFF+i,
					bCSCOffset_0_255[i]);
			}

			for (i = 0; i < SIZEOF_CSCMTX; i++) {
				it661x_write_i2c_byte(REG_CSC_MTX11_L + i,
					bCSCMtx_RGB2YUV_ITU601_0_255[i]);
			}

			break;
		}

	}
#endif

#ifdef SUPPORT_INPUTYUV
	if (csc == B_CSC_YUV2RGB) {
		pr_err("CSC = YUV2RGB %x ", csc);

		switch (binputmode & (F_VIDMODE_ITU709 |
					F_VIDMODE_16_235)) {
		case F_VIDMODE_ITU709 | F_VIDMODE_16_235:
			pr_err("ITU709 16-235\n");

			for (i = 0; i < SIZEOF_CSCOFFSET; i++) {
				it661x_write_i2c_byte(REG_CSC_YOFF + i,
					bCSCOffset_16_235[i]);
			}

			for (i = 0; i < SIZEOF_CSCMTX; i++) {
				it661x_write_i2c_byte(REG_CSC_MTX11_L + i,
					bCSCMtx_YUV2RGB_ITU709_16_235[i]);
			}

			break;

		case F_VIDMODE_ITU709 | F_VIDMODE_0_255:
			pr_err("ITU709 0-255\n");

			for (i = 0; i <  SIZEOF_CSCOFFSET; i++) {
				it661x_write_i2c_byte(REG_CSC_YOFF + i,
					bCSCOffset_0_255[i]);
			}

			for (i = 0; i < SIZEOF_CSCMTX; i++) {
				it661x_write_i2c_byte(REG_CSC_MTX11_L + i,
					bCSCMtx_YUV2RGB_ITU709_0_255[i]);
			}

			break;

		case F_VIDMODE_ITU601 | F_VIDMODE_16_235:
			pr_err("ITU601 16-235\n");

			for (i = 0; i <  SIZEOF_CSCOFFSET; i++) {
				it661x_write_i2c_byte(REG_CSC_YOFF + i,
					bCSCOffset_16_235[i]);
			}

			for (i = 0; i < SIZEOF_CSCMTX; i++) {
				it661x_write_i2c_byte(REG_CSC_MTX11_L + i,
					bCSCMtx_YUV2RGB_ITU601_16_235[i]);
			}

			break;

		case F_VIDMODE_ITU601 | F_VIDMODE_0_255:
		default:
			pr_err("ITU601 0-255 ");

			for (i = 0; i <  SIZEOF_CSCOFFSET; i++) {
				it661x_write_i2c_byte(REG_CSC_YOFF + i,
					bCSCOffset_0_255[i]);
			}

			for (i = 0; i < SIZEOF_CSCMTX; i++) {
				it661x_write_i2c_byte(REG_CSC_MTX11_L + i,
					bCSCMtx_YUV2RGB_ITU601_0_255[i]);
			}

			break;
		}
	}
#endif

	ucdata = it661x_read_i2c_byte(REG_CSC_CTRL) &
		~(M_CSC_SEL | B_DNFREE_GO | B_EN_DITHER | B_EN_UDFILTER);

	ucdata |= filter | csc;

	it661x_write_i2c_byte(REG_CSC_CTRL, ucdata);
}


/*
 * Function: it661x_setup_afe
 * Parameter: bool HighFreqMode
 *            false - PCLK < 80Hz ( for mode less than 1080p)
 *            true  - PCLK > 80Hz ( for 1080p mode or above)
 * Return: N/A
 * Remark: set reg62~reg65 depended on HighFreqMode
 *         reg61 have to be programmed at last and after video stable input.
 * Side-Effect:
 */
static void it661x_setup_afe(unsigned long plkfreq)
{
	unsigned char uc;

	ENTER_FUNC(("%s\n", __func__));

	/* turn off reg61 before setting up afe parameters. */
	SWITCH_HDMITX_BANK(0);
	uc = (it661x_read_i2c_byte(REG_INT_CTRL) &
			B_IDENT_6612) ? B_AFE_XP_BYPASS : 0;

	/* for identifying the CAT6612 operation. */
	it661x_write_i2c_byte(REG_AFE_DRV_CTRL, B_AFE_DRV_RST); /* 0x00 */

	if (plkfreq > 80000000L) {
		it661x_write_i2c_byte(REG_AFE_XP_CTRL1,
				B_AFE_XP_GAINBIT | B_AFE_XP_RESETB | uc);

		it661x_write_i2c_byte(REG_AFE_XP_CTRL2, B_XP_CLKSEL_1_PCLKHV);

		it661x_write_i2c_byte(REG_AFE_IP_CTRL,
			B_AFE_IP_GAINBIT | B_AFE_IP_SEDB |
			B_AFE_IP_RESETB | B_AFE_IP_PDIV1);

		it661x_write_i2c_byte(REG_AFE_RING, 0x00);
	} else {
		it661x_write_i2c_byte(REG_AFE_XP_CTRL1,
				B_AFE_XP_ER0 | B_AFE_XP_RESETB | uc);

		it661x_write_i2c_byte(REG_AFE_XP_CTRL2, B_XP_CLKSEL_1_PCLKHV);

		if (plkfreq > 15000000L) {
			it661x_write_i2c_byte(REG_AFE_IP_CTRL,
				B_AFE_IP_SEDB | B_AFE_IP_ER0 |
				B_AFE_IP_RESETB | B_AFE_IP_PDIV1);
		} else {
			it661x_write_i2c_byte(REG_AFE_IP_CTRL, B_AFE_IP_ER0 |
					B_AFE_IP_RESETB | B_AFE_IP_PDIV1);
		}

		it661x_write_i2c_byte(REG_AFE_RING, 0x00);
	}

	it661x_or_reg_byte(REG_INT_MASK3, B_VIDSTABLE_MASK);

	uc = it661x_read_i2c_byte(REG_SW_RST);
	uc &= ~(B_REF_RST | B_VID_RST);

	it661x_write_i2c_byte(REG_SW_RST, uc);
}


/*
 * Function: it661x_fire_afe
 * Parameter: No
 * Return: No
 * Remark: write reg61 with 0x04
 *         When program reg61 with 0x04, then audio and video circuit work.
 * Side-Effect: N/A
 */
static void it661x_fire_afe(void)
{
	ENTER_FUNC(("%s\n", __func__));
	SWITCH_HDMITX_BANK(0);

	it661x_write_i2c_byte(REG_AFE_DRV_CTRL, 0);
}

/*
 * ddc module
 */


/*
 * Function: it661x_ddc_clear_fifo
 * Parameter: N/A
 * Return: N/A
 * Remark: clear the DDC FIFO.
 * Side-Effect: DDC master will set to be HOST.
 */

static void it661x_ddc_clear_fifo(void)
{
	ENTER_FUNC(("%s\n", __func__));

	it661x_write_i2c_byte(REG_DDC_MASTER_CTRL, B_MASTERDDC | B_MASTERHOST);
	it661x_write_i2c_byte(REG_DDC_CMD, CMD_FIFO_CLR);
}

static void it661x_ddc_generate_sclk(void)
{
	ENTER_FUNC(("%s\n", __func__));

	it661x_write_i2c_byte(REG_DDC_MASTER_CTRL, B_MASTERDDC | B_MASTERHOST);
	it661x_write_i2c_byte(REG_DDC_CMD, CMD_GEN_SCLCLK);
}

/*
 * Function: AbortDDC
 * Parameter: N/A
 * Return: N/A
 * Remark: Force abort DDC and reset DDC bus.
 * Side-Effect: No
 */
void it661x_ddc_adjust(bool benable)
{
	ENTER_FUNC(("%s\n", __func__));

	if (benable)
		it661x_write_i2c_byte(0x65, 0x02);
	else
		it661x_write_i2c_byte(0x65, 0x00);
}

static void it661x_ddc_abort(void)
{
	unsigned char cpdesire, reset, ddc_master;
	unsigned char count, uc;

	ENTER_FUNC(("%s\n", __func__));
	/* save the SW reset, DDC master, and CP Desire setting. */
	reset = it661x_read_i2c_byte(REG_SW_RST);
	cpdesire = it661x_read_i2c_byte(REG_HDCP_DESIRE);
	ddc_master = it661x_read_i2c_byte(REG_DDC_MASTER_CTRL);

	it661x_ddc_adjust(true);

	/* change order*/
	it661x_write_i2c_byte(REG_HDCP_DESIRE, cpdesire & (~B_CPDESIRE));
	it661x_write_i2c_byte(REG_SW_RST, reset | B_HDCP_RST);
	it661x_write_i2c_byte(REG_DDC_MASTER_CTRL, B_MASTERDDC | B_MASTERHOST);
	it661x_write_i2c_byte(REG_DDC_CMD, CMD_DDC_ABORT);

	for (count = 200; count > 0; count--) {
		uc = it661x_read_i2c_byte(REG_DDC_STATUS);
		if (uc & B_DDC_DONE)
			break;

		if (uc & B_DDC_ERROR)
			break; /* error when abort. */
	}

	it661x_write_i2c_byte(REG_DDC_CMD, CMD_DDC_ABORT);

	for (count = 200; count > 0; count--) {
		uc = it661x_read_i2c_byte(REG_DDC_STATUS);

		if (uc & B_DDC_DONE)
			break;

		if (uc & B_DDC_ERROR)
			break; /* error when aborting*/
	}

	it661x_ddc_adjust(false);
}

/*
 * Function: it661x_read_edid
 * Parameter: pdata - the pointer of buffer to receive edid ucdata.
 *            bsegment - the segment of edid readback.
 *            offset - the offset of edid ucdata in the segment. in byte.
 *            count - the read back bytes count, cannot exceed 32
 * Return: true if successfully getting edid. false otherwise.
 * Remark: function for read edid ucdata from receiver.
 * Side-Effect: ddc master will set to be HOST.
 * DDC fifo will be used and dirty.
 */
static bool it661x_read_edid(unsigned char *pdata, unsigned char bsegment,
				unsigned char offset, short count)
{
	short remainedcount, reqcount;
	unsigned char bcurroffset;
	short timeout;
	unsigned char *pbuff = pdata;
	unsigned char ucdata;

	ENTER_FUNC(("%s\n", __func__));

	if (!pdata) {
		pr_err("%s: Invallid pData pointer %08lX\n",
			__func__, (unsigned long)pdata);

		goto readedid_fail;
	}

	if (it661x_read_i2c_byte(REG_INT_STAT1) & B_INT_DDC_BUS_HANG) {
		pr_err("called it661x_ddc_abort()\n");
		it661x_ddc_abort();
	}

	it661x_ddc_clear_fifo();

	remainedcount = count;
	bcurroffset = offset;

	SWITCH_HDMITX_BANK(0);

	while (remainedcount > 0) {
		reqcount = (remainedcount > DDC_FIFO_MAXREQ) ?
				DDC_FIFO_MAXREQ : remainedcount;

		it661x_ddc_clear_fifo();

		it661x_ddc_adjust(true);

		it661x_write_i2c_byte(REG_DDC_MASTER_CTRL,
					B_MASTERDDC | B_MASTERHOST);

		/* for EDID ucdata get */
		it661x_write_i2c_byte(REG_DDC_HEADER, DDC_EDID_ADDRESS);
		it661x_write_i2c_byte(REG_DDC_REQOFF, bcurroffset);
		it661x_write_i2c_byte(REG_DDC_REQCOUNT,
						(unsigned char)reqcount);

		it661x_write_i2c_byte(REG_DDC_EDIDSEG, bsegment);
		it661x_write_i2c_byte(REG_DDC_CMD, CMD_EDID_READ);

		bcurroffset += reqcount;
		remainedcount -= reqcount;

		for (timeout = 250; timeout > 0; timeout--) {
			ucdata = it661x_read_i2c_byte(REG_DDC_STATUS);
			if (ucdata & B_DDC_DONE)
				break;

			if (ucdata & B_DDC_ERROR) {
				pr_err("%s: DDC_STATUS = %02X, fail.\n",
					 __func__, ucdata);

				goto readedid_fail;
			}
		}

		if (timeout == 0) {
			pr_err("%s: DDC TimeOut.\n", __func__);
			goto readedid_fail;
		}

		do {
			*(pbuff++) = it661x_read_i2c_byte(REG_DDC_READFIFO);
			reqcount--;
		} while (reqcount > 0);
	}

	it661x_ddc_adjust(false);

	return true;

readedid_fail:
	it661x_ddc_adjust(false);
	return false;
}

/*
 * Packet and InfoFrame
 *
 * Function: SetAVIInfoFrame()
 * Parameter: pavi_info_frame - the pointer to HDMI AVI Infoframe ucData
 * Return: N/A
 * Remark: Fill the AVI InfoFrame ucData, and count checksum, then fill into
 *         AVI InfoFrame registers.
 * Side-Effect: N/A
 */
static bool it661x_set_avi_info_frame(union avi_info_frame *pavi_info_frame)
{
	int i;
	unsigned char ucdata;

	ENTER_FUNC(("%s\n", __func__));

	if (!pavi_info_frame)
		return false;

	SWITCH_HDMITX_BANK(1);

	it661x_write_i2c_byte(REG_AVIINFO_DB1,
					pavi_info_frame->pktbyte.AVI_DB[0]);
	it661x_write_i2c_byte(REG_AVIINFO_DB2,
					pavi_info_frame->pktbyte.AVI_DB[1]);
	it661x_write_i2c_byte(REG_AVIINFO_DB3,
					pavi_info_frame->pktbyte.AVI_DB[2]);
	it661x_write_i2c_byte(REG_AVIINFO_DB4,
					pavi_info_frame->pktbyte.AVI_DB[3]);
	it661x_write_i2c_byte(REG_AVIINFO_DB5,
					pavi_info_frame->pktbyte.AVI_DB[4]);
	it661x_write_i2c_byte(REG_AVIINFO_DB6,
					pavi_info_frame->pktbyte.AVI_DB[5]);
	it661x_write_i2c_byte(REG_AVIINFO_DB7,
					pavi_info_frame->pktbyte.AVI_DB[6]);
	it661x_write_i2c_byte(REG_AVIINFO_DB8,
					pavi_info_frame->pktbyte.AVI_DB[7]);
	it661x_write_i2c_byte(REG_AVIINFO_DB9,
					pavi_info_frame->pktbyte.AVI_DB[8]);
	it661x_write_i2c_byte(REG_AVIINFO_DB10,
					pavi_info_frame->pktbyte.AVI_DB[9]);
	it661x_write_i2c_byte(REG_AVIINFO_DB11,
					pavi_info_frame->pktbyte.AVI_DB[10]);
	it661x_write_i2c_byte(REG_AVIINFO_DB12,
					pavi_info_frame->pktbyte.AVI_DB[11]);
	it661x_write_i2c_byte(REG_AVIINFO_DB13,
					pavi_info_frame->pktbyte.AVI_DB[12]);

	for (i = 0, ucdata = 0; i < 13; i++)
		ucdata -= pavi_info_frame->pktbyte.AVI_DB[i];

	ucdata -= 0x80 + AVI_INFOFRAME_VER + AVI_INFOFRAME_TYPE +
						AVI_INFOFRAME_LEN;

	it661x_write_i2c_byte(REG_AVIINFO_SUM, ucdata);

	SWITCH_HDMITX_BANK(0);

	ENABLE_AVI_INFO_FRM_PKT();

	return true;
}

/*
 * authentication mode
 */

#ifdef SUPPORT_HDCP
static void it661x_hdcp_clear_auth_interrupt(void)
{
	unsigned char uc;

	ENTER_FUNC(("%s\n", __func__));

	uc = it661x_read_i2c_byte(REG_INT_MASK2) &
		(~(B_KSVLISTCHK_MASK | B_AUTH_DONE_MASK | B_AUTH_FAIL_MASK));

	it661x_write_i2c_byte(REG_INT_CLR0,
		B_CLR_AUTH_FAIL | B_CLR_AUTH_DONE | B_CLR_KSVLISTCHK);

	it661x_write_i2c_byte(REG_INT_CLR1, 0);

	it661x_write_i2c_byte(REG_SYS_STATUS, B_INTACTDONE);
}


/*
 * Function: it661x_hdcp_enable_encryption
 * Parameter: N/A
 * Return: true if done.
 * Remark: Set regC1 as zero to enable continue authentication.
 * Side-Effect: register bank will reset to zero.
 */
static bool it661x_hdcp_enable_encryption(void)
{
	ENTER_FUNC(("%s\n", __func__));

	SWITCH_HDMITX_BANK(0);

	return it661x_write_i2c_byte(REG_ENCRYPTION, B_ENABLE_ENCRYPTION);
}


/*
 * Function: it661x_hdcp_auth_fire()
 * Parameter: N/A
 * Return: N/A
 * Remark: write anything to reg21 to enable HDCP authentication by HW
 * Side-Effect: N/A
 */
static void it661x_hdcp_auth_fire(void)
{
	ENTER_FUNC(("%s\n", __func__));

	/* MASTERHDCP,no need command but fire. */
	it661x_write_i2c_byte(REG_DDC_MASTER_CTRL, B_MASTERDDC | B_MASTERHDCP);
	it661x_write_i2c_byte(REG_AUTHFIRE, 1);
}

/*
 * Function: it661x_hdcp_start_ancipher
 * Parameter: N/A
 * Return: N/A
 * Remark: Start the Cipher to free run for random number. When stop,An is
 *         ready in Reg30.
 * Side-Effect: N/A
 */
static void it661x_hdcp_start_ancipher(void)
{
	ENTER_FUNC(("%s\n", __func__));

	it661x_write_i2c_byte(REG_AN_GENERATE, B_START_CIPHER_GEN);
}

/*
 * Function: HDCP_StopAnCipher
 * Parameter: N/A
 * Return: N/A
 * Remark: Stop the Cipher,and An is ready in Reg30.
 * Side-Effect: N/A
 */
static void it661x_hdcp_stop_ancipher(void)
{
	ENTER_FUNC(("%s\n", __func__));

	it661x_write_i2c_byte(REG_AN_GENERATE, B_STOP_CIPHER_GEN);
}

/*
 * Function: it661x_hdcp_generate_ancipher
 * Parameter: N/A
 * Return: N/A
 * Remark: start An ciper random run at first,then stop it. Software can get
 *         an in reg30~reg38,the write to reg28~2F
 * Side-Effect:
 */
static void it661x_hdcp_generate_ancipher(void)
{
	unsigned char i;
	unsigned char uc;

	ENTER_FUNC(("%s\n", __func__));

	it661x_hdcp_start_ancipher();

	it661x_hdcp_stop_ancipher();

	SWITCH_HDMITX_BANK(0);

	/* new An is ready in reg30 */
	for (i = 0; i < 8; i++) {
		uc = it661x_read_i2c_byte(REG_AN_GEN + i);

		it661x_write_i2c_byte(REG_AN+i, uc);
	}
}


/*
 * Function: it661x_hdcp_get_bcaps
 * Parameter: pbcaps - pointer of byte to get BCaps.
 *            pbstatus - pointer of two bytes to get BStatus
 * Return: true if successfully got BCaps and BStatus.
 * Remark: get B status and capability from HDCP receiver via DDC bus.
 * Side-Effect:
 */
static bool it661x_hdcp_get_bcaps(unsigned char *pbcaps,
				unsigned short *pbstatus)
{
	unsigned char ucdata;
	unsigned char timeout;

	ENTER_FUNC(("%s\n", __func__));

	SWITCH_HDMITX_BANK(0);

	it661x_write_i2c_byte(REG_DDC_MASTER_CTRL,
			B_MASTERDDC | B_MASTERHOST);
	it661x_write_i2c_byte(REG_DDC_HEADER, DDC_HDCP_ADDRESS);
	it661x_write_i2c_byte(REG_DDC_REQOFF, 0x40); /* BCaps offset */
	it661x_write_i2c_byte(REG_DDC_REQCOUNT, 3);
	it661x_write_i2c_byte(REG_DDC_CMD, CMD_DDC_SEQ_BURSTREAD);

	for (timeout = 200; timeout > 0; timeout--) {
		ucdata = it661x_read_i2c_byte(REG_DDC_STATUS);

		if (ucdata & B_DDC_DONE)
			break;

		if (ucdata & B_DDC_ERROR) {
			pr_err("%s: DDC fail by reg16=%02X.\n",
				__func__, ucdata);
			return false;
		}
	}

	if (timeout == 0)
		return false;

	ucdata = it661x_read_i2c_byte(REG_BSTAT + 1);
	*pbstatus = ((unsigned short)ucdata & 0xFF) << 8;
	ucdata = it661x_read_i2c_byte(REG_BSTAT);
	*pbstatus |= (unsigned short)ucdata & 0xFF;

	*pbcaps = it661x_read_i2c_byte(REG_BCAP);

	return true;
}


/*
 * Function: it661x_hdcp_get_bksv
 * Parameter: pBKSV - pointer of 5 bytes buffer for getting BKSV
 * Return: true if successfuly got BKSV from Rx.
 * Remark: Get BKSV from HDCP receiver.
 * Side-Effect: N/A
 */
static bool it661x_hdcp_get_bksv(unsigned char *pbksv)
{
	unsigned char ucdata;
	unsigned char timeout;

	ENTER_FUNC(("%s\n", __func__));

	SWITCH_HDMITX_BANK(0);

	it661x_write_i2c_byte(REG_DDC_MASTER_CTRL,
				B_MASTERDDC | B_MASTERHOST);

	it661x_write_i2c_byte(REG_DDC_HEADER,
				DDC_HDCP_ADDRESS);

	it661x_write_i2c_byte(REG_DDC_REQOFF, 0x00); /* BKSV offset */
	it661x_write_i2c_byte(REG_DDC_REQCOUNT, 5);
	it661x_write_i2c_byte(REG_DDC_CMD, CMD_DDC_SEQ_BURSTREAD);

	for (timeout = 200; timeout > 0; timeout--) {
		ucdata = it661x_read_i2c_byte(REG_DDC_STATUS);

		if (ucdata & B_DDC_DONE) {
			pr_err("%s: DDC Done.\n", __func__);
			break;
		}

		if (ucdata & B_DDC_ERROR) {
			pr_err("%s: DDC No ack %x\n",
				__func__, ucdata);
			return false;
		}
	}

	if (timeout == 0)
		return false;

	*(pbksv) = it661x_read_i2c_byte(REG_BKSV);
	*(pbksv + 1) = it661x_read_i2c_byte(REG_BKSV + 1);
	*(pbksv + 2) = it661x_read_i2c_byte(REG_BKSV + 2);
	*(pbksv + 3) = it661x_read_i2c_byte(REG_BKSV + 3);
	*(pbksv + 4) = it661x_read_i2c_byte(REG_BKSV + 4);

	return true;
}

/*
 * Function:it661x_hdcp_read_ri
 * Parameter: N/A
 * Return: true if Authenticated without error.
 * Remark: do Authentication with Rx
 * Side-Effect:
 *  1. gauthenticated global variable will be true when authenticated.
 *  2. Auth_done interrupt and AUTH_FAIL interrupt will be enabled.
 */
static void it661x_hdcp_read_ri(void)
{
	unsigned char ucdata;
	unsigned char timeout = 0;

	ENTER_FUNC(("%s\n", __func__));

	SWITCH_HDMITX_BANK(0);
	it661x_write_i2c_byte(REG_DDC_MASTER_CTRL,
				B_MASTERDDC | B_MASTERHOST);
	it661x_write_i2c_byte(REG_DDC_HEADER, DDC_HDCP_ADDRESS);
	it661x_write_i2c_byte(REG_DDC_REQOFF, 0x08); /* Ri offset */
	it661x_write_i2c_byte(REG_DDC_REQCOUNT, 2);
	it661x_write_i2c_byte(REG_DDC_CMD, CMD_DDC_SEQ_BURSTREAD);

	ucdata = it661x_read_i2c_byte(REG_DDC_STATUS);

	while (!(ucdata & B_DDC_DONE) && !(ucdata & B_DDC_ERROR)
		&& (timeout < 200)) {
		ucdata = it661x_read_i2c_byte(REG_DDC_STATUS);
	}
}

static void it661x_hdcp_reset(void)
{
	unsigned char uc;

	ENTER_FUNC(("%s\n", __func__));

	it661x_write_i2c_byte(REG_LISTCTRL, 0);
	it661x_write_i2c_byte(REG_HDCP_DESIRE, 0);

	uc = it661x_read_i2c_byte(REG_SW_RST);
	uc |= B_HDCP_RST;

	it661x_write_i2c_byte(REG_SW_RST, uc);
	it661x_write_i2c_byte(REG_DDC_MASTER_CTRL,
			B_MASTERDDC | B_MASTERHOST);

	it661x_hdcp_clear_auth_interrupt();
	it661x_ddc_abort();
}


static bool it661x_hdcp_validate_bksv(unsigned char *bksv)
{
	int i, j;
	int counter = 0;

	ENTER_FUNC(("%s\n", __func__));

	for (i = 0; i < 5; i++) {
		for (j = 0; j < 8; j++) {
			if ((bksv[i] & (1 << j)) != 0)
				counter++;
		}
	}

	if (counter == 20)
		return true;

	return false;
}

static bool it661x_hdcp_authenticate(void)
{
	unsigned char ucdata;
	unsigned char bcaps;
	unsigned short bstatus;
	unsigned short timeout;
	unsigned char bhdmimode;
	unsigned char bksv[5];

	ENTER_FUNC(("%s\n", __func__));

	gauthenticated = false;

	/* Authenticate should be called after AFE setup up. */
	it661x_hdcp_reset();

	for (timeout = 100; timeout > 0; timeout--) {
		bhdmimode = it661x_read_i2c_byte(REG_HDMI_MODE) & 1;

		if (!it661x_hdcp_get_bcaps(&bcaps, &bstatus)) {
			pr_err("it661x_hdcp_get_bcaps fail.\n");
			return false;
		}

		if (bhdmimode) {
			if (bstatus & B_CAP_HDMI_MODE)
				break;
			pr_err("Sink HDCP in DVI mode over HDMI\n");
		} else {
			if (!(bstatus & B_CAP_HDMI_MODE))
				break;
			pr_err("Sink HDCP in HDMI mode over DVI\n");
		}
	}

	if (timeout == 0) {
		if (bhdmimode) {
			if ((bstatus & B_CAP_HDMI_MODE) == 0)
				pr_err("Not a HDMI mode.\n");
		} else {
			if ((bstatus & B_CAP_HDMI_MODE) != 0)
				pr_err("Not a HDMI mode again\n");
		}
	}

	pr_err("bcaps = %02X bstatus = %04X\n", bcaps, bstatus);

	it661x_hdcp_get_bksv(bksv);

	pr_err("bksv %02X %02X %02X %02X %02X\n",
		bksv[0], bksv[1], bksv[2], bksv[3], bksv[4]);

	for (timeout = 0; timeout < 5; timeout++) {
		if (bksv[timeout] == 0xFF) {
			gauthenticated = true;
			return true;
		}
	}

	if (!it661x_hdcp_validate_bksv(bksv))
		return true;

	/* switch bank action should start on direct
	 * register writing of each function.
	 */
	SWITCH_HDMITX_BANK(0);

	/* enable HDCP on CPDired enabled. */
	ucdata = it661x_read_i2c_byte(REG_SW_RST);
	ucdata &= ~B_HDCP_RST;
	it661x_write_i2c_byte(REG_SW_RST, ucdata);

	it661x_write_i2c_byte(REG_HDCP_DESIRE, B_CPDESIRE);

	it661x_hdcp_clear_auth_interrupt();

	pr_err("int2 = %02X DDC_Status = %02X\n",
		it661x_read_i2c_byte(REG_INT_STAT2),
		it661x_read_i2c_byte(REG_DDC_STATUS));

	it661x_hdcp_generate_ancipher();

	it661x_write_i2c_byte(REG_LISTCTRL, 0);
	gauthenticated = false;

	if ((bcaps & B_CAP_HDMI_REPEATER) == 0) {
		it661x_hdcp_auth_fire();

		/* wait for status; */
		for (timeout = 250; timeout > 0; timeout--) {
			ucdata = it661x_read_i2c_byte(REG_AUTH_STAT);

			if (ucdata & B_AUTH_DONE) {
				gauthenticated = true;
				break;
			}

			ucdata = it661x_read_i2c_byte(REG_INT_STAT2);
			if (ucdata & B_INT_AUTH_FAIL) {
				pr_err("%s: authenticate fail\n", __func__);
				gauthenticated = false;
				return false;
			}
		}

		if (timeout == 0) {
			pr_err("%s: Time out. return fail\n", __func__);
			gauthenticated = false;
			return false;
		}

		return true;
	}

	return it661x_hdcp_authenticate_repeater();
}

static  unsigned char ksvlist[32];
static  unsigned char vr[20];
static  unsigned char m0[8];
static  unsigned long w[80];
static  unsigned long digest[5];

#define rol(x, y) (((x) << (y)) | (((unsigned long)x) >> (32 - y)))

static void it661x_sha_transform(unsigned long *h)
{
	long t;

	ENTER_FUNC(("%s\n", __func__));

	for (t = 16; t < 80; t++) {
		unsigned long tmp = w[t - 3] ^ w[t - 8] ^ w[t - 14] ^
						w[t - 16];

		w[t] = rol(tmp, 1);
	}

	h[0] = 0x67452301;
	h[1] = 0xefcdab89;
	h[2] = 0x98badcfe;
	h[3] = 0x10325476;
	h[4] = 0xc3d2e1f0;

	for (t = 0; t < 20; t++) {
		unsigned long tmp = rol(h[0], 5) + ((h[1] & h[2]) |
				(h[3] & ~h[1])) + h[4] + w[t] + 0x5a827999;

		h[4] = h[3];
		h[3] = h[2];
		h[2] = rol(h[1], 30);
		h[1] = h[0];
		h[0] = tmp;

	}

	for (t = 20; t < 40; t++) {
		unsigned long tmp = rol(h[0], 5) + (h[1] ^ h[2] ^ h[3]) +
			h[4] + w[t] + 0x6ed9eba1;

		h[4] = h[3];
		h[3] = h[2];
		h[2] = rol(h[1], 30);
		h[1] = h[0];
		h[0] = tmp;
	}

	for (t = 40; t < 60; t++) {
		unsigned long tmp = rol(h[0], 5) + ((h[1] & h[2]) |
				(h[1] & h[3]) | (h[2] & h[3])) + h[4] +
				w[t] + 0x8f1bbcdc;

		h[4] = h[3];
		h[3] = h[2];
		h[2] = rol(h[1], 30);
		h[1] = h[0];
		h[0] = tmp;
	}

	for (t = 60; t < 80; t++) {
		unsigned long tmp = rol(h[0], 5) + (h[1] ^ h[2] ^ h[3]) +
				h[4] + w[t] + 0xca62c1d6;

		h[4] = h[3];
		h[3] = h[2];
		h[2] = rol(h[1], 30);
		h[1] = h[0];
		h[0] = tmp;
	}

	h[0] += 0x67452301;
	h[1] += 0xefcdab89;
	h[2] += 0x98badcfe;
	h[3] += 0x10325476;
	h[4] += 0xc3d2e1f0;

}

/*
 * Outer SHA algorithm: take an arbitrary length byte string,
 * convert it into 16-word blocks with the prescribed padding at
 * the end,and pass those blocks to the core SHA algorithm.
 */
static void it661x_sha_simple(void *p, long len, unsigned char *output)
{
	int i, t;
	unsigned long c;
	char *pbuff = p;

	ENTER_FUNC(("%s\n", __func__));

	for (i = 0; i < len; i++) {
		t = i / 4;
		if (i % 4 == 0)
			w[t] = 0;

		c = pbuff[i];
		c <<= (3 - (i % 4)) * 8;
		w[t] |= c;
	}

	t = i / 4;
	if (i % 4 == 0)
		w[t] = 0;

	c = 0x80 << ((3 - i % 4) * 8);
	w[t] |= c;
	t++;
	for (; t < 15; t++)
		w[t] = 0;

	w[15] = len * 8;

	it661x_sha_transform(digest);

	for (i = 0; i < 20; i += 4) {
		output[i] = (unsigned char)(digest[i / 4] & 0xFF);
		output[i + 1] = (unsigned char)((digest[i / 4] >> 8) & 0xFF);
		output[i + 2] = (unsigned char)((digest[i / 4] >> 16) & 0xFF);
		output[i + 3] = (unsigned char)((digest[i / 4] >> 24) & 0xFF);
	}
}

static void it661x_hdcp_cancel_repeater_authenticate(void)
{
	ENTER_FUNC(("%s", __func__));

	it661x_write_i2c_byte(REG_DDC_MASTER_CTRL, B_MASTERDDC | B_MASTERHOST);

	it661x_ddc_abort();

	it661x_write_i2c_byte(REG_LISTCTRL, B_LISTFAIL | B_LISTDONE);

	it661x_hdcp_clear_auth_interrupt();
}

static void it661x_hdcp_resume_repeater_authenticate(void)
{
	ENTER_FUNC(("%s\n", __func__));
	it661x_write_i2c_byte(REG_LISTCTRL, B_LISTDONE);
	it661x_write_i2c_byte(REG_DDC_MASTER_CTRL, B_MASTERHDCP);
}


static bool it661x_hdcp_get_ksv_list(unsigned char *pksvlist,
				unsigned char cdownstream)
{
	unsigned char timeout = 100;
	unsigned char ucdata;

	ENTER_FUNC(("%s\n", __func__));

	if (pksvlist == NULL)
		return false;

	if (cdownstream == 0)
		return true;

	it661x_write_i2c_byte(REG_DDC_MASTER_CTRL, B_MASTERHOST);
	it661x_write_i2c_byte(REG_DDC_HEADER, 0x74);
	it661x_write_i2c_byte(REG_DDC_REQOFF, 0x43);
	it661x_write_i2c_byte(REG_DDC_REQCOUNT, cdownstream * 5);
	it661x_write_i2c_byte(REG_DDC_CMD, CMD_DDC_SEQ_BURSTREAD);

	for (timeout = 200; timeout > 0; timeout--) {
		ucdata = it661x_read_i2c_byte(REG_DDC_STATUS);
		if (ucdata & B_DDC_DONE) {
			pr_err("%s: DDC Done.\n", __func__);
			break;
		}

		if (ucdata & B_DDC_ERROR) {
			pr_err("%s: DDC Fail by REG_DDC_STATUS = %x.\n",
			__func__, ucdata);
			return false;
		}
	}

	if (timeout == 0)
		return false;

	for (timeout = 0; timeout < cdownstream * 5; timeout++) {
		pksvlist[timeout] = it661x_read_i2c_byte(REG_DDC_READFIFO);
		pr_err(" %02X", pksvlist[timeout]);
	}

	pr_err("\n");

	return true;
}

static  bool it661x_hdcp_get_vr(unsigned char *pvr)
{
	unsigned char timeout;
	unsigned char ucdata;

	ENTER_FUNC(("%s\n", __func__));

	if (pvr == NULL)
		return false;

	it661x_write_i2c_byte(REG_DDC_MASTER_CTRL, B_MASTERHOST);
	it661x_write_i2c_byte(REG_DDC_HEADER, 0x74);
	it661x_write_i2c_byte(REG_DDC_REQOFF, 0x20);
	it661x_write_i2c_byte(REG_DDC_REQCOUNT, 20);
	it661x_write_i2c_byte(REG_DDC_CMD, CMD_DDC_SEQ_BURSTREAD);

	for (timeout = 200; timeout > 0; timeout--) {
		ucdata = it661x_read_i2c_byte(REG_DDC_STATUS);

		if (ucdata & B_DDC_DONE) {
			pr_err("%s: DDC Done.\n", __func__);
			break;
		}

		if (ucdata & B_DDC_ERROR) {
			pr_err("%s: DDC fail by REG_DDC_STATUS = %x.\n",
			 __func__, ucdata);
			return false;
		}
	}

	if (timeout == 0) {
		pr_err("%s: DDC fail by timeout.\n", __func__);
		return false;
	}

	SWITCH_HDMITX_BANK(0);

	for (timeout = 0; timeout < 5; timeout++) {
		it661x_write_i2c_byte(REG_SHA_SEL, timeout);

		pvr[timeout * 4]   = it661x_read_i2c_byte(REG_SHA_RD_BYTE1);
		pvr[timeout * 4 + 1] = it661x_read_i2c_byte(REG_SHA_RD_BYTE2);
		pvr[timeout * 4 + 2] = it661x_read_i2c_byte(REG_SHA_RD_BYTE3);
		pvr[timeout * 4 + 3] = it661x_read_i2c_byte(REG_SHA_RD_BYTE4);
	}

	return true;
}

static bool it661x_hdcp_get_m0(unsigned char *pm0)
{
	int i;

	ENTER_FUNC(("%s\n", __func__));

	if (!pm0)
		return false;

	/* read m0[31:0] from reg51~reg54 */
	it661x_write_i2c_byte(REG_SHA_SEL, 5);

	pm0[0] = it661x_read_i2c_byte(REG_SHA_RD_BYTE1);
	pm0[1] = it661x_read_i2c_byte(REG_SHA_RD_BYTE2);
	pm0[2] = it661x_read_i2c_byte(REG_SHA_RD_BYTE3);
	pm0[3] = it661x_read_i2c_byte(REG_SHA_RD_BYTE4);

	it661x_write_i2c_byte(REG_SHA_SEL, 0);
	/* read m0[39:32] from reg55 */
	pm0[4] = it661x_read_i2c_byte(REG_AKSV_RD_BYTE5);
	it661x_write_i2c_byte(REG_SHA_SEL, 1);
	/* read m0[47:40] from reg55 */
	pm0[5] = it661x_read_i2c_byte(REG_AKSV_RD_BYTE5);
	it661x_write_i2c_byte(REG_SHA_SEL, 2);
	/* read m0[55:48] from reg55 */
	pm0[6] = it661x_read_i2c_byte(REG_AKSV_RD_BYTE5);
	it661x_write_i2c_byte(REG_SHA_SEL, 3);
	/* read m0[63:56] from reg55 */
	pm0[7] = it661x_read_i2c_byte(REG_AKSV_RD_BYTE5);

	pr_err("M[] =");
	for (i = 0; i < 8; i++)
		pr_err("0x%02x,", pm0[i]);

	pr_err("\n");
	return true;
}

static bool it661x_hdcp_check_sha(unsigned char *pm0, unsigned short bstatus,
		unsigned char *pksvlist, int cdownstream, unsigned char *vr)
{
	int i, n;
	unsigned char shabuff[64], v[20];

	ENTER_FUNC(("%s\n", __func__));

	for (i = 0; i < cdownstream * 5; i++)
		shabuff[i] = pksvlist[i];

	shabuff[i++] = bstatus & 0xFF;
	shabuff[i++] = (bstatus >> 8) & 0xFF;

	for (n = 0; n < 8; n++, i++)
		shabuff[i] = pm0[n];

	n = i;

	for ( ; i < 64; i++)
		shabuff[i] = 0;

	for (i = 0; i < 64; i++) {
		if (i % 16 == 0)
			pr_err("SHA[]: ");

		pr_err(" %02X", shabuff[i]);

		if ((i % 16) == 15)
			pr_err("\n");
	}

	it661x_sha_simple(shabuff, n, v);
	pr_err("V[] =");

	for (i = 0; i < 20; i++)
		pr_err(" %02X", v[i]);

	pr_err("\nVr[] =");
	for (i = 0; i < 20; i++)
		pr_err(" %02X", vr[i]);

	for (i = 0; i < 20; i++) {
		if (v[i] != vr[i])
			return false;
	}

	return true;
}

/*
 * Function: it661x_hdcp_authenticate_repeater
 * Parameter: BCaps and BStatus
 * Return: true if success,if AUTH_FAIL interrupt status,return fail.
 * Remark:
 * Side-Effect: as Authentication
 */
static bool it661x_hdcp_authenticate_repeater(void)
{
	unsigned char uc;
	unsigned char cdownstream;
	unsigned char bcaps;
	unsigned short bstatus;
	unsigned short timeout;

	ENTER_FUNC(("%s\n", __func__));

	/* Authenticate Fired */
	it661x_hdcp_get_bcaps(&bcaps, &bstatus);

	it661x_hdcp_auth_fire();

	for (timeout = 250*8; timeout > 0; timeout--) {

		uc = it661x_read_i2c_byte(REG_INT_STAT1);
		if (uc & B_INT_DDC_BUS_HANG) {
			pr_err("DDC Bus hang\n");
			goto hdcp_repeater_fail;
		}

		uc = it661x_read_i2c_byte(REG_INT_STAT2);

		if (uc & B_INT_AUTH_FAIL) {
			pr_err("%s():B_INT_AUTH_FAIL.\n", __func__);
			goto hdcp_repeater_fail;
		}

		if (uc & B_INT_KSVLIST_CHK) {
			it661x_write_i2c_byte(REG_INT_CLR0, B_CLR_KSVLISTCHK);
			it661x_write_i2c_byte(REG_INT_CLR1, 0);
			it661x_write_i2c_byte(REG_SYS_STATUS, B_INTACTDONE);
			it661x_write_i2c_byte(REG_SYS_STATUS, 0);
			pr_err("B_INT_KSVLIST_CHK\n");
			break;
		}
	}

	/*
	 * clear ksv list check interrupt.
	 */

	for (timeout = 500; timeout > 0; timeout--) {
		if ((timeout % 100) == 0)
			pr_err("Wait KSV FIFO Ready %d\n", timeout);

		if (it661x_hdcp_get_bcaps(&bcaps, &bstatus) == false) {
			pr_err("Get BCaps fail\n");
			goto hdcp_repeater_fail;
		}

		if (bcaps & B_CAP_KSV_FIFO_RDY) {
			pr_err("FIFO Ready\n");
			break;
		}
	}

	if (timeout == 0) {
		pr_err("Get KSV FIFO ready TimeOut\n");
		goto hdcp_repeater_fail;
	}

	pr_err("Wait timeout = %d\n", timeout);

	it661x_ddc_clear_fifo();
	it661x_ddc_generate_sclk();
	cdownstream =  (bstatus & M_DOWNSTREAM_COUNT);

	if (cdownstream > 6 || bstatus & (B_MAX_CASCADE_EXCEEDED |
					B_DOWNSTREAM_OVER)) {
		pr_err("Invalid Down stream count,fail\n");
		goto hdcp_repeater_fail;
	}

	if (it661x_hdcp_get_ksv_list(ksvlist, cdownstream) == false)
		goto hdcp_repeater_fail;

	if (it661x_hdcp_get_vr(vr) == false)
		goto hdcp_repeater_fail;

	if (it661x_hdcp_get_m0(m0) == false)
		goto hdcp_repeater_fail;

	/* do check SHA */
	if (it661x_hdcp_check_sha(m0, bstatus, ksvlist,
				cdownstream, vr) == false)
		goto hdcp_repeater_fail;

	it661x_hdcp_resume_repeater_authenticate();

	gauthenticated = true;

	return true;

hdcp_repeater_fail:
	it661x_hdcp_cancel_repeater_authenticate();

	return false;
}

/*
 * Function: it661x_hdcp_resume_authentication
 * Parameter: N/A
 * Return: N/A
 * Remark: called by interrupt handler to restart
 *	Authentication and Encryption.
 * Side-Effect: as Authentication and Encryption.
 */
static void it661x_hdcp_resume_authentication(void)
{
	ENTER_FUNC(("%s\n", __func__));
	it661x_set_avmute(true);

	if (it661x_hdcp_authenticate() == true)
		it661x_hdcp_enable_encryption();

	it661x_set_avmute(false);
}

#endif /* end define of SUPPORT_HDCP */
