/*
 * CSR sirfsoc vdss composition header file
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
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
	bool passthrough;
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

	u32 num_displays;
	struct vdsscomp_display_data displays[MAX_DISPLAYS];
};

#ifdef CONFIG_VDSSCOMP_DEBUG
static void print_vdss_layer_info(struct sirfsoc_vdss_layer_info *info)
{
	pr_info("fmt %d\n", info->fmt);
	pr_info("src_rect (%d, %d, %d, %d)\n",
		info->src_rect.left, info->src_rect.top,
		info->src_rect.right, info->src_rect.bottom);
	pr_info("dst_rect (%d, %d, %d, %d)\n",
		info->dst_rect.left, info->dst_rect.top,
		info->dst_rect.right, info->dst_rect.bottom);
	pr_info("width %d, height %d\n", info->surf_width, info->surf_height);
	pr_info("pre_mult_alpha %d, source_alpha %d\n", info->pre_mult_alpha,
		info->source_alpha);
}
#else
static void print_vdss_layer_info(struct sirfsoc_vdss_layer_info *info)
{
}
#endif

#endif
