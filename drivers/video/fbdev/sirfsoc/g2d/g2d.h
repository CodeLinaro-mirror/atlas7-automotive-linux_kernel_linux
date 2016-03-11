/*
 * Driver for Graphics 2D module in SirfSOC
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

#ifndef __G2D_H_
#define __G2D_H_

#include <linux/types.h>

#define g2d_inf(fmt, arg...)	pr_info("[G2D]: " fmt, ## arg)
#define g2d_warn(fmt, arg...)	pr_warn("[G2D] %s: " fmt, __func__, ## arg)
#define g2d_err(fmt, arg...)	pr_err("[G2D] %s:" fmt, __func__, ## arg)

#define G2D_MAX_BLIT_CMD_SIZE		0x40
#define RING_BUF_SIZE			(64*1024UL)
#define RING_BUF_ALIGNMENT		0x8
#define RINGBUFFULLGAP			0x10

#define G2D_PATTERN_WIDTH		0x08
#define G2D_PATTERN_HEIGHT		0x08
#define G2D_PATTERN_STEP		0x04
#define G2D_PATTERN_STRIDE	(G2D_PATTERN_STEP   * G2D_PATTERN_WIDTH)
#define G2D_PATTERN_SIZE	(G2D_PATTERN_STRIDE * G2D_PATTERN_HEIGHT)
#define MAX_PATTERN_BUF_RESERVED	0x400
#define G2D_FENCE_BUF_SIZE		0x1000
#define G2D_FENCE_BUF_ALIGNMENT		0x10
#define G2D_FENCE_ANY			0x0
#define G2D_FENCE_YUV			0x1
#define G2D_FENCE_G2D_START		0x2
#define SYNCOBJECTGAP			0x1000L
#define G2D_MEMINFO_MAX 5

/*
 * G2D Commands Description.
 *
 * Command header formats shows below.
 *
 * 31      29|28              0|
 * -----------------------------
 * |  opcode |    Parameter    |
 * -----------------------------
 *
 */
#define G2D_CMD_OP(x)			(((x) & 0x7) << 29)
#define G2D_CMD_OP_MASK			(7 << 29)
/*
 * NOP:
 * The lowest 14 bits of command header. indicates how many dwords
 * should be skipped after this command. The max value is 16K.
 *
 * Format:
 *
 * 31      29|28      14|13          0|
 * ------------------------------------
 * |   0x0   |Reserved  |skip dwords  |
 * ------------------------------------
 */
#define G2D_CMD_SKIP_COMMAND				(0x0)
#define CMD_SKIP_MASK				(0x3FFF << 0)

/*
 * Normal fence and normal fence wait:
 * Requires the fence address is 4bytes aligned.
 *
 * Format:
 *
 * 31        29|28            0|
 * -----------------------------
 * | 0x1/0x2   |Address[31..3] |
 * -----------------------------
 * |  FenceID                  |
 * -----------------------------
 *
 */
#define G2D_CMD_FENCE_WRITE			(0x1)
#define G2D_CMD_FENCE_WAIT			(0x2)
#define CMD_FENCE_ADDR_MASK			(0x1FFFFFFF << 0)
#define CMD_FENCE_ADDR(x)			(((x) >> 3) & 0x1FFFFFFF)

/*
 * set register command:
 * the register count from the start register offset to the end is
 * (start + 4 * (n - 1)).
 *
 * format:
 * 31      29|28      16|15    8|7                     0|
 * ------------------------------------------------------
 * |   0x3   |reserved  | count | start register offset |
 * ------------------------------------------------------
 * |  value1.                                           |
 * |  value2.                                           |
 * |  ...                                               |
 * ------------------------------------------------------
 */
#define G2DCMD_SET_REGISTER			(0x3)
#define CMD_SET_REG_START_MASK			(0xFF << 0)
#define CMD_SET_REG_FOLLOW_MASK			(0xFF << 8)
#define CMD_SET_REG_FOLLOW(x)			(((x) & 0xFF) << 8)

