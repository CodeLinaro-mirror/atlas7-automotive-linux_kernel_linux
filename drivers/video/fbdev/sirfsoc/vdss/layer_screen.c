/*
 * CSR sirfsoc vdss core file
 *
 * Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
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

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/jiffies.h>

#include <video/sirfsoc_vdss.h>

#include "vdss.h"

#define NUM_SCREENS_PER_LCDC	1
#define NUM_LAYERS_PER_LCDC	4

static int num_screens[NUM_LCDC];
static struct sirfsoc_vdss_screen *screens[NUM_LCDC];
static int num_layers[NUM_LCDC];
static struct sirfsoc_vdss_layer *layers[NUM_LCDC];

#define SCREEN_TIMING		BIT(0)
#define SCREEN_DATA_LINES	BIT(1)
#define SCREEN_GAMMA		BIT(2)
#define SCREEN_ERROR_DIFFUSION	BIT(3)

int sirfsoc_vdss_get_num_screens(u32 lcdc_index)
{
	return num_screens[lcdc_index];
}
EXPORT_SYMBOL(sirfsoc_vdss_get_num_screens);

struct sirfsoc_vdss_screen *sirfsoc_vdss_get_screen(u32 lcdc_index, int num)
{
	if (num >= num_screens[lcdc_index])
		return NULL;

	return &screens[lcdc_index][num];
}
EXPORT_SYMBOL(sirfsoc_vdss_get_screen);

int sirfsoc_vdss_get_num_layers(u32 lcdc_index)
{
	return num_layers[lcdc_index];
}
EXPORT_SYMBOL(sirfsoc_vdss_get_num_layers);

struct sirfsoc_vdss_layer *sirfsoc_vdss_get_layer(u32 lcdc_index, int num)
{
	if (num >= num_layers[lcdc_index])
		return NULL;

	return &layers[lcdc_index][num];
}
EXPORT_SYMBOL(sirfsoc_vdss_get_layer);


struct sirfsoc_vdss_layer *sirfsoc_vdss_get_layer_from_screen(
	struct sirfsoc_vdss_screen *scn, enum vdss_layer id, bool rearview)
{
	struct sirfsoc_vdss_layer *l;

	l = &layers[scn->lcdc_id][id];
	if (!rearview && l->is_enabled(l))
		return NULL;

	if ((l->screen->id == scn->id) && !l->is_enabled(l)) {
		if (l->screen)
			l->unset_screen(l);
		if (l->set_screen(l, scn))
			return NULL;
		return l;
	}

	return NULL;
}
EXPORT_SYMBOL(sirfsoc_vdss_get_layer_from_screen);

struct layer_priv_data {

	bool user_info_dirty;
	struct sirfsoc_vdss_layer_info user_info;

	bool info_dirty;
	struct sirfsoc_vdss_layer_info info;

	bool shadow_info_dirty;

	bool extra_info_dirty;
	bool shadow_extra_info_dirty;

	/*
	 * it's used to remember the client op, isn't the
	 * real hw status. And to check hw status, should
	 * use both "enabled" and "preempted" flags
	 * */
	bool enabled;
	/*
	 * if there is a task with high priority to do, sometimes
	 * some layer should be disabled forcedly, and "preempted"
	 * flag will be set as true. Notes that at preset
	 * the value of the "enabled" flag is unchanged
	 * */
	bool preempted;

	/*
	 * when this layer is preempted, vdss will notify the owner with
	 * the following callback
	 */
	sirfsoc_layer_notify_t func;
	void *arg;

	/*
	 * True if overlay is to be enabled. Used to check and calculate configs
	 * for the overlay before it is enabled in the HW.
	 */
	bool enabling;
};

struct screen_priv_data {

	bool user_info_dirty;
	struct sirfsoc_vdss_screen_info user_info;

	bool info_dirty;
	struct sirfsoc_vdss_screen_info info;
	bool shadow_info_dirty;

	/* If true, GO bit is up and shadow registers cannot be written.
	 * Never true for manual update displays */
	bool busy;

	/* If true, dispc output is enabled */
	bool updating;

	/* If true, a display is enabled using this manager */
	bool enabled;

	bool have_gamma;
	int extra_info_dirty;
	bool shadow_extra_info_dirty;

	struct sirfsoc_video_timings timings;
	int data_lines;
	u8 gamma[256 * 3];
	bool error_diffusion;
};

struct layer_wait {
	struct completion comp;
	struct sirfsoc_vdss_layer *l;
};

static struct {
	struct layer_priv_data layer_datas[NUM_LCDC][NUM_LAYERS_PER_LCDC];
	struct screen_priv_data screen_datas[NUM_LCDC][NUM_SCREENS_PER_LCDC];

	bool irq_enabled;
} vdss_data;

/* protects vdss_data */
static DEFINE_SPINLOCK(data_lock);
/* lock for blocking functions */
static DEFINE_MUTEX(apply_lock);
static struct layer_priv_data *get_layer_data(struct sirfsoc_vdss_layer *l)
{
	return &vdss_data.layer_datas[l->lcdc_id][l->id];
}

static struct screen_priv_data *get_screen_data(struct sirfsoc_vdss_screen *scn)
{
	/*FIXME: current only one screen for each lcd, if there is more
	 * screen with one lcdc, this logic need refine.*/

	return &vdss_data.screen_datas[scn->lcdc_id][0];
}

static void vdss_set_layer_status(struct sirfsoc_vdss_layer *l,
	bool enable);
static bool vdss_get_layer_status(struct sirfsoc_vdss_layer *l);
static void vdss_layer_get_info(struct sirfsoc_vdss_layer *layer,
	struct sirfsoc_vdss_layer_info *info);
static int vdss_layer_set_info(struct sirfsoc_vdss_layer *layer,
	struct sirfsoc_vdss_layer_info *info);


static ssize_t layer_enable_show(struct sirfsoc_vdss_layer *l,
	char *buf)
{
	unsigned long flags;
	bool e;

	spin_lock_irqsave(&data_lock, flags);

	e = vdss_get_layer_status(l);

	spin_unlock_irqrestore(&data_lock, flags);

	return snprintf(buf, PAGE_SIZE, "%d\n", e);
}

