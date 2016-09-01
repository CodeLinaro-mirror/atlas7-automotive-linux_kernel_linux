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

#define CVC_SEND_CTRL_NUM (2)
#define CVC_SEND_MODE_MAX (2)
#define CVC_SEND_UCID_MAX (2)

/* Create control interfaces */
static int cvc_send_init(struct kasobj_op *op)
{
	struct snd_kcontrol_new *ctrl;
	char names_buf[256], *names = names_buf, *name;
	int ctrl_idx = 0; /* control interface index */
	int max;

	if (!op->db->ctrl_names.s)
		return 0;

	if (snprintf(names_buf, 256, "%s", op->db->ctrl_names.s) >= 256) {
		pr_err("KASOP(%s): control names too long!\n", op->obj.name);
		return -EINVAL;
	}

	while ((name = strsep(&names, ":;"))) {
		if (ctrl_idx >= CVC_SEND_CTRL_NUM) {
			pr_err("KASOP(%s): too many controls!\n",
				op->obj.name);
			return -EINVAL;
		}
		if (kcm_strcasestr(name, "Mode"))
			max = CVC_SEND_MODE_MAX;
		else if (kcm_strcasestr(name, "UCID"))
			max = CVC_SEND_UCID_MAX;
		else {
			pr_err("KASOP(%s): unknown control '%s'!\n",
				op->obj.name, name);
			return -EINVAL;
		}

		ctrl = kasop_ctrl_single_ext_tlv(name, op, max, NULL, ctrl_idx);
		kcm_register_ctrl(ctrl);
		ctrl_idx++;
	}
	op->ctrl_value = kcalloc(ctrl_idx, sizeof(int), GFP_KERNEL);
	op->ctrl_flag  = kcalloc(ctrl_idx, sizeof(int), GFP_KERNEL);

	return 0;
}

static const struct kasop_impl cvc_send_impl = {
	.init = cvc_send_init,
};

/* registe cvc send operator */
static int __init kasop_init_cvc_send(void)
{
	return kcm_register_cap(CAPABILITY_ID_CVCHF_SEND_DUMMY, &cvc_send_impl);
}

subsys_initcall(kasop_init_cvc_send);
