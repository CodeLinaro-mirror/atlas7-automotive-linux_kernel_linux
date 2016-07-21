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

#define DCU_EE_NUM_LUMA				10
#define DCU_EE_NUM_CHROMA			5
#define DCU_EE_DEF_SHARP			50
#define DCU_EE_NUM_SHARP_LEVELS			50
#define DCU_EE_NUM_SHARP_SD2HD_LEVELS		101
#define DCU_EE_SCALING_FACTOR_SD2HD		1500
#define DCU_EE_SCALING_FACTOR_SD2HD_720P	2000

struct dcu_ee_filter {
	struct dcu_ee_luma_filter luma_filter[DCU_EE_NUM_LUMA];
	u8	luma_filter_count;
	struct dcu_ee_chroma_filter chroma_filter[DCU_EE_NUM_CHROMA];
	u8	chroma_filter_count;
};
static struct dcu_ee_filter ee_filter;

static u8 ee_softtbl[DCU_EE_NUM_SHARP_LEVELS][3] = {
	{40,	30,	14},
	{40,	30,	14},
	{40,	30,	14},
	{40,	30,	14},
	{56,	30,	6},
	{56,	30,	6},
	{56,	30,	6},
	{56,	30,	6},
	{60,	30,	4},
	{60,	30,	4},
	{60,	30,	4},
	{60,	30,	4},
	{64,	30,	2},
	{64,	30,	2},
	{64,	30,	2},
	{64,	30,	2},
	{72,	28,	0},
	{72,	28,	0},
	{72,	28,	0},
	{72,	28,	0},
	{80,	22,	2},
	{80,	22,	2},
	{80,	22,	2},
	{88,	20,	0},
	{88,	20,	0},
	{88,	20,	0},
	{92,	18,	0},
	{92,	18,	0},
	{92,	18,	0},
	{96,	16,	0},
	{96,	16,	0},
	{96,	16,	0},
	{100,	14,	0},
	{100,	14,	0},
	{100,	14,	0},
	{104,	12,	0},
	{104,	12,	0},
	{104,	12,	0},
	{108,	10,	0},
	{108,	10,	0},
	{108,	10,	0},
	{112,	8,	0},
	{112,	8,	0},
	{112,	8,	0},
	{116,	6,	0},
	{116,	6,	0},
	{116,	6,	0},
	{128,	0,	0},
	{128,	0,	0},
	{128,	0,	0},
};

