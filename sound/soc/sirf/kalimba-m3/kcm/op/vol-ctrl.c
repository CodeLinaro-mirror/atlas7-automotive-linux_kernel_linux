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

/* the control sequence */
#define VOLCTRL_CTRL_FRONT_LEFT 0
#define VOLCTRL_CTRL_FRONT_RIGHT 1
#define VOLCTRL_CTRL_REAR_LEFT 2
#define VOLCTRL_CTRL_REAR_RIGHT 3
#define VOLCTRL_CTRL_MASTER_GAIN 4
#define VOLCTRL_CTRL_MASTER_MUTE 5
#define VOLCTRL_CONTROL_NUM 6

#define VOLCTRL_MIN_DB (-120)
#define VOLCTRL_MAX_DB 9
#define VOLCTRL_STEP_DB 1
#define VOLCTRL_MAX_GAIN (VOLCTRL_MAX_DB - VOLCTRL_MIN_DB)


static const DECLARE_TLV_DB_SCALE(volctrl_db_tlv,
	VOLCTRL_MIN_DB*100, VOLCTRL_STEP_DB*100, 0);

/* Create control interfaces */
static int volctrl_init(struct kasobj_op *op)
{
	struct snd_kcontrol_new *ctrl;
	char names_buf[256], *names = names_buf, *name;
	int ctl_idx = 0; /* control interface index */
	int max;
	const int *tlv = NULL;

	if (!op->db->ctrl_names.s)
		return 0;

	if (snprintf(names_buf, 256, "%s", op->db->ctrl_names.s) >= 256) {
		pr_err("KASOP(%s): control names too long!\n", op->obj.name);
		return -EINVAL;
	}

	while ((name = strsep(&names, ":;"))) {
			/* volume controls */
		if (ctl_idx >= VOLCTRL_CONTROL_NUM) {
			pr_err("KASOP(%s): too many volume controls!\n",
				op->obj.name);
			break;
		}
		if (kcm_strcasestr(name, "Mute")) {
			max = 1;
		} else {
			max = VOLCTRL_MAX_GAIN;
			tlv = volctrl_db_tlv;
		}
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

static const struct kasop_impl volctrl_impl = {
	.init = volctrl_init,
};

/* registe volume control operator */
static int __init kasop_init_volctrl(void)
{
	return kcm_register_cap(CAPABILITY_ID_VOLUME_CONTROL, &volctrl_impl);
}

subsys_initcall(kasop_init_volctrl);
