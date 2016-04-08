/*
 * CSR SiRFSoc VIP host driver
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/async.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <linux/time.h>
#include <linux/version.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/mutex.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/videodev2.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/memblock.h>
#include <linux/dmaengine.h>
#include <linux/vmalloc.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/pm_qos.h>
#include <linux/i2c.h>
#include <linux/pinctrl/consumer.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-of.h>
#include <linux/i2c.h>

#include "vip_capture.h"


#ifndef MODULE
#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX
#endif


#define is_streaming(vip)	(vb2_is_streaming(&(vip)->vb2_vidq))

#define VIP_VERSION_CODE KERNEL_VERSION(1, 0, 0)
#define VIP_DRV_NAME "sirfsoc-vip"


static void vip_dma_count_done(void *pdata);
static int vip_start_dma(struct vip_dev *vip);
static void vip_hw_stop(struct vip_dev *vip);
static void vip_hw_wait_dma_idle(struct vip_dev *vip);
static void vip_hw_stop_fifo(struct vip_dev *vip);
static const struct vip_format *vip_find_format(u32 pixelformat);


/* VIP supported formats */
static const struct vip_format vip_formats[] = {
	{
		.desc		= "UYVY 4:2:2",
		.pixelformat	= V4L2_PIX_FMT_UYVY,
		.mbus_code	= V4L2_MBUS_FMT_UYVY8_2X8,
		.bpp		= 2,
	},
	{
		.desc		= "YUYV 4:2:2",
		.pixelformat	= V4L2_PIX_FMT_YUYV,
		.mbus_code	= V4L2_MBUS_FMT_YUYV8_2X8,
		.bpp		= 2,
	},
	{
		.desc		= "YVYU 4:2:2",
		.pixelformat	= V4L2_PIX_FMT_YVYU,
		.mbus_code	= V4L2_MBUS_FMT_YVYU8_2X8,
		.bpp		= 2,
	},
};
#define N_VIP_FMTS	ARRAY_SIZE(vip_formats)

/* VIP default V4L2 pixel format and mbus code */
static const struct v4l2_pix_format vip_def_pix_format = {
	.width		= VIP_DEFAULT_WIDTH,
	.height		= VIP_DEFAULT_HEIGHT,
	.pixelformat	= V4L2_PIX_FMT_UYVY,
	.field		= V4L2_FIELD_NONE,
	.bytesperline	= VIP_DEFAULT_WIDTH * 2,
	.sizeimage	= VIP_DEFAULT_WIDTH * VIP_DEFAULT_HEIGHT * 2,
};
static const enum v4l2_mbus_pixelcode vip_def_mbus_code =
						V4L2_MBUS_FMT_UYVY8_2X8;



/*
 *  Videobuf operations
 */


/*
 * vip_queue_setup : Callback function for queue setup.
 * @vq: vb2_queue ptr
 * @fmt: v4l2 format
 * @nbuffers: ptr to number of buffers requested by application
 * @nplanes:: contains number of distinct video planes needed to hold a frame
 * @sizes[]: contains the size (in bytes) of each plane.
 * @alloc_ctxs: ptr to allocation context
 *
 * This callback function is called when reqbuf() is called to adjust
 * the buffer count and buffer size
 */
static int vip_queue_setup(struct vb2_queue *vq,
				const struct v4l2_format *fmt,
				unsigned int *nbuffers, unsigned int *nplanes,
				unsigned int sizes[], void *alloc_ctxs[])
{
	struct vip_dev *vip = vb2_get_drv_priv(vq);
	unsigned int size;

	if (fmt && fmt->fmt.pix.sizeimage)
		size = fmt->fmt.pix.sizeimage;
	else
		size = vip->user_format.sizeimage;

	if (0 == *nbuffers)
		*nbuffers = 2;

	if (vip->video_limit)
		while (size * *nbuffers > vip->video_limit)
			(*nbuffers)--;

	dev_dbg(vip->dev, "%s: nbuffers= %d, size= %d\n", __func__,
		*nbuffers, size);

	*nplanes = 1;
	sizes[0] = size;
	alloc_ctxs[0] = vip->alloc_ctx;

	return 0;
}

/*
 * vip_buffer_init :  callback function for buffer init
 * @vb: ptr to vb2_buffer
 *
 * Called once after allocating each new buffer
 */
static int vip_buffer_init(struct vb2_buffer *vb)
{
	struct vip_buffer *buf = container_of(vb, struct vip_buffer, vb);

	INIT_LIST_HEAD(&buf->list);

	return 0;
}

/*
 * vip_buffer_prepare :  callback function for buffer prepare
 * @vb: ptr to vb2_buffer
 *
 * This is the callback function for buffer prepare when vb2_qbuf()
 * function is called.
 */
static int vip_buffer_prepare(struct vb2_buffer *vb)
{
	struct vip_dev *vip = vb2_get_drv_priv(vb->vb2_queue);
	struct vip_buffer *buf = container_of(vb, struct vip_buffer, vb);
	unsigned long addr, size, plane_size;
	unsigned int bytesperline;

	if (vb->state != VB2_BUF_STATE_ACTIVE &&
		vb->state != VB2_BUF_STATE_PREPARED) {
		size = vip->user_format.sizeimage;
		plane_size = vb2_plane_size(vb, 0);
		if (plane_size < size) {
			dev_err(vip->dev, "%s: plane size small(%ld<%ld)\n",
				__func__, plane_size, size);
			return -EINVAL;
		}

		/* Update sizeimage according to user setting by qbuf.
		 * For dma buffer case, sometimes we can not get accurate
		 * buffer demension while calling s_fmt until buffer is
		 * allocated. However, some buffers may has width padding
		 * which means the whole image size is larger than before.
		 */
		bytesperline = plane_size / vip->user_format.height;
		if (!vip->user_format.bytesperline ||
			vip->user_format.bytesperline != bytesperline) {
			size = bytesperline * vip->user_format.height;
			vip->user_format.bytesperline = bytesperline;
			vip->user_format.sizeimage = size;
		}
		vb2_set_plane_payload(vb, 0, size);

		addr = vb2_dma_contig_plane_dma_addr(vb, 0);

		buf->fmt = &vip->user_format;
		buf->dma = addr;

		if (!IS_ALIGNED(addr, 32)) {
			dev_err(vip->dev, "%s: addr not 32bit aligned\n",
						__func__);
			return -EINVAL;
		}
	}
	return 0;
}

/*
 * vip_buffer_queue : Callback function to add buffer to DMA queue
 * @vb: ptr to vb2_buffer
 */
static void vip_buffer_queue(struct vb2_buffer *vb)
{
	struct vip_dev *vip = vb2_get_drv_priv(vb->vb2_queue);
	struct vip_buffer *buf = container_of(vb, struct vip_buffer, vb);
	unsigned long flags;

	spin_lock_irqsave(&vip->lock, flags);

	if (list_empty(&vip->capture) && vb2_is_streaming(vb->vb2_queue)) {
		list_add_tail(&buf->list, &vip->capture);
		spin_unlock_irqrestore(&vip->lock, flags);

		/* this callback has been protected by host_lock mutex */
		if (!vip->rv.preemption)
			vip_start_dma(vip);

		return;
	}

	list_add_tail(&buf->list, &vip->capture);
	spin_unlock_irqrestore(&vip->lock, flags);
}

/*
 * vip_buffer_finish : Callback function before DQ buffer return
 * @vb: ptr to vb2_buffer
 */
static void vip_buffer_finish(struct vb2_buffer *vb)
{
	struct vip_dev *vip = vb2_get_drv_priv(vb->vb2_queue);
	struct v4l2_pix_format *upix = &vip->user_format;

	vb->v4l2_buf.field = upix->field;
}

/*
 * vip_start_streaming : Callback function to start streaming
 * @vq: vb2_queue ptr
 * @count: the number of already queued buffers
 *
 * This function is called whenever user space wants to start grabbing data
 * that may happen in response to a VIDIOC_STREAMON ioctl(), but the videobuf2
 * implementation of the read() system call can also use it.
 */
static int vip_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct vip_dev *vip = vb2_get_drv_priv(vq);
	unsigned long flags;

	spin_lock_irqsave(&vip->lock, flags);
	vip_hw_stop(vip);
	spin_unlock_irqrestore(&vip->lock, flags);

	return 0;
}

/*
 * vip_stop_streaming : Callback function to stop streaming
 * @vq: vb2_queue ptr
 *
 * A call to stop_streaming() will be made when user space no longer wants data
 * this callback should not return until DMA has been stopped
 */
static void vip_stop_streaming(struct vb2_queue *vq)
{
	struct vip_dev *vip = vb2_get_drv_priv(vq);
	struct vip_buffer *buf;
	unsigned long flags;

	/* release all active buffers */
	spin_lock_irqsave(&vip->lock, flags);
	if (list_empty(&vip->capture))
		goto out;

	if (is_cvd_vip(vip) || is_com_vip(vip))
		vip_hw_wait_dma_idle(vip);
	else
		dmaengine_terminate_all(vip->dma_chan);

	vip_hw_stop(vip);
	vip->vb2_active = NULL;

	while (!list_empty(&vip->capture)) {
		buf = list_entry(vip->capture.next, struct vip_buffer, list);
		list_del_init(&buf->list);
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
	}
out:
	spin_unlock_irqrestore(&vip->lock, flags);
}

/*
 * vip_wait_prepare : Callback function to free buffer
 * @vq: vb2_queue ptr
 *
 * Release any locks taken while calling vb2 functions
 * it is called before an ioctl needs to wait for a new
 * buffer to arrive; required to avoid a deadlock in
 * blocking access type
 */
static void vip_wait_prepare(struct vb2_queue *vq)
{
	struct vip_dev *vip = vb2_get_drv_priv(vq);

	mutex_unlock(&vip->host_lock);
}

/*
 * vip_wait_finish : Callback function to free buffer
 * @vq: vb2_queue ptr
 *
 * Reacquire all locks released in the previous callback
 * required to continue operation after sleeping while
 * waiting for a new buffer to arrive
 */
static void vip_wait_finish(struct vb2_queue *vq)
{
	struct vip_dev *vip = vb2_get_drv_priv(vq);

	mutex_lock(&vip->host_lock);
}

static struct vb2_ops vip_video_qops = {
	.queue_setup		= vip_queue_setup,
	.buf_init		= vip_buffer_init,
	.buf_prepare		= vip_buffer_prepare,
	.buf_queue		= vip_buffer_queue,
	.buf_finish		= vip_buffer_finish,
	/*.buf_cleanup		= vip_buffer_cleanup,*/
	.start_streaming	= vip_start_streaming,
	.stop_streaming		= vip_stop_streaming,
	.wait_prepare		= vip_wait_prepare,
	.wait_finish		= vip_wait_finish,
};

static int vip_init_videobuf2(struct vip_dev *vip)
{
	struct vb2_queue *q = &vip->vb2_vidq;
	struct device *dev = vip->dev;
	int ret;

	/*
	 * passing vip_dev dev to context
	 * so DMA buf will be allocated in declaerd area
	 */
	vip->alloc_ctx = vb2_dma_contig_init_ctx(dev);
	if (IS_ERR(vip->alloc_ctx)) {
		dev_err(vip->dev, "%s: fail to get context\n", __func__);
		return PTR_ERR(vip->alloc_ctx);
	}

	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->drv_priv = vip;
	q->ops = &vip_video_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct vip_buffer);
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;

	ret = vb2_queue_init(q);
	if (ret) {
		dev_err(vip->dev, "%s: vb2_queue_init() fail\n", __func__);
		vb2_dma_contig_cleanup_ctx(vip->alloc_ctx);
		return ret;
	}

	return 0;
}

/*
 * vip hw opvertaion
 */

#define vip_write(addr, value)	writel((value), vip->io_base + (addr))
#define vip_read(addr)		readl(vip->io_base + (addr))


