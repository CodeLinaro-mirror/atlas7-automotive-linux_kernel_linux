/*
 * CSR sirfsoc framebuffer header file
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

#ifndef __SIRFSOC_FB_H
#define __SIRFSOC_FB_H

#include <linux/rwsem.h>
#include <linux/dma-attrs.h>
#include <linux/dma-mapping.h>

#include <video/sirfsoc_vdss.h>


enum SIRFSOCFB_FORMAT {
	FORMAT_ARGB_8888 = 0,
	FORMAT_RGB_565,
	FORMAT_YUV_422,
};

/* ------------------kernel only---------------------------------- */
#ifdef DEBUG
#define DBG(format, ...) \
	pr_info("SIRFSOCFB: " format, ## __VA_ARGS__)
#else
#define DBG(format, ...)
#endif

#define FB2SFB(fb_info) ((struct sirfsocfb_info *)(fb_info->par))

/* max number of overlays to which a framebuffer data can be direct */
#define SIRFSOCFB_MAX_LAYER_PER_FB 3

#define MAX_DISPLAY_NUM	2


#define SIRFSOCFB_PLANE_XRES_MIN		8
#define SIRFSOCFB_PLANE_YRES_MIN		8

struct sirfsocfb_mem_region {
	int             id;
	struct dma_attrs attrs;
	void		*token;
	dma_addr_t	dma_handle;
	u32		paddr;
	void __iomem	*vaddr;
	unsigned long	size;
	u8		type;		/* OMAPFB_PLANE_MEM_* */
	bool		alloc;		/* allocated by the driver */
	bool		map;		/* kernel mapped by the driver */
	atomic_t	map_count;
	struct rw_semaphore lock;
	atomic_t	lock_count;
};

/* appended to fb_info */
struct sirfsocfb_info {
	int id;
	struct sirfsocfb_mem_region *region;
	int num_layers;
	struct sirfsoc_vdss_layer *layers[SIRFSOCFB_MAX_LAYER_PER_FB];
	struct sirfsocfb_device *fbdev;
};

/* display data per lcdc */
struct sirfsocfb_display_data {
	unsigned lcdc_index;

	unsigned num_layers;
	struct sirfsoc_vdss_layer *layers[10];
	unsigned num_screens;
	struct sirfsoc_vdss_screen *screens[10];

	struct sirfsoc_vdss_panel *panel;
};

struct sirfsocfb_device {
	struct device *dev;
	struct mutex  mtx;

	u32 pseudo_palette[17];

	int state;

	unsigned num_fbs;
	struct fb_info *fbs[10];
	struct sirfsocfb_mem_region regions[10];

	unsigned num_displays;
	struct sirfsocfb_display_data displays[MAX_DISPLAY_NUM];
};

struct sirfsocfb_colormode {
	enum vdss_pixelformat fmt;
	u32 bits_per_pixel;
	u32 nonstd;
	struct fb_bitfield red;
	struct fb_bitfield green;
	struct fb_bitfield blue;
	struct fb_bitfield transp;
};

/* find the panel connected to this fb, if any */
static inline struct sirfsoc_vdss_panel *fb2display(struct fb_info *fbi)
{
	struct sirfsocfb_info *sfbi = FB2SFB(fbi);
	struct sirfsoc_vdss_layer *l;

	/* returns the panel connected to first attached layer */

	if (sfbi->num_layers == 0)
		return NULL;

	l = sfbi->layers[0];

	return l->get_panel(l);
}

static inline struct sirfsocfb_display_data *get_display_data(
	struct sirfsocfb_device *fbdev, struct sirfsoc_vdss_panel *panel)
{
	int i;

	for (i = 0; i < fbdev->num_displays; ++i)
		if (fbdev->displays[i].panel == panel)
			return &fbdev->displays[i];

	/* This should never happen */
	BUG();
	return NULL;
}

static inline void sirfsocfb_lock(struct sirfsocfb_device *fbdev)
{
	mutex_lock(&fbdev->mtx);
}

static inline void sirfsocfb_unlock(struct sirfsocfb_device *fbdev)
{
	mutex_unlock(&fbdev->mtx);
}


static inline int sirfsocfb_layer_enable(struct sirfsoc_vdss_layer *l,
	int enable)
{
	if (enable)
		return l->enable(l);

	return l->disable(l);
}

static inline struct sirfsocfb_mem_region *
	sirfsocfb_get_mem_region(struct sirfsocfb_mem_region *rg)
{
	down_read_nested(&rg->lock, rg->id);
	atomic_inc(&rg->lock_count);
	return rg;
}

static inline void sirfsocfb_put_mem_region(struct sirfsocfb_mem_region *rg)
{
	atomic_dec(&rg->lock_count);
	up_read(&rg->lock);
}
#endif
