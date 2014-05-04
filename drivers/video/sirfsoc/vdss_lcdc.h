/*
 * CSR sirfsoc LCD library interface
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __SIRFSOC_VDSS_LCDC_H
#define __SIRFSOC_VDSS_LCDC_H

struct vdss_rect {
	int	left;
	int	top;
	int	right;
	int	bottom;
};

enum lcdc_layer {
	LCDC_PRIMARY = 0,
	LCDC_OVERLAY_1 = 1,
	LCDC_OVERLAY_2 = 2,
	LCDC_OVERLAY_3 = 3,
	LCDC_CURSOR = 6,
	LCDC_LAYER_UNKNOWN = 0xffffffff,
};

enum lcdc_cursor_mode {
	LCDC_CURSOR_MODE_32x32x2_2_T	= 0,
	LCDC_CURSOR_MODE_32x32x2_4	= 1,
	LCDC_CURSOR_MODE_32x32x2_3_T	= 2,
	LCDC_CURSOR_MODE_64x64x2_2_T	= 4,
	LCDC_CURSOR_MODE_64x64x2_4	= 5,
	LCDC_CURSOR_MODE_64x64x2_3_T	= 6,
};

enum vdss_pixelformat {
	VDSS_PIXELFORMAT_UNKNOWN = 0,

	/* RGB format goes here */
	VDSS_PIXELFORMAT_1BPP = 1,
	VDSS_PIXELFORMAT_2BPP = 2,
	VDSS_PIXELFORMAT_4BPP = 3,
	VDSS_PIXELFORMAT_8BPP = 4,

	VDSS_PIXELFORMAT_565 = 5,
	VDSS_PIXELFORMAT_5551 = 6,
	VDSS_PIXELFORMAT_4444 = 7,
	VDSS_PIXELFORMAT_5550 = 8,
	VDSS_PIXELFORMAT_BGRX_8880 = 9,
	VDSS_PIXELFORMAT_8888 = 10,

	VDSS_PIXELFORMAT_556 = 11,
	VDSS_PIXELFORMAT_655 = 12,
	VDSS_PIXELFORMAT_RGBX_8880 = 13,	/* R8G8B8 format */
	VDSS_PIXELFORMAT_666 = 14,		/* CSR only */

	VDSS_PIXELFORMAT_15BPPGENERIC = 15,	/* some generic types */
	VDSS_PIXELFORMAT_16BPPGENERIC = 16,
	VDSS_PIXELFORMAT_24BPPGENERIC = 17,
	VDSS_PIXELFORMAT_32BPPGENERIC = 18,

	/* FOURCC format goes here */
	VDSS_PIXELFORMAT_UYVY = 19,
	VDSS_PIXELFORMAT_UYNV = 20,
	VDSS_PIXELFORMAT_YUY2 = 21,
	VDSS_PIXELFORMAT_YUYV = 22,
	VDSS_PIXELFORMAT_YUNV = 23,
	VDSS_PIXELFORMAT_YVYU = 24,
	VDSS_PIXELFORMAT_VYUY = 25,

	VDSS_PIXELFORMAT_IMC2 = 26,		/* 4:2:0 planar YUV formats */
	VDSS_PIXELFORMAT_YV12 = 27,
	VDSS_PIXELFORMAT_I420 = 28,

	VDSS_PIXELFORMAT_IMC1 = 29,
	VDSS_PIXELFORMAT_IMC3 = 30,
	VDSS_PIXELFORMAT_IMC4 = 31,
	VDSS_PIXELFORMAT_NV12 = 32,
	VDSS_PIXELFORMAT_NV21 = 33,
	VDSS_PIXELFORMAT_UYVI = 34,
	VDSS_PIXELFORMAT_VLVQ = 35,

	VDSS_PIXELFORMAT_CUSTOMFORMAT = 0X1000
};

enum lcdc_out_format {
	LCDC_OUT_8_BIT_RBGRBG = 0,
	LCDC_OUT_8_BIT_YUV422 = 1,
	LCDC_OUT_16BIT_YUV422 = 2,
	LCDC_OUT_18BIT_RBG666 = 3,
	LCDC_OUT_24BIT_RBG888 = 4
};

enum lcdc_chip_id {
	LCDC_CHIP_V1 = 1,
	LCDC_CHIP_V2 = 2,
	LCDC_CHIP_ROM = 3,
};

struct lcdc_wait_for_vblank {
	bool block_begin;		/* IN: Returns when the vertical-blank
					       interval begins */
};