/*
 * Fence write with interrupt:
 * Fence write back command, after the fence write back,
 * HW triggers an interrupt.
 *
 * format:
 * 31      29|28                      0|
 * -------------------------------------
 * |   0x4   |    Reserved             |
 * -------------------------------------
 * |  FenceID                          |
 * -------------------------------------
 */
#define G2DCMD_WRITE_FENCE_INTERRUPT		(0x4)

/***************************************************************************
**
** G2D Engine Register Declare Area
****************************************************************************/
#define FB_BASE					(0x00)
#define ENG_STATUS				(0x04)
#define ENG_CTRL				(0x08)
#define RB_OFFSET				(0x0C)
#define RB_LENGTH				(0x10)
#define RB_RD_PTR				(0x14)
#define RB_WR_PTR				(0x18)
#define DST_OFFSET				(0x1C)
#define DST_FORMAT				(0x20)
#define DST_LT					(0x24)
#define DST_RB					(0x28)
#define CLIP_LT					(0x2C)
#define CLIP_RB					(0x30)
#define SRC_OFFSET				(0x34)
#define SRC_FORMAT				(0x38)
#define SRC_LT					(0x3C)
#define SRC_RB					(0x40)
#define PAT_OFFSET				(0x44)
#define FILL_COLOR				(0x48)
#define COLOR_KEY				(0x4C)
#define GBL_ALPHA				(0x50)
#define DRAW_CTL				(0x54)
/* 0x58~0x7c reserved */
#define INTERRUPT_ENABLE			(0x80)
#define INTERRUPT_CLEAR				(0x84)
#define INTERRUPT_STATUS			(0x88)
#define HW_RESERVED				(0x8C)
#define MAX_REG_COUNT				(0x40)

#define BOUNDARY				(0x18)

#define VALID_INTERRUPT_MASK			(0x0000000F)

#define BLT_COMPLETE_INTERRUPT                  (0x00000000)
#define BLT_TIMEOUT_INTERRUPT			(0x00000001)
#define FENCE_INTERRUPT				(0x00000002)
#define CMD_BUF_EMPTY_INTERRUPT			(0x00000003)

/* reg_fb_base */
#define REG_FB_MASK				(0xFFFFFFFF << 0)
/* reg_eng_status */
#define ENG_STATUS_ENABLE_MASK			(0x1 << 0)
#define ENG_STATUS_IDLE_MASK			(0x1 << 1)
#define ENG_STATUS_IDLE(x)			(((x) & 0x2) << 1)
/* reg_eng_ctrl */
#define ENG_CTRL_ENABLE_MASK			(0x1 << 0)
#define ENG_CTRL_IDLE_MASK			(0x1 << 1)
#define ENG_CTRL_IDLE(x)			(((x) & 0x2) << 1)
/* reg_interrupt_clear */
#define INTERRUPT_CLEAR_BUF_MASK		(0x1 << 0)
#define INTERRUPT_CLEAR_FENCE_MASK		(0x1 << 1)
#define INTERRUPT_CLEAR_TIMEOUT_MASK		(0x1 << 2)
#define INTERRUPT_CLEAR_COMPLETE_MASK		(0x1 << 3)
#define INTERRUPT_CLEAR_FENCE(x)		(((x) & 0x1) << 1)
#define INTERRUPT_CLEAR_TIMEOUT(x)		(((x) & 0x1) << 2)
#define INTERRUPT_CLEAR_COMPLETET(x)		(((x) & 0x1) << 3)

