/*
 * CSR SiRFprima2 VIP host driver
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
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
#include <linux/mutex.h>
#include <linux/clk.h>
#include <linux/videodev2.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/memblock.h>
#include <linux/dmaengine.h>
#include <linux/sirfsoc_dma.h>

#include <asm/dma.h>
#include <mach/hardware.h>

#include "sirfsoc_vip.h"

#ifndef MODULE
#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX
#endif

static DEFINE_MUTEX(camera_lock);

#define SIRFSOC_CAM_VERSION_CODE KERNEL_VERSION(0, 0, 5)
#define SIRFSOC_CAM_DRV_NAME "sirfsoc-vip"
#define SIRFSOC_TVDECODER_DEV 1
#define DMA_SYNCTO_EVENFIELD_START

/* v4l2 csr extensions */
#define V4L2_CID_PADDR_Y (V4L2_CID_PRIVATE_BASE + 0)

static const char *sirfsoc_cam_driver_description = "sirfsoc_vip";

static u32 IfOdd[4], RDPtr, WRPtr, DMASync;
static phys_addr_t sirf_vip_phy_base;
static phys_addr_t sirf_vip_phy_size;

/*
 *  Videobuf operations
 */
static int sirfsoc_camera_videobuf_setup(struct videobuf_queue *vq,
						unsigned int *count,
						unsigned int *size)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct sirfsoc_camera_dev *pcdev = ici->priv;
	int bytes_per_line = soc_mbus_bytes_per_line(icd->user_width,
						icd->current_fmt->host_fmt);

	/* planar capture requires Y, U and V buffers to be page aligned */
	*size = bytes_per_line * icd->user_height;
	if (0 == *count)
		*count = 2;

	if (pcdev->video_limit)
		while (*size * *count > pcdev->video_limit)
			(*count)--;

	dev_dbg(icd->pdev, "%s: count=%d, size=%d\n", __func__,
		*count, *size);

	return 0;
}

static void free_buffer(struct videobuf_queue *vq,
			struct sirfsoc_buffer *buf)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct sirfsoc_camera_dev *pcdev = ici->priv;
	unsigned long flags;

	dev_dbg(icd->pdev, "%s: (vb=0x%p) 0x%08lx %zd\n", __func__,
		&buf->vb, buf->vb.baddr, buf->vb.bsize);

	if (in_interrupt())
		BUG();

	spin_lock_irqsave(&pcdev->lock, flags);
	if (pcdev->active != NULL && pcdev->active == &buf->vb) {
		pcdev->vip_funcs.pfnStop();
		dma_release_channel(pcdev->dma_chan);
		pcdev->active = NULL;
		list_del_init(&(buf->vb.queue));
		buf->vb.state = VIDEOBUF_ERROR;
		wake_up_all(&buf->vb.done);
	}
	spin_unlock_irqrestore(&pcdev->lock, flags);

	/* This waits until this buffer is out of danger, i.e., until it is no
	 * longer in STATE_QUEUED or STATE_ACTIVE */
	videobuf_waiton(vq, &buf->vb, 0, 0);

	videobuf_dma_contig_free(vq, &buf->vb);
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

static int sirfsoc_camera_videobuf_prepare(struct videobuf_queue *vq,
					  struct videobuf_buffer *vb,
					  enum v4l2_field field)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct sirfsoc_buffer *buf;
	int ret;

	int bytes_per_line = soc_mbus_bytes_per_line(icd->user_width,
						icd->current_fmt->host_fmt);

	buf = container_of(vb, struct sirfsoc_buffer, vb);

	dev_dbg(icd->pdev, "%s: (vb=0x%p) 0x%08lx %zd\n", __func__,
		vb, vb->baddr, vb->bsize);

	/* Added list head initialization on alloc */
	WARN_ON(!list_empty(&vb->queue));
#ifdef SIRFSOC_CAM_DEBUG
	/* This can be useful if you want to see if we actually fill
	 * the buffer with something */
	memset((void *)vb->baddr, 0xaa, vb->bsize);
#endif

	BUG_ON(NULL == icd->current_fmt);

	if (buf->fmt	!= (struct soc_camera_data_format *)icd->current_fmt ||
	    vb->width	!= icd->user_width ||
	    vb->height	!= icd->user_height ||
	    vb->field	!= field) {
		buf->fmt	=
			(struct soc_camera_data_format *)icd->current_fmt;
		vb->width	= icd->user_width;
		vb->height	= icd->user_height;
		vb->field	= field;
		vb->state	= VIDEOBUF_NEEDS_INIT;
	}

	vb->size = bytes_per_line * vb->height;
	if (0 != vb->baddr && vb->bsize < vb->size) {
		ret = -EINVAL;
		goto out;
	}

	if (vb->state == VIDEOBUF_NEEDS_INIT) {
		ret = videobuf_iolock(vq, vb, NULL);
		if (ret)
			goto fail;
		vb->state = VIDEOBUF_PREPARED;
	}

	return 0;
