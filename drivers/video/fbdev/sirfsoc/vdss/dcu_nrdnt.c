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

void dcu_nrdnt_init(struct dcu_param_set *dcu_param_set)
{
	dcu_nrdnt_reset(dcu_param_set);
}

void dcu_nrdnt_reset(struct dcu_param_set *dcu_param_set)
{
	void *io_mem = dcu_param_set->core_iomem;

	dcu_write_reg(io_mem, DCU_NRDNT_SOFT_RESET, 0x27);
	dcu_write_reg(io_mem, DCU_NRDNT_SOFT_RESET, 0x0);
	dcu_write_reg(io_mem, DCU_NRDNT_INPUT_FORMAT, 0x7);
	dcu_write_reg(io_mem, DCU_NRDNT_HORIZONTAL_EDGE_DETECT, 0x1);
	dcu_write_reg(io_mem, DCU_NRDNT_NOISE_LEVEL_SOURCE, 0x12);
	dcu_write_reg(io_mem, DCU_NRDNT_IMPLUSE_WINDOW_SEL, 0x1);
	dcu_write_reg(io_mem, DCU_NRDNT_LAI_CEIL, 0xa);
	dcu_write_reg(io_mem, DCU_NRDNT_LAI_VAR_FLOOR, 0x1);
	dcu_write_reg(io_mem, DCU_NRDNT_LAI_VAR_FUNC, 0x1c);
	dcu_write_reg(io_mem, DCU_NRDNT_LAI_VIOLATE, 0X27);
}

void dcu_nrdnt_set_deint_mode(struct dcu_param_set *dcu_param_set,
	u32 deint_mode, u32 luma_weave_mode, u32 chroma_weave_mode)
{
	struct dcu_nrdnt_params *nrdnt_params;

	nrdnt_params = &dcu_param_set->nrdnt_params;
	nrdnt_params->deint_mode |= deint_mode & 0xf;
	nrdnt_params->deint_mode |= ((luma_weave_mode & 0x3) << 4);
	nrdnt_params->deint_mode |= ((chroma_weave_mode & 0x3) << 6);
}

void dcu_nrdnt_set_lowangle_interpmode(struct dcu_param_set *dcu_param_set,
			u32 interp_mode, u32 interp_force,
			u32 interp_relaxed, u32 interp_blend_halfpix)
{
	struct dcu_nrdnt_params *nrdnt_params;

	nrdnt_params = &dcu_param_set->nrdnt_params;
	nrdnt_params->interp_ctrl |= interp_mode & 0x1f;
	nrdnt_params->interp_ctrl |= ((interp_force & 0x1) << 5);
	nrdnt_params->interp_ctrl |= ((interp_relaxed & 0x1) << 6);
	nrdnt_params->interp_ctrl |= ((interp_blend_halfpix & 0x1) << 7);
}

void dcu_nrdnt_set_lowangle_interpv90mode(struct dcu_param_set *dcu_param_set,
			u32 interp_gain, u32 interp_strong,
			u32 interp_blk_repeat, u32 interp_blend_repeat)
{
	struct dcu_nrdnt_params *nrdnt_params;

	nrdnt_params = &dcu_param_set->nrdnt_params;
	nrdnt_params->interp_v90_ctrl |= interp_gain & 0xf;
	nrdnt_params->interp_v90_ctrl |= ((interp_strong & 0x1) << 4);
	nrdnt_params->interp_v90_ctrl |= ((interp_blk_repeat & 0x1) << 5);
	nrdnt_params->interp_v90_ctrl |= ((interp_blend_repeat & 0x1) << 6);
}

void dcu_nrdnt_set_mode(struct dcu_param_set *dcu_param_set,
		u32 field_mdet_luma, u32 frame_mdet_luma,
		u32 frame_mdet_chroma, u32 frame_mdet_chromakadap)
{
	struct dcu_nrdnt_params *nrdnt_params;

	nrdnt_params = &dcu_param_set->nrdnt_params;
	nrdnt_params->luma_mdet_mode |= frame_mdet_luma & 0x3;
	nrdnt_params->luma_mdet_mode |= ((field_mdet_luma & 0x3) << 2);
	nrdnt_params->chroma_mdet_mode |= frame_mdet_chroma & 0x3;
	nrdnt_params->chroma_mdet_mode |=
			((frame_mdet_chromakadap & 0x1) << 7);
}

void dcu_nrdnt_set_frame_mode(struct dcu_param_set *dcu_param_set,
				bool enable_5field, u32 mad_adap,
				u32 mad_5field_gain, u32 k_mode)
{
	struct dcu_nrdnt_params *nrdnt_params;

	nrdnt_params = &dcu_param_set->nrdnt_params;
	nrdnt_params->frame_mdet_mode |= enable_5field & 0x1;
	nrdnt_params->frame_mdet_mode |= ((mad_adap & 0x3) << 1);
	nrdnt_params->frame_mdet_mode |= ((mad_5field_gain & 0xf) << 3);
	nrdnt_params->frame_mdet_mode |= ((k_mode & 0x1) << 7);
}

void dcu_nrdnt_set_frame_mad(struct dcu_param_set *dcu_param_set,
	bool mad_blendth, u32 mad_blendres, u32 mad_gain, u32 mad_gain_core,
	u32 mad_gain_core2, u32 mad_gain_coreth, u32 mad_gain_core2th)
{
	struct dcu_nrdnt_params *nrdnt_params;

	nrdnt_params = &dcu_param_set->nrdnt_params;
	nrdnt_params->frame_mdet_blendth = mad_blendth & 0xff;
	nrdnt_params->frame_mdet_blendres = mad_blendres & 0xff;
	nrdnt_params->frame_mdet_gain = mad_gain & 0xff;
	nrdnt_params->frame_mdet_gaincore = mad_gain_core & 0xff;
	nrdnt_params->frame_mdet_gaincore2 = mad_gain_core2 & 0xff;
	nrdnt_params->frame_mdet_gaincoreth = mad_gain_coreth & 0xff;
	nrdnt_params->frame_mdet_gaincore2th = mad_gain_core2th & 0xff;
}

