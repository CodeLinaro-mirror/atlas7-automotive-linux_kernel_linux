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
#include "kasobj.h"
#include "kasop.h"
#include "kcm.h"

/* TODO: suspend/resume */

/* A global response buffer is safe as IPC in KCM are exclusive */
u16 __kcm_resp[64];

static int _kcm_init_status;

int kcm_drv_status(void)
{
	return _kcm_init_status;
}
EXPORT_SYMBOL(kcm_drv_status);

static struct device *_pcmdev;

void kcm_set_dev(void *dev)
{
	_pcmdev = dev;
}
EXPORT_SYMBOL(kcm_set_dev);

struct device *kcm_get_dev(void)
{
	return _pcmdev;
}

/* ALSA CPU DAI table */
static struct snd_soc_dai_driver *_dai;
static int _dai_cnt;

struct snd_soc_dai_driver *kcm_alloc_dai(void)
{
	BUG_ON(_dai_cnt >= __kasobj_fe_cnt);

	if (!_dai)
		_dai = kzalloc(sizeof(struct snd_soc_dai_driver) *
				__kasobj_fe_cnt, GFP_KERNEL);
	return &_dai[_dai_cnt++];
}

struct snd_soc_dai_driver *kcm_get_dai(int *cnt)
{
	*cnt = _dai_cnt;
	return _dai;
}
EXPORT_SYMBOL(kcm_get_dai);

/* ALSA DAPM routing table */
static struct snd_soc_dapm_route *_route;
static int _route_cnt;

struct snd_soc_dapm_route *kcm_alloc_route(void)
{
	/* Each FE contains at most two routing entries: playback and capture */
	BUG_ON(_route_cnt >= __kasobj_fe_cnt * 2);

	if (!_route)
		_route = kzalloc(sizeof(struct snd_soc_dapm_route) *
				__kasobj_fe_cnt * 2, GFP_KERNEL);
	return &_route[_route_cnt++];
}

struct snd_soc_dapm_route *kcm_get_route(int *cnt)
{
	*cnt = _route_cnt;
	return _route;
}
EXPORT_SYMBOL(kcm_get_route);

/* ALSA DAI link table */
static struct snd_soc_dai_link *_dai_link;
static int _dai_link_cnt;

struct snd_soc_dai_link *kcm_alloc_dai_link(void)
{
	BUG_ON(_dai_link_cnt >= __kasobj_fe_cnt);

	if (!_dai_link)
		_dai_link = kzalloc(sizeof(struct snd_soc_dai_link) *
				(__kasobj_fe_cnt + __kasobj_codec_cnt),
				GFP_KERNEL);
	return &_dai_link[_dai_link_cnt++];
}

struct snd_soc_dai_link *kcm_get_dai_link(int *cnt, int *free_cnt)
{
	*cnt = _dai_link_cnt;
	*free_cnt = __kasobj_fe_cnt + __kasobj_codec_cnt - _dai_link_cnt;
	return _dai_link;
}
EXPORT_SYMBOL(kcm_get_dai_link);

/* ALSA control interface list */
struct _ctrl {
	struct snd_kcontrol_new *ctrl;
	struct list_head link;
};

static struct list_head _ctrl_list = LIST_HEAD_INIT(_ctrl_list);
static struct list_head *_ctrl_ptr;

void kcm_register_ctrl(void *ctrl)
{
	struct _ctrl *_ctrl = kmalloc(sizeof(struct _ctrl), GFP_KERNEL);

	_ctrl->ctrl = ctrl;
	list_add_tail(&_ctrl->link, &_ctrl_list);
}

static struct snd_kcontrol_new *kcm_ctrl_get(void)
{
	if (_ctrl_ptr == &_ctrl_list)
		return NULL;
	else
		return container_of(_ctrl_ptr, struct _ctrl, link)->ctrl;
}

struct snd_kcontrol_new *kcm_ctrl_first(void)
{
	_ctrl_ptr = _ctrl_list.next;
	return kcm_ctrl_get();
}
EXPORT_SYMBOL(kcm_ctrl_first);

struct snd_kcontrol_new *kcm_ctrl_next(void)
{
	_ctrl_ptr = _ctrl_ptr->next;
	return kcm_ctrl_get();
}
EXPORT_SYMBOL(kcm_ctrl_next);

/* Capability implementation list. Created before driver init(). */
static struct list_head _cap_list = LIST_HEAD_INIT(_cap_list);

struct _kasop_cap {
	int cap_id;
	struct kasop_impl *impl;
	struct list_head link;
};

/* Called before driver init() */
struct kasop_impl *kcm_find_cap(int cap_id)
{
	struct _kasop_cap *cap;

	list_for_each_entry(cap, &_cap_list, link) {
		if (cap->cap_id == cap_id)
			return cap->impl;
	}
	return NULL;
}

/* Called before driver init() */
int kcm_register_cap(int cap_id, struct kasop_impl *impl)
{
	struct _kasop_cap *cap;

	if (kcm_find_cap(cap_id)) {
		pr_err("KASOP: capability %d already exists!\n", cap_id);
		return -EINVAL;
	}

	cap = kmalloc(sizeof(struct _kasop_cap), GFP_KERNEL);
	cap->cap_id = cap_id;
	cap->impl = impl;
	list_add_tail(&cap->link, &_cap_list);

	return 0;
}

/* Global mutex. Only used by chain and control callbacks. */
static struct mutex _mtx;

void kcm_lock(void)
{
	mutex_lock(&_mtx);
}
EXPORT_SYMBOL(kcm_lock);

void kcm_unlock(void)
{
	mutex_unlock(&_mtx);
}
EXPORT_SYMBOL(kcm_unlock);

struct kasobj_fe *kcm_find_fe(const char *dai_name, int playback)
{
	return kasobj_find_fe_by_dai(dai_name, playback);
}
EXPORT_SYMBOL(kcm_find_fe);

/* Find s2 in s1, case insensitive. Put under lib? */
char *kcm_strcasestr(const char *s1, const char *s2)
{
	size_t l1, l2;

	l2 = strlen(s2);
	if (!l2)
		return (char *)s1;
	l1 = strlen(s1);
	while (l1 >= l2) {
		l1--;
		if (!strncasecmp(s1, s2, l2))
			return (char *)s1;
		s1++;
	}
	return NULL;
}

static int __init kcm_init(void)
{
	void *db_base = NULL;
	size_t db_sz = 0;

	mutex_init(&_mtx);

	/* TODO: assign user db base and size to db_base and db_sz */

	/* Load user database, fallback to default db if failed */
	if (db_base && kasdb_load_user(db_base, db_sz) == 0) {
		kcm_debug("KCM: user database loaded\n");
	} else {
		kasdb_load_default();
		kcm_debug("KCM: default database loaded\n");
	}

	/* Initialize all objects and chains */
	_kcm_init_status = kasobj_init();
	return _kcm_init_status;
}

/* Must be earlier than ALSA drivers */
fs_initcall(kcm_init);