fail:
	free_buffer(vq, buf);
out:
	return ret;
}

static void sirfsoc_camera_callback (void *pdata) {
	struct videobuf_buffer *vb = NULL;
	struct sirfsoc_camera_dev *pcdev = (struct sirfsoc_camera_dev*)pdata;

	if(!pcdev) {
		return;
	} else {
		vb = pcdev->active;
	}
	if (vb) {
		list_del_init(&vb->queue);
		vb->state = VIDEOBUF_DONE;
		wake_up(&vb->done);
	}

	if (!list_empty(&pcdev->capture)) {
		pcdev->active = list_entry(pcdev->capture.next,
			struct videobuf_buffer, queue);
	} else {
		pcdev->active = NULL;
		pcdev->vip_funcs.pfnStop();
	}

	if (pcdev->active != NULL) {
		struct dma_async_tx_descriptor *rx_desc;

		pcdev->active->state = VIDEOBUF_ACTIVE;
		pcdev->dma_xt->dst_start = videobuf_to_dma_contig(vb);

		rx_desc = dmaengine_prep_interleaved_dma(pcdev->dma_chan, pcdev->dma_xt, 0);
	        rx_desc->callback = sirfsoc_camera_callback;
	        rx_desc->callback_param = pcdev;

	        dmaengine_submit(rx_desc);
	        dma_async_issue_pending(pcdev->dma_chan);
	}

}

static int sirfsoc_camera_start_dma_channel(
			struct sirfsoc_camera_dev *pcdev, int bResetFifo)
{
	struct videobuf_buffer *vb = pcdev->active;
	struct dma_async_tx_descriptor *rx_desc;

	if (vb == NULL) {
		pcdev->vip_funcs.pfnStop();
		mdelay(1);
		dma_release_channel(pcdev->dma_chan);
		return -EINVAL;
	}

	vb->state = VIDEOBUF_ACTIVE;

	if (bResetFifo)	{
		pcdev->vip_funcs.pfnStop();
		mdelay(1);
	}

	pcdev->dma_xt->sgl[0].size = vb->size/vb->height; /* transfer size in byte*/
	pcdev->dma_xt->sgl[0].icg = 0;
	pcdev->dma_xt->frame_size = 1;
	pcdev->dma_xt->numf = vb->height;
	pcdev->dma_xt->dst_start = videobuf_to_dma_contig(vb);
        pcdev->dma_xt->dir = DMA_DEV_TO_MEM;

	rx_desc = dmaengine_prep_interleaved_dma(pcdev->dma_chan, pcdev->dma_xt, 0);
	rx_desc->callback = sirfsoc_camera_callback;
	rx_desc->callback_param = pcdev;

	dmaengine_submit(rx_desc);
	dma_async_issue_pending(pcdev->dma_chan);

	pcdev->vip_funcs.pfnStart(0);

	return 0;
}

static void sirfsoc_camera_videobuf_queue(struct videobuf_queue *vq,
					 struct videobuf_buffer *vb)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct sirfsoc_camera_dev *pcdev = ici->priv;

	dev_dbg(icd->pdev, "%s: (vb=0x%p) 0x%08lx %zd\n", __func__,
		vb, vb->baddr, vb->bsize);
	vb->state = VIDEOBUF_QUEUED;
	list_add_tail(&vb->queue, &pcdev->capture);
	if (!pcdev->active) {
		pcdev->active = vb;
		sirfsoc_camera_start_dma_channel(pcdev, 1);
#if 0
		pcdev->vip_funcs.pfnPrintRegister();
#endif
	}
}

static void sirfsoc_camera_videobuf_release(struct videobuf_queue *vq,
					   struct videobuf_buffer *vb)
{
	free_buffer(vq, container_of(vb, struct sirfsoc_buffer, vb));
}

static struct videobuf_queue_ops sirfsoc_videobuf_ops = {
	.buf_setup      = sirfsoc_camera_videobuf_setup,
	.buf_prepare    = sirfsoc_camera_videobuf_prepare,
	.buf_queue      = sirfsoc_camera_videobuf_queue,
	.buf_release    = sirfsoc_camera_videobuf_release,
};

static void sirfsoc_camera_init_videobuf(struct videobuf_queue *q,
			      struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct sirfsoc_camera_dev *pcdev = ici->priv;

	/* We must pass NULL as dev pointer, then all pci_* dma operations
	 * transform to normal dma_* ones. */
	videobuf_queue_dma_contig_init(q,
			&sirfsoc_videobuf_ops,
			pcdev->dev, &pcdev->lock,
			V4L2_BUF_TYPE_VIDEO_CAPTURE,
			V4L2_FIELD_NONE,
			sizeof(struct sirfsoc_buffer),
			icd,
			&ici->host_lock);
}

