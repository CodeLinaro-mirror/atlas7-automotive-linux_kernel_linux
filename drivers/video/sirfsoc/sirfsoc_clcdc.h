/*
 * CSR sirfsoc framebuffer internal interface
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __SIRFSOC_LCD_H
#define __SIRFSOC_LCD_H

#include <linux/fb.h>

#define PANEL_PCLK_POLAR	(1<<2)
#define PANEL_PCLK_EDGE		(1<<3)
#define PANEL_HSYNC_POLAR	(1<<5)
#define PANEL_VSYNC_POLAR	(1<<7)

#define DONT_PAN	0
#define WAIT_PAN	1


#define SIRFSOCFB_BYTES(bpp)	(bpp>>3)

/* Panel information */
struct sirfsocfb_panel {
	struct fb_videomode mode;
	unsigned int grayscale;
	unsigned int bpp;
	unsigned int timing;
	void (*enable_pre) (void);
	void (*enable_post) (void);
	void (*disable_pre) (void);
	void (*disable_post) (void);
};

enum output_format_t {
	OUTPUT_RGBRGB,
	OUTPUT_8YUV422,
	OUTPUT_YUV422,
	OUTPUT_RGB565,		/* This is relevant only for Atlas4 */
	OUTPUT_RGB666,		/* This is relevant only for Prima/Belmont */
	OUTPUT_RGB888		/* This is relevant only for Prima/Belmont */
};


struct sirfsocfb_flipitem {
	u32 base;
	int bufidx;
	u8 wait;
	u32 vsync_count;
};

struct sirfsocfb_overlay {
	u32	vsync_count;
	u32	prev_count;

	u32	hstride_byte;
	u32	num_buffers;

	struct sirfsocfb_flipitem *pflips;

	int	num_q; /* queued but not fliped */
	int	num_dq; /* not wait or (queued & fliped)*/

	int	dq_idx;
	int	q_idx;
	int	flip_idx;
};

struct layer_info {
	u8	registered;
	struct mutex layer_lock;
	struct completion done;
	u32	alpha;
	int	valid;
	int	waiting_to_pan;
	struct sirfsocfb_colorkeys ckey;
	int	fifo_underflow;
	int	fifo_overflow;
	u8	enabled;
	u8	queued;
	u8	valid_pos;
	enum sirfsocfb_feature_layer feature;

	struct sirfsocfb_overlay *ovl;
};

struct overlay_pos {
	int	x;
	int	y;
	int	w;
	int	h;
};

struct dma_buf {
	unsigned long   size;
	void            *base;
	unsigned long   dma_addr;
};

struct sirfsocfb {
	struct fb_info		fb[SIRFSOCFB_MAX_LAYERS];
	struct dma_buf		dma_buf[SIRFSOCFB_MAX_LAYERS];
	struct layer_info	layer_info[SIRFSOCFB_MAX_LAYERS];
	struct overlay_pos	ovl_pos;
	struct mutex ovl_lock;
	struct clk		*clk;
	void __iomem		*base;
	struct sirfsocfb_board	*board;
	struct sirfsocfb_panel	*panel;
	struct platform_device	*dev;
	int			irq;
	spinlock_t		lock;
	int			init_enabled;
	int			record_toplayer;

	struct vdss_lcdc_ops	lcdc_ops;
	struct vdss_vpp_ops	*vpp_ops;

	void __iomem		*vpp_base;
	struct clk		*vpp_clk;

#ifdef SUPPORT_BLE
	void __iomem *ble_base;
	struct clk *ble_clk;
	int ble_irq;
	void  *ble_mem_base;
	dma_addr_t ble_mem_offset;
	unsigned int ble_mem_size;
	struct vdss_ble_ops ble_func;
	void *ble_context;
#endif

	struct workqueue_struct	*flip_wq;
	struct work_struct work;
	ktime_t vsync_timestamp;
	struct work_struct vsync_work;
};

/* Version name */
#define SIRFSOCFB_MODULE_NAME		"SIRFSOC-FB"

/* debug feature defines */
#ifndef FB_DEBUG
#define FB_DEBUG			0
#endif

/* messages */
#ifdef pr_fmt
#undef pr_fmt
#endif
#ifdef SIRFSOCFB_MODULE_NAME
#define pr_fmt(fmt)	SIRFSOCFB_MODULE_NAME ": "fmt
#else
#define pr_fmt(fmt)	fmt
#endif
#define FB_ERR_MSG(fmt, args...)	pr_err(fmt, ## args)
#define FB_WRN_MSG(fmt, args...)	pr_warn(fmt, ## args)
#define FB_NOT_MSG(fmt, args...)	pr_notice(fmt, ## args)
#define FB_INF_MSG(fmt, args...)	pr_info(fmt, ## args)
#define FB_ASSERT(expr)			BUG_ON(!(expr))
#define FB_FUN_MSG(fmt, args...)

#if FB_DEBUG
#define FB_DBG_MSG(fmt, args...)	pr_info(fmt, ## args)
#else
#define FB_DBG_MSG(fmt, args...)
#endif

#endif