static ssize_t layer_enable_store(struct sirfsoc_vdss_layer *l,
	const char *buf, size_t size)
{
	int r;
	bool e;
	struct layer_priv_data *ldata = get_layer_data(l);
	unsigned long flags;

	r = strtobool(buf, &e);
	if (r)
		return r;

	spin_lock_irqsave(&data_lock, flags);

	/*
	 * If the layer has been preempted, enable/disable operation
	 * will be skipped
	 * */
	if (ldata->preempted) {
		spin_unlock_irqrestore(&data_lock, flags);
		return size;
	}

	/*
	 * If there is no client to enable this layer, it is invalid to
	 * enable it, because the display pipeline hasn't been setup.
	 * */
	if (!ldata->enabled && e) {
		spin_unlock_irqrestore(&data_lock, flags);
		return size;
	}

	vdss_set_layer_status(l, e);

	spin_unlock_irqrestore(&data_lock, flags);

	return size;
}

static ssize_t layer_src_alpha_show(struct sirfsoc_vdss_layer *l,
	char *buf)
{
	struct sirfsoc_vdss_layer_info info;

	vdss_layer_get_info(l, &info);

	return snprintf(buf, PAGE_SIZE, "%d\n", info.source_alpha);
}

static ssize_t layer_src_alpha_store(struct sirfsoc_vdss_layer *l,
	const char *buf, size_t size)
{
	int r;
	bool alpha;
	struct sirfsoc_vdss_layer_info info;

	r = strtobool(buf, &alpha);
	if (r)
		return r;

	vdss_layer_get_info(l, &info);

	if (info.source_alpha != alpha) {
		info.source_alpha = alpha;
		vdss_layer_set_info(l, &info);
		l->screen->apply(l->screen);
	}

	return size;
}

static ssize_t layer_premulti_alpha_show(struct sirfsoc_vdss_layer *l,
	char *buf)
{
	struct sirfsoc_vdss_layer_info info;

	vdss_layer_get_info(l, &info);

	return snprintf(buf, PAGE_SIZE, "%d\n", info.pre_mult_alpha);
}

static ssize_t layer_premulti_alpha_store(struct sirfsoc_vdss_layer *l,
	const char *buf, size_t size)
{
	int r;
	bool alpha;
	struct sirfsoc_vdss_layer_info info;

	r = strtobool(buf, &alpha);
	if (r)
		return r;

	vdss_layer_get_info(l, &info);

	if (info.pre_mult_alpha != alpha) {
		info.pre_mult_alpha = alpha;
		vdss_layer_set_info(l, &info);
		l->screen->apply(l->screen);
	}

	return size;
}

struct layer_attribute {
	struct attribute attr;
	ssize_t (*show)(struct sirfsoc_vdss_layer *, char *);
	ssize_t (*store)(struct sirfsoc_vdss_layer *, const char *, size_t);
};

#define LAYER_ATTR(_name, _mode, _show, _store) \
	struct layer_attribute layer_attr_##_name = \
	__ATTR(_name, _mode, _show, _store)

static LAYER_ATTR(layer_enable, S_IRUGO|S_IWUSR,
	layer_enable_show, layer_enable_store);
static LAYER_ATTR(layer_src_alpha, S_IRUGO|S_IWUSR,
	layer_src_alpha_show, layer_src_alpha_store);
static LAYER_ATTR(layer_premulti_alpha, S_IRUGO|S_IWUSR,
	layer_premulti_alpha_show, layer_premulti_alpha_store);



static struct attribute *layer_sysfs_attrs[] = {
	&layer_attr_layer_enable.attr,
	&layer_attr_layer_src_alpha.attr,
	&layer_attr_layer_premulti_alpha.attr,
	NULL
};

static ssize_t layer_attr_show(struct kobject *kobj, struct attribute *attr,
		char *buf)
{
	struct sirfsoc_vdss_layer *l;
	struct layer_attribute *layer_attr;

	l = container_of(kobj, struct sirfsoc_vdss_layer, kobj);
	layer_attr = container_of(attr, struct layer_attribute, attr);

	if (!layer_attr->show)
		return -ENOENT;

	return layer_attr->show(l, buf);
}

static ssize_t layer_attr_store(struct kobject *kobj, struct attribute *attr,
		const char *buf, size_t size)
{
	struct sirfsoc_vdss_layer *l;
	struct layer_attribute *layer_attr;

	l = container_of(kobj, struct sirfsoc_vdss_layer, kobj);
	layer_attr = container_of(attr, struct layer_attribute, attr);

	if (!layer_attr->store)
		return -ENOENT;

	return layer_attr->store(l, buf, size);
}

static const struct sysfs_ops layer_sysfs_ops = {
	.show = layer_attr_show,
	.store = layer_attr_store,
};

static struct kobj_type layer_ktype = {
	.sysfs_ops = &layer_sysfs_ops,
	.default_attrs = layer_sysfs_attrs,
};

int vdss_init_layers_sysfs(u32 lcdc_index)
{
	int i;
	int r;
	int num_layer = sirfsoc_vdss_get_num_layers(lcdc_index);
	struct platform_device *pdev = vdss_get_core_pdev();

	for (i = 0; i < num_layer; ++i) {
		struct sirfsoc_vdss_layer *l =
			sirfsoc_vdss_get_layer(lcdc_index, i);
		if (!l) {
			VDSSERR("failed to get valid layer\n");
			r = -EINVAL;
			goto err;
		}

		r = kobject_init_and_add(&l->kobj, &layer_ktype,
				&pdev->dev.kobj, "lcd%d-%s",
				lcdc_index, l->name);

		if (r) {
			VDSSERR("failed to create layer sysfs files\n");
			goto err;
		}
	}

	return 0;

err:
	vdss_uninit_layers_sysfs(lcdc_index);

	return r;
}

