/*
 * CSR sirfsoc Graphics 2D driver.
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/of_platform.h>
#include <linux/suspend.h>
#include <video/sirfsoc_vdss.h>
#include <vpp.h>
#include <vdss.h>
#include <video/sirfsoc_g2d.h>
#include "g2d.h"

#define G2D_DEV_NAME "g2d"
#define G2D_DRI_NAME "g2d"

#define G2D_DELAY_MAX 3000UL

struct g2d_device_data {
	struct miscdevice	misc_dev;
	void __iomem		*reg_base;
	struct clk		*clk;
	int			irq;
	void			*rb_vaddr;
	dma_addr_t		rb_paddr;
	unsigned int		rb_size;
	struct g2d_context	*context;

	struct g2d_meminfo	mem_src_info[G2D_MEMINFO_MAX];
	struct g2d_meminfo	mem_dst_info[G2D_MEMINFO_MAX];
	int			cur_mem_info;

	struct platform_device	*dev;
	struct dentry		*debugfs_dir;
	struct mutex		rb_lock;
};

static int get_vpp_in_fmt(int fmt)
{
	int vpp_fmt = -1;

	switch (fmt) {
	case G2D_EX_YUYV:
		vpp_fmt = VDSS_PIXELFORMAT_YUYV;
		break;
	case G2D_EX_YVYU:
		vpp_fmt = VDSS_PIXELFORMAT_YVYU;
		break;
	case G2D_EX_UYVY:
		vpp_fmt = VDSS_PIXELFORMAT_UYVY;
		break;
	case G2D_EX_VYUY:
		vpp_fmt = VDSS_PIXELFORMAT_VYUY;
		break;
	case G2D_EX_NV12:
		vpp_fmt = VDSS_PIXELFORMAT_NV12;
		break;
	case G2D_EX_NV21:
		vpp_fmt = VDSS_PIXELFORMAT_NV21;
		break;
	case G2D_EX_YV12:
		vpp_fmt = VDSS_PIXELFORMAT_YV12;
		break;
	case G2D_EX_I420:
		vpp_fmt = VDSS_PIXELFORMAT_I420;
		break;
	default:
		g2d_err("Unsupported format:%d!\n", fmt);
		break;
	}

	return vpp_fmt;
}

static int get_vpp_out_fmt(int fmt)
{
	int vpp_fmt = -1;

	switch (fmt) {
	case G2D_RGB565:
		vpp_fmt = VDSS_PIXELFORMAT_565;
		break;
	case G2D_EX_BGRX8880:
		vpp_fmt = VDSS_PIXELFORMAT_BGRX_8880;
		break;
	case G2D_EX_YUYV:
		vpp_fmt = VDSS_PIXELFORMAT_YUYV;
		break;
	case G2D_EX_YVYU:
		vpp_fmt = VDSS_PIXELFORMAT_YVYU;
		break;
	case G2D_EX_UYVY:
		vpp_fmt = VDSS_PIXELFORMAT_UYVY;
		break;
	case G2D_EX_VYUY:
		vpp_fmt = VDSS_PIXELFORMAT_VYUY;
		break;
	default:
		g2d_err("Unsupported format:%d!\n", fmt);
		break;
	}

	return vpp_fmt;
}

static int get_vdss_field(enum g2d_ex_field field)
{
	enum vdss_field vdss_fid;

	switch (field) {
	case G2D_EX_FIELD_NONE:
		vdss_fid = VDSS_FIELD_NONE;
		break;
	case G2D_EX_FIELD_SEQ_TB:
		vdss_fid = VDSS_FIELD_SEQ_TB;
		break;
	case G2D_EX_FIELD_SEQ_BT:
		vdss_fid = VDSS_FIELD_SEQ_BT;
		break;
	case G2D_EX_FIELD_INTERLACED_TB:
		vdss_fid = VDSS_FIELD_INTERLACED_TB;
		break;
	case G2D_EX_FIELD_INTERLACED_BT:
		vdss_fid = VDSS_FIELD_INTERLACED_BT;
		break;
	default:
		g2d_err("Unsupported field:%d!\n", field);
		break;
	}

	return vdss_fid;
}

static int g2d_check_size(struct sirf_g2d_rect_wh *new_rect,
	struct sirf_g2d_surface *surf)
{
	if (surf->width == 0 || surf->height == 0) {
		g2d_err("invalid surface size\n");
		return -EINVAL;
	}

	if (new_rect->x < 0) {
		new_rect->w += new_rect->x;
		new_rect->x = 0;
	}

	if (new_rect->y < 0) {
		new_rect->h += new_rect->y;
		new_rect->y = 0;
	}

	new_rect->w = (new_rect->w < surf->width) ?
			new_rect->w : surf->width;
	new_rect->h = (new_rect->h < surf->height) ?
			new_rect->h : surf->height;

	if (new_rect->x + new_rect->w > surf->width)
		new_rect->w = surf->width - new_rect->x;
	if (new_rect->y + new_rect->h > surf->height)
		new_rect->h = surf->height  - new_rect->y;

	if (new_rect->w == 0 || new_rect->h == 0) {
		g2d_err("invalid rectangle size\n");
		return -EINVAL;
	}

	return 0;
}

/*
 * Perform yuv2rgb bitbld with memory to memory mode of YUV.
 * Bitbid with rgb color mode in both src and dest MUST be performed
 * with G2D.
 */