static u32 dma_hw_get_interrupts(struct vip_dev *vip)
{
	return (vip_read(DMAN_INT_EN) &	vip_read(DMAN_INT)) & DMAN_INT_MASK;
}

static void dma_hw_clear_interrupts(struct vip_dev *vip, u32 status)
{
	vip_write(DMAN_INT, status & DMAN_INT_MASK);
}

static inline void dma_hw_set_start_addr(struct vip_dev *vip, u32 addr)
{
	vip_write(DMAN_ADDR, addr);
}

static void dma_hw_set_match_addr(struct vip_dev *vip,
						u32 *addrs, bool enable)
{
	if (enable) {
		vip_write(DMAN_MATCH_ADDR1, addrs[0]);
		vip_write(DMAN_MATCH_ADDR2, addrs[1]);
		vip_write(DMAN_MATCH_ADDR3, addrs[2]);

		vip_write(DMAN_MATCH_ADDR_EN, 0x1);
	} else {
		vip_write(DMAN_MATCH_ADDR_EN, 0x0);

		vip_write(DMAN_MATCH_ADDR1, 0x0);
		vip_write(DMAN_MATCH_ADDR2, 0x0);
		vip_write(DMAN_MATCH_ADDR3, 0x0);
	}
}

static void dma_hw_set_chain_mode(struct vip_dev *vip)
{
	unsigned int value;

	value = vip_read(DMAN_CTRL);
	value |= DMAN_CTRL_TABLE_NUM(1) | DMAN_CTRL_CHAIN_EN;
	vip_write(DMAN_CTRL, value);
}

/* single dma mode */
static void vip_hw_start_dma(struct vip_dev *vip, struct vip_buffer *buf)
{
	unsigned int size, width, height, bytesperline, addr;
	const struct vip_format *f = vip_find_format(buf->fmt->pixelformat);

	width = buf->fmt->width;
	height = buf->fmt->height;
	size = width * height * f->bpp;
	bytesperline = buf->fmt->bytesperline;
	addr = buf->dma;

	vip_write(DMAN_XLEN, size/height/4);	/* 32bit unit */
	vip_write(DMAN_YLEN, height - 1);	/* Actual line: DMAN_YLEN +1 */
	vip_write(DMAN_WIDTH, bytesperline/4);  /* 32bit unit, 2-D DMA mode */

	vip_write(DMAN_MUL, size/4/2);		/* WIDTH * ((YLEN + 1)>>1) */

	vip_write(DMAN_CTRL, 0x3);	/* BURST_LEN: 3, NO CHAIN, TO MEM */

	vip_write(DMAN_INT_CNT, size/4);	/* 32bit unit */
	vip_write(DMAN_INT_EN, 0x2);		/* Counter interrupt enable */

	dma_hw_set_start_addr(vip, addr);	/* The last reg, start dma */
}

/* single dma mode */
static void vip_hw_wait_dma_idle(struct vip_dev *vip)
{
	uint32_t times = 200000;
	/*
	* Currently we have no workable way to abort an active DMA,
	* so we have to wait for its finishing.
	* add ~40ms timeout in case hang up.
	* this solution is just a workround, it will make the interrupt
	* datency bigger than 40ms. we will discard it later.
	*/
	while ((vip_read(DMAN_VALID) & 0x1) && times--)
		cpu_relax();

}

/* set vip fifo level stop check/low check/high check length */
static void vip_hw_set_fifo_level_chk(struct vip_dev *vip, u8 sc, u8 lc, u8 hc)
{
	u32 val;

	val = CAM_FIFO_LEVEL_CHK_FIFO_SC(sc) |
		CAM_FIFO_LEVEL_CHK_FIFO_LC(lc) |
		CAM_FIFO_LEVEL_CHK_FIFO_HC(hc);
	vip_write(CAM_FIFO_LEVEL_CHECK, val);
}

static void vip_hw_reset(struct vip_dev *vip)
{
	u32 val;

	/* Disable camera SENSOR_INT interrupt */
	vip_write(CAM_INT_COUNT, 0x7fff7fff);

	/* Disable all camera interrupt */
	vip_write(CAM_INT_EN, 0);

	/* Set active window with maximum values */
	vip_write(CAM_START, 0x4fff4fff);
	vip_write(CAM_END,   0xffffffff);

	/* Work in continnous mode, bypass yuv2rgb */
	vip_write(CAM_CTRL, 0);

	if (is_cvd_vip(vip))
		vip_write(CAM_PIXEL_SHIFT, CAM_PIXEL_BYTE_SWAP |
						CAM_PIXEL_SHIFT_16BIT);
	else
		vip_write(CAM_PIXEL_SHIFT, CAM_PIXEL_SHIFT_0TO7);

	/* Disable FIFO */
	vip_hw_stop_fifo(vip);

	/* Set FIFO config data, high check, low check and stop check. */
	if (is_cvd_vip(vip) || is_com_vip(vip))
		vip_hw_set_fifo_level_chk(vip, 0x1, 0x8, 0x10);
	else
		vip_hw_set_fifo_level_chk(vip, 0x4, 0x8, 0x10);

	if (is_cvd_vip(vip) || is_com_vip(vip))
		val = CAM_DMA_CTRL_DMA_OP | CAM_DMA_CTRL_ENDIAN_NO_CHG;
	else
		val = CAM_DMA_CTRL_DMA_FLUSH | CAM_DMA_CTRL_ENDIAN_WXDW;
	vip_write(CAM_DMA_CTRL, val);

	/* DMA transfer will operate continuously until it is stopped */
	vip_write(CAM_DMA_LEN, 0);

	vip_write(DMAN_XLEN, 0x0);
	vip_write(DMAN_YLEN, 0x0);
	vip_write(DMAN_CTRL, 0x4); /* 16 burst length */
	vip_write(DMAN_WIDTH, 0x0);
	vip_write(DMAN_INT_EN, 0x0);
	vip_write(DMAN_MUL, 0x0);
}

static void vip_hw_set_src_size(struct vip_dev *vip, struct vip_rect rect)
{
	vip_write(CAM_START, CAM_START_XS(rect.left) | CAM_START_YS(rect.top));

	if (is_cvd_vip(vip))
		vip_write(CAM_END, CAM_END_XE(rect.right + 1) |
					CAM_END_YE(rect.bottom + 1));
	else
		vip_write(CAM_END, CAM_END_XE(rect.right) |
					CAM_END_YE(rect.bottom));
}

static void vip_hw_set_control(struct vip_dev *vip, struct vip_control control)
{
	u32 val;
	bool yuv2rgb = false;

	val = vip_read(CAM_CTRL);

	val &= ~CAM_CTRL_YUV_FORMAT_MASK;
	switch (control.input_fmt) {
	case VIP_PIXELFORMAT_YUYV:
		val |= CAM_CTRL_INPUT_YUYV;
		break;
	case VIP_PIXELFORMAT_UYYV:
		val |= CAM_CTRL_INPUT_UYYV;
		break;
	case VIP_PIXELFORMAT_YUVY:
		val |= CAM_CTRL_INPUT_YUVY;
		break;
	case VIP_PIXELFORMAT_UYVY:
		val |= CAM_CTRL_INPUT_UYVY;
		break;
	case VIP_PIXELFORMAT_YVYU:
		val |= CAM_CTRL_INPUT_YVYU;
		break;
	case VIP_PIXELFORMAT_VYYU:
		val |= CAM_CTRL_INPUT_VYYU;
		break;
	case VIP_PIXELFORMAT_YVUY:
		val |= CAM_CTRL_INPUT_YVUY;
		break;
	case VIP_PIXELFORMAT_VYUY:
		val |= CAM_CTRL_INPUT_VYUY;
		break;
	default:
		pr_err("%s(%d): unknown input format 0x%x\n",
			__func__, __LINE__, control.input_fmt);
		return;
	}

	val &= ~CAM_CTRL_OUT_FORMAT_MASK;
	switch (control.output_fmt) {
	case VIP_PIXELFORMAT_UYVY:
	case VIP_PIXELFORMAT_YUYV:
	case VIP_PIXELFORMAT_YVYU:
		yuv2rgb = false;
		break;
	case VIP_PIXELFORMAT_565:
		yuv2rgb = true;
		val |= CAM_CTRL_OUT_RGB565;
		break;
	case VIP_PIXELFORMAT_8880:
		yuv2rgb = true;
		val |= CAM_CTRL_OUT_RGB888;
		break;
	default:
		pr_err("%s(%d): unknown output format 0x%x\n",
			__func__, __LINE__, control.output_fmt);
		return;
	}

	if (yuv2rgb) {
		vip_write(CAM_YUV_COEF1, 0x12A00198);
		vip_write(CAM_YUV_COEF2, 0x12A190D0);
		vip_write(CAM_YUV_COEF3, 0x12A81000);
		vip_write(CAM_YUV_OFFSET, 0x115220DF);

		val |= CAM_CTRL_YUV_TO_RGB;
	} else
		val &= ~CAM_CTRL_YUV_TO_RGB;

	if (control.pixclk_internal)
		val |= CAM_CTRL_PXCLK_INTERNAL;
	else
		val &= ~CAM_CTRL_PXCLK_INTERNAL;

	if (control.hsync_internal)
		val |= CAM_CTRL_HSYNC_INTERNAL;
	else
		val &= ~CAM_CTRL_HSYNC_INTERNAL;

	if (control.vsync_internal)
		val |= CAM_CTRL_VSYNC_INTERNAL;
	else
		val &= ~CAM_CTRL_VSYNC_INTERNAL;

	if (control.pixclk_invert)
		val |= CAM_CTRL_PIXCLK_INV;
	else
		val &= ~CAM_CTRL_PIXCLK_INV;

	if (control.hsync_invert)
		val |= CAM_CTRL_HSYNC_INV;
	else
		val &= ~CAM_CTRL_HSYNC_INV;

	if (control.vsync_invert)
		val |= CAM_CTRL_VSYNC_INV;
	else
		val &= ~CAM_CTRL_VSYNC_INV;

	if (control.ccir656_en)
		val |= CAM_CTRL_CCIR656_EN;
	else
		val &= ~CAM_CTRL_CCIR656_EN;

	if (control.single_cap)
		val |= CAM_CTRL_SINGLE;
	else
		val &= ~CAM_CTRL_SINGLE;

	if (!is_cvd_vip(vip) && !is_com_vip(vip)) {
		if (control.pad_mux_on_upli)
			val |= CAM_CTRL_PAD_MUX_ON_UPLI;
		else
			val &= ~CAM_CTRL_PAD_MUX_ON_UPLI;
	}

	if (control.cap_from_even_en)
		val |= CAM_CTRL_CAP_FROM_EVEN;
	else
		val &= ~CAM_CTRL_CAP_FROM_EVEN;

	if (control.cap_from_odd_en)
		val |= CAM_CTRL_CAP_FROM_ODD;
	else
		val &= ~CAM_CTRL_CAP_FROM_ODD;

	if (control.hor_mirror_en)
		val |= CAM_CTRL_HOR_MIRROR;
	else
		val &= ~CAM_CTRL_HOR_MIRROR;

	switch (vip->data_mode) {
	case VIP_DATA_SAMPLE_MODE_SDR:
		val &= ~CAM_CTRL_DDR_SYNC_EN;
		val &= ~CAM_DDR_NEGPOS_EN;
		break;
	case VIP_DATA_SAMPLE_MODE_DDR:
		val |= CAM_CTRL_DDR_SYNC_EN;
		val |= CAM_DDR_NEGPOS_EN;
		break;
	default:
		pr_err("%s(%d): unknown data mode 0x%x\n",
			__func__, __LINE__, vip->data_mode);
		return;
	}

	vip_write(CAM_CTRL, val);
}

