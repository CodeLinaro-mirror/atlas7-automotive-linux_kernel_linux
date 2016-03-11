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
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <video/sirfsoc_vdss.h>

#include "vdss.h"

static LIST_HEAD(output_list);
static DEFINE_MUTEX(output_lock);

int sirfsoc_vdss_output_set_panel(struct sirfsoc_vdss_output *out,
	struct sirfsoc_vdss_panel *panel)
{
	int r;

	mutex_lock(&output_lock);

	if (out->dst) {
		VDSSERR("output already has device %s connected to it\n",
			out->dst->name);
		r = -EINVAL;
		goto err;
	}

	if (!(out->supported_panel && panel->type)) {
		VDSSERR("output type and display type don't match\n");
		r = -EINVAL;
		goto err;
	}

	out->dst = panel;
	panel->src = out;

	mutex_unlock(&output_lock);

	return 0;
err:
	mutex_unlock(&output_lock);

	return r;
}
EXPORT_SYMBOL(sirfsoc_vdss_output_set_panel);

int sirfsoc_vdss_output_unset_panel(struct sirfsoc_vdss_output *out)
{
	int r;

	mutex_lock(&output_lock);

	if (!out->dst) {
		VDSSERR("output doesn't have a device connected to it\n");
		r = -EINVAL;
		goto err;
	}

	if (out->dst->state != SIRFSOC_VDSS_PANEL_DISABLED) {
		VDSSERR("device %s is not disabled, cannot unset device\n",
			out->dst->name);
		r = -EINVAL;
		goto err;
	}

	out->dst->src = NULL;
	out->dst = NULL;

	mutex_unlock(&output_lock);

	return 0;
err:
	mutex_unlock(&output_lock);

	return r;
}
EXPORT_SYMBOL(sirfsoc_vdss_output_unset_panel);

int sirfsoc_vdss_register_output(struct sirfsoc_vdss_output *out)
{
	list_add_tail(&out->list, &output_list);
	return 0;
}
EXPORT_SYMBOL(sirfsoc_vdss_register_output);

void sirfsoc_vdss_unregister_output(struct sirfsoc_vdss_output *out)
{
	list_del(&out->list);
}
EXPORT_SYMBOL(sirfsoc_vdss_unregister_output);

struct sirfsoc_vdss_output *sirfsoc_vdss_get_output(enum vdss_output id)
{
	struct sirfsoc_vdss_output *out;

	list_for_each_entry(out, &output_list, list) {
		if (out->id == id)
			return out;
	}

	return NULL;
}
EXPORT_SYMBOL(sirfsoc_vdss_get_output);

struct sirfsoc_vdss_output *sirfsoc_vdss_find_output(const char *name)
{
	struct sirfsoc_vdss_output *out;

	list_for_each_entry(out, &output_list, list) {
		if (strcmp(out->name, name) == 0)
			return out;
	}

	return NULL;
}
EXPORT_SYMBOL(sirfsoc_vdss_find_output);

struct sirfsoc_vdss_output *sirfsoc_vdss_find_output_from_panel(
	struct sirfsoc_vdss_panel *panel)
{
	struct sirfsoc_vdss_output *out;

	out = panel->src;

	if (out != NULL && out->id != 0)
		return out;

	return NULL;
}
EXPORT_SYMBOL(sirfsoc_vdss_find_output_from_panel);

struct sirfsoc_vdss_screen *sirfsoc_vdss_find_screen_from_panel
	(struct sirfsoc_vdss_panel *panel)
{
	struct sirfsoc_vdss_output *out;
	struct sirfsoc_vdss_screen *scn;

	out = sirfsoc_vdss_find_output_from_panel(panel);

	if (out == NULL)
		return NULL;

	scn = out->screen;

	return scn;
}
EXPORT_SYMBOL(sirfsoc_vdss_find_screen_from_panel);
