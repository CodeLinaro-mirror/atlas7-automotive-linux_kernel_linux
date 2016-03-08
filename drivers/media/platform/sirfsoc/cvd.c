/*
 * CSR SiRF Atlas7DA CVD driver
 *
 * Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/async.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <linux/time.h>
#include <linux/version.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/videodev2.h>
#include <linux/ioctl.h>
#include <linux/slab.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/memblock.h>
#include <linux/vmalloc.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>

#include "cvd.h"


#ifndef MODULE
#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX
#endif

#define CVD_DRV_NAME "sirfsoc-cvd"
#define CVD_DUMP(fmt, ...)	pr_info(fmt, ## __VA_ARGS__)

#define FIELD_SKIP_NUM		4
#define VSYNC_DELAY_LINE	0


struct cvd_dev {
	struct device		*dev;
	struct clk		*clk;
	unsigned int		irq;
	struct resource		*res;
	void __iomem		*io_base;

	u32			input_port;
	s32			saturation;
	s32			brightness;
	s32			contrast;
	s32			hue;
	v4l2_std_id		norm;
	struct v4l2_subdev	sd;
	struct v4l2_ctrl_handler hdl;

	bool			norm_detected;

	int			skip_count;
	struct completion	skip_done;	/* skip unstable fields */
	struct completion	locked_done;	/* get locked signals */
};

struct cvd_reg {
	u16		reg_addr;
	u32		reg_value;
};


#define cvd_write(addr, value, sd)	\
		writel((value), (to_state(sd)->io_base) + (addr))
#define cvd_read(addr, sd)	\
		readl((to_state(sd)->io_base) + (addr))


static const struct cvd_reg config_ntsc[] = {
	{CVBSD_AFEPWR_EN,		0x3}, /* must PWR on before setting */
	{CVBSD_AGC_GATE_THRE_ADC_SWAP,	0x80},
	{CVBSD_CVD1_CONTROL0,		0x0},	/* NTSC format */
	{CVBSD_CVD1_CONTROL1,		0x1},
	{CVBSD_YC_SEPARATION,		0x7000},
	{CVBSD_OUTPUT_CONTROL,		0x20}, /* BT601 UYVY output */
	{CVBSD_CHROMA_AGC,		0x8A},
	{CVBSD_CHROMA_DTO_INCREMENT,	0x233ea847},
	{CVBSD_HSYNC_DTO_INCREMENT,	0x213b13b1},
	{CVBSD_SECAM_DR_FREQ_OFFSET,	0x4E},
	{CVBSD_SECAM_DB_FREQ_OFFSET,	0xEFC},
	{CVBSD_CHROMA_BURST_GATE_START,	0x46},
	{CVBSD_CHROMA_BURST_GATE_END,	0x5A},
	{CVBSD_CVD2_2D_COMB_ADAP_CTRL2,	0x58},
	{CVBSD_CVD2_CHROMA_EDGE_ENHANC,	0x23},
	{CVBSD_ACTIVE_VIDEO_VSTART,	0x24},
	{CVBSD_ACTIVE_VIDEO_VHEIGHT,	0x63},
	{CVBSD_DIFF_GAIN,		0x3A},
	{CVBSD_CAGC_TIME_CONSTANT,	0x25},
	{CVBSD_CORDIC_GATE_START,	0x3C},
	{CVBSD_CORDIC_GATE_END,		0x6E},
	{CVBSD_CTRL0,			0x809},
	{CVBSD_CAGC_GATE_START,		0x32},
	{CVBSD_CAGC_GATE_END,		0x50},
	{CVBSD_MD_LUMA_FLATFIELD_CASE1_2, 0x20},
	{CVBSD_MD_LUMA_FLATFIELD_CASE1_3, 0x28},
	{CVBSD_MD_LUMA_FLATFIELD_CASE2_3, 0x28},
	{CVBSD_MD_LUMA_FLATFIELD_CASE3_3, 0x28},
	{CVBSD_MD_LUMA_FLATFIELD_CASE4_3, 0x28},
	{CVBSD_MD_INTER_COMB,		0x8},
	{CVBSD_HV_DELAY_VSTART,		0x18A0083},
	{CVBSD_VACTIVE_HV_WINDOW,	0x12800FC},
	{CVBSD_VTOTAL_CONFIG,		0x107020D},
	{CVBSD_AFEPWR_EN,		0x1} /* PWR off after setting */
};

static const struct cvd_reg config_pal[] = {
	{CVBSD_AFEPWR_EN,		0x3}, /* must PWR on before setting */
	{CVBSD_AGC_GATE_THRE_ADC_SWAP,	0x80},
	{CVBSD_CVD1_CONTROL0,		0x32},	/* PAL (I,B,G,H,D,N) */
	{CVBSD_CVD1_CONTROL1,		0x0},
	{CVBSD_YC_SEPARATION,		0x7000},
	{CVBSD_OUTPUT_CONTROL,		0x20},
	{CVBSD_CHROMA_AGC,		0x67},
	{CVBSD_CHROMA_DTO_INCREMENT,	0x2ba77297},
	{CVBSD_HSYNC_DTO_INCREMENT,	0x213b13b1},
	{CVBSD_SECAM_DR_FREQ_OFFSET,	0x4E},
	{CVBSD_SECAM_DB_FREQ_OFFSET,	0xEFC},
	{CVBSD_CHROMA_BURST_GATE_START,	0x42},
	{CVBSD_CHROMA_BURST_GATE_END,	0x56},
	{CVBSD_ACTIVE_VIDEO_VSTART,	0x2E}, /* skip 23 line blanking data */
	{CVBSD_ACTIVE_VIDEO_VHEIGHT,	0xC0},
	{CVBSD_DIFF_GAIN,		0x1A},
	{CVBSD_CVD2_2D_COMB_ADAP_CTRL2,	0x58},
	{CVBSD_CVD2_CHROMA_EDGE_ENHANC,	0x23},
	{CVBSD_CAGC_TIME_CONSTANT,	0x5},
	{CVBSD_CORDIC_GATE_START,	0x46},
	{CVBSD_CORDIC_GATE_END,		0x5A},
	{CVBSD_CTRL0,			0x9},
	{CVBSD_CAGC_GATE_START,		0x37},
	{CVBSD_CAGC_GATE_END,		0x4B},
	{CVBSD_MD_LUMA_FLATFIELD_CASE1_2, 0x0A},
	{CVBSD_MD_LUMA_FLATFIELD_CASE1_3, 0x1E},
	{CVBSD_MD_LUMA_FLATFIELD_CASE2_3, 0x23},
	{CVBSD_MD_LUMA_FLATFIELD_CASE3_3, 0x28},
	{CVBSD_MD_LUMA_FLATFIELD_CASE4_3, 0x2D},
	{CVBSD_MD_INTER_COMB,		0x18},
	{CVBSD_HV_DELAY_VSTART,		0x1D5009C},
	{CVBSD_VACTIVE_HV_WINDOW,	0x15C012C},
	{CVBSD_VTOTAL_CONFIG,		0x1390271},
	{CVBSD_AFEPWR_EN,		0x1} /* PWR off after setting */
};

static inline struct cvd_dev *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct cvd_dev, sd);
}

static inline int get_fid(struct v4l2_subdev *sd)
{
	return (cvd_read(CVBSD_CVD1_STATUS_REGISTER_2, sd) >> 6) & 0x1;
}


static const struct cvd_reg initial_registers[] = {
	/* Initialize AFE */
	/* reset CVBSAFE active*/
	{CVBSD_AFEPWR_EN,		0x0},
	/* enable CVBSAFE active*/
	{CVBSD_AFEPWR_EN,		0x2},
	/* reset CVBSAFE inactive */
	{CVBSD_AFEPWR_EN,		0x3},
	/* Enable all AFE sub-circuits */
	{CVBSD_AFE_REG0,		0xff},
	/* Maybe need to adjust for another source by [7:4] */
	{CVBSD_AFE_REG3,		0x0},
	/* Enables the digital clamp up/down pulses */
	{CVBSD_AFE_REG5,		0x80},
	/* CVBS0 input */
	{CVBSD_AFE_REG7,		0x2},
	/* Initialize analog NTSC */
	/* swap the DC clamp up/down controls to the analog front-end */
	{CVBSD_AGC_GATE_THRE_ADC_SWAP,	0x80},
	/* 2D mode, fully adaptive comb */
	{CVBSD_YC_SEPARATION,		0x7000},
	/* CCIR601 UYVY output, auto blue screen mode */
	{CVBSD_OUTPUT_CONTROL,		0x20},
	/* chroma DTO increment */
	{CVBSD_CHROMA_DTO_INCREMENT,	0x233ea847},
	/* horizontal sync DTO increment */
	{CVBSD_HSYNC_DTO_INCREMENT,	0x213b13b1},
	/* secam black level adjustment on the DR color compenent */
	{CVBSD_SECAM_DR_FREQ_OFFSET,	0x4E},
	/*  secam black level adjustment on the DB color compenent */
	{CVBSD_SECAM_DB_FREQ_OFFSET,	0xEFC},
	/* 2D YC separation mode, no frame buffer is required */
	{CVBSD_CVD2_2D_COMB_ADAP_CTRL2,	0x58},
	/*  peak gain for the primary&secondary chroma edge enhancement */
	{CVBSD_CVD2_CHROMA_EDGE_ENHANC,	0x23},
	/*  the first active video line in a field, the number of half-lines */
	{CVBSD_ACTIVE_VIDEO_VSTART,	0x24},
	/*  the active video height, the number of half lines, 384 is added */
	{CVBSD_ACTIVE_VIDEO_VHEIGHT,	0x63},
	/* Setting color */
	{CVBSD_LUMA_CONTRAST,		0x80},	/* brightness: default */
	{CVBSD_LUMA_BRIGHTNESS,		0x20},	/* contrast: default */
	{CVBSD_CHROMA_SATURATION,	0x80},	/* saturation: default */
	{CVBSD_CHROMA_HUE,		0x0},	/* hue: default */

	{CVBSD_VDETCET_IMPROVEMENT,	0x303},	/* vfield hoffset fixed mode */
	{CVBSD_VFIELD_HOFFSET_LSB,	0x50},

	/* full ADC range, chroma & luma AGC enable */
	{CVBSD_CVD1_CONTROL2,		0x143},

	/* enlarge the sharpness of the picture */
	{CVBSD_CVD1_COMB_FILTER_THRE1,	0x15},

	{CVBSD_AFEPWR_EN,		0x1} /* PWR off after setting */
};