static void vip_hw_set_data_pin(struct vip_dev *vip, u32 pin_config)
{
	u32 val;

	val = vip_read(CAM_PIXEL_SHIFT);

	val &= ~CAM_PIXEL_SHIFT_MASK;
	switch (pin_config) {
	case VIP_PIXELSET_DATAPIN_0TO7:
		if (vip->data_mode == VIP_DATA_SAMPLE_MODE_DDR)
			val |= CAM_PIXEL_SHIFT_16BIT;
		else
			val |= CAM_PIXEL_SHIFT_0TO7;
		break;
	case VIP_PIXELSET_DATAPIN_0TO15:
		val |= CAM_PIXEL_SHIFT_16BIT;
		break;
	default:
		pr_err("%s(%d): unknown data shift 0x%x\n",
			__func__, __LINE__, pin_config);
		return;
	}

	if (is_cvd_vip(vip))
		val |= CAM_PIXEL_BYTE_SWAP;
	if (is_com_vip(vip))
		val |= CAM_PIXEL_WORD_SWAP;

	vip_write(CAM_PIXEL_SHIFT, val);

	/* re-sequence 8bit SDR 656&601 pixel data */
	if ((vip->data_shift == VIP_PIXELSET_DATAPIN_0TO7) &&
		(vip->data_mode == VIP_DATA_SAMPLE_MODE_SDR)) {
		val = vip_read(CAM_DMA_CTRL);
		val &= ~CAM_DMA_CTRL_ENDIAN_MODE_MASK;
		val |= CAM_DMA_CTRL_ENDIAN_BXDW;
		vip_write(CAM_DMA_CTRL, val);
	}

	if (is_com_vip(vip)) {
		vip_write(CAM_PXCLK_CFG, 0);
		vip_write(CAM_INPUT_BIT_SEL_0, 0);
		vip_write(CAM_INPUT_BIT_SEL_1, 1);
		vip_write(CAM_INPUT_BIT_SEL_2, 2);
		vip_write(CAM_INPUT_BIT_SEL_3, 3);
		vip_write(CAM_INPUT_BIT_SEL_4, 4);
		vip_write(CAM_INPUT_BIT_SEL_5, 5);
		vip_write(CAM_INPUT_BIT_SEL_6, 6);
		vip_write(CAM_INPUT_BIT_SEL_7, 7);
		vip_write(CAM_INPUT_BIT_SEL_8, 12);
		vip_write(CAM_INPUT_BIT_SEL_9, 13);
		vip_write(CAM_INPUT_BIT_SEL_10, 14);
		vip_write(CAM_INPUT_BIT_SEL_11, 15);
		vip_write(CAM_INPUT_BIT_SEL_12, 16);
		vip_write(CAM_INPUT_BIT_SEL_13, 17);
		vip_write(CAM_INPUT_BIT_SEL_14, 18);
		vip_write(CAM_INPUT_BIT_SEL_15, 19);
		vip_write(CAM_INPUT_BIT_SEL_HSYNC, 9);
		vip_write(CAM_INPUT_BIT_SEL_VSYNC, 10);
	}


}

static void vip_hw_set_int_count(struct vip_dev *vip, u16 x, u16 y)
{
	vip_write(CAM_INT_COUNT, CAM_INT_COUNT_XI(x) | CAM_INT_COUNT_YI(y));
}

static void vip_hw_set_linebuf(struct vip_dev *vip, u32 word_num)
{
	u32 val;

	/* linebuf reset active */
	val = vip_read(CAM_HOR_MIR_LINEBUF_CTRL);
	val |= CAM_LINEBUF_SW_RST;
	vip_write(CAM_HOR_MIR_LINEBUF_CTRL, val);

	/* linebuf reset inactive */
	val = vip_read(CAM_HOR_MIR_LINEBUF_CTRL);
	val &= ~CAM_LINEBUF_SW_RST;
	vip_write(CAM_HOR_MIR_LINEBUF_CTRL, val);

	/* set the number of pixel data in one line in the unit of 32 bit */
	val = vip_read(CAM_HOR_MIR_LINEBUF_CTRL);
	val &= ~CAM_LINEBUF_WORD_NUM_MASK;
	val |= CAM_LINEBUF_WORD_NUM(word_num);
	vip_write(CAM_HOR_MIR_LINEBUF_CTRL, val);
}

static void vip_hw_reset_fifo(struct vip_dev *vip)
{
	u32 val;

	/* Reset fifo */
	val = vip_read(CAM_FIFO_OP_REG);
	vip_write(CAM_FIFO_OP_REG, val | CAM_FIFO_OP_FIFO_RESET);
	vip_write(CAM_FIFO_OP_REG, val & ~CAM_FIFO_OP_FIFO_RESET);
}

static void vip_hw_start_fifo(struct vip_dev *vip)
{
	u32 val;

	/* Start FIFO transfer to DMA */
	val = vip_read(CAM_FIFO_OP_REG);
	vip_write(CAM_FIFO_OP_REG, val | CAM_FIFO_OP_FIFO_START);
}

static void vip_hw_stop_fifo(struct vip_dev *vip)
{
	vip_write(CAM_FIFO_OP_REG, CAM_FIFO_OP_FIFO_STOP);
}

static void vip_hw_start(struct vip_dev *vip)
{
	u32 val;

	/* clear all interrupts */
	if (is_cvd_vip(vip) || is_com_vip(vip))
		vip_write(CAM_INT_CTRL, CAM_INT_CTRL_MASK_A7);
	else
		vip_write(CAM_INT_CTRL, CAM_INT_CTRL_MASK);

	/* Enable overflow, underflow, sensor interrupt and bad field */
	if (is_cvd_vip(vip) || is_com_vip(vip))
		vip_write(CAM_INT_EN, CAM_INT_EN_FIFO_OFLOW |
					CAM_INT_EN_FIFO_UFLOW |
					CAM_INT_EN_CCIR656_INCOMP |
					CAM_INT_EN_BAD_FIELD);
	else
		vip_write(CAM_INT_EN, CAM_INT_EN_FIFO_OFLOW |
					CAM_INT_EN_FIFO_UFLOW);

	/* Reset and active all the configuration before starting */
	val = vip_read(CAM_CTRL);
	vip_write(CAM_CTRL, val | CAM_CTRL_INIT);
	vip_write(CAM_CTRL, val & ~CAM_CTRL_INIT);

	/* Start FIFO transfer to DMA */
	vip_hw_start_fifo(vip);
}

static void vip_hw_stop(struct vip_dev *vip)
{
	/* Stop the FIFO first */
	vip_hw_stop_fifo(vip);

	/* Disable camera interrupt */
	vip_write(CAM_INT_EN, 0);
	vip_write(CAM_INT_CTRL, CAM_INT_CTRL_MASK_A7);

	/* Disable DMA interrupt */
	vip_write(DMAN_INT_EN, 0x0);
	vip_write(DMAN_INT, DMAN_INT_MASK);
}

static u32 vip_hw_get_interrupts(struct vip_dev *vip)
{
	if (is_cvd_vip(vip) || is_com_vip(vip))
		return (vip_read(CAM_INT_EN) &
			vip_read(CAM_INT_CTRL)) & CAM_INT_CTRL_MASK_A7;
	else
		return (vip_read(CAM_INT_EN) &
			vip_read(CAM_INT_CTRL)) & CAM_INT_CTRL_MASK;
}

static void vip_hw_clear_interrupts(struct vip_dev *vip, u32 status)
{
	if (is_cvd_vip(vip) || is_com_vip(vip))
		vip_write(CAM_INT_CTRL, status & CAM_INT_CTRL_MASK_A7);
	else
		vip_write(CAM_INT_CTRL, status & CAM_INT_CTRL_MASK);
}

static void dma_hw_wait_first_table_done(struct vip_dev *vip)
{
	int value, old_value;

	/* clear table finish interrupt flag firstly */
	dma_hw_clear_interrupts(vip, DMAN_FINI_INT);

	old_value = value = vip_read(DMAN_INT_EN);
	value |= DMAN_FINI_INT;
	vip_write(DMAN_INT_EN, value); /* enable table finish interrupt */

	/* 120ms is enough for transfer done & timing control for the caller */
	if (!wait_for_completion_timeout(&vip->rv.done, msecs_to_jiffies(120)))
		dev_err(vip->dev, "wait dma table completiont timeout\n");

	vip_write(DMAN_INT_EN, old_value); /* restore dma interrupt enable */
}

