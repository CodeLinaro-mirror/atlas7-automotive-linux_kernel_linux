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
#include "utils.h"

#define CONTROL_NUM 9
#define MIN_DB (-32)
#define STEP_DB 1
#define MAXV (-MIN_DB / STEP_DB)
#define MINV (-MAX_DB / STEP_DB)

static const DECLARE_TLV_DB_SCALE(bass_db_tlv, MIN_DB*100, STEP_DB*100, 0);
static const int param_min[CONTROL_NUM] = {
	0, 0, 50, 30, 0, 40, 0, 0, 1};	/* min value of control value */
static const int param_max[CONTROL_NUM] = {
	/* max value of control value */
	100, 32, 300, 300, 100, 1000, 100, 2, 2};

/* Create control interfaces */
static int bass_init(struct kasobj_op *op)
{
	struct snd_kcontrol_new *ctrl;
	char names_buf[256], *names = names_buf, *name;
	int ctl_idx = 0; /* control interface index */
	int max;
	const int *tlv;

	if (!op->db->ctrl_names.s)
		return 0;

	if (snprintf(names_buf, 256, "%s", op->db->ctrl_names.s) >= 256) {
		pr_err("KASOP(%s): control names too long!\n", op->obj.name);
		return -EINVAL;
	}

	while ((name = strsep(&names, ":;"))) {
		/* Bass+ controls */
		if (ctl_idx >= CONTROL_NUM) {
			pr_err("KASOP(%s): too many bass controls!\n",
				op->obj.name);
			return -EINVAL;
		}
		if (kcm_strcasestr(name, "Limit"))
			tlv = bass_db_tlv;
		else
			tlv = NULL;
		max = param_max[ctl_idx];
		if (max <= 0) {
			pr_err("KASOP(%s): invalid control max value, %d!\n",
				 op->obj.name, max);
			return -EINVAL;
		}
		ctrl = kasop_ctrl_single_ext_tlv(name, op, max, tlv, ctl_idx);
		kcm_register_ctrl(ctrl);
		ctl_idx++;
	}
	op->ctrl_value = kcalloc(ctl_idx, sizeof(int), GFP_KERNEL);
	op->ctrl_flag  = kcalloc(ctl_idx, sizeof(int), GFP_KERNEL);

	return 0;
}

static const struct kasop_impl bass_impl = {
	.init = bass_init,
};

/* registe Bass operator */
static int __init kasop_init_bass(void)
{
	return kcm_register_cap(CAPABILITY_ID_DBE_FULLBAND_IN_OUT, &bass_impl);
}

subsys_initcall(kasop_init_bass);
