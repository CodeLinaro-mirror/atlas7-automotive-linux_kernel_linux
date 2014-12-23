/*
 * CSR SiRFprima2 VIP host driver
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef _SIRFSOC_VOUT_H_

#define _SIRFSOC_VOUT_H_

#include <video/sirfsoc_vdss.h>


#define SIRFSOC_MAX_VOUT 2
#define SIRFSOC_MAX_DISPLAY 2


struct sirfsoc_vout_buf {
	struct vb2_buffer vb;
	struct list_head list;
};

struct sirfsoc_video_device {
	struct platform_device *pdev;
	struct v4l2_device v4l2_dev;
	struct sirfsoc_vout_device *vouts[SIRFSOC_MAX_VOUT];

	int num_panel;
	struct sirfsoc_vdss_panel *display[SIRFSOC_MAX_DISPLAY];
};

struct sirfsoc_vout_device {
	struct video_device *vd;
	struct sirfsoc_video_device *vid_dev;
	int opened;
	spinlock_t vbq_lock;
	struct mutex lock;
	struct sirfsoc_vout_buf *disp_buf;
	unsigned int numbuffers;

	struct v4l2_pix_format pix_fmt;

	enum v4l2_buf_type type;
	struct vb2_queue vb2_q;
	struct vb2_buffer *active_frm, *next_frm;
	/* allocator-specific contexts for each plane */
	struct vb2_alloc_ctx *alloc_ctx;
	struct list_head dma_queue;
	struct v4l2_rect rect;
	u32 surf_width;
	u32 surf_height;
	struct sirfsoc_vdss_panel *display;
	struct sirfsoc_vdss_layer *layer;
};


#endif
