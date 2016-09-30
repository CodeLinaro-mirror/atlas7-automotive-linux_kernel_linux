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

#define PASSTHR_CTRL_ID_GAIN 0
#define PASSTHR_CTRL_ID_MUTE 1

#define MIN_DB	(-120)
#define STEP_DB	1
#define MAXV	(-MIN_DB / STEP_DB)

static const DECLARE_TLV_DB_SCALE(vol_tlv, MIN_DB*100, STEP_DB*100, 0);

/* Create control interfaces */
static int passthr_init(struct kasobj_op *op)
{
	char names_buf[256], *names = names_buf, *name;
	struct snd_kcontrol_new *ctrl;
	int ctrl_id = 0;

	if (!op->db->ctrl_names.s)
		return 0;

	if (snprintf(names_buf, 256, "%s", op->db->ctrl_names.s) >= 256) {
		pr_err("KASOP(%s): control names too long!\n", op->obj.name);
		return -EINVAL;
	}

	while ((name = strsep(&names, ":;"))) {
		if (kcm_strcasestr(name, "Pregain")) {
			/* Volume control */
			ctrl = kasop_ctrl_single_ext_tlv(name, op, MAXV,
					vol_tlv, ctrl_id);
			kcm_register_ctrl(ctrl);
			ctrl_id++;
		} else if (kcm_strcasestr(name, "Premute")) {
			/* Mute control */
			ctrl = kasop_ctrl_single_ext_tlv(name, op, 1,
					NULL, ctrl_id);
			kcm_register_ctrl(ctrl);
			ctrl_id++;
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

static struct kasop_impl passthr_impl = {
	.init = passthr_init,
};

static int __init kasop_init_passthr(void)
{
	return kcm_register_cap(CAPABILITY_ID_BASIC_PASSTHROUGH, &passthr_impl);
}

/* Must be earlier than kcm driver init() */
subsys_initcall(kasop_init_passthr);