void vdss_uninit_layers_sysfs(u32 lcdc_index)
{
	int i;
	const int num_layer = sirfsoc_vdss_get_num_layers(lcdc_index);

	for (i = 0; i < num_layer; ++i) {
		struct sirfsoc_vdss_layer *l =
			sirfsoc_vdss_get_layer(lcdc_index, i);

		kobject_del(&l->kobj);
		kobject_put(&l->kobj);
	}
}

/*
 * check manager and overlay settings using overlay_info from data->info
 */
static int vdss_screen_check_settings(struct sirfsoc_vdss_screen *scn,
	struct sirfsoc_vdss_screen_info *info)
{
	return 0;
}

static int vdss_layer_check_settings(struct sirfsoc_vdss_layer *layer,
	struct sirfsoc_vdss_layer_info *info)
{
	return 0;
}

static int vdss_check_settings(struct sirfsoc_vdss_screen *scn)
{
	return 0;
}

static void vdss_layer_update_regs(struct sirfsoc_vdss_layer *l)
{
	struct layer_priv_data *ldata = get_layer_data(l);
	struct sirfsoc_vdss_layer_info *info;
	struct screen_priv_data *sdata;

	VDSSDBG("writing layer %d regs", l->id);

	if (!ldata->enabled || !ldata->info_dirty)
		return;

	info = &ldata->info;

	sdata = get_screen_data(l->screen);

	lcdc_layer_setup(l->lcdc_id, l->id, info, &sdata->timings);

	ldata->info_dirty = false;
	if (sdata->updating)
		ldata->shadow_info_dirty = true;
}

static void vdss_set_layer_status(struct sirfsoc_vdss_layer *l,
	bool enable)
{
	struct layer_priv_data *ldata = get_layer_data(l);
	/*
	 * When inline mode, dcu and layer must be enabled or
	 * disabled together. If we only disable layer, dcu will
	 * hang
	 */
	if (ldata->info.disp_mode == VDSS_DISP_INLINE) {
		if (enable == false)
			dcu_disable();
		else
			dcu_enable();
	}

	lcdc_layer_enable(l->lcdc_id, l->id, enable,
		ldata->info.disp_mode != VDSS_DISP_NORMAL);
}

static bool vdss_get_layer_status(struct sirfsoc_vdss_layer *l)
{
	return lcdc_get_layer_status(l->lcdc_id, l->id);
}

static void vdss_layer_update_regs_extra(struct sirfsoc_vdss_layer *l)
{
	struct layer_priv_data *ldata = get_layer_data(l);
	struct screen_priv_data *sdata;

	VDSSDBG("writing layer %d regs extra", l->id);

	if (!ldata->extra_info_dirty || ldata->preempted)
		return;

	vdss_set_layer_status(l, ldata->enabled);

	sdata = get_screen_data(l->screen);

	ldata->extra_info_dirty = false;
	if (sdata->updating)
		ldata->shadow_extra_info_dirty = true;
}

static void vdss_screen_update_regs(struct sirfsoc_vdss_screen *scn)
{
	struct screen_priv_data *sdata = get_screen_data(scn);
	struct sirfsoc_vdss_layer *l;

	VDSSDBG("writing scn %d regs", scn->id);

	if (!sdata->enabled)
		return;

	WARN_ON(sdata->busy);

	if (sdata->info_dirty) {
		lcdc_screen_setup(scn->lcdc_id, scn->id, &sdata->info);

		sdata->info_dirty = false;
		if (sdata->updating)
			sdata->shadow_info_dirty = true;
	}

	/* Commit overlay settings */
	list_for_each_entry(l, &scn->layers, list) {
		vdss_layer_update_regs(l);
		vdss_layer_update_regs_extra(l);
	}
}

void vdss_screen_update_regs_extra(struct sirfsoc_vdss_screen *scn)
{
	struct screen_priv_data *sdata = get_screen_data(scn);

	VDSSDBG("writing screen %d regs extra", scn->id);

	if (!sdata->extra_info_dirty)
		return;

	if (sdata->extra_info_dirty & SCREEN_TIMING) {
		lcdc_screen_set_timings(scn->lcdc_id, scn->id, &sdata->timings);
		sdata->extra_info_dirty &= ~SCREEN_TIMING;
	}

	if (sdata->extra_info_dirty & SCREEN_DATA_LINES) {
		lcdc_screen_set_data_lines(scn->lcdc_id, scn->id,
			sdata->data_lines);
		sdata->extra_info_dirty &= ~SCREEN_DATA_LINES;
	}

	if (sdata->extra_info_dirty & SCREEN_GAMMA) {
		lcdc_screen_set_gamma(scn->lcdc_id, scn->id, &sdata->gamma[0]);
		sdata->extra_info_dirty &= ~SCREEN_GAMMA;
	}

	if (sdata->extra_info_dirty & SCREEN_ERROR_DIFFUSION) {
		lcdc_screen_set_error_diffusion(scn->lcdc_id, scn->id,
			sdata->data_lines, sdata->error_diffusion);
		sdata->extra_info_dirty &= ~SCREEN_ERROR_DIFFUSION;
	}

	if (sdata->updating)
		sdata->shadow_extra_info_dirty = true;
}

static void vdss_update_regs(u32 lcdc_index)
{
	const int num_scns = sirfsoc_vdss_get_num_screens(lcdc_index);
	int i;

	for (i = 0; i < num_scns; ++i) {
		struct sirfsoc_vdss_screen *scn;
		struct screen_priv_data *sdata;
		int r;

		scn = sirfsoc_vdss_get_screen(lcdc_index, i);
		if (!scn) {
			VDSSERR("screen error\n");
			return;
		}

		sdata = get_screen_data(scn);

		if (!sdata->enabled || sdata->busy)
			continue;

		r = vdss_check_settings(scn);
		if (r) {
			VDSSERR("cannot update regs for %s: bad config\n",
				scn->name);
			continue;
		}

		vdss_screen_update_regs_extra(scn);
		vdss_screen_update_regs(scn);
	}
}

