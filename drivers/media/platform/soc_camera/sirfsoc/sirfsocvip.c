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
#include <linux/slab.h>
#include <linux/pm_qos.h>

#include <asm/dma.h>
#include <media/sirfsoc_v4l2.h>

#include "sirfsocvip.h"
#include "rearview.h"

#ifndef MODULE
#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX
#endif

static DEFINE_MUTEX(camera_lock);

#define SIRFSOC_CAM_VERSION_CODE KERNEL_VERSION(0, 0, 5)
#define SIRFSOC_CAM_DRV_NAME "sirfsoc-vip"

static const char *sirfsoc_cam_driver_description = SIRFSOC_CAM_DRV_NAME;

static bool rearview = true;
module_param(rearview, bool, S_IRUGO);
MODULE_PARM_DESC(rearview, "to launch rearview thread.");

static struct task_struct *rearview_task;
static LIST_HEAD(decoder_list);

static phys_addr_t sirf_vip_phy_base;
static phys_addr_t sirf_vip_phy_size;
static int brestart;

static struct pm_qos_request qos_cpufreq_min_req;

static inline bool need_launch_rearview(struct sirfsoc_camera_dev *pcdev)
{
	return !rearview ? false : (pcdev->rearview_decoder_ops &&
		!pcdev->rearview_decoder_ops->detect());
}

int sirfsoc_register_decoder_ops(struct sirfsoc_decoder_ops *decoder_ops)
{
	WARN_ON(decoder_ops == NULL);

	list_add_tail(&decoder_ops->list, &decoder_list);

	return 0;
}
EXPORT_SYMBOL(sirfsoc_register_decoder_ops);


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
		dmaengine_terminate_all(pcdev->dma_chan);
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

static int sirfsoc_camera_start_dma(
	struct sirfsoc_camera_dev *pcdev, int resetfifo);

static void sirfsoc_camera_callback (void *pdata) {
	struct videobuf_buffer *vb = NULL;
	struct sirfsoc_camera_dev *pcdev = (struct sirfsoc_camera_dev*)pdata;
	unsigned long flags;

	dev_dbg(pcdev->dev, "%s\n", __func__);
	spin_lock_irqsave(&pcdev->lock, flags);

	if (!pcdev) {
		spin_unlock_irqrestore(&pcdev->lock, flags);
		return;
	}

	if (brestart) {
		sirfsoc_camera_start_dma(pcdev, 1);
		brestart = 0;
		spin_unlock_irqrestore(&pcdev->lock, flags);
		return;
	}

	vb = pcdev->active;
	if (vb == NULL) {
		spin_unlock_irqrestore(&pcdev->lock, flags);
		return;
	}

	/*
	 * if rearview switch has taken place in hardware, not add the
	 * active buffer to outgoing queue. The dma content may have
	 * been overidden now.
	 */
	if (rearview_task == NULL ||
		!gpio_get_value(pcdev->rearview_gpio)) {

		list_del_init(&vb->queue);
		vb->state = VIDEOBUF_DONE;
		wake_up(&vb->done);

		if (!list_empty(&pcdev->capture)) {
			pcdev->active = list_entry(pcdev->capture.next,
				struct videobuf_buffer, queue);
		} else {
			pcdev->active = NULL;
			pcdev->vip_funcs.pfnStop();
		}
	}

	if (pcdev->active != NULL) {
		struct dma_async_tx_descriptor *rx_desc;

		pcdev->active->state = VIDEOBUF_ACTIVE;
		pcdev->dma_xt->dst_start = videobuf_to_dma_contig(pcdev->active);

		rx_desc = dmaengine_prep_interleaved_dma(pcdev->dma_chan, pcdev->dma_xt, 0);
	        rx_desc->callback = sirfsoc_camera_callback;
	        rx_desc->callback_param = pcdev;

	        dmaengine_submit(rx_desc);
	        dma_async_issue_pending(pcdev->dma_chan);

		if (pcdev->pdata->sirfsoc_camera_single)
			pcdev->vip_funcs.pfnStart(0);
	}

	spin_unlock_irqrestore(&pcdev->lock, flags);
}

static int sirfsoc_camera_start_dma(
	struct sirfsoc_camera_dev *pcdev, int resetfifo)
{
	struct videobuf_buffer *vb = pcdev->active;
	struct dma_async_tx_descriptor *rx_desc;