static void __maybe_unused vip_print_registers(struct vip_dev *vip)
{
	pr_info("VIP registers\n");
	pr_info("CAM_COUNT             = 0x%.8x\n",
		vip_read(CAM_COUNT));
	pr_info("CAM_INT_COUNT         = 0x%.8x\n",
		vip_read(CAM_INT_COUNT));
	pr_info("CAM_START             = 0x%.8x\n",
		vip_read(CAM_START));
	pr_info("CAM_END               = 0x%.8x\n",
		vip_read(CAM_END));
	pr_info("CAM_CTRL              = 0x%.8x\n",
		vip_read(CAM_CTRL));
	pr_info("CAM_PIXEL_SHIFT       = 0x%.8x\n",
		vip_read(CAM_PIXEL_SHIFT));
	pr_info("CAM_YUV_COEF1         = 0x%.8x\n",
		vip_read(CAM_YUV_COEF1));
	pr_info("CAM_YUV_COEF2         = 0x%.8x\n",
		vip_read(CAM_YUV_COEF2));
	pr_info("CAM_YUV_COEF3         = 0x%.8x\n",
		vip_read(CAM_YUV_COEF3));
	pr_info("CAM_YUV_OFFSET        = 0x%.8x\n",
		vip_read(CAM_YUV_OFFSET));
	pr_info("CAM_INT_EN            = 0x%.8x\n",
		vip_read(CAM_INT_EN));
	pr_info("CAM_INT_CTRL          = 0x%.8x\n",
		vip_read(CAM_INT_CTRL));
	pr_info("CAM_VSYNC_CTRL        = 0x%.8x\n",
		vip_read(CAM_VSYNC_CTRL));
	pr_info("CAM_HSYNC_CTRL        = 0x%.8x\n",
		vip_read(CAM_HSYNC_CTRL));
	pr_info("CAM_PXCLK_CTRL        = 0x%.8x\n",
		vip_read(CAM_PXCLK_CTRL));
	pr_info("CAM_VSYNC_HSYNC       = 0x%.8x\n",
		vip_read(CAM_VSYNC_HSYNC));
	pr_info("CAM_TIMING_CTRL       = 0x%.8x\n",
		vip_read(CAM_TIMING_CTRL));
	pr_info("CAM_DMA_CTRL          = 0x%.8x\n",
		vip_read(CAM_DMA_CTRL));
	pr_info("CAM_DMA_LEN           = 0x%.8x\n",
		vip_read(CAM_DMA_LEN));
	pr_info("CAM_FIFO_CTRL_REG     = 0x%.8x\n",
		vip_read(CAM_FIFO_CTRL_REG));
	pr_info("CAM_FIFO_LEVEL_CHECK  = 0x%.8x\n",
		vip_read(CAM_FIFO_LEVEL_CHECK));
	pr_info("CAM_FIFO_OP_REG       = 0x%.8x\n",
		vip_read(CAM_FIFO_OP_REG));
	pr_info("CAM_FIFO_STATUS_REG   = 0x%.8x\n",
		vip_read(CAM_FIFO_STATUS_REG));
	pr_info("CAM_RD_FIFO_DATA      = 0x%.8x\n",
		vip_read(CAM_RD_FIFO_DATA));
	pr_info("CAM_TS_CTRL           = 0x%.8x\n",
		vip_read(CAM_TS_CTRL));
	pr_info("CAM_HOR_MIR_LINEBUF   = 0x%.8x\n",
		vip_read(CAM_HOR_MIR_LINEBUF_CTRL));

	pr_info("CAM_PXCLK_CFG         = 0x%.8x\n",
		vip_read(CAM_PXCLK_CFG));
	pr_info("CAM_INPUT_BIT_SEL_0   = 0x%.8x\n",
		vip_read(CAM_INPUT_BIT_SEL_0));
	pr_info("CAM_INPUT_BIT_SEL_1   = 0x%.8x\n",
		vip_read(CAM_INPUT_BIT_SEL_1));
	pr_info("CAM_INPUT_BIT_SEL_2   = 0x%.8x\n",
		vip_read(CAM_INPUT_BIT_SEL_2));
	pr_info("CAM_INPUT_BIT_SEL_3   = 0x%.8x\n",
		vip_read(CAM_INPUT_BIT_SEL_3));
	pr_info("CAM_INPUT_BIT_SEL_4   = 0x%.8x\n",
		vip_read(CAM_INPUT_BIT_SEL_4));
	pr_info("CAM_INPUT_BIT_SEL_5   = 0x%.8x\n",
		vip_read(CAM_INPUT_BIT_SEL_5));
	pr_info("CAM_INPUT_BIT_SEL_6   = 0x%.8x\n",
		vip_read(CAM_INPUT_BIT_SEL_6));
	pr_info("CAM_INPUT_BIT_SEL_7   = 0x%.8x\n",
		vip_read(CAM_INPUT_BIT_SEL_7));
	pr_info("CAM_INPUT_BIT_SEL_8   = 0x%.8x\n",
		vip_read(CAM_INPUT_BIT_SEL_8));
	pr_info("CAM_INPUT_BIT_SEL_9   = 0x%.8x\n",
		vip_read(CAM_INPUT_BIT_SEL_9));
	pr_info("CAM_INPUT_BIT_SEL_10  = 0x%.8x\n",
		vip_read(CAM_INPUT_BIT_SEL_10));
	pr_info("CAM_INPUT_BIT_SEL_11  = 0x%.8x\n",
		vip_read(CAM_INPUT_BIT_SEL_11));
	pr_info("CAM_INPUT_BIT_SEL_12  = 0x%.8x\n",
		vip_read(CAM_INPUT_BIT_SEL_12));
	pr_info("CAM_INPUT_BIT_SEL_13  = 0x%.8x\n",
		vip_read(CAM_INPUT_BIT_SEL_13));
	pr_info("CAM_INPUT_BIT_SEL_14  = 0x%.8x\n",
		vip_read(CAM_INPUT_BIT_SEL_14));
	pr_info("CAM_INPUT_BIT_SEL_15  = 0x%.8x\n",
		vip_read(CAM_INPUT_BIT_SEL_15));
	pr_info("CAM_INPUT_BIT_SEL_HS  = 0x%.8x\n",
		vip_read(CAM_INPUT_BIT_SEL_HSYNC));
	pr_info("CAM_INPUT_BIT_SEL_VS  = 0x%.8x\n",
		vip_read(CAM_INPUT_BIT_SEL_VSYNC));

	pr_info("DMAN_ADDR             = 0x%.8x\n",
		vip_read(DMAN_ADDR));
	pr_info("DMAN_XLEN             = 0x%.8x\n",
		vip_read(DMAN_XLEN));
	pr_info("DMAN_YLEN             = 0x%.8x\n",
		vip_read(DMAN_YLEN));
	pr_info("DMAN_CTRL             = 0x%.8x\n",
		vip_read(DMAN_CTRL));
	pr_info("DMAN_WIDTH            = 0x%.8x\n",
		vip_read(DMAN_WIDTH));
	pr_info("DMAN_VALID            = 0x%.8x\n",
		vip_read(DMAN_VALID));
	pr_info("DMAN_INT              = 0x%.8x\n",
		vip_read(DMAN_INT));
	pr_info("DMAN_INT_EN           = 0x%.8x\n",
		vip_read(DMAN_INT_EN));
	pr_info("DMAN_LOOP_CTRL        = 0x%.8x\n",
		vip_read(DMAN_LOOP_CTRL));
	pr_info("DMAN_INT_CNT          = 0x%.8x\n",
		vip_read(DMAN_INT_CNT));
	pr_info("DMAN_TIMEOUT_CNT      = 0x%.8x\n",
		vip_read(DMAN_TIMEOUT_CNT));
	pr_info("DMAN_PAU_TIME_CNT     = 0x%.8x\n",
		vip_read(DMAN_PAU_TIME_CNT));
	pr_info("DMAN_CUR_TABLE_ADDR   = 0x%.8x\n",
		vip_read(DMAN_CUR_TABLE_ADDR));
	pr_info("DMAN_CUR_DATA_ADDR    = 0x%.8x\n",
		vip_read(DMAN_CUR_DATA_ADDR));
	pr_info("DMAN_MUL              = 0x%.8x\n",
		vip_read(DMAN_MUL));
	pr_info("DMAN_STATE0           = 0x%.8x\n",
		vip_read(DMAN_STATE0));
	pr_info("DMAN_STATE1           = 0x%.8x\n",
		vip_read(DMAN_STATE1));
}

/*
 * internal operations.
 */

static const struct vip_format *vip_find_format(u32 pixelformat)
{
	unsigned int i;

	for (i = 0; i < N_VIP_FMTS; i++)
		if (vip_formats[i].pixelformat == pixelformat)
			return vip_formats + i;
	/* Not found? Then return the first format. */
	return vip_formats;
}

static const struct vip_format *vip_find_format_with_code
						(enum v4l2_mbus_pixelcode code)
{
	unsigned int i;

	for (i = 0; i < N_VIP_FMTS; i++)
		if (vip_formats[i].mbus_code == code)
			return vip_formats + i;
	/* Not found? Then return the first format. */
	return vip_formats;
}

static void vip_vip_isr(struct vip_dev *vip)
{
	u32 status;

	status = vip_hw_get_interrupts(vip);
	vip_hw_clear_interrupts(vip, status);

	/*under the error condition, we should stop the vip firstly,
	 *and then schedule a work to restart the vip again to recovery.
	 *if we don't stop the vip here, this isr will be called
	 *again and again	since the data transfer is running,
	 *and the restart work won't get run
	 */
	if (status & (VIP_INTMASK_FIFO_OFLOW | VIP_INTMASK_FIFO_UFLOW |
		VIP_INTMASK_656_INCOMP | VIP_INTMASK_BAD_FIELD)){
		vip_hw_stop(vip);
		schedule_work(&vip->restart_work);
	}

	if (status & VIP_INTMASK_SENSOR)
		dev_dbg(vip->dev, "sensor interrupt happens\n");

	if (status & VIP_INTMASK_FIFO_OFLOW)
		dev_err(vip->dev, "FIFO overflow interrupt happens\n");

	if (status & VIP_INTMASK_FIFO_UFLOW)
		dev_err(vip->dev, "FIFO underflow interrupt happens\n");

	if (is_cvd_vip(vip) || is_com_vip(vip)) {
		if (status & VIP_INTMASK_656_INCOMP)
			dev_err(vip->dev,
				"BT656 incomplete interrupt happens\n");

		if (status & VIP_INTMASK_BAD_FIELD)
			dev_err(vip->dev,
				"Bad field detection interrupt happens\n");
	}
}

static irqreturn_t dma_irq(int irq, void *data)
{
	struct vip_dev *vip = data;
	u32 dma_status;

	dma_status = dma_hw_get_interrupts(vip);
	dma_hw_clear_interrupts(vip, dma_status);

	/* DMA CNT_INT happens */
	if (dma_status & DMAN_INTMASK_CNT)
		vip_dma_count_done(vip);

	return IRQ_HANDLED;
}


static irqreturn_t vip_irq(int irq, void *data)
{
	struct vip_dev *vip = data;
	struct vip_subdev_info *subdev = &vip->subdev[0]; /* CVD always 1st */
	struct v4l2_subdev *sd = subdev->sd;
	void __iomem *irq_base = NULL;
	bool hd;
	u32 status, dma_status;

	if (is_cvd_vip(vip)) {
		irq_base = v4l2_get_subdevdata(sd); /* I am CVD_VIP */
	} else {
		vip_vip_isr(vip);		/* I am VIP1 or ancient VIP */
		return IRQ_HANDLED;
	}

	/* CVD_VIP handler starts */
	status = readl(irq_base);

	/* CVD interrupts: vsync and signals locked */
	if ((status & CVD3_INT_MASK) || (status & DEBUG_INT_MASK))
		v4l2_subdev_call(sd, core, interrupt_service_routine,
								status, &hd);

	/* VIP interrupt */
	if (status & VIP_INT_MASK)
		vip_vip_isr(vip);

	/* DMA interrupt */
	if (status & DMAC_INT_MASK) {
		dma_status = dma_hw_get_interrupts(vip);
		dma_hw_clear_interrupts(vip, dma_status);

		/* DMA CNT_INT happens */
		if (dma_status & DMAN_INTMASK_CNT)
				vip_dma_count_done(vip);

		/* DMA FINI_INT happens */
		if (dma_status & DMAN_INTMASK_FINI)
				complete(&vip->rv.done);
	}

	return IRQ_HANDLED;
}

static int vip_start_dma(struct vip_dev *vip)
{
	struct vip_buffer *buf;
	struct vb2_buffer *vb;
	struct dma_async_tx_descriptor *rx_desc;
	struct dma_slave_config config;
	unsigned int size, height;

	if (list_empty(&vip->capture)) {
		vip_hw_stop(vip);
		return 0;
	}

	buf = list_entry(vip->capture.next, struct vip_buffer, list);
	vb = &buf->vb;
	vip->vb2_active = vb;

	BUG_ON(buf->fmt == NULL);

	size = buf->fmt->sizeimage;
	height = buf->fmt->height;
	BUG_ON(size == 0);
	BUG_ON(height == 0);

	if (vb == NULL) {
		vip_hw_stop(vip);
		return -EINVAL;
	}

	vb->state = VB2_BUF_STATE_ACTIVE;
	if (is_cvd_vip(vip) || is_com_vip(vip)) {
		buf->dma = vb2_dma_contig_plane_dma_addr(vb, 0);

		vip_hw_reset_fifo(vip);
		vip_hw_start_dma(vip, buf);
	} else {
		memset(&config, 0, sizeof(config));

		config.src_maxburst = 4;
		config.src_maxburst = 4;
		dmaengine_slave_config(vip->dma_chan, &config);

		vip->dst_start = vb2_dma_contig_plane_dma_addr(vb, 0);
		rx_desc = dmaengine_prep_slave_single(vip->dma_chan,
				vip->dst_start, size, DMA_DEV_TO_MEM,
				DMA_PREP_INTERRUPT | DMA_CTRL_ACK);

		rx_desc->callback = vip_dma_count_done;
		rx_desc->callback_param = vip;

		dmaengine_submit(rx_desc);
		dma_async_issue_pending(vip->dma_chan);
	}

	vip_hw_start(vip);

	return 0;
}

/* Before vip_restart_worker() return, vip_dma_count_done() won't be called */
static void vip_restart_worker(struct work_struct *work)
{
	struct vip_dev *vip = container_of(work, struct vip_dev, restart_work);

	/* if current vip was preempted by rearview, should do nothing */
	mutex_lock(&vip->host_lock);
	if (!vip->rv.preemption)
		vip_start_dma(vip);
	mutex_unlock(&vip->host_lock);
}