/* Restore lcdc register in PM resume. */
void vdss_restore_screen_layer(u32 lcdc_index)
{
	const int num_scns = sirfsoc_vdss_get_num_screens(lcdc_index);
	int i;

	for (i = 0; i < num_scns; ++i) {
		struct sirfsoc_vdss_screen *scn;
		struct screen_priv_data *sdata;
		struct sirfsoc_vdss_layer *l;

		scn = sirfsoc_vdss_get_screen(lcdc_index, i);
		if (!scn) {
			VDSSERR("screen error\n");
			return;
		}

		sdata = get_screen_data(scn);

		if (!sdata->enabled)
			continue;

		WARN_ON(sdata->busy);

		sdata->extra_info_dirty = SCREEN_TIMING |
				SCREEN_DATA_LINES |
				SCREEN_ERROR_DIFFUSION;

		if (sdata->have_gamma)
			sdata->extra_info_dirty |= SCREEN_GAMMA;

		vdss_screen_update_regs_extra(scn);

		lcdc_screen_setup(scn->lcdc_id, scn->id, &sdata->info);

		list_for_each_entry(l, &scn->layers, list) {
			struct layer_priv_data *ldata = get_layer_data(l);
			struct sirfsoc_vdss_layer_info *linfo;

			if (ldata->enabled) {
				linfo = &ldata->info;
				lcdc_layer_setup(l->lcdc_id, l->id,
						 linfo,
						 &sdata->timings);
			}

			if (!ldata->preempted)
				vdss_set_layer_status(l, ldata->enabled);
		}
	}
}

static void vdss_apply_layer_enable(struct sirfsoc_vdss_layer *layer,
	bool enable)
{
	struct layer_priv_data *ldata;

	ldata = get_layer_data(layer);

	if (ldata->enabled == enable)
		return;

	ldata->enabled = enable;
	ldata->extra_info_dirty = true;
}

static int vdss_layer_set_info(struct sirfsoc_vdss_layer *layer,
	struct sirfsoc_vdss_layer_info *info)
{
	struct layer_priv_data *ldata = get_layer_data(layer);
	unsigned long flags;
	int r;

	r = vdss_layer_check_settings(layer, info);
	if (r)
		return r;

	spin_lock_irqsave(&data_lock, flags);

	ldata->user_info = *info;
	ldata->user_info_dirty = true;

	spin_unlock_irqrestore(&data_lock, flags);

	return 0;
}

static void vdss_layer_get_info(struct sirfsoc_vdss_layer *layer,
	struct sirfsoc_vdss_layer_info *info)
{
	struct layer_priv_data *ldata = get_layer_data(layer);
	unsigned long flags;

	spin_lock_irqsave(&data_lock, flags);

	*info = ldata->user_info;

	spin_unlock_irqrestore(&data_lock, flags);
}

static int vdss_layer_set_screen(struct sirfsoc_vdss_layer *layer,
	struct sirfsoc_vdss_screen *scn)
{
	struct layer_priv_data *ldata = get_layer_data(layer);
	unsigned long flags;
	int r;

	if (!scn)
		return -EINVAL;

	mutex_lock(&apply_lock);

	if (layer->screen) {
		VDSSERR("layer '%s' already has a screen '%s'\n",
			layer->name, layer->screen->name);
		r = -EINVAL;
		goto err;
	}

	spin_lock_irqsave(&data_lock, flags);

	if (ldata->enabled) {
		spin_unlock_irqrestore(&data_lock, flags);
		VDSSERR("layer has to be disabled to change the screen\n");
		r = -EINVAL;
		goto err;
	}

	layer->screen = scn;
	list_add_tail(&layer->list, &scn->layers);

	spin_unlock_irqrestore(&data_lock, flags);

	mutex_unlock(&apply_lock);

	return 0;

err:
	mutex_unlock(&apply_lock);
	return r;
}

static int vdss_layer_unset_screen(struct sirfsoc_vdss_layer *layer)
{
	struct layer_priv_data *ldata = get_layer_data(layer);
	unsigned long flags;
	int r;

	mutex_lock(&apply_lock);

	if (!layer->screen) {
		VDSSERR("failed to detach layer: screen not set\n");
		r = -EINVAL;
		goto err;
	}

	spin_lock_irqsave(&data_lock, flags);

	if (ldata->enabled) {
		spin_unlock_irqrestore(&data_lock, flags);
		VDSSERR("layer has to be disabled to unset the screen\n");
		r = -EINVAL;
		goto err;
	}

	layer->screen = NULL;
	list_del(&layer->list);

	spin_unlock_irqrestore(&data_lock, flags);

	mutex_unlock(&apply_lock);

	return 0;
err:
	mutex_unlock(&apply_lock);
	return r;
}

static bool vdss_layer_is_enabled(struct sirfsoc_vdss_layer *layer)
{
	struct layer_priv_data *ldata = get_layer_data(layer);
	unsigned long flags;
	bool e;

	spin_lock_irqsave(&data_lock, flags);

	e = ldata->enabled;

	spin_unlock_irqrestore(&data_lock, flags);

	return e;
}

static bool vdss_layer_is_preempted(struct sirfsoc_vdss_layer *layer)
{
	struct layer_priv_data *ldata = get_layer_data(layer);
	unsigned long flags;
	bool e;

	spin_lock_irqsave(&data_lock, flags);

	e = ldata->preempted;

	spin_unlock_irqrestore(&data_lock, flags);

	return e;
}

static int vdss_layer_register_notify(
	struct sirfsoc_vdss_layer *layer,
	sirfsoc_layer_notify_t func,
	void *arg)
{
	struct layer_priv_data *ldata = get_layer_data(layer);
	unsigned long flags;

	spin_lock_irqsave(&data_lock, flags);

	ldata->func = func;
	ldata->arg = arg;

	spin_unlock_irqrestore(&data_lock, flags);

	return 0;
}

