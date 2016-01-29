/*
 * CSR sirfsoc DCU driver header file
 *
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

#ifndef __SIRFSOC_DCU_H
#define __SIRFSOC_DCU_H

#include <linux/dma-attrs.h>
#include <linux/dma-mapping.h>

/*DCU register*/
#define DCU_CTRL				0x0000
#define DCU_FBUF_X				0x0004
#define DCU_FUT_FBUF_RD				0x0008
#define DCU_TOP_FBUF_RD				0x000c
#define DCU_BOT_FBUF_RD				0x0010
#define DCU_MOT_FBUF_RD				0x0014
#define DCU_TOP_FBUF_WR				0x0018
#define DCU_BOT_FBUF_WR				0x001c
#define DCU_MOT_FBUF_WR				0x0020
#define DCU_PROG_TOP				0x0024
#define DCU_PROG_BOT				0x0028
#define DCU_FILL_COLOR				0x002c
#define DCU_FILL_AREA0				0x0030
#define DCU_FILL_AREA1				0x0034
#define DCU_FILL_AREA2				0x0038
#define DCU_INTR_EN				0x003c
#define DCU_INTR_STATUS				0x0040
#define DCU_INTR_STATUS_ALIAS			0x0044
#define DCU_INTR				0x0048
#define DCU_LBUF				0x004c
#define DCU_NUM_STRIP				0x0050
#define DCU_STRIP_SIZE1				0x0054
#define DCU_STRIP_SIZE2				0x0058
#define DCU_STRIP_OVRLP				0x005c
#define DCU_VSIZE				0x0060
#define DCU_BLANK				0x0064
#define DCU_DITHER				0x0068
#define DCU_CMIF_DISABLE			0x006c
#define DCU_LLU_DONE_SEL			0x0070
#define DCU_FUT_BOT_FBUF_RD			0x0074
#define DCU_PREV_FBUF_RD			0x0078
#define DCU_SOFT_RESET				0x007c
#define DCU_NRDNT_SOFT_RESET			0x0080
#define DCU_NRDNT_REV_ID			0x0084
#define DCU_NRDNT_INPUT_FORMAT			0x009c
#define DCU_NRDNT_FIELD_POLARITY		0x00a0
#define DCU_NRDNT_DBG_MODE			0x00a4
#define DCU_NRDNT_DEINT_MODE			0x00a8
#define DCU_NRDNT_FILM_CTRL			0x00ac
#define DCU_NRDNT_MOTION_CTRL			0x00b0
#define DCU_NRDNT_LAI_CTRL			0x00b4
#define DCU_NRDNT_LAI_CEIL			0x00b8
#define DCU_NRDNT_LAI_VAR_FLOOR			0x00bc
#define DCU_NRDNT_LAI_VAR_FUNC			0x00c0
#define DCU_NRDNT_LAI_NOISE_FLOOR		0x00c4
#define DCU_NRDNT_FRAME_MOTION_THA		0x00c8
#define DCU_NRDNT_FRAME_MOTION_THB		0x00cc
#define DCU_NRDNT_FRAME_MOTION_THC		0x00d0
#define DCU_NRDNT_LAI_V90_CTRL			0x00d4
#define DCU_NRDNT_FIELD_MOTION_THA		0x00d8
#define DCU_NRDNT_FIELD_MOTION_THB		0x00dc
#define DCU_NRDNT_FIELD_MOTION_THC		0x00e0
#define DCU_NRDNT_FIELD_MOTION_GAIN		0x00e4
#define DCU_NRDNT_FIELD_MOTION_AUTO_DIS_MSB	0x00e8
#define DCU_NRDNT_FIELD_MOTION_AUTO_DIS_LSB	0x00ec
#define DCU_NRDNT_LAI_VIOLATE			0x00f0
#define DCU_NRDNT_DEFEATHER_FALSE_COL_CTRL	0x00f4
#define DCU_NRDNT_FALSE_COL_CTRL		0x00f8
#define DCU_NRDNT_FILM_ACCU_CTRL		0x0100
#define DCU_NRDNT_FILM_PAT_MATCH_CTRL		0x0104
#define DCU_NRDNT_FILM_FRAME_DIFF_MSB		0x0108
#define DCU_NRDNT_FILM_FRAME_DIFF_LSB		0x010c
#define DCU_NRDNT_FILM_FIELD_STATIC_THA_MSB	0x0110
#define DCU_NRDNT_FILM_FIELD_STATIC_THA_LSB	0x0114
#define DCU_NRDNT_FILM_FIELD_MOTION_THA_MSB	0x0118
#define DCU_NRDNT_FILM_FIELD_MOTION_THA_LSB	0x011c
#define DCU_NRDNT_FILM_FRAME_THRESHA_MSB	0x0120
#define DCU_NRDNT_FILM_FRAME_THRESHA_LSB	0x0124
#define DCU_NRDNT_FILM_FIELD_STATIC_THB_MSB	0x0128
#define DCU_NRDNT_FILM_FIELD_STATIC_THB_LSB	0x012c
#define DCU_NRDNT_FILM_FIELD_MOTION_THB_MSB	0x0130
#define DCU_NRDNT_FILM_FIELD_MOTION_THB_LSB	0x0134
#define DCU_NDDNT_FILM_FRAME_THB_MSB		0x0138
#define DCU_NRDNT_FILM_FRAME_THB_LSB		0x013c
#define DCU_NRDNT_FILM_STATUS1			0x0140
#define DCU_NRDNT_FILM_STATUS2			0x0144
#define DCU_NRDNT_FILM_STATUS3			0x0148
#define DCU_NRDNT_NEXT_FIELD_MOTION_STATUS_MSB	0x0154
#define DCU_NRDNT_NEXT_FIELD_MOTION_STATUS_MID	0x0158
#define DCU_NRDNT_NEXT_FIELD_MOTION_STATUS_LSB	0x015c
#define DCU_NRDNT_FRAME_MOTION_STATUS_MSB	0x0164
#define DCU_NRDNT_FRAME_MOTION_STATUS_MID	0x0168
#define DCU_NRDNT_FRAME_MOTION_STATUS_LSB	0x016c
#define DCU_NRDNT_K_FRAME_MODE			0x0174
#define DCU_NRDNT_HORIZONTAL_EDGE_DETECT	0x0178
#define DCU_NRDNT_HORIZONTAL_EDGE_DETECT_ADJUST	0x017c
#define DCU_NRDNT_IMPLUSE_FILTER_MDETECT_TH	0x0190
#define DCU_NRDNT_SPATIAL_FILTER_MDETECT_TH	0x0194
#define DCU_NRDNT_NOISE_FILTER_FLUSH_COUNT	0x0198
#define DCU_NRDNT_NOISE_FILTER_FLUSH_ENABLE	0x019c
#define DCU_NRDNT_NOISE_FILTER_ENABLE		0x01a0
#define DCU_NRDNT_CONSTANT_NOISE		0x01a4
#define DCU_NRDNT_EXTERNAL_NOISE		0x01a8
#define DCU_NRDNT_NOISE_LEVEL_SOURCE		0x01ac
#define DCU_NRDNT_SPATIAL_TH_WEIGHTS		0x01b8
#define DCU_NRDNT_TEMPORAL_TH_WEIGHTS		0x01bc
#define DCU_NRDNT_LUMA_SPATIAL_TH_MAX		0x01c0
#define DCU_NRDNT_CHROMA_SPATIAL_TH_MAX		0x01c4
#define DCU_NRDNT_LUMA_TEMPORAL_TH_MAX		0x01c8
#define DCU_NRDNT_CHROMA_TEMPORAL_TH_MAX	0x01cc
#define DCU_NRDNT_LUMA_SPATIAL_TH_MIN		0x01d0
#define DCU_NRDNT_CHROMA_SPATIAL_TH_MIN		0x01d4
#define DCU_NRDNT_LUMA_TEMPORAL_TH_MIN		0x01d8
#define DCU_NRDNT_CHROMA_TEMPORAL_TH_MIN	0x01dc
#define DCU_NRDNT_HORIZONTAL_EDGE_THA		0x01e0
#define DCU_NRDNT_HORIZONTAL_EDGE_THB		0x01e4
#define DCU_NRDNT_HORIZONTAL_EDGE_THC		0x01e8
#define DCU_NRDNT_STATUS_NOISE			0x01ec
#define DCU_NRDNT_IMPLUSE_WINDOW_SEL		0x01f0
#define DCU_NRDNT_AFM_FRAME_MDETECT_THA		0x0200
#define DCU_NRDNT_AFM_FRAME_MDETECT_THB		0x0204
#define DCU_NRDNT_AFM_FRAME_MDETECT_THC		0x0208
#define DCU_NRDNT_AFM_FIELD_MDETECT_THA		0x020c
#define DCU_NRDNT_AFM_FIELD_MDETECT_THB		0x0210
#define DCU_NRDNT_AFM_FIELD_MDETECT_THC		0x0214
#define DCU_NRDNT_AFM_FIELD_MDETECT_GAIN	0x0218
#define DCU_NRDNT_MOTION_DETECTION		0x0224
#define DCU_NRDNT_SCENE_CHANGE_THA1		0x0228
#define DCU_NRDNT_SCENE_CHANGE_THA2		0x022c
#define DCU_NRDNT_SCENE_CHANGE_THA3		0x0230
#define DCU_NRDNT_SCENE_CHANGE_THB1		0x0234
#define DCU_NRDNT_SCENE_CHANGE_THB2		0x0238
#define DCU_NRDNT_SCENE_CHANGE_THB3		0x023c
#define DCU_NRDNT_SCENE_CHANGE_FLUSH_NUM	0x0240
#define DCU_NRDNT_TEMPORAL_NOISE_ESTIMATION1	0x0244
#define DCU_NRDNT_TEMPORAL_NOISE_ESTIMATION2    0x0248
#define DCU_NRDNT_TEMPORAL_NOISE_ESTIMATION3    0x024c
#define DCU_NRDNT_TEMPORAL_NOISE_ESTIMATION4    0x0250
#define DCU_NRDNT_TEMPORAL_NOISE_ESTIMATION5    0x0254
#define DCU_NRDNT_TEMPORAL_NOISE_ESTIMATION6    0x0258
#define DCU_NRDNT_TEMPORAL_NOISE_ESTIMATION7    0x025c
#define DCU_NRDNT_SPATIAL_NOISE_ESTIMATION1	0x0264
#define DCU_NRDNT_SPATIAL_NOISE_ESTIMATION2     0x0268
#define DCU_NRDNT_SPATIAL_NOISE_ESTIMATION3     0x026c
#define DCU_NRDNT_SPATIAL_NOISE_ESTIMATION4     0x0270
#define DCU_NRDNT_SPATIAL_NOISE_ESTIMATION5     0x0274
#define DCU_NRDNT_SPATIAL_NOISE_ESTIMATION6     0x0278
#define DCU_NRDNT_SPATIAL_NOISE_ESTIMATION7     0x027c
#define DCU_NRDNT_FRAME_MDETC			0x0340
#define DCU_NRDNT_FRAME_MDETC_MAD_BLEND_TH	0x0344
#define DCU_NRDNT_FRAME_MDETC_MAD_BLEND_RES	0x0348
#define DCU_NRDNT_FRAME_MDETC_MAD_GAIN		0x034c
#define DCU_NRDNT_FRAME_MDETC_MAD_GAIN_CORE	0x0350
#define DCU_NRDNT_FRAME_MDETC_MAD_GAIN_CORE2	0x0354
#define DCU_NRDNT_FRAME_MDETC_MAD_GAIN_CORE_TH	0x0358
#define DCU_NRDNT_FRAME_MDETC_MAD_GAIN_CORE2_TH	0x035c
#define DCU_NRDNT_FRAME_MDETC_FIELD_GAIN	0x0360
#define DCU_NRDNT_FRAME_MDETC_FIELD_CORE_GAIN	0x0364
#define DCU_NRDNT_FRAME_MDETC_FIELD_CORE_TH	0x0368
#define DCU_NRDNT_FRAME_MDETC_RECURSIVE		0x036c
#define DCU_NRDNT_FRAME_MDETC_RECURSIVE_GAIN	0x0370
#define DCU_BAD_EDIT_FRAME_THA			0x0374
#define DCU_BAD_EDIT_FRAME_THB			0x0378
#define DCU_BAD_EDIT_FRAME_THC			0x037c
#define DCU_BAD_EDIT_FIELD_THA			0x0380
#define DCU_BAD_EDIT_FIELD_THB			0x0384
#define DCU_BAD_EDIT_FIELD_THC			0x0388
#define DCU_BAD_EDIT_DETC_ENABLE		0x038c
#define DCU_BAD_EDIT_DETC_AFTER_MODE		0x0390
#define DCU_BAD_EDIT_DETC_STATUS		0x0394
#define DCU_DISTRESS_THA_ADDR			0x0398
#define DCU_DISTRESS_THB_ADDR			0x039c
#define DCU_CYCLES_PER_LINE_ADDRA		0x03a0
#define DCU_CYCLES_PER_LINE_ADDRB		0x03a4
#define DCU_DISTRESS_SEL			0x03a8
#define DCU_NRDNT_PREV_FIELD_MOTION_STATUS_MSB	0x03b8
#define DCU_NRDNT_PREV_FIELD_MOTION_STATUS_MID	0x03bc
#define DCU_NRDNT_PREV_FIELD_MOTION_STATUS_LSB	0x03c0
#define DCU_MPP_SOFT_RESET			0x0480
#define DCU_MPP_REVISION_ID			0x0484
#define DCU_MPP_DCT_BLK_CNTL			0x0490
#define DCU_MPP_DCT_CWIDTH16			0x0494
#define DCU_MPP_VBLKDET_CNTL0			0x04c0
#define DCU_MPP_VBLKDET_CNTL1			0x04c4
#define DCU_MPP_VBLKDET_CNTL2			0x04c8
#define DCU_MPP_HBLKDET_CNTL0			0x04d8
#define DCU_MPP_HBLKDET_CNTL1			0x04dc
#define DCU_MPP_HBLKDET_CNTL2			0x04e0
#define DCU_MPP_VDBK_CNTL			0x0500
#define DCU_MPP_VDBK_C_CNTL			0x0504
#define DCU_MPP_VDBK_C_CNTL2			0x0508
#define DCU_MPP_HDBK_CNTL			0x0520
#define DCU_MPP_HDBK_C_CNTL			0x0524
#define DCU_MPP_HDBK_C_CNTL2			0x0528
#define DCU_MPP_DRG_EDGE_TH			0x0530
#define DCU_MPP_DRG_TH				0x0534
#define DCU_MPP_DRG_FILT_CNTL			0x0538
#define DCU_MPP_DRG_FILT_SEL			0x053c
#define DCU_MPP_DBK_FILT_CNTL			0x0540
#define DCU_MPP_DBG_MODE			0x0560
#define DCU_MPP_STATUS_READY_TOGGLE		0x0580
#define DCU_MPP_STATUS_VBLK_SUM0		0x0584
#define DCU_MPP_STATUS_VBLK_SUM1		0x0588
#define DCU_MPP_STATUS_VBLK_SUM2		0x058c
#define DCU_MPP_STATUS_VBLK_SUM3		0x0590
#define DCU_MPP_STATUS_HBLK_SUM0		0x05a0
#define DCU_MPP_STATUS_HBLK_SUM1		0x05a4
#define DCU_MPP_STATUS_HBLK_SUM2		0x05a8
#define DCU_MPP_STATUS_HBLK_SUM3		0x05ac
#define DCU_MPP_EE_MODE_Y			0x05c0
#define DCU_MPP_PEAK_COEF_Y			0x05c4
#define DCU_MPP_PEAK_GAIN_SET1			0x05c8
#define DCU_MPP_PEAK_GAIN_SET2			0x05cc
#define DCU_MPP_EE_MODE_C			0x05e0
#define DCU_MPP_PEAK_COEF_C			0x05e4
#define DCU_VS_CFG				0x0680
#define DCU_VS_WRITEBACK_CFG			0x0684
#define DCU_VS_LUM_WRITEBACK_SIZE		0x068c
#define DCU_VS_LUM_RD_SIZE			0x078c
#define DCU_VS_LUM_V_INT_CTRL			0x07b4
#define DCU_VS_LUM_V_INT_START			0x07b8
#define DCU_VS_LUM_V_SCALE_PAR			0x07bc
#define DCU_VS_LUM_V_DDA_PAR			0x07c0
#define DCU_VS_LUM_V_DDA_START			0x07c4
#define DCU_VS_LUM_V_COEF_RAM01_START		0x0980
#define DCU_VS_LUM_V_COEF_RAM01_END		0x0a7c
#define DCU_VS_LUM_V_COEF_RAM23_START		0x0a80
#define DCU_VS_LUM_V_COEF_RAM23_END		0x0b7c
#define DCU_VS_LUM_V_COEF_RAM45_START		0x0b80
#define DCU_VS_LUM_V_COEF_RAM45_END		0x0c7c
#define DCU_CONTEXT_CONFIG			0x0e90
#define DCU_NRD_COUNTER_STATUS			0x0eec
#define DCU_INLINE_CROP_H			0x0f10
#define DCU_INLINE_CROP_V			0x0f14
#define DCU_INLINE_HSIZE_PATCH			0x0f18
#define DCU_INLINE_FIFO_FULL_TH			0x0f1c
#define DCU_INLINE_FIFO_VALID_TH		0x0f20
#define DCU_INLINE_FIFO_INVALID_TH		0x0f24
#define DCU_INLINE_FIFO1_STATUS			0x0f28
#define DCU_INLINE_FIFO2_STATUS			0x0f2c
#define DCU_INLINE_FIFO2_STATISTICS		0x0f30
#define DCU_INLINE_FIFO2_STATISTICS_CTRL	0x0f34
#define DCU_INLINE_FLD				0x0f38
#define DCU_LINEAR_CFG				0x0f50
#define DCU_LINEAR_START_ADDR_Y			0x0f54
#define DCU_LINEAR_START_ADDR_C			0x0f58
#define DCU_LINEAR_PITCH_Y			0x0f5c
#define DCU_LINEAR_PITCH_C			0x0f60
#define DCU_MIF2AXI_VPP_CTRL			0x1000
#define DCU_MIF2AXI_FUT_FBUF_Y_BASE		0x1010
#define DCU_MIF2AXI_FUT_FBUF_U_BASE		0x1014
#define DCU_MIF2AXI_FUT_FBUF_V_BASE		0x1018
#define DCU_MIF2AXI_FUT_FBUF_Y_STRIDE		0x101c
#define DCU_MIF2AXI_FUT_FBUF_U_STRIDE		0x1020
#define DCU_MIF2AXI_FUT_FBUF_V_STRIDE		0x1024
#define DCU_MIF2AXI_TOP_FBUF_Y_BASE		0x1028
#define DCU_MIF2AXI_TOP_FBUF_U_BASE		0x102c
#define DCU_MIF2AXI_TOP_FBUF_V_BASE		0x1030
#define DCU_MIF2AXI_TOP_FBUF_Y_STRIDE		0x1034
#define DCU_MIF2AXI_TOP_FBUF_U_STRIDE		0x1038
#define DCU_MIF2AXI_TOP_FBUF_V_STRIDE		0x103c
#define DCU_MIF2AXI_BOT_FBUF_Y_BASE		0x1040
#define DCU_MIF2AXI_BOT_FBUF_U_BASE		0x1044
#define DCU_MIF2AXI_BOT_FBUF_V_BASE		0x1048
#define DCU_MIF2AXI_BOT_FBUF_Y_STRIDE		0x104c
#define DCU_MIF2AXI_BOT_FBUF_U_STRIDE		0x1050
#define DCU_MIF2AXI_BOT_FBUF_V_STRIDE		0x1054
#define DCU_MIF2AXI_MOT_FBUF_RD_BASE		0x1058
#define DCU_MIF2AXI_MOT_FBUF_RD_STRIDE		0x105c
#define DCU_MIF2AXI_MOT_FBUF_WR_BASE		0x1060
#define DCU_MIF2AXI_MOT_FBUF_WR_STRIDE		0x1064
#define DCU_INLINE_ADDR				0x1080
#define DCU_AXIARB_PRI_LOW			0x10c4
#define DCU_AXIARB_PARK				0x10cc
#define DCU_AXIARB_RQ_EN			0x10d0
#define DCU_AXIARB_HI_SWITCH			0x10d4
#define DCU_AXIARB_INT_EN			0x10d8
#define DCU_AXIARB_INT_STATUS			0x10dc
#define DCU_AXIARB_TIMEOUT			0x10e0
#define DCU_AXIARB_CONF				0x10e4
#define DCU_AXIARB_ACC_NUM0			0x10e8
#define DCU_AXIARB_ACC_NUM1			0x10ec

