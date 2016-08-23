/*
 * Copyright (c) [2016] The Linux Foundation. All rights reserved.
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

static const struct kasdb_op op[] = {
	{
		/* Music passthrough */
		.name = __S("op_pass_music"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(KCM_CTRLS_BASICPASS("Music")),
		.cap_id = CAPABILITY_ID_BASIC_PASSTHROUGH,
		.rate = 0,
		.param.dummy = 0,
	},
};