static int vdss_layer_enable(struct sirfsoc_vdss_layer *layer)
{
	struct layer_priv_data *ldata = get_layer_data(layer);
	unsigned long flags;
	int r = 0;

	spin_lock_irqsave(&data_lock, flags);

	if (ldata->enabled) {
		r = 0;
		goto err;
	}

	if (layer->screen == NULL || layer->screen->output == NULL) {
		r = -EINVAL;
		goto err;
	}
	ldata->enabling = true;

	r = vdss_check_settings(layer->screen);
	if (r) {
		VDSSERR("failed to enable layer %d: check_settings failed\n",
			layer->id);
		ldata->enabling = false;
		goto err;
	}

	ldata->enabling = false;
	vdss_apply_layer_enable(layer, true);

	vdss_update_regs(layer->lcdc_id);

err:
	spin_unlock_irqrestore(&data_lock, flags);
	return r;
}

/*
 * Must call with lock held
 * */
static int vdss_layer_disable_l(struct sirfsoc_vdss_layer *layer)
{
	struct layer_priv_data *ldata = get_layer_data(layer);
	int r = 0;

	if (!ldata->enabled) {
		r = 0;
		goto err;
	}

	if (layer->screen == NULL || layer->screen->output == NULL) {
		r = -EINVAL;
		goto err;
	}

	vdss_apply_layer_enable(layer, false);
	vdss_update_regs(layer->lcdc_id);

err:
	return r;
}

static void layer_disable_in_vsync(void *data, u32 mask)
{
	struct layer_wait *wait = (struct layer_wait *)data;

	spin_lock(&data_lock);
	vdss_layer_disable_l(wait->l);
	complete(&wait->comp);
	spin_unlock(&data_lock);
}

static int vdss_layer_wait_for_vsync(struct sirfsoc_vdss_layer *layer,
	void (*callback)(void*, u32))
{
	unsigned long timeout = msecs_to_jiffies(100);
	int r;
	struct layer_wait wait =  {
		.comp = COMPLETION_INITIALIZER_ONSTACK(wait.comp),
		.l = layer,
	};

	r = sirfsoc_lcdc_register_isr(layer->lcdc_id, callback, &wait,
		LCDC_INT_VSYNC);

	if (r)
		return r;

	timeout = wait_for_completion_interruptible_timeout(&wait.comp,
		timeout);

	sirfsoc_lcdc_unregister_isr(layer->lcdc_id, callback, &wait,
		LCDC_INT_VSYNC);

	if (timeout == 0)
		return -ETIMEDOUT;

	if (timeout == -ERESTARTSYS)
		return -ERESTARTSYS;

	return r;
}

static int vdss_layer_disable(struct sirfsoc_vdss_layer *layer)
{
	unsigned long flags;
	int r = 0;
	struct sirfsoc_vdss_layer_info *info;
	struct layer_priv_data *ldata;

	ldata = get_layer_data(layer);
	info = &ldata->info;

	if (info->disp_mode != VDSS_DISP_INLINE) {
		spin_lock_irqsave(&data_lock, flags);
		r = vdss_layer_disable_l(layer);
		spin_unlock_irqrestore(&data_lock, flags);
	} else
		r = vdss_layer_wait_for_vsync(layer,
			layer_disable_in_vsync);

	return r;
}

void sirfsoc_vdss_set_exclusive_layers(struct sirfsoc_vdss_layer **pLayers,
				u32 size, bool enable)
{
	int i = 0, j = 0;
	struct sirfsoc_vdss_layer *l;
	unsigned long flags;
	struct layer_priv_data *ldata;
	struct sirfsoc_vdss_layer_info *info;
	bool skip;
	enum vdss_lcdc lcdc_id;

	if (pLayers == NULL || size == 0)
		return;

	lcdc_id = pLayers[0]->lcdc_id;

	for (i = 0; i < num_layers[lcdc_id]; i++) {
		int ret = 0;

		l = &layers[lcdc_id][i];
		skip = false;

		for (j = 0; j < size; j++) {
			if (pLayers[j] == l)
				skip = true;
		}

		if (skip)
			continue;

		ldata = get_layer_data(l);
		info = &ldata->info;

		spin_lock_irqsave(&data_lock, flags);

		if (enable)
			ldata->preempted = true;

		spin_unlock_irqrestore(&data_lock, flags);

		if (ldata->func)
			ret = ldata->func(ldata->arg, !enable);

		spin_lock_irqsave(&data_lock, flags);

		if (ldata->enabled && !ret)
			vdss_set_layer_status(l, !enable);

		if (!enable)
			ldata->preempted = false;

		spin_unlock_irqrestore(&data_lock, flags);
	}
}
EXPORT_SYMBOL(sirfsoc_vdss_set_exclusive_layers);