static u8 ee_sharp_gaintbl[DCU_EE_NUM_SHARP_LEVELS][DCU_EE_LUMA_NUM_GAINS] = {
	{0x41,	0x41,	0x41,	0x41,	0x41,	0x41,	0x41,	0x41},
	{0x42,	0x42,	0x42,	0x42,	0x42,	0x42,	0x42,	0x42},
	{0x43,	0x43,	0x43,	0x43,	0x43,	0x43,	0x43,	0x43},
	{0x44,	0x44,	0x44,	0x44,	0x44,	0x44,	0x44,	0x44},
	{0x45,	0x45,	0x45,	0x45,	0x45,	0x45,	0x45,	0x45},
	{0x46,	0x46,	0x46,	0x46,	0x46,	0x46,	0x46,	0x46},
	{0x47,	0x47,	0x47,	0x47,	0x47,	0x47,	0x47,	0x47},
	{0x48,	0x48,	0x48,	0x48,	0x48,	0x48,	0x48,	0x48},
	{0x49,	0x49,	0x49,	0x49,	0x49,	0x49,	0x49,	0x49},
	{0x4A,	0x4A,	0x4A,	0x4A,	0x4A,	0x4A,	0x4A,	0x4A},
	{0x4B,	0x4B,	0x4B,	0x4B,	0x4B,	0x4B,	0x4B,	0x4B},
	{0x4C,	0x4C,	0x4C,	0x4C,	0x4C,	0x4C,	0x4C,	0x4C},
	{0x4D,	0x4D,	0x4D,	0x4D,	0x4D,	0x4D,	0x4D,	0x4D},
	{0x4E,	0x4E,	0x4E,	0x4E,	0x4E,	0x4E,	0x4E,	0x4E},
	{0x4F,	0x4F,	0x4F,	0x4F,	0x4F,	0x4F,	0x4F,	0x4F},
	{0x38,	0x38,	0x38,	0x38,	0x38,	0x38,	0x38,	0x38},
	{0x39,	0x39,	0x39,	0x39,	0x39,	0x39,	0x39,	0x39},
	{0x39,	0x39,	0x39,	0x39,	0x39,	0x39,	0x39,	0x39},
	{0x3A,	0x3A,	0x3A,	0x3A,	0x3A,	0x3A,	0x3A,	0x3A},
	{0x3A,	0x3A,	0x3A,	0x3A,	0x3A,	0x3A,	0x3A,	0x3A},
	{0x3B,	0x3B,	0x3B,	0x3B,	0x3B,	0x3B,	0x3B,	0x3B},
	{0x3B,	0x3B,	0x3B,	0x3B,	0x3B,	0x3B,	0x3B,	0x3B},
	{0x3C,	0x3C,	0x3C,	0x3C,	0x3C,	0x3C,	0x3C,	0x3C},
	{0x3C,	0x3C,	0x3C,	0x3C,	0x3C,	0x3C,	0x3C,	0x3C},
	{0x3D,	0x3D,	0x3D,	0x3D,	0x3D,	0x3D,	0x3D,	0x3D},
	{0x3D,	0x3D,	0x3D,	0x3D,	0x3D,	0x3D,	0x3D,	0x3D},
	{0x3E,	0x3E,	0x3E,	0x3E,	0x3E,	0x3E,	0x3E,	0x3E},
	{0x3E,	0x3E,	0x3E,	0x3E,	0x3E,	0x3E,	0x3E,	0x3E},
	{0x3F,	0x3F,	0x3F,	0x3F,	0x3F,	0x3F,	0x3F,	0x3F},
	{0x3F,	0x3F,	0x3F,	0x3F,	0x3F,	0x3F,	0x3F,	0x3F},
	{0x3F,	0x3F,	0x3F,	0x3F,	0x3F,	0x3F,	0x3F,	0x3F},
	{0x28,	0x28,	0x28,	0x28,	0x28,	0x28,	0x28,	0x28},
	{0x28,	0x28,	0x28,	0x28,	0x28,	0x28,	0x28,	0x28},
	{0x28,	0x28,	0x28,	0x28,	0x28,	0x28,	0x28,	0x28},
	{0x28,	0x28,	0x28,	0x28,	0x28,	0x28,	0x28,	0x28},
	{0x29,	0x29,	0x29,	0x29,	0x29,	0x29,	0x29,	0x29},
	{0x29,	0x29,	0x29,	0x29,	0x29,	0x29,	0x29,	0x29},
	{0x29,	0x29,	0x29,	0x29,	0x29,	0x29,	0x29,	0x29},
	{0x29,	0x29,	0x29,	0x29,	0x29,	0x29,	0x29,	0x29},
	{0x2A,	0x2A,	0x2A,	0x2A,	0x2A,	0x2A,	0x2A,	0x2A},
	{0x2A,	0x2A,	0x2A,	0x2A,	0x2A,	0x2A,	0x2A,	0x2A},
	{0x2A,	0x2A,	0x2A,	0x2A,	0x2A,	0x2A,	0x2A,	0x2A},
	{0x2A,	0x2A,	0x2A,	0x2A,	0x2A,	0x2A,	0x2A,	0x2A},
	{0x2B,	0x2B,	0x2B,	0x2B,	0x2B,	0x2B,	0x2B,	0x2B},
	{0x2B,	0x2B,	0x2B,	0x2B,	0x2B,	0x2B,	0x2B,	0x2B},
	{0x2B,	0x2B,	0x2B,	0x2B,	0x2B,	0x2B,	0x2B,	0x2B},
	{0x2B,	0x2B,	0x2B,	0x2B,	0x2B,	0x2B,	0x2B,	0x2B},
	{0x2B,	0x2B,	0x2B,	0x2B,	0x2B,	0x2B,	0x2B,	0x2B},
	{0x2B,	0x2B,	0x2B,	0x2B,	0x2B,	0x2B,	0x2B,	0x2B},
	{0x2B,	0x2B,	0x2B,	0x2B,	0x2B,	0x2B,	0x2B,	0x2B},
};