static int sirfsoc_camera_activate(struct sirfsoc_camera_dev *pcdev)
{
	struct sirfsoc_camera_platform_data *pdata = pcdev->pdata;
	int ret;

	clk_prepare_enable(pcdev->clk);

	if (pcdev->pdata && pcdev->pdata->init) {
		dev_dbg(pcdev->dev, "%s: camera init\n", __func__);
		ret = pcdev->pdata->init(pcdev->dev);
		if (ret) {
			dev_err(pcdev->dev, "%s: dev init ret=%d\n",
				__func__, ret);
			return ret;
		}
	}

	pcdev->vip_funcs.pfnInitialize(pcdev->base, NULL);

	if (pdata && pdata->power) {
		dev_dbg(pcdev->dev, "%s: power on camera\n", __func__);
		ret = pdata->power(pcdev->dev, 1);
		if (ret) {
			dev_err(pcdev->dev, "%s: dev power ret=%d\n",
				__func__, ret);
			return ret;
		}
	}

	if (pdata && pdata->reset) {
		dev_dbg(pcdev->dev, "%s: camera reset\n",
			__func__);
		ret = pdata->reset(pcdev->dev);
		if (ret) {
			dev_err(pcdev->dev, "%s: dev reset ret=%d\n",
				__func__, ret);
			return ret;
		}

	}

	/*DMA not sync*/
	DMASync = 0;
	RDPtr = 0;
	WRPtr = 0;

	return 0;
}

static void sirfsoc_camera_deactivate(struct sirfsoc_camera_dev *pcdev)
{
	struct sirfsoc_camera_platform_data *board = pcdev->pdata;


	pcdev->vip_funcs.pfnTerminate();

	if (board && board->power) {
		dev_dbg(pcdev->dev, "%s: Power off camera\n", __func__);
		board->power(pcdev->dev, 0);
	}

	if (pcdev->pdata && pcdev->pdata->release)
		pcdev->pdata->release(pcdev->dev);

	clk_disable_unprepare(pcdev->clk);
}

/* The following two functions absolutely depend on the fact, that
 * there can be only one camera on sirfsoc quick capture interface */
static int sirfsoc_camera_add_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct sirfsoc_camera_dev *pcdev = ici->priv;
	struct sirfsoc_camera_platform_data *pdata = pcdev->pdata;
	VIP_PARAMS *params = &pcdev->vip_params;

	int ret;

	mutex_lock(&camera_lock);

	if (pcdev->icd) {
		ret = -EBUSY;
		goto err;
	}

	dev_info(icd->pdev, "%s: %d attach to vip\n", __func__,
		icd->devnum);
	/* For switch of camera and tvdecoder device:
	 * two devices are always registered, 0-camera, 1-tvdecoder
	 */
	if (icd->vdev != NULL) {
		if (icd->devnum == 0) {
			dev_info(icd->pdev, "%s: this is a tvdecoder device\n",
				__func__);
			pdata->sirfsoc_camera_ccir656_en = 1;
		} else {
			dev_info(icd->pdev, "%s: this is a camera device\n",
				__func__);
			pdata->sirfsoc_camera_ccir656_en = 0;
		}
	}
	ret = sirfsoc_camera_activate(pcdev);
	if (ret)
		goto err;

	pcdev->active = NULL;
	memset(params, 0, sizeof(*params));

	if (pdata->sirfsoc_camera_pxclk_en)
		params->uiFlag |= VIP_CTRL_PXCLK_CTRL;
	if (pdata->sirfsoc_camera_hsync_en)
		params->uiFlag |= VIP_CTRL_HSYNC_CTRL;
	if (pdata->sirfsoc_camera_vsync_en)
		params->uiFlag |= VIP_CTRL_VSYNC_CTRL;
	if (pdata->sirfsoc_camera_ccir656_en)
		params->uiFlag |= VIP_CTRL_CCIR656_EN;

	/* Vip multiplex USB0 for atlas6 */
#ifdef CONFIG_ARCH_ATLAS6
	params->uiFlag |= VIP_CTRL_PAD_MUX_UPLI;
#endif
	params->PixelBitSelect = pcdev->pdata->sirfsoc_camera_pixel_shift;

	pcdev->icd = icd;
	pcdev->task = current;

err:
	mutex_unlock(&camera_lock);
	return ret;
}

static void sirfsoc_camera_remove_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct sirfsoc_camera_dev *pcdev = ici->priv;

	BUG_ON(icd != pcdev->icd);

	dev_info(icd->pdev, "%s: camera %d detach from vip\n", __func__,
		 icd->devnum);


	sirfsoc_camera_deactivate(pcdev);

	pcdev->icd = NULL;
	pcdev->task = NULL;
}

