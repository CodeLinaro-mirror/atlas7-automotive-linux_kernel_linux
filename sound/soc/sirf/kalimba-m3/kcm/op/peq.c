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

/*
 * The squence number of peq controls
 * defined in db-default/op.c
 * control names	   control index
 * band 1~10 gain	   0 ~ 9
 * band 1~10 FC		   10~ 19
 * band num		   20
 * core type		   21
 * master gain		   22
 * switch mode		   23
 * ucid			   24
 */

#define PEQ_CNTL_BAND1_GAIN 0
#define PEQ_CNTL_BAND10_GAIN 9
#define PEQ_CNTL_BAND1_FC 10
#define PEQ_CNTL_BAND10_FC 19
#define PEQ_CNTL_BANDS_NUM 20
#define PEQ_CNTL_CORE_TYPE 21
#define PEQ_CNTL_MASTER_GAIN 22
#define PEQ_CNTL_SWITCH_MODE 23
#define PEQ_CNTL_UCID 24

#define PEQ_CONTROL_NUM 25
#define PEQ_DEFAULT_MSG_LEN 69
#define PEQ_MIN_DB (-60)
#define PEQ_MAX_DB 20
#define PEQ_STEP_DB 1
#define PEQ_MAX_GAIN (PEQ_MAX_DB - PEQ_MIN_DB)
#define PEQ_BANDS 10

#define PEQ_DEFAULT_UCID 0x01	/* default peq UCID */
#define PEQ_CUST_UCID_MAX 0x0a
#define PEQ_MAX_UCID PEQ_CUST_UCID_MAX

static const DECLARE_TLV_DB_SCALE(peq_db_tlv,
	PEQ_MIN_DB*100, PEQ_STEP_DB*100, 0);

/* Create control interfaces */
static int peq_init(struct kasobj_op *op)
{
	struct snd_kcontrol_new *ctrl;
	char names_buf[512], *names = names_buf, *name;
	int ctl_idx = 0; /* control interface index */
	int max;
	const int *tlv = NULL;

	if (!op->db->ctrl_names.s)
		return 0;

	if (snprintf(names_buf, 512, "%s", op->db->ctrl_names.s) >= 512) {
		pr_err("KASOP(%s): control names too long!\n", op->obj.name);
		return -EINVAL;
	}

	while ((name = strsep(&names, ":;"))) {
			/* PEQ controls */
		if (ctl_idx >= PEQ_CONTROL_NUM) {
			pr_err("KASOP(%s): too many PEQ controls!\n",
				op->obj.name);
			break;
		}
		if (kcm_strcasestr(name, "Gain")) {
			max = PEQ_MAX_GAIN;
			tlv = peq_db_tlv;
		} else if (kcm_strcasestr(name, "FC"))
			max = 24000;
		else if (kcm_strcasestr(name, "Mode"))
			max = 2;
		else if (kcm_strcasestr(name, "Type"))
			max = 2;
		else if (kcm_strcasestr(name, "Num"))
			max = 10;
		else if (kcm_strcasestr(name, "UCID"))
			max = PEQ_MAX_UCID;
		else {
			pr_err("KASOP(%s): Invalid control !\n", op->obj.name);
			continue;
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

static const struct kasop_impl peq_impl = {
	.init = peq_init,
};

/* registe Bass operator */
static int __init kasop_init_peq(void)
{
	return kcm_register_cap(CAPABILITY_ID_PEQ, &peq_impl);
}

subsys_initcall(kasop_init_peq);