/* reg_interrupt_enable */
#define INTERRUPT_ENABLE_BUF_MASK		(0x1 << 0)
#define INTERRUPT_ENABLE_FENCE_MASK		(0x1 << 1)
#define INTERRUPT_ENABLE_TIMEOUT_MASK		(0x1 << 2)
#define INTERRUPT_ENABLE_COMPLETE_MASK		(0x1 << 3)
#define INTERRUPT_ENABLE_FENCE(x)		(((x) & 0x1) << 1)
#define INTERRUPT_ENABLE_TIMEOUT(x)		(((x) & 0x1) << 2)
#define INTERRUPT_ENABLE_COMPLETET(x)		(((x) & 0x1) << 3)
/* reg_interrupt_status */
#define INTERRUPT_STATUS_BUF_MASK		(0x1 << 0)
#define INTERRUPT_STATUS_FENCE_MASK		(0x1 << 1)
#define INTERRUPT_STATUS_TIMEOUT_MASK		(0x1 << 2)
#define INTERRUPT_STATUS_COMPLETE_MASK		(0x1 << 3)
#define INTERRUPT_STATUS_FENCE(x)		(((x) & 0x1) << 1)
#define INTERRUPT_STATUS_TIMEOUT(x)		(((x) & 0x1) << 2)
#define INTERRUPT_STATUS_COMPLETET(x)		(((x) & 0x1) << 3)
#define INTERRUPT_STATUS_OUT_MASK		(0x1 << 0)
/* reg_rb_offset */
#define RB_OFFSET_MASK				(0xFFFFFFFF << 0)
/* reg_rb_length */
#define RB_LENGTH_MASK				(0xFFFF << 0)
/* reg_rb_rd_ptr */
#define RB_RD_OFFSET_MASK			(0xFFFF << 0)
/* reg_pat_offset
 * reg_dst_offset
 * reg_src_offset */
#define PAT_OFFSET_MASK				(0xFFFF << 0)

/* reg_dst_format */
#define DST_FORMAT_STRIDE_MASK			(0x3FFF << 0)
#define DST_FORMAT_FORMAT_MASK			(0xF << 16)
#define DST_FORMAT_FORMAT(x)			(((x) & 0xF) << 16)
/* reg_src_format */
#define SRC_FORMAT_STRIDE_MASK			(0x3FFF << 0)
#define SRC_FORMAT_FORMAT_MASK			(0xF << 16)
#define SRC_FORMAT_NONPREMUL_MASK		(0x1 << 20)
#define SRC_FORMAT_FORMAT(x)			(((x) & 0xF) << 16)
#define SRC_FORMAT_NONPREMUL(x)			(((x) & 0x1) << 20)

/* reg_draw_ctl */
#define DRAW_CTL_ROP3_MASK			(0xFF << 0)
#define DRAW_CTL_COLORFILL_MASK			(0x1 << 16)
#define DRAW_CTL_ALPHA_MASK			(0x3 << 17)
#define DRAW_CTL_FLIPH_MASK			(0x1 << 19)
#define DRAW_CTL_FLIPV_MASK			(0x1 << 20)
#define DRAW_CTL_ROTATION_MASK			(0x3 << 21)
#define DRAW_CTL_CLIP_MASK			(0x1 << 23)
#define DRAW_CTL_TRANSEN_MASK			(0x1 << 24)
#define DRAW_CTL_COLORKEY_MASK			(0x1 << 25)
#define DRAW_CTL_COLORFILL(x)			(((x) & 0x1) << 16)
#define DRAW_CTL_ALPHA(x)			(((x) & 0x3) << 17)
#define DRAW_CTL_FLIPH(x)			(((x) & 0x1) << 19)
#define DRAW_CTL_FLIPV(x)			(((x) & 0x1) << 20)
#define DRAW_CTL_ROTATION(x)			(((x) & 0x3) << 21)
#define DRAW_CTL_CLIP(x)			(((x) & 0x3) << 23)
#define DRAW_CTL_TRANSEN(x)			(((x) & 0x1) << 24)
#define DRAW_CTL_COLORKEY(x)			(((x) & 0x1) << 25)
/* COLOR
 * reg_fillcolor
 * reg_colorkey */