#define NOCFIFO_MODE				0x0000
#define NOCFIFO_INT_ENA				0x0004
#define NOCFIFO_INT_STS				0x0008
#define NOCFIFO_MAX_FULLNESS			0x000c
#define NOCFIFO_READ_OFFSET			0x0010
#define NOCFIFO_WRITE_OFFSET			0x0014
#define NOCFIFO_PROBE_ADDR			0x0018
#define NOCFIFO_PROBE_DATA_MSW			0x001c
#define NOCFIFO_PROBE_DATA_LSW			0x0020
#define NOCFIFO_GAP_COUNT			0x0024

/*DCU scaler parameter struct*/
#define DCU_VS_NUM_COEF_PHASE			64
#define DCU_VS_NUM_COEF_TOTAL			\
	(6 * DCU_VS_NUM_COEF_PHASE)
#define DCU_VS_NUM_COEF_6TAP			5
#define DCU_VS_NUM_COEF_12TAP			3
#define DCU_VS_SMALL_VSF_TH			25
#define DCU_VS_MAX_STRIP_SIZE_SMALL_VSF		512

#define DCU_NRDNT_SCALE_SHIFT			10
#define DCU_NRDNT_TH_SCALE(t, s)	(((t)*(s))>>DCU_NRDNT_SCALE_SHIFT)

