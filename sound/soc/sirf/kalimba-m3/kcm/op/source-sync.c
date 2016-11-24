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

#define SOURCE_SYNC_CTRL_NUM (3)
#define SOURCE_SYNC_CTRL_IDX_ACTIVE_STREAM (0)
#define SOURCE_SYNC_CTRL_IDX_PURGE_FLAG (1)
#define SOURCE_SYNC_CTRL_IDX_TRANS_SAMPLES (2)

#define SOURCE_SYNC_GROUPS_MAX (24)
#define SOURCE_SYNC_CHANNELS_MAX (24)
#define SOURCE_SYNC_TRANS_SAMPLES_MAX (65535)

/* Create control interfaces */
static int source_sync_init(struct kasobj_op *op)
{
	struct snd_kcontrol_new *ctrl;
	char names_buf[256], *names = names_buf, *name;
	int ctrl_idx = 0; /* control interface index */
	int max, idx, tmp, input, ch, cnt, switch_out_num, switch_in_st, flag;

	if (!op->db->ctrl_names.s)
		return 0;

	if (snprintf(names_buf, 256, "%s", op->db->ctrl_names.s) >= 256) {
		pr_err("KASOP(%s): control names too long!\n", op->obj.name);
		return -EINVAL;
	}

	switch_out_num = op->db->param.srcsync_cfg.stream_ch[0];
	for (idx = 0, cnt = 0, flag = 0; idx < SOURCE_SYNC_CHANNELS_MAX;
			idx += switch_out_num) {
		for (ch = 0; ch < switch_out_num; ch++) {
			input = op->db->param.srcsync_cfg.input_map[ch];
			tmp = op->db->param.srcsync_cfg.input_map[idx + ch];
			if (input != tmp) {
				switch_in_st = idx / switch_out_num;
				flag = 1;
				break;
			}
		}
		if (flag)
			break;
	}

	while ((name = strsep(&names, ":;"))) {
		if (ctrl_idx >= SOURCE_SYNC_CTRL_NUM) {
			pr_err("KASOP(%s): too many controls!\n",
				op->obj.name);
			return -EINVAL;
		}
		if (kcm_strcasestr(name, "Stream"))
			max = switch_in_st;
		else if (kcm_strcasestr(name, "Samples"))
			max = SOURCE_SYNC_TRANS_SAMPLES_MAX;
		else if (kcm_strcasestr(name, "Flag"))
			max = 1;
		else {
			pr_err("KASOP(%s): unknown control '%s'!\n",
				op->obj.name, name);
			return -EINVAL;
		}

		ctrl = kasop_ctrl_single_ext_tlv(name, op, max,
			NULL, ctrl_idx);
		kcm_register_ctrl(ctrl);
		ctrl_idx++;
	}
	op->ctrl_value = kcalloc(2, sizeof(int), GFP_KERNEL);
	op->ctrl_flag  = kcalloc(2, sizeof(int), GFP_KERNEL);

	return 0;
}

static const struct kasop_impl source_sync_impl = {
	.init = source_sync_init,
};

/* register source sync operator */
static int __init kasop_init_source_sync(void)
{
	return kcm_register_cap(CAPABILITY_ID_SOURCE_SYNC, &source_sync_impl);
}

subsys_initcall(kasop_init_source_sync);