#define COLOR_B_MASK				(0xFF << 0)
#define COLOR_G_MASK				(0xFF << 8)
#define COLOR_R_MASK				(0xFF << 16)
#define COLOR_A_MASK				(0xFF << 24)
#define COLOR_G(x)				(((x) & 0xFF) << 8)
#define COLOR_R(x)				(((x) & 0xFF) << 16)
#define COLOR_A(x)				(((x) & 0xFF) << 24)

/*  reg_gbl_alpha */
#define GBL_ALPHA_MASK				(0xFF << 0)

/* rect_lt
 * reg_dst_lt,reg_src_lt,reg_clip_lt */
#define RECT_LEFT_MASK				(0xFFF << 0)
#define RECT_TOP_MASK				(0xFFF << 16)
#define RECT_TOP(x)				(((x) & 0xFFF) << 16)
/*  rect_rb
 *reg_dst_rb,reg_src_rb,reg_clip_rb */
#define RECT_RIGHT_MASK				(0xFFF << 0)
#define RECT_BOTTOM_MASK			(0xFFF << 16)
#define RECT_BOTTOM(x)				(((x) & 0xFFF) << 16)

enum drawctrl_shift {
	G2D_DRAWCTRL_COLORFILL_SHIFT     = 16,
	G2D_DRAWCTRL_ALPHA_SHIFT         = 17,
	G2D_DRAWCTRL_FLIP_H_SHIFT        = 19,
	G2D_DRAWCTRL_FLIP_V_SHIFT        = 20,
	G2D_DRAWCTRL_ROTATION_SHIFT      = 21,
	G2D_DRAWCTRL_CLIP_SHIFT          = 23,
	G2D_DRAWCTRL_TRANSPARENT_SHIFT   = 24,
	G2D_DRAWCTRL_COLORKEY_MODE_SHIFT = 25,
};

enum g2d_blendfunc {
	/* source alpha : Cds = Csrc*Asrc + Cdst*(1-Asrc) */
	G2D_ALPHA_OP_NON_PREMULTIPLIED = 1,
	/* premultiplied source alpha : Cdst = Csrc + Cdst*(1-Asrc) */
	G2D_ALPHA_OP_PREMULTIPLIED     = 2,
};

/* flags for control information of additional blits */
enum g2d_blt_flags {
	/* disable all additional controls */
	G2D_BLIT_DISABLE_ALL                  = 0x00000000,
	/* enable transparent blt   */
	G2D_BLIT_TRANSPARENT_ENABLE           = 0x00000001,
	/* enable standard global alpha */
	G2D_BLIT_GLOBAL_ALPHA                 = 0x00000002,
	/* enable per-pixel alpha bleding */
	G2D_BLIT_PERPIXEL_ALPHA               = 0x00000004,
	/* alpha bleding mask, include global and perpixel alpha */
	G2D_BLIT_ALPHA_MASK                   = 0x00000006,
	/* enable pattern surf (disable fill) */
	G2D_BLIT_PAT_SURFACE_ENABLE           = 0x00000008,
	/* enable source surf  (disable fill) */
	G2D_BLIT_SRC_SURFACE_ENABLE           = 0x00000010,
	/* apply 90 degree rotation to the blt */
	G2D_BLIT_ROT_90                       = 0x00000020,
	/* apply 180 degree rotation to the blt */
	G2D_BLIT_ROT_180                      = 0x00000040,
	/* apply 270 degree rotation to the blt */
	G2D_BLIT_ROT_270                      = 0x00000080,
	/* apply roate degree mask to the blt */
	G2D_BLIT_ROT_MASK                     = 0x000000e0,
	/* apply mirror in horizontal */
	G2D_BLIT_FLIP_H                       = 0x00000100,
	/* apply mirror in vertical*/
	G2D_BLIT_FLIP_V                       = 0x00000200,
	/* apply mirror mask */
	G2D_BLIT_FLIP_MASK                    = 0x00000300,
	/* Source color Key  enabled    */
	G2D_BLIT_SRC_COLORKEY                 = 0x00000400,
	/* Destination color Key enabled */
	G2D_BLIT_DST_COLORKEY                 = 0x00000800,
	/* Color fill enabled    */
	G2D_BLIT_COLOR_FILL                   = 0x00001000,
	/* Clipping enabled     */
	G2D_BLIT_CLIP_ENABLE                  = 0x00002000,
	/* Blt via dedicated G2D 2D Core */
	G2D_BLIT_PATH_BLECORE               = 0x00004000,
	/* Blt via extern Core */
	G2D_BLIT_PATH_EXTERNCORE              = 0x00008000,
};