#define VIDEO_HD				(1920 * 1080)
#define VIDEO_SD				(720 * 480)

struct dcu_vs_coef_set {
	u32	scaling_factor;
	s16	coef[DCU_VS_NUM_COEF_TOTAL];
};

struct dcu_vs_interp_info {
	u8	num_tap;
	u8	num_phase;
	s16	init_phase;
	s16	start_line;
	struct dcu_vs_coef_set	*coef_set;
	u32	coef_set_num;
};

enum dcu_vs_interp {
	DCU_VS_6_TAP_INTERP = 0,
	DCU_VS_12_TAP_INTERP = 1,
	DCU_VS_NUM_INTERP,
};

struct dcu_vs_params {
	u32	clip_width;
	u32	clip_height;
	u32	output_width;
	u32	output_height;
	u16	left_strip_size;
	enum dcu_vs_interp	interp;
	u32	coef_scaling_factor;
	u32	scaling_factor;
	u8	num_tap;
	u8	num_phase;
	u8	frac_offset;
	s16	start_line;
	u16	scale_factor;
	s16	over_shoot;
	s16	under_shoot;
	s16	init_phase;
};

/*DCU EE parameter struct*/
#define DCU_EE_LUMA_NUM_GAINS		8
#define DCU_EE_LUMA_AUTO_INDEX		4
struct dcu_ee_params {
	u8	curr_luma_filter;
	u32	ee_mode_y;
	u32	coef_th_y;
	u32	peak_gains_14;
	u32	peak_gains_58;
	u8	curr_chroma_filter;
	u32	ee_mode_c;
	u32	coef_th_c;
	u32	ee_filter;
	u32	ee_filter_th;
	u32	sharpness_level;
	u32	ee_level;
	u32	scaling_factor;
};