int g2d_draw_with_sirfvpp(struct sirf_g2d_bltparams *params)
{
	struct vdss_vpp_op_params vpp_params = {0};
	struct vdss_rect srcrc, dstrc;
	int fmt;
	int ret;

	srcrc.left = params->src_rc.x;
	srcrc.top = params->src_rc.y;
	srcrc.right = params->src_rc.x + params->src_rc.w - 1;
	srcrc.bottom = params->src_rc.y + params->src_rc.h - 1;

	dstrc.left = params->dst_rc.x;
	dstrc.top = params->dst_rc.y;
	dstrc.right = params->dst_rc.x + params->dst_rc.w - 1;
	dstrc.bottom = params->dst_rc.y + params->dst_rc.h - 1;

	fmt = get_vpp_in_fmt(params->src.format);
	if (fmt <= 0)
		return -EINVAL;

	vpp_params.type = VPP_OP_BITBLT;
	vpp_params.op.blt.src_surf.fmt = fmt;
	vpp_params.op.blt.src_surf.width = params->src.width;
	vpp_params.op.blt.src_surf.height = params->src.height;
	vpp_params.op.blt.src_surf.base = params->src.paddr;
	vpp_params.op.blt.src_surf.field = get_vdss_field(params->src.field);
	vpp_params.op.blt.src_rect = srcrc;

	fmt = get_vpp_out_fmt(params->dst.format);
	if (fmt <= 0)
		return -EINVAL;
	vpp_params.op.blt.dst_surf[0].fmt = fmt;
	vpp_params.op.blt.dst_surf[0].width = params->dst.width;
	vpp_params.op.blt.dst_surf[0].height = params->dst.height;
	vpp_params.op.blt.dst_surf[0].base = params->dst.paddr;
	vpp_params.op.blt.dst_surf[0].field = get_vdss_field(params->dst.field);
	vpp_params.op.blt.dst_rect = dstrc;

	vpp_params.op.blt.interlace.di_mode = VDSS_VPP_DI_WEAVE;

	/*vpp color ctrl*/
	vpp_params.op.blt.color_ctrl.brightness = 0;
	vpp_params.op.blt.color_ctrl.contrast = 128;
	vpp_params.op.blt.color_ctrl.hue = 0;
	vpp_params.op.blt.color_ctrl.saturation = 128;

#ifdef CONFIG_SIRF_G2D_DEBUG_LOG
	g2d_inf("src:baddr:%x, bw:%d, bh:%d, fmt:%d, field:%d[%d,%d,%d,%d]\n",
		vpp_params.op.blt.src_surf.base,
		vpp_params.op.blt.src_surf.width,
		vpp_params.op.blt.src_surf.height,
		vpp_params.op.blt.src_surf.fmt,
		vpp_params.op.blt.src_surf.field,
		srcrc.left, srcrc.top, srcrc.right, srcrc.bottom);
	g2d_inf("dst:baddr:%x, bw:%d, bh:%d, fmt:%d, field:%d[%d,%d,%d,%d]\n",
		vpp_params.op.blt.dst_surf[0].base,
		vpp_params.op.blt.dst_surf[0].width,
		vpp_params.op.blt.dst_surf[0].height,
		vpp_params.op.blt.dst_surf[0].fmt,
		vpp_params.op.blt.dst_surf[0].field,
		dstrc.left, dstrc.top, dstrc.right, dstrc.bottom);
#endif

	ret = sirfsoc_vpp_present(NULL, &vpp_params);
	return ret;
}

static u32 g2d_read_reg(struct g2d_context *context, u32 offset)
{
	return readl(context->reg_base + offset);
}

static void g2d_write_reg(struct g2d_context *context,
			  u32 offset, u32 value)
{
	writel(value, context->reg_base + offset);
}

static inline u32 g2d_cmd_set_reg(u32 start_offset, u32 num_reg)
{
	u32 val = (start_offset & CMD_SET_REG_START_MASK) |
		   CMD_SET_REG_FOLLOW(num_reg) |
		   G2D_CMD_OP(G2DCMD_SET_REGISTER);
	return val;
}

static void g2d_log_ringbuf(struct g2d_context *context)
{
	u32 i;

	g2d_inf("size   = 0x%.8x\n", context->ringbuf.size << 2);
	g2d_inf("physical address: 0x%lx\n", context->ringbuf.paddr);
	g2d_inf("virtual address: 0x%p\n", context->ringbuf.vaddr);
	g2d_inf("buffer dumpped:\n");
	for (i = 0; i <= (DRAW_CTL + 0xC); i += 4)
		g2d_inf("R%.8x  = %.8x\n", i, g2d_read_reg(context, i));
}

static int g2d_commit_cmdlist(struct g2d_context *context, u32 *bltcmd,
			      u32 cmdlist_size,
			      u32 fence_write_idx)
{
	/*
	 * The value in RB_RD/WR_PTR register.
	 * the unit is DWORD(four bytes).
	 */
	u32 rptr;
	u32 wptr;
	u32 free;
	struct ring_bufinfo *ring = &context->ringbuf;
	void *dst;

	if (cmdlist_size > (ring->size - RINGBUFFULLGAP))
		return -EINVAL;

	rptr = g2d_read_reg(context, RB_RD_PTR);
	wptr = g2d_read_reg(context, RB_WR_PTR);
	if (rptr > wptr)
		free = rptr - wptr;
	else {
		if ((wptr + cmdlist_size) >= ring->size)
			free = rptr;
		else
			free = ring->size - wptr + rptr - 1;
	}

	if ((cmdlist_size + RINGBUFFULLGAP) > free)
		return -EAGAIN;

	if ((wptr + cmdlist_size) >= ring->size) {
		u32 len;

		dst = ring->vaddr + (wptr << 2);
		len = (ring->size - wptr - 1) << 2;
		memset(dst, 0x0, len);
		wptr = 0;
	}

	bltcmd[fence_write_idx] = context->sync_object.cur_id;
	context->sync_object.work_id = context->sync_object.cur_id;
	context->sync_object.cur_id++;
	if (context->sync_object.cur_id == 0)
		context->sync_object.cur_id = G2D_FENCE_G2D_START;
	dst = ring->vaddr + (wptr << 2);
	memcpy(dst, bltcmd, (cmdlist_size << 2));
	wptr += cmdlist_size;

	g2d_write_reg(context, RB_WR_PTR, wptr);
	return 0;
}

static u32 calc_intersection(struct g2d_rect *rcldst, struct g2d_rect *rclclip,
			     u32 num_clip_rects)
{
	u32 intersection = 0;
	struct g2d_rect *rclout;
	u32 nums = num_clip_rects;

	for (rclout = rclclip; nums > 0; rclclip++, nums--) {
		rclout->left = max(rcldst->left, rclclip->left);
		rclout->right = min(rcldst->right, rclclip->right);

		if (rclout->left < rclout->right) {
			rclout->top = max(rcldst->top, rclclip->top);
			rclout->bottom = min(rcldst->bottom, rclclip->bottom);
			if (rclout->top < rclout->bottom) {
				rclout++;
				intersection++;
			}
		}
	}
	return intersection;
}

