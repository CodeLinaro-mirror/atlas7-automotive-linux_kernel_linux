/*
 * linux/include/video/sirfsoc_vdss.h
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

#ifndef __SIRFSOC_VDSS_H
#define __SIRFSOC_VDSS_H

#include <linux/list.h>
#include <linux/kobject.h>
#include <linux/device.h>
#include <linux/interrupt.h>

#include <video/videomode.h>

#define LCDC_INT_L0_DMA		BIT(0)
#define LCDC_INT_L1_DMA		BIT(1)
#define LCDC_INT_L2_DMA		BIT(2)
#define LCDC_INT_L3_DMA		BIT(3)
#define LCDC_INT_L0_OFLOW	BIT(6)
#define LCDC_INT_L1_OFLOW	BIT(7)
#define LCDC_INT_L2_OFLOW	BIT(8)
#define LCDC_INT_L3_OFLOW	BIT(9)
#define LCDC_INT_L0_UFLOW	BIT(12)
#define LCDC_INT_L1_UFLOW	BIT(13)
#define LCDC_INT_L2_UFLOW	BIT(14)
#define LCDC_INT_L3_UFLOW	BIT(15)
#define LCDC_INT_VSYNC		BIT(18)
#define LCDC_INT_ALL		0xFFFFFFFF

enum sirfsoc_panel_type {
	SIRFSOC_PANEL_NONE = 0x0,
	SIRFSOC_PANEL_RGB = 0x1,
	SIRFSOC_PANEL_HDMI = 0x2,
	SIRFSOC_PANEL_LVDS = 0x4,
};

enum vdss_output {
	SIRFSOC_VDSS_OUTPUT_RGB	= 1,
	SIRFSOC_VDSS_OUTPUT_LVDS1 = 2,
	SIRFSOC_VDSS_OUTPUT_LVDS2 = 4,
};

enum vdss_layer {
	SIRFSOC_VDSS_LAYER0	= 0,
	SIRFSOC_VDSS_LAYER1,
	SIRFSOC_VDSS_LAYER2,
	SIRFSOC_VDSS_LAYER3,
	SIRFSOC_VDSS_CURSOR	= 6,
};

enum vdss_screen {
	SIRFSOC_VDSS_SCREEN0,
	SIRFSOC_VDSS_SCREEN1,
};

enum vdss_lcdc {
	SIRFSOC_VDSS_LCDC0,
	SIRFSOC_VDSS_LCDC1,
};

enum vdss_lvdsc_mode {
	SIRFSOC_VDSS_LVDSC_MODE_NONE,
	SIRFSOC_VDSS_LVDSC_MODE_SLAVE,
	SIRFSOC_VDSS_LVDSC_MODE_SYN,
};

enum vdss_lvdsc_fmt {
	SIRFSOC_VDSS_LVDSC_FMT_NONE,
	SIRFSOC_VDSS_LVDSC_FMT_VESA_6BIT,
	SIRFSOC_VDSS_LVDSC_FMT_VESA_8BIT,
};

struct vdss_rect {
	int	left;
	int	top;
	int	right;
	int	bottom;
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

	/* YUV420, w-stride 64 aligned h-stride 16 aligned */
	VDSS_PIXELFORMAT_Q420 = 36,

	/* Y component range[0,255], planes exactly like NV12 */
	VDSS_PIXELFORMAT_NJ12 = 37,

	VDSS_PIXELFORMAT_CUSTOM = 0X1000
};

enum vdss_signal_level {
	SIRFSOC_VDSS_SIG_ACTIVE_LOW,
	SIRFSOC_VDSS_SIG_ACTIVE_HIGH
};

enum vdss_signal_edge {
	SIRFSOC_VDSS_SIG_FALLING_EDGE,
	SIRFSOC_VDSS_SIG_RISING_EDGE
};

enum vdss_panel_state {
	SIRFSOC_VDSS_PANEL_DISABLED,
	SIRFSOC_VDSS_PANEL_ENABLED,
};