static int cvd_detect_video_signal(struct v4l2_subdev *sd)
{
	bool	fc_more_flag;
	bool	fc_less_flag;
	bool	fc_same_flag;
	unsigned int cvd1_status_1, cvd1_status_3;
	unsigned int fc_more_threshold, fc_less_threshold, freq_status;

	cvd1_status_1 = cvd_read(CVBSD_CVD1_STATUS_REGISTER_1, sd);
	cvd1_status_3 = cvd_read(CVBSD_CVD1_STATUS_REGISTER_3, sd);

	fc_more_threshold = 128 + 80;
	fc_less_threshold = 128 - 80;
	freq_status = (cvd_read(CVBSD_CORDIC_FREQ_STATUS, sd) + 0x80) & 0xFF;

	fc_more_flag = (freq_status > fc_more_threshold) ? true : false;
	fc_less_flag = (freq_status < fc_less_threshold) ? true : false;
	fc_same_flag = ((freq_status >= fc_less_threshold)
			&& (freq_status <= fc_more_threshold)) ? true : false;

	if (cvd1_status_1 & 0xE) {
		if (!(cvd1_status_3 & 0x4) /* !(625 scan lines detected) */
			&& !(cvd1_status_3 & 0x1)	/* !(PAL detected) */
			&& fc_same_flag) {
			/*
			* Note analog video standard maybe NTSC or PAL_M
			* in this condition, need check it further.
			*/
			return V4L2_STD_NTSC;
		} else if (!(cvd1_status_3 & 0x4) && (cvd1_status_3 & 0x1)
			&& fc_same_flag) {
			return V4L2_STD_PAL_60;
		} else if (!(cvd1_status_3 & 0x4) && !fc_same_flag) {
			return V4L2_STD_NTSC_443;
		} else if ((cvd1_status_3 & 0x4) && !fc_same_flag) {
			return V4L2_STD_PAL_I;
		} else if ((cvd1_status_3 & 0x4) && fc_same_flag) {
			return V4L2_STD_PAL_Nc;
		}
	}

	return -EIO;
}

static int cvd_g_std(struct v4l2_subdev *sd, v4l2_std_id *norm)
{
	struct cvd_dev *dec = to_state(sd);

	*norm = dec->norm;

	return 0;
}

static int cvd_s_std(struct v4l2_subdev *sd, v4l2_std_id norm)
{
	struct cvd_dev *dec = to_state(sd);
	int i;

	if (!(norm & (V4L2_STD_NTSC | V4L2_STD_PAL)))
		return -EINVAL;

	if (norm & V4L2_STD_NTSC) {
		for (i = 0; i < ARRAY_SIZE(config_ntsc); i++)
			cvd_write(config_ntsc[i].reg_addr,
						config_ntsc[i].reg_value, sd);
	}

	if (norm & V4L2_STD_PAL) {
		for (i = 0; i < ARRAY_SIZE(config_pal); i++)
			cvd_write(config_pal[i].reg_addr,
						config_pal[i].reg_value, sd);
	}

	dec->norm = norm;

	return 0;
}