static u32 g2d_pat_surfctl(struct g2d_context *dcontext,
			   struct g2d_bltinfo *bltinfo,
			   struct g2d_meminfo **meminfo)
{
	u32 i = 0;
	u32 cur_syncid = 0;
	bool found = false;
	u32 dsyncid = 0;
	u32 max_pat = MAX_PATTERN_BUF_RESERVED;
	struct g2d_meminfo *patinfo;

	/* Get Pattern Surface allocation */
	for (i = dcontext->cur_patbuf; i < max_pat && !found; i++) {
		if (dcontext->patsurf[i]->sync_object == NULL) {
			*meminfo = dcontext->patsurf[i];
			dcontext->cur_patbuf = i + 1;
			found = true;
		} else {
			/* read the sync id, and compare it with the
			 * sync object in the meminfo */
			patinfo = dcontext->patsurf[i];
			cur_syncid = *(u32 *)patinfo->sync_object->vaddr;
			dsyncid = patinfo->desired_syncid;

			if (cur_syncid >= dsyncid ||
			    (dsyncid - cur_syncid) > SYNCOBJECTGAP) {
				*meminfo = dcontext->patsurf[i];
				dcontext->cur_patbuf = i + 1;
				found = true;
			}
		}
	}
	if (found)
		goto out;
	/* continue to find from 0, found = false now */
	for (i = 0; i < dcontext->cur_patbuf && !found; i++) {
		if (dcontext->patsurf[i]->sync_object == NULL) {
			*meminfo = dcontext->patsurf[i];
			dcontext->cur_patbuf = i + 1;
			found = true;
		} else {
			/* read the sync id, and compare it
			 *  with the sync object in the meminfo*/
			patinfo = dcontext->patsurf[i];
			cur_syncid = *(u32 *)patinfo->sync_object->vaddr;
			dsyncid = dcontext->patsurf[i]->desired_syncid;

			if (cur_syncid >= dsyncid ||
			    (dsyncid - cur_syncid) > SYNCOBJECTGAP) {
				*meminfo = dcontext->patsurf[i];
				dcontext->cur_patbuf = i + 1;
				found = true;
			}
		}
	}

	if (!found)
		return -EINVAL;
out:
	return 0;
}

static u32 g2d_clip_check(struct g2d_registers *g2d_regs,
			  struct g2d_rect *rclclip, u32 *cmd)
{
	unsigned long drawctrl = g2d_regs->draw_ctrl;
	u32 size = 0;

	drawctrl |= (1 << G2D_DRAWCTRL_CLIP_SHIFT);

	g2d_regs->clip_lt = 0;
	g2d_regs->clip_rb = 0;

	g2d_regs->clip_lt = rclclip->left & RECT_LEFT_MASK;
	g2d_regs->clip_lt |= RECT_TOP(rclclip->top);
	g2d_regs->clip_rb = rclclip->right & RECT_RIGHT_MASK;
	g2d_regs->clip_rb |= RECT_BOTTOM(rclclip->bottom);

	g2d_regs->draw_ctrl = drawctrl;
	if (cmd) {
		*cmd++ = g2d_cmd_set_reg(CLIP_LT, 2);
		*cmd++ = g2d_regs->clip_lt;
		*cmd++ = g2d_regs->clip_rb;

		size += 3;
	}

	return size;
}

static u32 g2d_surface_check(struct g2d_bltinfo *info,
			     struct g2d_registers *g2d_regs, u32 *cmd)
{
	bool src_exist = info->src_exist;
	bool pat_exist = info->pat_exist;
	u32 size = 0;
	u32 stride;

	if (src_exist) {
		stride = info->src_bpp * info->src_surfwidth;
		g2d_regs->src_offset = info->smeminfo->offset;
		g2d_regs->src_format =
		    SRC_FORMAT_NONPREMUL(info->blendfunc) |
		    SRC_FORMAT_FORMAT(info->src_format) |
		    (stride & SRC_FORMAT_STRIDE_MASK);

		g2d_regs->src_lt = 0;
		g2d_regs->src_rb = 0;

		g2d_regs->src_lt = info->srcx & RECT_LEFT_MASK;
		g2d_regs->src_lt |= RECT_TOP(info->srcy);
		g2d_regs->src_rb =
		    (info->srcx + info->src_sizex) & RECT_RIGHT_MASK;
		g2d_regs->src_rb |=
		    RECT_BOTTOM(info->srcy + info->src_sizey);

		if (cmd) {
			*cmd++ = g2d_cmd_set_reg(SRC_OFFSET, 4);
			*cmd++ = g2d_regs->src_offset;
			*cmd++ = g2d_regs->src_format;
			*cmd++ = g2d_regs->src_lt;
			*cmd++ = g2d_regs->src_rb;
			size += 5;
		}
	}

	if (pat_exist) {
		g2d_regs->pat_offset = info->pat_meminfo->offset;

		if (cmd) {
			*cmd++ = g2d_cmd_set_reg(PAT_OFFSET, 1);
			*cmd++ = g2d_regs->pat_offset;
			size += 2;
		}
	}

	g2d_regs->dst_offset = (unsigned long)info->dmeminfo->offset;
	g2d_regs->dst_format = DST_FORMAT_FORMAT(info->dst_format) |
	    ((info->dst_bpp * info->dst_surfwidth) & DST_FORMAT_STRIDE_MASK);

	g2d_regs->dst_lt = 0;
	g2d_regs->dst_rb = 0;

	g2d_regs->dst_lt = info->dstx & RECT_LEFT_MASK;
	g2d_regs->dst_lt |= RECT_TOP(info->dsty);
	g2d_regs->dst_rb = (info->dstx + info->dst_sizex) & RECT_RIGHT_MASK;
	g2d_regs->dst_rb |= RECT_BOTTOM(info->dsty + info->dst_sizey);

	if (cmd) {
		*cmd++ = g2d_cmd_set_reg(DST_OFFSET, 4);
		*cmd++ = g2d_regs->dst_offset;
		*cmd++ = g2d_regs->dst_format;

		*cmd++ = g2d_regs->dst_lt;
		*cmd++ = g2d_regs->dst_rb;

		size += 5;
	}

