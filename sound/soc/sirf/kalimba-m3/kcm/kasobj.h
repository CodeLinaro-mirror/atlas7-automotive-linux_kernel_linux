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

#ifndef _KCM_OBJ_H
#define _KCM_OBJ_H

#include "kasdb.h"

struct kasobj;
struct kasobj_param;

/* Interface */
struct kasobj_ops {
	int (*init)(struct kasobj *obj);
};

/* Object types */
enum {
	kasobj_type_cd = BIT(0),	/* Codec */
	kasobj_type_hw = BIT(1),	/* Sink/Source */
	kasobj_type_fe = BIT(2),	/* Front End */
	kasobj_type_op = BIT(3),	/* Operator */
	kasobj_type_lk = BIT(4),	/* Link */
};

/* Base class */
struct kasobj {
	const char *name;
	struct kasobj_ops *ops;

	int type;
	struct list_head link;		/* Link to objects of same type */
};

struct kasobj_codec {
	struct kasobj obj;
	const struct kasdb_codec *db;

	int rate;
};
#define kasobj_to_codec(pobj)	container_of((pobj), struct kasobj_codec, obj)

struct kasobj_fe {
	struct kasobj obj;
	const struct kasdb_fe *db;

	int ep_cnt;
	u16 ep_id[0];			/* Depends on max channels */
};
#define kasobj_to_fe(pobj)	container_of((pobj), struct kasobj_fe, obj)

struct kasobj_op {
	struct kasobj obj;
	const struct kasdb_op *db;

	const struct kasop_impl *impl;	/* Operator specific implementation */
	u16 cap_id;
	u32 *op_m3;			/* The operator object cerated on M3 */
	int *ctrl_flag;			/* The modfy flag of ctrl value */
	int *ctrl_value;
};
#define kasobj_to_op(pobj)	container_of((pobj), struct kasobj_op, obj)

struct kasobj_link {
	struct kasobj obj;
	const struct kasdb_link *db;

	struct kasobj *source;
	struct kasobj *sink;
	u16 conn_id[0];
};
#define kasobj_to_link(pobj)	container_of((pobj), struct kasobj_link, obj)

/* Create kasobj_xxx based on kasdb_xxx */
/* Database interface is not OO, type must be designated explicitly. */
int kasobj_add(const void *db, int type);
void kcm_add_chain(const void *db);

int kasobj_init(void);
int kcm_init_chain(void);

struct kasobj *kasobj_find_obj(const char *name, int types);
struct kasobj_fe *kasobj_find_fe_by_dai(const char *dai_name, int playback);
struct kasobj_op *kasobj_find_op_by_capid(const u16 capid, int op_idx);

int kasobj_register_m3_op(void);

/* Hack: object count */
extern int __kasobj_fe_cnt, __kasobj_codec_cnt;

#define KCM_INVALID_EP_ID	0xFFFF
extern u16 __kcm_resp[64];

#endif
