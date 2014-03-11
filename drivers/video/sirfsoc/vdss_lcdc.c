/*
 * CSR sirfsoc LCD library
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#include "lcdc_defs.h"


static struct vdss_vpp_ops vpp_ops;
static struct lcdc_config lcdc_config;
static struct lcdc_panel_info panel_info;

static unsigned int hwfmt_to_bpp[] = {
	4,	/* LO_CTRL_BPP_RGB666 */
	2,	/* LO_CTRL_BPP_RGB565 */
	2,	/* LO_CTRL_BPP_RGB556 */
	2,	/* LO_CTRL_BPP_RGB655 */
	4,	/* LO_CTRL_BPP_RGB888 */
	4,	/* LO_CTRL_BPP_TRGB888 */
	4,	/* LO_CTRL_BPP_ARGB8888 */
	2,	/* LO_CTRL_BPP_UNKNOWN */
};


static void __lcdc_wait_idle(int layer, bool with_vpp)
{
	int timeout;

	if (with_vpp) {
		timeout = 0;
		while (vpp_ops.is_busy()) {
			msleep(20);
			timeout++;
			if (timeout > 1000)
				LCDC_DEBUG("wait vpp idle timeout\n");
		}
	}

	timeout = 0;
	while (lcdc_read_reg(DMA_STATUS) & (1 << layer)) {
		msleep(20);
		timeout++;
		if (timeout > 1000)
			LCDC_DEBUG("wait DMA_STATUS timeout\n");
	}

	timeout = 0;
	while (lcdc_read_reg(S0_LAYER_STATUS) & (1 << layer)) {
		msleep(20);
		timeout++;
		if (timeout > 1000)
			LCDC_DEBUG("wait S0_LAYER_STATUS timeout\n");
	}
}

static void __lcdc_disable_layer(int layer, bool wait)
{
	u32 s0_layer_sel;
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[layer]);

	s0_layer_sel = lcdc_read_reg(S0_LAYER_SEL);
	if (s0_layer_sel & S0_LS_LAYER_SEL(1 << layer)) {
		s0_layer_sel &= ~S0_LS_LAYER_SEL(1 << layer);
		lcdc_write_reg(S0_LAYER_SEL, s0_layer_sel);

		if (layer_state->need_vpp) {
			u32 lx_dma_ctrl =
				lcdc_read_reg(reg_offset(layer, L0_DMA_CTRL));
			lx_dma_ctrl &= ~LX_VPP_PASS_MODE;
			lcdc_write_reg(reg_offset(layer, L0_DMA_CTRL),
					lx_dma_ctrl);

			__lcdc_confirm_layer_setting(layer);
		}

		if (wait)
			__lcdc_wait_idle(layer, layer_state->need_vpp);
	}
}

static void __lcdc_enable_layer(int layer)
{
	u32 s0_layer_sel;
	u32 lx_dma_ctrl;
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[layer]);

	s0_layer_sel = lcdc_read_reg(S0_LAYER_SEL);
	if (!(s0_layer_sel & S0_LS_LAYER_SEL(1 << layer))) {
		__lcdc_wait_idle(layer, layer_state->need_vpp);
		__lcdc_reset_layer_fifo(layer);

		lx_dma_ctrl = lcdc_read_reg(reg_offset(layer, L0_DMA_CTRL));
		if (layer_state->need_vpp)
			lx_dma_ctrl |= LX_VPP_PASS_MODE;
		else
			lx_dma_ctrl &= ~LX_VPP_PASS_MODE;
		lcdc_write_reg(reg_offset(layer, L0_DMA_CTRL), lx_dma_ctrl);
		__lcdc_confirm_layer_setting(layer);

		s0_layer_sel |= S0_LS_LAYER_SEL(1 << layer);
		lcdc_write_reg(S0_LAYER_SEL, s0_layer_sel);
	}
}

static void __lcdc_set_dma(int layer, struct vdss_rect *src_rect)
{
	u32 lx_dma_ctrl = 0x0;
	u32 lx_fifo_chk = 0x0;
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[layer]);
	unsigned int bpp = hwfmt_to_bpp[__lcdc_fmt_to_hwfmt(layer_state->fmt)];

	/*Set DMA register configuration*/
	unsigned int width = src_rect->right - src_rect->left;
	unsigned int height = src_rect->bottom - src_rect->top;

	bool tv_mode = __lcdc_is_tvmode(&panel_info);
	unsigned int dma_unit = __lcdc_dma_unit(tv_mode);
	unsigned int offset = (src_rect->top * layer_state->surf_width +
				src_rect->left) * bpp;
	unsigned int xsize = (((offset & 7) + width * bpp  + dma_unit - 1) /
				dma_unit) - 1;

	unsigned int ysize, skip;

	if (tv_mode) {
		ysize = height / 2 - 1;
		skip = (layer_state->surf_width * bpp) * 2 - (xsize * dma_unit);
	} else {
		ysize = height - 1;	/* in line units */
		skip = (layer_state->surf_width * bpp) - (xsize * dma_unit);
	}

	/* Set overlay surface addr */
	lcdc_write_reg(reg_offset(layer, L0_BASE0), layer_state->base + offset);
	if (tv_mode)
		lcdc_write_reg(reg_offset(layer, L0_BASE1),
				layer_state->base + offset +
				layer_state->surf_width * bpp);

	lcdc_write_reg(reg_offset(layer, L0_XSIZE), xsize);
	lcdc_write_reg(reg_offset(layer, L0_YSIZE), ysize);
	lcdc_write_reg(reg_offset(layer, L0_SKIP),  skip);

	lx_fifo_chk = LX_LO_CHK(0xF0) | LX_MI_CHK(0x80) | LX_REQ_SEL;
	lcdc_write_reg(reg_offset(layer, L0_FIFO_CHK), lx_fifo_chk);

	lx_dma_ctrl = lcdc_read_reg(reg_offset(layer, L0_DMA_CTRL));
	lx_dma_ctrl &= ~LX_SUPPRESS_QW_NUM_MASK;
	lx_dma_ctrl &= ~LX_DMA_UNIT_MASK;
	lx_dma_ctrl |= LX_SUPPRESS_QW_NUM(((xsize + 1) * dma_unit -
				width * bpp - (offset & 7)) >> 3);
	lx_dma_ctrl |= LX_DMA_UNIT((dma_unit >> 3) - 1);
	lx_dma_ctrl |= LX_DMA_MODE;
	if (tv_mode)
		lx_dma_ctrl |= LX_DMA_CHAIN_MODE;
	lcdc_write_reg(reg_offset(layer, L0_DMA_CTRL), lx_dma_ctrl);
}

static u32 __lcdc_ckey_val(enum vdss_pixelformat fmt,
			bool duplicate, u32 value)
{
	if (fmt == VDSS_PIXELFORMAT_BGRX_8880 ||
		fmt == VDSS_PIXELFORMAT_8888) {
		return value;
	} else if (fmt == VDSS_PIXELFORMAT_565) {
		u32 ckval;

		ckval = LX_CKEY_R(((value >> 11) & 0x1F) << 3) |
			LX_CKEY_G(((value >> 5) & 0x3F) << 2) |
			LX_CKEY_B((value & 0x1F) << 3);

		if (duplicate) {
			ckval |= ((ckval & LX_CKEY_R_MASK) >> 5) |
				((ckval & LX_CKEY_G_MASK) >> 6) |
				((ckval & LX_CKEY_B_MASK) >> 5);
		}
		return ckval;
	} else if (fmt >= VDSS_PIXELFORMAT_UYVY) {
		return value;
	} else {
		LCDC_ERR("%s(%d): unknown format 0x%x\n",
			__func__, __LINE__, fmt);
		return 0;
	}
}

static void __lcdc_cal_size(struct vdss_rect *src_orig_rect,
			struct vdss_rect *dst_orig_rect,
			bool need_vpp,
			struct vdss_rect *src_rect,
			struct vdss_rect *dst_rect)
{
	int scr_width;
	int scr_height;

	int src_orig_width, src_orig_height;
	int dst_orig_width, dst_orig_height;

	__lcdc_get_screen_size((u32 *)(&scr_width),
				(u32 *)(&scr_height),
				&panel_info);

	src_orig_width = src_orig_rect->right - src_orig_rect->left;
	dst_orig_width = dst_orig_rect->right - dst_orig_rect->left;
	src_orig_height = src_orig_rect->bottom - src_orig_rect->top;
	dst_orig_height = dst_orig_rect->bottom - dst_orig_rect->top;

	if (dst_orig_rect->left < 0) {
		dst_rect->left = 0;
		src_rect->left = src_orig_rect->left +
				(src_orig_width / dst_orig_width *
				 (-dst_orig_rect->left));
	} else if (dst_orig_rect->left > (scr_width - 1)) {
		dst_rect->left = scr_width - 1;
		dst_rect->right = scr_width;

		src_rect->left = src_orig_rect->left;
		src_rect->right = src_orig_rect->left + 1;
	} else {
		dst_rect->left = dst_orig_rect->left;
		src_rect->left = src_orig_rect->left;
	}

	if (dst_orig_rect->right < 0) {
		dst_rect->left = -1;
		dst_rect->right = 0;
		src_rect->left = src_orig_rect->right - 1;
		src_rect->right = src_orig_rect->right;
	} else if (dst_orig_rect->right > scr_width) {
		dst_rect->right = scr_width;
		src_rect->right = src_orig_rect->right -
				(src_orig_width / dst_orig_width *
				 (dst_orig_rect->right - scr_width));
	} else {
		dst_rect->right = dst_orig_rect->right;
		src_rect->right = src_orig_rect->right;
	}


	if (dst_orig_rect->top < 0) {
		dst_rect->top = 0;
		src_rect->top = src_orig_rect->top +
				(src_orig_height / dst_orig_height *
				 (-dst_orig_rect->top));
	} else if (dst_orig_rect->top > (scr_height - 1)) {
		dst_rect->top = scr_height - 1;
		dst_rect->bottom = scr_height;

		src_rect->top = src_orig_rect->top;
		src_rect->bottom = src_orig_rect->top + 1;
	} else {
		dst_rect->top = dst_orig_rect->top;
		src_rect->top = src_orig_rect->top;
	}