static void vip_dma_count_done(void *pdata)
{
	struct vip_dev *vip = (struct vip_dev *)pdata;
	struct vb2_buffer *vb;
	struct vip_buffer *buf;
	unsigned long flags;

	dev_dbg(vip->dev, "%s\n", __func__);

	spin_lock_irqsave(&vip->lock, flags);

	vb = vip->vb2_active;
	if (vb == NULL)
		goto out;

	buf = container_of(vb, struct vip_buffer, vb);

	list_del_init(&buf->list);
	v4l2_get_timestamp(&vb->v4l2_buf.timestamp);
	vb2_buffer_done(vb, VB2_BUF_STATE_DONE);

	if (!list_empty(&vip->capture)) {
		buf = list_entry(vip->capture.next, struct vip_buffer, list);
		vip->vb2_active = &buf->vb;
	} else {
		vip->vb2_active = NULL;
		vip_hw_stop(vip);
	}

	if (vip->vb2_active != NULL) {
		struct dma_async_tx_descriptor *rx_desc;

		vip->vb2_active->state = VB2_BUF_STATE_ACTIVE;

		if (is_cvd_vip(vip) || is_com_vip(vip)) {
			buf->dma = vb2_dma_contig_plane_dma_addr(
							vip->vb2_active, 0);

			vip_hw_start_dma(vip, buf);
		} else {
			vip->dst_start =
				vb2_dma_contig_plane_dma_addr(vip->vb2_active,
									0);

			rx_desc = dmaengine_prep_slave_single(vip->dma_chan,
					vip->dst_start, buf->fmt->sizeimage,
					DMA_DEV_TO_MEM,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);

			rx_desc->callback = vip_dma_count_done;
			rx_desc->callback_param = vip;

			dmaengine_submit(rx_desc);
			dma_async_issue_pending(vip->dma_chan);
		}
	}

out:
	/* TODO: unlock could be refined */
	spin_unlock_irqrestore(&vip->lock, flags);
}

static int vip_pix_fmt_xlate(u32 pix_fmt)
{
	switch (pix_fmt) {
	case V4L2_PIX_FMT_UYVY:
		return VIP_PIXELFORMAT_UYVY;
	case V4L2_PIX_FMT_YUYV:
		return VIP_PIXELFORMAT_YUYV;
	case V4L2_PIX_FMT_YVYU:
		return VIP_PIXELFORMAT_YVYU;
	default:
		pr_warn("%s: input fmt error !\n", __func__);
		return VIP_PIXELFORMAT_UYVY;
	}
}

/* currently vip will not do scale and conversation */
static int vip_do_try_fmt(struct vip_subdev_info *subdev,
					struct v4l2_pix_format *upix)
{
	int ret = 0;
	struct v4l2_subdev *sd = subdev->sd;
	struct vip_dev *vip = subdev->host;
	struct v4l2_mbus_framefmt mbus_fmt;
	const struct vip_format *f = vip_find_format(upix->pixelformat);

	upix->pixelformat = f->pixelformat;

	v4l2_fill_mbus_format(&mbus_fmt, upix, f->mbus_code);
	v4l2_subdev_call(sd, video, try_mbus_fmt, &mbus_fmt);
	v4l2_fill_pix_format(upix, &mbus_fmt);

	/*
	 * Normally the input source into cvd-vip is interlaced, default TB SEQ
	 * Input source into vip1 is progressive,
	 * the capture from odd/even field feature should be disabled.
	 */
	switch (upix->field) {
	case V4L2_FIELD_NONE:
		subdev->interlaced = false;
		break;
	case V4L2_FIELD_SEQ_TB:
	case V4L2_FIELD_SEQ_BT:
		subdev->interlaced = true;
		break;
	default:
		/* can't support other formats, return default type to user */
		if (is_cvd_vip(vip)) {
			subdev->interlaced = true;
			upix->field = V4L2_FIELD_SEQ_TB;
		} else {
			subdev->interlaced = false;
			upix->field = V4L2_FIELD_NONE;
		}
		break;
	}

	upix->colorspace = V4L2_COLORSPACE_JPEG;

	if (!upix->bytesperline)
		upix->bytesperline = f->bpp * upix->width;
	upix->sizeimage = upix->bytesperline * upix->height;

	vip->user_format = *upix;
	vip->subdev_format = *upix;
	vip->mbus_code = mbus_fmt.code;

	return ret;
}

static int vip_config_subdev(struct vip_subdev_info *subdev)
{
	struct vip_dev *vip = subdev->host;
	struct v4l2_subdev *sd = subdev->sd;
	struct v4l2_mbus_framefmt mbus_fmt;
	struct v4l2_pix_format *spix = &vip->subdev_format;
	struct v4l2_dv_timings timings;
	unsigned int height, x_start, y_start, x_end, y_end;
	int ret = 0;

	v4l2_fill_mbus_format(&mbus_fmt, spix, vip->mbus_code);

	ret = v4l2_subdev_call(sd, video, s_mbus_fmt, &mbus_fmt);
	if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
		return ret;
	v4l2_fill_pix_format(spix, &mbus_fmt);

	height = spix->height;
	x_start = 0;
	y_start = 0;

	/*
	 * VIP always receive 2x8bit style data
	 * so if use 16 bits data width, set pixel width to END
	 * if use 8 bits data width, set bytesperline to END
	*/
	if ((vip->data_shift == VIP_PIXELSET_DATAPIN_0TO15) ||
				(vip->data_mode == VIP_DATA_SAMPLE_MODE_DDR))
		x_end = spix->width + x_start - 1;		/* 16 bits */
	else
		x_end = spix->bytesperline + x_start - 1;	/* 8 bits */

	if (subdev->interlaced)
		y_end = height / 2 + y_start - 1;
	else
		y_end = height + y_start - 1;

	if (subdev->endpoint.bus_type == V4L2_MBUS_PARALLEL) {
		memset(&timings, 0, sizeof(timings));
		v4l2_subdev_call(sd, video, g_dv_timings, &timings);

		if ((vip->data_shift == VIP_PIXELSET_DATAPIN_0TO15) ||
			(vip->data_mode == VIP_DATA_SAMPLE_MODE_DDR)) {
			x_start	+= timings.bt.hsync + timings.bt.hbackporch;
			x_end	+= timings.bt.hsync + timings.bt.hbackporch;
		} else {
			x_start	+= (timings.bt.hsync + timings.bt.hbackporch)
									* 2;
			x_end	+= (timings.bt.hsync + timings.bt.hbackporch)
									* 2;
		}

		y_start	+= timings.bt.vsync + timings.bt.vbackporch;
		y_end	+= timings.bt.vsync + timings.bt.vbackporch;
	}

	dev_dbg(vip->dev,
		"%s: x_start %d x_end %d y_start %d y_end %d\n",
		__func__, x_start, x_end, y_start, y_end);

	vip->target_rect.left = x_start;
	vip->target_rect.right = x_end;
	vip->target_rect.top = y_start;
	vip->target_rect.bottom = y_end;

	return 0;
}

static int vip_config_host(struct vip_subdev_info *subdev)
{
	struct vip_dev *vip = subdev->host;
	struct v4l2_pix_format *upix = &vip->user_format;
	struct v4l2_pix_format *spix = &vip->subdev_format;
	struct vip_rect rect = vip->target_rect;
	struct vip_control control;

	control.input_fmt = vip_pix_fmt_xlate(spix->pixelformat);
	control.output_fmt = vip_pix_fmt_xlate(upix->pixelformat);

	control.pixclk_internal	= 0;
	control.hsync_internal	= 0;
	control.vsync_internal	= 0;
	control.pixclk_invert	= 0;
	control.hsync_invert	= 0;
	control.vsync_invert	= 0;
	control.single_cap	= 0;
	control.hor_mirror_en	= 0;

	switch (upix->field) {
	case V4L2_FIELD_NONE:
		control.cap_from_odd_en	= 0;
		control.cap_from_even_en = 0;
		break;
	case V4L2_FIELD_SEQ_TB:
		control.cap_from_odd_en	= 1;
		control.cap_from_even_en = 0;
		break;
	case V4L2_FIELD_SEQ_BT:
		control.cap_from_odd_en	= 0;
		control.cap_from_even_en = 1;
		break;
	default:
		dev_err(vip->dev, "un-supported field format\n");
		return -EINVAL;
	}

	if (subdev->endpoint.bus_type == V4L2_MBUS_BT656)
		control.ccir656_en	= 1;

	if (subdev->endpoint.bus_type == V4L2_MBUS_PARALLEL)
		control.ccir656_en	= 0;

#ifdef CONFIG_ARCH_ATLAS6
	if (!is_cvd_vip(vip) && !is_com_vip(vip))
		control.pad_mux_on_upli	= 1;
#endif

	vip_hw_reset(vip);
	vip_hw_set_src_size(vip, rect);
	vip_hw_set_control(vip, control);

	vip_hw_set_data_pin(vip, vip->data_shift);

	vip_hw_set_int_count(vip, 0x0001, 0x0001);

	return 0;
}

/*
 * IOCTL interface.
 */
static int vidioc_querycap(struct file *file, void  *priv,
			       struct v4l2_capability *cap)
{
	WARN_ON(priv != file->private_data);

	strlcpy(cap->driver, VIP_DRV_NAME, sizeof(cap->driver));
	strlcpy(cap->card, VIP_DRV_NAME, sizeof(cap->card));
	cap->version = VIP_VERSION_CODE;
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;

	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
				      struct v4l2_format *f)
{
	struct vip_subdev_info *subdev = file->private_data;

	WARN_ON(priv != file->private_data);

	/* Only single-plane capture is supported so far */
	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	/* limit format to hardware capabilities */
	return vip_do_try_fmt(subdev, &f->fmt.pix);
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
				    struct v4l2_format *f)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct vip_dev *vip = subdev->host;

	WARN_ON(priv != file->private_data);

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	f->fmt.pix = vip->user_format;

	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
				    struct v4l2_format *f)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct vip_dev *vip = subdev->host;
	int ret = 0;

	WARN_ON(priv != file->private_data);

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (is_streaming(vip)) {
		dev_err(vip->dev, "S_FMT denied: queue initialised\n");
		return -EBUSY;
	}

	ret = vip_do_try_fmt(subdev, &f->fmt.pix);
	if (ret)
		return ret;

	ret = vip_config_subdev(subdev);
	if (!ret)
		ret = vip_config_host(subdev);

	return ret;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
				       struct v4l2_fmtdesc *f)
{
	int ret;
	enum v4l2_mbus_pixelcode code;
	struct vip_subdev_info *subdev = file->private_data;
	struct v4l2_subdev *sd = subdev->sd;
	struct vip_format *vip_fmt;

	WARN_ON(priv != file->private_data);

	ret = v4l2_subdev_call(sd, video, enum_mbus_fmt, f->index, &code);
	if (ret)
		return ret;

	vip_fmt = vip_find_format_with_code(code);
	f->pixelformat = vip_fmt->pixelformat;
	strlcpy(f->description, vip_fmt->desc, sizeof(f->description));

	return 0;
}

static int vidioc_enum_input(struct file *file, void *priv,
				 struct v4l2_input *inp)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct v4l2_subdev *sd = subdev->sd;
	int ret;
	u32 status;

	if (inp->index >= subdev->num_inputs)
		return -EINVAL;

	*inp = subdev->inputs[inp->index];
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std  = V4L2_STD_ALL;
	inp->capabilities = V4L2_IN_CAP_DV_TIMINGS | V4L2_IN_CAP_STD;

	/* get input status */
	ret = v4l2_subdev_call(sd, video, g_input_status, &status);
	if (!ret)
		inp->status = status;

	return 0;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct vip_subdev_info *subdev = file->private_data;

	*i = subdev->cur_input;

	return 0;
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct vip_dev *vip = subdev->host;
	struct v4l2_subdev *sd = subdev->sd;
	int ret;

	if (vb2_is_busy(&vip->vb2_vidq))
		return -EBUSY;

	if (i >= subdev->num_inputs)
		return -EINVAL;

	ret = v4l2_subdev_call(sd, video, s_routing, i, 0, 0);
	if ((ret < 0) && (ret != -ENOIOCTLCMD)) {
		dev_err(vip->dev, "Failed to set input\n");
		return ret;
	}

	subdev->cur_input = i;

	return 0;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id a)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct v4l2_subdev *sd = subdev->sd;

	return v4l2_subdev_call(sd, video, s_std, a);
}

