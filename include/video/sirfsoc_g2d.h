/*
 * CSR sirfsoc framebuffer driver
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

#ifndef __G2D_OP_H__
#define __G2D_OP_H__

#ifdef __KERNEL__
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/fs.h>       /* file system operations */
#endif /* _KERNEL_ */

/* disable all additional controls */
#define G2D_BLT_DISAG2D_ALL		0x00000000
/* enable transparent blt   */
#define G2D_BLT_TRANSPARENT_ENABLE	0x00000001
/* enable standard global alpha */
#define G2D_BLT_GLOBAL_ALPHA		0x00000002
/* enable per-pixel alpha bleding */
#define G2D_BLT_PERPIXEL_ALPHA		0x00000004
/* apply 90 degree rotation to the blt */
#define G2D_BLT_ROT_90			0x00000020
/* apply 180 degree rotation to the blt */
#define G2D_BLT_ROT_180			0x00000040
/* apply 270 degree rotation to the blt */
#define G2D_BLT_ROT_270			0x00000080
/* apply mirror in horizontal */
#define G2D_BLT_FLIP_H			0x00000100
/* apply mirror in vertical     */
#define G2D_BLT_FLIP_V			0x00000200
/* Source color Key  enabled    */
#define G2D_BLT_SRC_COLORKEY		0x00000400
/* Destination color Key enabled */
#define G2D_BLT_DST_COLORKEY		0x00000800
/* color fill enabled */
#define G2D_BLT_COLOR_FILL		0x00001000
/* wait blt to complete */
#define G2D_BLT_WAIT_COMPLETE		0x00100000
/* enable clip*/
#define G2D_BLT_CLIP_ENABLE		0x00002000


/* Memory type */
#define G2D_MEM_ADDR 0
#define G2D_MEM_ION  1

#define G2D_RECTS_MAX 4

enum g2d_format {
	G2D_ARGB8888,
	G2D_ABGR8888,
	G2D_RGB565,

	/* the following format isn't for g2d but vpp. */
	G2D_EX_YUYV = 0x10000,
	G2D_EX_YVYU,
	G2D_EX_UYVY,
	G2D_EX_VYUY,
	G2D_EX_NV12,
	G2D_EX_NV21,
	G2D_EX_YV12,
	G2D_EX_I420,
	G2D_EX_BGRX8880,
};

/* field type isn't for g2d but vpp */
enum g2d_ex_field {
	G2D_EX_FIELD_NONE = 0, /* no field*/
	G2D_EX_FIELD_SEQ_TB, /* both fields sequential into
				one buffer, top-bottom order */
	G2D_EX_FIELD_SEQ_BT, /* same as above + bottom-top order */
	G2D_EX_FIELD_INTERLACED_TB, /*both fields interlaced, top
				field is transmitted first */
	G2D_EX_FIELD_INTERLACED_BT, /* same as above + bottom field
				is transmitted first */
};

struct sirf_g2d_rect {
	int32_t left;
	int32_t top;
	int32_t right;
	int32_t bottom;
};

struct sirf_g2d_rect_wh {
	int32_t x;
	int32_t y;
	int32_t w;
	int32_t h;
};

struct sirf_g2d_surface {
	u_int32_t paddr; /* destination memory */
	u_int32_t bpp;
	enum g2d_format format;
	u_int32_t width; /* size of dest surface in pixels */
	u_int32_t height; /* size of dest surface in pixels */
	enum g2d_ex_field field;
};

struct sirf_g2d_bltparams {
	u_int32_t rop3;	/* rop3 code  */
	u_int32_t fill_color; /* fill color */
	u_int32_t color_key;     /* color key in argb8888 fromat */
	u_int8_t  global_alpha;  /* global alpha blending */
	u_int8_t  blend_func;    /* per-pixel alpha-blending function */
	u_int32_t num_rects;
	struct sirf_g2d_rect rects[G2D_RECTS_MAX];
				/* clip for build. */

	u_int32_t flags; /* additional blt control information */
	u_int32_t yuvflags; /* additional flag for yuv */

	struct sirf_g2d_surface src;
	struct sirf_g2d_rect_wh src_rc; /* rect of src to be selected. */
	struct sirf_g2d_surface dst;
	struct sirf_g2d_rect_wh dst_rc;	/* rect of destination for drawing. */
};

enum {
	G2D_BITBLT = 0xc,
	G2D_BITBLT_WAIT,
};

#define CSR_G2D_OP_BASE 'B'

#define SIRFSOC_G2D_SUBMIT_BITBLD \
	_IOW(CSR_G2D_OP_BASE, G2D_BITBLT, struct sirf_g2d_bltparams)
#define SIRFSOC_G2D_WAIT _IOR(CSR_G2D_OP_BASE, G2D_BITBLT_WAIT, int)

#endif /* __G2D_OP_H__ */