static int test_platform_param(struct sirfsoc_camera_dev *pcdev,
			       unsigned char buswidth, unsigned int *flags)
{
	/* If requested data width is supported by the platform, use it */
	switch (buswidth) {
	case 8:
		if (!(pcdev->platform_flags & SOCAM_DATAWIDTH_8))
			return -EINVAL;
		*flags |= SOCAM_DATAWIDTH_8;
		break;
	case 9:
		if (!(pcdev->platform_flags & SOCAM_DATAWIDTH_9))
			return -EINVAL;
		*flags |= SOCAM_DATAWIDTH_9;
		break;
	case 10:
		if (!(pcdev->platform_flags & SOCAM_DATAWIDTH_10))
			return -EINVAL;
		*flags |= SOCAM_DATAWIDTH_10;
	case 16:
		if (!(pcdev->platform_flags & SOCAM_DATAWIDTH_16))
			return -EINVAL;
		*flags |= SOCAM_DATAWIDTH_16;
	}

	return 0;
}

static int sirfsoc_camera_set_bus_param(struct soc_camera_device *icd,
					__u32 pixfmt)
{
	struct soc_camera_host *ici =
		to_soc_camera_host(icd->parent);
	struct sirfsoc_camera_dev *pcdev = ici->priv;
	unsigned int bus_flags;
	const struct soc_mbus_pixelfmt *fmt;
	VIP_PARAMS *params = &pcdev->vip_params;
	int ret;

	fmt = soc_mbus_get_fmtdesc(icd->current_fmt->code);
	if (!fmt)
		return -EINVAL;


	ret = test_platform_param(pcdev, fmt->bits_per_sample, &bus_flags);
	if (ret < 0)
		return ret;
#if 0
	params->uiFlag |= VIP_CTRL_HSYNC_INV;
	params->uiFlag |= VIP_CTRL_VSYNC_INV;
#endif
	/* We assume the input data is always UYVY */
	params->eSrcFormat = LCD_PIXELFORMAT_UYVY;

	switch (pixfmt) {
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_YUYV:
		params->eDstFormat = LCD_PIXELFORMAT_UNKNOWN;
		break;
	case V4L2_PIX_FMT_RGB24:
		params->eDstFormat = LCD_PIXELFORMAT_565;
		break;
	case V4L2_PIX_FMT_RGB565:
		params->eDstFormat = LCD_PIXELFORMAT_8880;
		break;
	}


	pcdev->vip_funcs.pfnSetParams(params);

	return 0;
}

static int sirfsoc_camera_set_fmt_cap(struct soc_camera_device *icd,
					struct v4l2_format *f)
{
	struct soc_camera_host *ici =
		to_soc_camera_host(icd->parent);
	struct sirfsoc_camera_dev *pcdev = ici->priv;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	int ret;
	unsigned int width, height, x_start, y_start, x_end, y_end;
	const struct soc_camera_format_xlate *xlate;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mf;
	VIP_PARAMS *params = &pcdev->vip_params;
	RECT  src_rect;

	int buswidth = soc_mbus_bytes_per_line(1,
		icd->current_fmt->host_fmt);


	xlate = soc_camera_xlate_by_fourcc(icd, pix->pixelformat);
	if (!xlate) {
		dev_warn(icd->pdev, "%s: format %x not found\n",
			 __func__, pix->pixelformat);
		return -EINVAL;
	}

	mf.width	= pix->width;
	mf.height	= pix->height;
	mf.field	= pix->field;
	mf.colorspace	= pix->colorspace;
	mf.code		= xlate->code;

	ret = v4l2_subdev_call(sd, video, s_mbus_fmt, &mf);

	if (ret)
		return ret;

	icd->current_fmt = xlate;

	width = pix->width * buswidth;
	height = pix->height;
	x_start = 0;
	x_end = width + x_start - 1;
	y_start = 0;
	if (pcdev->pdata->sirfsoc_camera_ccir656_en)
		y_end = height / 2 + y_start - 1;
	else
		y_end = height + y_start - 1;

	dev_dbg(icd->pdev,
		"%s: x_start %d x_end %d y_start %d y_end %d\n",
		__func__, x_start, x_end, y_start, y_end);

	printk("pix->width = %x,buswidth = %x\n",width,buswidth);

	src_rect.left = x_start;
	src_rect.right = x_end;
	src_rect.top = y_start;
	src_rect.bottom = y_end;

	params->SrcRect = src_rect;

	return 0;
}

