/*
 * CSR sirfsoc BLE library
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
*/

#include <linux/string.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include "ble_defs.h"

static bool first_cmd = true;

struct ble_context *ble_context;

void print_cmd(void *addr, u32 length)
{
	char *taddr = (char *)addr;
	u32 i;

/*    RETAILMSG(1,(TEXT("---------Cmd------------\n")));*/

	for (i = 0; i < length; i++) {
		BLE_MSG(("0x%.8x\n", *(u32 *) taddr));
		taddr += 4;
	}

/*    RETAILMSG(1,(TEXT("--------------------------"))); */

	return;
}

void print_ringbuf_info(struct ble_context *dcontext, u32 submit_size)
{
	u32 i;
	/* size */
	BLE_MSG(("size   = 0x%.8x\n", dcontext->ringbuf.size));
	BLE_MSG(("offset = 0x%.8x\n", dcontext->ringbuf.offset));
	BLE_MSG(("virtualAddr                  = 0x%.8x\n",
		 dcontext->ringbuf.virtual));
	BLE_MSG(("ringbufWritePtrUsedByDriver  = 0x%.8x\n",
		 dcontext->ringbuf_wtptr));
	BLE_MSG(("ringbuf_size_left        = 0x%.8x\n",
		 dcontext->ringbuf_size_left));
	BLE_MSG(("submit_size    = 0x%.8x\n", submit_size));

	for (i = 0; i <= (DRAW_CTL + 0xC); i += 4)
		BLE_MSG(("R%.8x  = %.8x\n", i, ble_read_reg(dcontext, i)));
}

static u32 *__ble_get_ringbuf_space(struct ble_context *dcontext,
				    u32 submit_size)
{
	u32 skipped_cmd = 0;
	u32 rbuf = 0;
	u32 wbuf = 0;
	u32 virtual = 0;

	if (submit_size < (dcontext->ringbuf_size_left))
		goto out;

	rbuf = ble_read_reg(dcontext, RB_RD_PTR);

	/* Wrap around */
	virtual = dcontext->ringbuf.virtual;
	if ((dcontext->ringbuf_wtptr + submit_size) >=
	    ((u32 *)virtual + dcontext->ringbuf.size)) {
		int tmp1 = (int) dcontext->ringbuf_size_left;
		int tmp2 = (int) submit_size;
		/* Wait for enough ringbuffer space */
		while (tmp2 >= (int) (tmp1 - RINGBUFFULLGAP)) {
			wbuf = ((int)dcontext->ringbuf_wtptr - virtual) / 4;

			while (rbuf > wbuf)
				rbuf = ble_read_reg(dcontext, RB_RD_PTR);

			rbuf = ble_read_reg(dcontext, RB_RD_PTR);
			dcontext->ringbuf_size_left = rbuf;
			tmp1 = (int) dcontext->ringbuf_size_left;
		}

		skipped_cmd = (dcontext->ringbuf.virtual +
			       dcontext->ringbuf.size * 4 -
			       (int)dcontext->ringbuf_wtptr) / 4;

		if (skipped_cmd) {
			memset(dcontext->ringbuf_wtptr, 0,
			       skipped_cmd * sizeof(int));
		}
		/* reset pointer to the beginning of ringbuffer */
		dcontext->ringbuf_wtptr = (u32 *) dcontext->ringbuf.virtual;

	} else {
		wbuf = ((int)dcontext->ringbuf_wtptr - virtual) / 4;

		while (rbuf > wbuf)
			rbuf = ble_read_reg(dcontext, RB_RD_PTR);

		dcontext->ringbuf_size_left =
		    dcontext->ringbuf.size - wbuf;
	}
	if (submit_size >= dcontext->ringbuf_size_left) {
		BLE_ERR(("Not Enough ringbuffer Space\n"));
		BLE_ERR(("ringbuf_size_left = 0x%.8x\n",
			 dcontext->ringbuf_size_left));
		BLE_ERR(("submit_size = 0x%.8x\n", submit_size));
		return NULL;
	}
out:
	return dcontext->ringbuf_wtptr;
}