	if (dst_orig_rect->bottom < 0) {
		dst_rect->top = -1;
		dst_rect->bottom = 0;
		src_rect->top = src_orig_rect->bottom - 1;
		src_rect->bottom = src_orig_rect->bottom;
	} else if (dst_orig_rect->bottom > scr_height) {
		dst_rect->bottom = scr_height;
		src_rect->bottom = src_orig_rect->bottom -
				(src_orig_height / dst_orig_height *
				 (dst_orig_rect->bottom - scr_height));
	} else {
		dst_rect->bottom = dst_orig_rect->bottom;
		src_rect->bottom = src_orig_rect->bottom;
	}

	/* workaround LCD don't support 1 line display */
	if ((dst_rect->bottom - dst_rect->top) == 1) {
		dst_rect->top = scr_height;
		dst_rect->bottom = scr_height + 2;

		src_rect->top = 0;
		src_rect->bottom = 2;
	}

	/* workaround RGB overlay stretch, we don't support it, so
	 * show it as the smaller rect.
	 */
	if (!need_vpp) {
		int src_width, src_height;
		int dst_width, dst_height;

		src_width = src_rect->right - src_rect->left;
		src_height = src_rect->bottom - src_rect->top;
		dst_width = dst_rect->right - dst_rect->left;
		dst_height = dst_rect->bottom - dst_rect->top;

		if (src_width > dst_width)
			src_rect->right = src_rect->left + dst_width;
		else if (src_width < dst_width)
			dst_rect->right = dst_rect->left + src_width;

		if (src_height > dst_height)
			src_rect->bottom = src_rect->top + dst_height;
		else if (src_height < dst_height)
			dst_rect->bottom = dst_rect->top + src_height;
	}
}


static void __lcdc_set_dst_rect(int layer, struct vdss_rect *dst_rect_on)
{
	switch (panel_info.out_fmt) {
	case LCDC_OUT_8_BIT_RBGRBG:
		lcdc_write_reg(reg_offset(layer, L0_HSTART),
			(dst_rect_on->left * 3) + panel_info.hstart);
		lcdc_write_reg(reg_offset(layer, L0_HEND),
			(dst_rect_on->right - 1) * 3 + panel_info.hstart);
		break;
	case LCDC_OUT_8_BIT_YUV422:
		lcdc_write_reg(reg_offset(layer, L0_HSTART),
			(dst_rect_on->left * 2) + panel_info.hstart);
		lcdc_write_reg(reg_offset(layer, L0_HEND),
			(dst_rect_on->right - 1) * 2 + panel_info.hstart);
		break;
	default:
		lcdc_write_reg(reg_offset(layer, L0_HSTART),
			dst_rect_on->left + panel_info.hstart);
		lcdc_write_reg(reg_offset(layer, L0_HEND),
			dst_rect_on->right + panel_info.hstart - 1);
		break;
	}

	lcdc_write_reg(reg_offset(layer, L0_VSTART),
		dst_rect_on->top + panel_info.vstart);
	lcdc_write_reg(reg_offset(layer, L0_VEND),
		dst_rect_on->bottom + panel_info.vstart - 1);
}

static void __lcdc_set_size(int layer, bool force_update)
{
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[layer]);
	struct vdss_rect src_rect_on, dst_rect_on;
	int src_diff = 0, dst_diff = 0, dst_range_diff = 0;

	__lcdc_cal_size(&layer_state->src_rect, &layer_state->dst_rect,
			layer_state->need_vpp, &src_rect_on, &dst_rect_on);
	if (!force_update) {
		src_diff = memcmp(&src_rect_on, &layer_state->src_rect_on,
							sizeof(src_rect_on));
		dst_diff = memcmp(&dst_rect_on, &layer_state->dst_rect_on,
							sizeof(dst_rect_on));
		dst_range_diff =
			((dst_rect_on.right - dst_rect_on.left) !=
			(layer_state->dst_rect_on.right -
			 layer_state->dst_rect_on.left)) ||
			((dst_rect_on.bottom - dst_rect_on.top) !=
			 (layer_state->dst_rect_on.bottom -
			  layer_state->dst_rect_on.top));
	}

	if (layer_state->need_vpp) {
		if (force_update || (src_diff || dst_range_diff)) {
			unsigned int  src_skip, dst_skip;
			struct vdss_rect vpp_dst_rect;

			if ((layer_state->fmt >= VDSS_PIXELFORMAT_UYVY) &&
				(layer_state->fmt <= VDSS_PIXELFORMAT_VYUY))
				src_skip = (src_rect_on.left & 3);
			else
				src_skip = (src_rect_on.left & 15);

			if (src_skip && (src_rect_on.right - src_rect_on.left))
				dst_skip = src_skip *
					(dst_rect_on.right - dst_rect_on.left) /
					(src_rect_on.right - src_rect_on.left);
			else
				dst_skip = 0;
			src_rect_on.left -= src_skip;

			lcdc_write_reg(reg_offset(layer, L0_BASE0),
					VPP_TO_LCDC_BPP * dst_skip);
			lcdc_write_reg(reg_offset(layer, L0_BASE1),
					VPP_TO_LCDC_BPP * dst_skip);

			memcpy(&vpp_dst_rect , &dst_rect_on,
				sizeof(vpp_dst_rect));
			vpp_dst_rect.left -= dst_skip;
			vpp_ops.set_size(&src_rect_on, &vpp_dst_rect);
		}
	} else {
		if (force_update || src_diff)
			__lcdc_set_dma(layer, &src_rect_on);
	}

	if (force_update || dst_diff)
		__lcdc_set_dst_rect(layer, &dst_rect_on);

	layer_state->dst_rect_on = dst_rect_on;
	layer_state->src_rect_on = src_rect_on;
}


static void __lcdc_get_primary_size(unsigned int *primary_size)
{
	unsigned int w, h, d;
	struct lcdc_layer_state *layer_state =
		&lcdc_config.layer_state[LCDC_PRIMARY];
	d = (layer_state->fmt == VDSS_PIXELFORMAT_565) ? 16 : 32;
	__lcdc_get_screen_size(&w, &h, &panel_info);
	*primary_size = ((byte_stride(w, d) * h) + 0xfff) & 0xfffff000;
}

static void __lcdc_flip(int layer, enum lcdc_flip_mode flip_mode)
{
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[layer]);
	u32 base = layer_state->base;

	if (layer_state->need_vpp) {
		if (layer_state->flip_mode != flip_mode) {
			bool top_field;

			layer_state->flip_mode = flip_mode;

			/* disable layer is needed for programming interlace */
			if (layer_state->show)
				__lcdc_disable_layer(layer, true);

			if (flip_mode != LCDC_FLIP_FRAME) {
				top_field = (flip_mode == LCDC_FLIP_TOP_FIELD);
				vpp_ops.set_interlace(true, VPP_OUTPUT_P_SINGLE,
					top_field, top_field, VPP_DI_VMRI,
					true, 0);
			} else {
				vpp_ops.set_interlace(false,
					VPP_OUTPUT_P_SINGLE,
					true, true,
					VPP_DI_RESERVED,
					true, 0);
			}

			if (layer_state->show)
				__lcdc_enable_layer(layer);
		}
		vpp_ops.set_base(base);
	} else {
		unsigned char bpp = __lcdc_fmt_to_bpp(layer_state->fmt);
		unsigned int offset = bpp * (layer_state->src_rect.top *
			layer_state->surf_width + layer_state->src_rect.left);

		lcdc_write_reg(reg_offset(layer, L0_BASE0), base+offset);
		if (__lcdc_is_tvmode(&panel_info))
			lcdc_write_reg(reg_offset(layer, L0_BASE1),
					base + offset +
					(bpp * layer_state->surf_width));
	}
}

static inline u16 __lcdc_mask_table(u8 and, u8 xor)
{
	u16 ret = 0;
	int i;

	for (i = 0; i < 8; i++) {
		ret |= (and & 1) << (i * 2 + 1);
		ret |= (xor & 1) << (i * 2);
		and >>= 1;
		xor >>= 1;
	}
	return ret;
}

static void __lcdc_gen_cursor_fifo(u32 *color, u32 *mask, int mask_stride)
{
	struct lcdc_cursor_state *cursor_state = &(lcdc_config.cursor_state);
	int width = cursor_state->width;
	int height = cursor_state->height;
	u32 *pval = &(cursor_state->fifo[0]);
	u32 *src, *dst, val;

	u8 *and_ptr, *xor_ptr;	/* input pointer */
	u16 val_16, *pval_16;
	u8 and;
	u8 xor;
	int i, row, col, j, dst_shift;

	if (color != NULL) {
		LCDC_ERR("%s(%d): do not support color cursor\n",
			__func__, __LINE__);
		return;
	}

	if (mask == NULL) {
		/* If mask == NULL, set the cursor to transparent */
		memset((void *)pval, 0xaa, (width * height) >> 2);
	} else {
		pval_16 = (u16 *)pval;
		and_ptr = (u8 *)mask;
		xor_ptr = (u8 *)mask + height * mask_stride;
		for (row = 0; row < height; row++) {
			for (col = 0; col < width / 8; col += 2) {
				and = and_ptr[row * mask_stride + col];
				xor = xor_ptr[row * mask_stride + col];

				val_16 = __lcdc_mask_table(and, xor);

				and = and_ptr[row * mask_stride + col + 1];
				xor = xor_ptr[row * mask_stride + col + 1];

				*pval_16++ = __lcdc_mask_table(and, xor);
				*pval_16++ = val_16;
			}
		}
	}

	/* Must clear the rotation fifo since we only OR the bits */
	if (cursor_state->rotate)
		memset(pval + 256, 0, sizeof(u32) * 256);

	switch (cursor_state->rotate) {
	case 90:
		src = (u32 *)pval;
		val = *src++;

		for (i = 0; i < height; i++) {
			dst = (u32 *)(pval + 256) + (width >> 4) *
					(width - 0x10) + (i >> 4);
			dst_shift = ((i ^ 0xf) & 0xf) << 1;

			for (j = 0; j < width; j++) {
				*dst |= (val & 3) << dst_shift;
				val >>= 2;

				if ((j & 0xf) == 0xf) {
					val = *src++;
					dst -= 0x20 * (width >> 4);
				}
				dst += width >> 4;
			}
		}
		break;
	case 180:
		src = (u32 *)pval;
		dst = (u32 *)(pval + 256) + (height >> 4) * width - 1;
		val = *src++;

		for (i = 0; i < height; i++) {
			for (j = 0; j < width; j++) {
				*dst |= (val & 3) << ((width-j-1) & 0xf) * 2;
				val >>= 2;

				if ((j & 0xf) == 0xf) {
					val = *src++;
					dst--;
				}
			}
		}
		break;
	case 270:
		src = (u32 *)pval;
		val = *src++;

		for (i = 0; i < height; i++) {
			dst = (u32 *)(pval + 256) + width - 1 - (i >> 4);
			dst_shift = (i & 0xf) << 1;

			for (j = 0; j < width; j++) {
				*dst |= (val & 3) << dst_shift;
				val >>= 2;

				if ((j & 0xf) == 0xf) {
					val = *src++;
					dst += 0x20 * (width >> 4);
				}
				dst -= width >> 4;
			}
		}
		break;
	default:
		break;
	}
}


