/*
 * CSR SiRFprima2 VIP host driver header
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef _SIRFSOC_VIP_H
#define _SIRFSOC_VIP_H

#include <linux/sched.h>
#include <linux/dmaengine.h>
#include <linux/pinctrl/consumer.h>
#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/videobuf-dma-contig.h>
#include <media/soc_camera.h>
#include <media/soc_mediabus.h>

#include "sirfsoc_decoder_op.h"
#include "CspCmnVip.h"

/* buffer for one video frame */
struct sirfsoc_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;
	const struct soc_camera_data_format        *fmt;
};

struct sirfsoc_camera_platform_data {
	int (*init) (struct device *);
	int (*power) (struct device *, int);
	int (*reset) (struct device *);
	int (*release) (struct device *);
	int vip_power_gpio;
	int power_gpio;
	int reset_gpio;

	unsigned long flags;
	unsigned long sirfsoc_cam_yuv_coef1;
	unsigned long sirfsoc_cam_yuv_coef2;
	unsigned long sirfsoc_cam_yuv_coef3;
	unsigned long sirfsoc_cam_yuv_offset;

	unsigned long sirfsoc_camera_pixel_shift:3;
	unsigned long sirfsoc_camera_pxclk_en:1;
	unsigned long sirfsoc_camera_hsync_en:1;
	unsigned long sirfsoc_camera_vsync_en:1;
	unsigned long sirfsoc_camera_ccir656_en:1;
};

struct sirfsoc_camera_dev {
	struct device		*dev;
	/* current active device (At a time sirfsoc is only supposed to
	* handle one device on its VIP interface.)
	*/
	struct soc_camera_device *icd;
	struct clk		*clk;
	struct pinctrl		*p;

	unsigned int		irq;
	void __iomem		*base;
	unsigned long		video_limit;

	struct dma_chan		*dma_chan;
	struct dma_interleaved_template *dma_xt;
	struct dma_slave_config dma_slave_config;
	struct sirfsoc_camera_platform_data *pdata;
	struct resource		*res;
	unsigned int		platform_flags;

	struct list_head	capture;

	spinlock_t		lock;

	struct videobuf_buffer	*active;

	VIP_FUNCTIONTABLE	vip_funcs;
	VIP_PARAMS              vip_params;

	struct sirfsoc_decoder_ops	*decoder_ops;
	/* Current active task which holds VIP hardware */
	struct task_struct      *task;

	/* callbacks filled by vip, rearview--->vip */
	void (*save_vip_context)(void *data);
	void (*restore_vip_context)(void *data);
};
#endif