static void __ble_release_ringbuf_space(struct ble_context *dcontext,
					u32 submit_size)
{
	u32 ringbuf_wrptr = 0;

	dcontext->ringbuf_size_left -= submit_size;
	dcontext->ringbuf_wtptr += submit_size;

	ringbuf_wrptr = ((int)dcontext->ringbuf_wtptr -
			 dcontext->ringbuf.virtual) / 4;

	/* add wmb to make sure all the commands have already write
	 * in the ringbuffer before ble run */
	wmb();
	if (ringbuf_wrptr < dcontext->ringbuf.size)
		ble_write_reg(dcontext, RB_WR_PTR, ringbuf_wrptr);
	else
		BLE_ERR(("ringbuf_wrptr Out Range\n"));
}

/* Add Timeout for the wait and check the engine status?? */
static int __ble_wait_complete(struct ble_context *dcontext,
				   struct ble_meminfo *meminfo,
				   bool wait_complete)
{
	struct sync_object *sync_object;
	u32 cur_syncid;
	u32 count = 0;
	bool print = true;
	u32 dsyncid;

	if (!meminfo)
		return BLE_ERR_MEMORY_UNAVAILABLE;

	sync_object = meminfo->sync_object;
	dsyncid = meminfo->desired_syncid;

	cur_syncid = *(u32 *) (sync_object->viraddr);

	if (cur_syncid >= dsyncid || ((int) (dsyncid - cur_syncid) >
				      SYNCOBJECTGAP)) {
		return BLE_OK;
	}
	if (!wait_complete)
		return BLE_ERR_BLT_NOTCOMPLETE;

	while (true) {
		cur_syncid = *(u32 *) (sync_object->viraddr);
		count++;
		if (cur_syncid >= dsyncid || ((int) (dsyncid -
						     cur_syncid) >
					      SYNCOBJECTGAP)) {
			break;
		}

		if (count > 1000 && print) {
			BLE_ERR(("Wait FenceBack Timeout"));
			BLE_ERR(("DesiredSyncID =0x%.8x\n", dsyncid));
			BLE_ERR(("ReadID = 0x%.8x\n", cur_syncid));
			print = false;
		}
		usleep_range(1500, 2000);
	}
	return BLE_OK;
}