	return size;
}

static u32 g2d_alphablend_check(struct g2d_bltinfo *bltinfo,
				struct g2d_registers *g2d_regs, u32 *cmd)
{
	unsigned long drawctrl = g2d_regs->draw_ctrl;
	u32 ret = 0;
	u32 tmp = 0;

	drawctrl &= ~(3 << G2D_DRAWCTRL_ALPHA_SHIFT);

	/* Both constant and perpixel alpha */
	tmp = G2D_BLIT_ALPHA_MASK;
	if ((bltinfo->flags & tmp) == tmp) {
		drawctrl |= (3 << G2D_DRAWCTRL_ALPHA_SHIFT);
		g2d_regs->gbl_alpha = bltinfo->global_alpha;
		if (cmd) {
			*cmd++ = g2d_cmd_set_reg(GBL_ALPHA, 1);
			*cmd++ = g2d_regs->gbl_alpha;
			ret += 2;
		}
	} else if (bltinfo->flags & G2D_BLIT_PERPIXEL_ALPHA) {
		drawctrl |= (2 << G2D_DRAWCTRL_ALPHA_SHIFT);
	} else if (bltinfo->flags & G2D_BLIT_GLOBAL_ALPHA) {
		drawctrl |= (1 << G2D_DRAWCTRL_ALPHA_SHIFT);
		g2d_regs->gbl_alpha = bltinfo->global_alpha;

		if (cmd) {
			*cmd++ = g2d_cmd_set_reg(GBL_ALPHA, 1);
			*cmd++ = g2d_regs->gbl_alpha;
			ret += 2;
		}
	}

	g2d_regs->draw_ctrl = drawctrl;

	return ret;
}

static u32 g2d_colorkey_check(struct g2d_bltinfo *bltinfo,
			      struct g2d_registers *g2d_regs, u32 *cmd)
{
	unsigned long drawctrl = g2d_regs->draw_ctrl;
	u32 ret = 0;

	drawctrl &= ~(1 << G2D_DRAWCTRL_COLORKEY_MODE_SHIFT);

	if (bltinfo->flags & G2D_BLIT_SRC_COLORKEY)
		drawctrl |= (0 << G2D_DRAWCTRL_COLORKEY_MODE_SHIFT);
	else
		drawctrl |= (1 << G2D_DRAWCTRL_COLORKEY_MODE_SHIFT);

	drawctrl |= (1 << G2D_DRAWCTRL_TRANSPARENT_SHIFT);

	g2d_regs->color_key = bltinfo->colorkey;

	if (cmd) {
		*cmd++ = g2d_cmd_set_reg(COLOR_KEY, 1);
		*cmd++ = g2d_regs->color_key;
		ret += 2;
	}

	g2d_regs->draw_ctrl = drawctrl;
	return ret;
}

static u32 g2d_colorfill_check(struct g2d_bltinfo *bltinfo,
			       struct g2d_registers *g2d_regs, u32 *cmd)
{
	unsigned long drawctrl = g2d_regs->draw_ctrl;
	u32 size = 0;

	drawctrl |= (1 << G2D_DRAWCTRL_COLORFILL_SHIFT);

	g2d_regs->fill_color = bltinfo->fill_color;

	g2d_regs->draw_ctrl = drawctrl;
	if (cmd) {
		*cmd++ = g2d_cmd_set_reg(FILL_COLOR, 1);
		*cmd++ = g2d_regs->fill_color;
		size += 2;
	}

	return size;
}

static void g2d_swizzle_check(struct g2d_bltinfo *bltinfo,
			      struct g2d_registers *g2d_regs)
{
	unsigned long drawctrl = g2d_regs->draw_ctrl;

	drawctrl &=
	    ~(3 << G2D_DRAWCTRL_ROTATION_SHIFT |
	      1 << G2D_DRAWCTRL_FLIP_H_SHIFT |
	      1 << G2D_DRAWCTRL_FLIP_V_SHIFT);

	switch (bltinfo->flags & G2D_BLIT_ROT_MASK) {
	case G2D_BLIT_ROT_90:
		drawctrl |= (1 << G2D_DRAWCTRL_ROTATION_SHIFT);
		break;
	case G2D_BLIT_ROT_180:
		drawctrl |= (2 << G2D_DRAWCTRL_ROTATION_SHIFT);
		break;
	case G2D_BLIT_ROT_270:
		drawctrl |= (3 << G2D_DRAWCTRL_ROTATION_SHIFT);
		break;
	default:
		break;
	}
	/* flip and rotate can work together, so it can't check in one switch */
	if (bltinfo->flags & G2D_BLIT_FLIP_H)
		drawctrl |= (1 << G2D_DRAWCTRL_FLIP_H_SHIFT);

	if (bltinfo->flags & G2D_BLIT_FLIP_V)
		drawctrl |= (1 << G2D_DRAWCTRL_FLIP_V_SHIFT);

	g2d_regs->draw_ctrl = drawctrl;
}

static u32 g2d_build_area(struct g2d_device_data *g2d_dev,
			  struct g2d_bltinfo *bltinfo,
			  struct g2d_rect *rclclip)
{
	struct g2d_registers g2dregs;
	u32 submit_size = 0;
	u32 bltcmd[G2D_MAX_BLIT_CMD_SIZE] = { 0, };
	u32 cur_cmdindex = 0;
	struct g2d_context *context = g2d_dev->context;
	u32 fence_write_idx;
	int ret = 0;
	u32 delay_count;

	memset(&g2dregs, 0xFF, sizeof(g2dregs));
	g2dregs.draw_ctrl = 0;
	g2dregs.draw_ctrl |= (bltinfo->rop3 & DRAW_CTL_ROP3_MASK);

	if (context->sync_object.work_id) {
		bltcmd[cur_cmdindex++] = G2D_CMD_OP(G2D_CMD_FENCE_WAIT) |
		    CMD_FENCE_ADDR(context->sync_object.paddr);
		bltcmd[cur_cmdindex++] = context->sync_object.work_id;
	}

	/* probe the shape and material. */
	if (bltinfo->flags & (G2D_BLIT_ROT_MASK | G2D_BLIT_FLIP_MASK))
		g2d_swizzle_check(bltinfo, &g2dregs);

