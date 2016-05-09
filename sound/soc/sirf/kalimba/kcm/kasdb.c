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
#include <sound/pcm_params.h>
#include "kasobj.h"
#include "../dsp.h"

#if SNDRV_PCM_RATE_5512 != 1 << 0 || SNDRV_PCM_RATE_192000 != 1 << 12
#error "Rate definition changed in kernel!"
#endif

#define __S(str)	{ .s = str }

#include "kasdb-ctrls.h"
#include "kasdb-hw.c"
#include "fe.c"
#include "op.c"
#include "link.c"
#include "chain.c"

void __init kasdb_load_database(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(codec); i++)
		kasobj_add(&codec[i], kasdb_elm_codec);
	for (i = 0; i < ARRAY_SIZE(hw); i++)
		kasobj_add(&hw[i], kasdb_elm_hw);
	for (i = 0; i < ARRAY_SIZE(fe); i++)
		kasobj_add(&fe[i], kasdb_elm_fe);
	for (i = 0; i < ARRAY_SIZE(op); i++)
		kasobj_add(&op[i], kasdb_elm_op);
	for (i = 0; i < ARRAY_SIZE(link); i++)
		kasobj_add(&link[i], kasdb_elm_link);
	for (i = 0; i < ARRAY_SIZE(chain); i++)
		kasobj_add(&chain[i], kasdb_elm_chain);
}