static u8 ee_sharpsd2hd_1080ip[DCU_EE_NUM_SHARP_SD2HD_LEVELS] = {
	0,	2,	4,	6,	7,
	9,	10,	11,	13,	14,
	15,	17,	18,	19,	20,
	21,	23,	24,	25,	26,
	27,	28,	29,	30,	31,
	32,	34,	35,	36,	37,
	38,	39,	40,	41,	42,
	43,	44,	45,	46,	47,
	48,	49,	49,	50,	51,
	52,	53,	54,	55,	56,
	57,	58,	59,	60,	61,
	61,	62,	63,	64,	65,
	66,	67,	68,	69,	69,
	70,	71,	72,	73,	74,
	75,	76,	76,	77,	78,
	79,	80,	81,	81,	82,
	83,	84,	85,	86,	86,
	87,	88,	89,	90,	91,
	91,	92,	93,	94,	95,
	95,	96,	97,	98,	99,
	100
};

static u8 ee_sharpsd2hd_720p[DCU_EE_NUM_SHARP_SD2HD_LEVELS] = {
	0,	1,	3,	5,	6,
	7,	9,	10,	11,	12,
	14,	15,	16,	17,	18,
	19,	21,	22,	23,	24,
	25,	26,	27,	28,	29,
	30,	31,	32,	33,	34,
	35,	36,	37,	38,	39,
	40,	41,	42,	43,	44,
	45,	46,	47,	48,	49,
	50,	51,	52,	53,	54,
	55,	56,	57,	58,	59,
	60,	61,	62,	62,	63,
	64,	65,	66,	67,	68,
	69,	70,	71,	72,	72,
	73,	74,	75,	76,	77,
	78,	79,	80,	80,	81,
	82,	83,	84,	85,	86,
	87,	87,	88,	89,	90,
	91,	92,	93,	94,	94,
	95,	96,	97,	98,	99,
	100
};

void dcu_ee_init(struct dcu_param_set *dcu_param_set)
{
	u8	i;
	struct dcu_ee_luma_filter luma_filter;
	struct dcu_ee_chroma_filter chroma_filter;

	ee_filter.luma_filter_count = 0;
	ee_filter.chroma_filter_count = 0;

	/*luma filters 0 to 1 - Bypass*/
	memset(&luma_filter, 0, sizeof(struct dcu_ee_luma_filter));

	dcu_ee_add_luma_filter(&luma_filter);
	dcu_ee_add_luma_filter(&luma_filter);

	/*luma filter 2 to 3 - Peaking, fixed coefficients, gains are 1 */
	luma_filter.use_lowpass = false;
	luma_filter.use_brightness = false;
	luma_filter.coring_mode = 0x0;
	luma_filter.coring_th = 0x0;

	for (i = 0; i < DCU_EE_LUMA_NUM_GAINS; i++)
		luma_filter.gains[i] = 0x38;

	luma_filter.peaking_mode = 2;
	luma_filter.coef1_5 = 0;
	luma_filter.coef2_4 = -32;
	luma_filter.coef3 = 64;

	dcu_ee_add_luma_filter(&luma_filter);

	luma_filter.peaking_mode = 3;
	luma_filter.coef1_5 = -32;
	luma_filter.coef2_4 = 0;
	luma_filter.coef3 = 64;

	dcu_ee_add_luma_filter(&luma_filter);

	/*luma filter 4 - Auto, according to scaling factor and sharpness*/
	dcu_ee_add_luma_filter(&luma_filter);

	/* Chroma filters 0 - Bypass*/
	memset(&chroma_filter, 0, sizeof(struct dcu_ee_chroma_filter));

	dcu_ee_add_chroma_filter(&chroma_filter);

	/* Chroma filters 1 - Auto, according to scaling factor and sharpness*/
	dcu_ee_add_chroma_filter(&chroma_filter);
}

