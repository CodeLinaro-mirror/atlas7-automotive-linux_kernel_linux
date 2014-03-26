/*
* (C) Copyright (C) 2007 SiRF Technology Inc.
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston,
* MA 02111-1307 USA
*/

#ifndef __SIRFSOC_FB_H_
#define __SIRFSOC_FB_H_

#include <linux/types.h>

struct sirfsocfb_screen {
	int xstart;
	int ystart;
	int xsize;
	int ysize;
};

struct sirfsocfb_backcolour {
	/*
	 * A non zero value means that the defined blank colour will
	 * be used in the active region else, the last displayed pixel
	 * value will be used to fill the inactive region
	 */
	int blankcolour_valid;
	/* This register is not used */
	unsigned long blankcolour;
	/*
	 * Pixel value to be used for region where no layer is active,
	 * must be given in 8:8:8 RGB format
	 */
	unsigned long background_colour;
};

struct sirfsocfb_colorkeys {
	int enable;
	__u32 color_key_big;
	__u32 color_key_small;
};

enum SIRFSOCFB_FORMAT {
	FORMAT_RGBA_8888 = 0,
	FORMAT_RGB_565,
	FORMAT_BGRA_8888,
	FORMAT_YCbCr_422_SP,
	FORMAT_YCbCr_420_SP,
	FORMAT_YCrCb_420_SP,
	FORMAT_YCbYCr_422_I,
	FORMAT_CrYCbY_422_I,
	FORMAT_YCbCr_420_P,
	FORMAT_BGRX_8888,
	FORMAT_RGBX_8888,
};


struct sirfsocfb_createlayer {
	__u32 width;
	__u32 height;
	enum SIRFSOCFB_FORMAT format;
	__u32 wstride_byte;	/* in byte */
	__u32 hstride_byte;	/* in byte */
	__u32 wstride_pixel;	/* in pixel */
	__u32 hstride_pixel;	/* in pixel */
	__u32 num_buffers;
};

enum SIRFSOCFB_FLUSH_CACHE_OP {
	FLUSH_CACHE_OP_INVALID = 0,
	FLUSH_CACHE_OP_CLEAN,
	FLUSH_CACHE_OP_FLUSH,
};

struct sirfsocfb_flush_cache_addr {
	__u32 phy_addr_start;
	__u32 phy_addr_size;
	enum SIRFSOCFB_FLUSH_CACHE_OP flush_cache_op;
};

struct sirfsocfb_rect {
	int left;
	int top;
	int right;
	int bottom;
};

struct sirfsocfb_surf {
	__u32 width;
	__u32 height;
	__u32 base;
	__u8 fmt;
	__u8 reserved[3];
	struct sirfsocfb_rect rect;
};

#if defined(CONFIG_PANEL_SAMSUNG_ROM_INTERFACE_QVGA)
#define SIRFSOCFB_MAX_LAYERS	1
#else
#define SIRFSOCFB_MAX_LAYERS	4
#endif

struct sirfsocfb_layer_parms {
	__u32 enable;
	__u32 format;
	__u32 width;
	__u32 height;
	struct sirfsocfb_rect src_rect;
	struct sirfsocfb_rect dst_rect;
};

struct sirfsocfb_layers_parms {
	__u32 size; /* size of valid data in this struct */
	__u32 wait;
	__u32 phys_addr[SIRFSOCFB_MAX_LAYERS];
	/* mask to update layer parameters, excludes only update base */
	__u32 layer_mask;
	/* layer parameters needs update */
	struct sirfsocfb_layer_parms layer_info[SIRFSOCFB_MAX_LAYERS];
};

#define BLT_DI_MODE_MASK		0x00000007
#define BLT_DI_NONE			0x00000000
#define BLT_DI_INTRA_FIELD_SPATIAL	0x00000001
#define BLT_DI_WEAVE			0x00000002
#define BLT_DI_3MEDIAN			0x00000003
#define BLT_DI_VMRI			0x00000004

#define BLT_BOT_FIELD_FIRST	0x00000008 /* input bottom field first */
/* bottom field reserved. useless now */
#define BLT_DI_FIELD_BOT	0x00000010
/* lines for even or odd fields are mixed */
#define BLT_FIELDS_MIX		0x00000020
/* frame rate will be doubled if deinterlace is enabled */
#define BLT_DOUBLE_FRATE	0x00000040
/* not wait blt to complete */
#define BLT_NOT_WAIT_COMPLETE	0x01000000

struct sirfsocfb_bltparms {
	struct sirfsocfb_surf src;
	struct sirfsocfb_surf dst;
	__u32  flag;
	__u32  reserved;	/* for potiential extension */
};

enum {
	BLE_BLT_ARGB8888,
	BLE_BLT_ABGR8888,
	BLE_BLT_RGB565,
};

enum {
	BLE_BLT_ALPHA_OP_NON_PREMULTIPLIED = 1,
	BLE_BLT_ALPHA_OP_PREMULTIPLIED     = 2
};

/* disable all additional controls */
#define BLE_BLT_DISABLE_ALL		0x00000000
/* enable transparent blt   */
#define BLE_BLT_TRANSPARENT_ENABLE	0x00000001
/* enable standard global alpha */
#define BLE_BLT_GLOBAL_ALPHA		0x00000002
/* enable per-pixel alpha bleding */
#define BLE_BLT_PERPIXEL_ALPHA		0x00000004
/* apply 90 degree rotation to the blt */
#define BLE_BLT_ROT_90			0x00000020
/* apply 180 degree rotation to the blt */
#define BLE_BLT_ROT_180			0x00000040
/* apply 270 degree rotation to the blt */
#define BLE_BLT_ROT_270			0x00000080
/* apply mirror in horizontal */
#define BLE_BLT_FLIP_H			0x00000100
/* apply mirror in vertical     */
#define BLE_BLT_FLIP_V			0x00000200
/* Source color Key  enabled    */
#define BLE_BLT_SRC_COLORKEY		0x00000400
/* Destination color Key enabled */
#define BLE_BLT_DST_COLORKEY		0x00000800
/* color fill enabled */
#define BLE_BLT_COLOR_FILL		0x00001000
/* wait blt to complete */
#define BLE_BLT_WAIT_COMPLETE		0x00100000