static void __lcdc_cal_cursor_region(int xpos, int ypos,
				struct vdss_rect *rect,
				int *left_skip, int *top_skip)
{
	int screen_width;
	int screen_height;
	int cur_x = 0, cur_y = 0;
	struct lcdc_cursor_state *cursor_state =
		&(lcdc_config.cursor_state);
	int cursor_width = cursor_state->width;
	int cursor_height = cursor_state->height;

	__lcdc_get_screen_size((u32 *)(&screen_width),
				(u32 *)(&screen_height),
				&panel_info);

	switch (cursor_state->rotate) {
	default:
	case 0:
		rect->left = xpos - cursor_state->xhot;
		if (rect->left > screen_width - 1)
			rect->left = screen_width - 1;

		rect->right = rect->left + cursor_width;
		if (rect->right > screen_width)
			rect->right = screen_width;

		rect->top = ypos - cursor_state->yhot;
		if (rect->top > screen_height - 1)
			rect->top = screen_height - 1;

		rect->bottom = rect->top + cursor_height;
		if (rect->bottom > screen_height)
			rect->bottom = screen_height;

		break;

	case 270:
		rect->top = xpos - cursor_state->xhot;
		if (rect->top > screen_width - 1)
			rect->top = screen_width - 1;

		rect->bottom = rect->top + cursor_width;
		if (rect->bottom > screen_width)
			rect->bottom = screen_width;

		rect->right = ypos - cursor_state->yhot;
		if (rect->right > screen_height)
			rect->right = screen_height;

		rect->right = screen_width - rect->right;
		rect->left = rect->right - cursor_height;
		break;

	case 90:
		rect->bottom  = xpos - cursor_state->xhot;
		if (rect->bottom > screen_width)
			rect->bottom = screen_width;

		rect->bottom = screen_height - rect->bottom;
		rect->top = rect->bottom - cursor_width;

		rect->left = ypos - cursor_state->yhot;
		if (rect->left > screen_height - 1)
			rect->left = screen_height - 1;

		rect->right = rect->left + cursor_height;
		if (rect->right > screen_height)
			rect->right = screen_height;
		break;

	case 180:
		rect->right = xpos - cursor_state->xhot;
		if (rect->right > screen_width)
			rect->right = screen_width;

		rect->right = screen_width - rect->right;
		rect->left  = rect->right - cursor_height;

		rect->bottom = ypos - cursor_state->yhot;
		if (rect->bottom > screen_height)
			rect->bottom = screen_height;

		rect->bottom = screen_height - rect->bottom;
		rect->top = rect->bottom - cursor_height;
		break;
	}

	if (rect->left < 0) {
		cur_x  = -(rect->left);
		rect->left = 0;
	}
	if (rect->top < 0) {
		cur_y  = -(rect->top);
		rect->top = 0;
	}

#if 0
	/* TODO: Why we have such requirement? */
	/* The smallest size is 2x2 */

	if ((rect->right - rect->left) < 2)
		rect->right = rect->left + 2;
	if ((rect->bottom - rect->top) < 2)
		rect->bottom = rect->top + 2;
	if (cur_x == (cursor_width-1))
		cur_x--;
	if (cur_y == (cursor_height-1))
		cur_y--;
#endif

	*left_skip = cur_x;
	*top_skip = cur_y;
}


static void __lcdc_set_cursor_region(struct vdss_rect *rect,
				int left_skip, int top_skip)
{
	u32 cur0_xy = 0x0;
	cur0_xy = CUR0_CUR_X(left_skip) | CUR0_CUR_Y(top_skip);

	/* Set hstart, hend */
	switch (panel_info.out_fmt) {
	case LCDC_OUT_8_BIT_RBGRBG:
		lcdc_write_reg(CUR0_HSTART,
			panel_info.hstart + (rect->left * 3));
		lcdc_write_reg(CUR0_HEND,
			panel_info.hstart + ((rect->right - 1) * 3));
		break;
	case LCDC_OUT_8_BIT_YUV422:
		lcdc_write_reg(CUR0_HSTART,
			panel_info.hstart + (rect->left * 2));
		lcdc_write_reg(CUR0_HEND,
			panel_info.hstart + ((rect->right - 1) * 2));
		break;
	default:
		lcdc_write_reg(CUR0_HSTART, panel_info.hstart + rect->left);
		lcdc_write_reg(CUR0_HEND, panel_info.hstart + rect->right - 1);
		break;
	}

	/* Set vstart, vend */
	lcdc_write_reg(CUR0_VSTART, panel_info.vstart + rect->top);
	lcdc_write_reg(CUR0_VEND, panel_info.vstart + rect->bottom - 1);

	lcdc_write_reg(CUR0_CURRENT_XY, cur0_xy);
	__lcdc_confirm_cursor_setting();
}


static void __lcdc_set_cursor_shape(void)
{
	struct vdss_rect vis_rect;
	int left_skip = 0, top_skip = 0;
	u32 reg_cur0_ctrl = 0x0;
	u32 s0_layer_sel;
	u32 *pval;
	int i;
	struct lcdc_cursor_state *cursor_state = &(lcdc_config.cursor_state);

	int width = cursor_state->width;
	int height = cursor_state->height;

	if (cursor_state->rotate == 0)
		pval = &(cursor_state->fifo[0]);
	else
		pval = &(cursor_state->fifo[256]);

	/* Only support 32*32, 64*64 */
	if (!((width == 32) && (height == 32)) &&
		!((width == 64) && (height == 64)))
		return;

	s0_layer_sel = lcdc_read_reg(S0_LAYER_SEL);
	s0_layer_sel &= S0_LS_LAYER_SEL(~(1 << LCDC_CURSOR));
	lcdc_write_reg(S0_LAYER_SEL, s0_layer_sel);

	__lcdc_cal_cursor_region(cursor_state->xpos, cursor_state->ypos,
		&vis_rect, &left_skip, &top_skip);
	__lcdc_set_cursor_region(&vis_rect, left_skip, top_skip);

	/* Set Cursor Color */
	lcdc_write_reg(CUR0_COLOR0, 0x0);
	lcdc_write_reg(CUR0_COLOR1, 0xffffff);
	lcdc_write_reg(CUR0_ALPHA, 0xff);

	if (cursor_state->width == 32) {
		/* 256 = 32 * 32 * 2bit / 8 */
		reg_cur0_ctrl |= CUR0_CTRL_MODE(LCDC_CURSOR_MODE_32x32x2_2_T);
		reg_cur0_ctrl |= CUR0_SRAM_ADDRST;
		lcdc_write_reg(CUR0_CTRL, reg_cur0_ctrl);
		for (i = 0; i < 256; i += 4)
			lcdc_write_reg(CUR0_FIFODATA + i, *pval++);
	} else {
		reg_cur0_ctrl |= CUR0_CTRL_MODE(LCDC_CURSOR_MODE_64x64x2_2_T);
		reg_cur0_ctrl |= CUR0_SRAM_ADDRST;
		lcdc_write_reg(CUR0_CTRL, reg_cur0_ctrl);
		/* 1024 = 64 * 64 * 2bit / 8 */
		for (i = 0; i < 1024; i += 4)
			lcdc_write_reg(CUR0_FIFODATA + i, *pval++);
	}
	reg_cur0_ctrl &= ~CUR0_SRAM_ADDRST;
	reg_cur0_ctrl |= CUR0_SETTING_VALID;
	lcdc_write_reg(CUR0_CTRL, reg_cur0_ctrl);

	if (cursor_state->show) {
		s0_layer_sel |= S0_LS_LAYER_SEL(1 << LCDC_CURSOR);
		lcdc_write_reg(S0_LAYER_SEL, s0_layer_sel);
	}
}


static void __lcdc_move_cursor(void)
{
	struct vdss_rect vis_rect;
	int left_skip = 0, top_skip = 0;
	struct lcdc_cursor_state *cursor_state = &(lcdc_config.cursor_state);
	int xpos = cursor_state->xpos;
	int ypos = cursor_state->ypos;
	u32 s0_layer_sel;

	/* Disable cursor */
	s0_layer_sel = lcdc_read_reg(S0_LAYER_SEL);
	s0_layer_sel &= S0_LS_LAYER_SEL(~(1 << LCDC_CURSOR));
	lcdc_write_reg(S0_LAYER_SEL, s0_layer_sel);

	if (cursor_state->show &&
		(cursor_state->width > 0) &&
		(cursor_state->height > 0)) {
		/* Calculate visible region of cursor */
		__lcdc_cal_cursor_region(xpos, ypos, &vis_rect,
					&left_skip, &top_skip);

		/* Calculate visible region of cursor */
		__lcdc_set_cursor_region(&vis_rect, left_skip, top_skip);

		/* Enable cursor */
		s0_layer_sel |= S0_LS_LAYER_SEL(1 << LCDC_CURSOR);
		lcdc_write_reg(S0_LAYER_SEL, s0_layer_sel);
	}
}

static void __lcdc_set_global_alpha(int layer)
{
	u32 alpha = 0x0;
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[layer]);
	alpha = LX_ALPHA_VAL(layer_state->alpha);
	lcdc_write_reg(reg_offset(layer, L0_ALPHA), alpha);
}