#define VPP_TO_LCD_BPP 4
#define VPP_TO_LCD_CTRL_VPP E_LO_CTRL_BPP_RGB888
#define VPP_TO_LCD_PIXELFORMAT VDSS_PIXELFORMAT_BGRX_8880

#define VDSS_VPP_MASK	0xf
#define VDSS_VPP_BLT	0x01
#define VDSS_VPP_UPDATE_SRCBASE 0x02
#define VDSS_VPP_COLOR_CTRL 0x04

enum vdss_deinterlace_mode {
	VDSS_VPP_DI_RESERVED = 0,
	VDSS_VPP_DI_WEAVE,
	VDSS_VPP_3MEDIAN,
	VDSS_VPP_DI_VMRI,
};

enum vdss_vpp_output_mode {
	VDSS_P_SINGLE = 0,
	VDSS_INTERLACE,
	VDSS_P_DOUBLE,
};

enum vdss_field {
	VDSS_FIELD_NONE = 0,
	VDSS_FIELD_TOP,
	VDSS_FIELD_BOTTOM,
	VDSS_FIELD_INTERLACED,
	VDSS_FIELD_SEQ_TB,
	VDSS_FIELD_SEQ_BT,
	VDSS_FIELD_INTERLACED_TB,
	VDSS_FIELD_INTERLACED_BT,
	VDSS_FRAME_TOP,
	VDSS_FRAME_BOTTOM,
};

struct vdss_vpp_interlace {
	bool di_top;
	enum vdss_deinterlace_mode di_mode;
};

struct vdss_surface {
	enum vdss_pixelformat fmt;
	enum vdss_field field;
	u32 width;
	u32 height;
	u32 base;
};

struct vdss_vpp_colorctrl {
	s16 hue;
	s16 brightness;
	s16 contrast;
	s16 saturation;
};

enum vdss_vpp {
	SIRFSOC_VDSS_VPP0 = 0,
	SIRFSOC_VDSS_VPP1,
};

enum vdss_vip_ext {
	SIRFSOC_VDSS_VIP0_EXT = 0,
	SIRFSOC_VDSS_VIP1_EXT,
};

enum vdss_vpp_op_type {
	VPP_OP_IDEL = 0,
	VPP_OP_BITBLT,
	VPP_OP_INLINE,
	VPP_OP_PASS_THROUGH,
	VPP_OP_IBV,
};

struct vdss_vpp_blt_params {
	struct vdss_surface src_surf;
	struct vdss_rect src_rect;
	struct vdss_vpp_interlace interlace;
	struct vdss_surface dst_surf[2];
	struct vdss_rect dst_rect;
	struct vdss_vpp_colorctrl color_ctrl;
};

struct vdss_vpp_inline_params {
	struct vdss_surface src_surf;
	struct vdss_rect src_rect;
	struct vdss_rect dst_rect;
	struct vdss_vpp_colorctrl color_ctrl;
};

struct vdss_vpp_passthrough_params {
	struct vdss_surface src_surf;
	struct vdss_vpp_interlace interlace;
	struct vdss_rect src_rect;
	struct vdss_rect dst_rect;
	struct vdss_vpp_colorctrl color_ctrl;
	bool flip;
};

struct vdss_vpp_ibv_params {
	enum vdss_vip_ext src_id;
	struct vdss_surface src_surf[3];
	u32 src_size;
	struct vdss_vpp_interlace interlace;
	struct vdss_rect src_rect;
	struct vdss_rect dst_rect;
	struct vdss_vpp_colorctrl color_ctrl;
	bool color_update_only;
};

struct vdss_vpp_op_params {
	enum vdss_vpp_op_type type;
	union {
		struct vdss_vpp_blt_params blt;
		struct vdss_vpp_inline_params inline_mode;
		struct vdss_vpp_passthrough_params passthrough;
		struct vdss_vpp_ibv_params ibv;
	} op;
};

typedef void (*sirfsoc_vpp_notify_t)(void *arg,
				enum vdss_vpp id,
				enum vdss_vpp_op_type type);