/* Interrupt handler */
static int cvd_isr(struct v4l2_subdev *sd, u32 status, bool *handled)
{
	u32 val;
	struct cvd_dev *dec = to_state(sd);

	/* CVD ext locked interrupt */
	if ((cvd_read(CVBSD_DEBUG_INTERRUPT, sd) & 0x2) &&
						(status & DEBUG_INT_MASK)) {
		/* cleared by writing 1 to the bit */
		cvd_write(CVBSD_DEBUG_INTERRUPT, 0x2, sd);

		/* only need once, then disable ext locked interrupt */
		val = cvd_read(CVBSD_DEBUG_INTERRUPT_MASK, sd);
		val &= ~0x2;
		cvd_write(CVBSD_DEBUG_INTERRUPT_MASK, val, sd);

		/* now all the PLLs are locked */
		complete(&dec->locked_done);
	}

	/* CVD vsync interrupt */
	if ((cvd_read(CVBSD_INTERRUPT_CONFIG, sd) & 0x1) &&
						(status & CVD3_INT_MASK)) {
		/*
		* Cleared by writing 0 to INTERRUPT_CONFIG.enable register,
		* also disable the vsync interrupt.
		*/
		cvd_write(CVBSD_INTERRUPT_CONFIG, 0x0, sd);

		/*
		* Field ID value is not reliable in the beginning time
		* even all the signals are locked, so we have to skip
		* the first several fields.
		*/
		if (dec->skip_count) {
			/*
			* Nothing to do, only re-enable vsync interrupt
			* and waiting for the next field coming.
			*
			* To make sure interrupt won't come in the blanking
			* time that it's too short(~1ms) for SW to complete
			* the subsequent works, so we have to set the interrupt
			* to several lines delayed to out of blanking area.
			*/
			cvd_write(CVBSD_INTERRUPT_CONFIG, 0x1 |
						(VSYNC_DELAY_LINE << 4), sd);
			dec->skip_count--;
			goto out;
		}

		complete(&dec->skip_done);
	}

out:
	*handled = true;

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static void cvd_print_regs(struct v4l2_subdev *sd)
{
	CVD_DUMP("CVD registers:\n");
	CVD_DUMP("CVBSD_CVD1_CONTROL0=0x%08x\n",
				cvd_read(CVBSD_CVD1_CONTROL0, sd));
	CVD_DUMP("CVBSD_CVD1_CONTROL1=0x%08x\n",
				cvd_read(CVBSD_CVD1_CONTROL1, sd));
	CVD_DUMP("CVBSD_CVD1_CONTROL2=0x%08x\n",
				cvd_read(CVBSD_CVD1_CONTROL2, sd));
	CVD_DUMP("CVBSD_YC_SEPARATION=0x%08x\n",
				cvd_read(CVBSD_YC_SEPARATION, sd));
	CVD_DUMP("CVBSD_LUMA_AGC_VALUE=0x%08x\n",
				cvd_read(CVBSD_LUMA_AGC_VALUE, sd));
	CVD_DUMP("CVBSD_NOISE_THRESHOLD=0x%08x\n",
				cvd_read(CVBSD_NOISE_THRESHOLD, sd));
	CVD_DUMP("CVBSD_AGC_GATE_THRE_ADC_SWAP=0x%08x\n",
				cvd_read(CVBSD_AGC_GATE_THRE_ADC_SWAP, sd));
	CVD_DUMP("CVBSD_OUTPUT_CONTROL=0x%08x\n",
				cvd_read(CVBSD_OUTPUT_CONTROL, sd));
	CVD_DUMP("CVBSD_LUMA_CONTRAST=0x%08x\n",
				cvd_read(CVBSD_LUMA_CONTRAST, sd));
	CVD_DUMP("CVBSD_LUMA_BRIGHTNESS=0x%08x\n",
				cvd_read(CVBSD_LUMA_BRIGHTNESS, sd));
	CVD_DUMP("CVBSD_CHROMA_SATURATION=0x%08x\n",
				cvd_read(CVBSD_CHROMA_SATURATION, sd));
	CVD_DUMP("CVBSD_CHROMA_HUE=0x%08x\n",
				cvd_read(CVBSD_CHROMA_HUE, sd));
	CVD_DUMP("CVBSD_CHROMA_AGC=0x%08x\n",
				cvd_read(CVBSD_CHROMA_AGC, sd));
	CVD_DUMP("CVBSD_CHROMA_KILL=0x%08x\n",
				cvd_read(CVBSD_CHROMA_KILL, sd));
	CVD_DUMP("CVBSD_NONSTANDARD_THRESHOLD=0x%08x\n",
				cvd_read(CVBSD_NONSTANDARD_THRESHOLD, sd));
	CVD_DUMP("CVBSD_CVD1_CONTROL3=0x%08x\n",
				cvd_read(CVBSD_CVD1_CONTROL3, sd));
	CVD_DUMP("CVBSD_AGC_PEAK_NOMINAL=0x%08x\n",
				cvd_read(CVBSD_AGC_PEAK_NOMINAL, sd));
	CVD_DUMP("CVBSD_AGC_PEAK_GATE_CONTROLS=0x%08x\n",
				cvd_read(CVBSD_AGC_PEAK_GATE_CONTROLS, sd));
	CVD_DUMP("CVBSD_BLUE_SCREEN_Y=0x%08x\n",
				cvd_read(CVBSD_BLUE_SCREEN_Y, sd));
	CVD_DUMP("CVBSD_BLUE_SCREEN_CB=0x%08x\n",
				cvd_read(CVBSD_BLUE_SCREEN_CB, sd));
	CVD_DUMP("CVBSD_BLUE_SCREEN_CR=0x%08x\n",
				cvd_read(CVBSD_BLUE_SCREEN_CR, sd));
	CVD_DUMP("CVBSD_HDETECT_CLAMP_LEVEL=0x%08x\n",
				cvd_read(CVBSD_HDETECT_CLAMP_LEVEL, sd));
	CVD_DUMP("CVBSD_LOCK_COUNT=0x%08x\n",
				cvd_read(CVBSD_LOCK_COUNT, sd));
	CVD_DUMP("CVBSD_H_LOOP_MAXSTATE=0x%08x\n",
				cvd_read(CVBSD_H_LOOP_MAXSTATE, sd));
	CVD_DUMP("CVBSD_CHROMA_DTO_INCREMENT=0x%08x\n",
				cvd_read(CVBSD_CHROMA_DTO_INCREMENT, sd));
	CVD_DUMP("CVBSD_HSYNC_DTO_INCREMENT=0x%08x\n",
				cvd_read(CVBSD_HSYNC_DTO_INCREMENT, sd));
	CVD_DUMP("CVBSD_HSYNC_RISING_EDGE_TIME=0x%08x\n",
				cvd_read(CVBSD_HSYNC_RISING_EDGE_TIME, sd));
	CVD_DUMP("CVBSD_HSYNC_PHASE_OFFSET=0x%08x\n",
				cvd_read(CVBSD_HSYNC_PHASE_OFFSET, sd));
	CVD_DUMP("CVBSD_HSYNC_DETECT_START_TIME=0x%08x\n",
				cvd_read(CVBSD_HSYNC_DETECT_START_TIME, sd));
	CVD_DUMP("CVBSD_HSYNC_DETECT_END_TIME=0x%08x\n",
				cvd_read(CVBSD_HSYNC_DETECT_END_TIME, sd));
	CVD_DUMP("CVBSD_HSYNC_TIP_DETECTION=0x%08x\n",
				cvd_read(CVBSD_HSYNC_TIP_DETECTION, sd));
	CVD_DUMP("CVBSD_STATUS_HSYNC_WIDTH=0x%08x\n",
				cvd_read(CVBSD_STATUS_HSYNC_WIDTH, sd));
	CVD_DUMP("CVBSD_HSYNC_RISING_EDGE_START=0x%08x\n",
				cvd_read(CVBSD_HSYNC_RISING_EDGE_START, sd));
	CVD_DUMP("CVBSD_HSYNC_RISING_EDGE_END=0x%08x\n",
				cvd_read(CVBSD_HSYNC_RISING_EDGE_END, sd));
	CVD_DUMP("CVBSD_STATUS_BURST_MAG=0x%08x\n",
				cvd_read(CVBSD_STATUS_BURST_MAG, sd));
	CVD_DUMP("CVBSD_HBLANK_START=0x%08x\n",
				cvd_read(CVBSD_HBLANK_START, sd));
	CVD_DUMP("CVBSD_HBLANK_END=0x%08x\n",
				cvd_read(CVBSD_HBLANK_END, sd));
	CVD_DUMP("CVBSD_CHROMA_BURST_GATE_START=0x%08x\n",
				cvd_read(CVBSD_CHROMA_BURST_GATE_START, sd));
	CVD_DUMP("CVBSD_CHROMA_BURST_GATE_END=0x%08x\n",
				cvd_read(CVBSD_CHROMA_BURST_GATE_END, sd));
	CVD_DUMP("CVBSD_ACTIVE_VIDEO_HSTART=0x%08x\n",
				cvd_read(CVBSD_ACTIVE_VIDEO_HSTART, sd));
	CVD_DUMP("CVBSD_ACTIVE_VIDEO_HWIDTH=0x%08x\n",
				cvd_read(CVBSD_ACTIVE_VIDEO_HWIDTH, sd));
	CVD_DUMP("CVBSD_ACTIVE_VIDEO_VSTART=0x%08x\n",
				cvd_read(CVBSD_ACTIVE_VIDEO_VSTART, sd));
	CVD_DUMP("CVBSD_ACTIVE_VIDEO_VHEIGHT=0x%08x\n",
				cvd_read(CVBSD_ACTIVE_VIDEO_VHEIGHT, sd));
	CVD_DUMP("CVBSD_VSYNC_H_LOCKOUT_START=0x%08x\n",
				cvd_read(CVBSD_VSYNC_H_LOCKOUT_START, sd));
	CVD_DUMP("CVBSD_VSYNC_H_LOCKOUT_END=0x%08x\n",
				cvd_read(CVBSD_VSYNC_H_LOCKOUT_END, sd));
	CVD_DUMP("CVBSD_VSYNC_AGC_LOCKOUT_START=0x%08x\n",
				cvd_read(CVBSD_VSYNC_AGC_LOCKOUT_START, sd));
	CVD_DUMP("CVBSD_VSYNC_AGC_LOCKOUT_END=0x%08x\n",
				cvd_read(CVBSD_VSYNC_AGC_LOCKOUT_END, sd));
	CVD_DUMP("CVBSD_VSYNC_VBI_LOCKOUT_START=0x%08x\n",
				cvd_read(CVBSD_VSYNC_VBI_LOCKOUT_START, sd));
	CVD_DUMP("CVBSD_VSYNC_VBI_LOCKOUT_END=0x%08x\n",
				cvd_read(CVBSD_VSYNC_VBI_LOCKOUT_END, sd));
	CVD_DUMP("CVBSD_VSYNC_CNTL=0x%08x\n",
				cvd_read(CVBSD_VSYNC_CNTL, sd));
	CVD_DUMP("CVBSD_VSYNC_TIME_CONSTANT=0x%08x\n",
				cvd_read(CVBSD_VSYNC_TIME_CONSTANT, sd));
	CVD_DUMP("CVBSD_CVD1_STATUS_REGISTER_1=0x%08x\n",
				cvd_read(CVBSD_CVD1_STATUS_REGISTER_1, sd));
	CVD_DUMP("CVBSD_CVD1_STATUS_REGISTER_2=0x%08x\n",
				cvd_read(CVBSD_CVD1_STATUS_REGISTER_2, sd));
	CVD_DUMP("CVBSD_CVD1_STATUS_REGISTER_3=0x%08x\n",
				cvd_read(CVBSD_CVD1_STATUS_REGISTER_3, sd));
	CVD_DUMP("CVBSD_CVD1_DEBUG_ANALOG=0x%08x\n",
				cvd_read(CVBSD_CVD1_DEBUG_ANALOG, sd));
	CVD_DUMP("CVBSD_CVD1_DEBUG_DIGITAL=0x%08x\n",
				cvd_read(CVBSD_CVD1_DEBUG_DIGITAL, sd));
	CVD_DUMP("CVBSD_CVD1_RESET_REGISTER=0x%08x\n",
				cvd_read(CVBSD_CVD1_RESET_REGISTER, sd));
	CVD_DUMP("CVBSD_HSYNC_DTO_INC_STATUS=0x%08x\n",
				cvd_read(CVBSD_HSYNC_DTO_INC_STATUS, sd));
	CVD_DUMP("CVBSD_CSYNC_DTO_INC_STATUS=0x%08x\n",
				cvd_read(CVBSD_CSYNC_DTO_INC_STATUS, sd));
	CVD_DUMP("CVBSD_AGC_ANALOG_GAIN_STATUS=0x%08x\n",
				cvd_read(CVBSD_AGC_ANALOG_GAIN_STATUS, sd));
	CVD_DUMP("CVBSD_CHROMA_MAGNITUDE_STATUS=0x%08x\n",
				cvd_read(CVBSD_CHROMA_MAGNITUDE_STATUS, sd));
	CVD_DUMP("CVBSD_CHROMA_GAIN_STATUS=0x%08x\n",
				cvd_read(CVBSD_CHROMA_GAIN_STATUS, sd));
	CVD_DUMP("CVBSD_CORDIC_FREQ_STATUS=0x%08x\n",
				cvd_read(CVBSD_CORDIC_FREQ_STATUS, sd));
	CVD_DUMP("CVBSD_CVD1_SYNC_HEIGHT_STATUS=0x%08x\n",
				cvd_read(CVBSD_CVD1_SYNC_HEIGHT_STATUS, sd));
	CVD_DUMP("CVBSD_CVD1_NOISE_STATUS=0x%08x\n",
				cvd_read(CVBSD_CVD1_NOISE_STATUS, sd));
	CVD_DUMP("CVBSD_CVD1_COMB_FILTER_THRE1=0x%08x\n",
				cvd_read(CVBSD_CVD1_COMB_FILTER_THRE1, sd));
	CVD_DUMP("CVBSD_CVD1_COMB_FILTER_CONFIG=0x%08x\n",
				cvd_read(CVBSD_CVD1_COMB_FILTER_CONFIG, sd));
	CVD_DUMP("CVBSD_CVD1_CHROMA_LOCK_CONFIG0=0x%08x\n",
				cvd_read(CVBSD_CVD1_CHROMA_LOCK_CONFIG0, sd));
	CVD_DUMP("CVBSD_CVD1_LOSE_CHROMALOCK_MODE=0x%08x\n",
				cvd_read(CVBSD_CVD1_LOSE_CHROMALOCK_MODE, sd));
	CVD_DUMP("CVBSD_CVD1_NONSTANDARD_STATUS=0x%08x\n",
				cvd_read(CVBSD_CVD1_NONSTANDARD_STATUS, sd));
	CVD_DUMP("CVBSD_CSTRIPE_DETECT_CONTROL=0x%08x\n",
				cvd_read(CVBSD_CSTRIPE_DETECT_CONTROL, sd));
	CVD_DUMP("CVBSD_CHROMA_LOCKING_RANGE=0x%08x\n",
				cvd_read(CVBSD_CHROMA_LOCKING_RANGE, sd));
	CVD_DUMP("CVBSD_CSTATE_CONTROL=0x%08x\n",
				cvd_read(CVBSD_CSTATE_CONTROL, sd));
	CVD_DUMP("CVBSD_CVD1_CHROMA_HRESAMP_CTRL=0x%08x\n",
				cvd_read(CVBSD_CVD1_CHROMA_HRESAMP_CTRL, sd));
	CVD_DUMP("CVBSD_CVD1_CHARGE_PUMP_DE_CTRL=0x%08x\n",
				cvd_read(CVBSD_CVD1_CHARGE_PUMP_DE_CTRL, sd));
	CVD_DUMP("CVBSD_CVD1_CHARGE_PUMP_ADJUST=0x%08x\n",
				cvd_read(CVBSD_CVD1_CHARGE_PUMP_ADJUST, sd));
	CVD_DUMP("CVBSD_CVD1_CHARGE_PUMP_DELAY0=0x%08x\n",
				cvd_read(CVBSD_CVD1_CHARGE_PUMP_DELAY0, sd));
	CVD_DUMP("CVBSD_CVD1_MV_SEL=0x%08x\n",
				cvd_read(CVBSD_CVD1_MV_SEL, sd));
	CVD_DUMP("CVBSD_CPUMP_KILL=0x%08x\n",
				cvd_read(CVBSD_CPUMP_KILL, sd));
	CVD_DUMP("CVBSD_CVBS_Y_DELAY=0x%08x\n",
				cvd_read(CVBSD_CVBS_Y_DELAY, sd));
	CVD_DUMP("CVBSD_RGB_CONTROL1=0x%08x\n",
				cvd_read(CVBSD_RGB_CONTROL1, sd));
	CVD_DUMP("CVBSD_RGB_SATURATION=0x%08x\n",
				cvd_read(CVBSD_RGB_SATURATION, sd));
	CVD_DUMP("CVBSD_RGB_CONTRAST=0x%08x\n",
				cvd_read(CVBSD_RGB_CONTRAST, sd));
	CVD_DUMP("CVBSD_RGB_BRIGHTNESS=0x%08x\n",
				cvd_read(CVBSD_RGB_BRIGHTNESS, sd));
	CVD_DUMP("CVBSD_RGB_CONTROL2=0x%08x\n",
				cvd_read(CVBSD_RGB_CONTROL2, sd));
	CVD_DUMP("CVBSD_RGB_FB_DELAY=0x%08x\n",
				cvd_read(CVBSD_RGB_FB_DELAY, sd));
	CVD_DUMP("CVBSD_RGB_NOMINAL_DELAY=0x%08x\n",
				cvd_read(CVBSD_RGB_NOMINAL_DELAY, sd));
	CVD_DUMP("CVBSD_RGB_Y_DELAY=0x%08x\n",
				cvd_read(CVBSD_RGB_Y_DELAY, sd));
	CVD_DUMP("CVBSD_RGB_BLEND=0x%08x\n",
				cvd_read(CVBSD_RGB_BLEND, sd));
	CVD_DUMP("CVBSD_FRAME_COUNT=0x%08x\n",
				cvd_read(CVBSD_FRAME_COUNT, sd));
	CVD_DUMP("CVBSD_CVD1_CPUMP_AUTO=0x%08x\n",
				cvd_read(CVBSD_CVD1_CPUMP_AUTO, sd));
	CVD_DUMP("CVBSD_CVD1_CPUMP_AUTO1=0x%08x\n",
				cvd_read(CVBSD_CVD1_CPUMP_AUTO1, sd));
	CVD_DUMP("CVBSD_CVD1_CPUMP_UP_MAX=0x%08x\n",
				cvd_read(CVBSD_CVD1_CPUMP_UP_MAX, sd));
	CVD_DUMP("CVBSD_CVD1_CPUMP_DN_MAX=0x%08x\n",
				cvd_read(CVBSD_CVD1_CPUMP_DN_MAX, sd));
	CVD_DUMP("CVBSD_CVD1_CPUMP_UP_DIFF_MAX=0x%08x\n",
				cvd_read(CVBSD_CVD1_CPUMP_UP_DIFF_MAX, sd));
	CVD_DUMP("CVBSD_CVD1_CPUMP_DN_DIFF_MAX=0x%08x\n",
				cvd_read(CVBSD_CVD1_CPUMP_DN_DIFF_MAX, sd));
	CVD_DUMP("CVBSD_CVD1_CPUMP_Y_OVERRIDE=0x%08x\n",
				cvd_read(CVBSD_CVD1_CPUMP_Y_OVERRIDE, sd));
	CVD_DUMP("CVBSD_CVD1_CPUMP_PB_OVERRIDE=0x%08x\n",
				cvd_read(CVBSD_CVD1_CPUMP_PB_OVERRIDE, sd));
	CVD_DUMP("CVBSD_CVD1_CPUMP_PR_OVERRIDE=0x%08x\n",
				cvd_read(CVBSD_CVD1_CPUMP_PR_OVERRIDE, sd));
	CVD_DUMP("CVBSD_SECAM_DR_FREQ_OFFSET=0x%08x\n",
				cvd_read(CVBSD_SECAM_DR_FREQ_OFFSET, sd));
	CVD_DUMP("CVBSD_SECAM_DB_FREQ_OFFSET=0x%08x\n",
				cvd_read(CVBSD_SECAM_DB_FREQ_OFFSET, sd));
	CVD_DUMP("CVBSD_COMB_NOIST_THRESHOLD=0x%08x\n",
				cvd_read(CVBSD_COMB_NOIST_THRESHOLD, sd));
	CVD_DUMP("CVBSD_DIFF_GAIN=0x%08x\n",
				cvd_read(CVBSD_DIFF_GAIN, sd));
	CVD_DUMP("CVBSD_THRESHOLD_GAIN=0x%08x\n",
				cvd_read(CVBSD_THRESHOLD_GAIN, sd));
	CVD_DUMP("CVBSD_CVD2_2D_COMB_ADAP_CTRL2=0x%08x\n",
				cvd_read(CVBSD_CVD2_2D_COMB_ADAP_CTRL2, sd));
	CVD_DUMP("CVBSD_CVD2_2D_COMB_ADAP_CTRL3=0x%08x\n",
				cvd_read(CVBSD_CVD2_2D_COMB_ADAP_CTRL3, sd));
	CVD_DUMP("CVBSD_CVD2_CHROMA_EDGE_ENHANC=0x%08x\n",
				cvd_read(CVBSD_CVD2_CHROMA_EDGE_ENHANC, sd));
	CVD_DUMP("CVBSD_CVD2_CONTROL_REGISTER=0x%08x\n",
				cvd_read(CVBSD_CVD2_CONTROL_REGISTER, sd));
	CVD_DUMP("CVBSD_CVD2_2D_COMB_FILTER_AND=0x%08x\n",
				cvd_read(CVBSD_CVD2_2D_COMB_FILTER_AND, sd));
	CVD_DUMP("CVBSD_CVD2_TCOMB_GAIN_REGISTER=0x%08x\n",
				cvd_read(CVBSD_CVD2_TCOMB_GAIN_REGISTER, sd));
	CVD_DUMP("CVBSD_CVD2_NOISE_LINE=0x%08x\n",
				cvd_read(CVBSD_CVD2_NOISE_LINE, sd));
	CVD_DUMP("CVBSD_FB_VSTART=0x%08x\n",
				cvd_read(CVBSD_FB_VSTART, sd));
	CVD_DUMP("CVBSD_FB_VHEIGHT=0x%08x\n",
				cvd_read(CVBSD_FB_VHEIGHT, sd));
	CVD_DUMP("CVBSD_HSYNC_PULSE_CONFIG=0x%08x\n",
				cvd_read(CVBSD_HSYNC_PULSE_CONFIG, sd));
	CVD_DUMP("CVBSD_CAGC_TIME_CONSTANT=0x%08x\n",
				cvd_read(CVBSD_CAGC_TIME_CONSTANT, sd));
	CVD_DUMP("CVBSD_CAGC_CORING_CONTROL=0x%08x\n",
				cvd_read(CVBSD_CAGC_CORING_CONTROL, sd));
	CVD_DUMP("CVBSD_ANTI_ALIASING_FILTER_EN=0x%08x\n",
				cvd_read(CVBSD_ANTI_ALIASING_FILTER_EN, sd));
	CVD_DUMP("CVBSD_NEW_DCRESTORE_CNTL=0x%08x\n",
				cvd_read(CVBSD_NEW_DCRESTORE_CNTL, sd));
	CVD_DUMP("CVBSD_DCRESTORE_ACCUM_WIDTH=0x%08x\n",
				cvd_read(CVBSD_DCRESTORE_ACCUM_WIDTH, sd));
	CVD_DUMP("CVBSD_MANUAL_GAIN_CONTROL=0x%08x\n",
				cvd_read(CVBSD_MANUAL_GAIN_CONTROL, sd));
	CVD_DUMP("CVBSD_BACKPORCH_KILL_THRESHOLD=0x%08x\n",
				cvd_read(CVBSD_BACKPORCH_KILL_THRESHOLD, sd));
	CVD_DUMP("CVBSD_DCRESTORE_HSYNC_MID=0x%08x\n",
				cvd_read(CVBSD_DCRESTORE_HSYNC_MID, sd));
	CVD_DUMP("CVBSD_MIN_SYNC_HEIGHT=0x%08x\n",
				cvd_read(CVBSD_MIN_SYNC_HEIGHT, sd));
	CVD_DUMP("CVBSD_VSYNC_SIGNAL_THRESHOLD=0x%08x\n",
				cvd_read(CVBSD_VSYNC_SIGNAL_THRESHOLD, sd));
	CVD_DUMP("CVBSD_VSYNC_NO_SIGNAL_THRESHOLD=0x%08x\n",
				cvd_read(CVBSD_VSYNC_NO_SIGNAL_THRESHOLD, sd));
	CVD_DUMP("CVBSD_VDETCET_IMPROVEMENT=0x%08x\n",
				cvd_read(CVBSD_VDETCET_IMPROVEMENT, sd));
	CVD_DUMP("CVBSD_VFIELD_HOFFSET_LSB=0x%08x\n",
				cvd_read(CVBSD_VFIELD_HOFFSET_LSB, sd));
	CVD_DUMP("CVBSD_HDETCET_IMPROVEMENT1=0x%08x\n",
				cvd_read(CVBSD_HDETCET_IMPROVEMENT1, sd));
	CVD_DUMP("CVBSD_HDETCET_IMPROVEMENT2=0x%08x\n",
				cvd_read(CVBSD_HDETCET_IMPROVEMENT2, sd));
	CVD_DUMP("CVBSD_HDETCET_IMPROVEMENT3=0x%08x\n",
				cvd_read(CVBSD_HDETCET_IMPROVEMENT3, sd));
	CVD_DUMP("CVBSD_HDETCET_IMPROVEMENT4=0x%08x\n",
				cvd_read(CVBSD_HDETCET_IMPROVEMENT4, sd));
	CVD_DUMP("CVBSD_CHROMA_ACTIVITY_LEVEL=0x%08x\n",
				cvd_read(CVBSD_CHROMA_ACTIVITY_LEVEL, sd));
	CVD_DUMP("CVBSD_FREQ_OFFSET_RANGE=0x%08x\n",
				cvd_read(CVBSD_FREQ_OFFSET_RANGE, sd));
	CVD_DUMP("CVBSD_ISSECAM_TH=0x%08x\n",
				cvd_read(CVBSD_ISSECAM_TH, sd));
	CVD_DUMP("CVBSD_STATUS_COMB3D_MOTION=0x%08x\n",
				cvd_read(CVBSD_STATUS_COMB3D_MOTION, sd));
	CVD_DUMP("CVBSD_HACTIVE_MD_START=0x%08x\n",
				cvd_read(CVBSD_HACTIVE_MD_START, sd));
	CVD_DUMP("CVBSD_HACTIVE_MD_WIDTH=0x%08x\n",
				cvd_read(CVBSD_HACTIVE_MD_WIDTH, sd));
	CVD_DUMP("CVBSD_MOTION_CONFIG=0x%08x\n",
				cvd_read(CVBSD_MOTION_CONFIG, sd));
	CVD_DUMP("CVBSD_CHROMA_BW_MOTION_TH=0x%08x\n",
				cvd_read(CVBSD_CHROMA_BW_MOTION_TH, sd));
	CVD_DUMP("CVBSD_STILL_IMAGE_TH=0x%08x\n",
				cvd_read(CVBSD_STILL_IMAGE_TH, sd));
	CVD_DUMP("CVBSD_MOTION_DEBUG=0x%08x\n",
				cvd_read(CVBSD_MOTION_DEBUG, sd));
	CVD_DUMP("CVBSD_PHASE_OFFSET_RANGE=0x%08x\n",
				cvd_read(CVBSD_PHASE_OFFSET_RANGE, sd));
	CVD_DUMP("CVBSD_ISPAL_TH=0x%08x\n",
				cvd_read(CVBSD_ISPAL_TH, sd));
	CVD_DUMP("CVBSD_CORDIC_GATE_START=0x%08x\n",
				cvd_read(CVBSD_CORDIC_GATE_START, sd));
	CVD_DUMP("CVBSD_CORDIC_GATE_END=0x%08x\n",
				cvd_read(CVBSD_CORDIC_GATE_END, sd));
	CVD_DUMP("CVBSD_ADC_CPUMP_SWAP=0x%08x\n",
				cvd_read(CVBSD_ADC_CPUMP_SWAP, sd));
	CVD_DUMP("CVBSD_CAPTION_START=0x%08x\n",
				cvd_read(CVBSD_CAPTION_START, sd));
	CVD_DUMP("CVBSD_WSS625_START=0x%08x\n",
				cvd_read(CVBSD_WSS625_START, sd));
	CVD_DUMP("CVBSD_TELETEXT_START=0x%08x\n",
				cvd_read(CVBSD_TELETEXT_START, sd));
	CVD_DUMP("CVBSD_VPS_START=0x%08x\n",
				cvd_read(CVBSD_VPS_START, sd));
	CVD_DUMP("CVBSD_CTRL0=0x%08x\n",
				cvd_read(CVBSD_CTRL0, sd));
	CVD_DUMP("CVBSD_CTRL1=0x%08x\n",
				cvd_read(CVBSD_CTRL1, sd));
	CVD_DUMP("CVBSD_CAGC_GATE_START=0x%08x\n",
				cvd_read(CVBSD_CAGC_GATE_START, sd));
	CVD_DUMP("CVBSD_CAGC_GATE_END=0x%08x\n",
				cvd_read(CVBSD_CAGC_GATE_END, sd));
	CVD_DUMP("CVBSD_CKILL=0x%08x\n",
				cvd_read(CVBSD_CKILL, sd));
	CVD_DUMP("CVBSD_QFIR_MODE=0x%08x\n",
				cvd_read(CVBSD_QFIR_MODE, sd));
	CVD_DUMP("CVBSD_ADAP_BW_CDIFF=0x%08x\n",
				cvd_read(CVBSD_ADAP_BW_CDIFF, sd));
	CVD_DUMP("CVBSD_ADAP_BW_CMIN=0x%08x\n",
				cvd_read(CVBSD_ADAP_BW_CMIN, sd));
	CVD_DUMP("CVBSD_ADAP_BW_VERT=0x%08x\n",
				cvd_read(CVBSD_ADAP_BW_VERT, sd));
	CVD_DUMP("CVBSD_ADAP_BW_LUMA=0x%08x\n",
				cvd_read(CVBSD_ADAP_BW_LUMA, sd));
	CVD_DUMP("CVBSD_ADAP_BW_CHROMA=0x%08x\n",
				cvd_read(CVBSD_ADAP_BW_CHROMA, sd));
	CVD_DUMP("CVBSD_ADAP_BW_COLOUR=0x%08x\n",
				cvd_read(CVBSD_ADAP_BW_COLOUR, sd));
	CVD_DUMP("CVBSD_ADAP_BW_HFY=0x%08x\n",
				cvd_read(CVBSD_ADAP_BW_HFY, sd));
	CVD_DUMP("CVBSD_MD_CFG1=0x%08x\n",
				cvd_read(CVBSD_MD_CFG1, sd));
	CVD_DUMP("CVBSD_MD_CFG2=0x%08x\n",
				cvd_read(CVBSD_MD_CFG2, sd));
	CVD_DUMP("CVBSD_MD_BPF1_STRG_TPO_COL_LVL=0x%08x\n",
				cvd_read(CVBSD_MD_BPF1_STRG_TPO_COL_LVL, sd));
	CVD_DUMP("CVBSD_MD_BPF1_WEAK_SPT_COL_LVL=0x%08x\n",
				cvd_read(CVBSD_MD_BPF1_WEAK_SPT_COL_LVL, sd));
	CVD_DUMP("CVBSD_MD_BPF1_STRG_TPO_LUMA_LVL=0x%08x\n",
				cvd_read(CVBSD_MD_BPF1_STRG_TPO_LUMA_LVL, sd));
	CVD_DUMP("CVBSD_MD_BPF1_STRG_SPT_LUMA_LVL=0x%08x\n",
				cvd_read(CVBSD_MD_BPF1_STRG_SPT_LUMA_LVL, sd));
	CVD_DUMP("CVBSD_MD_BPF1_WEAK_CHR_MOTN_LVL=0x%08x\n",
				cvd_read(CVBSD_MD_BPF1_WEAK_CHR_MOTN_LVL, sd));
	CVD_DUMP("CVBSD_MD_BPF1_WEAK_LUM_MOTN_LVL=0x%08x\n",
				cvd_read(CVBSD_MD_BPF1_WEAK_LUM_MOTN_LVL, sd));
	CVD_DUMP("CVBSD_MD_LPF_L_C_WEI_CASE1_1=0x%08x\n",
				cvd_read(CVBSD_MD_LPF_L_C_WEI_CASE1_1, sd));
	CVD_DUMP("CVBSD_MD_LPF_L_C_WEI_CASE1_2=0x%08x\n",
				cvd_read(CVBSD_MD_LPF_L_C_WEI_CASE1_2, sd));
	CVD_DUMP("CVBSD_MD_LPF_L_C_WEI_CASE1_3=0x%08x\n",
				cvd_read(CVBSD_MD_LPF_L_C_WEI_CASE1_3, sd));
	CVD_DUMP("CVBSD_MD_LPF_L_WEIGHTED_CASE1_1=0x%08x\n",
				cvd_read(CVBSD_MD_LPF_L_WEIGHTED_CASE1_1, sd));
	CVD_DUMP("CVBSD_MD_LPF_L_WEIGHTED_CASE1_2=0x%08x\n",
				cvd_read(CVBSD_MD_LPF_L_WEIGHTED_CASE1_2, sd));
	CVD_DUMP("CVBSD_MD_LPF_L_WEIGHTED_CASE1_3=0x%08x\n",
				cvd_read(CVBSD_MD_LPF_L_WEIGHTED_CASE1_3, sd));
	CVD_DUMP("CVBSD_MD_LPF_L_WEIGHTED_CASE2_1=0x%08x\n",
				cvd_read(CVBSD_MD_LPF_L_WEIGHTED_CASE2_1, sd));
	CVD_DUMP("CVBSD_MD_LPF_L_WEIGHTED_CASE2_1=0x%08x\n",
				cvd_read(CVBSD_MD_LPF_L_WEIGHTED_CASE2_2, sd));
	CVD_DUMP("CVBSD_MD_LPF_L_WEIGHTED_CASE2_1=0x%08x\n",
				cvd_read(CVBSD_MD_LPF_L_WEIGHTED_CASE2_3, sd));
	CVD_DUMP("CVBSD_MD_TEMPO_CHR_DIFF_CASE1_1=0x%08x\n",
				cvd_read(CVBSD_MD_TEMPO_CHR_DIFF_CASE1_1, sd));
	CVD_DUMP("CVBSD_MD_TEMPO_CHR_DIFF_CASE1_2=0x%08x\n",
				cvd_read(CVBSD_MD_TEMPO_CHR_DIFF_CASE1_2, sd));
	CVD_DUMP("CVBSD_MD_TEMPO_CHR_DIFF_CASE2_1=0x%08x\n",
				cvd_read(CVBSD_MD_TEMPO_CHR_DIFF_CASE2_1, sd));
	CVD_DUMP("CVBSD_MD_TEMPO_CHR_DIFF_CASE2_2=0x%08x\n",
				cvd_read(CVBSD_MD_TEMPO_CHR_DIFF_CASE2_2, sd));
	CVD_DUMP("CVBSD_MD_TEMPO_CHR_DIFF_CASE2_3=0x%08x\n",
				cvd_read(CVBSD_MD_TEMPO_CHR_DIFF_CASE2_3, sd));
	CVD_DUMP("CVBSD_MD_LUMA_LOOKUP_TH=0x%08x\n",
				cvd_read(CVBSD_MD_LUMA_LOOKUP_TH, sd));
	CVD_DUMP("CVBSD_MD_LUMA_FLATFIELD_CLK_LVL=0x%08x\n",
				cvd_read(CVBSD_MD_LUMA_FLATFIELD_CLK_LVL, sd));
	CVD_DUMP("CVBSD_MD_LUMA_FLATFIELD_CASE1_1=0x%08x\n",
				cvd_read(CVBSD_MD_LUMA_FLATFIELD_CASE1_1, sd));
	CVD_DUMP("CVBSD_MD_LUMA_FLATFIELD_CASE1_2=0x%08x\n",
				cvd_read(CVBSD_MD_LUMA_FLATFIELD_CASE1_2, sd));
	CVD_DUMP("CVBSD_MD_LUMA_FLATFIELD_CASE1_3=0x%08x\n",
				cvd_read(CVBSD_MD_LUMA_FLATFIELD_CASE1_3, sd));
	CVD_DUMP("CVBSD_MD_LUMA_FLATFIELD_CASE1_4=0x%08x\n",
				cvd_read(CVBSD_MD_LUMA_FLATFIELD_CASE1_4, sd));
	CVD_DUMP("CVBSD_MD_LUMA_FLATFIELD_CASE1_5=0x%08x\n",
				cvd_read(CVBSD_MD_LUMA_FLATFIELD_CASE1_5, sd));
	CVD_DUMP("CVBSD_MD_LUMA_FLATFIELD_CASE2_1=0x%08x\n",
				cvd_read(CVBSD_MD_LUMA_FLATFIELD_CASE2_1, sd));
	CVD_DUMP("CVBSD_MD_LUMA_FLATFIELD_CASE2_2=0x%08x\n",
				cvd_read(CVBSD_MD_LUMA_FLATFIELD_CASE2_2, sd));
	CVD_DUMP("CVBSD_MD_LUMA_FLATFIELD_CASE2_3=0x%08x\n",
				cvd_read(CVBSD_MD_LUMA_FLATFIELD_CASE2_3, sd));
	CVD_DUMP("CVBSD_MD_LUMA_FLATFIELD_CASE2_4=0x%08x\n",
				cvd_read(CVBSD_MD_LUMA_FLATFIELD_CASE2_4, sd));
	CVD_DUMP("CVBSD_MD_LUMA_FLATFIELD_CASE2_5=0x%08x\n",
				cvd_read(CVBSD_MD_LUMA_FLATFIELD_CASE2_5, sd));
	CVD_DUMP("CVBSD_MD_LUMA_FLATFIELD_CASE3_1=0x%08x\n",
				cvd_read(CVBSD_MD_LUMA_FLATFIELD_CASE3_1, sd));
	CVD_DUMP("CVBSD_MD_LUMA_FLATFIELD_CASE3_2=0x%08x\n",
				cvd_read(CVBSD_MD_LUMA_FLATFIELD_CASE3_2, sd));
	CVD_DUMP("CVBSD_MD_LUMA_FLATFIELD_CASE3_3=0x%08x\n",
				cvd_read(CVBSD_MD_LUMA_FLATFIELD_CASE3_3, sd));
	CVD_DUMP("CVBSD_MD_LUMA_FLATFIELD_CASE3_4=0x%08x\n",
				cvd_read(CVBSD_MD_LUMA_FLATFIELD_CASE3_4, sd));
	CVD_DUMP("CVBSD_MD_LUMA_FLATFIELD_CASE3_5=0x%08x\n",
				cvd_read(CVBSD_MD_LUMA_FLATFIELD_CASE3_5, sd));
	CVD_DUMP("CVBSD_MD_LUMA_FLATFIELD_CASE4_1=0x%08x\n",
				cvd_read(CVBSD_MD_LUMA_FLATFIELD_CASE4_1, sd));
	CVD_DUMP("CVBSD_MD_LUMA_FLATFIELD_CASE4_2=0x%08x\n",
				cvd_read(CVBSD_MD_LUMA_FLATFIELD_CASE4_2, sd));
	CVD_DUMP("CVBSD_MD_LUMA_FLATFIELD_CASE4_3=0x%08x\n",
				cvd_read(CVBSD_MD_LUMA_FLATFIELD_CASE4_3, sd));
	CVD_DUMP("CVBSD_MD_LUMA_FLATFIELD_CASE4_4=0x%08x\n",
				cvd_read(CVBSD_MD_LUMA_FLATFIELD_CASE4_4, sd));
	CVD_DUMP("CVBSD_MD_LUMA_FLATFIELD_CASE4_5=0x%08x\n",
				cvd_read(CVBSD_MD_LUMA_FLATFIELD_CASE4_5, sd));
	CVD_DUMP("CVBSD_MD_CHROMA_LOOKUP_TH=0x%08x\n",
				cvd_read(CVBSD_MD_CHROMA_LOOKUP_TH, sd));
	CVD_DUMP("CVBSD_MD_CHROMA_FLATFD_CLK_LVL=0x%08x\n",
				cvd_read(CVBSD_MD_CHROMA_FLATFD_CLK_LVL, sd));
	CVD_DUMP("CVBSD_MD_CHROMA_FLATFD_CASE1_1=0x%08x\n",
				cvd_read(CVBSD_MD_CHROMA_FLATFD_CASE1_1, sd));
	CVD_DUMP("CVBSD_MD_CHROMA_FLATFD_CASE1_2=0x%08x\n",
				cvd_read(CVBSD_MD_CHROMA_FLATFD_CASE1_2, sd));
	CVD_DUMP("CVBSD_MD_CHROMA_FLATFD_CASE1_3=0x%08x\n",
				cvd_read(CVBSD_MD_CHROMA_FLATFD_CASE1_3, sd));
	CVD_DUMP("CVBSD_MD_CHROMA_FLATFD_CASE1_4=0x%08x\n",
				cvd_read(CVBSD_MD_CHROMA_FLATFD_CASE1_4, sd));
	CVD_DUMP("CVBSD_MD_CHROMA_FLATFD_CASE1_5=0x%08x\n",
				cvd_read(CVBSD_MD_CHROMA_FLATFD_CASE1_5, sd));
	CVD_DUMP("CVBSD_MD_CHROMA_FLATFD_CASE2_1=0x%08x\n",
				cvd_read(CVBSD_MD_CHROMA_FLATFD_CASE2_1, sd));
	CVD_DUMP("CVBSD_MD_CHROMA_FLATFD_CASE2_2=0x%08x\n",
				cvd_read(CVBSD_MD_CHROMA_FLATFD_CASE2_2, sd));
	CVD_DUMP("CVBSD_MD_CHROMA_FLATFD_CASE2_3=0x%08x\n",
				cvd_read(CVBSD_MD_CHROMA_FLATFD_CASE2_3, sd));
	CVD_DUMP("CVBSD_MD_CHROMA_FLATFD_CASE2_4=0x%08x\n",
				cvd_read(CVBSD_MD_CHROMA_FLATFD_CASE2_4, sd));
	CVD_DUMP("CVBSD_MD_CHROMA_FLATFD_CASE2_5=0x%08x\n",
				cvd_read(CVBSD_MD_CHROMA_FLATFD_CASE2_5, sd));
	CVD_DUMP("CVBSD_MD_CHROMA_FLATFD_CASE3_1=0x%08x\n",
				cvd_read(CVBSD_MD_CHROMA_FLATFD_CASE3_1, sd));
	CVD_DUMP("CVBSD_MD_CHROMA_FLATFD_CASE3_2=0x%08x\n",
				cvd_read(CVBSD_MD_CHROMA_FLATFD_CASE3_2, sd));
	CVD_DUMP("CVBSD_MD_CHROMA_FLATFD_CASE3_3=0x%08x\n",
				cvd_read(CVBSD_MD_CHROMA_FLATFD_CASE3_3, sd));
	CVD_DUMP("CVBSD_MD_CHROMA_FLATFD_CASE3_4=0x%08x\n",
				cvd_read(CVBSD_MD_CHROMA_FLATFD_CASE3_4, sd));
	CVD_DUMP("CVBSD_MD_CHROMA_FLATFD_CASE3_5=0x%08x\n",
				cvd_read(CVBSD_MD_CHROMA_FLATFD_CASE3_5, sd));
	CVD_DUMP("CVBSD_MD_CHROMA_FLATFD_CASE4_1=0x%08x\n",
				cvd_read(CVBSD_MD_CHROMA_FLATFD_CASE4_1, sd));
	CVD_DUMP("CVBSD_MD_CHROMA_FLATFD_CASE4_2=0x%08x\n",
				cvd_read(CVBSD_MD_CHROMA_FLATFD_CASE4_2, sd));
	CVD_DUMP("CVBSD_MD_CHROMA_FLATFD_CASE4_3=0x%08x\n",
				cvd_read(CVBSD_MD_CHROMA_FLATFD_CASE4_3, sd));
	CVD_DUMP("CVBSD_MD_CHROMA_FLATFD_CASE4_4=0x%08x\n",
				cvd_read(CVBSD_MD_CHROMA_FLATFD_CASE4_4, sd));
	CVD_DUMP("CVBSD_MD_CHROMA_FLATFD_CASE4_5=0x%08x\n",
				cvd_read(CVBSD_MD_CHROMA_FLATFD_CASE4_5, sd));
	CVD_DUMP("CVBSD_ADAP_CHROMA_BW_CONFIG1=0x%08x\n",
				cvd_read(CVBSD_ADAP_CHROMA_BW_CONFIG1, sd));
	CVD_DUMP("CVBSD_ADAP_CHROMA_BW_CONFIG2=0x%08x\n",
				cvd_read(CVBSD_ADAP_CHROMA_BW_CONFIG2, sd));
	CVD_DUMP("CVBSD_MD_SPATIAL_CONFIG1=0x%08x\n",
				cvd_read(CVBSD_MD_SPATIAL_CONFIG1, sd));
	CVD_DUMP("CVBSD_MD_SPATIAL_CONFIG2=0x%08x\n",
				cvd_read(CVBSD_MD_SPATIAL_CONFIG2, sd));
	CVD_DUMP("CVBSD_CLAMP_AGC_RANGE=0x%08x\n",
				cvd_read(CVBSD_CLAMP_AGC_RANGE, sd));
	CVD_DUMP("CVBSD_HDETECT_CONFIG0=0x%08x\n",
				cvd_read(CVBSD_HDETECT_CONFIG0, sd));
	CVD_DUMP("CVBSD_HDETECT_CONFIG1=0x%08x\n",
				cvd_read(CVBSD_HDETECT_CONFIG1, sd));
	CVD_DUMP("CVBSD_DC_RESTORE_VSYNC_CNTL=0x%08x\n",
				cvd_read(CVBSD_DC_RESTORE_VSYNC_CNTL, sd));
	CVD_DUMP("CVBSD_HSTATE_CONFIG0=0x%08x\n",
				cvd_read(CVBSD_HSTATE_CONFIG0, sd));
	CVD_DUMP("CVBSD_HSTATE_CONFIG1=0x%08x\n",
				cvd_read(CVBSD_HSTATE_CONFIG1, sd));
	CVD_DUMP("CVBSD_HSTATE_CONFIG2=0x%08x\n",
				cvd_read(CVBSD_HSTATE_CONFIG2, sd));
	CVD_DUMP("CVBSD_HSTATE_CONFIG3=0x%08x\n",
				cvd_read(CVBSD_HSTATE_CONFIG3, sd));
	CVD_DUMP("CVBSD_HSTATE_CONFIG4=0x%08x\n",
				cvd_read(CVBSD_HSTATE_CONFIG4, sd));
	CVD_DUMP("CVBSD_HSTATE_CONFIG5=0x%08x\n",
				cvd_read(CVBSD_HSTATE_CONFIG5, sd));
	CVD_DUMP("CVBSD_HSTATE_CONFIG6=0x%08x\n",
				cvd_read(CVBSD_HSTATE_CONFIG6, sd));
	CVD_DUMP("CVBSD_HSTATE_CONFIG7=0x%08x\n",
				cvd_read(CVBSD_HSTATE_CONFIG7, sd));
	CVD_DUMP("CVBSD_HSTATE_CONFIG8=0x%08x\n",
				cvd_read(CVBSD_HSTATE_CONFIG8, sd));
	CVD_DUMP("CVBSD_HSTATE_CONFIG9=0x%08x\n",
				cvd_read(CVBSD_HSTATE_CONFIG9, sd));
	CVD_DUMP("CVBSD_HSTATE_CONFIG10=0x%08x\n",
				cvd_read(CVBSD_HSTATE_CONFIG10, sd));
	CVD_DUMP("CVBSD_IFCOMP_CONFIG0=0x%08x\n",
				cvd_read(CVBSD_IFCOMP_CONFIG0, sd));
	CVD_DUMP("CVBSD_IFCOMP_CONFIG1=0x%08x\n",
				cvd_read(CVBSD_IFCOMP_CONFIG1, sd));
	CVD_DUMP("CVBSD_IFCOMP_CONFIG2=0x%08x\n",
				cvd_read(CVBSD_IFCOMP_CONFIG2, sd));
	CVD_DUMP("CVBSD_IFCOMP_CONFIG3=0x%08x\n",
				cvd_read(CVBSD_IFCOMP_CONFIG3, sd));
	CVD_DUMP("CVBSD_IFCOMP_CONFIG4=0x%08x\n",
				cvd_read(CVBSD_IFCOMP_CONFIG4, sd));
	CVD_DUMP("CVBSD_IFCOMP_CONFIG5=0x%08x\n",
				cvd_read(CVBSD_IFCOMP_CONFIG5, sd));
	CVD_DUMP("CVBSD_IFCOMP_CONFIG6=0x%08x\n",
				cvd_read(CVBSD_IFCOMP_CONFIG6, sd));
	CVD_DUMP("CVBSD_IFCOMP_CONFIG7=0x%08x\n",
				cvd_read(CVBSD_IFCOMP_CONFIG7, sd));
	CVD_DUMP("CVBSD_IFCOMP_CONFIG8=0x%08x\n",
				cvd_read(CVBSD_IFCOMP_CONFIG8, sd));
	CVD_DUMP("CVBSD_IFCOMP_CONFIG9=0x%08x\n",
				cvd_read(CVBSD_IFCOMP_CONFIG9, sd));
	CVD_DUMP("CVBSD_IFCOMP_CONFIG10=0x%08x\n",
				cvd_read(CVBSD_IFCOMP_CONFIG10, sd));
	CVD_DUMP("CVBSD_IFCOMP_CONFIG11=0x%08x\n",
				cvd_read(CVBSD_IFCOMP_CONFIG11, sd));
	CVD_DUMP("CVBSD_IFCOMP_CONFIG12=0x%08x\n",
				cvd_read(CVBSD_IFCOMP_CONFIG12, sd));
	CVD_DUMP("CVBSD_IFCOMP_CONFIG13=0x%08x\n",
				cvd_read(CVBSD_IFCOMP_CONFIG13, sd));
	CVD_DUMP("CVBSD_IFCOMP_CONFIG14=0x%08x\n",
				cvd_read(CVBSD_IFCOMP_CONFIG14, sd));
	CVD_DUMP("CVBSD_IFCOMP_CONFIG15=0x%08x\n",
				cvd_read(CVBSD_IFCOMP_CONFIG15, sd));
	CVD_DUMP("CVBSD_IFCOMP_CONFIG16=0x%08x\n",
				cvd_read(CVBSD_IFCOMP_CONFIG16, sd));
	CVD_DUMP("CVBSD_CACTIVE_DELAY=0x%08x\n",
				cvd_read(CVBSD_CACTIVE_DELAY, sd));
	CVD_DUMP("CVBSD_KOR_PROT_THRESH=0x%08x\n",
				cvd_read(CVBSD_KOR_PROT_THRESH, sd));
	CVD_DUMP("CVBSD_KOR_PROT_DET_THRESH=0x%08x\n",
				cvd_read(CVBSD_KOR_PROT_DET_THRESH, sd));
	CVD_DUMP("CVBSD_STATUS_KOR_PROT=0x%08x\n",
				cvd_read(CVBSD_STATUS_KOR_PROT, sd));
	CVD_DUMP("CVBSD_UV2CRCB_GAIN=0x%08x\n",
				cvd_read(CVBSD_UV2CRCB_GAIN, sd));
	CVD_DUMP("CVBSD_MD_INTER_DIFF=0x%08x\n",
				cvd_read(CVBSD_MD_INTER_DIFF, sd));
	CVD_DUMP("CVBSD_MD_INTER_GAIN=0x%08x\n",
				cvd_read(CVBSD_MD_INTER_GAIN, sd));
	CVD_DUMP("CVBSD_MD_INTER_VERT_COMB_THRESH=0x%08x\n",
				cvd_read(CVBSD_MD_INTER_VERT_COMB_THRESH, sd));
	CVD_DUMP("CVBSD_MD_INTER_BAND_THRESH=0x%08x\n",
				cvd_read(CVBSD_MD_INTER_BAND_THRESH, sd));
	CVD_DUMP("CVBSD_MD_INTER_COMB=0x%08x\n",
				cvd_read(CVBSD_MD_INTER_COMB, sd));
	CVD_DUMP("CVBSD_VIDEO_MODE1_STATUS=0x%08x\n",
				cvd_read(CVBSD_VIDEO_MODE1_STATUS, sd));
	CVD_DUMP("CVBSD_VIDEO_MODE2_STATUS=0x%08x\n",
				cvd_read(CVBSD_VIDEO_MODE2_STATUS, sd));
	CVD_DUMP("CVBSD_VIDEO_MODE_CTL=0x%08x\n",
				cvd_read(CVBSD_VIDEO_MODE_CTL, sd));
	CVD_DUMP("CVBSD_HV_DELAY_VSTART=0x%08x\n",
				cvd_read(CVBSD_HV_DELAY_VSTART, sd));
	CVD_DUMP("CVBSD_VACTIVE_HV_WINDOW=0x%08x\n",
				cvd_read(CVBSD_VACTIVE_HV_WINDOW, sd));
	CVD_DUMP("CVBSD_VTOTAL_CONFIG=0x%08x\n",
				cvd_read(CVBSD_VTOTAL_CONFIG, sd));
	CVD_DUMP("CVBSD_LBADRGEN_INIT=0x%08x\n",
				cvd_read(CVBSD_LBADRGEN_INIT, sd));
	CVD_DUMP("CVBSD_LBADRGEN_STATUS=0x%08x\n",
				cvd_read(CVBSD_LBADRGEN_STATUS, sd));
	CVD_DUMP("CVBSD_INTERRUPT_CONFIG=0x%08x\n",
				cvd_read(CVBSD_INTERRUPT_CONFIG, sd));
	CVD_DUMP("CVBSD_AFE_REG0=0x%08x\n",
				cvd_read(CVBSD_AFE_REG0, sd));
	CVD_DUMP("CVBSD_AFE_ADC_MODE=0x%08x\n",
				cvd_read(CVBSD_AFE_ADC_MODE, sd));
	CVD_DUMP("CVBSD_AFE_ADC_CONTROL=0x%08x\n",
				cvd_read(CVBSD_AFE_ADC_CONTROL, sd));
	CVD_DUMP("CVBSD_AFE_REG3=0x%08x\n",
				cvd_read(CVBSD_AFE_REG3, sd));
	CVD_DUMP("CVBSD_AFE_REG4=0x%08x\n",
				cvd_read(CVBSD_AFE_REG4, sd));
	CVD_DUMP("CVBSD_AFE_REG5=0x%08x\n",
				cvd_read(CVBSD_AFE_REG5, sd));
	CVD_DUMP("CVBSD_AFE_REG6=0x%08x\n",
				cvd_read(CVBSD_AFE_REG6, sd));
	CVD_DUMP("CVBSD_AFE_REG7=0x%08x\n",
				cvd_read(CVBSD_AFE_REG7, sd));
	CVD_DUMP("CVBSD_AFE_CLOCK_CONTROL=0x%08x\n",
				cvd_read(CVBSD_AFE_CLOCK_CONTROL, sd));
	CVD_DUMP("CVBSD_AFE_BIAS=0x%08x\n",
				cvd_read(CVBSD_AFE_BIAS, sd));
	CVD_DUMP("CVBSD_AFE_REG10=0x%08x\n",
				cvd_read(CVBSD_AFE_REG10, sd));
	CVD_DUMP("CVBSD_AFE_CAL1=0x%08x\n",
				cvd_read(CVBSD_AFE_CAL1, sd));
	CVD_DUMP("CVBSD_AFE_CAL2=0x%08x\n",
				cvd_read(CVBSD_AFE_CAL2, sd));
	CVD_DUMP("CVBSD_AFE_CAL3=0x%08x\n",
				cvd_read(CVBSD_AFE_CAL3, sd));
	CVD_DUMP("CVBSD_AFE_CAL4=0x%08x\n",
				cvd_read(CVBSD_AFE_CAL4, sd));
	CVD_DUMP("CVBSD_AFE_CAL6=0x%08x\n",
				cvd_read(CVBSD_AFE_CAL6, sd));
	CVD_DUMP("CVBSD_AFE_TIMING0=0x%08x\n",
				cvd_read(CVBSD_AFE_TIMING0, sd));
	CVD_DUMP("CVBSD_AFE_TESTPORT0=0x%08x\n",
				cvd_read(CVBSD_AFE_TESTPORT0, sd));
	CVD_DUMP("CVBSD_AFE_TESTPORT1=0x%08x\n",
				cvd_read(CVBSD_AFE_TESTPORT1, sd));
	CVD_DUMP("CVBSD_AFE_COMMON1=0x%08x\n",
				cvd_read(CVBSD_AFE_COMMON1, sd));
	CVD_DUMP("CVBSD_AFEPWR_EN=0x%08x\n",
				cvd_read(CVBSD_AFEPWR_EN, sd));
	CVD_DUMP("CVBSD_INTERRUPT_STATUS=0x%08x\n",
				cvd_read(CVBSD_INTERRUPT_STATUS, sd));
}

static int cvd_g_register(struct v4l2_subdev *sd,
			     struct v4l2_dbg_register *reg)
{
	unsigned int ret;

	if (reg->reg > CVBSD_END)	/* register offset value */
		return -EINVAL;

	/* dump all the registers value */
	if (reg->reg == CVBSD_END) {
		cvd_print_regs(sd);
		return 0;
	}

	ret = cvd_read(reg->reg, sd);

	reg->val = (__u64)ret;

	return 0;
}

static int cvd_s_register(struct v4l2_subdev *sd,
			const struct v4l2_dbg_register *reg)
{
	if (reg->reg > CVBSD_END ||
	    reg->val > 0xFFFFFFFF)
		return -EINVAL;

	cvd_write(reg->reg, reg->val, sd);

	return 0;
}
#endif

static int cvd_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct cvd_dev *dec = to_state(sd);
	int ret = 0, value = 0;

	if (!enable) {
		/* need make sure power on before access INTERRUPT_CONFIG */
		cvd_write(CVBSD_AFEPWR_EN, 0x3, sd);

		/* disable field sync interrupt */
		cvd_write(CVBSD_INTERRUPT_CONFIG, 0x0, sd);

		cvd_write(CVBSD_AFEPWR_EN, 0x1, sd);	/* CVBSAFE disable */

		dec->norm_detected = false;

		return 0;
	}

	if ((cvd_read(CVBSD_AFEPWR_EN, sd) & 0x2) &&
		((cvd_read(CVBSD_CVD1_STATUS_REGISTER_1, sd) & 0xe) == 0xe)) {

		/* cvd has been working and locked, needn't skip fields */
		dec->skip_count = 0;

	} else {
		cvd_write(CVBSD_AFEPWR_EN, 0x3, sd);	/* CVBSAFE enable */

		/* start line buffer initialization process */
		cvd_write(CVBSD_LBADRGEN_INIT, 0x1, sd);

		/* line buffer initialization status busy(0x1) or idle(0x0) */
		while (cvd_read(CVBSD_LBADRGEN_STATUS, sd) & 0x1)
			cpu_relax();

		/* stop line buffer initialization process */
		cvd_write(CVBSD_LBADRGEN_INIT, 0x0, sd);

		/* soft reset CVD logic, register values are not reseted */
		cvd_write(CVBSD_CVD1_RESET_REGISTER, 0x1, sd);

		/* start CVD */
		cvd_write(CVBSD_CVD1_RESET_REGISTER, 0x0, sd);

		/* clear ext locked flag before enable it */
		cvd_write(CVBSD_DEBUG_INTERRUPT, 0x2, sd);

		/* then enable ext locked interrupt */
		value = cvd_read(CVBSD_DEBUG_INTERRUPT_MASK, sd);
		value |= 0x2;
		cvd_write(CVBSD_DEBUG_INTERRUPT_MASK, value, sd);

		/* wait for ext locked signals */
		ret = wait_for_completion_interruptible_timeout(
				&dec->locked_done, msecs_to_jiffies(100));

		if (ret == 0) {
			/* user might disconnect camera, set blank screen */
			cvd_write(CVBSD_BLUE_SCREEN_Y, 0x10, sd);
			cvd_write(CVBSD_BLUE_SCREEN_CB, 0x80, sd);
			cvd_write(CVBSD_BLUE_SCREEN_CR, 0x80, sd);

			/* needn't it, disable ext locked interrupt */
			value = cvd_read(CVBSD_DEBUG_INTERRUPT_MASK, sd);
			value &= ~0x2;
			cvd_write(CVBSD_DEBUG_INTERRUPT_MASK, value, sd);

			dev_err(to_state(sd)->dev,
				"ERROR: please make sure camera connected!\n");
			return -ETIMEDOUT;
		}

		if (ret < 0) {
			dev_err(to_state(sd)->dev,
				"wait for locked completion error: %d\n", ret);
			return ret;
		}

		/* we have to skip several fields to get correct FID */
		dec->skip_count = FIELD_SKIP_NUM;
	}

	/* enable delayed field sync interrupt */
	cvd_write(CVBSD_INTERRUPT_CONFIG, 0x1 | (VSYNC_DELAY_LINE << 4), sd);

	/* wait for a good field stream */
	ret = wait_for_completion_interruptible_timeout(&dec->skip_done,
							msecs_to_jiffies(200));

	if (ret == 0) {
		dev_err(to_state(sd)->dev, "Wait fi_sync INT timeout\n");
		return -ETIMEDOUT;
	}

	if (ret < 0) {
		dev_err(to_state(sd)->dev,
			"wait for fi_sync completion error: %d\n", ret);
		return ret;
	}

	return 0;
}