static int sirfsoc_camera_try_fmt_cap(struct soc_camera_device *icd,
				  struct v4l2_format *f)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mf;
	const struct soc_camera_format_xlate *xlate;
	__u32 pixfmt = pix->pixelformat;
	int ret;

	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	if (!xlate) {
		dev_warn(icd->pdev, "%s: format %x not found\n",
			__func__, pixfmt);
		return -EINVAL;
	}


	mf.width	= pix->width;
	mf.height	= pix->height;
	mf.field	= pix->field;
	mf.colorspace	= pix->colorspace;
	mf.code		= xlate->code;
	/* limit to sensor capabilities */
	ret = v4l2_subdev_call(sd, video, try_mbus_fmt, &mf);
	if (ret < 0)
		return ret;

	pix->width	= mf.width;
	pix->height	= mf.height;
	pix->colorspace	= mf.colorspace;

	switch (mf.field) {
	case V4L2_FIELD_ANY:
	case V4L2_FIELD_NONE:
		pix->field	= V4L2_FIELD_NONE;
		break;
	default:
		/* TODO: support interlaced at least in pass-through mode */
		dev_err(icd->pdev, "%s: field type %d unsupported.\n",
			__func__, mf.field);
		return -EINVAL;
	}

	return ret;

}

static int sirfsoc_camera_reqbufs(struct soc_camera_device *icd,
			      struct v4l2_requestbuffers *p)
{
	int i;

	/* This is for locking debugging only. I removed spinlocks and now I
	 * check whether .prepare is ever called on a linked buffer, or whether
	 * a dma IRQ can occur for an in-work or unlinked buffer. Until now
	 * it hadn't triggered */
	for (i = 0; i < p->count; i++) {
		struct sirfsoc_buffer *buf =
			container_of(icd->vb_vidq.bufs[i],
				struct sirfsoc_buffer, vb);
		INIT_LIST_HEAD(&buf->vb.queue);
	}

	return 0;
}

static unsigned int sirfsoc_camera_poll(struct file *file, poll_table *pt)
{
	struct soc_camera_device *icd = file->private_data;
	struct sirfsoc_buffer *buf;

	buf = list_entry(icd->vb_vidq.stream.next, struct sirfsoc_buffer,
			 vb.stream);
	poll_wait(file, &buf->vb.done, pt);
	if (buf->vb.state == VIDEOBUF_DONE ||
	    buf->vb.state == VIDEOBUF_ERROR)
		return POLLIN|POLLRDNORM;

	return 0;
}


static int sirfsoc_camera_querycap(struct soc_camera_host *ici,
			       struct v4l2_capability *cap)
{
	/* cap->name is set by the firendly caller:-> */
	strlcpy(cap->card, sirfsoc_cam_driver_description, sizeof(cap->card));
	cap->version = SIRFSOC_CAM_VERSION_CODE;
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;

	return 0;
}

int sirfsoc_camera_get_crop(struct soc_camera_device *icd,
				struct v4l2_crop *crop)
{
	return 0;
}
int sirfsoc_camera_set_crop(struct soc_camera_device *icd,
				struct v4l2_crop *crop)
{
	return 0;
}


static struct soc_camera_host_ops sirfsoc_soc_camera_host_ops = {
	.owner		= THIS_MODULE,
	.add		= sirfsoc_camera_add_device,
	.remove		= sirfsoc_camera_remove_device,
	.set_fmt	= sirfsoc_camera_set_fmt_cap,
	.try_fmt	= sirfsoc_camera_try_fmt_cap,
	.init_videobuf	= sirfsoc_camera_init_videobuf,
	.reqbufs	= sirfsoc_camera_reqbufs,
	.poll		= sirfsoc_camera_poll,
	.querycap	= sirfsoc_camera_querycap,
	.set_bus_param	= sirfsoc_camera_set_bus_param,
	.get_crop	= sirfsoc_camera_get_crop,
	.set_crop	= sirfsoc_camera_set_crop,
};

/* Should be allocated dynamically too, but we have only one. */
static struct soc_camera_host sirfsoc_soc_camera_host = {
	.drv_name		= SIRFSOC_CAM_DRV_NAME,
	.ops			= &sirfsoc_soc_camera_host_ops,
};

static irqreturn_t sirfsoc_camera_irq(int irq, void *data)
{
	struct sirfsoc_camera_dev *pcdev = data;
	u32 status;
	u32 temp;

	status = pcdev->vip_funcs.pfnGetInterrupts();
	pcdev->vip_funcs.pfnClearInterrupts(status);

	if (status & VIP_INTMASK_SENSOR) {
		dev_dbg(pcdev->dev, "sensor interrupt happens\n");
		if (pcdev->pdata->sirfsoc_camera_ccir656_en) {
			temp = pcdev->vip_funcs.pfnGetFID();

			if (IfOdd[(WRPtr + 3) % 4] == temp) {
				dev_err(pcdev->dev, "camera field bad sequence\n");
				DMASync = 0;
			}
			IfOdd[WRPtr] = temp;
			WRPtr = (WRPtr + 1) % 4;
		}
	}
	if (status & VIP_INTMASK_FIFO_OFLOW) {
		dev_err(pcdev->dev, "FIFO overflow interrupt happens\n");
		if (pcdev->pdata->sirfsoc_camera_ccir656_en)
			DMASync = 0;
	}
	if (status & VIP_INTMASK_FIFO_UFLOW)
		dev_err(pcdev->dev, "FIFO underflow interrupt happens\n");

	return IRQ_HANDLED;
}