typedef int (*sirfsoc_layer_notify_t)(void *arg, bool enable);

struct vdss_vpp_create_device_params {
	sirfsoc_vpp_notify_t func;
	void *arg;
};

enum vdss_dcu_op_type {
	DCU_OP_BITBLT = 0,
	DCU_OP_INLINE,
};

struct vdss_dcu_inline_params {
	struct vdss_surface src_surf[2];
	struct vdss_rect src_rect;
	struct vdss_rect dst_rect;
	bool flip;
};

struct vdss_dcu_blt_params {
	struct vdss_surface src_surf[2];
	struct vdss_surface dst_surf;
	struct vdss_rect src_rect;
	struct vdss_rect dst_rect;
};

struct vdss_dcu_op_params {
	enum vdss_dcu_op_type type;
	union {
		struct vdss_dcu_blt_params blt;
		struct vdss_dcu_inline_params inline_mode;
	} op;
};

enum vdss_disp_mode {
	VDSS_DISP_NORMAL = 0,
	VDSS_DISP_INLINE,
	VDSS_DISP_PASS_THROUGH,
	VDSS_DISP_IBV,
};

struct sirfsoc_vdss_screen;
struct sirfsoc_vdss_panel;
struct sirfsoc_vdss_output;

struct sirfsoc_video_timings {
	/* Unit: pixels */
	u16 xres;
	/* Unit: pixels */
	u16 yres;
	/* Unit: KHz */
	u32 pixel_clock;
	/* Unit: pixel clocks */
	u16 hsw;	/* Horizontal synchronization pulse width */
	/* Unit: pixel clocks */
	u16 hfp;	/* Horizontal front porch */
	/* Unit: pixel clocks */
	u16 hbp;	/* Horizontal back porch */
	/* Unit: line clocks */
	u16 vsw;	/* Vertical synchronization pulse width */
	/* Unit: line clocks */
	u16 vfp;	/* Vertical front porch */
	/* Unit: line clocks */
	u16 vbp;	/* Vertical back porch */

	/* Vsync logic level */
	enum vdss_signal_level vsync_level;
	/* Hsync logic level */
	enum vdss_signal_level hsync_level;
	/* Interlaced or Progressive timings */
	bool interlace;
	/* Pixel clock edge to drive LCD data */
	enum vdss_signal_edge pclk_edge;
	/* Data enable logic level */
	enum vdss_signal_level de_level;
};

struct sirfsoc_vdss_layer_info {
	struct vdss_surface src_surf;
	struct vdss_rect src_rect;	/* source rect offset */
	struct vdss_rect dst_rect;	/* destination rect offset */
	u32 line_skip;

	bool ckey_on;
	u32 ckey;

	bool dst_ckey_on;
	u32 dst_ckey;

	bool global_alpha;
	u8 alpha;
	bool pre_mult_alpha;
	bool source_alpha;
	enum vdss_disp_mode disp_mode;
};

struct sirfsoc_vdss_layer {
	struct list_head list;

	/* static fields */
	const char *name;
	enum vdss_layer id;
	enum vdss_pixelformat supported_fmts;
	int caps;
	struct kobject kobj;

	/* dynamic fields */
	struct sirfsoc_vdss_screen *screen;
	/* the lcd the layer belongs to */
	enum vdss_lcdc lcdc_id;

	/*
	 * The following functions do not block:
	 *
	 * is_enabled
	 * set_overlay_info
	 * get_overlay_info
	 *
	 * The rest of the functions may block and cannot be called from
	 * interrupt context
	 */

	int (*enable)(struct sirfsoc_vdss_layer *layer);
	int (*disable)(struct sirfsoc_vdss_layer *layer);
	bool (*is_enabled)(struct sirfsoc_vdss_layer *layer);
	int (*register_notify)(struct sirfsoc_vdss_layer *layer,
		sirfsoc_layer_notify_t func, void *arg);

	bool (*is_preempted)(struct sirfsoc_vdss_layer *layer);

