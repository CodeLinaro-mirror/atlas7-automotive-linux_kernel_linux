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

#ifndef _KCM_KASOP_H
#define _KCM_KASOP_H

#include "kas.h"

enum {
	kasop_event_start_ep,
	kasop_event_stop_ep,
	/* Cannot exceed 16 events, see following macro */
};

#define KASOP_MAKE_EVENT(event, param)	((event) | ((param) << 4))
#define KASOP_GET_EVENT(event_param)	((event_param) & 0xF)
#define KASOP_GET_PARAM(event_param)	((event_param) >> 4)
#define KASOP_MAX_SAMPLE_RATE		(48000)

struct kasop_impl {
	int (*init)(struct kasobj_op *op);
	int (*prepare)(struct kasobj_op *op, const struct kasobj_param *param);
	int (*create)(struct kasobj_op *op, const struct kasobj_param *param);
	int (*reconfig)(struct kasobj_op *op, const struct kasobj_param *param);
	int (*trigger)(struct kasobj_op *op, int event);
};

#endif