struct lcdc_scanline {
	u32 *scanline;			/* OUT: line number */
};

struct lcdc_mode {
	enum vdss_pixelformat fmt;	/* OUT: pixel format type */
	u32 stride;			/* OUT: byte stride */
	u32 width;			/* OUT: width */
	u32 height;			/* OUT: height */
	u32 ref_rate;			/* OUT: refresh rate of the display */
};

struct lcdc_video_mem {
	u32 size;			/* OUT: reserved size for display */
	u32 primary_size;		/* OUT: primary framebuffer size */
	u32 phy_base;			/* OUT: physical base address */
	u32 virt_base;			/* OUT: virtual base address */
};

struct lcdc_cursor_shape {
	u16 width;			/* IN: width */
	u16 height;			/* IN: height */
	s16 xhot;			/* IN: x coord of hot spot */
	s16 yhot;			/* IN: y coord of hot spot */

	void *mask;			/* IN: and/xor cpu virtual address */
	s16 mask_stride;		/* IN: and/xor stride */

	void *color;			/* IN: color surface virtual address */
	s16 color_stride;		/* IN: color surface stride*/
	enum vdss_pixelformat fmt;	/* IN: color surface format */
};

struct lcdc_cursor_info {
	s16 xpos;			/* IN: X position */
	s16 ypos;			/* IN: Y position */

	u32 rotation;			/* IN: rotation mode (0,90,180,270) */
	struct lcdc_cursor_shape cursor_shape;
};


struct lcdc_parms {
	enum vdss_pixelformat fmt;	/* IN/OUT: surface format */
	enum lcdc_layer layer;		/* IN/OUT: layer index*/
	struct vdss_rect src_rect;	/* IN: source rect offset */
	struct vdss_rect dst_rect;	/* IN: destination rect offset */
	int surf_width;			/* IN/OUT: surface width/stride */
	int surf_height;		/* IN/OUT: surface height */

	bool ckey_on;			/* IN: if color key enable */
	u32 ckey_high;			/* IN: high color key */
	u32 ckey_low;			/* IN: low color key */
	u32 base;			/* IN: physical base address */
	bool g_alpha_enabled;		/* IN: if global alpha enabled */
	u8 alpha;			/* IN: alpha value */
	bool dst_ckey_on;
	u32 dst_ckey_high;		/* IN: high color key */
	u32 dst_ckey_low;		/* IN: low color key */
	bool src_alpha_enabled;		/* IN: if source alpha enabled */
	bool pre_alpha_enabled;		/* IN: if premulti alpha enabled */
};

struct lcdc_overlay {
	enum vdss_pixelformat fmt;	/* IN/OUT: surface format */
	enum lcdc_layer layer;		/* IN: layer to be allocated */
	int width;			/* IN/OUT: surface width */
	int height;			/* IN/OUT: surface height */
	int wstride_pixel;
	int hstride_pixel;
	int wstride_byte;
	int hstride_byte;
};

enum lcdc_flip_mode {
	LCDC_FLIP_FRAME = 0,
	LCDC_FLIP_TOP_FIELD,
	LCDC_FLIP_BOTTOM_FIELD,
};

enum lcdc_interrupt_type {
	LCDC_INTERRUPT_L0_DMA = 0,
	LCDC_INTERRUPT_L1_DMA,
	LCDC_INTERRUPT_L2_DMA,
	LCDC_INTERRUPT_L3_DMA,
	LCDC_INTERRUPT_L0_OFLOW = 6,
	LCDC_INTERRUPT_L1_OFLOW,
	LCDC_INTERRUPT_L2_OFLOW,
	LCDC_INTERRUPT_L3_OFLOW,
	LCDC_INTERRUPT_L0_UFLOW = 12,
	LCDC_INTERRUPT_L1_UFLOW,
	LCDC_INTERRUPT_L2_UFLOW,
	LCDC_INTERRUPT_L3_UFLOW,
	LCDC_INTERRUPT_VSYNC = 18,
	LCDC_INTERRUPT_ALL = 0xFFFFFFFF
};


#define LCDC_COLORCONTROL_BRIGHTNESS	1
#define LCDC_COLORCONTROL_CONTRAST	2
#define LCDC_COLORCONTROL_HUE		4
#define LCDC_COLORCONTROL_SATURATION	8

struct lcdc_color_ctrl {
	u32 flags;
	int brightness;
	int contrast;
	int hue;
	int saturation;
};