	int (*set_screen)(struct sirfsoc_vdss_layer *layer,
		struct sirfsoc_vdss_screen *screen);
	int (*unset_screen)(struct sirfsoc_vdss_layer *layer);

	int (*set_info)(struct sirfsoc_vdss_layer *layer,
		struct sirfsoc_vdss_layer_info *info);
	void (*get_info)(struct sirfsoc_vdss_layer *layer,
		struct sirfsoc_vdss_layer_info *info);
	struct sirfsoc_vdss_panel *(*get_panel)(
		struct sirfsoc_vdss_layer *layer);
	void (*flip)(struct sirfsoc_vdss_layer *layer, u32 srcbase);
};

struct sirfsoc_vdss_screen_info {
	enum vdss_layer top_layer;
	u32 blank_color;
	u32 back_color;
};

struct sirfsoc_vdss_screen {
	/* static fields */
	const char *name;
	enum vdss_screen id;
	struct list_head layers;
	int caps;
	struct kobject kobj;
	enum sirfsoc_panel_type supported_panels;
	enum vdss_output supported_outputs;

	/* dynamic fields */
	struct sirfsoc_vdss_output *output;
	enum vdss_lcdc lcdc_id;

	int (*set_output)(struct sirfsoc_vdss_screen *screen,
		struct sirfsoc_vdss_output *output);
	int (*unset_output)(struct sirfsoc_vdss_screen *screen);

	int (*set_info)(struct sirfsoc_vdss_screen *screen,
			struct sirfsoc_vdss_screen_info *info);
	void (*get_info)(struct sirfsoc_vdss_screen *screen,
			struct sirfsoc_vdss_screen_info *info);

	int (*apply)(struct sirfsoc_vdss_screen *screen);
	int (*wait_for_vsync)(struct sirfsoc_vdss_screen *screen);

	int (*set_gamma)(struct sirfsoc_vdss_screen *screen,
		const u8 *gamma);
	int (*get_gamma)(struct sirfsoc_vdss_screen *screen,
		u8 *gamma);
	void (*set_err_diff)(struct sirfsoc_vdss_screen *screen,
		bool error_diffusion);

	struct sirfsoc_vdss_panel *(*get_panel)(
		struct sirfsoc_vdss_screen *screen);
};

struct sirfsoc_vdss_rgb_ops {
	int (*connect)(struct sirfsoc_vdss_output *out,
		struct sirfsoc_vdss_panel *panel);
	void (*disconnect)(struct sirfsoc_vdss_output *out,
		struct sirfsoc_vdss_panel *panel);

	int (*enable)(struct sirfsoc_vdss_output *out);
	void (*disable)(struct sirfsoc_vdss_output *out);

	int (*check_timings)(struct sirfsoc_vdss_output *out,
			struct sirfsoc_video_timings *timings);
	void (*set_timings)(struct sirfsoc_vdss_output *out,
			struct sirfsoc_video_timings *timings);
	void (*get_timings)(struct sirfsoc_vdss_output *out,
			struct sirfsoc_video_timings *timings);

	void (*set_data_lines)(struct sirfsoc_vdss_output *out,
		int data_lines);
};

struct sirfsoc_vdss_lvds_ops {
	int (*connect)(struct sirfsoc_vdss_output *out,
		struct sirfsoc_vdss_panel *panel);
	void (*disconnect)(struct sirfsoc_vdss_output *out,
	struct sirfsoc_vdss_panel *panel);

	int (*enable)(struct sirfsoc_vdss_output *out);
	void (*disable)(struct sirfsoc_vdss_output *out);

	int (*check_timings)(struct sirfsoc_vdss_output *out,
		struct sirfsoc_video_timings *timings);
	void (*set_timings)(struct sirfsoc_vdss_output *out,
		struct sirfsoc_video_timings *timings);

	void (*get_timings)(struct sirfsoc_vdss_output *out,
	struct sirfsoc_video_timings *timings);