static void sirfsoc_vip_save_context(void *data)
{
	struct sirfsoc_camera_dev *pcdev = data;

	if (pcdev->task == NULL) {
		free_irq(pcdev->irq, pcdev);
		return;
	}

	send_sig(SIGSTOP, pcdev->task, 0);

	while (!task_is_stopped(pcdev->task))
		msleep(20);

	pcdev->decoder_ops->stop();

	free_irq(pcdev->irq, pcdev);
	pcdev->vip_funcs.pfnStop();
#if 0
	sirfsoc_disable_dma(pcdev->dma_chan);
#endif

	sirfsoc_camera_deactivate(pcdev);
}

static void sirfsoc_vip_restore_context(void *data)
{
	struct sirfsoc_camera_dev *pcdev = data;

	if (request_irq(pcdev->irq, sirfsoc_camera_irq,
			0, SIRFSOC_CAM_DRV_NAME, pcdev)) {
		dev_err(pcdev->dev, "%s: request_irq error for VIP\n",
			__func__);
		return;
	}

	if (pcdev->task == NULL)
		return;

	sirfsoc_camera_activate(pcdev);

	pcdev->vip_funcs.pfnSetParams(&pcdev->vip_params);

	if (pcdev->pdata->sirfsoc_camera_ccir656_en)
		pcdev->decoder_ops->start(INPUT_YC);
	else
		/*TODO, how to restart camera */

	if (pcdev->active)
		sirfsoc_camera_start_dma_channel(pcdev, 1);

	send_sig(SIGCONT, pcdev->task, 0);
}

int platform_camera_init(struct device *cam_device)
{
	struct sirfsoc_camera_platform_data *pdata = cam_device->platform_data;
	static int GPIO_Cam_Reset, GPIO_Cam_Power;
	int ret = 0;

	if (GPIO_Cam_Power == 0) {
		ret = gpio_request(pdata->power_gpio, "camera power control");
		if (ret) {
			dev_err(cam_device, "failed to request GPIO%d\n",
							pdata->power_gpio);
			goto out;
		}
		if (gpio_is_valid(pdata->vip_power_gpio)) {
			ret = gpio_request(pdata->vip_power_gpio,
							"vip power control");
			if (ret) {
				dev_err(cam_device, "failed to request GPIO%d\n",
							pdata->vip_power_gpio);
				goto err_power_gpio_free;
			}
		}
		GPIO_Cam_Power++;
	}

	if (GPIO_Cam_Reset == 0 && gpio_is_valid(pdata->reset_gpio)) {
		ret = gpio_request(pdata->reset_gpio, "camera reset control");
		if (ret) {
			dev_err(cam_device, "failed to request GPIO%d\n",
							pdata->reset_gpio);
			goto err_vip_power_gpio_free;
		}
		GPIO_Cam_Reset++;
	}

	return ret;

err_reset_gpio_free:
	gpio_free(pdata->reset_gpio);
err_vip_power_gpio_free:
	if (gpio_is_valid(pdata->vip_power_gpio))
		gpio_free(pdata->vip_power_gpio);
err_power_gpio_free:
	gpio_free(pdata->power_gpio);
	GPIO_Cam_Power = 0;
	GPIO_Cam_Reset = 0;
out:
	return ret;
}

int platform_camera_power(struct device *cam_device, int on)
{
	struct sirfsoc_camera_platform_data *pdata = cam_device->platform_data;

	gpio_direction_output(pdata->power_gpio, on ? 1 : 0);
	if (gpio_is_valid(pdata->vip_power_gpio))
		gpio_direction_output(pdata->vip_power_gpio, !!on);

	return 0;
}

int platform_camera_reset(struct device *cam_device)
{
	struct sirfsoc_camera_platform_data *pdata = cam_device->platform_data;

	if (gpio_is_valid(pdata->reset_gpio)) {
		gpio_direction_output(pdata->reset_gpio, 0);
		msleep(20);
		gpio_direction_output(pdata->reset_gpio, 1);
		/* Wait 20 ms until the reset is done */
		msleep(20);
	}

	return 0;
}

int platform_camera_release(struct device *cam_device)
{
	return 0;
}

static struct sirfsoc_camera_platform_data sirfsoc_platform_camera_data = {
	.init = platform_camera_init,
	.power = platform_camera_power,
	.reset = platform_camera_reset,
	.release = platform_camera_release,

