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

#define DSS_SUBSYS_NAME "DISPLAY"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include <video/sirfsoc_vdss.h>

#include "vdss.h"

static void sirfsoc_vdss_default_get_resolution(
	struct sirfsoc_vdss_panel *panel,
	u16 *xres, u16 *yres)
{
	*xres = panel->timings.xres;
	*yres = panel->timings.yres;
}

static int sirfsoc_vdss_default_get_recommended_bpp(
	struct sirfsoc_vdss_panel *panel)
{
	switch (panel->type) {
	case SIRFSOC_PANEL_RGB:
	case SIRFSOC_PANEL_HDMI:
		return 24;

	case SIRFSOC_PANEL_LVDS:
		/* FIXME: the bpp should depend on the data_lines,
		 * but we return 24 always */
		return 24;

	default:
		BUG();
		return 0;
	}
}

static void sirfsoc_vdss_default_get_timings(
	struct sirfsoc_vdss_panel *panel,
	struct sirfsoc_video_timings *timings)
{
	*timings = panel->timings;
}

int vdss_suspend_all_panels(void)
{
	struct sirfsoc_vdss_panel *panel = NULL;

	for_each_vdss_panel(panel) {
		if (!panel->driver)
			continue;

		if (panel->state == SIRFSOC_VDSS_PANEL_ENABLED) {
			panel->driver->disable(panel);
			panel->activate_after_resume = true;
		} else {
			panel->activate_after_resume = false;
		}
	}
	return 0;
}

int vdss_resume_all_panels(void)
{
	struct sirfsoc_vdss_panel *panel = NULL;
	int i;

	for_each_vdss_panel(panel) {
		if (!panel->driver)
			continue;

		if (panel->activate_after_resume) {
			panel->driver->enable(panel);
			panel->activate_after_resume = false;
		}
	}

	for (i = 0; i < sirfsoc_vdss_get_num_lcdc(); i++)
		vdss_restore_screen_layer(i);
	return 0;
}

void vdss_disable_all_panels(void)
{
	struct sirfsoc_vdss_panel *panel = NULL;

	for_each_vdss_panel(panel) {
		if (!panel->driver)
			continue;

		if (panel->state == SIRFSOC_VDSS_PANEL_ENABLED)
			panel->driver->disable(panel);
	}
}

static LIST_HEAD(panel_list);
static DEFINE_MUTEX(panel_list_mutex);
static int disp_num_counter;

int sirfsoc_vdss_register_panel(struct sirfsoc_vdss_panel *panel)
{
	struct sirfsoc_vdss_driver *drv = panel->driver;
	int id;

	if (panel->dev->of_node) {
		id = of_alias_get_id(panel->dev->of_node, "display");
		if (id < 0)
			id = disp_num_counter++;
	} else {
		id = disp_num_counter++;
	}

	snprintf(panel->alias, sizeof(panel->alias),
		"display%d", id);

	panel->name = panel->alias;

	if (drv && drv->get_resolution == NULL)
		drv->get_resolution = sirfsoc_vdss_default_get_resolution;
	if (drv && drv->get_recommended_bpp == NULL)
		drv->get_recommended_bpp =
			sirfsoc_vdss_default_get_recommended_bpp;
	if (drv && drv->get_timings == NULL)
		drv->get_timings = sirfsoc_vdss_default_get_timings;

	mutex_lock(&panel_list_mutex);
	list_add_tail(&panel->list, &panel_list);
	mutex_unlock(&panel_list_mutex);
	return 0;
}
EXPORT_SYMBOL(sirfsoc_vdss_register_panel);

void sirfsoc_vdss_unregister_panel(struct sirfsoc_vdss_panel *panel)
{
	mutex_lock(&panel_list_mutex);
	list_del(&panel->list);
	mutex_unlock(&panel_list_mutex);
}
EXPORT_SYMBOL(sirfsoc_vdss_unregister_panel);

struct sirfsoc_vdss_panel *sirfsoc_vdss_get_panel(
	struct sirfsoc_vdss_panel *panel)
{
	if (!try_module_get(panel->owner))
		return NULL;

	if (get_device(panel->dev) == NULL) {
		module_put(panel->owner);
		return NULL;
	}

	return panel;
}
EXPORT_SYMBOL(sirfsoc_vdss_get_panel);

void sirfsoc_vdss_put_panel(struct sirfsoc_vdss_panel *panel)
{
	put_device(panel->dev);
	module_put(panel->owner);
}
EXPORT_SYMBOL(sirfsoc_vdss_put_panel);


#define PRIMARY_DISPLAY		"display0"
#define SECONDARY_DISPLAY	"display1"

struct sirfsoc_vdss_panel *sirfsoc_vdss_get_primary_device(void)
{
	struct list_head *l;
	struct sirfsoc_vdss_panel *panel = NULL;
	struct sirfsoc_vdss_panel *p;

	mutex_lock(&panel_list_mutex);

	if (list_empty(&panel_list))
		goto out;

	list_for_each(l, &panel_list) {
		p = list_entry(l, struct sirfsoc_vdss_panel, list);
		if (p->name && strcmp(PRIMARY_DISPLAY, p->name) == 0) {
			panel = p;
			goto out;
		}
	}
out:
	mutex_unlock(&panel_list_mutex);
	if (panel == NULL)
		pr_err("No primary display device found\n");

	return panel;
}
EXPORT_SYMBOL(sirfsoc_vdss_get_primary_device);

struct sirfsoc_vdss_panel *sirfsoc_vdss_get_secondary_device(void)
{
	struct list_head *l;
	struct sirfsoc_vdss_panel *panel = NULL;
	struct sirfsoc_vdss_panel *p;

	mutex_lock(&panel_list_mutex);