	if (bltinfo->flags & G2D_BLIT_CLIP_ENABLE) {
		cur_cmdindex += g2d_clip_check(&g2dregs, rclclip,
						 &bltcmd[cur_cmdindex]);
	}
	cur_cmdindex += g2d_surface_check(bltinfo, &g2dregs,
						 &bltcmd[cur_cmdindex]);
	if (bltinfo->flags & G2D_BLIT_ALPHA_MASK) {
		cur_cmdindex += g2d_alphablend_check(bltinfo, &g2dregs,
						       &bltcmd[cur_cmdindex]);
	}
	if (bltinfo->flags & G2D_BLIT_TRANSPARENT_ENABLE) {
		cur_cmdindex += g2d_colorkey_check(bltinfo, &g2dregs,
							&bltcmd[cur_cmdindex]);
	}
	if (bltinfo->flags & G2D_BLIT_COLOR_FILL) {
		cur_cmdindex += g2d_colorfill_check(bltinfo, &g2dregs,
						      &bltcmd[cur_cmdindex]);
	}

	bltcmd[cur_cmdindex++] = g2d_cmd_set_reg(DRAW_CTL, 1);
	bltcmd[cur_cmdindex++] = g2dregs.draw_ctrl;

	bltcmd[cur_cmdindex++] = G2D_CMD_OP(G2DCMD_WRITE_FENCE_INTERRUPT) |
				CMD_FENCE_ADDR(context->sync_object.paddr);
	fence_write_idx = cur_cmdindex;
	cur_cmdindex++;

	submit_size = cur_cmdindex;
	submit_size = ((submit_size + 3) & ~3);

	/* Submit Command to ringbuf */
	delay_count = 0;
	do {
		mutex_lock(&g2d_dev->rb_lock);
		ret = g2d_commit_cmdlist(context, bltcmd, submit_size,
					 fence_write_idx);
		mutex_unlock(&g2d_dev->rb_lock);

		if (ret == 0) {
			break;
		} else if (ret == -EAGAIN) {
			delay_count++;
			usleep_range(1000, 1500);
		} else
		       BUG();

		/*
		 * Something MUST be wrong if RB_RD_PTR
		 * doesn't move in seconds.
		 */
		if (delay_count > G2D_DELAY_MAX) {
			g2d_log_ringbuf(context);
			ret = -EIO;
			break;
		}
	} while (ret == -EAGAIN);

	return ret;
}

/*
 * Only RGB2RGB bitblt could be performed with G2D
 * In Atlas7. please refer to g2d_draw_with_sirfvpp
 * for yuv2rgb with VPP.
 */
static int g2d_draw_with_sirfg2d(struct g2d_device_data *g2d_dev,
				 struct sirf_g2d_bltparams *params)
{
	struct g2d_bltinfo bltinfo;
	struct g2d_rect rcldst, rclsrc;
	unsigned long cliprects;
	struct g2d_context *context = g2d_dev->context;
	struct g2d_rect *rclclip = NULL;
	int i;
	int ret = 0;

	g2d_dev->cur_mem_info++;
	if (g2d_dev->cur_mem_info >= G2D_MEMINFO_MAX)
		g2d_dev->cur_mem_info = 0;

	memset(&bltinfo, 0, sizeof(bltinfo));
	bltinfo.rop3 = params->rop3;
	bltinfo.fill_color = params->fill_color;
	bltinfo.colorkey = params->color_key;
	bltinfo.global_alpha = params->global_alpha;
	bltinfo.blendfunc = params->blend_func;
	bltinfo.num_cliprect = params->num_rects;
	bltinfo.cliprect = (struct g2d_rect *)params->rects;
	bltinfo.flags = (params->flags & ~G2D_BLT_WAIT_COMPLETE);

	bltinfo.dmeminfo = &g2d_dev->mem_dst_info[g2d_dev->cur_mem_info];
	bltinfo.dmeminfo->offset = params->dst.paddr;
	bltinfo.dst_bpp = params->dst.bpp;
	bltinfo.dstx = params->dst_rc.x;
	bltinfo.dsty = params->dst_rc.y;
	bltinfo.dst_sizex = params->dst_rc.w;
	bltinfo.dst_sizey = params->dst_rc.h;
	bltinfo.dst_format = params->dst.format;
	bltinfo.dst_surfwidth = params->dst.width;
	bltinfo.dst_surfheight = params->dst.height;

	bltinfo.pat_exist = false;
	bltinfo.src_exist = false;

	if (params->src.paddr > 0) {
		bltinfo.smeminfo =
		       &g2d_dev->mem_src_info[g2d_dev->cur_mem_info];
		bltinfo.smeminfo->offset = params->src.paddr;
		bltinfo.src_bpp = params->src.bpp;
		bltinfo.srcx = params->src_rc.x;
		bltinfo.srcy = params->src_rc.y;
		bltinfo.src_sizex = params->src_rc.w;
		bltinfo.src_sizey = params->src_rc.h;
		bltinfo.src_format = params->src.format;
		bltinfo.src_surfwidth = params->src.width;
		bltinfo.src_surfheight = params->src.height;
		bltinfo.src_exist = true;
	}

	if (params->flags & G2D_BLT_WAIT_COMPLETE)
		bltinfo.need_synclast = true;

	/* Dest rect */
	rcldst.left = bltinfo.dstx;
	rcldst.right = rcldst.left + bltinfo.dst_sizex;
	rcldst.top = bltinfo.dsty;
	rcldst.bottom = rcldst.top + bltinfo.dst_sizey;

	/* Check Pattern */
	if (bltinfo.pat_exist) {
		struct g2d_meminfo *patsurf = NULL;

		if (g2d_pat_surfctl(context, &bltinfo, &patsurf) != 0) {
			g2d_err("Can't get available Pattern Surface\n");
			return -EFAULT;
		}

		bltinfo.pat_meminfo = patsurf;
		bltinfo.pat_meminfo->sync_object = &context->sync_object;
		bltinfo.pat_meminfo->desired_syncid =
		    context->sync_object.cur_id;
	}
	bltinfo.dmeminfo->sync_object = &context->sync_object;
	bltinfo.dmeminfo->desired_syncid = context->sync_object.cur_id;