static int cvd_querystd(struct v4l2_subdev *sd, v4l2_std_id *norm)
{
	struct cvd_dev *dec = to_state(sd);
	int value;

	cvd_s_stream(sd, 1);

	value = cvd_detect_video_signal(sd);
	*norm = (value < 0) ? V4L2_STD_UNKNOWN : value;

	dec->norm_detected = true;

	cvd_s_stream(sd, 0);

	return 0;
}

static int cvd_try_mbus_fmt(struct v4l2_subdev *sd,
				struct v4l2_mbus_framefmt *fmt)
{
	struct cvd_dev *dec = to_state(sd);

	switch (fmt->field) {
	/* such both interlaced fmts supported, needn't change */
	case V4L2_FIELD_SEQ_TB:
	case V4L2_FIELD_SEQ_BT:
		break;

	default:
		/* other fmt can't support, return default fmt to user */
		fmt->field = V4L2_FIELD_SEQ_TB;
		break;
	}

	fmt->width = 720;
	if (!dec->norm_detected)
		cvd_querystd(sd, &dec->norm);
	fmt->height = dec->norm & V4L2_STD_525_60 ? 480 : 576;

	return 0;
}

static int cvd_g_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	struct cvd_dev *dec = to_state(sd);

	if (dec->norm & V4L2_STD_NTSC) {
		mf->width	= 720;
		mf->height	= 480;
	} else {
		mf->width	= 720;
		mf->height	= 576;
	}

	mf->code	= V4L2_MBUS_FMT_UYVY8_2X8;
	mf->colorspace	= V4L2_COLORSPACE_JPEG;

	return 0;
}