#define RGB_SEQ_RGB	0x186
#define RGB_SEQ_BGR	0x924
#define RGB_SEQ_BRG	0x861

struct lcdc_panel_info {
	u32 hsync_period;
	u32 hsync_width;
	u32 vsync_period;
	u32 vsync_width;

	u32 hstart;
	u32 hend;
	u32 vstart;
	u32 vend;

	enum lcdc_out_format out_fmt;
	u32 rgb_sequence;

	bool pclk_polar;
	bool pclk_edge;
	bool hsync_polar;
	bool vsync_polar;
	bool iomaster;
	u32 hsync_delay;

	u32 sys_clk;
	u32 ref_rate;

	enum lcdc_layer maxlayer;
	enum lcdc_layer layer;		/* Current primary layer */

	void (*pre_power_up) (void);
	void (*post_power_up) (void);
	void (*pre_power_down) (void);
	void (*post_power_down) (void);
	void (*reset) (void);
};


struct vdss_lcdc_ops {
	bool	(*init)			(void *regs, void *vpp_regs,
					u32 prim_base, unsigned int bpp,
					struct lcdc_panel_info *panel);
	void	(*terminate)		(void);
	void	(*sleep)		(void);
	bool	(*wakeup)		(void);
	void	(*get_scanline)		(struct lcdc_scanline *data);
	void	(*wait_for_vblank)	(struct lcdc_wait_for_vblank *data);
	void	(*get_mode)		(struct lcdc_mode *disp_mode);
	void	(*get_video_mem)	(struct lcdc_video_mem *data);

	int	(*alloc_overlay)	(struct lcdc_overlay *data);
	void	(*free_overlay)		(int layer);

	bool	(*set_parameters)	(struct lcdc_parms *parms);
	void	(*get_parameters)	(struct lcdc_parms *parms);
	void	(*show_overlay)		(int layer);
	void	(*hide_overlay)		(int layer);
	void	(*set_overlay_pos)	(int layer, struct vdss_rect *src_rect,
					struct vdss_rect *dst_rect);
	void	(*pan_display)		(int layer, int x, int y);

	void	(*set_global_alpha)	(int layer, unsigned char alpha);
	void	(*set_alpha_property)	(int layer, bool premulti,
					bool global, bool source);
	void	(*set_src_ckey)		(int layer, bool on, u32 high, u32 low);
	void	(*set_dst_ckey)		(int layer, bool on, u32 high, u32 low);
	void	(*set_toplayer)		(int layer);
	int	(*get_toplayer)		(void);

	void	(*flip_overlay)		(int layer, u32 base,
					enum lcdc_flip_mode field);

	void	(*enable_interrupt)	(enum lcdc_interrupt_type type);
	void	(*disable_interrupt)	(enum lcdc_interrupt_type type);
	void	(*clear_interrupt)	(enum lcdc_interrupt_type type);
	u32	(*irq_detected)		(enum lcdc_interrupt_type type);

	void	(*set_cursor_shape)	(u32 *mask, int mask_stride,
					u32 *color, int xhot, int yhot,
					int width, int height);
	void	(*move_cursor)		(int xpos, int ypos);
	void	(*set_cursor_rotate)	(int angle);

	void	(*get_gamma_ramp)	(u16 *gamma);
	void	(*set_gamma_ramp)	(u16 *gamma);
	void	(*get_color_ctrl)	(int layer,
					struct lcdc_color_ctrl *data);
	void	(*set_color_ctrl)	(int layer,
					struct lcdc_color_ctrl *data);

	void *	(*load_vpp_ops)		(void);

	void	(*print_register)	(void);
	void	(*reset)		(void);
	void	(*output_ctrl)		(bool turnoff);
	int	(*get_chip_id)		(void);
	void	(*set_pixel_clk)	(unsigned int pix_clk);
	u32	(*get_pixel_clk)	(void);

	bool	(*change_mode)		(struct lcdc_panel_info *panel);
};


void vdss_install_lcdc_ops(struct vdss_lcdc_ops *lcdc_ops);


#define LCDC_ERR(fmt, ...)	pr_err(fmt, ## __VA_ARGS__)
#define LCDC_DEBUG(fmt, ...)	pr_debug(fmt, ## __VA_ARGS__)
#define LCDC_ENTRY(fmt, ...)
#define LCDC_DUMP(fmt, ...)	pr_info(fmt, ## __VA_ARGS__)


#endif