	if (list_empty(&panel_list))
		goto out;

	list_for_each(l, &panel_list) {
		p = list_entry(l, struct sirfsoc_vdss_panel, list);
		if (p->name && strcmp(SECONDARY_DISPLAY, p->name) == 0) {
			panel = p;
			goto out;
		}
	}

out:
	mutex_unlock(&panel_list_mutex);
	return panel;
}
EXPORT_SYMBOL(sirfsoc_vdss_get_secondary_device);

/*
 * ref count of the found device is incremented.
 * ref count of from-device is decremented.
 */
struct sirfsoc_vdss_panel *sirfsoc_vdss_get_next_panel(
	struct sirfsoc_vdss_panel *from)
{
	struct list_head *l;
	struct sirfsoc_vdss_panel *panel;

	mutex_lock(&panel_list_mutex);

	if (list_empty(&panel_list)) {
		panel = NULL;
		goto out;
	}

	if (from == NULL) {
		panel = list_first_entry(&panel_list, struct sirfsoc_vdss_panel,
			list);
		sirfsoc_vdss_get_panel(panel);
		goto out;
	}

	sirfsoc_vdss_put_panel(from);

	list_for_each(l, &panel_list) {
		panel = list_entry(l, struct sirfsoc_vdss_panel, list);
		if (panel == from) {
			if (list_is_last(l, &panel_list)) {
				panel = NULL;
				goto out;
			}

			panel = list_entry(l->next, struct sirfsoc_vdss_panel,
				list);
			sirfsoc_vdss_get_panel(panel);
			goto out;
		}
	}

	WARN(1, "'from' panel not found\n");

	panel = NULL;
out:
	mutex_unlock(&panel_list_mutex);
	return panel;
}
EXPORT_SYMBOL(sirfsoc_vdss_get_next_panel);

struct sirfsoc_vdss_panel *sirfsoc_vdss_find_panel(void *data,
		int (*match)(struct sirfsoc_vdss_panel *panel, void *data))
{
	struct sirfsoc_vdss_panel *panel = NULL;

	while ((panel = sirfsoc_vdss_get_next_panel(panel)) != NULL) {
		if (match(panel, data))
			return panel;
	}

	return NULL;
}
EXPORT_SYMBOL(sirfsoc_vdss_find_panel);

void videomode_to_sirfsoc_video_timings(const struct videomode *vm,
	struct sirfsoc_video_timings *timings)
{
	memset(timings, 0, sizeof(*timings));

	timings->pixel_clock = vm->pixelclock;
	timings->xres = vm->hactive;
	timings->hbp = vm->hback_porch;
	timings->hfp = vm->hfront_porch;
	timings->hsw = vm->hsync_len;
	timings->yres = vm->vactive;
	timings->vbp = vm->vback_porch;
	timings->vfp = vm->vfront_porch;
	timings->vsw = vm->vsync_len;

	timings->vsync_level = vm->flags & DISPLAY_FLAGS_VSYNC_HIGH ?
		SIRFSOC_VDSS_SIG_ACTIVE_HIGH :
		SIRFSOC_VDSS_SIG_ACTIVE_LOW;
	timings->hsync_level = vm->flags & DISPLAY_FLAGS_HSYNC_HIGH ?
		SIRFSOC_VDSS_SIG_ACTIVE_HIGH :
		SIRFSOC_VDSS_SIG_ACTIVE_LOW;
	timings->de_level = vm->flags & DISPLAY_FLAGS_DE_HIGH ?
		SIRFSOC_VDSS_SIG_ACTIVE_HIGH :
		SIRFSOC_VDSS_SIG_ACTIVE_LOW;
	timings->pclk_edge = vm->flags & DISPLAY_FLAGS_PIXDATA_POSEDGE ?
		SIRFSOC_VDSS_SIG_RISING_EDGE :
		SIRFSOC_VDSS_SIG_FALLING_EDGE;

}
EXPORT_SYMBOL(videomode_to_sirfsoc_video_timings);

void sirfsoc_video_timings_to_videomode(
	const struct sirfsoc_video_timings *timings,
	struct videomode *vm)
{
	memset(vm, 0, sizeof(*vm));

	vm->pixelclock = timings->pixel_clock;

	vm->hactive = timings->xres;
	vm->hback_porch = timings->hbp;
	vm->hfront_porch = timings->hfp;
	vm->hsync_len = timings->hsw;
	vm->vactive = timings->yres;
	vm->vback_porch = timings->vbp;
	vm->vfront_porch = timings->vfp;
	vm->vsync_len = timings->vsw;

	if (timings->hsync_level == SIRFSOC_VDSS_SIG_ACTIVE_HIGH)
		vm->flags |= DISPLAY_FLAGS_HSYNC_HIGH;
	else
		vm->flags |= DISPLAY_FLAGS_HSYNC_LOW;

	if (timings->vsync_level == SIRFSOC_VDSS_SIG_ACTIVE_HIGH)
		vm->flags |= DISPLAY_FLAGS_VSYNC_HIGH;
	else
		vm->flags |= DISPLAY_FLAGS_VSYNC_LOW;

	if (timings->de_level == SIRFSOC_VDSS_SIG_ACTIVE_HIGH)
		vm->flags |= DISPLAY_FLAGS_DE_HIGH;
	else
		vm->flags |= DISPLAY_FLAGS_DE_LOW;

	if (timings->pclk_edge == SIRFSOC_VDSS_SIG_RISING_EDGE)
		vm->flags |= DISPLAY_FLAGS_PIXDATA_POSEDGE;
	else
		vm->flags |= DISPLAY_FLAGS_PIXDATA_NEGEDGE;
}
EXPORT_SYMBOL(sirfsoc_video_timings_to_videomode);