static int cvd_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	struct cvd_dev *dec = to_state(sd);

	a->bounds.left			= 0;
	a->bounds.top			= 0;
	if (dec->norm & V4L2_STD_NTSC) {
		a->bounds.width         = 720;
		a->bounds.height        = 480;
	} else {
		a->bounds.width         = 720;
		a->bounds.height        = 576;
	}
	a->defrect                      = a->bounds;
	a->type				= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	return 0;
}

static int cvd_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	struct cvd_dev *dec = to_state(sd);

	a->c.left	= 0;
	a->c.top	= 0;
	if (dec->norm & V4L2_STD_NTSC) {
		a->c.width	= 720;
		a->c.height	= 480;
	} else {
		a->c.width	= 720;
		a->c.height	= 576;
	}
	a->type	= V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return 0;
}

static int cvd_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			   enum v4l2_mbus_pixelcode *code)
{
	if (index)
		return -EINVAL;

	*code = V4L2_MBUS_FMT_UYVY8_2X8;
	return 0;
}

static int cvd_s_routing(struct v4l2_subdev *sd, u32 input,
				      u32 output, u32 config)
{
	unsigned int value;
	struct cvd_dev *dec = to_state(sd);

	/* port select doesn't depend on AFE PWR on or off */

	switch (input) {
	case 0:		/* INPUT_CVBS_0 */
		value = cvd_read(CVBSD_AFE_REG7, sd);
		value &= 0x3F;
		cvd_write(CVBSD_AFE_REG7, value, sd);
		break;
	case 1:		/* INPUT_CVBS_1 */
		value = cvd_read(CVBSD_AFE_REG7, sd);
		value &= 0x3F;
		value |= 0x40;
		cvd_write(CVBSD_AFE_REG7, value, sd);
		break;

	default:
		return -EINVAL;
	}

	dec->input_port = input;

	return 0;
}