	/* The vip controller supports both 8-bit and 16-bit */
	.flags = SOCAM_DATAWIDTH_8 | SOCAM_DATAWIDTH_16,

	.sirfsoc_cam_yuv_coef1 = 0x12a00198,
	.sirfsoc_cam_yuv_coef2 = 0x12a190d0,
	.sirfsoc_cam_yuv_coef3 = 0x12a81000,
	.sirfsoc_cam_yuv_offset = 0x115220df,

	.sirfsoc_camera_pixel_shift = 0x01,
	.sirfsoc_camera_pxclk_en = 0,
	.sirfsoc_camera_hsync_en = 0,
	.sirfsoc_camera_vsync_en = 0,
	.sirfsoc_camera_ccir656_en = 0,
};

void  __init sirfsoc_vip_reserve_memblock(void)
{
	sirf_vip_phy_size = 12 * SZ_1M;
	sirf_vip_phy_base = memblock_alloc(sirf_vip_phy_size, PAGE_SIZE);
	memblock_remove(sirf_vip_phy_base, sirf_vip_phy_size);
}
EXPORT_SYMBOL(sirfsoc_vip_reserve_memblock);

static void sirfsoc_camera_probe_async(void *async_data, async_cookie_t cookie)
{
	struct platform_device *pdev = async_data;
	struct sirfsoc_camera_dev *pcdev;
	struct resource *res;
	void __iomem *base;
	u32 dma_ch;
	dma_cap_mask_t dma_cap_mask;
	int irq;
	int ret = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "%s: fail to get vip regs resource\n",
			__func__);
		ret = -EINVAL;
		goto exit;
	}

	irq = platform_get_irq(pdev, 0);
	if (!irq) {
		dev_err(&pdev->dev, "%s: fail to get vip irq\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	pcdev = devm_kzalloc(&pdev->dev, sizeof(*pcdev), GFP_KERNEL);
	if (!pcdev) {
		dev_err(&pdev->dev, "%s: fail to allocate pcdev\n", __func__);
		ret = -ENOMEM;
		goto exit;
	}

	pcdev->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(pcdev->clk)) {
		dev_err(&pdev->dev, "%s: fail to get vip clock\n", __func__);
		ret = -EINVAL;
		goto exit_kfree;
	}

	sirfsoc_platform_camera_data.vip_power_gpio =
		of_get_named_gpio(pdev->dev.of_node, "vip-power-gpio", 0);
	sirfsoc_platform_camera_data.power_gpio =
		of_get_named_gpio(pdev->dev.of_node, "power-gpio", 0);
	sirfsoc_platform_camera_data.reset_gpio =
		of_get_named_gpio(pdev->dev.of_node, "reset-gpio", 0);
	pdev->dev.platform_data = &sirfsoc_platform_camera_data;

	pcdev->res = res;
	pcdev->pdata = pdev->dev.platform_data;
	pcdev->platform_flags = pcdev->pdata->flags;
	dev_dbg(&pdev->dev, "%s: the flags is %#x\n",
		__func__, pcdev->platform_flags);
	INIT_LIST_HEAD(&pcdev->capture);
	spin_lock_init(&pcdev->lock);

	base = devm_request_and_ioremap(&pdev->dev, res);
	if (!base) {
		dev_err(&pdev->dev, "%s: fail to remap vip regs\n", __func__);
		ret = -ENOMEM;
		goto exit_clk;
	}

	pcdev->irq = irq;
	pcdev->base = base;
	pcdev->dev = &pdev->dev;

	dev_info(pcdev->dev, "%s: dma mem=%x:%x\n", __func__,
		 sirf_vip_phy_base,  sirf_vip_phy_size);
	ret = dma_declare_coherent_memory(&pdev->dev, sirf_vip_phy_base,
					  sirf_vip_phy_base,
					  10 * SZ_1M,
					  DMA_MEMORY_MAP |
					  DMA_MEMORY_EXCLUSIVE);
	if (!ret) {
		dev_err(&pdev->dev, "%s: unable to declare dma memory.\n",
			__func__);
		ret = -ENXIO;
		goto exit_iounmap;
	}

	pcdev->video_limit =  10 * SZ_1M;
	pcdev->dma_addr = sirf_vip_phy_base + 10 * SZ_1M;

	pcdev->dma_xt = kzalloc(sizeof(struct dma_interleaved_template) +
		sizeof(struct data_chunk), GFP_KERNEL);
	if (!pcdev->dma_xt) {
		ret = -ENOMEM;
		goto exit_release_mem;
	}

	/* request dma channel */
	ret = of_property_read_u32(pdev->dev.of_node,
			"sirf,vip-dma-rx-channel", &dma_ch);
	if (ret) {
		dev_err(&pdev->dev, "%s: Unable to get vip dma channel\n",
			__func__);
		goto exit_free_dma_xt;
	}

	dma_cap_zero(dma_cap_mask);
	dma_cap_set(DMA_INTERLEAVE, dma_cap_mask);

	pcdev->dma_chan = dma_request_channel(dma_cap_mask,
					      (dma_filter_fn)sirfsoc_dma_filter_id,
					      (void *)dma_ch);
	if (!pcdev->dma_chan) {
		dev_err(&pdev->dev, "%s: can not allocate vip dma channel\n",
			__func__);
		goto exit_free_dma_xt;
	}


	pcdev->p = pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(pcdev->p))
		goto exit_free_dma;

	VIP_GetFuncTable(&pcdev->vip_funcs);

	sirfsoc_soc_camera_host.priv		= pcdev;
	sirfsoc_soc_camera_host.v4l2_dev.dev	= &pdev->dev;
	sirfsoc_soc_camera_host.nr		= 0;
	ret = soc_camera_host_register(&sirfsoc_soc_camera_host);
	if (ret)
		goto exit_free_irq;

	pcdev->save_vip_context = sirfsoc_vip_save_context;
	pcdev->restore_vip_context = sirfsoc_vip_restore_context;
	return;