bool dcu_ee_add_luma_filter(struct dcu_ee_luma_filter *luma_filter)
{
	struct dcu_ee_luma_filter *new_luma_filter;

	if ((NULL == luma_filter) ||
		(DCU_EE_NUM_LUMA <= ee_filter.luma_filter_count)) {
		dcu_err("Failed at %s, line %d\n", __func__, __LINE__);
		return false;
	}

	new_luma_filter = &ee_filter.luma_filter[ee_filter.luma_filter_count];
	*new_luma_filter = *luma_filter;
	ee_filter.luma_filter_count++;

	return true;
}

bool dcu_ee_add_chroma_filter(struct dcu_ee_chroma_filter *chroma_filter)
{
	if ((NULL == chroma_filter) ||
		(DCU_EE_NUM_CHROMA <= ee_filter.chroma_filter_count)) {
		dcu_err("Failed at %s, line %d\n", __func__, __LINE__);
		return false;
	}

	ee_filter.chroma_filter[ee_filter.chroma_filter_count] =
							*chroma_filter;
	ee_filter.chroma_filter_count++;

	return true;
}

static void __dcu_ee_calc(struct dcu_param_set *dcu_param_set,
				bool bluma_filter)
{
	u32 i;
	u8 *gain;
	u8 *filter;
	u32 sharp_level = 0;
	struct dcu_ee_params *ee_params;
	struct dcu_ee_luma_filter *luma_filter;
	struct dcu_ee_chroma_filter *chroma_filter;

	ee_params = &dcu_param_set->ee_params;
	if (bluma_filter) {
		luma_filter =
			&ee_filter.luma_filter[ee_params->curr_luma_filter];

		if (DCU_EE_LUMA_AUTO_INDEX == ee_params->curr_luma_filter) {
			if (DCU_EE_SCALING_FACTOR_SD2HD <=
					ee_params->scaling_factor) {
				if (DCU_EE_SCALING_FACTOR_SD2HD_720P >=
					ee_params->scaling_factor)
					sharp_level =
			ee_sharpsd2hd_720p[ee_params->sharpness_level];
				else
					sharp_level =
			ee_sharpsd2hd_1080ip[ee_params->sharpness_level];
			} else
				sharp_level = ee_params->sharpness_level;

			if (DCU_EE_DEF_SHARP > sharp_level) {
				filter = &ee_softtbl[sharp_level][0];
				luma_filter->coef1_5 = filter[2];
				luma_filter->coef2_4 = filter[1];
				luma_filter->coef3 = filter[0];

				luma_filter->use_lowpass = true;
				luma_filter->peaking_mode = 0;
			} else if (DCU_EE_DEF_SHARP == sharp_level) {
				luma_filter->use_lowpass = false;
				luma_filter->peaking_mode = 0;
			} else {
				gain = &ee_sharp_gaintbl[sharp_level -
					DCU_EE_DEF_SHARP - 1][0];
				luma_filter->use_lowpass = false;
				luma_filter->peaking_mode = 2;
				luma_filter->coef1_5 = 0;
				luma_filter->coef2_4 = -32;
				luma_filter->coef3 = 64;
				/*Coring circuit mode*/
				luma_filter->coring_mode = 0x1;
				/*Coring circuit threshold*/
				luma_filter->coring_th = 3;
				/*Peaking gains*/
				luma_filter->use_brightness = false;
				for (i = 0; i < DCU_EE_LUMA_NUM_GAINS; i++)
					luma_filter->gains[i] = gain[i];
			}
		}

		ee_params->ee_mode_y |= luma_filter->use_lowpass & 0x1;
		ee_params->ee_mode_y |=
			((luma_filter->peaking_mode & 0x3) << 4);
		ee_params->ee_mode_y |=
			((luma_filter->use_brightness & 0x1) << 8);
		ee_params->ee_mode_y |=
			((luma_filter->coring_mode & 0x3) << 12);

		ee_params->coef_th_y |= luma_filter->coef1_5 & 0xff;
		ee_params->coef_th_y |= ((luma_filter->coef2_4 & 0xff) << 8);
		ee_params->coef_th_y |= ((luma_filter->coef3 & 0xff) << 16);
		ee_params->coef_th_y |=
			((luma_filter->coring_th & 0x7f) << 24);

		ee_params->peak_gains_14 |= luma_filter->gains[0] & 0x7f;
		ee_params->peak_gains_14 |=
			((luma_filter->gains[1] & 0x7f) << 8);
		ee_params->peak_gains_14 |=
			((luma_filter->gains[2] & 0x7f) << 16);
		ee_params->peak_gains_14 |=
			((luma_filter->gains[3] & 0x7f) << 24);

		ee_params->peak_gains_58 |= luma_filter->gains[4] & 0x7f;
		ee_params->peak_gains_58 |=
			((luma_filter->gains[5] & 0x7f) << 8);
		ee_params->peak_gains_58 |=
			((luma_filter->gains[6] & 0x7f) << 16);
		ee_params->peak_gains_58 |=
			((luma_filter->gains[7] & 0x7f) << 24);
	} else {
		chroma_filter =
		&ee_filter.chroma_filter[ee_params->curr_chroma_filter];

		ee_params->ee_mode_c |= chroma_filter->use_filter & 0x3;
		ee_params->ee_mode_c |=
			((chroma_filter->coring_mode & 0x3) << 4);

		ee_params->coef_th_c |= chroma_filter->coef1_5 & 0xff;
		ee_params->coef_th_c |= ((chroma_filter->coef2_4 & 0xff) << 8);
		ee_params->coef_th_c |= ((chroma_filter->coef3 & 0xff) << 16);
		ee_params->coef_th_c |=
			((chroma_filter->coring_th & 0x7f) << 24);
	}
}