static void __lcdc_set_alpha_property(int layer)
{
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[layer]);
	u32 lx_ctrl;
	lx_ctrl = lcdc_read_reg(reg_offset(layer, L0_CTRL));
	if (layer_state->global_alpha)
		lx_ctrl |= LX_CTRL_GLOBAL_ALPHA;
	else
		lx_ctrl &= ~LX_CTRL_GLOBAL_ALPHA;

	if (layer_state->fmt == VDSS_PIXELFORMAT_8888) {
		if (layer_state->premulti_alpha)
			lx_ctrl |= LX_CTRL_PREMULTI_ALPHA;
		else
			lx_ctrl &= ~LX_CTRL_PREMULTI_ALPHA;

		if (layer_state->source_alpha)
			lx_ctrl |= LX_CTRL_SOURCE_ALPHA;
		else
			lx_ctrl &= ~LX_CTRL_SOURCE_ALPHA;
	} else {
		/* Spec require following setting for non-ARGB format */
		lx_ctrl |= LX_CTRL_PREMULTI_ALPHA;
		lx_ctrl &= ~LX_CTRL_SOURCE_ALPHA;
	}
	lx_ctrl &= ~LX_CTRL_CONFIRM;
	lcdc_write_reg(reg_offset(layer, L0_CTRL), lx_ctrl);
}

static void __lcdc_set_colorkey(int layer)
{
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[layer]);
	enum vdss_pixelformat fmt = layer_state->fmt;
	enum vdss_pixelformat prim_fmt =
		lcdc_config.layer_state[PRIMARY].fmt;
	bool prim_replicate = lcdc_config.layer_state[PRIMARY].replicate;
	u32 lx_ctrl;

	lx_ctrl = lcdc_read_reg(reg_offset(layer, L0_CTRL));

	if (layer_state->ckey_on) {
		lx_ctrl |= LX_CTRL_SRC_CKEY_EN;
		lcdc_write_reg(reg_offset(layer, L0_CKEYB_SRC),
				__lcdc_ckey_val(fmt,
						layer_state->replicate,
						layer_state->ckey_high));
		lcdc_write_reg(reg_offset(layer, L0_CKEYS_SRC),
				__lcdc_ckey_val(fmt,
						layer_state->replicate,
						layer_state->ckey_low));
	} else {
		lx_ctrl &= ~LX_CTRL_SRC_CKEY_EN;
	}

	if (layer_state->dst_ckey_on) {
		lx_ctrl |= LX_CTRL_DST_CKEY_EN;
		lcdc_write_reg(reg_offset(layer, L0_CKEYB_DST),
				__lcdc_ckey_val(prim_fmt,
						prim_replicate,
						layer_state->dst_ckey_high));
		lcdc_write_reg(reg_offset(layer, L0_CKEYS_DST),
				__lcdc_ckey_val(prim_fmt,
						prim_replicate,
						layer_state->dst_ckey_low));
	} else {
		lx_ctrl &= ~LX_CTRL_DST_CKEY_EN;
	}
	lx_ctrl &= ~LX_CTRL_CONFIRM;
	lcdc_write_reg(reg_offset(layer, L0_CTRL), lx_ctrl);
}

static void __lcdc_set_toplayer(void)
{
	u32 s0_disp_mode;
	s0_disp_mode = lcdc_read_reg(S0_DISP_MODE);
	s0_disp_mode &= ~S0_TOP_LAYER_MASK;
	s0_disp_mode |= S0_TOP_LAYER(lcdc_config.top_layer);
	s0_disp_mode |= S0_FRAME_VALID;	/* FRAME_VALID will be 0 if screen_en
					   is 0 even write 1 to this bit */
	lcdc_write_reg(S0_DISP_MODE, s0_disp_mode);
}

static bool __lcdc_set_parameters(int layer)
{
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[layer]);
	enum vdss_pixelformat fmt = layer_state->fmt;
	u32 lx_ctrl;
	bool ret = true;

	if (!layer_state->show)
		__lcdc_disable_layer(layer, true);
	lx_ctrl = lcdc_read_reg(reg_offset(layer, L0_CTRL));
	if (layer_state->need_vpp) {
		struct vpp_parms vpp_params;

		lx_ctrl &= ~LX_CTRL_BPP_MASK;
		lx_ctrl |= LX_CTRL_BPP(VPP_TO_LCDC_CTRL_BPP);
		memset(&vpp_params, 0, sizeof(vpp_params));
		vpp_params.src_fmt = fmt;
		vpp_params.src_base = layer_state->base;
		vpp_params.src_wstride_pixel = layer_state->surf_width;
		vpp_params.src_hstride_pixel = layer_state->surf_height;
		vpp_params.dst_fmt = VPP_TO_LCDC_PIXELFORMAT;
		vpp_params.dst_base = 0;

		vpp_ops.lock(true);
		ret = vpp_ops.set_params(&vpp_params);
	} else {
		lx_ctrl &= ~LX_CTRL_BPP_MASK;
		lx_ctrl |= LX_CTRL_BPP(__lcdc_fmt_to_hwfmt(fmt));
	}

	if (layer_state->replicate)
		lx_ctrl |= LX_CTRL_REPLICATE;
	else
		lx_ctrl &= ~LX_CTRL_REPLICATE;

	if (layer_state->premulti_alpha)
		lx_ctrl |= LX_CTRL_PREMULTI_ALPHA;
	else
		lx_ctrl &= ~LX_CTRL_PREMULTI_ALPHA;

	lx_ctrl |= LX_CTRL_CONFIRM;

	lcdc_write_reg(reg_offset(layer, L0_CTRL), lx_ctrl);

	/* Force to update src & dst rect related parameters */
	__lcdc_set_size(layer, true);
	__lcdc_flip(layer, LCDC_FLIP_FRAME);

	__lcdc_set_colorkey(layer);

	__lcdc_set_alpha_property(layer);
	__lcdc_set_global_alpha(layer);
	__lcdc_confirm_layer_setting(layer);

	if (layer_state->show)
		__lcdc_enable_layer(layer);

	return ret;
}

static void __lcdc_reset_layer_state(int layer)
{
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[layer]);

	/* Set default layer state here */
	memset(layer_state, 0, sizeof(*layer_state));
	layer_state->replicate = LCDC_DEFAULT_REPLICATE;
	layer_state->premulti_alpha = LCDC_DEFAULT_PREMULTI_ALPHA;
}

static void __lcdc_set_gamma_ramp(void)
{
	int i;
	u32 *pval;
	u32 s0_disp_mode;

	s0_disp_mode = lcdc_read_reg(S0_DISP_MODE);
	s0_disp_mode &= ~S0_GAMMA_COR_EN;
	s0_disp_mode |= S0_FRAME_VALID;
	lcdc_write_reg(S0_DISP_MODE, s0_disp_mode);

	pval = (u32 *)(&lcdc_config.gamma[0]);
	for (i = 0; i < 256 * 3; i += 4)
		lcdc_write_reg(S0_GAMMAFIFO_R + i, pval[i>>2]);
	s0_disp_mode |= S0_GAMMA_COR_EN;
	lcdc_write_reg(S0_DISP_MODE, s0_disp_mode);
}

static void __lcdc_set_interrupt(void)
{
	/* Clear interrupt firstly before enable */
	lcdc_write_reg(INT_CTRL_STATUS, lcdc_config.int_state);
	lcdc_write_reg(INT_MASK, lcdc_config.int_state);
}

static bool lcdc_change_mode(struct lcdc_panel_info *panel)
{
	u32 prim_base = lcdc_config.layer_state[panel->layer].base;
	enum vdss_pixelformat fmt = lcdc_config.layer_state[panel->layer].fmt;

	if (panel->pre_power_down)
		panel->pre_power_down();
	if (panel->post_power_down)
		panel->post_power_down();

	if (panel->pre_power_up)
		panel->pre_power_up();
	__lcdc_power_up(prim_base, fmt, panel);
	if (panel->post_power_up)
		panel->post_power_up();

	panel_info = *panel;
	return true;
}

static bool lcdc_init(void *regs, void *vpp_regs, u32 prim_base,
		unsigned int bpp, struct lcdc_panel_info *panel)
{
	struct lcdc_layer_state *layer_state;
	u32 s0_layer_sel;
	int i;
	unsigned int w, h;
	bool enabled = true;
	bool show = true;

	LCDC_ENTRY("%s\n", __func__);

	if (regs)
		lcdc_regs = regs;
	else {
		LCDC_ERR("%s(%d): NULL registers\n",
			__func__, __LINE__);
		return false;
	}

	panel_info = *panel;

	s0_layer_sel = lcdc_read_reg(S0_LAYER_SEL);
	if (!(s0_layer_sel & S0_LS_LAYER_SEL(1 << LCDC_PRIMARY))) {
		LCDC_DEBUG("%s: enabled == false\n", __func__);
		enabled = false;
		if (!prim_base || !bpp) {
			show = false;
			panel_info.reset();

			if (panel_info.pre_power_up)
				panel_info.pre_power_up();

			__lcdc_config_screen(&panel_info);

			if (panel_info.post_power_up)
				panel_info.post_power_up();
		} else {
			__lcdc_boot_up((void *)lcdc_regs, prim_base,
						bpp, &panel_info);
		}
	} else {
		u32 lx_ctrl;
		lx_ctrl = lcdc_read_reg(reg_offset(LCDC_PRIMARY, L0_CTRL));

		/* Driver should keep consistant with UBOOT */
		if ((lx_ctrl & LX_CTRL_BPP_MASK) == LO_CTRL_BPP_RGB565)
			bpp = 16;
		else
			bpp = 32;
		prim_base = lcdc_read_reg(reg_offset(LCDC_PRIMARY, L0_BASE0));
	}

	memset(&lcdc_config, 0, sizeof(lcdc_config));

	if (show) {
		__lcdc_get_screen_size(&w, &h, &panel_info);
		layer_state = &lcdc_config.layer_state[LCDC_PRIMARY];
		layer_state->base = prim_base;
		layer_state->surf_width = w;
		layer_state->surf_height = h;
		layer_state->fmt = (bpp == 16) ? (VDSS_PIXELFORMAT_565) :
						(VDSS_PIXELFORMAT_8888);
		layer_state->src_rect.left = layer_state->dst_rect.left = 0;
		layer_state->src_rect.top = layer_state->dst_rect.top = 0;
		layer_state->src_rect.right = layer_state->dst_rect.right = w;
		layer_state->src_rect.bottom = layer_state->dst_rect.bottom = h;
		layer_state->replicate = LCDC_DEFAULT_REPLICATE;
		if (layer_state->fmt == VDSS_PIXELFORMAT_8888)
			layer_state->source_alpha = 1;
		else
			layer_state->source_alpha = 0;
		layer_state->premulti_alpha = LCDC_DEFAULT_PREMULTI_ALPHA;
		layer_state->in_use = true;
		layer_state->show = true;
	}

	lcdc_config.vpp_handle = (void *)0xabcdabcd;
	vdss_install_vpp_ops(&vpp_ops);
	if (vpp_regs)
		vpp_ops.init(vpp_regs);
	else
		vpp_ops.init(NULL);

	lcdc_config.top_layer = panel_info.maxlayer;
	lcdc_config.gamma_enable = false;
	for (i = 0; i < 256; i++) {
		lcdc_config.gamma[i] = i;
		lcdc_config.gamma[256 + i] = i;
		lcdc_config.gamma[512 + i] = i;
	}
	for (i = LCDC_OVERLAY_1; i <= panel_info.maxlayer; i++) {
		layer_state = &lcdc_config.layer_state[i];
		layer_state->brightness = 0;
		layer_state->contrast = 128;
		layer_state->hue = 0;
		layer_state->saturation = 128;
	}
	return enabled;
}

