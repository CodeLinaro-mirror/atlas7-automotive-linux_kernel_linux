/*
 * CSR sirfsoc framebuffer internal interface
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
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


struct sirfsocfb_flipitem
{
	u32 base;
	int bufidx;
	u8 wait;
	u32 vsync_count;
};

struct sirfsocfb_overlay
{
	volatile u32 vsync_count;
	u32	 prev_count;

	u32	 hstride_byte;
	u32	 num_buffers;

	struct sirfsocfb_flipitem *pflips;

	int	 num_q; /* queued but not fliped */
	int	 num_dq; /* not wait or (queued & fliped)*/

	int	  dq_idx;
	int	  q_idx;
	int	  flip_idx;
};

struct layer_info {
	u8		registered;
	struct 	mutex layer_lock;
	struct completion done;
	u32		alpha;
	int		valid;
	int		waiting_to_pan;
	struct 	sirfsocfb_colorkeys ckey;
	int		fifo_underflow;
	int		fifo_overflow;
	u8		enabled;
	u8		queued;
	u8		valid_pos;
	enum sirfsocfb_feature_layer feature;

	struct sirfsocfb_overlay *pOvl;
};

struct overlay_pos {
	int	x;
	int	y;
	int	w;
	int	h;
};

struct sirfsocfb{
	struct fb_info		fb[SIRFSOCFB_MAX_LAYERS];
	struct layer_info	layer_info[SIRFSOCFB_MAX_LAYERS];
	struct overlay_pos	ovl_pos;
	struct mutex ovl_lock;
	struct clk 		*clk;
	void __iomem		*base;
	struct sirfsocfb_board	*board;
	struct sirfsocfb_panel	*panel;
	struct platform_device	*dev;
	int 			irq;
	spinlock_t 		lock;
	int			init_enabled;
	int			record_toplayer;

	LCD_FUNCTIONTABLE lcd_func;
	VPP_FUNCTIONTABLE *vpp_func;

	void __iomem *vpp_base;
	struct clk 		*vpp_clk;

#ifdef SUPPORT_BLE
	void __iomem *ble_base;
	struct clk *ble_clk;
	int ble_irq;
	void  *ble_mem_base;
	dma_addr_t ble_mem_offset;
	unsigned int ble_mem_size;
	BLE_FUNCTIONTABLE ble_func;
	void *ble_context;
#endif

	struct workqueue_struct	*flip_wq;
	struct work_struct work;
	ktime_t vsync_timestamp;
	struct work_struct vsync_work;
};
#endif