bool dcu_ee_set_luma_filter(struct dcu_param_set *dcu_param_set,
				u8 filter_idx)
{
	struct dcu_ee_params *ee_params;

	ee_params = &dcu_param_set->ee_params;
	if (ee_filter.luma_filter_count <= filter_idx) {
		dcu_err("Failed at %s, line %d\n", __func__, __LINE__);
		return false;
	}

	ee_params->curr_luma_filter = filter_idx;
	__dcu_ee_calc(dcu_param_set, true);

	return true;
}

bool dcu_ee_set_chroma_filter(struct dcu_param_set *dcu_param_set,
				u8 filter_idx)
{
	struct dcu_ee_params *ee_params;

	ee_params = &dcu_param_set->ee_params;
	if (ee_filter.chroma_filter_count <= filter_idx) {
		dcu_err("Failed at %s, line %d\n", __func__, __LINE__);
		return false;
	}

	ee_params->curr_chroma_filter = filter_idx;
	__dcu_ee_calc(dcu_param_set, false);

	return true;
}

bool dcu_ee_set_sharpness(struct dcu_param_set *dcu_param_set,
				u32 sharpness_level)
{
	struct dcu_ee_params *ee_params;

	ee_params = &dcu_param_set->ee_params;
	if (ee_params->sharpness_level != sharpness_level) {
		ee_params->sharpness_level = sharpness_level;
		__dcu_ee_calc(dcu_param_set, true);
	}

	return true;
}

bool dcu_ee_set_scale_factor(struct dcu_param_set *dcu_param,
			u32 src_height, u32 dst_height)
{
	u32	scale_factor;
	struct dcu_ee_params *ee_params;

	ee_params = &dcu_param->ee_params;
	if (0 == src_height) {
		dcu_err("Failed at %s, line %d", __func__, __LINE__);
		return false;
	}

	scale_factor = (dst_height * 1000) / src_height;
	if (ee_params->scaling_factor != scale_factor) {
		ee_params->scaling_factor = scale_factor;
		__dcu_ee_calc(dcu_param, true);
	}

	return true;
}