static int vidioc_g_std(struct file *file, void *priv, v4l2_std_id *a)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct v4l2_subdev *sd = subdev->sd;

	return v4l2_subdev_call(sd, video, g_std, a);
}

static int vidioc_querystd(struct file *file, void *priv, v4l2_std_id *a)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct v4l2_subdev *sd = subdev->sd;

	return v4l2_subdev_call(sd, video, querystd, a);
}

static int vidioc_enum_framesizes(struct file *file, void *fh,
					 struct v4l2_frmsizeenum *fsize)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct v4l2_subdev *sd = subdev->sd;
	struct v4l2_frmsizeenum fsize_mbus = *fsize;
	int ret;

	ret = v4l2_subdev_call(sd, video, enum_framesizes, &fsize_mbus);
	if (ret) {
		if (fsize->index != 0)
			return -EINVAL;

		fsize->type = V4L2_FRMSIZE_TYPE_CONTINUOUS;
		fsize->stepwise.min_width = 1;
		fsize->stepwise.min_height = 1;
		fsize->stepwise.max_width = VIP_DEFAULT_WIDTH;
		fsize->stepwise.max_height = VIP_DEFAULT_HEIGHT;
		fsize->stepwise.step_width = fsize->stepwise.step_height = 1;
	} else
		*fsize = fsize_mbus;

	return 0;

}

static int vidioc_reqbufs(struct file *file, void *priv,
			      struct v4l2_requestbuffers *p)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct vip_dev *vip = subdev->host;

	WARN_ON(priv != file->private_data);

	return vb2_reqbufs(&vip->vb2_vidq, p);
}

static int vidioc_querybuf(struct file *file, void *priv,
				struct v4l2_buffer *p)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct vip_dev *vip = subdev->host;

	WARN_ON(priv != file->private_data);

	return vb2_querybuf(&vip->vb2_vidq, p);
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct vip_dev *vip = subdev->host;

	WARN_ON(priv != file->private_data);

	return vb2_qbuf(&vip->vb2_vidq, p);
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct vip_dev *vip = subdev->host;

	WARN_ON(priv != file->private_data);

	return vb2_dqbuf(&vip->vb2_vidq, p, file->f_flags & O_NONBLOCK);
}

static int vidioc_expbuf(struct file *file, void *priv,
				struct v4l2_exportbuffer *eb)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct vip_dev *vip = subdev->host;

	WARN_ON(priv != file->private_data);

	return vb2_expbuf(&vip->vb2_vidq, eb);
}

static int vidioc_create_bufs(struct file *file, void *priv,
					struct v4l2_create_buffers *create)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct vip_dev *vip = subdev->host;

	return vb2_create_bufs(&vip->vb2_vidq, create);
}

static int vidioc_prepare_buf(struct file *file, void *priv,
				  struct v4l2_buffer *b)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct vip_dev *vip = subdev->host;

	return vb2_prepare_buf(&vip->vb2_vidq, b);
}

static int vidioc_streamon(struct file *file, void *priv,
			       enum v4l2_buf_type i)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct vip_dev *vip = subdev->host;
	struct v4l2_subdev *sd = subdev->sd;
	int ret;

	WARN_ON(priv != file->private_data);

	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	ret = vb2_streamon(&vip->vb2_vidq, i);
	if (!ret)
		v4l2_subdev_call(sd, video, s_stream, 1);

	/* we need start vip later than sub device */
	vip_start_dma(vip);

	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv,
				enum v4l2_buf_type i)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct vip_dev *vip = subdev->host;
	struct v4l2_subdev *sd = subdev->sd;

	WARN_ON(priv != file->private_data);

	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	vb2_streamoff(&vip->vb2_vidq, i);

	v4l2_subdev_call(sd, video, s_stream, 0);

	return 0;
}

static int vidioc_cropcap(struct file *file, void *fh,
			      struct v4l2_cropcap *a)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct v4l2_subdev *sd = subdev->sd;

	return v4l2_subdev_call(sd, video, cropcap, a);
}

static int vidioc_g_crop(struct file *file, void *fh,
			     struct v4l2_crop *a)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct v4l2_subdev *sd = subdev->sd;

	return v4l2_subdev_call(sd, video, g_crop, a);
}

/*
 * According to the V4L2 API, drivers shall not update the struct v4l2_crop
 * argument with the actual geometry, instead, the user shall use G_CROP to
 * retrieve it.
 */
static int vidioc_s_crop(struct file *file, void *fh,
			     const struct v4l2_crop *a)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct v4l2_subdev *sd = subdev->sd;

	return v4l2_subdev_call(sd, video, s_crop, a);
}

static int vidioc_g_parm(struct file *file, void *fh,
			     struct v4l2_streamparm *a)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct v4l2_subdev *sd = subdev->sd;

	return v4l2_subdev_call(sd, video, g_parm, a);
}

static int vidioc_s_parm(struct file *file, void *fh,
			     struct v4l2_streamparm *a)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct v4l2_subdev *sd = subdev->sd;

	return v4l2_subdev_call(sd, video, s_parm, a);
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int vidioc_g_register(struct file *file, void *fh,
				 struct v4l2_dbg_register *reg)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct v4l2_subdev *sd = subdev->sd;

	return v4l2_subdev_call(sd, core, g_register, reg);
}

static int vidioc_s_register(struct file *file, void *fh,
				 const struct v4l2_dbg_register *reg)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct v4l2_subdev *sd = subdev->sd;

	return v4l2_subdev_call(sd, core, s_register, reg);
}
#endif


/*
 * file system operation interfaces.
 */
static int sirfsoc_camera_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct vip_subdev_info *subdev;
	struct vip_dev *vip;
	struct v4l2_subdev *sd;
	int ret;

	if (!vdev || !video_is_registered(vdev))
		return -ENODEV;

	subdev = video_get_drvdata(vdev);
	vip = subdev->host;
	sd = subdev->sd;

	ret = try_module_get(vdev->fops->owner) ? 0 : -ENODEV;
	if (ret < 0) {
		dev_err(vip->dev, "Couldn't lock capture bus driver.\n");
		return ret;
	}

	if (!vip->dev) {
		/* No device driver attached */
		ret = -ENODEV;
		goto exit_module_put;
	}

	/* if rearview is running or vip opened, we forbid user to use vip */
	if (test_and_set_bit(1, &vip->device_is_used)) {
		ret = -EBUSY;
		goto exit_module_put;
	}

	if (mutex_lock_interruptible(&vip->host_lock)) {
		ret = -ERESTARTSYS;
		goto exit_module_put;
	}

	ret = vip_init_videobuf2(vip);
	if (ret)
		goto exit_power;

	/* save dvd application info for rearview concurrency case */
	vip->task = current;

	mutex_unlock(&vip->host_lock);

	file->private_data = subdev;
	dev_dbg(vip->dev, "camera device open\n");

	return 0;


exit_power:
	v4l2_subdev_call(sd, core, s_power, 0);
	mutex_unlock(&vip->host_lock);
exit_module_put:
	module_put(vdev->fops->owner);

	return ret;
}

static int sirfsoc_camera_close(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct vip_subdev_info *subdev = file->private_data;
	struct vip_dev *vip = subdev->host;
	struct v4l2_subdev *sd = subdev->sd;

	mutex_lock(&vip->host_lock);

	vb2_queue_release(&vip->vb2_vidq);
	vb2_dma_contig_cleanup_ctx(vip->alloc_ctx);

	vip_hw_stop(vip);

	clear_bit(1, &vip->device_is_used);

	vip->task = NULL;

	mutex_unlock(&vip->host_lock);

	module_put(vdev->fops->owner);

	dev_dbg(vip->dev, "camera device close\n");

	return 0;
}

static int sirfsoc_camera_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct vip_dev *vip = subdev->host;
	int err;

	dev_dbg(vip->dev, "mmap called,vma=0x%08lx\n", (unsigned long)vma);

	if (mutex_lock_interruptible(&vip->host_lock))
		return -ERESTARTSYS;

	err = vb2_mmap(&vip->vb2_vidq, vma);

	mutex_unlock(&vip->host_lock);

	dev_dbg(vip->dev, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end - (unsigned long)vma->vm_start,
		err);

	return err;
}

static unsigned int sirfsoc_camera_poll(struct file *file, poll_table *pt)
{
	struct vip_subdev_info *subdev = file->private_data;
	struct vip_dev *vip = subdev->host;
	int ret = 0;

	mutex_lock(&vip->host_lock);
	ret = vb2_poll(&vip->vb2_vidq, file, pt);
	mutex_unlock(&vip->host_lock);

	return ret;
}

static struct v4l2_file_operations sirfsoc_camera_fops = {
	.owner		= THIS_MODULE,
	.open		= sirfsoc_camera_open,
	.release	= sirfsoc_camera_close,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= sirfsoc_camera_mmap,
	.poll		= sirfsoc_camera_poll,
};

static const struct v4l2_ioctl_ops sirfsoc_camera_ioctl_ops = {
	.vidioc_querycap	 = vidioc_querycap,
	.vidioc_try_fmt_vid_cap  = vidioc_try_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap    = vidioc_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap    = vidioc_s_fmt_vid_cap,
	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,
	.vidioc_enum_input	 = vidioc_enum_input,
	.vidioc_g_input		 = vidioc_g_input,
	.vidioc_s_input		 = vidioc_s_input,
	.vidioc_s_std		 = vidioc_s_std,
	.vidioc_g_std		 = vidioc_g_std,
	.vidioc_querystd	 = vidioc_querystd,
	.vidioc_enum_framesizes  = vidioc_enum_framesizes,
	.vidioc_reqbufs		 = vidioc_reqbufs,
	.vidioc_querybuf	 = vidioc_querybuf,
	.vidioc_qbuf		 = vidioc_qbuf,
	.vidioc_dqbuf		 = vidioc_dqbuf,
	.vidioc_expbuf		 = vidioc_expbuf,
	.vidioc_create_bufs	 = vidioc_create_bufs,
	.vidioc_prepare_buf	 = vidioc_prepare_buf,
	.vidioc_streamon	 = vidioc_streamon,
	.vidioc_streamoff	 = vidioc_streamoff,
	.vidioc_cropcap		 = vidioc_cropcap,
	.vidioc_g_crop		 = vidioc_g_crop,
	.vidioc_s_crop		 = vidioc_s_crop,
	.vidioc_g_parm		 = vidioc_g_parm,
	.vidioc_s_parm		 = vidioc_s_parm,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register	 = vidioc_g_register,
	.vidioc_s_register	 = vidioc_s_register,
#endif
};

/*
 * probe function usage operations.
 */
/* get subdev source input platform information */
static int vip_get_subdev_input(struct device_node *remote,
						struct vip_subdev_info *subdev)
{
	struct vip_dev *vip = subdev->host;
	struct device_node *np;
	const char *input_name;
	u32 input_id, i;
	int ret = 0;

	/*
	* if these info disappear in the DT, we won't treat it as error,
	* then input-number will be 0, nothing to do.
	*/
	of_property_read_u32(remote, "input-number", &subdev->num_inputs);
	if (subdev->num_inputs > SUBDEV_MAX_INPUTS) {
		dev_err(vip->dev, "Invalidate input-number value %d\n",
							subdev->num_inputs);
		subdev->num_inputs = SUBDEV_MAX_INPUTS;
	}

	for (i = 0; i < subdev->num_inputs; i++) {

		np = of_parse_phandle(remote, "inputs", i);
		if (!np) {
			dev_err(vip->dev, "Unable to get inputs %d\n", i);
			ret = -EINVAL;
			continue;
		}

		ret = of_property_read_u32(np, "input-id", &input_id);
		if (ret) {
			dev_err(vip->dev, "Unable to get input %d id\n", i);
			continue;
		}
		if (input_id >= SUBDEV_MAX_INPUTS) {
			dev_err(vip->dev, "Invalidate input-id value %d\n",
								input_id);
			input_id = i;
		}

		ret = of_property_read_string(np, "input-name", &input_name);
		if (ret) {
			dev_err(vip->dev, "Unable to get input %d name\n", i);
			continue;
		}

		if (strcmp(input_name, "camera") == 0)
			vip->rv.subdev_index = vip->num_subdev;

		subdev->inputs[input_id].index = input_id;
		strncpy(subdev->inputs[input_id].name, input_name,
					sizeof(subdev->inputs[input_id].name));
	}