struct dcu_ee_luma_filter {
	bool	use_lowpass;
	u32	peaking_mode;
	s32	coef1_5;
	s32	coef2_4;
	u32	coef3;
	u32	coring_mode;
	u32	coring_th;
	bool	use_brightness;
	u32	gains[DCU_EE_LUMA_NUM_GAINS];
};

struct dcu_ee_chroma_filter {
	u32	use_filter;
	s32	coef1_5;
	s32	coef2_4;
	u32	coef3;
	u32	coring_mode;
	u32	coring_th;
};

/*DCU MPP parameter struct*/
struct dcu_ipp_setting {
	u32	dblk_v_mode;
	u32	dblk_vc_mode;
	u32	dblk_h_mode;
	u32	dblk_hc_mode;
};

struct dcu_ipp_params {
	u32	blk_filt_ctrl;
	u32	vblk_luma_ctrl;
	u32	vblk_chroma_ctrl;
	u32	vblk_chroma_ctrl2;
	u32	hblk_luma_ctrl;
	u32	hblk_chroma_ctrl;
	u32	hblk_chroma_ctrl2;
	u32	dct_blk_ctrl;
	u32	dct_h_offset;
	u32	dct_h_confidence;
	u32	dct_v_offset;
	u32	dct_v_confidence;
	u32	vblk_det_ctrl;
	u32	vblk_det_th13;
	u32	vblk_det_th46;
	u32	hblk_det_ctrl;
	u32	hblk_det_th13;
	u32	hblk_det_th46;
};