exit_host_unregister:
	soc_camera_host_unregister(&sirfsoc_soc_camera_host);
exit_free_irq:
	free_irq(pcdev->irq, pcdev);
exit_free_pin:
	pinctrl_put(pcdev->p);
exit_free_dma:
	dma_release_channel(pcdev->dma_chan);
exit_free_dma_xt:
	kfree(pcdev->dma_xt);
exit_release_mem:
	dma_release_declared_memory(&pdev->dev);
exit_iounmap:
	iounmap(base);
exit_clk:
	clk_put(pcdev->clk);
exit_kfree:
	devm_kfree(&pdev->dev, pcdev);
exit:
	return;
}

static int sirfsoc_camera_probe(struct platform_device *pdev)
{
	async_schedule(sirfsoc_camera_probe_async, pdev);
	return 0;
}

static int sirfsoc_camera_remove(struct platform_device *pdev)
{
	struct soc_camera_host *soc_host = to_soc_camera_host(&pdev->dev);
	struct sirfsoc_camera_dev *pcdev = soc_host->priv;

	dev_info(&pdev->dev, "%s\n", __func__);

	soc_camera_host_unregister(&sirfsoc_soc_camera_host);
	clk_put(pcdev->clk);
	dma_release_channel(pcdev->dma_chan);
	free_irq(pcdev->irq, pcdev);
	dma_release_declared_memory(&pdev->dev);
	kfree(pcdev->dma_xt);
	iounmap(pcdev->base);
	pinctrl_put(pcdev->p);

	devm_kfree(&pdev->dev, pcdev);

	return 0;
}

#ifdef CONFIG_PM
static int sirfsoc_cam_pm_suspend(struct device *dev)
{
	dev_info(dev, "%s\n", __func__);

	return 0;
}

static int sirfsoc_cam_pm_resume(struct device *dev)
{
	dev_info(dev, "%s\n", __func__);

	return 0;
}
static int sirfsoc_cam_pm_freeze(struct device *dev)
{
	dev_info(dev, "%s\n", __func__);

	return 0;
}
static int sirfsoc_cam_pm_restore(struct device *dev)
{
	dev_info(dev, "%s\n", __func__);

	return 0;
}
#else
#define sirfsoc_cam_pm_suspend   NULL
#define sirfsoc_cam_pm_resume    NULL
#define sirfsoc_cam_pm_freeze    NULL
#define sirfsoc_cam_pm_restore   NULL
#endif

static const struct dev_pm_ops sirfsoc_cam_pm_ops = {
	.freeze = sirfsoc_cam_pm_freeze,
	.restore = sirfsoc_cam_pm_restore,
	.suspend = sirfsoc_cam_pm_suspend,
	.resume = sirfsoc_cam_pm_resume,
};

static struct of_device_id sirfsoc_cam_match_tbl[] = {
	{ .compatible = "sirf,prima2-vip", },
	{ /* end */ }
};

static struct platform_driver sirfsoc_camera_driver = {
	.driver		= {
		.name = SIRFSOC_CAM_DRV_NAME,
		.pm = &sirfsoc_cam_pm_ops,
		.of_match_table = sirfsoc_cam_match_tbl,
	},
	.probe = sirfsoc_camera_probe,
	.remove = sirfsoc_camera_remove,
};

module_platform_driver(sirfsoc_camera_driver);

MODULE_DESCRIPTION("sirfsoc SoC Camera Host driver(VIP interface)");
MODULE_AUTHOR("Guennadi Liakhovetski <kernel@pengutronix.de>");
MODULE_LICENSE("GPL");
