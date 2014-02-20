/*
 * CSR SiRFprima2 Rearview header
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef _REARVIEW_H
#define _REARVIEW_H

#include <linux/types.h>
#include <linux/fb.h>
#include <linux/spinlock.h>
#include <linux/gpio.h>
#include <linux/atomic.h>

#include <video/sirfsoc_fb.h>

#include "CspCmnVip.h"
#include "sirfsocvip.h"

#define SRC_WIDTH	720
#define SRC_HEIGHT	480

#define SRC_PXLFORMAT	LCD_PIXELFORMAT_UYVY

struct rearview_setting  {
	/* vip */
	VIP_PARAMS		vip_params;
	VIP_FUNCTIONTABLE	*vip_funcs;
	void			*base;
	u32			vip_irq;
	struct clk		*vip_clk;
	struct device		*vip_dev;

	struct dma_chan		*dma_chan;
	unsigned long		dma_addr;	/* dma address */
	u32			dma_buf_span;   /* dma buf size per field */

	unsigned		gpio;
	unsigned		irq;

	/* tvdecoder */
	struct sirfsoc_decoder_ops	*decoder_ops;

	/* vpp & lcd */
	struct fb_info		*fbi;
	unsigned long		fb_addr;
	int			fb_opened;

#ifdef REARVIEW_AUXILIARY
	struct fb_info		*aux_fbi;
	unsigned long		aux_fb_addr;
#endif

	void (*save_vip_context)(void *data);	/* callback save context */
	void (*restore_vip_context)(void *data);/* callback restore context */
	void			*data;		/* callback parameters */

	struct sirfsoc_camera_platform_data	*pdata;
};

struct rearview_state {
	int		active;
	spinlock_t	lock;
};


extern int rearview_thread(void *data);
extern int sirfsocfb_km_blt_yuv2rgb(struct fb_info *info,
	struct sirfsocfb_bltparms *parms);
extern int sirfsocfb_km_enable_feature_layer(struct fb_info *info,
	enum sirfsocfb_feature_layer feature);
extern int sirfsocfb_km_disable_feature_layer(struct fb_info *info);
#endif