#define FB_ACCEL_BLE	0xFF	/* CSR BLE */

#define BLE_RECTS_NUM_MAX		4
struct sirfsocfb_bltparms_ble {
	__u32 rop3;			/* rop3 code  */
	__u32 fill_color;		/* fill color */
	__u32 color_key;		/* color key in argb8888 fromat */
	__u8  global_alpha;		/* global alpha blending */
	__u8  blend_func;		/* per-pixel alpha-blending function */
	__u32 num_rects;
	struct sirfsocfb_rect rects[BLE_RECTS_NUM_MAX];
	/* additional blit control information */
	__u32 flags;
	__u32 dst_offset;		/* destination memory */
	/* the number of bytes from pixel 0,0 to 0,1 */
	__u32 dst_stride;
	/* pixel offset from start of dest surface to start of blt rectangle */
	__u32 dstx, dsty;
	__u32 dst_sizex, dst_sizey;	/* blt size */
	__u32 dst_fmt;			/* dest format */
	__u32 dst_width;		/* size of dest surface in pixels */
	__u32 dst_height;		/* size of dest surface in pixels */
	/* source mem, (source fields are also used for patterns) */
	__u32 src_offset;
	/* signed stride, the number of bytes from pixel 0,0 to 0,1 */
	__u32 src_stride;
	/* pixel offset from start of surface to start of source rectangle */
	__u32 srcx, srcy;
	/* source rectangle size or pattern size in pixels */
	__u32 src_sizex, src_sizey;
	__u32 src_fmt;			/* source format */
	__u32 src_width;		/* size of source surface in pixels */
	__u32 src_height;		/* size of source surface in pixels */
};

enum sirfsocfb_feature_layer {
	NORMAL_LAYER = -1,
	REARVIEW_FEATURE_LAYER,
	HDMI_FEATURE_LAYER,
};

/* SiRF SoC FB specific ioctls */
#define SIRFSOCFB_SET_TOPLAYER	_IO('S', 0x0)
#define SIRFSOCFB_GET_TOPLAYER	_IOR('S', 0x0, int)
#define SIRFSOCFB_SET_ALPHA	_IOW('S', 0x1, unsigned long)
#define SIRFSOCFB_GET_ALPHA	_IOR('S', 0x1, unsigned long)
#define SIRFSOCFB_SET_SCRSIZE	_IOW('S', 0x2, struct sirfsocfb_screen)
#define SIRFSOCFB_GET_SCRSIZE	_IOR('S', 0x2, struct sirfsocfb_screen)
#define SIRFSOCFB_GET_BACKCOLOR	_IOR('S', 0x3, struct sirfsocfb_backcolour)
#define SIRFSOCFB_SET_BACKCOLOR	_IOW('S', 0x3, struct sirfsocfb_backcolour)
#define SIRFSOCFB_SET_COLORKEYS _IOW('S', 0x4, struct sirfsocfb_colorkeys)
#define SIRFSOCFB_GET_COLORKEYS _IOR('S', 0x4, struct sirfsocfb_colorkeys)
#define SIRFSOCFB_Q_BUFFER	_IOR('S', 0x5, int)
#define SIRFSOCFB_DQ_BUFFER	_IOW('S', 0x5, int)
#define SIRFSOCFB_CREATE_LAYER  _IOWR('S', 0x6, struct sirfsocfb_createlayer)
#define SIRFSOCFB_DESTROY_LAYER _IO('S', 0x6)
#define SIRFSOCFB_ENABLE_LAYER	_IO('S', 0x7)
#define SIRFSOCFB_DISABLE_LAYER	_IO('S', 0x8)
#define SIRFSOCFB_SET_DMASIZE	_IOW('S', 0x8, struct sirfsocfb_screen)
#define SIRFSOCFB_GET_DMASIZE	_IOR('S', 0x8, struct sirfsocfb_screen)
#define SIRFSOCFB_FLUSH_CACHE  _IOW('S', 0x9, struct sirfsocfb_flush_cache_addr)
#define SIRFSOCFB_DUMP_REGISTER	_IO('S', 0xa)
#define SIRFSOCFB_BLT_YUV2RGB	_IOWR('S', 0xB, struct sirfsocfb_bltparms)
#define SIRFSOCFB_BLT_BLE _IOW('S', 0xc, struct sirfsocfb_bltparms_ble)
#define SIRFSOCFB_BLT_BLE_COMPLETE _IOR('S', 0xc, int)
#define SIRFSOCFB_DUMP_HDMI_REGISTER	_IO('S', 0xD)
#define SIRFSOCFB_SET_LAYERS	_IOW('S', 0xF, struct sirfsocfb_layers_parms)
#define SIRFSOCFB_ENABLE_FEATURE_LAYER	_IOW('S', 0x10, int)
#define SIRFSOCFB_DISABLE_FEATURE_LAYER	_IO('S', 0x10)
#define SIRFSOCFB_SET_GAMMA_TABLE	_IOW('S', 0x11, __u16[256 * 3])
#define SIRFSOCFB_GET_GAMMA_TABLE	_IOR('S', 0x11, __u16[256 * 3])

#endif /* __SIRFSOC_FB_H_ */