static void lcdc_terminate(void)
{
	LCDC_ENTRY("%s\n", __func__);
	if (lcdc_config.vpp_handle) {
		vpp_ops.terminate();
		lcdc_config.vpp_handle = NULL;
	}

	if (panel_info.pre_power_down)
		panel_info.pre_power_down();
	if (panel_info.post_power_down)
		panel_info.post_power_down();
}


static void lcdc_sleep(void)
{
	LCDC_ENTRY("%s\n", __func__);

	if (panel_info.pre_power_down)
		panel_info.pre_power_down();

	if (lcdc_config.vpp_handle)
		vpp_ops.sleep();

	if (panel_info.post_power_down)
		panel_info.post_power_down();
}

static bool lcdc_wakeup(void)
{
	int layer;
	u32 s0_layer_sel;
	bool enabled = true;

	LCDC_ENTRY("%s\n", __func__);

	s0_layer_sel = lcdc_read_reg(S0_LAYER_SEL);

	/* accel-hiberation/resume, VCC will not enable. suspend/resume ok.
	 * reason: when hiberation, FB will disable VCC via gpio, and pm
	 * suspend will save gpio. after resume back, uboot will init lcd.
	 * later pm resume will restore saved gpio, this will disable VCC
	 * and FB won't power on lcd since uboot have done, so add workaround
	 * here.
	 */
	if (panel_info.pre_power_up)
		panel_info.pre_power_up();

	if (!(s0_layer_sel & S0_LS_LAYER_SEL(1 << LCDC_PRIMARY))) {
		enabled = false;
		__lcdc_config_screen(&panel_info);
	}

	if (lcdc_config.vpp_handle)
		vpp_ops.wakeup();

	__lcdc_set_interrupt();

	for (layer = LCDC_PRIMARY; layer <= panel_info.maxlayer; layer++) {
		struct lcdc_layer_state *layer_state =
			&(lcdc_config.layer_state[layer]);
		if (layer_state->show)
			__lcdc_set_parameters(layer);
	}

	if (lcdc_config.cursor_state.show) {
		__lcdc_set_cursor_shape();
		__lcdc_move_cursor();
	}

	__lcdc_set_toplayer();

	if (lcdc_config.gamma_enable)
		__lcdc_set_gamma_ramp();

	/* tmp solution to fix hibernation cold boot taishan 8" bl issue */
	/* if (!enabled) */
		if (panel_info.post_power_up)
			panel_info.post_power_up();

	return enabled;
}



static void lcdc_get_scanline(struct lcdc_scanline *data)
{
	LCDC_ENTRY(("%s\n", __func__));
	*(data->scanline) = lcdc_read_reg(S0_VCOUNT) - panel_info.vstart;
}

static void lcdc_wait_for_vblank(struct lcdc_wait_for_vblank *data)
{
	int times = 3000000;	/* ~= 18ms */
	unsigned int line, scr_vend;
	bool in_vb;

	LCDC_ENTRY("%s\n", __func__);
	do {
		line = lcdc_read_reg(S0_VCOUNT);
		scr_vend = lcdc_read_reg(S0_ACT_VEND);
		in_vb = (line >= scr_vend) &&
			(line < panel_info.vstart);
		times--;
	} while ((times > 0) && (in_vb == data->block_begin));

	return;
}


static void lcdc_clear_interrupt(enum lcdc_interrupt_type type)
{
	if (type == LCDC_INTERRUPT_ALL) {
		lcdc_write_reg(INT_CTRL_STATUS, 0xFFFFFFFF);
	} else {
		u32 val;
		val = 1 << type;
		lcdc_write_reg(INT_CTRL_STATUS, val);
	}
}

static void lcdc_enable_interrupt(enum lcdc_interrupt_type type)
{
	if (type == LCDC_INTERRUPT_ALL)
		lcdc_config.int_state = 0xFFFFFFFF;
	else
		lcdc_config.int_state |= 1 << type;

	__lcdc_set_interrupt();
}

static void lcdc_disable_interrupt(enum lcdc_interrupt_type type)
{
	if (type == LCDC_INTERRUPT_ALL)
		lcdc_config.int_state = 0x0;
	else
		lcdc_config.int_state &= ~(1 << type);

	__lcdc_set_interrupt();
}

static u32 lcdc_irq_detected(enum lcdc_interrupt_type type)
{
	u32 int_stat;
	u32 int_mask;
	u32 status;

	int_stat = lcdc_read_reg(INT_CTRL_STATUS);
	int_mask = lcdc_read_reg(INT_MASK);

	status = int_stat & int_mask;

	if (type != LCDC_INTERRUPT_ALL)
		status &= 1 << type;

	return status;
}

static void lcdc_get_mode(struct lcdc_mode *disp_mode)
{
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[LCDC_PRIMARY]);
	LCDC_ENTRY("%s\n", __func__);

	if (disp_mode) {
		disp_mode->fmt = layer_state->fmt;
		__lcdc_get_screen_size(&disp_mode->width,
					&disp_mode->height,
					&panel_info);
		disp_mode->stride = layer_state->surf_width *
					__lcdc_fmt_to_bpp(disp_mode->fmt);
		disp_mode->ref_rate = LCDC_DISPLAY_FREQUENCY;
	}
}

static void lcdc_get_video_mem(struct lcdc_video_mem *data)
{
	LCDC_ENTRY("%s\n", __func__);
	data->size = lcdc_config.fb_size;

	__lcdc_get_primary_size(&data->primary_size);
}

static int lcdc_alloc_overlay(struct lcdc_overlay *data)
{
	struct lcdc_layer_state *layer_state;
	int layer, ret_layer;
	bool need_vpp = false;

	LCDC_ENTRY("%s: layer:%d fmt:%d w:%d h:%d\n", __func__,
		data->layer, data->fmt, data->width, data->height);

	if ((data->layer != LCDC_LAYER_UNKNOWN) &&
		((data->layer < LCDC_OVERLAY_1) ||
		 (data->layer > panel_info.maxlayer))) {
		LCDC_ERR("%s(%d): wrong layer to allocate\n",
			__func__, __LINE__);
		return LCDC_LAYER_UNKNOWN;
	}

	/* if support the format */
	switch (data->fmt) {
	case VDSS_PIXELFORMAT_565:
	case VDSS_PIXELFORMAT_556:
	case VDSS_PIXELFORMAT_655:

	case VDSS_PIXELFORMAT_BGRX_8880:
	case VDSS_PIXELFORMAT_8888:
		data->wstride_byte = ((__lcdc_fmt_to_bpp(data->fmt) *
					data->width + 7) / 8) * 8;
		data->hstride_byte = data->wstride_byte * data->height;
		data->wstride_pixel = data->width;
		data->hstride_pixel = data->hstride_byte / data->wstride_byte;
		break;
	case VDSS_PIXELFORMAT_YUYV:
	case VDSS_PIXELFORMAT_UYVY:
	case VDSS_PIXELFORMAT_YUY2:
	case VDSS_PIXELFORMAT_YUNV:
	case VDSS_PIXELFORMAT_YVYU:
	case VDSS_PIXELFORMAT_UYNV:
	case VDSS_PIXELFORMAT_VYUY:

	case VDSS_PIXELFORMAT_IMC1:
	case VDSS_PIXELFORMAT_IMC3:
	case VDSS_PIXELFORMAT_YV12:
	case VDSS_PIXELFORMAT_I420:
	case VDSS_PIXELFORMAT_UYVI:
	case VDSS_PIXELFORMAT_NV12:
	case VDSS_PIXELFORMAT_NV21:
		if (!lcdc_config.vpp_handle) {
			data->fmt = VDSS_PIXELFORMAT_UNKNOWN;
			return LCDC_LAYER_UNKNOWN;
		}

		if (vpp_ops.alloc_overlay(data))
			need_vpp = true;
		else
			return LCDC_LAYER_UNKNOWN;

		break;
	default:
		data->fmt = VDSS_PIXELFORMAT_UNKNOWN;
		return LCDC_LAYER_UNKNOWN;
	}

	/* if support the dimension */
	if ((data->width > LCDC_MAX_OVERLAY_WIDTH) ||
		(data->height > LCDC_MAX_OVERLAY_HEIGHT) ||
		(data->fmt == VDSS_PIXELFORMAT_UNKNOWN))
		return LCDC_LAYER_UNKNOWN;


	ret_layer = LCDC_LAYER_UNKNOWN;
	if (data->layer == LCDC_LAYER_UNKNOWN) {
		for (layer = LCDC_OVERLAY_1;
			layer <= panel_info.maxlayer;
			layer++) {
			layer_state =  &(lcdc_config.layer_state[layer]);
			if (!layer_state->in_use) {
				ret_layer = layer;
				break;
			}
		}
	} else {
		layer_state =  &(lcdc_config.layer_state[data->layer]);
		if (!layer_state->in_use)
			ret_layer = data->layer;
	}

	if (ret_layer != LCDC_LAYER_UNKNOWN) {
		__lcdc_reset_layer_state(ret_layer);
		layer_state =  &(lcdc_config.layer_state[ret_layer]);
		layer_state->in_use = true;
		layer_state->show = false;
		layer_state->need_vpp = need_vpp;
		layer_state->fmt = data->fmt;
		layer_state->surf_width = data->wstride_pixel;
		layer_state->surf_height = data->hstride_pixel;
		layer_state->src_rect.left = layer_state->src_rect.top = 0;
		layer_state->src_rect.right = data->width;
		layer_state->src_rect.bottom = data->height;
	}

	LCDC_DEBUG("%s: ret_layer:%d wstride_pixel:%d hstride_pixel:%d\n",
		__func__, ret_layer, data->wstride_pixel, data->hstride_pixel);

	return ret_layer;
}

