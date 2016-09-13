/*
 * linux/include/video/vdsscomp.h
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

#ifndef __LINUX_VDSSCOMP_H
#define __LINUX_VDSSCOMP_H

#define MAX_LAYERS	4

enum vdsscomp_display_type {
	VDSSCOMP_DISPLAY_NONE = 0x0,
	VDSSCOMP_DISPLAY_RGB = 0x1,
	VDSSCOMP_DISPLAY_HDMI = 0x2,
	VDSSCOMP_DISPLAY_LVDS = 0x4,
};

enum vdsscomp_layer {
	VDSSCOMP_LAYER0 = 0,
	VDSSCOMP_LAYER1,
	VDSSCOMP_LAYER2,
	VDSSCOMP_LAYER3,
	VDSSCOMP_CURSOR = 6,
};

/* stay the same with vdss_pixelformat */
enum vdsscomp_pixelformat {
	VDSSCOMP_PIXELFORMAT_UNKNOWN = 0,

	VDSSCOMP_PIXELFORMAT_1BPP = 1,
	VDSSCOMP_PIXELFORMAT_2BPP = 2,
	VDSSCOMP_PIXELFORMAT_4BPP = 3,
	VDSSCOMP_PIXELFORMAT_8BPP = 4,

	VDSSCOMP_PIXELFORMAT_565 = 5,
	VDSSCOMP_PIXELFORMAT_5551 = 6,
	VDSSCOMP_PIXELFORMAT_4444 = 7,
	VDSSCOMP_PIXELFORMAT_5550 = 8,
	VDSSCOMP_PIXELFORMAT_BGRX_8880 = 9,
	VDSSCOMP_PIXELFORMAT_8888 = 10,

	VDSSCOMP_PIXELFORMAT_556 = 11,
	VDSSCOMP_PIXELFORMAT_655 = 12,
	VDSSCOMP_PIXELFORMAT_RGBX_8880 = 13,
	VDSSCOMP_PIXELFORMAT_666 = 14,

	VDSSCOMP_PIXELFORMAT_15BPPGENERIC = 15,
	VDSSCOMP_PIXELFORMAT_16BPPGENERIC = 16,
	VDSSCOMP_PIXELFORMAT_24BPPGENERIC = 17,
	VDSSCOMP_PIXELFORMAT_32BPPGENERIC = 18,

	VDSSCOMP_PIXELFORMAT_UYVY = 19,
	VDSSCOMP_PIXELFORMAT_UYNV = 20,
	VDSSCOMP_PIXELFORMAT_YUY2 = 21,
	VDSSCOMP_PIXELFORMAT_YUYV = 22,
	VDSSCOMP_PIXELFORMAT_YUNV = 23,
	VDSSCOMP_PIXELFORMAT_YVYU = 24,
	VDSSCOMP_PIXELFORMAT_VYUY = 25,

	VDSSCOMP_PIXELFORMAT_IMC2 = 26,
	VDSSCOMP_PIXELFORMAT_YV12 = 27,
	VDSSCOMP_PIXELFORMAT_I420 = 28,

	VDSSCOMP_PIXELFORMAT_IMC1 = 29,
	VDSSCOMP_PIXELFORMAT_IMC3 = 30,
	VDSSCOMP_PIXELFORMAT_IMC4 = 31,
	VDSSCOMP_PIXELFORMAT_NV12 = 32,
	VDSSCOMP_PIXELFORMAT_NV21 = 33,
	VDSSCOMP_PIXELFORMAT_UYVI = 34,
	VDSSCOMP_PIXELFORMAT_VLVQ = 35,

	VDSSCOMP_PIXELFORMAT_CUSTOM = 0X1000
};

struct vdsscomp_video_timings {
	/* Unit: pixels */
	__u16 xres;
	/* Unit: pixels */
	__u16 yres;
	/* Unit: KHz */
	__u32 pixel_clock;
	/* Unit: pixel clocks */
	__u16 hsw;	/* Horizontal synchronization pulse width */
	/* Unit: pixel clocks */
	__u16 hfp;	/* Horizontal front porch */
	/* Unit: pixel clocks */
	__u16 hbp;	/* Horizontal back porch */
	/* Unit: line clocks */
	__u16 vsw;	/* Vertical synchronization pulse width */
	/* Unit: line clocks */
	__u16 vfp;	/* Vertical front porch */
	/* Unit: line clocks */
	__u16 vbp;	/* Vertical back porch */
};

struct vdsscomp_rect {
	__s32 left;
	__s32 top;
	__u32 right;
	__u32 bottom;
};

enum vdsscomp_deinterlace_mode {
	VDSSCOMP_DI_RESERVED = 0,
	VDSSCOMP_DI_WEAVE,
	VDSSCOMP_3MEDIAN,
	VDSSCOMP_DI_VMRI,
};

struct vdsscomp_interlace {
	__u32 field_offset;
	__u32 interlaced;
	enum vdsscomp_deinterlace_mode mode;
};

struct vdsscomp_layer_info {
	__u32 enabled;
	enum vdsscomp_pixelformat fmt;
	struct vdsscomp_interlace interlace;
	struct vdsscomp_rect src_rect;
	struct vdsscomp_rect dst_rect;

	__u32 width;		/* surface width/stride */
	__u32 height;		/* surface height */
	__u8 pre_mult_alpha;
	__u8 pack[3];
};

struct vdsscomp_screen_info {
	__u32 back_color;
	__u32 top_layer;
};

struct vdsscomp_setup_disp_data {
	__u16 num_layers;
	__u32 dirty_mask;
	__u32 phys_addr[MAX_LAYERS];
	struct vdsscomp_screen_info scn;
	struct vdsscomp_layer_info layers[MAX_LAYERS];
};

struct vdsscomp_setup_data {
	__u32 sync_id; /* for debugging */
	__u16 num_disps;
	struct vdsscomp_setup_disp_data disps[2];
};
struct vdsscomp_display_info {
	__u32 ix;
	__u32 layers_avail;	/* bitmask of available overlays */
	__u32 layers_owned;		/* bitmask of owned overlays */
	enum vdsscomp_display_type type;
	__u8 enabled;
	struct vdsscomp_video_timings timings;
	__u16 width_in_mm;		/* screen dimensions */
	__u16 height_in_mm;
};

int vdsscomp_gralloc_queue(struct vdsscomp_setup_data *d,
	void (*cb_fn)(void *, int), void *cb_arg);

 /* Gets information about the display. */
#define VDSSCIOC_QUERY_DISPLAY	_IOWR('O', 131, struct vdsscomp_display_info)

#endif