void dcu_nrdnt_set_frame_field(struct dcu_param_set *dcu_param_set,
		u32 field_gain, u32 field_core_gain, u32 field_core_th)
{
	struct dcu_nrdnt_params *nrdnt_params;

	nrdnt_params = &dcu_param_set->nrdnt_params;
	nrdnt_params->frame_mdet_fieldgain = field_gain & 0xff;
	nrdnt_params->frame_mdet_fieldcoregain = field_core_gain & 0xff;
	nrdnt_params->frame_mdet_fieldcoreth = field_core_th & 0x1f;
}

void dcu_nrdnt_set_frame_recursive(struct dcu_param_set *dcu_param_set,
	bool enable_recursive, u32 recursive_accth_mode, u32 recursive_accth,
	u32 recursive_frame_mult, u32 recursive_field_div)
{
	struct dcu_nrdnt_params *nrdnt_params;

	nrdnt_params = &dcu_param_set->nrdnt_params;
	nrdnt_params->frame_mdet_recursive |= enable_recursive & 0x1;
	nrdnt_params->frame_mdet_recursive |=
			((recursive_accth_mode & 0x1) << 1);
	nrdnt_params->frame_mdet_recursive |= ((recursive_accth & 0x1f) << 2);

	nrdnt_params->frame_mdet_recursivegain |=
			((recursive_frame_mult & 0xf) << 4);
	nrdnt_params->frame_mdet_recursivegain |= recursive_field_div & 0xf;
}

void dcu_nrdnt_set_frame_th(struct dcu_param_set *dcu_param_set,
		u32 frame_mdet_tha, u32 frame_mdet_thb, u32 frame_mdet_thc)
{
	struct dcu_nrdnt_params *nrdnt_params;

	nrdnt_params = &dcu_param_set->nrdnt_params;
	nrdnt_params->frame_mdet_tha = frame_mdet_tha & 0xff;
	nrdnt_params->frame_mdet_thb = frame_mdet_thb & 0xff;
	nrdnt_params->frame_mdet_thc = frame_mdet_thc & 0xff;
}

void dcu_nrdnt_set_field_th(struct dcu_param_set *dcu_param_set,
	u32 field_mdet_tha, u32 field_mdet_thb, u32 field_mdet_thc,
	u32 field_mdet_adap, u32 field_mdet_gain)
{
	struct dcu_nrdnt_params *nrdnt_params;

	nrdnt_params = &dcu_param_set->nrdnt_params;
	nrdnt_params->field_mdet_tha |= ((field_mdet_adap & 0x1) << 7);
	nrdnt_params->field_mdet_tha |= field_mdet_tha & 0x7f;
	nrdnt_params->field_mdet_thb = field_mdet_thb & 0xff;
	nrdnt_params->field_mdet_thc = field_mdet_thc & 0xff;
	nrdnt_params->field_mdet_gain = field_mdet_gain & 0xff;
}

void dcu_nrdnt_set_field_automode(struct dcu_param_set *dcu_param_set,
			u32 field_mdet_autodisen, u32 field_mdet_autoth)
{
	struct dcu_nrdnt_params *nrdnt_params;

	nrdnt_params = &dcu_param_set->nrdnt_params;
	nrdnt_params->field_mdet_autodislsb |= (field_mdet_autodisen & 0x1);
	nrdnt_params->field_mdet_autodislsb |=
			(((field_mdet_autoth>>1) & 0x7f) << 1);
	nrdnt_params->field_mdet_autodismsb |=
			((field_mdet_autoth >> 8) & 0xff);

	nrdnt_params->field_mdet_autodis = field_mdet_autoth;
}

void dcu_nrdnt_set_k_history(struct dcu_param_set *dcu_param_set,
	u32 k_frame_on, u32 k_frame_mode, u32 k_frame_noise, u32 k_frame_count)
{
	struct dcu_nrdnt_params *nrdnt_params;

	nrdnt_params = &dcu_param_set->nrdnt_params;
	nrdnt_params->k_history_det |= k_frame_on & 0x1;
	nrdnt_params->k_history_det |= ((k_frame_mode & 0x1) << 1);
	nrdnt_params->k_history_det |= ((k_frame_noise & 0x3) << 2);
	nrdnt_params->k_history_det |= ((k_frame_count & 0xf) << 4);
}

void dcu_nrdnt_set_th_scale(struct dcu_param_set *dcu_param_set,
			bool hd_src, u32 h_size, u32 v_size)
{
	u32 scale_ratio, temp_val;
	struct dcu_nrdnt_params *nrdnt_params;

	nrdnt_params = &dcu_param_set->nrdnt_params;
	scale_ratio = ((h_size * v_size) << DCU_NRDNT_SCALE_SHIFT) /
			(hd_src ? VIDEO_HD : VIDEO_SD);

	temp_val = DCU_NRDNT_TH_SCALE(nrdnt_params->field_mdet_autodis,
					scale_ratio);
	nrdnt_params->field_mdet_autodislsb |= (((temp_val >> 1) & 0x7f) << 1);
	nrdnt_params->field_mdet_autodismsb |= (temp_val >> 8) & 0xff;
}