	void (*set_data_lines)(struct sirfsoc_vdss_output *out,
			int data_lines);
	void (*set_fmt)(struct sirfsoc_vdss_output *out,
		enum vdss_lvdsc_fmt fmt);
	void (*set_mode)(struct sirfsoc_vdss_output *out,
		enum vdss_lvdsc_mode mode);
};

struct sirfsoc_vdss_panel {
	struct device *dev;

	struct module *owner;

	struct list_head list;

	/*
	 * alias in the form of "display%d", primary or secondary
	 * display will be choosed base on it.
	 */
	char alias[16];

	enum sirfsoc_panel_type type;

	union {
		struct {
			u8 data_lines;
		} rgb;

		struct {
			u8 data_lines;
		} lvds;
	} phy;

	const char *name;

	struct sirfsoc_video_timings timings;

	struct sirfsoc_vdss_driver *driver;

	struct sirfsoc_vdss_output *src;

	enum vdss_panel_state state;
	/* helper variable for driver suspend/resume */
	bool activate_after_resume;
};

struct sirfsoc_vdss_output {
	struct device *dev;

	struct module *owner;

	struct list_head list;
	const char *name;
	union {
		const struct sirfsoc_vdss_lvds_ops *lvds;
		const struct sirfsoc_vdss_rgb_ops *rgb;
	} ops;

	/* panel type supported by the output */
	int supported_panel;

	/* lcd for this output */
	enum vdss_lcdc lcdc_id;

	/* screen in the lcd for this output */
	enum vdss_screen screen_id;

	/* output instance */
	enum vdss_output id;

	/* dynamic fields */
	struct sirfsoc_vdss_screen *screen;

	struct sirfsoc_vdss_panel *dst;
};
struct sirfsoc_vdss_driver {
	int (*probe)(struct sirfsoc_vdss_panel *panel);
	void (*remove)(struct sirfsoc_vdss_panel *panel);

	int (*connect)(struct sirfsoc_vdss_panel *panel);
	void (*disconnect)(struct sirfsoc_vdss_panel *panel);

	int (*enable)(struct sirfsoc_vdss_panel *panel);
	void (*disable)(struct sirfsoc_vdss_panel *panel);


	void (*get_resolution)(struct sirfsoc_vdss_panel *panel,
		u16 *xres, u16 *yres);
	void (*get_dimensions)(struct sirfsoc_vdss_panel *panel,
		u32 *width, u32 *height);
	int (*get_recommended_bpp)(struct sirfsoc_vdss_panel *panel);

	int (*check_timings)(struct sirfsoc_vdss_panel *panel,
			struct sirfsoc_video_timings *timings);
	void (*set_timings)(struct sirfsoc_vdss_panel *panel,
			struct sirfsoc_video_timings *timings);
	void (*get_timings)(struct sirfsoc_vdss_panel *panel,
			struct sirfsoc_video_timings *timings);
};

bool sirfsoc_vdss_is_initialized(void);
bool sirfsoc_vdss_lvds_is_initialized(void);
struct sirfsoc_vdss_panel *sirfsoc_vdss_get_primary_device(void);
struct sirfsoc_vdss_panel *sirfsoc_vdss_get_secondary_device(void);

int sirfsoc_vdss_register_panel(struct sirfsoc_vdss_panel *panel);
void sirfsoc_vdss_unregister_panel(struct sirfsoc_vdss_panel *panel);
struct sirfsoc_vdss_panel *sirfsoc_vdss_get_panel(
	struct sirfsoc_vdss_panel *panel);
void sirfsoc_vdss_put_panel(struct sirfsoc_vdss_panel *panel);
struct sirfsoc_vdss_panel *sirfsoc_vdss_find_panel(void *data,
	int (*match)(struct sirfsoc_vdss_panel *panel, void *data));
#define for_each_vdss_panel(p) \
	for (p = sirfsoc_vdss_get_next_panel(p); p != NULL; \
		p = sirfsoc_vdss_get_next_panel(p))