/*DCU nrdnt paramrter struct*/
struct dcu_nrdnt_params {
	u32	deint_mode;
	u32	interp_ctrl;
	u32	interp_v90_ctrl;
	u32	luma_mdet_mode;
	u32	chroma_mdet_mode;
	u32	frame_mdet_tha;
	u32	frame_mdet_thb;
	u32	frame_mdet_thc;
	u32	frame_mdet_mode;
	u32	frame_mdet_blendth;
	u32	frame_mdet_blendres;
	u32	frame_mdet_gain;
	u32	frame_mdet_gaincore;
	u32	frame_mdet_gaincore2;
	u32	frame_mdet_gaincoreth;
	u32	frame_mdet_gaincore2th;
	u32	frame_mdet_fieldgain;
	u32	frame_mdet_fieldcoregain;
	u32	frame_mdet_fieldcoreth;
	u32	frame_mdet_recursive;
	u32	frame_mdet_recursivegain;
	u32	field_mdet_tha;
	u32	field_mdet_thb;
	u32	field_mdet_thc;
	u32	field_mdet_gain;
	u32	field_mdet_autodis;
	u32	field_mdet_autodislsb;
	u32	field_mdet_autodismsb;
	u32	k_history_det;
};

/* DCU motion buffer memory region */
struct dcu_mem_region {
	struct dma_attrs attrs;
	void *token;
	dma_addr_t dma_handle;
	u32 paddr;
	void __iomem *vaddr;
	u32 size;
};