	if (vb == NULL) {
		pcdev->vip_funcs.pfnStop();
		mdelay(1);
		dmaengine_terminate_all(pcdev->dma_chan);
		return -EINVAL;
	}

	vb->state = VIDEOBUF_ACTIVE;
	if (resetfifo)
		pcdev->vip_funcs.pfnStop();

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
		sirfsoc_camera_start_dma(pcdev, 1);
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
	pm_qos_update_request(&qos_cpufreq_min_req,
		PM_QOS_CPU_FREQ_MAX_DEFAULT_VALUE);

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
	pm_qos_update_request(&qos_cpufreq_min_req, PM_QOS_DEFAULT_VALUE);
}

/* The following two functions absolutely depend on the fact, that
 * there can be only one camera on sirfsoc quick capture interface */
static int sirfsoc_camera_add_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct sirfsoc_camera_dev *pcdev = ici->priv;
	struct sirfsoc_camera_platform_data *pdata = pcdev->pdata;
	VIP_PARAMS *params = &pcdev->vip_params;

	struct sirfsoc_decoder_ops *decoder_ops;
	int ret, i = 0;

	mutex_lock(&camera_lock);

	if (pcdev->icd || (pcdev->rearview_enabled != NULL &&
			pcdev->rearview_enabled())) {
		ret = -EBUSY;
		goto err;
	}

	dev_info(icd->pdev, "%s: %d attach to vip\n", __func__,
		icd->devnum);
	/*  currently only two devices are registered, 1-tvdecoder, 1-hdmi
	    receiver  */
	if (icd->vdev != NULL) {
		if (icd->devnum == 0) {
			dev_info(icd->pdev, "%s: this is a tvdecoder device\n",
				__func__);
			pdata->sirfsoc_camera_ccir656_en = 1;
			pdata->sirfsoc_camera_single = 0;
		} else if (icd->devnum == 1) {
			dev_info(icd->pdev, "%s: this is a HDMI receiver device\n",
				__func__);
			pdata->sirfsoc_camera_ccir656_en = 1;
			pdata->sirfsoc_camera_single = 0;
		} else {
			dev_info(icd->pdev, "%s: unsupported device\n",
				__func__);
			pdata->sirfsoc_camera_ccir656_en = 0;
		}

		list_for_each_entry(decoder_ops, &decoder_list, list) {
			if (i++ == icd->devnum)
				break;
		}
		pcdev->vip_decoder_ops = decoder_ops;
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
	if (pdata->sirfsoc_camera_single)
		params->uiFlag |= VIP_CTRL_SINGLE_MODE;

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

/*need remove the pixfmt from the arg */
static int sirfsoc_camera_set_bus_param(struct soc_camera_device *icd)
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

	params->uiFlag |= VIP_CTRL_HSYNC_INV;
	params->uiFlag |= VIP_CTRL_VSYNC_INV;

	/* We assume the input data is always UYVY */
	params->eSrcFormat = LCD_PIXELFORMAT_UYVY;
	params->eDstFormat = LCD_PIXELFORMAT_UNKNOWN;

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

	if (pcdev->pdata->sirfsoc_camera_interlaced)
		y_end = height / 2 + y_start - 1;
	else
		y_end = height + y_start - 1;

	dev_dbg(icd->pdev,
		"%s: x_start %d x_end %d y_start %d y_end %d\n",
		__func__, x_start, x_end, y_start, y_end);

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

static struct soc_camera_device *ctrl_to_icd(struct v4l2_ctrl *ctrl)
{
        return container_of(ctrl->handler, struct soc_camera_device,
                                                        ctrl_handler);
}

static int sirfsoc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct soc_camera_device *icd = ctrl_to_icd(ctrl);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct sirfsoc_camera_dev *pcdev = ici->priv;
	struct videobuf_queue *q;
	int index;

        switch (ctrl->id) {
	case V4L2_CID_GET_ADDR:
                q = &icd->vb_vidq;
                index = ctrl->val;

                if (index < 0 || index > VIDEO_MAX_FRAME - 1)
                        return -EINVAL;

                if (q->bufs[index] == NULL ||
                        q->bufs[index]->map == NULL)
                        return -EINVAL;

                ctrl->val = videobuf_to_dma_contig(q->bufs[index]);
                break;

	case V4L2_CID_SET_INTERLACE:
		if (ctrl->val)
			pcdev->pdata->sirfsoc_camera_interlaced = 1;
		else
			pcdev->pdata->sirfsoc_camera_interlaced = 0;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int sirfsoc_g_ctrl(struct v4l2_ctrl *ctrl)
{
	struct soc_camera_device *icd = ctrl_to_icd(ctrl);
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct videobuf_queue *q;
	struct v4l2_control control;
	int index;
	unsigned int status = 0;
	int ret = 0;
	unsigned int value = 0;

	switch (ctrl->id) {
	case V4L2_CID_GET_ADDR:
		q = &icd->vb_vidq;
		index = ctrl->val;

		if (index < 0 || index > VIDEO_MAX_FRAME - 1)
			return -EINVAL;

		if (q->bufs[index] == NULL ||
			q->bufs[index]->map == NULL)
			return -EINVAL;

		ctrl->val = videobuf_to_dma_contig(q->bufs[index]);
		break;

	case V4L2_CID_GET_VIDEO_STATE:
		ret = v4l2_subdev_call(sd, video, g_input_status, &status);
		if (ret < 0)
			return -EINVAL;
		ctrl->val = 1;
		if (!status)
			ctrl->val = 0;
		break;
	case V4L2_CID_GET_AUDIO_SAMPLE_RATE:
		control.id = ctrl->id;
		ret = v4l2_subdev_call(sd, core, g_ctrl, &control);
		if (ret < 0)
			return -EINVAL;
		ctrl->val = control.value;
		break;
        default:
                return -EINVAL;
        }
        return 0;
	return 0;
}

static const struct v4l2_ctrl_ops sirfsoc_vip_ctrl_ops = {
	.s_ctrl = sirfsoc_s_ctrl,
	.g_volatile_ctrl = sirfsoc_g_ctrl,
};

static const struct v4l2_ctrl_config sirfsoc_ctrl_get_addr = {
	.ops = &sirfsoc_vip_ctrl_ops,
	.id = V4L2_CID_GET_ADDR,
	.name = "get videobuf physical address",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.def = 1,
	.min = 0,
	.max = 4,
	.step = 1,
};

static const struct v4l2_ctrl_config sirfsoc_ctrl_set_interlace = {
	.ops = &sirfsoc_vip_ctrl_ops,
	.id = V4L2_CID_SET_INTERLACE,
	.name = "set interlace flag",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.def = 1,
	.min = 0,
	.max = 1,
	.step = 1,
};

static const struct v4l2_ctrl_config sirfsoc_ctrl_get_video_state = {
	.ops = &sirfsoc_vip_ctrl_ops,
	.id = V4L2_CID_GET_VIDEO_STATE,
	.name = "get video state",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.def = 1,
	.min = 0,
	.max = 1,
	.step = 1,
	.flags = V4L2_CTRL_FLAG_VOLATILE,
};

static const struct v4l2_ctrl_config sirfsoc_ctrl_get_audio_sample_rate = {
	.ops = &sirfsoc_vip_ctrl_ops,
	.id = V4L2_CID_GET_AUDIO_SAMPLE_RATE,
	.name = "get audio sample rate",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.def = 1,
	.min = 0,
	.max = 1,
	.step = 1,
	.flags = V4L2_CTRL_FLAG_VOLATILE,
};
static const struct soc_mbus_pixelfmt sirfsoc_camera_formats[] = {
	{
		.fourcc			= V4L2_PIX_FMT_UYVY,
		.name			= "Packed YUV422 16 bit",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_LE,
		.layout			= SOC_MBUS_LAYOUT_PACKED,
	},
};

static int sirfsoc_camera_get_formats(struct soc_camera_device *icd,
	unsigned int idx, struct soc_camera_format_xlate *xlate) {
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct device *dev = icd->parent;
	int formats = 0, ret;
	enum v4l2_mbus_pixelcode code;
	const struct soc_mbus_pixelfmt *fmt;

	ret = v4l2_subdev_call(sd, video, enum_mbus_fmt, idx, &code);
	if (ret < 0)
		/* No more formats */
		return 0;

	fmt = soc_mbus_get_fmtdesc(code);
	if (!fmt) {
		dev_err(dev, "Invalid format code #%u: %d\n", idx, code);
		return 0;
	}

	if (!icd->host_priv) {
		icd->host_priv = icd;
		v4l2_ctrl_new_custom(&icd->ctrl_handler,
			&sirfsoc_ctrl_set_interlace, NULL);
		if (icd->ctrl_handler.error)
			return icd->ctrl_handler.error;
		v4l2_ctrl_new_custom(&icd->ctrl_handler,
			&sirfsoc_ctrl_get_addr, NULL);
		if (icd->ctrl_handler.error)
			return icd->ctrl_handler.error;
		v4l2_ctrl_new_custom(&icd->ctrl_handler,
			&sirfsoc_ctrl_get_video_state, NULL);
		if (icd->ctrl_handler.error)
			return icd->ctrl_handler.error;
		v4l2_ctrl_new_custom(&icd->ctrl_handler,
			&sirfsoc_ctrl_get_audio_sample_rate, NULL);
		if (icd->ctrl_handler.error)
			return icd->ctrl_handler.error;
	}

	switch (code) {
	case V4L2_MBUS_FMT_UYVY8_2X8:
		formats++;
		if (xlate) {
			xlate->host_fmt = &sirfsoc_camera_formats[0];
			xlate->code	= code;
			dev_dbg(dev, "Providing format %s using code %d\n",
				sirfsoc_camera_formats[0].name, code);
		}
		return formats;
	default:
		return 0;
	}

}

static int sirfsoc_camera_reqbufs(struct soc_camera_device *icd,
			      struct v4l2_requestbuffers *p) {
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
				const struct v4l2_crop *crop)
{
	return 0;
}


static struct soc_camera_host_ops sirfsoc_soc_camera_host_ops = {
	.owner		= THIS_MODULE,
	.add		= sirfsoc_camera_add_device,
	.remove		= sirfsoc_camera_remove_device,
	.get_formats	= sirfsoc_camera_get_formats,
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

	status = pcdev->vip_funcs.pfnGetInterrupts();
	pcdev->vip_funcs.pfnClearInterrupts(status);

	if (status & VIP_INTMASK_SENSOR) {
		dev_dbg(pcdev->dev, "sensor interrupt happens\n");
	}
	if (status & VIP_INTMASK_FIFO_OFLOW) {
		/* dev_info(pcdev->dev, "FIFO overflow interrupt happens\n"); */
		brestart = 1;
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

	pcdev->vip_decoder_ops->stop();

	free_irq(pcdev->irq, pcdev);
	pcdev->vip_funcs.pfnStop();

	dmaengine_terminate_all(pcdev->dma_chan);

	sirfsoc_camera_deactivate(pcdev);
}

static void sirfsoc_vip_restore_context(void *data)
{
	struct sirfsoc_camera_dev *pcdev = data;

	if (devm_request_irq(pcdev->dev, pcdev->irq, sirfsoc_camera_irq,
			0, SIRFSOC_CAM_DRV_NAME, pcdev)) {
		dev_err(pcdev->dev, "%s: request_irq error for VIP\n",
			__func__);
		return;
	}

	if (pcdev->task == NULL)
		return;

	sirfsoc_camera_activate(pcdev);

	pcdev->vip_funcs.pfnSetParams(&pcdev->vip_params);

	if (strcmp(pcdev->vip_decoder_ops->desc, "tw9900") == 0)
		pcdev->vip_decoder_ops->start(INPUT_CVBS_AIN1);
	else
		pcdev->vip_decoder_ops->start(INPUT_ANY);

	if (pcdev->active)
		sirfsoc_camera_start_dma(pcdev, 1);

	send_sig(SIGCONT, pcdev->task, 0);
}

int sirfsoc_camera_init(struct device *cam_device)
{
	return 0;
}

int sirfsoc_camera_power(struct device *cam_device, int on)
{
	return 0;
}

int sirfsoc_camera_reset(struct device *cam_device)
{
	return 0;
}

int sirfsoc_camera_release(struct device *cam_device)
{
	return 0;
}

static struct sirfsoc_camera_platform_data sirfsoc_platform_camera_data = {
	.init = sirfsoc_camera_init,
	.power = sirfsoc_camera_power,
	.reset = sirfsoc_camera_reset,
	.release = sirfsoc_camera_release,

	/* The vip controller supports both 8-bit and 16-bit */
	.flags = SOCAM_DATAWIDTH_8,

	.sirfsoc_cam_yuv_coef1 = 0x12a00198,
	.sirfsoc_cam_yuv_coef2 = 0x12a190d0,
	.sirfsoc_cam_yuv_coef3 = 0x12a81000,
	.sirfsoc_cam_yuv_offset = 0x115220df,

	.sirfsoc_camera_pixel_shift = 0x01,
	.sirfsoc_camera_pxclk_en = 0,
	.sirfsoc_camera_hsync_en = 0,
	.sirfsoc_camera_vsync_en = 0,
	.sirfsoc_camera_ccir656_en = 0,
	.sirfsoc_camera_interlaced = 1,
	.sirfsoc_camera_single = 0,
};

void  __init sirfsoc_vip_reserve_memblock(void)
{
	sirf_vip_phy_size = 12 * SZ_1M;
	sirf_vip_phy_base = memblock_alloc(sirf_vip_phy_size, SZ_1M);
	memblock_remove(sirf_vip_phy_base, sirf_vip_phy_size);
}
EXPORT_SYMBOL(sirfsoc_vip_reserve_memblock);

static void sirfsoc_camera_probe_async(void *async_data, async_cookie_t cookie)
{
	struct platform_device *pdev = async_data;
	struct sirfsoc_camera_dev *pcdev;
	struct resource *res;
	struct sirfsoc_decoder_ops *decoder_ops;
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

	pcdev = devm_kzalloc(&pdev->dev, sizeof(*pcdev), GFP_KERNEL);
	if (!pcdev) {
		dev_err(&pdev->dev, "%s: fail to allocate pcdev\n", __func__);
		ret = -ENOMEM;
		goto exit;
	}

	irq = platform_get_irq(pdev, 0);
	if (!irq) {
		dev_err(&pdev->dev, "%s: fail to get vip irq\n", __func__);
		ret = -EINVAL;
		goto exit_kfree;
	}

	ret = devm_request_irq(&pdev->dev, irq, sirfsoc_camera_irq, 0,
                                SIRFSOC_CAM_DRV_NAME, pcdev);
	if (ret)
		goto exit_kfree;

	pcdev->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(pcdev->clk)) {
		dev_err(&pdev->dev, "%s: fail to get vip clock\n", __func__);
		ret = -EINVAL;
		goto exit_free_irq;
	}

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

	pcdev->video_limit = 10 * SZ_1M;
	pcdev->rearview_dma_addr = sirf_vip_phy_base + 10 * SZ_1M;

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

	pcdev->dma_slave_config.direction = DMA_DEV_TO_MEM;
	pcdev->dma_slave_config.src_addr = 0;
	pcdev->dma_slave_config.dst_addr = 0;
	pcdev->dma_slave_config.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	pcdev->dma_slave_config.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	pcdev->dma_slave_config.src_maxburst = 4;
	pcdev->dma_slave_config.dst_maxburst = 4;
	pcdev->dma_slave_config.device_fc = 0;

	if(dmaengine_slave_config(pcdev->dma_chan, &pcdev->dma_slave_config)) {
		dev_err(&pdev->dev, "%s: can not set dma slave config\n",
			__func__);
		goto exit_free_dma;
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
		goto exit_free_pin;

	/*
	 * At this point client .probe() should have run already,
	 * decoder_ops is available.
	 */
	list_for_each_entry(decoder_ops, &decoder_list, list) {
		if (strcmp(decoder_ops->desc, "tw9900") == 0) {
			pcdev->rearview_decoder_ops = decoder_ops;
			break;
		}
	}

	pcdev->save_vip_context = sirfsoc_vip_save_context;
	pcdev->restore_vip_context = sirfsoc_vip_restore_context;

	if (need_launch_rearview(pcdev)) {
		pcdev->rearview_gpio = of_get_named_gpio(pdev->dev.of_node,
							"rearview-gpio", 0);
		rearview_task = kthread_create(rearview_thread,
						pcdev, "rearview");
		if (IS_ERR(rearview_task)) {
			dev_err(pcdev->dev, "%s: create rearview thread error\n",
				__func__);
			goto exit_host_unregister;
		}
		/*
		 * make sure fb asynchronous probe is finished, rearview need
		 * access rearview layer fb.
		 */
		async_synchronize_cookie(cookie);
		wake_up_process(rearview_task);
	}

	return;

exit_host_unregister:
	soc_camera_host_unregister(&sirfsoc_soc_camera_host);
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
exit_free_irq:
	free_irq(irq, pcdev);
exit_kfree:
	devm_kfree(&pdev->dev, pcdev);
exit:
	return;
}

static int sirfsoc_camera_probe(struct platform_device *pdev)
{
	pm_qos_add_request(&qos_cpufreq_min_req, PM_QOS_CPU_FREQ_MIN,
			PM_QOS_DEFAULT_VALUE);
	async_schedule(sirfsoc_camera_probe_async, pdev);
	return 0;
}

static int sirfsoc_camera_remove(struct platform_device *pdev)
{
	struct soc_camera_host *soc_host = to_soc_camera_host(&pdev->dev);
	struct sirfsoc_camera_dev *pcdev = soc_host->priv;

	dev_info(&pdev->dev, "%s\n", __func__);

	pm_qos_remove_request(&qos_cpufreq_min_req);

	if (rearview_task) {
		kthread_stop(rearview_task);
		rearview_task = NULL;
	}

	soc_camera_host_unregister(&sirfsoc_soc_camera_host);
	clk_put(pcdev->clk);
	dmaengine_terminate_all(pcdev->dma_chan);
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
	struct soc_camera_host *soc_host = to_soc_camera_host(dev);
	struct sirfsoc_camera_dev *pcdev = soc_host->priv;

	dev_info(dev, "%s\n", __func__);

	if (rearview_task != NULL && pcdev->rearview_suspend)
		pcdev->rearview_suspend();

	if (pcdev->icd == NULL)
		return 0;

	disable_irq(pcdev->irq);
	pcdev->vip_funcs.pfnStop();
	dmaengine_terminate_all(pcdev->dma_chan);

	sirfsoc_camera_deactivate(pcdev);

	return 0;
}

static int sirfsoc_cam_pm_resume(struct device *dev)
{
	struct soc_camera_host *soc_host = to_soc_camera_host(dev);
	struct sirfsoc_camera_dev *pcdev = soc_host->priv;

	dev_info(dev, "%s\n", __func__);

	if (rearview_task != NULL && pcdev->rearview_resume)
		pcdev->rearview_resume();

	if (pcdev->icd == NULL || (pcdev->rearview_enabled != NULL &&
					pcdev->rearview_enabled()))
		return 0;

	sirfsoc_camera_activate(pcdev);

	pcdev->vip_funcs.pfnSetParams(&pcdev->vip_params);
	enable_irq(pcdev->irq);

	/* Restart frame capture if active buffer exists */
	if (pcdev->active)
		sirfsoc_camera_start_dma(pcdev, 1);

	return 0;
}
static int sirfsoc_cam_pm_freeze(struct device *dev)
{
	struct soc_camera_host *soc_host = to_soc_camera_host(dev);
	struct sirfsoc_camera_dev *pcdev = soc_host->priv;

	dev_info(dev, "%s\n", __func__);

	if (rearview_task != NULL && pcdev->rearview_freeze)
		pcdev->rearview_freeze();

	if (pcdev->icd == NULL)
		return 0;

	disable_irq(pcdev->irq);
	pcdev->vip_funcs.pfnStop();
	dmaengine_terminate_all(pcdev->dma_chan);

	sirfsoc_camera_deactivate(pcdev);

	return 0;
}
static int sirfsoc_cam_pm_restore(struct device *dev)
{
	struct soc_camera_host *soc_host = to_soc_camera_host(dev);
	struct sirfsoc_camera_dev *pcdev = soc_host->priv;

	dev_info(dev, "%s\n", __func__);

	if (rearview_task != NULL && pcdev->rearview_restore)
		pcdev->rearview_restore();

	if (pcdev->icd == NULL || (pcdev->rearview_enabled != NULL &&
					pcdev->rearview_enabled()))
		return 0;

	sirfsoc_camera_activate(pcdev);

	pcdev->vip_funcs.pfnSetParams(&pcdev->vip_params);
	enable_irq(pcdev->irq);

	/* Restart frame capture if active buffer exists */
	if (pcdev->active)
		sirfsoc_camera_start_dma(pcdev, 1);

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
MODULE_AUTHOR("Renwei Wu <Renwei.Wu@csr.com>, "
	"Xiaomeng Hou <Xiaomeng.Hou@csr.com>");
MODULE_LICENSE("GPL v2");