	return ret;
}

static int vip_get_data_shift(struct vip_dev *vip,	struct device_node *np)
{
	int ret = 0;

	if (of_find_property(np, "data-shift", NULL)) {
		/* VIP1 should point this */
		ret = of_property_read_u32(np, "data-shift", &vip->data_shift);
		if (ret) {
			dev_err(vip->dev, "%s: Can't get data shift value\n",
								__func__);
			return ret;
		}
	}

	/* Translate VIP0&VIP1 data shift value to our language */

	/* VIP0 uses fixed 15:0 data pins  */
	if (is_cvd_vip(vip))
		vip->data_shift = VIP_PIXELSET_DATAPIN_0TO15;

	if (is_com_vip(vip)) {
		switch (vip->data_shift) {
		case 0:
			vip->data_shift = VIP_PIXELSET_DATAPIN_0TO15;
			break;
		case 1:
			vip->data_shift = VIP_PIXELSET_DATAPIN_0TO7;
			break;
		case 2:
			vip->data_shift = VIP_PIXELSET_DATAPIN_1TO8;
			break;
		default:
			vip->data_shift = VIP_PIXELSET_DATAPIN_0TO7;
			break;
		}
	}

	return 0;
}

static int vip_get_data_mode(struct vip_dev *vip, struct device_node *np)
{
	int ret = 0;
	const char *mode;

	ret = of_property_read_string(np, "data-mode", &mode);
	if (!ret) {
		if (!strcmp(mode, "SDR"))
			vip->data_mode = VIP_DATA_SAMPLE_MODE_SDR;
		else if (!strcmp(mode, "DDR"))
			vip->data_mode = VIP_DATA_SAMPLE_MODE_DDR;
		else {
			dev_info(vip->dev, "invalid data mode, set to SDR\n");
			vip->data_mode = VIP_DATA_SAMPLE_MODE_SDR;
		}
	} else {
		dev_info(vip->dev, "no set data mode, default SDR\n");
		vip->data_mode = VIP_DATA_SAMPLE_MODE_SDR;
	}

	return 0;
}

static void vip_subdev_notify(struct v4l2_subdev *sd, unsigned int notification,
			void *arg)
{
	kobject_uevent(&sd->dev->kobj, notification);
}



static int vip_subdevs_register(struct vip_dev *vip)
{
	struct device_node *parent = vip->dev->of_node;
	struct device_node *l_ep, *r_ep, *remote, *port;
	struct i2c_client *client;
	struct platform_device *pdev;
	struct v4l2_subdev *sd;
	int ret = 0;

	/* The VIP node have only one port subnode */
	port = of_get_next_child(parent, NULL);

	/* There may be tvdecoder & hdmi receiver endpoints under port node */
	for_each_available_child_of_node(port, l_ep) {

		struct vip_subdev_info *subdev = &vip->subdev[vip->num_subdev];
		struct v4l2_of_endpoint *endpoint = &subdev->endpoint;

		subdev->host = vip;

		r_ep = of_parse_phandle(l_ep, "remote-endpoint", 0);
		if (!r_ep) {
			dev_err(vip->dev, "%s: can't find %s's remote-ep\n",
				__func__, l_ep->full_name);
			return -EINVAL;
		}

		v4l2_of_parse_endpoint(r_ep, endpoint);

		ret = vip_get_data_shift(vip, r_ep);
		if (ret)
			dev_info(vip->dev, "%s: can't get data shift\n",
						__func__);

		ret = vip_get_data_mode(vip, r_ep);
		if (ret)
			dev_info(vip->dev, "%s: can't get data mode\n",
						__func__);
		of_node_put(r_ep);

		remote = of_graph_get_remote_port_parent(l_ep);
		of_node_put(l_ep);
		if (remote == NULL) {
			dev_err(vip->dev, "%s: remote dev at %s not found\n",
				__func__, l_ep->full_name);
			ret = -EINVAL;
			continue;
		}

		ret = vip_get_subdev_input(remote, subdev);
		if (ret)
			dev_info(vip->dev, "%s: get subdev input info error\n",
						__func__);

		client = of_find_i2c_device_by_node(remote);
		if (!client) {
			dev_dbg(vip->dev, "%s: find i2c client %s failed\n",
				__func__, remote->full_name);

			/* now we try to find cvd */
			pdev = of_find_device_by_node(remote);
			if (!pdev) {
				dev_err(vip->dev, "%s: find cvd pdev %s failed\n",
					__func__, remote->full_name);
				ret = -EPROBE_DEFER;
				continue;
			}

			device_lock(&pdev->dev);

			sd = dev_get_platdata(&pdev->dev);

			ret = v4l2_device_register_subdev(&vip->v4l2_dev, sd);
			if (ret < 0) {
				dev_err(vip->dev, "Register subdev failed\n");
				device_unlock(&pdev->dev);
				of_node_put(remote);
				continue;
			}

			v4l2_set_subdev_hostdata(sd, subdev);
			subdev->sd = sd;

			device_unlock(&pdev->dev);
			of_node_put(remote);
		} else {
			/* we found a i2c subdev */
			device_lock(&client->dev);

			if (!client->dev.driver ||
				!try_module_get(client->dev.driver->owner)) {
				device_unlock(&client->dev);
				of_node_put(remote);
				put_device(&client->dev);
				ret = -EPROBE_DEFER;
				continue;
			}

			sd = i2c_get_clientdata(client);

			ret = v4l2_device_register_subdev(&vip->v4l2_dev, sd);
			if (ret < 0) {
				dev_err(vip->dev, "Register subdev failed\n");
				module_put(client->dev.driver->owner);
				device_unlock(&client->dev);
				of_node_put(remote);
				put_device(&client->dev);
				continue;
			}

			v4l2_set_subdev_hostdata(sd, subdev);
			subdev->sd = sd;

			module_put(client->dev.driver->owner);
			device_unlock(&client->dev);
			of_node_put(remote);
			put_device(&client->dev);
		}

		vip->num_subdev++;
	}

	if (vip->num_subdev == 0)
		return ret;


	return (vip->num_subdev == 0) ? ret : 0;
}

static int vip_video_devs_create(struct vip_dev *vip)
{
	struct device	*dev = vip->dev;
	struct video_device *vdev;
	unsigned int index, num_subdev = vip->num_subdev;
	int ret = 0;

	for (index = 0; num_subdev > 0; num_subdev--, index++) {
		struct vip_subdev_info *subdev = &vip->subdev[index];

		vdev = video_device_alloc();
		if (!vdev)
			return -ENOMEM;

		snprintf(vdev->name, sizeof(vdev->name),
				"vip-%s", subdev->sd->name);

		vdev->dev_parent	= dev;
		vdev->fops		= &sirfsoc_camera_fops;
		vdev->ioctl_ops		= &sirfsoc_camera_ioctl_ops;
		vdev->release		= video_device_release;
		vdev->tvnorms		= V4L2_STD_NTSC | V4L2_STD_PAL;
		vdev->ctrl_handler	= subdev->sd->ctrl_handler;
		vdev->lock		= &vip->host_lock;
		vdev->v4l2_dev		= &vip->v4l2_dev;

		subdev->vdev = vdev;
		video_set_drvdata(vdev, subdev);

		ret = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
		if (ret < 0) {
			dev_err(dev, "%s: video_register_device failed: %d\n",
				__func__, ret);
			return ret;
		}

		v4l2_info(vdev->v4l2_dev,
			"/dev/video%d created as capture device\n", vdev->num);
	}

	return 0;
}

/* stop vip & subdev, backup std & port info, stop dvd player application */
static void vip_rv_pre_preempt(struct vip_dev *vip)
{
	unsigned int index = vip->rv.subdev_index;
	struct vip_subdev_info *subdev = &vip->subdev[index];
	struct v4l2_subdev *sd = subdev->sd;
	unsigned long flags;

	spin_lock_irqsave(&vip->lock, flags);

	vip_hw_wait_dma_idle(vip);
	vip_hw_stop(vip);

	spin_unlock_irqrestore(&vip->lock, flags);

	vip->dvd_port = subdev->cur_input;
	v4l2_subdev_call(sd, video, g_std, &vip->dvd_std);
	v4l2_subdev_call(sd, video, s_stream, 0);

	if (vip->task) {
		send_sig(SIGSTOP, vip->task, 0);

		while (!task_is_stopped(vip->task))
			usleep_range(250, 350);
	}
}

/* continue dvd player application, restore vip setting and start it */
static void vip_rv_post_preempt(struct vip_dev *vip)
{
	unsigned int index = vip->rv.subdev_index;
	struct vip_subdev_info *subdev = &vip->subdev[index];
	struct v4l2_subdev *sd = subdev->sd;

	if (vip->task)
		send_sig(SIGCONT, vip->task, 0);

	/* restore VIP hardware configuration */
	vip_config_subdev(subdev);
	vip_config_host(subdev);

	/* restore subdev hardware configuration and start */
	v4l2_subdev_call(sd, video, s_routing, vip->dvd_port, 0, 0);
	v4l2_subdev_call(sd, video, s_std, vip->dvd_std);
	v4l2_subdev_call(sd, video, s_stream, 1);

	/* start hardware pipe line */
	vip_start_dma(vip);

	/* preempt done */
	vip->rv.preemption = false;
}

v4l2_std_id vip_rv_querystd(void *data)
{
	struct vip_dev	*vip = data;
	unsigned int index = vip->rv.subdev_index;
	struct vip_subdev_info *subdev = &vip->subdev[index];
	struct v4l2_subdev *sd = subdev->sd;
	v4l2_std_id std;

	mutex_lock(&vip->host_lock);

	v4l2_subdev_call(sd, video, s_routing, index, 0, 0);
	v4l2_subdev_call(sd, video, querystd, &std);

	mutex_unlock(&vip->host_lock);

	return std;
}
EXPORT_SYMBOL(vip_rv_querystd);

/* called by rearview for configuration before hardware start */
void vip_rv_config(struct vip_rv_info *rv_info)
{
	struct vip_dev	*vip = rv_info->rv_vip;
	struct vip_control control;
	struct vip_rect rect;
	v4l2_std_id std;
	u32 *addrs;

	vip->rv.std		= rv_info->std;
	vip->rv.rv_vip		= rv_info->rv_vip;
	vip->rv.mirror_en	= rv_info->mirror_en;
	vip->rv.match_addrs[0]	= rv_info->match_addrs[0];
	vip->rv.match_addrs[1]	= rv_info->match_addrs[1];
	vip->rv.match_addrs[2]	= rv_info->match_addrs[2];
	vip->rv.dma_table_addr	= rv_info->dma_table_addr;

	std = vip->rv.std;
	addrs = vip->rv.match_addrs;

	mutex_lock(&vip->host_lock);

	if (test_and_set_bit(1, &vip->device_is_used)) {
		dev_info(vip->dev, "VIP has been using\n");

		/* rearview take over vip from dvd player */
		vip->rv.preemption = true;

		/* need to stop dvd player before rearview preempt vip */
		vip_rv_pre_preempt(vip);
	}

	vip_hw_reset(vip);

	if (std == V4L2_STD_PAL) {
		rect.left	= 0;
		rect.right	= PAL_STD_F_W - 1;
		rect.top	= 0;
		rect.bottom	= PAL_STD_F_H - 1;
	} else { /* default V4L2_STD_NTSC format */
		rect.left	= 0;
		rect.right	= NTSC_STD_F_W - 1;
		rect.top	= 0;
		rect.bottom	= NTSC_STD_F_H - 1;

		vip->rv.std     = V4L2_STD_NTSC;
	}

	vip_hw_set_src_size(vip, rect);

	if (rv_info->mirror_en) {
		vip_hw_set_fifo_level_chk(vip, 0x4, 0x8, 0x10);

		vip_hw_set_linebuf(vip, 0x167);
	}

	control.input_fmt = VIP_PIXELFORMAT_UYVY;
	control.output_fmt = VIP_PIXELFORMAT_UYVY;
	control.pixclk_internal	= 0;
	control.hsync_internal	= 0;
	control.vsync_internal	= 0;
	control.pixclk_invert	= 0;
	control.hsync_invert	= 0;
	control.vsync_invert	= 0;
	control.single_cap	= 0;
	if (rv_info->mirror_en)
		control.hor_mirror_en = 1;
	else
		control.hor_mirror_en = 0;
	control.cap_from_odd_en	= 1;
	control.cap_from_even_en = 0;
	control.ccir656_en	= 0;
	vip_hw_set_control(vip, control);

	vip_hw_set_data_pin(vip, VIP_PIXELSET_DATAPIN_0TO15);

	dma_hw_set_chain_mode(vip);

	dma_hw_set_match_addr(vip, addrs, true);

	mutex_unlock(&vip->host_lock);
}
EXPORT_SYMBOL(vip_rv_config);

