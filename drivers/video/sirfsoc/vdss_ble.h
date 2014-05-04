/*
 * CSR sirfsoc BLE library interface
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __SIRFSOC_VDSS_BLE_H
#define __SIRFSOC_VDSS_BLE_H

#include <linux/kernel.h>
#include "ble_defs.h"

/*************************************************************************/
/*              Misc Definition Area Begin                               */
#ifndef max
#define max(a, b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a, b)            (((a) < (b)) ? (a) : (b))
#endif

enum drawctrl_shift {
	BLE_DRAWCTRL_COLORFILL_SHIFT     = 16,
	BLE_DRAWCTRL_ALPHA_SHIFT         = 17,
	BLE_DRAWCTRL_FLIP_H_SHIFT        = 19,
	BLE_DRAWCTRL_FLIP_V_SHIFT        = 20,
	BLE_DRAWCTRL_ROTATION_SHIFT      = 21,
	BLE_DRAWCTRL_CLIP_SHIFT          = 23,
	BLE_DRAWCTRL_TRANSPARENT_SHIFT   = 24,
	BLE_DRAWCTRL_COLORKEY_MODE_SHIFT = 25,
};

/***************************************************************************
**
** Blt Engine Misc Declare Area
****************************************************************************/

enum ble_mode {
	BLEMMIOMODE    = 1,
	BLECOMMANDMODE = 2,
};

struct ring_bufinfo {
	u32   offset;
	u32   virtual;
	/* double word aligened */
	u32   size;
};

struct fence_bufinfo {
	u32    offset;
	u32    virtual;
	/* double word aligened */
	u32    size;
};

enum ble_format {
	BLE_ARGB8888,
	BLE_ABGR8888,
	BLE_RGB565,
	BLE_ARGB1555,  /* this format is not support yet */
	BLE_ARGB4444,  /* this format is not support yet */
	BLE_YUYV,      /* this format is not support yet */
	BLE_YVYU,      /* this format is not support yet */
	BLE_UYVY,      /* this format is not support yet */
	BLE_VYUY,      /* this format is not support yet */
	/* this format is for LCD support */
	BLE_RGB556,
	BLE_RGB655,
	BLE_RGB666,
};

enum ble_blendfunc {
	/* source alpha : Cds = Csrc*Asrc + Cdst*(1-Asrc) */
	BLE_ALPHA_OP_NON_PREMULTIPLIED = 1,
	/* premultiplied source alpha : Cdst = Csrc + Cdst*(1-Asrc) */
	BLE_ALPHA_OP_PREMULTIPLIED     = 2,
};

/* flags for control information of additional blits */
enum ble_blt_flags {
	/* disable all additional controls */
	BLE_BLIT_DISABLE_ALL                  = 0x00000000,
	/* enable transparent blt   */
	BLE_BLIT_TRANSPARENT_ENABLE           = 0x00000001,
	/* enable standard global alpha */
	BLE_BLIT_GLOBAL_ALPHA                 = 0x00000002,
	/* enable per-pixel alpha bleding */
	BLE_BLIT_PERPIXEL_ALPHA               = 0x00000004,
	/* alpha bleding mask, include global and perpixel alpha */
	BLE_BLIT_ALPHA_MASK                   = 0x00000006,
	/* enable pattern surf (disable fill) */
	BLE_BLIT_PAT_SURFACE_ENABLE           = 0x00000008,
	/* enable source surf  (disable fill) */
	BLE_BLIT_SRC_SURFACE_ENABLE           = 0x00000010,
	/* apply 90 degree rotation to the blt */
	BLE_BLIT_ROT_90                       = 0x00000020,
	/* apply 180 degree rotation to the blt */
	BLE_BLIT_ROT_180                      = 0x00000040,
	/* apply 270 degree rotation to the blt */
	BLE_BLIT_ROT_270                      = 0x00000080,
	/* apply roate degree mask to the blt */
	BLE_BLIT_ROT_MASK                     = 0x000000e0,
	/* apply mirror in horizontal */
	BLE_BLIT_FLIP_H                       = 0x00000100,
	/* apply mirror in vertical*/
	BLE_BLIT_FLIP_V                       = 0x00000200,
	/* apply mirror mask */
	BLE_BLIT_FLIP_MASK                    = 0x00000300,
	/* Source color Key  enabled    */
	BLE_BLIT_SRC_COLORKEY                 = 0x00000400,
	/* Destination color Key enabled */
	BLE_BLIT_DST_COLORKEY                 = 0x00000800,
	/* Color fill enabled    */
	BLE_BLIT_COLOR_FILL                   = 0x00001000,
	/* Clipping enabled     */
	BLE_BLIT_CLIP_ENABLE                  = 0x00002000,
	/* Blt via dedicated BLE 2D Core */
	BLE_BLIT_PATH_BLECORE               = 0x00004000,
	/* Blt via extern Core */
	BLE_BLIT_PATH_EXTERNCORE              = 0x00008000,
};

struct sync_object {
	unsigned long   phyaddr;
	unsigned long   viraddr;
	unsigned long   cur_syncid;
};

/* surface info structure */
struct ble_meminfo {
	unsigned long           reserved1;
	unsigned long           reserved2;
	unsigned long           offset;
	unsigned long           memsize;
	unsigned long           tag;
	unsigned long           desired_syncid;
	struct sync_object     *sync_object;
};