static u32 calc_intersection(struct ble_rect *rcldst, struct ble_rect *rclclip,
			     u32 num_clip_rects)
{
	u32 intersection = 0;
	struct ble_rect *rclout;
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

static u32 __ble_pat_surfctl(struct ble_context *dcontext,
			     struct ble_bltinfo *bltinfo,
			     struct ble_meminfo **meminfo)
{
	u32 i = 0;
	u32 cur_syncid = 0;
	bool found = false;
	u32 dsyncid = 0;
	u32 max_pat = MAX_PATTERN_BUF_RESERVED;
	struct ble_meminfo *patinfo;
	/* Get Pattern Surface allocation */
	for (i = dcontext->cur_patbuf; i < max_pat; i++) {
		if (dcontext->patsurf[i]->sync_object == NULL) {
			*meminfo = dcontext->patsurf[i];
			dcontext->cur_patbuf = i + 1;
			found = true;
			break;
		} else {
			/* read the sync id, and compare it with the
			 * sync object in the meminfo */
			patinfo = dcontext->patsurf[i];
			cur_syncid = *(u32 *) patinfo->sync_object->viraddr;
			dsyncid = patinfo->desired_syncid;

			if (cur_syncid >= dsyncid
			    || (dsyncid - cur_syncid) > SYNCOBJECTGAP) {
				*meminfo = dcontext->patsurf[i];
				dcontext->cur_patbuf = i + 1;
				found = true;
				break;
			}
		}
	}
	if (found)
		goto out;
	/* continue to find from 0, found = false now */
	for (i = 0; i < dcontext->cur_patbuf; i++) {
		if (dcontext->patsurf[i]->sync_object == NULL) {
			*meminfo = dcontext->patsurf[i];
			dcontext->cur_patbuf = i + 1;
			found = true;
			break;
		} else {
			/* read the sync id, and compare it
			 *  with the sync object in the meminfo*/
			patinfo = dcontext->patsurf[i];
			cur_syncid = *(u32 *) patinfo->sync_object->viraddr;
			dsyncid = dcontext->patsurf[i]->desired_syncid;

			if (cur_syncid >= dsyncid
			    || (dsyncid - cur_syncid) > SYNCOBJECTGAP) {
				*meminfo = dcontext->patsurf[i];
				dcontext->cur_patbuf = i + 1;
				found = true;
				break;
			}
		}
	}

	if (!found)
		return BLE_ERR_GENERIC;
out:
	return BLE_OK;
}

static u32 __ble_clip_check(struct ble_registers *ble_regs,
			    struct ble_rect *rclclip, u32 *cmd)
{
	unsigned long drawctrl = ble_regs->reg_draw_ctrl;
	u32 ret = 0;

	drawctrl |= (1 << BLE_DRAWCTRL_CLIP_SHIFT);

	ble_regs->reg_clip_lt = 0;
	ble_regs->reg_clip_rb = 0;

	ble_regs->reg_clip_lt = rclclip->left & RECT_LEFT_MASK;
	ble_regs->reg_clip_lt |= RECT_TOP(rclclip->top);
	ble_regs->reg_clip_rb = rclclip->right & RECT_RIGHT_MASK;
	ble_regs->reg_clip_rb |= RECT_BOTTOM(rclclip->bottom);

	ble_regs->reg_draw_ctrl = drawctrl;
	if (cmd) {
		*cmd++ = set_ble_register(CLIP_LT, 2);
		*cmd++ = ble_regs->reg_clip_lt;
		*cmd++ = ble_regs->reg_clip_rb;

		ret += 3;
	}

	return ret;
}

static u32 __ble_surface_rect_check(struct ble_bltinfo *info,
				    struct ble_registers *ble_regs, u32 *cmd)
{
	bool src_exist = info->src_exist;
	bool pat_exist = info->pat_exist;
	u32 ret = 0;

	if (src_exist) {
		ble_regs->reg_src_offset = info->smeminfo->offset;
		ble_regs->reg_src_format =
		    SRC_FORMAT_NONPREMUL(info->blendfunc) |
		    SRC_FORMAT_FORMAT(info->src_format) |
		    (info->src_stride & SRC_FORMAT_STRIDE_MASK);

		ble_regs->reg_src_lt = 0;
		ble_regs->reg_src_rb = 0;

		ble_regs->reg_src_lt = info->srcx & RECT_LEFT_MASK;
		ble_regs->reg_src_lt |= RECT_TOP(info->srcy);
		ble_regs->reg_src_rb =
		    (info->srcx + info->src_sizex) & RECT_RIGHT_MASK;
		ble_regs->reg_src_rb |=
		    RECT_BOTTOM(info->srcy + info->src_sizey);

		if (cmd) {
			*cmd++ = set_ble_register(SRC_OFFSET, 4);
			*cmd++ = ble_regs->reg_src_offset;
			*cmd++ = ble_regs->reg_src_format;
			*cmd++ = ble_regs->reg_src_lt;
			*cmd++ = ble_regs->reg_src_rb;
			ret += 5;
		}
	}

	if (pat_exist) {
		ble_regs->reg_pat_offset = info->pat_meminfo->offset;

		if (cmd) {
			*cmd++ = set_ble_register(PAT_OFFSET, 1);
			*cmd++ = ble_regs->reg_pat_offset;
			ret += 2;
		}
	}

	ble_regs->reg_dst_offset = (unsigned long)info->dmeminfo->offset;
	ble_regs->reg_dst_format = DST_FORMAT_FORMAT(info->dst_format) |
	    (info->dst_stride & DST_FORMAT_STRIDE_MASK);

	ble_regs->reg_dst_lt = 0;
	ble_regs->reg_dst_rb = 0;

	ble_regs->reg_dst_lt = info->dstx & RECT_LEFT_MASK;
	ble_regs->reg_dst_lt |= RECT_TOP(info->dsty);
	ble_regs->reg_dst_rb = (info->dstx + info->dst_sizex) & RECT_RIGHT_MASK;
	ble_regs->reg_dst_rb |= RECT_BOTTOM(info->dsty + info->dst_sizey);

	if (cmd) {
		*cmd++ = set_ble_register(DST_OFFSET, 4);
		*cmd++ = ble_regs->reg_dst_offset;
		*cmd++ = ble_regs->reg_dst_format;

		*cmd++ = ble_regs->reg_dst_lt;
		*cmd++ = ble_regs->reg_dst_rb;

		ret += 5;

	}

	return ret;
}

static u32 __ble_alphablend_check(struct ble_bltinfo *bltinfo,
				  struct ble_registers *ble_regs, u32 *cmd)
{
	unsigned long drawctrl = ble_regs->reg_draw_ctrl;
	u32 ret = 0;
	u32 tmp = 0;
	drawctrl &= ~(3 << BLE_DRAWCTRL_ALPHA_SHIFT);

	/* Both constant and perpixel alpha */
	tmp = BLE_BLIT_ALPHA_MASK;
	if ((bltinfo->blt_flags & tmp) == tmp) {
		drawctrl |= (3 << BLE_DRAWCTRL_ALPHA_SHIFT);
		ble_regs->reg_gbl_alpha = bltinfo->global_alpha;
		if (cmd) {
			*cmd++ = set_ble_register(GBL_ALPHA, 1);
			*cmd++ = ble_regs->reg_gbl_alpha;
			ret += 2;
		}
	} else if (bltinfo->blt_flags & BLE_BLIT_PERPIXEL_ALPHA) {
		drawctrl |= (2 << BLE_DRAWCTRL_ALPHA_SHIFT);
	} else if (bltinfo->blt_flags & BLE_BLIT_GLOBAL_ALPHA) {
		drawctrl |= (1 << BLE_DRAWCTRL_ALPHA_SHIFT);
		ble_regs->reg_gbl_alpha = bltinfo->global_alpha;

		if (cmd) {
			*cmd++ = set_ble_register(GBL_ALPHA, 1);
			*cmd++ = ble_regs->reg_gbl_alpha;
			ret += 2;
		}
	}

	ble_regs->reg_draw_ctrl = drawctrl;

	return ret;
}

static u32 __ble_transparent_check(struct ble_bltinfo *bltinfo,
				   struct ble_registers *ble_regs, u32 *cmd)
{
	unsigned long drawctrl = ble_regs->reg_draw_ctrl;
	u32 ret = 0;

	drawctrl &= ~(1 << BLE_DRAWCTRL_COLORKEY_MODE_SHIFT);

	if (bltinfo->blt_flags & BLE_BLIT_SRC_COLORKEY)
		drawctrl |= (0 << BLE_DRAWCTRL_COLORKEY_MODE_SHIFT);
	else
		drawctrl |= (1 << BLE_DRAWCTRL_COLORKEY_MODE_SHIFT);

	drawctrl |= (1 << BLE_DRAWCTRL_TRANSPARENT_SHIFT);

	ble_regs->reg_color_key = bltinfo->colorkey;

	if (cmd) {
		*cmd++ = set_ble_register(COLOR_KEY, 1);
		*cmd++ = ble_regs->reg_color_key;
		ret += 2;
	}

	ble_regs->reg_draw_ctrl = drawctrl;
	return ret;
}

static u32 __ble_colorfill_check(struct ble_bltinfo *bltinfo,
				 struct ble_registers *ble_regs, u32 *cmd)
{
	unsigned long drawctrl = ble_regs->reg_draw_ctrl;
	u32 ret = 0;

	drawctrl |= (1 << BLE_DRAWCTRL_COLORFILL_SHIFT);

	ble_regs->reg_fill_color = bltinfo->fill_color;

	ble_regs->reg_draw_ctrl = drawctrl;
	if (cmd) {
		*cmd++ = set_ble_register(FILL_COLOR, 1);
		*cmd++ = ble_regs->reg_fill_color;
		ret += 2;
	}

	return ret;
}

static void __ble_swizzle_check(struct ble_bltinfo *bltinfo,
				struct ble_registers *ble_regs)
{
	unsigned long drawctrl = ble_regs->reg_draw_ctrl;
	drawctrl &=
	    ~(3 << BLE_DRAWCTRL_ROTATION_SHIFT |
	      1 << BLE_DRAWCTRL_FLIP_H_SHIFT |
	      1 << BLE_DRAWCTRL_FLIP_V_SHIFT);

	switch (bltinfo->blt_flags & BLE_BLIT_ROT_MASK) {
	case BLE_BLIT_ROT_90:
		drawctrl |= (1 << BLE_DRAWCTRL_ROTATION_SHIFT);
		break;
	case BLE_BLIT_ROT_180:
		drawctrl |= (2 << BLE_DRAWCTRL_ROTATION_SHIFT);
		break;
	case BLE_BLIT_ROT_270:
		drawctrl |= (3 << BLE_DRAWCTRL_ROTATION_SHIFT);
		break;
	default:
		break;
	}
	/* flip and rotate can work together, so it can't check in one switch */
	if (bltinfo->blt_flags & BLE_BLIT_FLIP_H)
		drawctrl |= (1 << BLE_DRAWCTRL_FLIP_H_SHIFT);

	if (bltinfo->blt_flags & BLE_BLIT_FLIP_V)
		drawctrl |= (1 << BLE_DRAWCTRL_FLIP_V_SHIFT);

	ble_regs->reg_draw_ctrl = drawctrl;

}

static u32 __ble2d_bitblt(struct ble_context *dcontext,
			  struct ble_bltinfo *bltinfo, struct ble_rect *rclclip)
{
	struct ble_registers ble2dreg;
	struct ble_registers *ble_regs = &ble2dreg;
	u32 submit_size = 0;
	u32 bltcmd[BLE_MAX_BLIT_CMD_SIZE] = { 0, };
	u32 cur_cmdindex = 0;
	void *buf = NULL;
	bool print = false;
	/* every blt command need wait before last command back, for debug */
	bool wait_complete = false;
	memset(ble_regs, 0xFF, sizeof(*ble_regs));

	ble_regs->reg_draw_ctrl = 0;
	ble_regs->reg_draw_ctrl |= (bltinfo->rop3 & DRAW_CTL_ROP3_MASK);

	/* Check rotation & mirror */
	if (bltinfo->blt_flags & (BLE_BLIT_ROT_MASK | BLE_BLIT_FLIP_MASK))
		__ble_swizzle_check(bltinfo, ble_regs);

	if (bltinfo->need_synclast && !first_cmd) {
		bltcmd[cur_cmdindex++] = FENCE_HEAD(OP_FENCE_WAIT) |
		    FENCE_ADDR(dcontext->sync_object.phyaddr);
		bltcmd[cur_cmdindex++] = dcontext->sync_object.cur_syncid - 1;
	} else if (first_cmd) {
		first_cmd = false;
	}
	/* Check Clip */
	if (bltinfo->blt_flags & BLE_BLIT_CLIP_ENABLE) {
		cur_cmdindex += __ble_clip_check(ble_regs, rclclip,
						 &bltcmd[cur_cmdindex]);
	}
	/* Check Surface */
	cur_cmdindex += __ble_surface_rect_check(bltinfo, ble_regs,
						 &bltcmd[cur_cmdindex]);

	/* Check Alpha Blend */
	if (bltinfo->blt_flags & BLE_BLIT_ALPHA_MASK) {
		cur_cmdindex += __ble_alphablend_check(bltinfo, ble_regs,
						       &bltcmd[cur_cmdindex]);
	}
	/* Check transparent and color key */
	if (bltinfo->blt_flags & BLE_BLIT_TRANSPARENT_ENABLE) {
		cur_cmdindex += __ble_transparent_check(bltinfo, ble_regs,
							&bltcmd[cur_cmdindex]);
	}
	/* Check color fill */
	if (bltinfo->blt_flags & BLE_BLIT_COLOR_FILL) {
		cur_cmdindex += __ble_colorfill_check(bltinfo, ble_regs,
						      &bltcmd[cur_cmdindex]);
	}

	bltcmd[cur_cmdindex++] = set_ble_register(DRAW_CTL, 1);
	bltcmd[cur_cmdindex++] = ble_regs->reg_draw_ctrl;

	bltcmd[cur_cmdindex++] = FENCE_HEAD(OP_FENCE_WRITE_INTERRUPT) |
	    FENCE_ADDR(dcontext->sync_object.phyaddr);
	bltcmd[cur_cmdindex++] = dcontext->sync_object.cur_syncid++;

	submit_size = cur_cmdindex;
	submit_size = ((submit_size + 3) & ~3);

	/* Submit Command to ringbuf */
	buf = (u8 *) __ble_get_ringbuf_space(dcontext, submit_size);

	if (print)
		print_cmd(bltcmd, submit_size);

	memcpy(buf, bltcmd, submit_size * 4);

	__ble_release_ringbuf_space(dcontext, submit_size);

	if (wait_complete) {
		u32 counter = 0;
		while (true) {
			uint fenceid = dcontext->sync_object.cur_syncid - 1;
			if (*(uint *) dcontext->sync_object.viraddr >= fenceid)
				break;
			counter++;

			if (counter == 0xFFFFFF) {
				counter = 0;
				BLE_ERR(("Engine hang\n"));
				break;
			}
		}
	}

	return BLE_OK;
}

static u32 __ble_bitblt(void *hcontext, struct ble_bltinfo *info)
{
	struct ble_context *dcontext = (struct ble_context *)hcontext;
	struct ble_rect rcldst, rclsrc;
	unsigned long num_cliprects = info->num_cliprect;
	struct ble_rect *pc_rect = info->ble_cliprect;
	unsigned long cliprects;
	struct ble_rect *rclclip = NULL;
	int i;

	/* Dest rect */
	rcldst.left = info->dstx;
	rcldst.right = rcldst.left + info->dst_sizex;
	rcldst.top = info->dsty;
	rcldst.bottom = rcldst.top + info->dst_sizey;

	/* Check Pattern */
	if (info->pat_exist) {
		struct ble_meminfo *patsurf = NULL;

		if (__ble_pat_surfctl(dcontext, info, &patsurf) != BLE_OK) {
			BLE_ERR(("Can't get available Pattern Surface\n"));
			return BLE_ERR_GENERIC;
		}

		info->pat_meminfo = patsurf;
		info->pat_meminfo->sync_object = &dcontext->sync_object;
		info->pat_meminfo->desired_syncid =
		    dcontext->sync_object.cur_syncid;
	}
	info->dmeminfo->sync_object = &dcontext->sync_object;
	info->dmeminfo->desired_syncid = dcontext->sync_object.cur_syncid;
	/* Check Src */
	if (info->src_exist) {
		/* Src rect */
		struct ble_meminfo *src_meminfo = info->smeminfo;
		rclsrc.left = info->srcx;
		rclsrc.right = rclsrc.left + info->src_sizex;
		rclsrc.top = info->srcy;
		rclsrc.bottom = rclsrc.top + info->src_sizey;
		src_meminfo->sync_object = &dcontext->sync_object;
		src_meminfo->desired_syncid = dcontext->sync_object.cur_syncid;
	}

	if (num_cliprects) {
		cliprects = calc_intersection(&rcldst, pc_rect, num_cliprects);

		if (cliprects > 0)
			info->blt_flags |= BLE_BLIT_CLIP_ENABLE;
		rclclip = pc_rect;
	} else {
		cliprects = 1;
		rclclip = &rcldst;
	}

	for (i = 0; i < cliprects; i++) {
		__ble2d_bitblt(dcontext, info, rclclip);

		rclclip++;
	}

	/* Everything is clipped */
	if (cliprects == 0)
		return BLE_OK;
#ifdef SYNC_BLT
	__ble_wait_complete(dcontext, info->dmeminfo, 1);
#endif

	return BLE_OK;
}

static void __ble_clear_interrupt(struct ble_context *dcontext,
				  u32 interrupt_index)
{
	u32 reg_int_clear;

	reg_int_clear = (int)(1 << interrupt_index);

	ble_write_reg(dcontext, INTERRUPT_CLEAR, reg_int_clear);
}

static void __ble_enable_interrupt(struct ble_context *dcontext,
				   u32 interrupt_index)
{
	u32 reg_int_enable;

	reg_int_enable = ble_read_reg(dcontext, INTERRUPT_ENABLE);

	reg_int_enable |= (1 << interrupt_index);

	ble_write_reg(dcontext, INTERRUPT_ENABLE, reg_int_enable);
}

static void __ble_enable_clock(void)
{
}

static void __ble_disable_clock(void)
{
}

static void __ble_reset(void)
{
}

static void __ble_setup(void *data)
{
	struct ble_context *context = (struct ble_context *)data;

	__ble_enable_clock();
	__ble_reset();

	if (!context)
		return;
	/* Config FB base register */
	ble_write_reg(context, FB_BASE, 0);

	context->ringbuf_wtptr = (u32 *) context->ringbuf.virtual;
	context->ringbuf_size_left = context->ringbuf.size;

	ble_write_reg(context, ENG_CTRL, 0x1);
	ble_write_reg(context, RB_OFFSET, context->ringbuf.offset);
	ble_write_reg(context, RB_LENGTH, context->ringbuf.size);
	ble_write_reg(context, RB_RD_PTR, 0);
	ble_write_reg(context, RB_WR_PTR, 0);

	__ble_enable_interrupt(context, FENCE_INTERRUPT);

}

/*****************************************************************************/
/*                      Function Table  Area                                 */
/*****************************************************************************/

static bool ble_isbusy(void)
{
	u32 reg_eng_status;

	reg_eng_status = ble_read_reg(ble_context, ENG_STATUS);

	return ((reg_eng_status & ENG_STATUS_IDLE_MASK) == 0);
}

static void ble_wakeup(void)
{
	first_cmd = true;
	__ble_setup(ble_context);

	return;
}

static void ble_sleep(void)
{
	u32 times = 0;

	while (ble_isbusy()) {
		mdelay(1);
		times++;
		if (times > 20) {
			BLE_ERR(("Error: Blit engine is busy\n"));
			break;
		}
	}

	return;
}

static bool ble_initialize(void **dcontext, void *initdata)
{
	struct ble_init_meminfo *meminfo = (struct ble_init_meminfo *)initdata;
	struct ble_context *context;
	int i;

	context = kzalloc(sizeof(*context), GFP_KERNEL);

	if (!context)
		return false;

	ble_context = context;

	for (i = 0; i < MAX_PATTERN_BUF_RESERVED; i++)
		context->patsurf[i] = NULL;

	context->cur_patbuf = 0;

	context->ble_reg_base = meminfo->regbase;

	context->ringbuf.offset =
	    (meminfo->memoffset + RING_BUF_ALIGNMENT - 1) &
	    (~(RING_BUF_ALIGNMENT - 1));
	context->ringbuf.size = RING_BUF_SIZE / 4;
	context->ringbuf.virtual = meminfo->membase +
	    (context->ringbuf.offset - meminfo->memoffset);

	context->ringbuf_wtptr = (u32 *) context->ringbuf.virtual;
	context->ringbuf_size_left = context->ringbuf.size;

	context->sync_object.phyaddr =
	    (context->ringbuf.offset + RING_BUF_SIZE +
	     FENCE_BUF_ALIGNMENT - 1) & (~(FENCE_BUF_ALIGNMENT - 1));
	context->sync_object.viraddr = meminfo->membase +
	    (context->sync_object.phyaddr - meminfo->memoffset);
	/* we only care BLECOMMANDMODE in linux, there is MMIOMODE in WINCE */
	context->ble_op_mode = BLECOMMANDMODE;
	context->sync_object.cur_syncid = 1;
	/* CspRegMap(false); */

	__ble_setup(context);

	*dcontext = (void *)context;
	return true;
}

static bool ble_terminate(void *hcontext)
{
	/* CspRegUnMap(); */

	return true;
}

static bool ble_check_params(void *bltparams)
{
	/* To Do, add Linux support */
	return true;
}

static int ble_query_blt_status(void *dcontext, void *meminfo, bool wait)
{
	return __ble_wait_complete((struct ble_context *)dcontext,
				       (struct ble_meminfo *)meminfo, wait);
}

static u32 ble_bitblt(void *hcontext, void *bltinfo)
{
	return __ble_bitblt(hcontext, (struct ble_bltinfo *)bltinfo);
}

static void ble_interrupt_routine(void *hcontext)
{
	u32 intr_status = 0;
	u32 intr_enabled = 0;
	u32 temp = 0;
	struct ble_context *context = (struct ble_context *)hcontext;
	static u32 fenceid;

	intr_status = ble_read_reg(context, INTERRUPT_STATUS);
	intr_status &= VALID_INTERRUPT_MASK;

	intr_enabled = ble_read_reg(context, INTERRUPT_ENABLE);
	intr_enabled &= VALID_INTERRUPT_MASK;

	intr_status &= intr_enabled;

	/* Disalbe All Interrupt */
	ble_write_reg(context, INTERRUPT_ENABLE, 0x0);

	if (intr_status & (1 << CMD_BUF_EMPTY_INTERRUPT))
		__ble_clear_interrupt(context, CMD_BUF_EMPTY_INTERRUPT);

	if (intr_status & (1 << FENCE_INTERRUPT)) {
		temp = fenceid;

		__ble_clear_interrupt(context, FENCE_INTERRUPT);
		fenceid = *(u32 *) (context->sync_object.viraddr);

		if (fenceid != (temp + 1)) {
			;
			/* RETAILMSG(1,(TEXT("Fence Lost, Last FenceID = 0x%.8x,
			 * New Fence ID = 0x%.8x\n"),temp,fenceid)); */
		}

		if (temp > fenceid) {
			BLE_ERR(("Last Fence ID = 0x%.8x\n", temp));
			BLE_ERR(("New FenceID = 0x%.8x\n", fenceid));
			print_ringbuf_info(context, 0);
		}

		if (fenceid % 1000 == 0)
			BLE_MSG(("fenceid = 0x%.8x is back\n", fenceid));
	}

	if (intr_status & (1 << BLT_TIMEOUT_INTERRUPT))
		__ble_clear_interrupt(context, BLT_TIMEOUT_INTERRUPT);

	if (intr_status & (1 << BLT_COMPLETE_INTERRUPT))
		__ble_clear_interrupt(context, BLT_COMPLETE_INTERRUPT);

	/* Enable All Interrupt */
	ble_write_reg(context, INTERRUPT_ENABLE, intr_enabled);
}

static void ble_print_registers(void)
{
	u32 i = 0;

	for (i = 0; i <= DRAW_CTL; i += 4) {
		BLE_MSG(("Register offset %.8x = %.8x\r\n", i,
			 ble_read_reg(NULL, i)));
	}
}

static void ble_enable_clock(void)
{
	__ble_enable_clock();
}

static void ble_disable_clock(void)
{
	__ble_disable_clock();
}

static void ble_reset(void)
{
	__ble_reset();
}

void vdss_ble_install_ops(struct vdss_ble_ops *ble_ops)
{
	memset(ble_ops, 0, sizeof(*ble_ops));

	ble_ops->initialize = ble_initialize;
	ble_ops->terminate = ble_terminate;
	ble_ops->check_params = ble_check_params;
	ble_ops->bitblt = ble_bitblt;
	ble_ops->query_status = ble_query_blt_status;
	ble_ops->print_registers = ble_print_registers;
	ble_ops->wakeup = ble_wakeup;
	ble_ops->sleep = ble_sleep;
	ble_ops->interrupt_routine = ble_interrupt_routine;

	ble_ops->enable_clock = ble_enable_clock;
	ble_ops->disable_clock = ble_disable_clock;
	ble_ops->reset = ble_reset;
}
