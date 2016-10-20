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

#include <linux/module.h>
#include <linux/slab.h>
#include "../audio-protocol.h"
#include "kcm.h"
#include "kasobj.h"

#include "kasobj/codec.c"
#include "kasobj/fe.c"
#include "kasobj/op.c"

static struct list_head codec_list = LIST_HEAD_INIT(codec_list);
static struct list_head fe_list = LIST_HEAD_INIT(fe_list);
static struct list_head op_list = LIST_HEAD_INIT(op_list);

int __kasobj_fe_cnt, __kasobj_codec_cnt;

static void __init kasobj_init_obj(struct kasobj *obj, const char *name,
		struct list_head *obj_list, int type, struct kasobj_ops *ops)
{
	obj->name = name;
	obj->type = type;
	obj->ops = ops;

	list_add_tail(&obj->link, obj_list);
}

static void __init kasobj_add_codec(const void *db)
{
	struct kasobj_codec *codec =
		kzalloc(sizeof(struct kasobj_codec), GFP_KERNEL);

	codec->db = db;
	kasobj_init_obj(&codec->obj, codec->db->name.s, &codec_list,
			kasobj_type_cd, &codec_ops);
	__kasobj_codec_cnt++;
}

static void __init kasobj_add_fe(const void *db)
{
	const struct kasdb_fe *_db = db;
	struct kasobj_fe *fe = kzalloc(sizeof(struct kasobj_fe) +
			sizeof(u16) * _db->channels_max, GFP_KERNEL);

	fe->db = db;
	kasobj_init_obj(&fe->obj, fe->db->name.s, &fe_list, kasobj_type_fe,
			&fe_ops);
	__kasobj_fe_cnt++;
}

static void __init kasobj_add_op(const void *db)
{
	struct kasobj_op *op = kzalloc(sizeof(struct kasobj_op), GFP_KERNEL);

	op->db = db;
	kasobj_init_obj(&op->obj, op->db->name.s, &op_list, kasobj_type_op,
			&op_ops);
}

int __init kasobj_add(const void *db, int type)
{
	static void (*type2func[kasdb_elm_max])(const void *) = {
		kasobj_add_codec,
		kasobj_add_fe,
		kasobj_add_op,
	};

	if (type < 0 || type >= kasdb_elm_max) {
		kcm_debug("KASOBJ: unknown element!\n");
		return -EINVAL;
	}

	type2func[type](db);
	return 0;
}

static int __init kasobj_init_all(struct list_head *obj_list)
{
	int ret;
	struct kasobj *obj;

	list_for_each_entry(obj, obj_list, link) {
		if (!obj->ops->init)
			continue;
		ret = obj->ops->init(obj);
		if (ret) {
			kcm_debug("KASOBJ: %s init failed!\n", obj->name);
			return ret;
		}
	}
	return 0;
}

struct kasobj_fe *kasobj_find_fe_by_dai(const char *dai_name, int playback)
{
	struct kasobj *obj;
	struct kasobj_fe *fe;

	list_for_each_entry(obj, &fe_list, link) {
		/* Normally, dai_name = obj->name + " PIN" */
		if (strncasecmp(dai_name, obj->name, strlen(obj->name)))
			continue;
		fe = kasobj_to_fe(obj);
		if (fe->db->playback == playback)
			return fe;
	}
	return NULL;
}

/* Return the kasobj_op with a squence of op_idx in op_list */
struct kasobj_op *kasobj_find_op_by_capid(const u16 capid, int op_idx)
{
	struct kasobj *obj;
	struct kasobj_op *op;
	int idx = 0;

	list_for_each_entry(obj, &op_list, link) {
		op = kasobj_to_op(obj);
		if (op->cap_id != capid)
			continue;
		if (op_idx == idx++)
			return op;
	}
	return NULL;
}

int kasobj_register_m3_op(void)
{
	struct kasobj *obj;
	struct kasobj_op *op;
	static bool reg_flag;
	int ret;
	u32 *op_m3;

	if (reg_flag)
		return 0;

	ret = audio_rpmsg_check();
	if (ret) {
		kcm_debug("KCM: rpmsg dev is invalid!\n");
		return -EINVAL;
	}
	list_for_each_entry(obj, &op_list, link) {
		op = kasobj_to_op(obj);
		if (!op->db->ctrl_names.s)
			continue;
		op_m3 = kas_get_m3_op_obj(op->obj.name, strlen(op->obj.name));
		if (!op_m3) {
			kcm_debug("KASOBJ(%s): fail to get OP on M3!\n",
				op->obj.name);
		}
		op->op_m3 = op_m3;
	}
	reg_flag = true;

	return 0;
}

/* Find FE, BE, OP, Link */
struct kasobj *kasobj_find_obj(const char *name, int types)
{
	struct kasobj *obj;

	/* Shall I add { } to fix this ugly code format */
	if (types & kasobj_type_fe)
		list_for_each_entry(obj, &fe_list, link)
			if (strcasecmp(name, obj->name) == 0)
				return obj;

	if (types & kasobj_type_op)
		list_for_each_entry(obj, &op_list, link)
			if (strcasecmp(name, obj->name) == 0)
				return obj;

	if (types & kasobj_type_cd)
		list_for_each_entry(obj, &codec_list, link)
			if (strcasecmp(name, obj->name) == 0)
				return obj;
	return NULL;
}

int __init kasobj_init(void)
{
	int ret;

	/* Initialize all objects (order matters) */
	ret = kasobj_init_all(&fe_list);
	if (ret)
		return ret;
	kcm_debug("KCM: FE init success\n");
	ret = kasobj_init_all(&codec_list);
	if (ret)
		return ret;
	kcm_debug("KCM: CODEC init success\n");
	ret =  kasobj_init_all(&op_list);
	if (ret)
		return ret;
	kcm_debug("KCM: OP init success\n");

	return 0;
}
