/*
 * CSR sirfsoc vdss core file
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include <video/sirfsoc_vdss.h>
#include "dcu.h"

#define DCU_IPP_MAX_BINS			8
#define DCU_IPP_DBK_STATUS_MIN_TH		200
#define DCU_IPP_DBK_CONFIDENCE_GAIN		8
#define DCU_IPP_DCT_OFFSET_NOT_VALID		0xFF
#define DCU_IPP_DBK_CONFIDENCE_MAX_LEVEL	8

void dcu_ipp_init(struct dcu_param_set *dcu_param_set)
{
	dcu_ipp_reset(dcu_param_set);
}

void dcu_ipp_reset(struct dcu_param_set *dcu_param_set)
{
	dcu_write_reg(dcu_param_set->core_iomem, DCU_MPP_SOFT_RESET, 0x1);
	dcu_write_reg(dcu_param_set->core_iomem, DCU_MPP_SOFT_RESET, 0x0);
	dcu_write_reg(dcu_param_set->core_iomem, DCU_MPP_DCT_CWIDTH16, 0x1);
}

void dcu_ipp_setup(struct dcu_param_set *dcu_param_set)
{
	struct dcu_ipp_params *ipp_params;

	ipp_params = &dcu_param_set->ipp_params;
	ipp_params->dct_h_offset = DCU_IPP_DCT_OFFSET_NOT_VALID;
	ipp_params->dct_h_confidence = 0x0;
	ipp_params->dct_v_offset = DCU_IPP_DCT_OFFSET_NOT_VALID;
	ipp_params->dct_v_confidence = 0x0;

	ipp_params->dct_blk_ctrl = 0x0;
	ipp_params->dct_blk_ctrl |= ipp_params->dct_v_offset & 0x7;
	ipp_params->dct_blk_ctrl |=
		((ipp_params->dct_v_confidence & 0xf) << 8);
	ipp_params->dct_blk_ctrl |= ((ipp_params->dct_h_offset & 0x7) << 4);
	ipp_params->dct_blk_ctrl |=
		((ipp_params->dct_h_confidence & 0xf) << 12);
}

void dcu_ipp_set_mode(struct dcu_param_set *dcu_param_set,
			u32 v_mode, u32 v_cmode,
			u32 h_mode, u32 h_cmode)
{
	struct dcu_ipp_params *ipp_params;

	ipp_params = &dcu_param_set->ipp_params;
	ipp_params->blk_filt_ctrl = 0x0;
	ipp_params->blk_filt_ctrl |= v_mode & 0x3;
	ipp_params->blk_filt_ctrl |= ((v_cmode & 0x3) << 4);
	ipp_params->blk_filt_ctrl |= ((h_mode & 0x3) << 8);
	ipp_params->blk_filt_ctrl |= ((h_cmode & 0x3) << 12);
}

void dcu_ipp_get_mode(struct dcu_param_set *dcu_param_set,
			u32 *v_mode, u32 *v_cmode,
			u32 *h_mode, u32 *h_cmode)
{
	struct dcu_ipp_params *ipp_params;

	ipp_params = &dcu_param_set->ipp_params;
	*v_mode = ipp_params->blk_filt_ctrl & 0x3;
	*v_cmode = (ipp_params->blk_filt_ctrl >> 4) & 0x3;
	*h_mode = (ipp_params->blk_filt_ctrl >> 8) & 0x3;
	*h_cmode = (ipp_params->blk_filt_ctrl >> 12) & 0x3;
}

void dcu_ipp_set_luma(struct dcu_param_set *dcu_param_set,
			bool bvertical, u32 blk_mode,
			u32 blk_thresh1, u32 blk_thresh2,
			u32 blk_clip)
{
	struct dcu_ipp_params *ipp_params;

	ipp_params = &dcu_param_set->ipp_params;
	if (bvertical) {
		ipp_params->vblk_luma_ctrl = 0x0;
		ipp_params->vblk_luma_ctrl |= blk_thresh1 & 0x3ff;
		ipp_params->vblk_luma_ctrl |= ((blk_thresh2 & 0x3ff) << 10);
		ipp_params->vblk_luma_ctrl |= ((blk_clip & 0x3ff) << 20);
	} else {
		ipp_params->hblk_luma_ctrl = 0x0;
		ipp_params->hblk_luma_ctrl |= ((blk_mode & 0x1) << 30);
		ipp_params->hblk_luma_ctrl |= blk_thresh1 & 0x3ff;
		ipp_params->hblk_luma_ctrl |= ((blk_thresh2 & 0x3ff) << 10);
		ipp_params->hblk_luma_ctrl |= ((blk_clip & 0x3ff) << 20);
	}
}

void dcu_ipp_set_chroma(struct dcu_param_set *dcu_param_set,
			bool bvertical, u32 blk_mode,
			u32 blk_thresh1, u32 blk_thresh2,
			u32 blk_clip, u32 c_same)
{
	struct dcu_ipp_params *ipp_params;

	ipp_params = &dcu_param_set->ipp_params;
	if (bvertical) {
		ipp_params->vblk_chroma_ctrl = 0x0;
		ipp_params->vblk_chroma_ctrl |= blk_thresh1 & 0x3ff;
		ipp_params->vblk_chroma_ctrl |= ((blk_thresh2 & 0x3ff) << 10);
		ipp_params->vblk_chroma_ctrl |= ((blk_clip & 0x3ff) << 20);

		ipp_params->vblk_chroma_ctrl2 = c_same & 0x7;
	} else {
		ipp_params->hblk_chroma_ctrl = 0x0;
		ipp_params->hblk_chroma_ctrl |= ((blk_mode & 0x1) << 30);
		ipp_params->hblk_chroma_ctrl |= blk_thresh1 & 0x3ff;
		ipp_params->hblk_chroma_ctrl |= ((blk_thresh2 & 0x3ff) << 10);
		ipp_params->hblk_chroma_ctrl |= ((blk_clip & 0x3ff) << 20);

		ipp_params->hblk_chroma_ctrl2 |= c_same & 0x7;
	}
}

void dcu_ipp_set_det_mode(struct dcu_param_set *dcu_param_set,
			bool bvertical, u32 blk_det_mode,
			u32 blk_det_ratio, u32 blk_det_ratio1)
{
	struct dcu_ipp_params *ipp_params;

	ipp_params = &dcu_param_set->ipp_params;
	if (bvertical) {
		ipp_params->vblk_det_ctrl = 0x0;
		ipp_params->vblk_det_ctrl |= ((blk_det_mode & 0x1) << 8);
		ipp_params->vblk_det_ctrl |= blk_det_ratio & 0xf;
		ipp_params->vblk_det_ctrl |= ((blk_det_ratio1 & 0xf) << 4);
	} else {
		ipp_params->hblk_det_ctrl = 0x0;
		ipp_params->hblk_det_ctrl |= ((blk_det_mode & 0x1) << 8);
		ipp_params->hblk_det_ctrl |= blk_det_ratio & 0xf;
		ipp_params->hblk_det_ctrl |= ((blk_det_ratio1 & 0xf) << 4);
	}
}

void dcu_ipp_set_det_thresh(struct dcu_param_set *dcu_param_set,
			bool bvertical, u32 blk_det_thresh1,
			u32 blk_det_thresh2, u32 blk_det_thresh3,
			u32 blk_det_thresh4, u32 blk_det_thresh5,
			u32 blk_det_thresh6)
{
	struct dcu_ipp_params *ipp_params;

	ipp_params = &dcu_param_set->ipp_params;
	if (bvertical) {
		ipp_params->vblk_det_th13 = 0x0;
		ipp_params->vblk_det_th13 |= blk_det_thresh1 & 0x3ff;
		ipp_params->vblk_det_th13 |= ((blk_det_thresh2 & 0x3ff) << 10);
		ipp_params->vblk_det_th13 |= ((blk_det_thresh3 & 0x3ff) << 20);

		ipp_params->vblk_det_th46 = 0x0;
		ipp_params->vblk_det_th46 |= blk_det_thresh4 & 0x3ff;
		ipp_params->vblk_det_th46 |= ((blk_det_thresh5 & 0x3ff) << 10);
		ipp_params->vblk_det_th46 |= ((blk_det_thresh6 & 0x3ff) << 20);
	} else {
		ipp_params->hblk_det_th13 = 0x0;
		ipp_params->hblk_det_th13 |= blk_det_thresh1 & 0x3ff;
		ipp_params->hblk_det_th13 |= ((blk_det_thresh2 & 0x3ff) << 10);
		ipp_params->hblk_det_th13 |= ((blk_det_thresh3 & 0x3ff) << 20);

		ipp_params->hblk_det_th46 = 0x0;
		ipp_params->hblk_det_th46 |= blk_det_thresh4 & 0x3ff;
		ipp_params->hblk_det_th46 |= ((blk_det_thresh5 & 0x3ff) << 10);
		ipp_params->hblk_det_th46 |= ((blk_det_thresh6 & 0x3ff) << 20);
	}
}

static void __dcu_ipp_calc_dct_offset(struct dcu_param_set *dcu_param_set,
				bool bvertical, u32 *offset, u32 *confidence)
{
	u32	i = 0;
	u32	reg_val;
	u32	cur_bin_value;
	u32	cur_sum = 0;
	u32	cur_max_value = 0;
	u32	cur_offset = 0;
	u32	cur_average = 0;
	u32	cur_confidence = 0;
	u16	reg_bin_val[DCU_IPP_MAX_BINS];
	u32	confidence_gain = DCU_IPP_DBK_CONFIDENCE_GAIN / 8;
	void	*io_mem;
	struct	dcu_ipp_params *ipp_params;

	io_mem = dcu_param_set->core_iomem;
	ipp_params = &dcu_param_set->ipp_params;
	if (bvertical) {
		reg_val = dcu_read_reg(io_mem, DCU_MPP_STATUS_VBLK_SUM0);
		reg_bin_val[i++] = reg_val & 0xffff;
		reg_bin_val[i++] = (reg_val >> 16) & 0xffff;

		reg_val = dcu_read_reg(io_mem, DCU_MPP_STATUS_VBLK_SUM1);
		reg_bin_val[i++] = reg_val & 0xffff;
		reg_bin_val[i++] = (reg_val >> 16) & 0xffff;

		reg_val = dcu_read_reg(io_mem, DCU_MPP_STATUS_VBLK_SUM2);
		reg_bin_val[i++] = reg_val & 0xffff;
		reg_bin_val[i++] = (reg_val >> 16) & 0xffff;

		reg_val = dcu_read_reg(io_mem, DCU_MPP_STATUS_VBLK_SUM3);
		reg_bin_val[i++] = reg_val & 0xffff;
		reg_bin_val[i++] = (reg_val >> 16) & 0xffff;
	} else {
		reg_val = dcu_read_reg(io_mem, DCU_MPP_STATUS_HBLK_SUM0);
		reg_bin_val[i++] = reg_val & 0xffff;
		reg_bin_val[i++] = (reg_val >> 16) & 0xffff;

		reg_val = dcu_read_reg(io_mem, DCU_MPP_STATUS_HBLK_SUM1);
		reg_bin_val[i++] = reg_val & 0xffff;
		reg_bin_val[i++] = (reg_val >> 16) & 0xffff;

		reg_val = dcu_read_reg(io_mem, DCU_MPP_STATUS_HBLK_SUM2);
		reg_bin_val[i++] = reg_val & 0xffff;
		reg_bin_val[i++] = (reg_val >> 16) & 0xffff;

		reg_val = dcu_read_reg(io_mem, DCU_MPP_STATUS_HBLK_SUM3);
		reg_bin_val[i++] = reg_val & 0xffff;
		reg_bin_val[i++] = (reg_val >> 16) & 0xffff;
	}

	/*Determine the following (with the register values)
	1.Sum of all the bin values (curSum)
	2.The largest value found in the bins (curMaxValue)
	3.The bin number where the largest value was found (curOffset)*/
	for (i = 0; i < DCU_IPP_MAX_BINS; i++) {
		cur_bin_value = reg_bin_val[i];
		cur_sum += cur_bin_value;

		if (cur_bin_value > cur_max_value) {
			cur_offset = i;
			cur_max_value = cur_bin_value;
		}
	}

	if (DCU_IPP_DBK_STATUS_MIN_TH > cur_max_value)
		cur_confidence = 0;
	else {
		/*calculate the Confidence Level (curConfidence)*/
		cur_average = ((cur_sum - cur_max_value) /
				(DCU_IPP_MAX_BINS - 1));
		cur_confidence = (cur_max_value*10 + 5) /
					(cur_average * 10);

		/*Decrement non-zero confidence results by one(1)*/
		if (0 < cur_confidence)
			cur_confidence--;

		/*Adjust current confidence by gain*/
		cur_confidence = cur_confidence * confidence_gain;

		/*constrain confidence to be within limits*/
		if (DCU_IPP_DBK_CONFIDENCE_MAX_LEVEL < cur_confidence)
			cur_confidence = DCU_IPP_DBK_CONFIDENCE_MAX_LEVEL;
	}

	*offset = cur_offset;
	*confidence = cur_confidence;
}

void dcu_ipp_set_dct_offset(struct dcu_param_set *dcu_param_set)
{
	struct dcu_ipp_params *ipp_params;

	ipp_params = &dcu_param_set->ipp_params;
	__dcu_ipp_calc_dct_offset(dcu_param_set, false,
				&ipp_params->dct_h_offset,
				&ipp_params->dct_h_confidence);
	__dcu_ipp_calc_dct_offset(dcu_param_set, true,
				&ipp_params->dct_v_offset,
				&ipp_params->dct_v_confidence);

	ipp_params->dct_blk_ctrl = 0x0;
	ipp_params->dct_blk_ctrl |= ipp_params->dct_v_offset & 0x7;
	ipp_params->dct_blk_ctrl |=
		((ipp_params->dct_v_confidence & 0xf) << 8);
	ipp_params->dct_blk_ctrl |= ((ipp_params->dct_h_offset & 0x7) << 4);
	ipp_params->dct_blk_ctrl |=
		((ipp_params->dct_h_confidence & 0xf) << 12);
}