static int cvd_enum_framesizes(struct v4l2_subdev *sd,
					struct v4l2_frmsizeenum *fsize)
{
	if (fsize->index != 0)
			return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise.min_width = 2;
	fsize->stepwise.min_height = 1;
	fsize->stepwise.max_width = 720;
	fsize->stepwise.max_height = 576;
	fsize->stepwise.step_width = 2;
	fsize->stepwise.step_height = 1;

	return 0;
}

static int cvd_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct cvd_dev *dec = container_of(ctrl->handler, struct cvd_dev, hdl);
	struct v4l2_subdev *sd = &dec->sd;
	unsigned int original_val = cvd_read(CVBSD_AFEPWR_EN, sd);

	switch (ctrl->id) {
	case V4L2_CID_SATURATION:
		cvd_write(CVBSD_AFEPWR_EN, 0x3, sd); /* PWR on firstly */
		cvd_write(CVBSD_CHROMA_SATURATION, ctrl->val, sd);
		dec->saturation = ctrl->val;
		break;
	case V4L2_CID_BRIGHTNESS:
		cvd_write(CVBSD_AFEPWR_EN, 0x3, sd); /* PWR on firstly */
		cvd_write(CVBSD_LUMA_BRIGHTNESS, ctrl->val, sd);
		dec->brightness = ctrl->val;
		break;
	case V4L2_CID_CONTRAST:
		cvd_write(CVBSD_AFEPWR_EN, 0x3, sd); /* PWR on firstly */
		cvd_write(CVBSD_LUMA_CONTRAST, ctrl->val, sd);
		dec->contrast = ctrl->val;
		break;
	case V4L2_CID_HUE:
		cvd_write(CVBSD_AFEPWR_EN, 0x3, sd); /* PWR on firstly */
		cvd_write(CVBSD_CHROMA_HUE, ctrl->val, sd);
		dec->hue  = ctrl->val;
		break;
	default:
		return -EINVAL;
	}

	/* we should set back to original PWR status */
	cvd_write(CVBSD_AFEPWR_EN, original_val, sd);

	return 0;
}