bool sirfsoc_vdss_check_size(enum vdss_disp_mode disp_mode,
	struct vdss_surface *src_surf,
	struct vdss_rect *src_rect,
	int *psrc_skip,
	struct sirfsoc_vdss_layer *l,
	struct vdss_rect *dst_rect,
	int *pdst_skip)
{
	int scn_width, scn_height;
	struct screen_priv_data *sdata;
	int src_rect_width, src_rect_height;
	int dst_rect_width, dst_rect_height;
	int src_skip = 0;
	int dst_skip = 0;
	bool ret = false;

	sdata = get_screen_data(l->screen);
	scn_width = sdata->timings.xres;
	scn_height = sdata->timings.yres;

	src_rect_width = src_rect->right - src_rect->left + 1;
	src_rect_height = src_rect->bottom - src_rect->top + 1;
	dst_rect_width = dst_rect->right - dst_rect->left + 1;
	dst_rect_height = dst_rect->bottom - dst_rect->top + 1;

#ifdef CONFIG_SIRF_VDSS_DEBUG
	VDSSINFO("In: fmt = %d, src(%d,%d,%d,%d), dst(%d,%d,%d,%d)\n",
	  src_surf->fmt,
	  src_rect->left, src_rect->top, src_rect->right, src_rect->bottom,
	  dst_rect->left, dst_rect->top, dst_rect->right, dst_rect->bottom);
#endif

	/*
	 * If the src rect is out the range of src surface
	 * or the dst rect is out the range of the screen,
	 * they are invalid inputs, return false
	 * */
	if (src_rect->right < 0 || src_rect->bottom < 0 ||
		dst_rect->right < 0 || dst_rect->bottom < 0) {
		VDSSWARN("source or destination rect is out of range\n");
		return false;
	}

	if (src_rect->left >= src_surf->width ||
		src_rect->top >= src_surf->height ||
		dst_rect->left >= scn_width ||
		dst_rect->top >= scn_height) {
		VDSSWARN("source or destination rect is out of range\n");
		return false;
	}

	/* check and update the source rect */
	if (src_rect->left < 0) {
		dst_rect->left = dst_rect->left +
			(dst_rect_width * (-src_rect->left) /
			src_rect_width);
		src_rect->left = 0;
	}

	if (src_rect->top < 0) {
		dst_rect->top = dst_rect->top +
			(dst_rect_height * (-src_rect->top) /
			src_rect_height);
		src_rect->top = 0;
	}

	if (src_rect->right >= src_surf->width) {
		dst_rect->right = dst_rect->right -
			(dst_rect_width *
			(src_rect->right - src_surf->width + 1) /
			src_rect_width);
		src_rect->right = src_surf->width - 1;
	}

	if (src_rect->bottom >= src_surf->height) {
		dst_rect->bottom = dst_rect->bottom -
			(dst_rect_height *
			(src_rect->bottom - src_surf->height + 1) /
			src_rect_height);
		src_rect->bottom = src_surf->height - 1;
	}

	/* check and update the destination rect */
	if (dst_rect->left < 0) {
		src_rect->left = src_rect->left +
			(src_rect_width * (-dst_rect->left) /
			dst_rect_width);
		dst_rect->left = 0;
	}

	if (dst_rect->top < 0) {
		src_rect->top = src_rect->top +
			(src_rect_height * (-dst_rect->top) /
			dst_rect_height);
		dst_rect->top = 0;
	}

	if (dst_rect->right >= scn_width) {
		src_rect->right = src_rect->right -
			(src_rect_width * (dst_rect->right - scn_width + 1) /
			dst_rect_width);
		dst_rect->right = scn_width - 1;
	}

	if (dst_rect->bottom >= scn_height) {
		src_rect->bottom = src_rect->bottom -
			(src_rect_height * (dst_rect->bottom - scn_height + 1) /
			dst_rect_height);
		dst_rect->bottom = scn_height - 1;
	}

	if ((dst_rect->bottom - dst_rect->top + 1) < 2) {
		VDSSWARN("The height of dst rect is less than 2!\n");
		return false;
	}

	if (disp_mode == VDSS_DISP_NORMAL)
		ret = lcdc_check_size(src_rect, dst_rect);
	else if (disp_mode == VDSS_DISP_INLINE)
		ret = dcu_inline_check_size(src_surf,
			src_rect, &src_skip, dst_rect, &dst_skip);
	else
		ret = vpp_passthrough_check_size(src_surf,
			src_rect, &src_skip, dst_rect, &dst_skip);

	if (!ret)
		return ret;

#ifdef CONFIG_SIRF_VDSS_DEBUG
	VDSSINFO("Out: src(%d,%d,%d,%d), src_skip(%d)\n",
	   src_rect->left, src_rect->top, src_rect->right, src_rect->bottom,
	   src_skip);
	VDSSINFO("     dst(%d,%d,%d,%d), dst_skip(%d)\n",
	   dst_rect->left, dst_rect->top, dst_rect->right, dst_rect->bottom,
	   dst_skip);
#endif

	*psrc_skip = src_skip;
	*pdst_skip = dst_skip;
	return true;
}
EXPORT_SYMBOL(sirfsoc_vdss_check_size);

static void vdss_layer_flip(struct sirfsoc_vdss_layer *l, u32 srcbase)
{
	struct layer_priv_data *ldata = get_layer_data(l);
	struct sirfsoc_vdss_layer_info *info = &ldata->info;
	unsigned long flags;

	spin_lock_irqsave(&data_lock, flags);

	info->src_surf.base = srcbase;
	lcdc_flip(l->lcdc_id, l->id, info);

	spin_unlock_irqrestore(&data_lock, flags);
}

static struct sirfsoc_vdss_panel *vdss_layer_get_panel(
	struct sirfsoc_vdss_layer *layer)
{
	return layer->screen ?
		(layer->screen->output ? layer->screen->output->dst : NULL) :
		NULL;
}

static int vdss_screen_set_info(struct sirfsoc_vdss_screen *scn,
	struct sirfsoc_vdss_screen_info *info)
{
	struct screen_priv_data *sdata = get_screen_data(scn);
	unsigned long flags;
	int r;

	r = vdss_screen_check_settings(scn, info);
	if (r)
		return r;

	spin_lock_irqsave(&data_lock, flags);

	sdata->user_info = *info;
	sdata->user_info_dirty = true;

	spin_unlock_irqrestore(&data_lock, flags);

	return 0;
}

static void vdss_screen_get_info(struct sirfsoc_vdss_screen *scn,
	struct sirfsoc_vdss_screen_info *info)
{
	struct screen_priv_data *sdata = get_screen_data(scn);
	unsigned long flags;

	spin_lock_irqsave(&data_lock, flags);

	*info = sdata->user_info;

	spin_unlock_irqrestore(&data_lock, flags);
}

static int vdss_screen_wait_for_vsync(struct sirfsoc_vdss_screen *scn)
{
	void irq_handler(void *data, u32 mask)
	{
		complete((struct completion *)data);
	}

	unsigned long timeout = msecs_to_jiffies(100);
	int r;
	DECLARE_COMPLETION_ONSTACK(completion);

	if (scn->output == NULL)
		return -ENODEV;

	r = sirfsoc_lcdc_register_isr(scn->lcdc_id, irq_handler, &completion,
		LCDC_INT_VSYNC);

	if (r)
		return r;

	timeout = wait_for_completion_interruptible_timeout(&completion,
		timeout);

	sirfsoc_lcdc_unregister_isr(scn->lcdc_id, irq_handler, &completion,
		LCDC_INT_VSYNC);

	if (timeout == 0)
		return -ETIMEDOUT;

	if (timeout == -ERESTARTSYS)
		return -ERESTARTSYS;

	return r;
}

