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
#include "../kasobj.h"
#include "../kasop.h"
#include "../kcm.h"
#include "../../dsp.h"

static int resampler_rate_index(int rate)
{
	static const int rates[] = { 8000, 11025, 12000, 16000, 22050, 24000,
		32000, 44100, 48000 };
	int si = 0, ei = ARRAY_SIZE(rates) - 1;

	/* Yes, I'm sick enough to adopt binary search here. */
	while (si <= ei) {
		int mi = (si + ei) / 2;

		if (rate < rates[mi])
			ei = mi - 1;
		else if (rate > rates[mi])
			si = mi + 1;
		else
			return mi;
	}
	return -1;
}

static u16 resampler_conversion_rate(int input_rate, int output_rate)
{
	int input_rate_index = resampler_rate_index(input_rate);
	int output_rate_index = resampler_rate_index(output_rate);

	if (input_rate_index < 0 || output_rate_index < 0)
		return 0xFFFF;
	return (input_rate_index << 4) | output_rate_index;
}

/* Set conversion rate */
static int resampler_create(struct kasobj_op *op,
		const struct kasobj_param *param)
{
	const struct kasdb_op *db = op->db;
	int input_rate, output_rate;
	u16 conversion_rate;

	if (db->param.resampler_custom_output) {
		input_rate = db->rate;
		output_rate = param->rate;
	} else {
		input_rate = param->rate;
		output_rate = db->rate;
	}

	conversion_rate = resampler_conversion_rate(input_rate, output_rate);
	if (conversion_rate == 0xFFFF) {
		pr_err("KASOBJ(%s): rate not supported: %d to %d\n",
			       op->obj.name, input_rate, output_rate);
		return -EINVAL;
	}

	kalimba_operator_message(op->op_id, RESAMPLER_SET_CONVERSION_RATE, 1,
			&conversion_rate, NULL, NULL, __kcm_resp);

	return 0;
}

static struct kasop_impl resampler_impl = {
	.create = resampler_create,
};

static int __init kasop_init_resampler(void)
{
	return kcm_register_cap(CAPABILITY_ID_RESAMPLER, &resampler_impl);
}

subsys_initcall(kasop_init_resampler);