struct sirfsoc_vdss_panel *sirfsoc_vdss_get_next_panel(
	struct sirfsoc_vdss_panel *from);
void videomode_to_sirfsoc_video_timings(const struct videomode *vm,
	struct sirfsoc_video_timings *ovt);
void sirfsoc_video_timings_to_videomode(
	const struct sirfsoc_video_timings *timings,
	struct videomode *vm);

int sirfsoc_vdss_output_set_panel(struct sirfsoc_vdss_output *out,
	struct sirfsoc_vdss_panel *panel);
int sirfsoc_vdss_output_unset_panel(struct sirfsoc_vdss_output *out);
int sirfsoc_vdss_register_output(struct sirfsoc_vdss_output *out);
void sirfsoc_vdss_unregister_output(struct sirfsoc_vdss_output *out);
struct sirfsoc_vdss_output *sirfsoc_vdss_get_output(enum vdss_output id);
struct sirfsoc_vdss_output *sirfsoc_vdss_find_output(const char *name);
struct sirfsoc_vdss_output *sirfsoc_vdss_find_output_from_panel(
	struct sirfsoc_vdss_panel *panel);
struct sirfsoc_vdss_screen *sirfsoc_vdss_find_screen_from_panel
	(struct sirfsoc_vdss_panel *panel);

int sirfsoc_vdss_panel_enable_encoder(void);
int sirfsoc_vdss_panel_disable_encoder(void);
bool sirfsoc_vdss_panel_find_encoder(void);

int sirfsoc_vdss_get_num_lcdc(void);
int sirfsoc_vdss_get_num_screens(u32 lcdc_index);
struct sirfsoc_vdss_screen *sirfsoc_vdss_get_screen(u32 lcdc_index, int num);
int sirfsoc_vdss_get_num_layers(u32 lcdc_index);
struct sirfsoc_vdss_layer *sirfsoc_vdss_get_layer(u32 lcdc_index, int num);
struct sirfsoc_vdss_layer *sirfsoc_vdss_get_layer_from_screen(
	struct sirfsoc_vdss_screen *scn, enum vdss_layer id, bool rearview);
void sirfsoc_vdss_set_exclusive_layers(struct sirfsoc_vdss_layer **layers,
				u32 size, bool enable);
bool sirfsoc_vdss_check_size(
	enum vdss_disp_mode disp_mode,
	struct vdss_surface *src_surf,
	struct vdss_rect *src_rect,
	int *psrc_skip,
	struct sirfsoc_vdss_layer *l,
	struct vdss_rect *dst_rect,
	int *pdst_skip);

typedef void (*sirfsoc_lcdc_isr_t) (void *arg, u32 mask);
int sirfsoc_lcdc_register_isr(u32 lcdc_index, sirfsoc_lcdc_isr_t isr,
	void *arg, u32 mask);
int sirfsoc_lcdc_unregister_isr(u32 lcdc_index, sirfsoc_lcdc_isr_t isr,
	void *arg, u32 mask);

/* vpp functions*/
bool sirfsoc_vpp_is_passthrough_support(enum vdss_pixelformat fmt);
void *sirfsoc_vpp_create_device(enum vdss_vpp id,
				struct vdss_vpp_create_device_params *params);
int sirfsoc_vpp_destroy_device(void *handle);
int sirfsoc_vpp_present(void *handle, struct vdss_vpp_op_params *params);

/* dcu functions */
int sirfsoc_dcu_reset(void);
int sirfsoc_dcu_present(struct vdss_dcu_op_params *params);
bool sirfsoc_dcu_is_inline_support(enum vdss_pixelformat fmt,
	enum vdss_field field);

static inline bool sirfsoc_vdss_panel_is_connected(
		struct sirfsoc_vdss_panel *panel)
{
	return panel->src;
}

static inline bool sirfsoc_vdss_panel_is_enabled(
		struct sirfsoc_vdss_panel *panel)
{
	return panel->state == SIRFSOC_VDSS_PANEL_ENABLED;
}

#endif