	/* src sould be set in bitbld mode. */
	if (bltinfo.src_exist) {
		struct g2d_meminfo *src_meminfo = bltinfo.smeminfo;

		rclsrc.left = bltinfo.srcx;
		rclsrc.right = rclsrc.left + bltinfo.src_sizex;
		rclsrc.top = bltinfo.srcy;
		rclsrc.bottom = rclsrc.top + bltinfo.src_sizey;
		src_meminfo->sync_object = &context->sync_object;
		src_meminfo->desired_syncid = context->sync_object.cur_id;
	}

	if (bltinfo.num_cliprect) {
		cliprects = calc_intersection(&rcldst, bltinfo.cliprect,
					      bltinfo.num_cliprect);

		if (cliprects > 0)
			bltinfo.flags |= G2D_BLIT_CLIP_ENABLE;
		rclclip = bltinfo.cliprect;
	} else {
		cliprects = 1;
		rclclip = &rcldst;
	}

	/* Need do nothing is Okay too */
	if (cliprects == 0)
		return 0;

	for (i = 0; i < cliprects; i++)
		ret = g2d_build_area(g2d_dev, &bltinfo, &rclclip[i]);

	return ret;
}

#define RGB 0
#define YUV 1
static int g2d_bitblt(struct g2d_device_data *g2d_dev, unsigned long arg)
{
	struct sirf_g2d_bltparams params;
	int sformat;
	int ret = 0;

	if (copy_from_user(&params, (void __user *)arg,
			   sizeof(params)))
		return -EFAULT;

	switch (params.src.format) {
	case G2D_ARGB8888:
	case G2D_ABGR8888:
	case G2D_RGB565:
		sformat = RGB;
		break;
	case G2D_EX_YUYV:
	case G2D_EX_YVYU:
	case G2D_EX_UYVY:
	case G2D_EX_VYUY:
	case G2D_EX_NV12:
	case G2D_EX_NV21:
	case G2D_EX_YV12:
	case G2D_EX_I420:
		sformat = YUV;
		break;
	default:
		g2d_err("invalid source format:%d\n", params.src.format);
		return -EINVAL;
	}

	if (sformat == RGB) {
		switch (params.dst.format) {
		case G2D_ARGB8888:
		case G2D_RGB565:
			break;
		default:
			g2d_err("invalid destination format:%d\n",
				params.dst.format);
			return -EINVAL;
		}
	} else if (sformat == YUV) {
		switch (params.dst.format) {
		case G2D_EX_YUYV:
		case G2D_EX_YVYU:
		case G2D_EX_UYVY:
		case G2D_EX_VYUY:
		case G2D_EX_BGRX8880:
		case G2D_RGB565:
			break;
		default:
			g2d_err("invalid destination format:%d\n",
				params.dst.format);
			return -EINVAL;
		}
	}

	if (!(params.flags & G2D_BLT_COLOR_FILL)) {
		ret = g2d_check_size(&params.src_rc, &params.src);
		if (ret)
			return ret;
	}

	ret = g2d_check_size(&params.dst_rc, &params.dst);
	if (ret)
		return ret;

	if (sformat == YUV)
		return g2d_draw_with_sirfvpp(&params);

	if (params.flags & (G2D_BLT_ROT_90 | G2D_BLT_ROT_270)) {
		if (params.src_rc.w != params.dst_rc.h ||
			params.src_rc.h != params.dst_rc.w) {
			g2d_err("invalid rectangle for rotation blt\n");
			return -EINVAL;
		}
	} else if (!(params.flags & G2D_BLT_COLOR_FILL)) {
		if (params.num_rects == 0 &&
			(params.src_rc.w != params.dst_rc.w ||
			params.src_rc.h != params.dst_rc.h)) {
			g2d_err("dismatch rectangle for blt\n");
			return -EINVAL;
		}
	}

	return g2d_draw_with_sirfg2d(g2d_dev, &params);
}

static int g2d_wait(struct g2d_device_data *g2d_dev)
{
	struct sync_object *sync_object;
	u32 *ret_sync;
	u32 dsyncid;
	struct g2d_meminfo *meminfo;
	int ret = 0;

	if (g2d_dev->cur_mem_info == -1)
		return 0;

	meminfo = &g2d_dev->mem_dst_info[g2d_dev->cur_mem_info];
	sync_object = meminfo->sync_object;
	dsyncid = meminfo->desired_syncid;

	ret_sync = sync_object->vaddr;
	ret = wait_event_timeout(g2d_dev->context->bldwq,
				 (*ret_sync >= dsyncid ||
				  ((dsyncid - *ret_sync) > SYNCOBJECTGAP)),
				 HZ * 2);

	if (!ret) {
		g2d_err("Wait FenceBack Timeout");
		g2d_err("DesiredSyncID =0x%.8x\n", dsyncid);
		g2d_err("ReadID = 0x%.8x\n", *ret_sync);
	}
	return -EIO;
}

static void g2d_clear_interrupt(struct g2d_context *context,
				u32 interrupt_index)
{
	u32 reg_int_clear;

	reg_int_clear = 1UL << interrupt_index;
	g2d_write_reg(context, INTERRUPT_CLEAR, reg_int_clear);
}

static void g2d_enable_interrupt(struct g2d_context *context,
				 u32 interrupt_index)
{
	u32 reg_int_enable;

	reg_int_enable = g2d_read_reg(context, INTERRUPT_ENABLE);
	reg_int_enable |= 1UL << interrupt_index;
	g2d_write_reg(context, INTERRUPT_ENABLE, reg_int_enable);
}

static irqreturn_t g2d_irq_handler(int irq, void *data)
{
	struct g2d_device_data *g2d_dev = (struct g2d_device_data *)data;
	struct g2d_context *context = g2d_dev->context;
	u32 intr_status = 0;

	intr_status = g2d_read_reg(context, INTERRUPT_STATUS);
	if (intr_status & (1 << FENCE_INTERRUPT)) {
		g2d_clear_interrupt(context, FENCE_INTERRUPT);
		if (waitqueue_active(&context->bldwq))
			wake_up(&context->bldwq);
	}
	return IRQ_HANDLED;
}

