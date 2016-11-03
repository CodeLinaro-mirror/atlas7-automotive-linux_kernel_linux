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
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include "../kasobj.h"
#include "../kasop.h"
#include "../kcm.h"
#include "../kas.h"
#include "utils.h"

#define MIN_DB		(-120)
#define STEP_DB		1
#define MAXV		(-MIN_DB / STEP_DB)
#define CHANNEL_MAX	8

static const DECLARE_TLV_DB_SCALE(vol_tlv, MIN_DB*100, STEP_DB*100, 0);
static const int chmixer_gain_max[CHANNEL_MAX] =
		/* -20 * log(input_channels) */
		{ 0, -7, -10, -13, -14, -16, -17, -19 };

/* Create control interfaces */
static int chmixer_init(struct kasobj_op *op)
{
	struct snd_kcontrol_new *ctrl;
	char names_buf[256], name_tmp[52], *names = names_buf, *name;
	int ctrl_idx = 0; /* control interface index */
	int io_num, gain_num, in, out, len, max, input_ch, output_ch;

	/* 0x00XY -> X: input channel num, Y: output channel num */
	io_num = op->db->param.chmixer_io;
	output_ch = io_num & 0x000f;
	input_ch = (io_num >> 4) & 0x000f;
	if (input_ch < 1 || output_ch < 1) {
		pr_err("KASOP(%s): invalid input(%d)/output(%d) channels !\n",
			op->obj.name, input_ch, output_ch);
		return -EINVAL;
	}
	gain_num = input_ch * output_ch;
	max = MAXV;

	if (!op->db->ctrl_names.s)
		return 0;

	if (snprintf(names_buf, 256, "%s", op->db->ctrl_names.s) >= 256) {
		pr_err("KASOP(%s): control names too long!\n", op->obj.name);
		return -EINVAL;
	}

	while ((name = strsep(&names, ":;"))) {
		if (ctrl_idx >= gain_num) {
			pr_err("KASOP(%s): too many controls!\n",
				op->obj.name);
			return -EINVAL;
		}
		if (kcm_strcasestr(name, "Gain")) {
			len = snprintf(name_tmp, 50, "%s", name);
			if (len >= 50) {
				pr_err("KASOP(%s): single ctrl too long!\n",
					op->obj.name);
				return -EINVAL;
			}
			for (out = 0; out < output_ch; out++) {
				for (in = 0; in < input_ch; in++) {
					name_tmp[len] = '0' + out;
					name_tmp[len + 1] = '0' + in;
					name_tmp[len + 2] = '\0';
					ctrl = kasop_ctrl_single_ext_tlv(
						name_tmp, op, max,
						vol_tlv, ctrl_idx);
					kcm_register_ctrl(ctrl);
					ctrl_idx++;
				}
			}
		} else {
			pr_err("KASOP(%s): unknown control '%s'!\n",
				op->obj.name, name);
			return -EINVAL;
		}
	}
	op->ctrl_value = kcalloc(2, sizeof(int), GFP_KERNEL);
	op->ctrl_flag  = kcalloc(2, sizeof(int), GFP_KERNEL);

	return 0;
}

static const struct kasop_impl chmixer_impl = {
	.init = chmixer_init,
};

/* registe channel mixer operator */
static int __init kasop_init_chmixer(void)
{
	return kcm_register_cap(CAPABILITY_ID_CHANNEL_MIXER, &chmixer_impl);
}

subsys_initcall(kasop_init_chmixer);
