/*
 * CSR sirfsoc vdss composition header file
 *
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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

#ifndef __VDSSCOMP_H
#define __VDSSCOMP_H

#include <linux/miscdevice.h>

#define MAX_LAYERS	4
#define MAX_SCREENS	1
#define MAX_DISPLAYS	2

#define DEV(c)		(c->dev.this_device)

/* gralloc composition sync object */
struct vdsscomp_sync {
	struct work_struct work;
	void (*cb_fn)(void *, int);
	void *cb_arg;
	struct list_head list;
};

struct vdsscomp_layer_data {
	struct sirfsoc_vdss_layer *layer;
	void *vpp;
	enum vdss_disp_mode disp_mode;
	bool preempted;
};

/* display data per lcdc */
struct vdsscomp_display_data {
	unsigned lcdc_index;

	unsigned num_layers;
	struct vdsscomp_layer_data layers[MAX_LAYERS];
	unsigned num_screens;
	struct sirfsoc_vdss_screen *screens[MAX_SCREENS];

	struct sirfsoc_vdss_panel *panel;

};


/**
 * VDSS Composition Device Driver
 *
 * @pdev:  hook for platform device data
 * @dev:   misc device base
 */
struct vdsscomp_dev {
	struct device *pdev;
	struct miscdevice dev;

	struct list_head flip_list;
	spinlock_t flip_lock;

	struct workqueue_struct *sync_wkq;
#ifdef CONFIG_ANDROID
	ktime_t vsync_timestamp;
	struct work_struct vsync_work;
#endif

	u32 num_displays;
	struct vdsscomp_display_data displays[MAX_DISPLAYS];
};

#ifdef CONFIG_VDSSCOMP_DEBUG
static void print_vdss_layer_info(struct sirfsoc_vdss_layer_info *info)
{
	pr_info("fmt %d\n", info->src_surf.fmt);
	pr_info("src_rect (%d, %d, %d, %d)\n",
		info->src_rect.left, info->src_rect.top,
		info->src_rect.right, info->src_rect.bottom);
	pr_info("dst_rect (%d, %d, %d, %d)\n",
		info->dst_rect.left, info->dst_rect.top,
		info->dst_rect.right, info->dst_rect.bottom);
	pr_info("width %d, height %d\n", info->src_surf.width,
		info->src_surf.height);
	pr_info("pre_mult_alpha %d, source_alpha %d\n", info->pre_mult_alpha,
		info->source_alpha);
}
#else
static void print_vdss_layer_info(struct sirfsoc_vdss_layer_info *info)
{
}
#endif

#endif
