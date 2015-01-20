/*
 * CSR sirfsoc vdss core file
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc
 * group company.
 * Licensed under GPLv2 or later.
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
	struct sirfsoc_vdss_screen *scn)
{
	int i = 0;
	struct sirfsoc_vdss_layer *l;

	for (i = 0; i < num_layers[scn->lcdc_id]; i++) {
		l = &layers[scn->lcdc_id][i];
		if ((l->screen->id == scn->id) && !l->is_enabled(l)) {
			if (l->screen)
				l->unset_screen(l);
			if (l->set_screen(l, scn))
				return NULL;
			return l;
		}
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

	bool enabled;

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

	bool extra_info_dirty;
	bool shadow_extra_info_dirty;

	struct sirfsoc_video_timings timings;
	int data_lines;
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

static void vdss_layer_update_regs_extra(struct sirfsoc_vdss_layer *l)
{
	struct layer_priv_data *ldata = get_layer_data(l);
	struct screen_priv_data *sdata;

	VDSSDBG("writing layer %d regs extra", l->id);

	if (!ldata->extra_info_dirty)
		return;

	/* note: write also when op->enabled == false, so that the ovl gets
	 * disabled */

	lcdc_layer_enable(l->lcdc_id, l->id, ldata->enabled,
		ldata->info.passthrough);

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

static void vdss_screen_update_regs_extra(struct sirfsoc_vdss_screen *scn)
{
	struct screen_priv_data *sdata = get_screen_data(scn);

	VDSSDBG("writing screen %d regs extra", scn->id);

	if (!sdata->extra_info_dirty)
		return;

	lcdc_screen_set_timings(scn->lcdc_id, scn->id, &sdata->timings);

	sdata->extra_info_dirty = false;
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

static int vdss_layer_enable(struct sirfsoc_vdss_layer *layer)
{
	struct layer_priv_data *ldata = get_layer_data(layer);
	unsigned long flags;
	int r;

	mutex_lock(&apply_lock);

	if (ldata->enabled) {
		r = 0;
		goto err1;
	}

	if (layer->screen == NULL || layer->screen->output == NULL) {
		r = -EINVAL;
		goto err1;
	}

	spin_lock_irqsave(&data_lock, flags);

	ldata->enabling = true;

	r = vdss_check_settings(layer->screen);
	if (r) {
		VDSSERR("failed to enable layer %d: check_settings failed\n",
			layer->id);
		goto err2;
	}

	ldata->enabling = false;
	vdss_apply_layer_enable(layer, true);

	vdss_update_regs(layer->lcdc_id);

	spin_unlock_irqrestore(&data_lock, flags);

	mutex_unlock(&apply_lock);

	return 0;
err2:
	ldata->enabling = false;
	spin_unlock_irqrestore(&data_lock, flags);
err1:
	mutex_unlock(&apply_lock);
	return r;
}

static int vdss_layer_disable(struct sirfsoc_vdss_layer *layer)
{
	struct layer_priv_data *ldata = get_layer_data(layer);
	unsigned long flags;
	int r;

	mutex_lock(&apply_lock);

	if (!ldata->enabled) {
		r = 0;
		goto err;
	}

	if (layer->screen == NULL || layer->screen->output == NULL) {
		r = -EINVAL;
		goto err;
	}

	spin_lock_irqsave(&data_lock, flags);

	vdss_apply_layer_enable(layer, false);
	vdss_update_regs(layer->lcdc_id);

	spin_unlock_irqrestore(&data_lock, flags);

	mutex_unlock(&apply_lock);

	return 0;

err:
	mutex_unlock(&apply_lock);
	return r;
}

bool vdss_layer_flip(struct sirfsoc_vdss_layer *l, u32 srcbase)
{
	struct layer_priv_data *ldata = get_layer_data(l);
	struct sirfsoc_vdss_layer_info *info = &ldata->info;

	info->base = srcbase;

	return lcdc_flip(l->lcdc_id, l->id, info);
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
	sdata->extra_info_dirty = true;
out:
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
	sdata->extra_info_dirty = true;
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
		sdata->user_info_dirty = true;
		scn->caps = 0;
		INIT_LIST_HEAD(&scn->layers);
		scn->apply = sirfsoc_vdss_screen_apply;
		scn->set_info = vdss_screen_set_info;
		scn->get_info = vdss_screen_get_info;
		scn->wait_for_vsync = vdss_screen_wait_for_vsync;
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
		l->enable = vdss_layer_enable;
		l->disable = vdss_layer_disable;
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