int vdss_screen_set_output(struct sirfsoc_vdss_screen *scn,
	struct sirfsoc_vdss_output *output)
{
	int r;

	mutex_lock(&apply_lock);

	if (scn->output) {
		VDSSERR("screen %s is already connected to an output\n",
			scn->name);
		r = -EINVAL;
		goto err;
	}

	if ((scn->supported_outputs & output->id) == 0) {
		VDSSERR("output does not support screen %s\n",
			scn->name);
		r = -EINVAL;
		goto err;
	}

	output->screen = scn;
	scn->output = output;

	mutex_unlock(&apply_lock);

	return 0;
err:
	mutex_unlock(&apply_lock);
	return r;
}

int vdss_screen_unset_output(struct sirfsoc_vdss_screen *scn)
{
	int r;
	struct screen_priv_data *sdata = get_screen_data(scn);
	unsigned long flags;

	mutex_lock(&apply_lock);

	if (!scn->output) {
		VDSSERR("failed to unset output, output not set\n");
		r = -EINVAL;
		goto err;
	}

	spin_lock_irqsave(&data_lock, flags);

	if (sdata->enabled) {
		VDSSERR("output can't be unset when manager is enabled\n");
		r = -EINVAL;
		goto err1;
	}

	spin_unlock_irqrestore(&data_lock, flags);

	scn->output->screen = NULL;
	scn->output = NULL;

	mutex_unlock(&apply_lock);

	return 0;
err1:
	spin_unlock_irqrestore(&data_lock, flags);
err:
	mutex_unlock(&apply_lock);

	return r;
}

void vdss_screen_set_timings(struct sirfsoc_vdss_screen *scn,
	const struct sirfsoc_video_timings *timings)
{
	unsigned long flags;
	struct screen_priv_data *sdata = get_screen_data(scn);

	spin_lock_irqsave(&data_lock, flags);

	if (sdata->updating) {
		VDSSERR("cannot set timings for %s: screen is enabled\n",
			scn->name);
		goto out;
	}

	sdata->timings = *timings;
	sdata->extra_info_dirty |= SCREEN_TIMING;
out:
	spin_unlock_irqrestore(&data_lock, flags);
}

static int vdss_screen_set_gamma(struct sirfsoc_vdss_screen *scn,
	const u8 *gamma)
{
	int i;
	struct screen_priv_data *sdata = get_screen_data(scn);
	unsigned long flags;

	if (gamma == NULL)
		return -EFAULT;

	sdata->have_gamma = true;
	spin_lock_irqsave(&data_lock, flags);

	for (i = 0; i < 256 * 3; i++)
		sdata->gamma[i] = *(gamma + i);

	sdata->extra_info_dirty |= SCREEN_GAMMA;

	spin_unlock_irqrestore(&data_lock, flags);

	return 0;
}

static int vdss_screen_get_gamma(struct sirfsoc_vdss_screen *scn,
	u8 *gamma)
{
	int i;
	struct screen_priv_data *sdata = get_screen_data(scn);
	unsigned long flags;

	if (gamma == NULL)
		return -EFAULT;

	spin_lock_irqsave(&data_lock, flags);

	for (i = 0; i < 256 * 3; i++)
		*(gamma + i) = sdata->gamma[i];

	spin_unlock_irqrestore(&data_lock, flags);

	return 0;
}

static void vdss_screen_set_error_diffusion(struct sirfsoc_vdss_screen *scn,
	bool error_diffusion)
{
	unsigned long flags;
	struct screen_priv_data *sdata = get_screen_data(scn);

	spin_lock_irqsave(&data_lock, flags);

	sdata->error_diffusion = error_diffusion;
	sdata->extra_info_dirty |= SCREEN_ERROR_DIFFUSION;

	spin_unlock_irqrestore(&data_lock, flags);
}

void vdss_screen_set_data_lines(struct sirfsoc_vdss_screen *scn,
	int data_lines)
{
	unsigned long flags;
	struct screen_priv_data *sdata = get_screen_data(scn);

	spin_lock_irqsave(&data_lock, flags);

	if (sdata->enabled) {
		VDSSERR("cannot set data lines for %s: screen is enabled\n",
			scn->name);
		goto out;
	}

	sdata->data_lines = data_lines;
	sdata->extra_info_dirty |= SCREEN_DATA_LINES;
out:
	spin_unlock_irqrestore(&data_lock, flags);
}

int vdss_screen_enable(struct sirfsoc_vdss_screen *scn)
{
	struct screen_priv_data *sdata = get_screen_data(scn);
	unsigned long flags;
	int r;

	mutex_lock(&apply_lock);

	if (sdata->enabled)
		goto out;

	spin_lock_irqsave(&data_lock, flags);

	sdata->enabled = true;

	r = vdss_check_settings(scn);
	if (r) {
		VDSSERR("failed to enable screen %d: check_settings failed\n",
			scn->id);
		goto err;
	}

	vdss_update_regs(scn->lcdc_id);

	spin_unlock_irqrestore(&data_lock, flags);
out:
	mutex_unlock(&apply_lock);

	return 0;

err:
	sdata->enabled = false;
	spin_unlock_irqrestore(&data_lock, flags);
	mutex_unlock(&apply_lock);
	return r;
}

void vdss_screen_disable(struct sirfsoc_vdss_screen *scn)
{
	struct screen_priv_data *sdata = get_screen_data(scn);
	unsigned long flags;

	mutex_lock(&apply_lock);

	if (!sdata->enabled)
		goto out;

	spin_lock_irqsave(&data_lock, flags);

	sdata->updating = false;
	sdata->enabled = false;

	spin_unlock_irqrestore(&data_lock, flags);

out:
	mutex_unlock(&apply_lock);
}