struct g2d_registers {
	u32 fb_base;
	u32 eng_status;
	u32 eng_ctrl;

	u32 rb_base;
	u32 rb_length;
	u32 rb_read;
	u32 rb_write;

	u32 dst_offset;
	u32 dst_format;
	u32 dst_lt;
	u32 dst_rb;
	u32 clip_lt;
	u32 clip_rb;

	u32 src_offset;
	u32 src_format;
	u32 src_lt;
	u32 src_rb;

	u32 pat_offset;

	u32 fill_color;
	u32 color_key;
	u32 gbl_alpha;

	u32 draw_ctrl;
	u32 intr_clear;
	u32 intr_enable;
	u32 reg_intr_status;
};

struct ring_bufinfo {
	unsigned long	paddr;
	void		*vaddr;
	u32		size; /* the unit is DWORD */
};

struct fence_bufinfo {
	unsigned long    paddr;
	void		*vaddr;
	u32		size; /* double word aligened */
};

struct sync_object {
	unsigned long	paddr;
	void		*vaddr;
	u32		cur_id;
	u32		work_id;
};

/* surface info structure */
struct g2d_meminfo {
	unsigned long           reserved1;
	unsigned long           reserved2;
	unsigned long           offset;
	unsigned long           memsize;
	unsigned long           tag;
	unsigned long           desired_syncid;
	struct sync_object     *sync_object;
};

struct g2d_rect {
	long  left;
	long  top;
	long  right;
	long  bottom;
};

struct g2d_bltinfo {
	unsigned long                  rop3;                  /* rop3 code  */
	unsigned long                  fill_color;             /* fill color */
	/* color key in argb8888 fromat */
	unsigned long                  colorkey;
	/* global alpha blending */
	u8                             global_alpha;
	/* per-pixel alpha-blending function */
	u8                             blendfunc;
	unsigned long                  num_cliprect;
	struct g2d_rect                *cliprect;
	/* additional blit control information */
	int			       flags;

	/* destination memory */
	struct g2d_meminfo             *dmeminfo;
	/* signed stride, the number of bytes per pixel */
	unsigned long                  dst_bpp;
	/* pixel offset from start of dest surface to start of blt rectangle */
	unsigned long                  dstx, dsty;
	unsigned long                  dst_sizex, dst_sizey;     /* blt size */
	int			       dst_format;             /* dest format */
	/* size of dest surface in pixels */
	unsigned long                  dst_surfwidth;
	/* size of dest surface in pixels */
	unsigned long                  dst_surfheight;

	/* source mem, (source fields are also used for patterns) */
	struct g2d_meminfo             *smeminfo;
	/* signed stride, the number of bytes per pixel */
	unsigned long                  src_bpp;
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
	struct g2d_meminfo             *pat_meminfo;
	unsigned long                  patx, paty;
	unsigned long                  pat_sizex, patsizey;
	/* byte offset from start of allocation to start of pattern */
	unsigned long                  patoffset;
	bool                           pat_exist;
	bool                           need_synclast;
};

struct g2d_context {
	struct g2d_meminfo	*patsurf[MAX_PATTERN_BUF_RESERVED];
	u32			cur_patbuf;
	void __iomem		*reg_base;
	struct ring_bufinfo	ringbuf;
	struct fence_bufinfo	fencebuf;
	struct sync_object	sync_object;
	wait_queue_head_t	bldwq;
};

#endif /* __G2D_H_ */