/*DCU field buffer*/
struct dcu_field_buf {
	u32 yaddr_phy;
	u32 uaddr_phy;
	u32 vaddr_phy;
	u32 ystride;
	u32 ustride;
	u32 vstride;
};

/*DCU motion buffer*/
struct dcu_mot_buf {
	struct dcu_field_buf *buf_read;
	struct dcu_field_buf *buf_write;
	struct dcu_field_buf mot_buf[2];
	/*struct dcu_mem_region rg*/
	dma_addr_t mot_mem_addr;
	void __iomem *mot_base;
	u32 mot_size;
};

/*DCU parameter struct*/
struct dcu_param_set {
	bool	is_motbuf_init;
	bool	is_inline;
	bool	is_first;
	bool	dcu_start;
	u32	dcu_ctrl;
	u32	cmif_disable;
	u32	linear_config;
	u32	input_format;
	u32	vpp_ctrl;
	u32	tempbuf_base;
	struct dcu_nrdnt_params	nrdnt_params;
	struct dcu_vs_params	vs_params;
	struct dcu_ipp_params	ipp_params;
	struct dcu_ee_params	ee_params;

	struct dcu_field_buf	out_buf;
	struct dcu_field_buf	top_buf;
	struct dcu_field_buf	bot_buf;
	struct dcu_field_buf	fut_buf;
	struct dcu_mot_buf	mot_buf;

	void	*dev;
	void	*core_iomem;
	void	*nocfifo_iomem;
};

/*DCU common function declare*/
unsigned int dcu_read_reg(void __iomem *iomem,
		unsigned int offset);
void dcu_write_reg(void __iomem *iomem,
	unsigned int offset,
	unsigned int value);

/*DCU scaler function declare*/
void dcu_vs_reset(struct dcu_param_set *dcu_param_set);
void dcu_vs_init(struct dcu_param_set *dcu_param_set);
bool dcu_vs_calc_params(struct dcu_param_set *dcu_param_set);
bool dcu_vs_load_coefset(struct dcu_param_set *dcu_param_set);

