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

/* Called before the operator is created */
static int cvc_recv_prepare(struct kasobj_op *op,
	const struct kasobj_param *param)
{
	switch (param->rate) {
	case 8000:
		op->cap_id = CAPABILITY_ID_CVC_RCV_NB;
		break;
	case 16000:
		op->cap_id = CAPABILITY_ID_CVC_RCV_WB;
		break;
	case 24000:
		op->cap_id = CAPABILITY_ID_CVC_RCV_UWB;
		break;
	default:
		pr_err("KASOBJ(%s): Unsupported sample rate(%d) !\n",
			op->obj.name, param->rate);

		break;
	}

	return 0;
}

/* Called after the operator is created */
static int cvc_recv_create(struct kasobj_op *op,
	const struct kasobj_param *param)
{
	u16 cvc_recv_ucid = 4; /* stable user case ID */
	int ret;

	ret = kalimba_operator_message(op->op_id, OPERATOR_MSG_SET_UCID,
		1, &cvc_recv_ucid, NULL, NULL, __kcm_resp);
	if (ret) {
		pr_err("KASOBJ(%s): set UCID failed(%d)!\n", op->obj.name, ret);
		return ret;
	}

	return 0;
}

static const struct kasop_impl cvc_recv_impl = {
	.prepare = cvc_recv_prepare,
	.create = cvc_recv_create,
};

/* registe cvc recv operator */
static int __init kasop_init_cvc_recv(void)
{
	return kcm_register_cap(CAPABILITY_ID_CVC_RCV_DUMMY, &cvc_recv_impl);
}

subsys_initcall(kasop_init_cvc_recv);