/* error codes */
enum ble_error {
	BLE_OK                           =  0,
	BLE_ERR_INVALID_PARAMETER        = -1,
	BLE_ERR_DEVICE_UNAVAILABLE       = -2,
	BLE_ERR_INVALID_CONTEXT          = -3,
	BLE_ERR_MEMORY_UNAVAILABLE       = -4,
	BLE_ERR_DEVICE_NOT_PRESENT       = -5,
	BLE_ERR_GENERIC		         = -6,
	BLE_ERR_BLT_NOTCOMPLETE          = -7,
	BLE_ERR_HW_FEATURE_NOT_SUPPORTED = -8,
	BLE_ERR_NOT_YET_IMPLEMENTED      = -9,
	BLE_ERR_MAPPING_FAILED           = -10
};

struct ble_rect {
	long  left;
	long  top;
	long  right;
	long  bottom;
};

struct ble_bltinfo {
	unsigned long                  rop3;                  /* rop3 code  */
	unsigned long                  fill_color;             /* fill color */
	/* color key in argb8888 fromat */
	unsigned long                  colorkey;
	/* global alpha blending */
	u8                             global_alpha;
	/* per-pixel alpha-blending function */
	u8                             blendfunc;
	unsigned long                  num_cliprect;
	struct ble_rect                *ble_cliprect;
	/* additional blit control information */
	int			       blt_flags;
	/* destination memory */
	struct ble_meminfo             *dmeminfo;
	/* signed stride, the number of bytes from pixel 0,0 to 0,1 */
	unsigned long                  dst_stride;
	/* pixel offset from start of dest surface to start of blt rectangle */
	unsigned long                  dstx, dsty;
	unsigned long                  dst_sizex, dst_sizey;     /* blt size */
	int			       dst_format;             /* dest format */
	/* size of dest surface in pixels */
	unsigned long                  dst_surfwidth;
	/* size of dest surface in pixels */
	unsigned long                  dst_surfheight;
	/* source mem, (source fields are also used for patterns) */
	struct ble_meminfo             *smeminfo;
	/* signed stride, the number of bytes from pixel 0,0 to 0,1 */
	unsigned long                  src_stride;
	/* pixel offset from start of surface to start of source rectangle */
	long                           srcx, srcy;
	/* source rectangle size or pattern size in pixels */
	unsigned long                  src_sizex, src_sizey;
	int                            src_format;        /* source format */
	/* size of source surface in pixels */
	unsigned long                  src_surfwidth;
	/* size of source surface in pixels */
	unsigned long                  src_surfheight;
	bool                           src_exist;
	/* pattern memory containing argb8888 color table */
	struct ble_meminfo             *pat_meminfo;
	unsigned long                  patx, paty;
	unsigned long                  pat_sizex, patsizey;
	/* byte offset from start of allocation to start of pattern */
	unsigned long                  patoffset;
	bool                           pat_exist;
	bool                           need_synclast;
};

#define BLE_MAX_BLIT_CMD_SIZE  0x40

#define RING_BUF_SIZE            (64*1024UL)
#define RING_BUF_ALIGNMENT       0x8
#define RINGBUFFULLGAP           0x4

static inline unsigned long convert_rgb565to8888(unsigned long color)
{
	/* R | G I B */
	return (((color >> 8) & 0xF8UL) | ((color >> 13) & 0x7UL)) << 16 |
		(((color >> 3) & 0xFCUL) | ((color >>  9) & 0x3UL)) <<  8 |
		(((color << 3) & 0xF8UL) | ((color >>  2) & 0x7UL));
}

#define BLE_PATTERN_WIDTH       0x08
#define BLE_PATTERN_HEIGHT      0x08
#define BLE_PATTERN_STEP        0x04
#define BLE_PATTERN_STRIDE      (BLE_PATTERN_STEP   * BLE_PATTERN_WIDTH)
#define BLE_PATTERN_SIZE        (BLE_PATTERN_STRIDE * BLE_PATTERN_HEIGHT)

#define MAX_PATTERN_BUF_RESERVED   0x400

struct ble_context {
	int                  ble_op_mode;
	struct ble_meminfo   *patsurf[MAX_PATTERN_BUF_RESERVED];
	u32                  cur_patbuf;
	struct sync_object   sync_object;
	void __iomem	     *ble_reg_base;
	struct ring_bufinfo  ringbuf;
	/* double word aligened */
	u32                  ringbuf_size_left;
	u32                  *ringbuf_wtptr;
	struct fence_bufinfo fencebuf;
};

#define FENCE_BUF_SIZE       0x1000
#define FENCE_BUF_ALIGNMENT  0x10

#define SYNCOBJECTGAP        0x1000L

/***************************************************************************
**
** Function table Declare Area
****************************************************************************/
struct ble_init_meminfo {
	void __iomem	*regbase;
	u32		memoffset;
	u32		membase;
	u32		memsize;
};

struct vdss_ble_ops {
	bool    (*initialize)	    (void **dcontext, void *initdta);
	bool    (*terminate)	    (void *hcontext);
	bool    (*check_params)	    (void *parms);
	u32     (*bitblt)	    (void *hcontext, void *bltinfo);
	int     (*query_status)     (void *dcontext, void *meminfo, bool wait);
	void    (*interrupt_routine)(void *hcontext);
	void    (*wakeup)	    (void);
	void    (*sleep)	    (void);
	void    (*enable_clock)	    (void);
	void    (*disable_clock)    (void);
	void    (*reset)	    (void);
	void    (*print_registers)  (void);
};

void vdss_ble_install_ops(struct vdss_ble_ops *ble_ops);
#endif