/*DCU IPP function declare*/
void dcu_ipp_init(struct dcu_param_set *dcu_param_set);
void dcu_ipp_reset(struct dcu_param_set *dcu_param_set);
void dcu_ipp_setup(struct dcu_param_set *dcu_param_set);
void dcu_ipp_set_mode(struct dcu_param_set *dcu_param_set,
		u32 v_mode, u32 v_c_mode, u32 h_mode, u32 h_c_mode);
void dcu_ipp_get_mode(struct dcu_param_set *dcu_param_set,
		u32 *v_mode, u32 *v_c_mode, u32 *h_mode, u32 *h_c_mode);
void dcu_ipp_set_luma(struct dcu_param_set *dcu_param_set,
			bool bvertical, u32 blk_mode, u32 blk_thresh1,
			u32 blk_thresh2, u32 blk_clip);
void dcu_ipp_set_chroma(struct dcu_param_set *dcu_param_set,
			bool bvertical, u32 blk_mode, u32 blk_thresh1,
			u32 blk_thresh2, u32 blk_clip, u32 c_same);
void dcu_ipp_set_det_mode(struct dcu_param_set *dcu_param_set,
			bool bvertical, u32 blk_det_mode,
			u32 blk_det_ratio, u32 blk_det_ratio1);
void dcu_ipp_set_det_thresh(struct dcu_param_set *dcu_param_set,
			bool bvertical, u32 blk_det_thresh1,
			u32 blk_det_thresh2, u32 blk_det_thresh3,
			u32 blk_det_thresh4, u32 blk_det_thresh5,
			u32 blk_det_thresh6);
void dcu_ipp_set_dct_offset(struct dcu_param_set *dcu_param_set);

/*DCU EE function declare*/
void dcu_ee_init(struct dcu_param_set *dcu_param_set);
bool dcu_ee_add_luma_filter(struct dcu_ee_luma_filter *luma_filter);
bool dcu_ee_set_luma_filter(
	struct dcu_param_set *dcu_param_set, u8 filter_ind);
bool dcu_ee_add_chroma_filter(struct dcu_ee_chroma_filter *chroma_filter);
bool dcu_ee_set_chroma_filter(
	struct dcu_param_set *dcu_param_set, u8 filter_ind);
bool dcu_ee_set_sharpness(struct dcu_param_set *dcu_param_set,
				u32 sharpness_level);
bool dcu_ee_set_scale_factor(struct dcu_param_set *dcu_param_set,
				u32 src_height, u32 dst_height);

/*DCU NRDNT function declare*/
void dcu_nrdnt_init(struct dcu_param_set *dcu_param_set);
void dcu_nrdnt_reset(struct dcu_param_set *dcu_param_set);
void dcu_nrdnt_set_deint_mode(struct dcu_param_set *dcu_param_set,
		u32 deint_mode, u32 luma_weave_mode, u32 chroma_weave_mode);
void dcu_nrdnt_set_lowangle_interpmode(struct dcu_param_set *dcu_param_set,
			u32 interp_mode, u32 interp_force,
			u32 interp_relaxed, u32 interp_blend_halfpix);
void dcu_nrdnt_set_lowangle_interpv90mode(struct dcu_param_set *dcu_param_set,
			u32 interp_gain, u32 interp_strong,
			u32 interp_blk_repeat, u32 inter_pblend_repeat);
void dcu_nrdnt_set_mode(struct dcu_param_set *dcu_param_set,
			u32 field_mdet_luma, u32 frame_mdet_luma,
			u32 frame_mdet_chroma, u32 frame_mdet_chromakadap);
void dcu_nrdnt_set_frame_mode(struct dcu_param_set *dcu_param_set,
				bool enable_5field, u32 mad_adap,
				u32 mad_5field_gain, u32 k_mode);
void dcu_nrdnt_set_frame_mad(struct dcu_param_set *dcu_param_set,
	bool mad_blend_th, u32 mad_blend_res, u32 mad_gain, u32 mad_gain_core,
	u32 mad_gain_core2, u32 mad_gain_coreth, u32 mad_gain_core2th);
void dcu_nrdnt_set_frame_field(struct dcu_param_set *dcu_param_set,
		u32 field_gain, u32 field_core_gain, u32 field_core_th);
void dcu_nrdnt_set_frame_recursive(struct dcu_param_set *dcu_param_set,
			bool enable_recursive, u32 recursive_accth_mode,
			u32 recursive_accth, u32 recursive_frame_mult,
			u32 recursive_field_div);
void dcu_nrdnt_set_frame_th(struct dcu_param_set *dcu_param_set,
		u32 frame_mdet_tha, u32 frame_mdet_thb, u32 frame_mdet_thc);