static void g2d_context_init(struct g2d_device_data *g2d_dev)
{
	int i;
	struct g2d_context *context = g2d_dev->context;

	for (i = 0; i < MAX_PATTERN_BUF_RESERVED; i++)
		context->patsurf[i] = NULL;

	context->cur_patbuf = 0;
	context->reg_base = g2d_dev->reg_base;
	context->ringbuf.paddr =
		(g2d_dev->rb_paddr + RING_BUF_ALIGNMENT - 1) &
		(~(RING_BUF_ALIGNMENT - 1));
	context->ringbuf.size = RING_BUF_SIZE / 4;
	context->ringbuf.vaddr = g2d_dev->rb_vaddr +
			(context->ringbuf.paddr - g2d_dev->rb_paddr);

	context->sync_object.paddr =
				(context->ringbuf.paddr + RING_BUF_SIZE +
				 G2D_FENCE_BUF_ALIGNMENT - 1) &
				(~(G2D_FENCE_BUF_ALIGNMENT - 1));

	context->sync_object.vaddr = g2d_dev->rb_vaddr +
	    (context->sync_object.paddr - g2d_dev->rb_paddr);

	context->sync_object.cur_id = G2D_FENCE_G2D_START;
	context->sync_object.work_id = 0;
	init_waitqueue_head(&context->bldwq);
}

static void g2d_hw_init(struct g2d_device_data *g2d_dev)
{
	/*
	 * FrameBuffer is treated as ordinary memroy block.
	 * When blend something into FB, we just set the
	 * physical address of framebuffer todestination.
	 * this register is useless
	 */
	g2d_write_reg(g2d_dev->context, FB_BASE, 0);

	/*
	 * enable DMA command mode. in this mode, a command
	 * buffer is used and the driver sends commands to this buffer,
	 * hardware will fetch the command by DMA and transfer the
	 * commands to registers value. Multiple blt operation can
	 * be batched in the buffer, the later commands only need
	 * update the registers which are different to previous.
	 */
	g2d_write_reg(g2d_dev->context, ENG_CTRL, 0x1);

	/*
	 * The DMA command mode needs a memory block uses and ring buffer.
	 */
	g2d_write_reg(g2d_dev->context, RB_OFFSET,
		      g2d_dev->context->ringbuf.paddr);
	g2d_write_reg(g2d_dev->context, RB_LENGTH,
		      g2d_dev->context->ringbuf.size);
	g2d_write_reg(g2d_dev->context, RB_RD_PTR, 0);
	g2d_write_reg(g2d_dev->context, RB_WR_PTR, 0);
}

/* Misc device layer */
static inline struct g2d_device_data *to_g2d_device_data_priv(struct file *file)
{
	struct miscdevice *dev = file->private_data;

	return container_of(dev, struct g2d_device_data, misc_dev);
}

static int g2d_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int g2d_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long g2d_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct g2d_device_data *g2d_dev = to_g2d_device_data_priv(file);

	switch (cmd) {
	/*
	 * SIRFSOC_G2D_SUBMIT_BITBLD only submits g2d commands to the DMA
	 * command buffer of G2D, it doesn't mean these commands is finished.
	 * If you want to make use of the result of your previous BITBLD,
	 * please call SIRFSOC_G2D_WAIT after your SIRFSOC_G2D_SUBMIT_BITBLD.
	 * calls.
	 *
	 */
	case SIRFSOC_G2D_SUBMIT_BITBLD:
		ret = g2d_bitblt(g2d_dev, arg);
		if (ret) {
			g2d_err("bit bld error %d.\n", ret);
			return -EFAULT;
		}
		break;
	case SIRFSOC_G2D_WAIT:
		return g2d_wait(g2d_dev);
	default:
		g2d_err("No handler for ioctl 0x%08X 0x%08lX\n", cmd, arg);
		return -EINVAL;
	}
	return 0;
}

#if defined(CONFIG_DEBUG_FS)
static int g2d_debug_regs_show(struct seq_file *s, void *data)
{
	struct g2d_device_data *g2d_dev;
	u32 i = 0;

	g2d_dev = (struct g2d_device_data *)s->private;
	for (i = 0; i <= DRAW_CTL; i += 4) {
		seq_printf(s, "Register offset %.8x = %.8x\r\n", i,
			   g2d_read_reg(g2d_dev->context, i));
	}

	/* interrupt status */
	seq_puts(s, "\n");
	seq_printf(s, "Interrupt Enable: %.8x\n",
		   g2d_read_reg(g2d_dev->context, 0x80));
	seq_printf(s, "Interrupt Status: %.8x\n",
		   g2d_read_reg(g2d_dev->context, 0x88));
	return 0;
}

static int g2d_debug_regs_open(struct inode *inode, struct file *file)
{
	return single_open(file, g2d_debug_regs_show, inode->i_private);
}