static const struct v4l2_ctrl_ops cvd_ctrl_ops = {
	.s_ctrl = cvd_s_ctrl,
};

static int cvd_init(struct v4l2_subdev *sd, u32 val)
{
	int i;
	struct v4l2_ctrl ctrl;
	struct cvd_dev *dec = to_state(sd);

	/* set initial registers */
	for (i = 0; i < ARRAY_SIZE(initial_registers); i++)
		cvd_write(initial_registers[i].reg_addr,
					initial_registers[i].reg_value, sd);

	/* set CVBS source input port */
	cvd_s_routing(sd, dec->input_port, 0, 0);

	/* set input analog video standard */
	cvd_s_std(sd, dec->norm);

	/* set saturation brightnes contrast hue */
	cvd_write(CVBSD_AFEPWR_EN, 0x3, sd); /* must PWR on before setting */
	cvd_write(CVBSD_CHROMA_SATURATION, dec->saturation, sd);
	cvd_write(CVBSD_LUMA_BRIGHTNESS, dec->brightness, sd);
	cvd_write(CVBSD_LUMA_CONTRAST, dec->contrast, sd);
	cvd_write(CVBSD_CHROMA_HUE, dec->hue, sd);
	cvd_write(CVBSD_AFEPWR_EN, 0x1, sd); /* PWR off after setting */

	return 0;
}