static void sirfsoc_vdss_layer_info_apply(struct sirfsoc_vdss_layer *layer)
{
	struct layer_priv_data *ldata;

	ldata = get_layer_data(layer);

	if (!ldata->user_info_dirty)
		return;

	ldata->user_info_dirty = false;
	ldata->info_dirty = true;
	ldata->info = ldata->user_info;
}

static void sirfsoc_vdss_screen_info_apply(struct sirfsoc_vdss_screen *scn)
{
	struct screen_priv_data *sdata;

	sdata = get_screen_data(scn);

	if (!sdata->user_info_dirty)
		return;

	sdata->user_info_dirty = false;
	sdata->info_dirty = true;
	sdata->info = sdata->user_info;
}

static int sirfsoc_vdss_screen_apply(struct sirfsoc_vdss_screen *scn)
{
	unsigned long flags;
	struct sirfsoc_vdss_layer *l;
	int r;

	VDSSDBG("sirfsoc_vdss_scn_apply(%s)\n", scn->name);

	spin_lock_irqsave(&data_lock, flags);

	r = vdss_check_settings(scn);
	if (r) {
		spin_unlock_irqrestore(&data_lock, flags);
		VDSSERR("failed to apply settings: illegal configuration.\n");
		return r;
	}

	/* Configure overlays */
	list_for_each_entry(l, &scn->layers, list)
		sirfsoc_vdss_layer_info_apply(l);

	/* Configure manager */
	sirfsoc_vdss_screen_info_apply(scn);

	vdss_update_regs(scn->lcdc_id);

	spin_unlock_irqrestore(&data_lock, flags);

	return 0;
}

int vdss_init_screens(u32 lcdc_index)
{
	int i;
	struct lcdc_prop *p = lcdc_get_prop(lcdc_index);

	num_screens[lcdc_index] = NUM_SCREENS_PER_LCDC;

	screens[lcdc_index] = kzalloc(sizeof(struct sirfsoc_vdss_screen) *
		num_screens[lcdc_index], GFP_KERNEL);

	BUG_ON(screens == NULL);


	for (i = 0; i < num_screens[lcdc_index]; ++i) {
		struct sirfsoc_vdss_screen *scn = &screens[lcdc_index][i];
		struct screen_priv_data *sdata;

		switch (i) {
		case 0:
			scn->name = "screen0";
			scn->id = SIRFSOC_VDSS_SCREEN0;
			if (SIRFSOC_VDSS_LCDC0 == lcdc_index)
				scn->supported_outputs =
					SIRFSOC_VDSS_OUTPUT_RGB |
					SIRFSOC_VDSS_OUTPUT_LVDS1;
			else
				scn->supported_outputs =
					SIRFSOC_VDSS_OUTPUT_LVDS2;
		}
		/*
		 * Sometimes the default setting of screen is applicable for
		 * vdss client, so it will not set screen info directly. But
		 * screen regs need have the chance to be intialized. So flag
		 * as dirty for the very first time.
		 */
		scn->lcdc_id = lcdc_index;
		sdata = get_screen_data(scn);
		sdata->user_info.top_layer = SIRFSOC_VDSS_LAYER3;
		sdata->user_info.back_color = 0;
		sdata->user_info_dirty = true;
		for (i = 0; i < 256; i++) {
			sdata->gamma[i] = i;
			sdata->gamma[256 + i] = i;
			sdata->gamma[512 + i] = i;
		}
		sdata->error_diffusion = p->error_diffusion;
		sdata->extra_info_dirty |= SCREEN_ERROR_DIFFUSION;
		scn->caps = 0;
		INIT_LIST_HEAD(&scn->layers);
		scn->apply = sirfsoc_vdss_screen_apply;
		scn->set_info = vdss_screen_set_info;
		scn->get_info = vdss_screen_get_info;
		scn->wait_for_vsync = vdss_screen_wait_for_vsync;
		scn->set_gamma = vdss_screen_set_gamma;
		scn->get_gamma = vdss_screen_get_gamma;
		scn->set_err_diff = vdss_screen_set_error_diffusion;
	}

	return 0;
}

void vdss_uninit_screens(u32 lcdc_index)
{
	kfree(screens[lcdc_index]);
	screens[lcdc_index] = NULL;
	num_screens[lcdc_index] = 0;
}

void vdss_init_layers(u32 lcdc_index)
{
	int i;

	num_layers[lcdc_index] = NUM_LAYERS_PER_LCDC;

	layers[lcdc_index] = kzalloc(sizeof(struct sirfsoc_vdss_layer) *
		num_layers[lcdc_index], GFP_KERNEL);

	BUG_ON(layers == NULL);

	for (i = 0; i < num_layers[lcdc_index]; ++i) {
		struct sirfsoc_vdss_layer *l = &layers[lcdc_index][i];

		switch (i) {
		case 0:
			l->name = "layer0";
			l->id = SIRFSOC_VDSS_LAYER0;
			break;
		case 1:
			l->name = "layer1";
			l->id = SIRFSOC_VDSS_LAYER1;
			break;
		case 2:
			l->name = "layer2";
			l->id = SIRFSOC_VDSS_LAYER2;
			break;
		case 3:
			l->name = "layer3";
			l->id = SIRFSOC_VDSS_LAYER3;
			break;
		}

		l->lcdc_id = lcdc_index;
		l->caps = 0;
		l->supported_fmts = 0;
		l->is_enabled = vdss_layer_is_enabled;
		l->is_preempted = vdss_layer_is_preempted;
		l->enable = vdss_layer_enable;
		l->disable = vdss_layer_disable;
		l->register_notify = vdss_layer_register_notify;
		l->set_screen = vdss_layer_set_screen;
		l->unset_screen = vdss_layer_unset_screen;
		l->set_info = vdss_layer_set_info;
		l->get_info = vdss_layer_get_info;
		l->get_panel = vdss_layer_get_panel;
		l->flip = vdss_layer_flip;
	}
}

void vdss_uninit_layers(u32 lcdc_index)
{
	kfree(layers[lcdc_index]);
	layers[lcdc_index] = NULL;
	num_layers[lcdc_index] = 0;
}