static const struct file_operations g2d_debug_regs_fops = {
	.open           = g2d_debug_regs_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static int g2d_debug_rbbuf_show(struct seq_file *s, void *data)
{
	struct g2d_device_data *g2d_dev;
	struct g2d_context *context;
	u32 *pcmd;
	u32 i;
	u32 rptr, wptr;

	g2d_dev = (struct g2d_device_data *)s->private;
	context = g2d_dev->context;

	rptr = g2d_read_reg(context, RB_RD_PTR);
	wptr = g2d_read_reg(context, RB_WR_PTR);
	seq_printf(s, "## read pointer:%.8x write pointer:%.8x\n",
		   rptr, wptr);

	seq_printf(s, "## fenceid return:0x%.8x, submit:0x%.8x\n\n",
		   context->sync_object.work_id,
		   context->sync_object.cur_id);
	seq_puts(s, "command buffer:\n");
	pcmd = context->ringbuf.vaddr;
	for (i = 0; i < context->ringbuf.size; i++)
		seq_printf(s, "[%.8x]\t%.8x\n", i, pcmd[i]);

	return 0;
}

static int g2d_debug_rbbuf_open(struct inode *inode, struct file *file)
{
	return single_open(file, g2d_debug_rbbuf_show, inode->i_private);
}

static const struct file_operations g2d_debug_rbbuf_fops = {
	.open           = g2d_debug_rbbuf_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};
#endif

static const struct file_operations g2d_fops = {
	.owner          = THIS_MODULE,
	.open           = g2d_open,
	.release	= g2d_release,
	.unlocked_ioctl = g2d_ioctl,
};

#ifdef CONFIG_PM_SLEEP
static int sirfsoc_g2d_suspend(struct device *dev)
{
	struct g2d_device_data *g2d_dev = dev_get_drvdata(dev);

	g2d_wait(g2d_dev);
	g2d_clear_interrupt(g2d_dev->context, FENCE_INTERRUPT);
	clk_disable_unprepare(g2d_dev->clk);
	return 0;
}

static int sirfsoc_g2d_resume(struct device *dev)
{
	int ret;
	struct g2d_device_data *g2d_dev = dev_get_drvdata(dev);

	ret = clk_prepare_enable(g2d_dev->clk);
	if (ret < 0)
		dev_err(dev, "resume error, failed to open clock\n");
	else {
		g2d_hw_init(g2d_dev);
		g2d_enable_interrupt(g2d_dev->context, FENCE_INTERRUPT);
	}
	return ret;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(sirf_g2d_pm_ops,
			 sirfsoc_g2d_suspend,
			 sirfsoc_g2d_resume);

static int g2d_probe(struct platform_device *pdev)
{
	struct g2d_device_data *g2d_dev;
	struct resource *res;
	int ret = 0;

	g2d_dev = devm_kzalloc(&pdev->dev, sizeof(*g2d_dev), GFP_KERNEL);
	if (!g2d_dev)
		return -ENOMEM;

	g2d_dev->context = devm_kzalloc(&pdev->dev, sizeof(*g2d_dev->context),
					GFP_KERNEL);
	if (!g2d_dev->context)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	g2d_dev->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(g2d_dev->reg_base))
		return PTR_ERR(g2d_dev->reg_base);

	g2d_dev->rb_size = SZ_1M;
	g2d_dev->rb_vaddr = dma_alloc_coherent(&pdev->dev,
					       g2d_dev->rb_size,
					       &g2d_dev->rb_paddr,
					       GFP_KERNEL);

	g2d_dev->dev = pdev;
	g2d_dev->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(g2d_dev->clk)) {
		ret = PTR_ERR(g2d_dev->clk);
		goto free_dma;
	}
	ret = clk_prepare_enable(g2d_dev->clk);
	if (ret < 0) {
		dev_err(&pdev->dev, "Fail to enable g2d clock\n");
		goto free_dma;
	}

	g2d_dev->irq = platform_get_irq(pdev, 0);
	if (g2d_dev->irq < 0) {
		ret = g2d_dev->irq;
		goto free_dma;
	}

	ret = devm_request_irq(&pdev->dev, g2d_dev->irq, g2d_irq_handler,
			       0, "SIRFSOC-G2D", g2d_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Fail to request g2d irq\n");
		goto free_dma;
	}

	g2d_dev->cur_mem_info = -1;
	g2d_context_init(g2d_dev);
	g2d_hw_init(g2d_dev);
	g2d_enable_interrupt(g2d_dev->context, FENCE_INTERRUPT);

#if defined(CONFIG_DEBUG_FS)
	g2d_dev->debugfs_dir = debugfs_create_dir(G2D_DEV_NAME, NULL);
	if (g2d_dev->debugfs_dir != NULL) {
		debugfs_create_file("regs", 0600, g2d_dev->debugfs_dir,
				    g2d_dev, &g2d_debug_regs_fops);
		debugfs_create_file("rbbuf", 0600, g2d_dev->debugfs_dir,
				    g2d_dev, &g2d_debug_rbbuf_fops);
	}
#endif

	g2d_dev->misc_dev.minor	= MISC_DYNAMIC_MINOR;
	g2d_dev->misc_dev.name = G2D_DEV_NAME;
	g2d_dev->misc_dev.fops = &g2d_fops;

	ret = misc_register(&g2d_dev->misc_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register g2d\n");
		goto free_dma;
	}

	platform_set_drvdata(pdev, g2d_dev);

	mutex_init(&g2d_dev->rb_lock);
	dev_info(&pdev->dev, "initialized\n");
	return 0;

free_dma:
	clk_disable_unprepare(g2d_dev->clk);
	dma_free_coherent(&pdev->dev, g2d_dev->rb_size,
			  g2d_dev->rb_vaddr, g2d_dev->rb_paddr);
	return ret;
}

static int g2d_remove(struct platform_device *pdev)
{
	struct g2d_device_data *g2d_dev = platform_get_drvdata(pdev);
	struct g2d_context *context = g2d_dev->context;

	g2d_dev = (struct g2d_device_data *)platform_get_drvdata(pdev);
	g2d_wait(g2d_dev);

	g2d_clear_interrupt(context, BLT_COMPLETE_INTERRUPT);
	g2d_clear_interrupt(context, CMD_BUF_EMPTY_INTERRUPT);
	g2d_clear_interrupt(context, FENCE_INTERRUPT);
	g2d_clear_interrupt(context, BLT_TIMEOUT_INTERRUPT);
	g2d_write_reg(context, INTERRUPT_ENABLE, 0x0);
	if (g2d_dev->rb_vaddr)
		dma_free_writecombine(&pdev->dev, g2d_dev->rb_size,
				      g2d_dev->rb_vaddr, g2d_dev->rb_paddr);
#if defined(CONFIG_DEBUG_FS)
	if (g2d_dev->debugfs_dir != NULL)
		debugfs_remove_recursive(g2d_dev->debugfs_dir);
#endif
	misc_deregister(&g2d_dev->misc_dev);
	return 0;
}

static struct of_device_id g2d_match_tbl[] = {
	{ .compatible = "sirf, atlas7-g2d", },
	{ /* end */ }
};

static struct platform_driver g2d_driver = {
	.probe = g2d_probe,
	.remove = g2d_remove,
	.driver = {
		.name = G2D_DRI_NAME,
		.of_match_table = g2d_match_tbl,
		.pm	= &sirf_g2d_pm_ops,
	},
};

module_platform_driver(g2d_driver);

MODULE_DESCRIPTION("SiRF SoC 2D graphics driver");
MODULE_LICENSE("GPL v2");