static struct v4l2_subdev_core_ops cvd_core_ops = {
	.interrupt_service_routine = cvd_isr,
	.init = cvd_init,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= cvd_g_register,
	.s_register	= cvd_s_register,
#endif
};

static struct v4l2_subdev_video_ops cvd_video_ops = {
	.s_std		= cvd_s_std,
	.g_std		= cvd_g_std,
	.querystd	= cvd_querystd,
	.s_stream	= cvd_s_stream,
	.try_mbus_fmt	= cvd_try_mbus_fmt,
	.g_mbus_fmt	= cvd_g_fmt,
	.cropcap	= cvd_cropcap,
	.g_crop		= cvd_g_crop,
	.enum_mbus_fmt	= cvd_enum_fmt,
	.s_routing	= cvd_s_routing,
	.enum_framesizes = cvd_enum_framesizes,
};

static struct v4l2_subdev_ops cvd_ops = {
	.core	= &cvd_core_ops,
	.video	= &cvd_video_ops,
};


static int cvd_probe(struct platform_device *pdev)
{
	int ret;
	struct device	*dev = &pdev->dev;
	struct cvd_dev *dec = NULL;
	struct v4l2_subdev *sd;
	struct v4l2_ctrl_handler *hdl;

	dec = devm_kzalloc(dev, sizeof(*dec), GFP_KERNEL);
	if (!dec)
		return -ENOMEM;

	dec->dev = dev;

	init_completion(&dec->skip_done);
	init_completion(&dec->locked_done);

	dec->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (dec->res == NULL) {
		dev_err(dev, "%s: fail to get cvd regs resource\n", __func__);
		return -EINVAL;
	}

	dec->io_base = devm_ioremap_resource(dev, dec->res);
	if (!dec->io_base) {
		dev_err(dev, "%s: fail to ioremap cvd regs\n", __func__);
		return -ENOMEM;
	}

	dec->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(dec->clk)) {
		dev_err(dev, "%s: fail to get cvd clock\n", __func__);
		return -EINVAL;
	}

	ret = clk_prepare_enable(dec->clk);
	if (ret) {
		dev_err(dev, "%s: fail to enable cvd clock\n", __func__);
		return -EINVAL;
	}

	ret = device_reset(dev);
	if (ret) {
		dev_err(dev, "Failed to reset\n");
		return -EINVAL;
	}

	platform_set_drvdata(pdev, dec);

	sd = &dec->sd;
	v4l2_subdev_init(sd, &cvd_ops);

	/* we pass INT central status reg addr to VIP for further handling */
	v4l2_set_subdevdata(sd, dec->io_base + CVBSD_INTERRUPT_STATUS);

	dev->platform_data = sd;

	if (!sd->name[0])
		strncpy(sd->name, CVD_DRV_NAME, sizeof(sd->name));

	hdl = &dec->hdl;
	v4l2_ctrl_handler_init(hdl, 5);
	v4l2_ctrl_new_std(hdl, &cvd_ctrl_ops,
				V4L2_CID_BRIGHTNESS, 0, 255, 1, 32);
	v4l2_ctrl_new_std(hdl, &cvd_ctrl_ops,
				V4L2_CID_CONTRAST, 0, 255, 1, 128);
	v4l2_ctrl_new_std(hdl, &cvd_ctrl_ops,
				V4L2_CID_SATURATION, 0, 255, 1, 128);
	v4l2_ctrl_new_std(hdl, &cvd_ctrl_ops,
				V4L2_CID_HUE, -128, 127, 1, 0);
	sd->ctrl_handler = hdl;
	if (hdl->error) {
		v4l2_ctrl_handler_free(hdl);
		clk_disable_unprepare(dec->clk);
		return hdl->error;
	}

	/* Initialize cvd with default value */
	dec->norm	= V4L2_STD_NTSC;
	dec->input_port	= 0;
	dec->contrast	= 0x80;
	dec->brightness	= 0x20;
	dec->saturation	= 0x80;
	dec->hue	= 0;

	cvd_init(sd, 0);

	return 0;
}

static int cvd_remove(struct platform_device *pdev)
{
	struct cvd_dev *dec = platform_get_drvdata(pdev);
	struct v4l2_subdev *sd = &dec->sd;

	clk_disable_unprepare(dec->clk);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&dec->hdl);

	return 0;
}


#ifdef CONFIG_PM_SLEEP
static int cvd_pm_suspend(struct device *dev)
{
	struct cvd_dev *dec = dev_get_drvdata(dev);

	clk_disable_unprepare(dec->clk);

	return 0;
}

static int cvd_pm_resume(struct device *dev)
{
	struct cvd_dev *dec = dev_get_drvdata(dev);

	clk_prepare_enable(dec->clk);

	/* restore all the HW configuration */
	cvd_init(&dec->sd, 0);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(cvd_pm_ops, cvd_pm_suspend, cvd_pm_resume);

static struct of_device_id cvd_match_tbl[] = {
	{ .compatible = "sirf,cvd", },
	{ /* end */ }
};

static struct platform_driver cvd_driver = {
	.driver		= {
		.name = CVD_DRV_NAME,
		.pm = &cvd_pm_ops,
		.of_match_table = cvd_match_tbl,
	},
	.probe = cvd_probe,
	.remove = cvd_remove,
};

static int __init sirfsoc_cvd_init(void)
{
	return platform_driver_register(&cvd_driver);
}

static void __exit sirfsoc_cvd_exit(void)
{
	platform_driver_unregister(&cvd_driver);
}

subsys_initcall(sirfsoc_cvd_init);
module_exit(sirfsoc_cvd_exit);


MODULE_DESCRIPTION("sirfsoc CVD(CVBS Decoder) driver");
MODULE_LICENSE("GPL v2");