static void lcdc_show_overlay(int layer)
{
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[layer]);
	LCDC_ENTRY("%s layer=%d\n", __func__, layer);

	if (!layer_state->show &&
		(layer_state->dst_rect.right -
		 layer_state->dst_rect.left != 0) &&
		(layer_state->dst_rect.bottom -
		 layer_state->dst_rect.top != 0)) {
		layer_state->show = true;
		__lcdc_set_parameters(layer);
	}
}

static void lcdc_hide_overlay(int layer)
{
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[layer]);
	LCDC_ENTRY("%s\n", __func__);
	if (layer_state->show) {
		layer_state->show = false;
		__lcdc_disable_layer(layer, false);

		if (layer_state->need_vpp)
			vpp_ops.unlock();
	}
}

static void lcdc_free_overlay(int layer)
{
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[layer]);
	LCDC_ENTRY("%s\n", __func__);
	if (layer_state->in_use && layer_state->show)
		lcdc_hide_overlay(layer);
	layer_state->in_use = false;
	return;
}

#if 0
static void lcdc_print_parameters(struct lcdc_parms *data)
{
	LCDC_DEBUG("LCD layer %d parameters:\n", data->layer);
	LCDC_DEBUG("fmt=%d\n", data->fmt);
	LCDC_DEBUG("surf_width=%d\n", data->surf_width);
	LCDC_DEBUG("surf_height=%d\n", data->surf_height);
	LCDC_DEBUG("src_rect=%d %d %d %d\n", data->src_rect.left,
					data->src_rect.top,
					data->src_rect.right,
					data->src_rect.bottom);
	LCDC_DEBUG("dst_rect=%d %d %d %d\n", data->dst_rect.left,
					data->dst_rect.top,
					data->dst_rect.right,
					data->dst_rect.bottom);
	LCDC_DEBUG("base=0x%x\n", data->base);
	LCDC_DEBUG("ckey_on=%d\n", data->ckey_on);
	LCDC_DEBUG("ckey_low=0x%x\n", data->ckey_low);
	LCDC_DEBUG("ckey_high=0x%x\n", data->ckey_high);
	LCDC_DEBUG("dst_ckey_on=%d\n", data->dst_ckey_on);
	LCDC_DEBUG("dst_ckey_low=0x%x\n", data->dst_ckey_low);
	LCDC_DEBUG("dst_ckey_high=0x%x\n", data->dst_ckey_high);
	LCDC_DEBUG("global_alpha=%d\n", data->g_alpha_enabled);
	LCDC_DEBUG("source_alpha=%d\n", data->src_alpha_enabled);
	LCDC_DEBUG("premulti_alpha=%d\n", data->pre_alpha_enabled);
	LCDC_DEBUG("alpha=0x%x\n", data->alpha);
}
#endif

static bool lcdc_set_parameters(struct lcdc_parms *data)
{
	int layer = data->layer;
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[layer]);
	LCDC_ENTRY("%s\n", __func__);

	layer_state->need_vpp = __lcdc_need_vpp(data->fmt);
	if ((lcdc_config.vpp_handle == NULL) && layer_state->need_vpp) {
		LCDC_ERR("%s(%d)\n", __func__, __LINE__);
		return false;
	}

	layer_state->fmt = data->fmt;
	layer_state->surf_width = data->surf_width;
	layer_state->surf_height = data->surf_height;
	layer_state->src_rect = data->src_rect;
	layer_state->dst_rect = data->dst_rect;
	layer_state->base = data->base;
	layer_state->ckey_on = data->ckey_on;
	layer_state->ckey_low = data->ckey_low;
	layer_state->ckey_high = data->ckey_high;
	layer_state->dst_ckey_on = data->dst_ckey_on;
	layer_state->dst_ckey_low = data->dst_ckey_low;
	layer_state->dst_ckey_high = data->dst_ckey_high;
	layer_state->global_alpha = data->g_alpha_enabled;
	layer_state->source_alpha = data->src_alpha_enabled;
	layer_state->premulti_alpha = data->pre_alpha_enabled;
	layer_state->alpha = data->alpha;
	return __lcdc_set_parameters(layer);
}

static void lcdc_get_parameters(struct lcdc_parms *data)
{
	int layer = data->layer;
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[layer]);
	LCDC_ENTRY("%s\n", __func__);

	if ((lcdc_config.vpp_handle == NULL) && __lcdc_need_vpp(data->fmt))
		LCDC_ERR("%s(%d)\n", __func__, __LINE__);

	data->fmt = layer_state->fmt;
	data->surf_width = layer_state->surf_width;
	data->surf_height = layer_state->surf_height;
	data->src_rect = layer_state->src_rect;
	data->dst_rect = layer_state->dst_rect;
	data->base = layer_state->base;
	data->ckey_on = layer_state->ckey_on;
	data->ckey_low = layer_state->ckey_low;
	data->ckey_high = layer_state->ckey_high;
	data->dst_ckey_on = layer_state->dst_ckey_on;
	data->dst_ckey_low = layer_state->dst_ckey_low;
	data->dst_ckey_high = layer_state->dst_ckey_high;
	data->g_alpha_enabled = layer_state->global_alpha;
	data->src_alpha_enabled = layer_state->source_alpha;
	data->pre_alpha_enabled = layer_state->premulti_alpha;
	data->alpha = layer_state->alpha;
}


static void lcdc_pan_display(int layer, int x, int y)
{
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[layer]);
	int w, h;
	LCDC_ENTRY("%s\n", __func__);
	w = layer_state->src_rect.right - layer_state->src_rect.left;
	h = layer_state->src_rect.bottom - layer_state->src_rect.top;
	layer_state->src_rect.left = x;
	layer_state->src_rect.right = x + w;
	layer_state->src_rect.top = y;
	layer_state->src_rect.bottom = y + h;
	__lcdc_flip(layer, LCDC_FLIP_FRAME);
	__lcdc_confirm_layer_setting(layer);
}

static void lcdc_set_overlay_pos(int layer, struct vdss_rect *src,
				struct vdss_rect *dst)
{
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[layer]);
	LCDC_ENTRY("%s\n", __func__);
	if (dst)
		layer_state->dst_rect = *dst;
	if (src)
		layer_state->src_rect = *src;

	if (layer_state->show) {
		/* Only update src, dst related parameters only when they
		 * change */
		__lcdc_set_size(layer, false);
		__lcdc_confirm_layer_setting(layer);
	}
}

static void lcdc_set_global_alpha(int layer, u8 alpha)
{
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[layer]);
	LCDC_ENTRY("%s\n", __func__);
	layer_state->alpha = alpha;
	if (layer_state->show) {
		__lcdc_set_global_alpha(layer);
		__lcdc_confirm_layer_setting(layer);
	}
}

static void lcdc_set_alpha_property(int layer, bool premulti,
				bool global, bool source)
{
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[layer]);
	LCDC_ENTRY("%s\n", __func__);
	layer_state->premulti_alpha = premulti;
	layer_state->global_alpha = global;
	layer_state->source_alpha = source;
	if (layer_state->show) {
		__lcdc_set_alpha_property(layer);
		__lcdc_confirm_layer_setting(layer);
	}
}

static void lcdc_set_src_ckey(int layer, bool on, u32 high, u32 low)
{
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[layer]);
	LCDC_ENTRY("%s\n", __func__);
	layer_state->ckey_on = on;
	layer_state->ckey_high = high;
	layer_state->ckey_low = low;
	if (layer_state->show) {
		__lcdc_set_colorkey(layer);
		__lcdc_confirm_layer_setting(layer);
	}
}

static void lcdc_set_dst_ckey(int layer, bool on, u32 high, u32 low)
{
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[layer]);
	LCDC_ENTRY("%s\n", __func__);
	layer_state->dst_ckey_on = on;
	layer_state->dst_ckey_high = high;
	layer_state->dst_ckey_low = low;
	if (layer_state->show) {
		__lcdc_set_colorkey(layer);
		__lcdc_confirm_layer_setting(layer);
	}
}

static int lcdc_get_toplayer(void)
{
	LCDC_ENTRY("%s\n", __func__);
	return lcdc_config.top_layer;
}

static void lcdc_set_toplayer(int layer)
{
	LCDC_ENTRY("%s\n", __func__);
	lcdc_config.top_layer = layer;
	__lcdc_set_toplayer();
}


static void lcdc_flip_overlay(int layer, u32 base, enum lcdc_flip_mode field)
{
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[layer]);
	LCDC_ENTRY("%s\n", __func__);
	layer_state->base = base;
	if (layer_state->show) {
		__lcdc_flip(layer, field);
		__lcdc_confirm_layer_setting(layer);
	}
}

static void lcdc_set_cursor_shape(u32 *mask, int mask_stride, u32 *color,
				int xhot, int yhot, int width, int height)
{
	struct lcdc_cursor_state *cursor_state = &(lcdc_config.cursor_state);

	cursor_state->xhot = xhot;
	cursor_state->yhot = yhot;
	cursor_state->width = width;
	cursor_state->height = height;
	__lcdc_gen_cursor_fifo(color, mask, mask_stride);
	__lcdc_set_cursor_shape();
}

