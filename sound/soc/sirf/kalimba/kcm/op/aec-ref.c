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
#include "../../dsp.h"
#include "utils.h"

/* Called after the operator is created */
static int aec_ref_create(struct kasobj_op *op,
	const struct kasobj_param *param)
{
	u16 sample_rate[2];
	u16 aec_ref_ucid = 4; /* stable user case ID */
	int ret;

	if (!op->db->rate)
		pr_err("KASOBJ(%s): sample rate invalid(%d)!\n",
			op->obj.name, op->db->rate);
	sample_rate[0] = op->db->rate;
	sample_rate[1] = 16000; /* wide band */
	ret = kalimba_operator_message(op->op_id, AEC_REF_SET_SAMPLE_RATES,
		2, sample_rate, NULL, NULL, __kcm_resp);
	if (ret) {
		pr_err("KASOBJ(%s): set sample rate failed(%d)!\n",
			op->obj.name, ret);
		return ret;
	}
	ret = kalimba_operator_message(op->op_id, OPERATOR_MSG_SET_UCID,
		1, &aec_ref_ucid, NULL, NULL, __kcm_resp);
	if (ret) {
		pr_err("KASOBJ(%s): set UCID failed(%d)!\n", op->obj.name, ret);
		return ret;
	}

	return 0;
}

static const struct kasop_impl aec_ref_impl = {
	.create = aec_ref_create,
};

/* registe AEC-Ref operator */
static int __init kasop_init_aec_ref(void)
{
	int ret;

	ret = kcm_register_cap(CAPABILITY_ID_AEC_REF_1MIC, &aec_ref_impl);
	if (ret)
		return ret;

	ret = kcm_register_cap(CAPABILITY_ID_AEC_REF_2MIC, &aec_ref_impl);

	return ret;
}

subsys_initcall(kasop_init_aec_ref);