void vip_rv_start(void *data)
{
	struct vip_dev	*vip = data;
	unsigned int index = vip->rv.subdev_index;
	struct vip_subdev_info *subdev = &vip->subdev[index];
	struct v4l2_subdev *sd = subdev->sd;
	unsigned int dma_table_addr = vip->rv.dma_table_addr;

	mutex_lock(&vip->host_lock);

	vip_hw_reset_fifo(vip);
	dma_hw_set_start_addr(vip, dma_table_addr);

	v4l2_subdev_call(sd, video, s_routing, index, 0, 0);
	v4l2_subdev_call(sd, video, s_std, vip->rv.std);
	v4l2_subdev_call(sd, video, s_stream, 1);

	/* Reset and active all the configuration before starting */
	index = vip_read(CAM_CTRL);
	vip_write(CAM_CTRL, index | CAM_CTRL_INIT);
	vip_write(CAM_CTRL, index & ~CAM_CTRL_INIT);

	vip_hw_start_fifo(vip);

	/* timing control: after that we can do the display */
	dma_hw_wait_first_table_done(vip);

	mutex_unlock(&vip->host_lock);
}
EXPORT_SYMBOL(vip_rv_start);

void vip_rv_stop(void *data)
{
	struct vip_dev	*vip = data;
	unsigned int index = vip->rv.subdev_index;
	struct vip_subdev_info *subdev = &vip->subdev[index];
	struct v4l2_subdev *sd = subdev->sd;

	mutex_lock(&vip->host_lock);

	vip_hw_wait_dma_idle(vip);

	vip_hw_stop_fifo(vip);
	vip_hw_reset_fifo(vip);

	dma_hw_set_match_addr(vip, NULL, false);

	v4l2_subdev_call(sd, video, s_stream, 0);

	if (!vip->rv.preemption)	/* only rearview is running */
		clear_bit(1, &vip->device_is_used);
	else	/* dvd player ran before, need to restore it */
		vip_rv_post_preempt(vip);

	mutex_unlock(&vip->host_lock);
}
EXPORT_SYMBOL(vip_rv_stop);

/*
 * module interfaces.
 */
static int vip_probe(struct platform_device *pdev)
{
	struct device	*dev = &pdev->dev;
	struct vip_dev	*vip = NULL;
	struct pinctrl *p;
	int i = 0, ret = 0;

	vip = devm_kzalloc(dev, sizeof(*vip), GFP_KERNEL);
	if (!vip)
		return -ENOMEM;

	vip->dev	 = dev;
	vip->user_format = vip->subdev_format = vip_def_pix_format;
	vip->mbus_code = vip_def_mbus_code;
	platform_set_drvdata(pdev, vip);

	INIT_LIST_HEAD(&vip->capture);
	spin_lock_init(&vip->lock);
	mutex_init(&vip->host_lock);
	init_completion(&vip->rv.done);
	INIT_WORK(&vip->restart_work, vip_restart_worker);
	vip->rv.preemption = false;
	vip->task = NULL;

	vip->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (vip->res == NULL) {
		dev_err(dev, "%s: fail to get vip regs resource\n", __func__);
		return -EINVAL;
	}
	vip->io_base = devm_ioremap_resource(dev, vip->res);
	if (!vip->io_base) {
		dev_err(dev, "%s: fail to ioremap vip regs\n", __func__);
		return -ENOMEM;
	}

	vip->irq = platform_get_irq(pdev, 0);
	if (!vip->irq) {
		dev_err(dev, "%s: fail to get vip irq\n", __func__);
		return -EINVAL;
	}
	ret = devm_request_irq(dev, vip->irq, vip_irq, 0, VIP_DRV_NAME, vip);
	if (ret) {
		dev_err(dev, "%s: fail to request irq\n", __func__);
		return ret;
	}

	vip->type = *(const unsigned int *)(of_match_device(dev->driver
					->of_match_table, dev)->data);
	if (is_com_vip(vip)) {
		vip->dma_irq = platform_get_irq(pdev, 1);
		if (!vip->dma_irq) {
			dev_err(dev, "%s: fail to get dma_irq\n", __func__);
			return -EINVAL;
		}
		ret = devm_request_irq(dev, vip->dma_irq, dma_irq, 0,
						VIP_DRV_NAME, vip);
		if (ret) {
			dev_err(dev, "%s: fail to request irq\n", __func__);
			return ret;
		}
		p = devm_pinctrl_get_select_default(&pdev->dev);
		if (IS_ERR(p)) {
			dev_err(dev, "Fail to select vip1 pinmux\n");
			return  -EINVAL;
		}
	}

	vip->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(vip->clk)) {
		dev_err(dev, "%s: fail to get vip clock\n", __func__);
		return -EINVAL;
	}

	ret = clk_prepare_enable(vip->clk);
	if (ret) {
		dev_err(dev, "%s: fail to open vip clock\n", __func__);
		return ret;
	}

	ret = of_property_read_u32(dev->of_node,
				"sirf,vip_cma_size", &vip->video_limit);
	if (ret) {
		dev_err(dev, "%s: Unable to get vip mem size\n", __func__);
		goto exit;
	}

	if (!is_cvd_vip(vip) && !is_com_vip(vip)) {
		vip->dma_chan = dma_request_slave_channel(vip->dev, "rx");
		if (!vip->dma_chan) {
			dev_err(dev, "%s: vip dma channel req fail\n",
								__func__);
			ret = -ENODEV;
			goto exit;
		}
	}

	if (is_com_vip(vip))
		vip->v4l2_dev.notify = vip_subdev_notify;


	ret = v4l2_device_register(dev, &vip->v4l2_dev);
	if (ret)
		goto exit_uninit_dma;

	ret = vip_subdevs_register(vip);
	if (ret < 0)
		goto exit_v4l2_unregister;

	ret = vip_video_devs_create(vip);
	if (ret) {
		dev_err(dev, "%s: create video device fail\n", __func__);
		goto exit_subdev_unregister;
	}
	if (is_com_vip(vip)) {
		ret = device_reset(dev);
		if (ret) {
			dev_err(dev, "Failed to reset\n");
			goto exit_subdev_unregister;
		}
	}

	return 0;

exit_subdev_unregister:
	for (i = 0; vip->num_subdev > 0; vip->num_subdev--, i++)
		v4l2_device_unregister_subdev(vip->subdev[i].sd);
exit_v4l2_unregister:
	v4l2_device_unregister(&vip->v4l2_dev);
exit_uninit_dma:
	if (!is_cvd_vip(vip) && !is_com_vip(vip))
		dma_release_channel(vip->dma_chan);
exit:
	clk_disable_unprepare(vip->clk);
	return ret;
}

static int vip_remove(struct platform_device *pdev)
{
	struct vip_dev *vip = platform_get_drvdata(pdev);
	unsigned int i;

	dev_info(&pdev->dev, "%s\n", __func__);

	for (i = 0; vip->num_subdev > 0; vip->num_subdev--, i++)
		v4l2_device_unregister_subdev(vip->subdev[i].sd);

	v4l2_device_unregister(&vip->v4l2_dev);

	if (is_cvd_vip(vip) || is_com_vip(vip)) {
		vip_hw_wait_dma_idle(vip);
	} else {
		dmaengine_terminate_all(vip->dma_chan);
		dma_release_channel(vip->dma_chan);
	}

	dma_release_declared_memory(&pdev->dev);

	return 0;
}


#ifdef CONFIG_PM_SLEEP
static int vip_pm_suspend(struct device *dev)
{
	struct vip_dev *vip = dev_get_drvdata(dev);
	struct vip_subdev_info *subdev = &vip->subdev[0];
	struct v4l2_subdev *sd = subdev->sd;

	disable_irq(vip->irq);
	flush_work(&vip->restart_work);
	if (is_com_vip(vip))
		disable_irq(vip->dma_irq);

	if (is_cvd_vip(vip) || is_com_vip(vip))
		vip_hw_wait_dma_idle(vip);
	else
		dmaengine_terminate_all(vip->dma_chan);

	vip_hw_stop(vip);

	/* capture pipe line is working, should also stop subdev */
	if (is_streaming(vip))
		v4l2_subdev_call(sd, video, s_stream, 0);

	clk_disable_unprepare(vip->clk);

	return 0;
}

static int vip_pm_resume(struct device *dev)
{
	struct vip_dev *vip = dev_get_drvdata(dev);
	struct vip_subdev_info *subdev = &vip->subdev[0];
	struct v4l2_subdev *sd = subdev->sd;
	int ret;

	ret = clk_prepare_enable(vip->clk);
	if (ret) {
		dev_err(dev, "vip resume clk enable failed\n");
		return ret;
	}

	enable_irq(vip->irq);
	if (is_com_vip(vip))
		enable_irq(vip->dma_irq);

	/* Before suspend it's active, so we should restore the pipe line */
	if (is_streaming(vip)) {

		/* restore VIP hardware configuration */
		vip_config_host(subdev);

		/* restore subdev hardware configuration and start */
		v4l2_subdev_call(sd, core, init, 0);
		v4l2_subdev_call(sd, video, s_stream, 1);

		/* continue to transfer */
		if (vip->vb2_active)
			vip_start_dma(vip);
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(vip_pm_ops, vip_pm_suspend, vip_pm_resume);


static const unsigned int p2_vip_type  = P2_VIP;
static const unsigned int cvd_vip_type = CVD_VIP;
static const unsigned int com_vip_type = COM_VIP;

static struct of_device_id vip_match_tbl[] = {
	{ .compatible = "sirf,prima2-vip",     .data = &p2_vip_type },
	{ .compatible = "sirf,atlas7-cvd-vip", .data = &cvd_vip_type },
	{ .compatible = "sirf,atlas7-com-vip", .data = &com_vip_type },
	{ /* end */ }
};

static struct platform_driver vip_driver = {
	.driver		= {
		.name = VIP_DRV_NAME,
		.pm = &vip_pm_ops,
		.of_match_table = vip_match_tbl,
	},
	.probe = vip_probe,
	.remove = vip_remove,
};

static int __init sirfsoc_vip_init(void)
{
	return platform_driver_register(&vip_driver);
}

static void __exit sirfsoc_vip_exit(void)
{
	platform_driver_unregister(&vip_driver);
}

subsys_initcall(sirfsoc_vip_init);
module_exit(sirfsoc_vip_exit);


MODULE_DESCRIPTION("sirfsoc VIP V4l2 capture driver");
MODULE_LICENSE("GPL v2");