static void lcdc_move_cursor(int xpos, int ypos)
{
	struct lcdc_cursor_state *cursor_state = &(lcdc_config.cursor_state);

	cursor_state->xpos = xpos;
	cursor_state->ypos = ypos;
	cursor_state->show = (xpos != -1);
	__lcdc_move_cursor();
}

static void lcdc_set_cursor_rotate(int angle)
{
	LCDC_ENTRY("%s\n", __func__);
	lcdc_config.cursor_state.rotate = angle;
}

static void lcdc_reset(void)
{
	LCDC_ENTRY("%s\n", __func__);
}

static void lcdc_output_ctrl(bool turn_off)
{
	/* Wait until scan line go below VSTART */
	unsigned int i;
	LCDC_ENTRY("%s\n", __func__);

	do {
		i = lcdc_read_reg(S0_VCOUNT);
	} while (i >= panel_info.vstart);
}

static void lcdc_get_gamma_ramp(u16 *gamma)
{
	int i;

	LCDC_ENTRY("%s\n", __func__);
	if (gamma)
		for (i = 0; i < 256 * 3; i++)
			gamma[i] = lcdc_config.gamma[i];
}

static void lcdc_set_gamma_ramp(u16 *gamma)
{
	int i;

	LCDC_ENTRY("%s\n", __func__);
	if (gamma) {
		for (i = 0; i < 256 * 3; i++)
			lcdc_config.gamma[i] = (u8)gamma[i];
		lcdc_config.gamma_enable = true;
		__lcdc_set_gamma_ramp();
	}
}

static void lcdc_get_color_ctrl(int layer, struct lcdc_color_ctrl *data)
{
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[layer]);

	LCDC_ENTRY("%s\n", __func__);
	if (data->flags & LCDC_COLORCONTROL_BRIGHTNESS)
		data->brightness = layer_state->brightness;
	if (data->flags & LCDC_COLORCONTROL_CONTRAST)
		data->contrast = layer_state->contrast;
	if (data->flags & LCDC_COLORCONTROL_HUE)
		data->hue = layer_state->hue;
	if (data->flags & LCDC_COLORCONTROL_SATURATION)
		data->saturation = layer_state->saturation;
}

static void lcdc_set_color_ctrl(int layer, struct lcdc_color_ctrl *data)
{
	struct lcdc_layer_state *layer_state =
		&(lcdc_config.layer_state[layer]);

	LCDC_ENTRY("%s\n", __func__);
	if (lcdc_config.vpp_handle && layer_state->need_vpp) {
		if (data->flags & LCDC_COLORCONTROL_BRIGHTNESS) {
			layer_state->brightness = data->brightness;
			vpp_ops.update_bright(layer_state->brightness);
		}
		if (data->flags & LCDC_COLORCONTROL_CONTRAST) {
			layer_state->contrast = data->contrast;
			vpp_ops.update_contrast(data->contrast);
		}
		if (data->flags & LCDC_COLORCONTROL_HUE)
			layer_state->hue = data->hue;
		if (data->flags & LCDC_COLORCONTROL_SATURATION)
			layer_state->saturation = data->saturation;
	}
}


static void lcdc_print_register(void)
{
	LCDC_DUMP("LCD registers:\n");
	LCDC_DUMP("S0_HSYNC_PERIOD=0x%08x\n", lcdc_read_reg(S0_HSYNC_PERIOD));
	LCDC_DUMP("S0_HSYNC_WIDTH=0x%08x\n", lcdc_read_reg(S0_HSYNC_WIDTH));
	LCDC_DUMP("S0_VSYNC_PERIOD=0x%08x\n", lcdc_read_reg(S0_VSYNC_PERIOD));
	LCDC_DUMP("S0_VSYNC_WIDTH=0x%08x\n", lcdc_read_reg(S0_VSYNC_WIDTH));
	LCDC_DUMP("S0_ACT_HSTART=0x%08x\n", lcdc_read_reg(S0_ACT_HSTART));
	LCDC_DUMP("S0_ACT_VSTART=0x%08x\n", lcdc_read_reg(S0_ACT_VSTART));
	LCDC_DUMP("S0_ACT_HEND=0x%08x\n", lcdc_read_reg(S0_ACT_HEND));
	LCDC_DUMP("S0_ACT_VEND=0x%08x\n", lcdc_read_reg(S0_ACT_VEND));
	LCDC_DUMP("S0_OSC_RATIO=0x%08x\n", lcdc_read_reg(S0_OSC_RATIO));
	LCDC_DUMP("S0_TIM_CTRL=0x%08x\n", lcdc_read_reg(S0_TIM_CTRL));
	LCDC_DUMP("S0_TIM_STATUS=0x%08x\n", lcdc_read_reg(S0_TIM_STATUS));
	LCDC_DUMP("S0_HCOUNT=0x%08x\n", lcdc_read_reg(S0_HCOUNT));
	LCDC_DUMP("S0_VCOUNT=0x%08x\n", lcdc_read_reg(S0_VCOUNT));
	LCDC_DUMP("S0_BLANK=0x%08x\n", lcdc_read_reg(S0_BLANK));
	LCDC_DUMP("S0_BACK_COLOR=0x%08x\n", lcdc_read_reg(S0_BACK_COLOR));
	LCDC_DUMP("S0_DISP_MODE=0x%08x\n", lcdc_read_reg(S0_DISP_MODE));
	LCDC_DUMP("S0_LAYER_SEL=0x%08x\n", lcdc_read_reg(S0_LAYER_SEL));
	LCDC_DUMP("S0_RGB_SEQ=0x%08x\n", lcdc_read_reg(S0_RGB_SEQ));
	LCDC_DUMP("S0_RGB_YUV_COEF1=0x%08x\n", lcdc_read_reg(S0_RGB_YUV_COEF1));
	LCDC_DUMP("S0_RGB_YUV_COEF2=0x%08x\n", lcdc_read_reg(S0_RGB_YUV_COEF2));
	LCDC_DUMP("S0_RGB_YUV_COEF3=0x%08x\n", lcdc_read_reg(S0_RGB_YUV_COEF3));
	LCDC_DUMP("S0_YUV_CTRL=0x%08x\n", lcdc_read_reg(S0_YUV_CTRL));
	LCDC_DUMP("S0_TV_FIELD=0x%08x\n", lcdc_read_reg(S0_TV_FIELD));
	LCDC_DUMP("S0_INT_LINE=0x%08x\n", lcdc_read_reg(S0_INT_LINE));
	LCDC_DUMP("S0_LAYER_STATUS=0x%08x\n", lcdc_read_reg(S0_LAYER_STATUS));
	LCDC_DUMP("DMA_STATUS=0x%08x\n", lcdc_read_reg(DMA_STATUS));
	LCDC_DUMP("SCR_CTRL=0X%08X\n", lcdc_read_reg(SCR_CTRL));
	LCDC_DUMP("INT_MASK=0X%08X\n", lcdc_read_reg(INT_MASK));
	LCDC_DUMP("INT_CTRL_STATUS=0X%08X\n", lcdc_read_reg(INT_CTRL_STATUS));

	/* Lay0 register */
	LCDC_DUMP("L0_CTRL=0x%08x\n", lcdc_read_reg(L0_CTRL));
	LCDC_DUMP("L0_HSTART=0x%08x\n", lcdc_read_reg(L0_HSTART));
	LCDC_DUMP("L0_VSTART=0X%08X\n", lcdc_read_reg(L0_VSTART));
	LCDC_DUMP("L0_HEND=0X%08X\n", lcdc_read_reg(L0_HEND));
	LCDC_DUMP("L0_VEND=0x%08x\n", lcdc_read_reg(L0_VEND));
	LCDC_DUMP("L0_BASE0=0x%08x\n", lcdc_read_reg(L0_BASE0));
	LCDC_DUMP("L0_BASE1=0X%08X\n", lcdc_read_reg(L0_BASE1));
	LCDC_DUMP("L0_XSIZE=0X%08X\n", lcdc_read_reg(L0_XSIZE));
	LCDC_DUMP("L0_YSIZE=0x%08x\n", lcdc_read_reg(L0_YSIZE));
	LCDC_DUMP("L0_SKIP=0x%08x\n", lcdc_read_reg(L0_SKIP));
	LCDC_DUMP("L0_DMA_CTRL=0X%08X\n", lcdc_read_reg(L0_DMA_CTRL));
	LCDC_DUMP("L0_ALPHA=0X%08X\n", lcdc_read_reg(L0_ALPHA));
	LCDC_DUMP("L0_CKEYB_SRC=0x%08x\n", lcdc_read_reg(L0_CKEYB_SRC));
	LCDC_DUMP("L0_CKEYS_SRC=0X%08X\n", lcdc_read_reg(L0_CKEYS_SRC));
	LCDC_DUMP("L0_CKEYB_DST=0x%08x\n", lcdc_read_reg(L0_CKEYB_DST));
	LCDC_DUMP("L0_CKEYS_DST=0X%08X\n", lcdc_read_reg(L0_CKEYS_DST));
	LCDC_DUMP("L0_FIFO_CHK=0X%08X\n", lcdc_read_reg(L0_FIFO_CHK));
	LCDC_DUMP("L0_FIFO_STATUS=0x%08x\n", lcdc_read_reg(L0_FIFO_STATUS));

	/* Lay1 Register */
	LCDC_DUMP("L1_CTRL=0x%08x\n", lcdc_read_reg(L1_CTRL));
	LCDC_DUMP("L1_HSTART=0x%08x\n", lcdc_read_reg(L1_HSTART));
	LCDC_DUMP("L1_VSTART=0X%08X\n", lcdc_read_reg(L1_VSTART));
	LCDC_DUMP("L1_HEND=0X%08X\n", lcdc_read_reg(L1_HEND));
	LCDC_DUMP("L1_VEND=0x%08x\n", lcdc_read_reg(L1_VEND));
	LCDC_DUMP("L1_BASE0=0x%08x\n", lcdc_read_reg(L1_BASE0));
	LCDC_DUMP("L1_BASE1=0X%08X\n", lcdc_read_reg(L1_BASE1));
	LCDC_DUMP("L1_XSIZE=0X%08X\n", lcdc_read_reg(L1_XSIZE));
	LCDC_DUMP("L1_YSIZE=0x%08x\n", lcdc_read_reg(L1_YSIZE));
	LCDC_DUMP("L1_SKIP=0x%08x\n", lcdc_read_reg(L1_SKIP));
	LCDC_DUMP("L1_DMA_CTRL=0X%08X\n", lcdc_read_reg(L1_DMA_CTRL));
	LCDC_DUMP("L1_ALPHA=0X%08X\n", lcdc_read_reg(L1_ALPHA));
	LCDC_DUMP("L1_CKEYB_SRC=0x%08x\n", lcdc_read_reg(L1_CKEYB_SRC));
	LCDC_DUMP("L1_CKEYS_SRC=0X%08X\n", lcdc_read_reg(L1_CKEYS_SRC));
	LCDC_DUMP("L1_CKEYB_DST=0x%08x\n", lcdc_read_reg(L1_CKEYB_DST));
	LCDC_DUMP("L1_CKEYS_DST=0X%08X\n", lcdc_read_reg(L1_CKEYS_DST));
	LCDC_DUMP("L1_FIFO_CHK=0X%08X\n", lcdc_read_reg(L1_FIFO_CHK));
	LCDC_DUMP("L1_FIFO_STATUS=0x%08x\n", lcdc_read_reg(L1_FIFO_STATUS));

	/* Lay2 Register */
	LCDC_DUMP("L2_CTRL=0x%08x\n", lcdc_read_reg(L2_CTRL));
	LCDC_DUMP("L2_HSTART=0x%08x\n", lcdc_read_reg(L2_HSTART));
	LCDC_DUMP("L2_VSTART=0X%08X\n", lcdc_read_reg(L2_VSTART));
	LCDC_DUMP("L2_HEND=0X%08X\n", lcdc_read_reg(L2_HEND));
	LCDC_DUMP("L2_VEND=0x%08x\n", lcdc_read_reg(L2_VEND));
	LCDC_DUMP("L2_BASE0=0x%08x\n", lcdc_read_reg(L2_BASE0));
	LCDC_DUMP("L2_BASE1=0X%08X\n", lcdc_read_reg(L2_BASE1));
	LCDC_DUMP("L2_XSIZE=0X%08X\n", lcdc_read_reg(L2_XSIZE));
	LCDC_DUMP("L2_YSIZE=0x%08x\n", lcdc_read_reg(L2_YSIZE));
	LCDC_DUMP("L2_SKIP=0x%08x\n", lcdc_read_reg(L2_SKIP));
	LCDC_DUMP("L2_DMA_CTRL=0X%08X\n", lcdc_read_reg(L2_DMA_CTRL));
	LCDC_DUMP("L2_ALPHA=0X%08X\n", lcdc_read_reg(L2_ALPHA));
	LCDC_DUMP("L2_CKEYB_SRC=0x%08x\n", lcdc_read_reg(L2_CKEYB_SRC));
	LCDC_DUMP("L2_CKEYS_SRC=0X%08X\n", lcdc_read_reg(L2_CKEYS_SRC));
	LCDC_DUMP("L2_CKEYB_DST=0x%08x\n", lcdc_read_reg(L2_CKEYB_DST));
	LCDC_DUMP("L2_CKEYS_DST=0X%08X\n", lcdc_read_reg(L2_CKEYS_DST));
	LCDC_DUMP("L2_FIFO_CHK=0X%08X\n", lcdc_read_reg(L2_FIFO_CHK));
	LCDC_DUMP("L2_FIFO_STATUS=0x%08x\n", lcdc_read_reg(L2_FIFO_STATUS));

	/* Lay3 Register */
	LCDC_DUMP("L3_CTRL=0x%08x\n", lcdc_read_reg(L3_CTRL));
	LCDC_DUMP("L3_HSTART=0x%08x\n", lcdc_read_reg(L3_HSTART));
	LCDC_DUMP("L3_VSTART=0X%08X\n", lcdc_read_reg(L3_VSTART));
	LCDC_DUMP("L3_HEND=0X%08X\n", lcdc_read_reg(L3_HEND));
	LCDC_DUMP("L3_VEND=0x%08x\n", lcdc_read_reg(L3_VEND));
	LCDC_DUMP("L3_BASE0=0x%08x\n", lcdc_read_reg(L3_BASE0));
	LCDC_DUMP("L3_BASE1=0X%08X\n", lcdc_read_reg(L3_BASE1));
	LCDC_DUMP("L3_XSIZE=0X%08X\n", lcdc_read_reg(L3_XSIZE));
	LCDC_DUMP("L3_YSIZE=0x%08x\n", lcdc_read_reg(L3_YSIZE));
	LCDC_DUMP("L3_SKIP=0x%08x\n", lcdc_read_reg(L3_SKIP));
	LCDC_DUMP("L3_DMA_CTRL=0X%08X\n", lcdc_read_reg(L3_DMA_CTRL));
	LCDC_DUMP("L3_ALPHA=0X%08X\n", lcdc_read_reg(L3_ALPHA));
	LCDC_DUMP("L3_CKEYB_SRC=0x%08x\n", lcdc_read_reg(L3_CKEYB_SRC));
	LCDC_DUMP("L3_CKEYS_SRC=0X%08X\n", lcdc_read_reg(L3_CKEYS_SRC));
	LCDC_DUMP("L3_CKEYB_DST=0x%08x\n", lcdc_read_reg(L3_CKEYB_DST));
	LCDC_DUMP("L3_CKEYS_DST=0X%08X\n", lcdc_read_reg(L3_CKEYS_DST));
	LCDC_DUMP("L3_FIFO_CHK=0X%08X\n", lcdc_read_reg(L3_FIFO_CHK));
	LCDC_DUMP("L3_FIFO_STATUS=0x%08x\n", lcdc_read_reg(L3_FIFO_STATUS));


	if (lcdc_config.vpp_handle)
		vpp_ops.print_register();
}

