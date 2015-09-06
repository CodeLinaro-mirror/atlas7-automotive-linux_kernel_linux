/*
 * CSR SiRFprima2 VIP host driver
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

#ifndef _SIRFSOC_VOUT_H_

#define _SIRFSOC_VOUT_H_

#include <video/sirfsoc_vdss.h>


#define SIRFSOC_MAX_VOUT 6
#define SIRFSOC_MAX_DISPLAY 2
#define SIRFSOC_MAX_VOUT_ON_EACH_DISPLAY 3

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
	unsigned long device_is_open;
	spinlock_t vbq_lock;
	struct mutex lock;
	struct sirfsoc_vout_buf *disp_buf;
	unsigned int numbuffers;

	struct v4l2_pix_format pix_fmt;
	struct v4l2_framebuffer fbuf;

	enum v4l2_buf_type type;
	struct vb2_queue vb2_q;
	struct vb2_buffer *active_frm, *next_frm;
	/* allocator-specific contexts for each plane */
	struct vb2_alloc_ctx *alloc_ctx;
	struct list_head dma_queue;
	struct v4l2_rect src_rect;
	struct v4l2_rect dst_rect;
	u32 surf_width;
	u32 surf_height;

	u32 chromakey;
	u32 src_ckey;
	u32 dst_ckey;
	u32 global_alpha;

	struct sirfsoc_vdss_panel *display;
	struct sirfsoc_vdss_layer *layer;
	void *vpp_handle;
	bool passthrough;
	bool preempted;
	bool vout_info_dirty;

	struct v4l2_ctrl_handler ctrl_handler;
	struct vdss_vpp_colorctrl color_ctrl;
};


#endif
