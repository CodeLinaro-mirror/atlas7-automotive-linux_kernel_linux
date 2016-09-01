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

#define MIN_SAMPLES 0
#define MAX_SAMPLES 768

/* Create control interfaces */
static int delay_init(struct kasobj_op *op)
{
	struct snd_kcontrol_new *ctrl;
	char names_buf[256], *names = names_buf, *name;
	int sample_idx = 0; /* control interface index */

	if (!op->db->ctrl_names.s)
		return 0;

	if (snprintf(names_buf, 256, "%s", op->db->ctrl_names.s) >= 256) {
		pr_err("KASOP(%s): control names too long!\n", op->obj.name);
		return -EINVAL;
	}

	while ((name = strsep(&names, ":;"))) {
		if (kcm_strcasestr(name, "Delay")) {
			/* Samples control */
			if (sample_idx >= op->db->param.delay_channels) {
				pr_err("KASOP(%s): too many Sample controls!\n",
					op->obj.name);
				return -EINVAL;
			}
			ctrl = kasop_ctrl_single_ext_tlv(name, op, MAX_SAMPLES,
				NULL, sample_idx);
			kcm_register_ctrl(ctrl);
			sample_idx++;
		} else {
			pr_err("KASOP(%s): unknown control '%s'!\n",
				op->obj.name, name);
			return -EINVAL;
		}
	}
	op->ctrl_value = kcalloc(sample_idx, sizeof(int), GFP_KERNEL);
	op->ctrl_flag  = kcalloc(sample_idx, sizeof(int), GFP_KERNEL);

	return 0;
}

static const struct kasop_impl delay_impl = {
	.init = delay_init,
};

/* registe Delay operator */
static int __init kasop_init_delay(void)
{
	return kcm_register_cap(CAPABILITY_ID_DELAY, &delay_impl);
}

subsys_initcall(kasop_init_delay);