static void *lcdc_load_vpp_ops(void)
{
	if (lcdc_config.vpp_handle)
		return &vpp_ops;
	else
		return NULL;
}

static int lcdc_get_chip_id(void)
{
	return LCDC_CHIP_V2;
}

static void lcdc_set_pixel_clk(u32 pixel_clk)
{
	u32 s0_osc_ratio;
	panel_info.ref_rate = refresh_rate(pixel_clk,
				panel_info.hsync_period,
				panel_info.vsync_period);

	s0_osc_ratio = lcdc_read_reg(S0_OSC_RATIO);
	s0_osc_ratio &= ~S0_OSC_DIV_RATIO_MASK;
	s0_osc_ratio |= S0_OSC_DIV_RATIO(panel_info.sys_clk / pixel_clk - 1);
	lcdc_write_reg(S0_OSC_RATIO, s0_osc_ratio);

	return;
}

static u32 lcdc_get_pixel_clk(void)
{
	return pixel_clock(panel_info.ref_rate,
			panel_info.hsync_period,
			panel_info.vsync_period);
}

void vdss_install_lcdc_ops(struct vdss_lcdc_ops *lcdc_ops)
{
	memset(lcdc_ops, 0, sizeof(*lcdc_ops));

	lcdc_ops->init = lcdc_init;
	lcdc_ops->terminate = lcdc_terminate;
	lcdc_ops->sleep = lcdc_sleep;
	lcdc_ops->wakeup = lcdc_wakeup;
	lcdc_ops->get_scanline = lcdc_get_scanline;
	lcdc_ops->wait_for_vblank = lcdc_wait_for_vblank;
	lcdc_ops->get_mode = lcdc_get_mode;
	lcdc_ops->get_video_mem = lcdc_get_video_mem;

	lcdc_ops->alloc_overlay = lcdc_alloc_overlay;
	lcdc_ops->free_overlay = lcdc_free_overlay;

	lcdc_ops->show_overlay = lcdc_show_overlay;
	lcdc_ops->set_parameters = lcdc_set_parameters;
	lcdc_ops->get_parameters = lcdc_get_parameters;
	lcdc_ops->hide_overlay = lcdc_hide_overlay;
	lcdc_ops->set_overlay_pos = lcdc_set_overlay_pos;
	lcdc_ops->pan_display = lcdc_pan_display;
	lcdc_ops->flip_overlay = lcdc_flip_overlay;

	lcdc_ops->set_global_alpha = lcdc_set_global_alpha;
	lcdc_ops->set_alpha_property = lcdc_set_alpha_property;
	lcdc_ops->set_src_ckey = lcdc_set_src_ckey;
	lcdc_ops->set_dst_ckey = lcdc_set_dst_ckey;
	lcdc_ops->set_toplayer = lcdc_set_toplayer;
	lcdc_ops->get_toplayer = lcdc_get_toplayer;

	lcdc_ops->enable_interrupt = lcdc_enable_interrupt;
	lcdc_ops->disable_interrupt = lcdc_disable_interrupt;
	lcdc_ops->clear_interrupt = lcdc_clear_interrupt;
	lcdc_ops->irq_detected = lcdc_irq_detected;

	lcdc_ops->set_cursor_shape = lcdc_set_cursor_shape;
	lcdc_ops->move_cursor = lcdc_move_cursor;
	lcdc_ops->set_cursor_rotate = lcdc_set_cursor_rotate;

	lcdc_ops->get_gamma_ramp = lcdc_get_gamma_ramp;
	lcdc_ops->set_gamma_ramp = lcdc_set_gamma_ramp;
	lcdc_ops->get_color_ctrl = lcdc_get_color_ctrl;
	lcdc_ops->set_color_ctrl = lcdc_set_color_ctrl;

	lcdc_ops->load_vpp_ops = lcdc_load_vpp_ops;

	lcdc_ops->print_register = lcdc_print_register;
	lcdc_ops->reset = lcdc_reset;
	lcdc_ops->output_ctrl = lcdc_output_ctrl;
	lcdc_ops->get_chip_id = lcdc_get_chip_id;
	lcdc_ops->set_pixel_clk = lcdc_set_pixel_clk;
	lcdc_ops->get_pixel_clk = lcdc_get_pixel_clk;

	lcdc_ops->change_mode = lcdc_change_mode;
}