void dcu_nrdnt_set_field_th(struct dcu_param_set *dcu_param_set,
		u32 field_mdet_tha, u32 field_mdet_thb, u32 field_mdet_thc,
		u32 field_mdet_adap, u32 field_mdet_gain);
void dcu_nrdnt_set_field_automode(struct dcu_param_set *dcu_param_set,
		u32 field_mdet_autodisen, u32 field_mdet_autoth);
void dcu_nrdnt_set_k_history(struct dcu_param_set *dcu_param_set,
			u32 k_frame_on, u32 k_frame_mode,
			u32 k_frame_noise, u32 k_frame_count);
void dcu_nrdnt_set_th_scale(struct dcu_param_set *dcu_param_set,
			bool hd_src, u32 h_size, u32 v_size);

#define FBUF_MASK		(0x3 << 6)
#define INPUT_FORMAT_YUV422	(0x0 << 0)
#define INPUT_FORMAT_YUV420	(0x7 << 0)
#define OUT_LITTLE_ENDIAN	(0x1 << 17)
#define OUT_BIG_ENDIAN		(0x0 << 17)
#define IN_LITTLE_ENDIAN	(0x1 << 16)
#define IN_BIG_ENDIAN		(0x0 << 16)
#define UV_INTERLEAVE_EN	(0x1 << 14)
#define VUVU_MODE		(0x0 << 13)
#define UVUV_MODE		(0x1 << 13)
#define NOCFIFO_DIRECT_AXIW	(0x1 << 8)
#define YUV422_FORMAT_YUYV	(0x0 << 2)
#define YUV422_FORMAT_YVYU	(0x1 << 2)
#define YUV422_FORMAT_UYVY	(0x2 << 2)
#define YUV422_FORMAT_VYUY	(0x3 << 2)
#define YUV422_FORMAT_LITTLE_ENDIAN	(0x0 << 1)
#define YUV422_FORMAT_BIG_ENDIAN	(0x1 << 1)
#define YUV422_FORMAT		(0x0 << 0)
#define YUV420_FORMAT		(0x1 << 0)
#define RAND2_EN_B		(0x1 << 25)
#define RAND3_EN_B		(0x1 << 24)
#define RAND2_EN_A		(0x1 << 17)
#define RAND3_EN_A		(0x1 << 16)
#define LBUF_START		(0x1 << 0)
#define EDGE_DETECT_EN		(0x1 << 0)
#define SEL1_DISABLE		(0xf << 4)
#define INLINE_DONE		(0x3 << 0)
#define SHADOW_EN		(0x1 << 8)
#define DCU_START		(0x1 << 9)
#define VS_EN			(0x1 << 31)
#define FIELD_PIX_SIZE		(0x8 << 28)
#define MOT_PIX_SIZE		(0x6 << 28)
#define FUT_BASE		(0x0 << 0)
#define TOP_BASE		(0x1 << 0)
#define BOT_BASE		(0x2 << 0)
#define RD_BASE			(0x3 << 0)
#define WR_BASE			(0x4 << 0)
#define LAI_VAR_CEIL		(0xa << 0)
#define LAI_VAR_FLOOR		(0x1 << 0)
#define LAI_VAR_FUNC		(0x256/0x9)
#define LAI_NOISE_FLOOR         0x10
#define LAI_VIOLATE		0x27
#define NOISE_LEVEL_SRC		0x12
#define IMPLUSE_WIN_SEL		0x1
#define NOISE_FILTER_EN		0x3
#define INLINE_FIFO01_RESET	(0x1 << 8)
#define INLINE_FIFO2_RESET	(0x1 << 9)
#define NOCFIFO_EN		0x1
#define NOCFIFO_DISABLE		0x0
#define EVEN_FIELD		(0x1 << 9)
#define INLINE_EN		(0x1 << 23)
#define INLINE_DONE_INTR_EN	0x8
#define DCU_YUV420_FORMAT	(0x0 << 16)
#define DCU_YUV422_FORMAT	(0x1 << 16)
#define NXT_WR_DIS		(0x1 << 10)
#define BOT_PROG_WR_DIS		(0x1 << 9)
#define TOP_PROG_WR_DIS		(0x1 << 8)
#define PREV_RD_DIS		(0x1 << 4)
#define SWAP_C_8		(0x1 << 4)
#define SWAP_Y_8		(0x1 << 1)
#define NOCFIFO_GAP_MAX		0x3f
#define HBLANK_SIZE		0x94
#define VBLANK_SIZE		(0xd << 16)

#define dcu_success		0x0
#define dcu_more_data		0x1
#define dcu_param_invalid	0x2

#define dcu_err(fmt, ...)	pr_err(fmt, ## __VA_ARGS__)
#define dcu_dbg(fmt, ...)	pr_debug(fmt, ## __VA_ARGS__)
#define dcu_info(fmt, ...)	pr_info(fmt, ## __VA_ARGS__)

#endif
