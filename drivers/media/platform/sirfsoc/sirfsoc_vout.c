/*
 * CSR SiRFprima2 Video Output Driver
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/module.h>

#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>
#include "sirfsoc_vout.h"

#define SIRFSOC_VOUT_DRV_NAME	"sirfsoc_vout"
#define SIRFSOC_VOUT_VERSION_CODE KERNEL_VERSION(0, 0, 1)

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-2)");

#define VIDEO_MIN_HEIGHT	1
#define VIDEO_MIN_WIDTH	1

#define VIDEO_MAX_WIDTH  1920
#define VIDEO_MAX_HEIGHT 1080
#define FPS_MAX 60


static const struct v4l2_fract
	fi_min = {.numerator = 1, .denominator = FPS_MAX},
	fi_max = {.numerator = FPS_MAX, .denominator = 1};

static inline u32 align_size(u32 size, u32 align)
{
	return (size + align - 1) & ~(align - 1);
}

static const struct v4l2_fmtdesc sirfsoc_vout_formats[] = {
	{
	.description = "RGB565",
	.pixelformat = V4L2_PIX_FMT_RGB565,
	},
	{
	.description = "RGB8888",
	.pixelformat = V4L2_PIX_FMT_RGB32,
	},
	{
	.description = "YVYU 422",
	.pixelformat = V4L2_PIX_FMT_YVYU,
	},
	{
	.description = "UYVY 422",
	.pixelformat = V4L2_PIX_FMT_UYVY,
	},
	{
	.description = "I420",
	.pixelformat = V4L2_PIX_FMT_YUV420,
	},
	{
	.description = "YUYV 422",
	.pixelformat = V4L2_PIX_FMT_YUYV,
	},
	{
	.description = "VYUY 422",
	.pixelformat = V4L2_PIX_FMT_VYUY,
	},
	{
	.description = "NV12",
	.pixelformat = V4L2_PIX_FMT_NV12,
	},
	{
	.description = "NV21",
	.pixelformat = V4L2_PIX_FMT_NV21,
	},
};

#define NUM_OUTPUT_FORMATS (ARRAY_SIZE(sirfsoc_vout_formats))

static int __sirfsoc_vout_v4l2_fmt_to_vdss_fmt(__u32 pix_fmt)
{
	enum vdss_pixelformat vdss_pixfmt;

	switch (pix_fmt) {
	case V4L2_PIX_FMT_YUYV:
		vdss_pixfmt = VDSS_PIXELFORMAT_YUYV;
		break;

	case V4L2_PIX_FMT_YVYU:
		vdss_pixfmt = VDSS_PIXELFORMAT_YVYU;
		break;

	case V4L2_PIX_FMT_UYVY:
		vdss_pixfmt = VDSS_PIXELFORMAT_UYVY;
		break;

	case V4L2_PIX_FMT_VYUY:
		vdss_pixfmt = VDSS_PIXELFORMAT_VYUY;
		break;

	case V4L2_PIX_FMT_NV12:
		vdss_pixfmt = VDSS_PIXELFORMAT_NV12;
		break;

	case V4L2_PIX_FMT_NV21:
		vdss_pixfmt = VDSS_PIXELFORMAT_NV21;
		break;

	case V4L2_PIX_FMT_YUV420:
		vdss_pixfmt = VDSS_PIXELFORMAT_I420;
		break;

	case V4L2_PIX_FMT_RGB565:
		vdss_pixfmt = VDSS_PIXELFORMAT_565;
		break;

	case V4L2_PIX_FMT_RGB32:
		vdss_pixfmt = VDSS_PIXELFORMAT_8888;
		break;

	default:
		vdss_pixfmt = VDSS_PIXELFORMAT_565;
		break;
	}
	return vdss_pixfmt;
}

static int __sirfsoc_vout_alignment(u32 pix_fmt, u32 width, u32 height,
	u32 *hor_stride, u32 *ver_stride)
{
	switch (pix_fmt) {
	case VDSS_PIXELFORMAT_NV12:
	case VDSS_PIXELFORMAT_NV21:
		/*
		 * New vxd hw deocder buffer alignment spec, width: 64byte,
		 * heigh: 16. Seems had better define private fmt for it.
		 */
		*hor_stride = align_size(width, 64);
		*ver_stride = align_size(height, 16);
		break;
	case VDSS_PIXELFORMAT_I420:
		*hor_stride = align_size(width, 16);
		*ver_stride = height;
		break;
	case VDSS_PIXELFORMAT_YV12:
		*hor_stride = align_size(width, 16);
		*ver_stride = height;
		break;
	case VDSS_PIXELFORMAT_IMC1:
	case VDSS_PIXELFORMAT_IMC3:
	case VDSS_PIXELFORMAT_VYUY:
		*hor_stride = align_size(width, 8);
		*ver_stride = height;
		break;
	case VDSS_PIXELFORMAT_UYVY:
	case VDSS_PIXELFORMAT_YUY2:
	case VDSS_PIXELFORMAT_YVYU:
	case VDSS_PIXELFORMAT_YUYV:
		*hor_stride = align_size(width * 2, 8) / 2;
		*ver_stride = height;
		break;
	case VDSS_PIXELFORMAT_565:
	case VDSS_PIXELFORMAT_8888:
	case VDSS_PIXELFORMAT_BGRX_8880:
	case VDSS_PIXELFORMAT_RGBX_8880:
		*hor_stride = width;
		*ver_stride = height;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void __sirfsoc_vout_set_display_info(struct sirfsoc_vout_device *vout,
	struct vb2_buffer *buf)
{
	struct sirfsoc_vdss_layer *l;
	struct sirfsoc_vdss_layer_info info;
	enum vdss_pixelformat pixfmt;

	pixfmt  = __sirfsoc_vout_v4l2_fmt_to_vdss_fmt(
		vout->pix_fmt.pixelformat);

	l = vout->layer;

	/* VPP setting */
	if (vout->passthrough) {
		struct vdss_vpp_op_params params = {0};

		params.type = VPP_OP_PASS_THROUGH;

		params.op.passthrough.src_surf.fmt = pixfmt;
		params.op.passthrough.src_surf.width = vout->surf_width;
		params.op.passthrough.src_surf.height = vout->surf_height;
		params.op.passthrough.src_surf.base =
			vb2_dma_contig_plane_dma_addr(buf, 0);

		params.op.passthrough.src_rect.left = vout->src_rect.left;
		params.op.passthrough.src_rect.top = vout->src_rect.top;
		params.op.passthrough.src_rect.right =
			vout->src_rect.left + vout->src_rect.width - 1;
		params.op.passthrough.src_rect.bottom =
			vout->src_rect.top + vout->src_rect.height - 1;

		params.op.passthrough.dst_rect.left = vout->dst_rect.left;
		params.op.passthrough.dst_rect.top = vout->dst_rect.top;
		params.op.passthrough.dst_rect.right =
			vout->dst_rect.left + vout->dst_rect.width - 1;
		params.op.passthrough.dst_rect.bottom =
			vout->dst_rect.top + vout->dst_rect.height - 1;

		sirfsoc_vpp_present(vout->vpp_handle, &params);
	}

	/* layer setting */
	l->get_info(l, &info);

	info.base = vb2_dma_contig_plane_dma_addr(buf, 0);
	info.passthrough = vout->passthrough;

	info.src_rect.left = vout->src_rect.left;
	info.src_rect.top = vout->src_rect.top;
	info.src_rect.right = vout->src_rect.left + vout->src_rect.width - 1;
	info.src_rect.bottom = vout->src_rect.top + vout->src_rect.height - 1;

	info.dst_rect.left = vout->dst_rect.left;
	info.dst_rect.top = vout->dst_rect.top;
	info.dst_rect.right =  vout->dst_rect.left + vout->dst_rect.width - 1;
	info.dst_rect.bottom = vout->dst_rect.top + vout->dst_rect.height - 1;
	info.fmt = pixfmt;

	info.surf_width = vout->surf_width;
	info.surf_height = vout->surf_height;
	l->set_info(l, &info);
	l->screen->apply(l->screen);

}

static void __sirfsoc_vout_display(struct sirfsoc_vout_device *vout,
	struct vb2_buffer *buf)
{
	struct sirfsoc_vdss_layer *l = vout->layer;

	if (l->is_enabled(l)) {
		if ((vout->pix_fmt.pixelformat != V4L2_PIX_FMT_RGB565) &&
			(vout->pix_fmt.pixelformat != V4L2_PIX_FMT_RGB32)) {
			struct sirfsoc_vdss_layer *l = vout->layer;

			if (vout->passthrough) {
				struct vdss_vpp_op_params vpp_op = {0};

				vpp_op.type = VPP_OP_PASS_THROUGH;
				vpp_op.op.passthrough.src_surf.base =
					vb2_dma_contig_plane_dma_addr(buf, 0);
				vpp_op.op.passthrough.flip = true;

				sirfsoc_vpp_present(vout->vpp_handle, &vpp_op);
			}
			/*
			 * In passthrough mode, we found that if only
			 * VPP registers are changed, we should also
			 * set LX_CTRL_CONFIRM, otherwise VPP shadow
			 * registers won't take effect in next vsync
			 * */
			l->flip(l, vb2_dma_contig_plane_dma_addr(buf, 0));

		} else
			__sirfsoc_vout_set_display_info(vout, buf);
	} else
		__sirfsoc_vout_set_display_info(vout, buf);
}

static void __vpp_callback(void *arg,
				enum vdss_vpp id,
				enum vdss_vpp_op_type type)
{
	struct sirfsoc_vout_device *vout =
			(struct sirfsoc_vout_device *)arg;
	struct sirfsoc_vdss_layer *l = vout->layer;

	if (id == l->lcdc_id) {
		if (type > VPP_OP_PASS_THROUGH) {
			if (vout->preempted == false)
				l->disable(l);
			vout->preempted = true;
		} else {
			if (vout->preempted)
				l->enable(l);
			vout->preempted = false;
		}
	}
}

static int __sirfsoc_vout_start_streaming(struct sirfsoc_vout_device *vout)
{
	struct sirfsoc_vout_buf *buf;
	struct vb2_buffer *vb2_buf;
	struct sirfsoc_vdss_layer *l = vout->layer;
	enum vdss_pixelformat pixfmt;

	buf = list_entry(vout->dma_queue.next, struct sirfsoc_vout_buf, list);
	list_del_init(&buf->list);

	vb2_buf = &buf->vb;
	vout->active_frm = &buf->vb;
	vout->next_frm = &buf->vb;

	vout->active_frm->state = VB2_BUF_STATE_ACTIVE;

	/* check whether pass-through needed */
	pixfmt = __sirfsoc_vout_v4l2_fmt_to_vdss_fmt(
		vout->pix_fmt.pixelformat);
	vout->passthrough = sirfsoc_vpp_is_passthrough_support(pixfmt);

	if (vout->passthrough && !vout->vpp_handle) {
		struct vdss_vpp_create_device_params params = {0};

		params.func = __vpp_callback;
		params.arg = vout;
		vout->vpp_handle =
			sirfsoc_vpp_create_device(l->lcdc_id, &params);
	}

	/*Start display*/
	__sirfsoc_vout_display(vout, vb2_buf);

	if (!l->is_enabled(l) && !vout->preempted)
		l->enable(l);

	return 0;
}

static int __sirfsoc_vout_try_fmt(struct v4l2_pix_format *pix, u32 *hor_stride,
	u32 *ver_stride)
{
	int index = 0;
	int bpp = 0; /* byte per pixel */
	int vdss_pixfmt;

	pix->width = clamp(pix->width, (u32)VIDEO_MIN_WIDTH,
			(u32)VIDEO_MAX_WIDTH);
	pix->height = clamp(pix->height, (u32)VIDEO_MIN_HEIGHT,
			(u32)VIDEO_MAX_HEIGHT);

	for (index = 0; index < NUM_OUTPUT_FORMATS; index++)
		if (pix->pixelformat == sirfsoc_vout_formats[index].pixelformat)
			break;

	if (index == NUM_OUTPUT_FORMATS) {
		index = 0;
		return -EINVAL;
	}

	pix->pixelformat = sirfsoc_vout_formats[index].pixelformat;
	pix->field = V4L2_FIELD_ANY;
	pix->priv = 0;

	switch (pix->pixelformat) {
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_UYVY:
		pix->colorspace = V4L2_COLORSPACE_JPEG;
		bpp = 2;
		break;
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_YUV420:
		pix->colorspace = V4L2_COLORSPACE_JPEG;
		/*
		 * Note: When the image format is planar, the bytesperline
		 * value applies to the largest plane, so here set bpp to be 1
		 */
		bpp = 1;
		break;
	case V4L2_PIX_FMT_RGB565:
		pix->colorspace = V4L2_COLORSPACE_SRGB;
		bpp = 2;
		break;
	case V4L2_PIX_FMT_RGB32:
		pix->colorspace = V4L2_COLORSPACE_SRGB;
		bpp = 4;
		break;
	default:
		pix->colorspace = V4L2_COLORSPACE_SRGB;
		bpp = 2;
		break;
	}

	vdss_pixfmt = __sirfsoc_vout_v4l2_fmt_to_vdss_fmt(pix->pixelformat);

	__sirfsoc_vout_alignment(vdss_pixfmt, pix->width, pix->height,
		hor_stride, ver_stride);

	pix->bytesperline = *hor_stride * bpp;

	pix->sizeimage = pix->bytesperline * (*ver_stride);

	switch (vdss_pixfmt) {
	case VDSS_PIXELFORMAT_NV12:
	case VDSS_PIXELFORMAT_NV21:
	case VDSS_PIXELFORMAT_I420:
		/* Planar format should contain Y and UV sections */
		pix->sizeimage += pix->sizeimage >> 1;
		break;
	default:
		break;
	}

	return 0;
}

static int __sirfsoc_vout_try_rect(struct v4l2_rect *new_rect, u32 ref_width,
	u32 ref_height)
{
	if (new_rect->left < 0) {
		new_rect->width += new_rect->left;
		new_rect->left = 0;
	}

	if (new_rect->top < 0) {
		new_rect->height += new_rect->top;
		new_rect->top = 0;
	}

	new_rect->width = (new_rect->width < ref_width) ?
			new_rect->width : ref_width;
	new_rect->height = (new_rect->height < ref_height) ?
			new_rect->height : ref_height;

	if (new_rect->left + new_rect->width  > ref_width)
		new_rect->width = ref_width - new_rect->left;
	if (new_rect->top + new_rect->height > ref_height)
		new_rect->height = ref_height - new_rect->top;

	return 0;
}

static int __sirfsoc_setup_video_data(struct sirfsoc_vout_device *vout)
{
	struct v4l2_pix_format *fmt = &vout->pix_fmt;

	fmt->width = vout->display->timings.xres;
	fmt->height = vout->display->timings.yres;

	fmt->pixelformat = V4L2_PIX_FMT_RGB565;
	fmt->field = V4L2_FIELD_ANY;
	fmt->bytesperline = fmt->width * 2;
	fmt->sizeimage = fmt->bytesperline * fmt->height;
	fmt->priv = 0;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;

	vout->src_rect.left = 0;
	vout->src_rect.top = 0;
	vout->src_rect.width = vout->display->timings.xres;
	vout->src_rect.height = vout->display->timings.yres;

	vout->dst_rect.left = 0;
	vout->dst_rect.top = 0;
	vout->dst_rect.width = vout->display->timings.xres;
	vout->dst_rect.height = vout->display->timings.yres;

	vout->surf_width = fmt->width;
	vout->surf_height = fmt->height;

	return 0;
}

/*
 * sirfsoc_vout_queue_setup()
 * This function allocates memory for the buffers
 */
static int sirfsoc_vout_queue_setup(struct vb2_queue *vq,
	const struct v4l2_format *fmt, unsigned int *nbuffers,
	unsigned int *nplanes, unsigned int sizes[], void *alloc_ctxs[])
{
	struct sirfsoc_vout_device *vout = vb2_get_drv_priv(vq);
	struct v4l2_device *v4l2_dev = &vout->vid_dev->v4l2_dev;

	v4l2_dbg(1, debug, v4l2_dev, "Enter %s\n", __func__);

	*nplanes = 1;
	sizes[0] = vout->pix_fmt.sizeimage;
	alloc_ctxs[0] = vout->alloc_ctx;

	v4l2_dbg(1, debug, v4l2_dev, "Exit %s\n", __func__);

	return 0;
}

static void sirfsoc_vout_wait_prepare(struct vb2_queue *vq)
{
	struct sirfsoc_vout_device *vout = vb2_get_drv_priv(vq);

	mutex_unlock(&vout->lock);
}

static void sirfsoc_vout_wait_finish(struct vb2_queue *vq)
{
	struct sirfsoc_vout_device *vout = vb2_get_drv_priv(vq);

	mutex_lock(&vout->lock);
}

static int sirfsoc_vout_buf_init(struct vb2_buffer *vb)
{
	struct sirfsoc_vout_buf *buf = container_of(vb,
		struct sirfsoc_vout_buf, vb);

	INIT_LIST_HEAD(&buf->list);

	return 0;
}
/*
 * sirfsoc_vout_buf_prepare()
 * This is the callback function called from vb2_qbuf function
 * the buffer is prepared and user space virtual address is converted to
 * physical address.
 */
static int sirfsoc_vout_buf_prepare(struct vb2_buffer *vb)
{
	unsigned long addr;
	struct vb2_queue	*q = vb->vb2_queue;
	struct sirfsoc_vout_device *vout = vb2_get_drv_priv(q);

	if (vb->state != VB2_BUF_STATE_ACTIVE &&
		vb->state != VB2_BUF_STATE_PREPARED) {
		vb2_set_plane_payload(vb, 0, vout->pix_fmt.sizeimage);
		if (vb2_plane_vaddr(vb, 0) &&
			vb2_get_plane_payload(vb, 0) > vb2_plane_size(vb, 0))
			return -EINVAL;

		addr = vb2_dma_contig_plane_dma_addr(vb, 0);
		if (q->streaming) {
			if (!IS_ALIGNED(addr, 8))
				return -EINVAL;
		}
	}

	return 0;
}
/*video output callback,set active buf to VB2_BUF_SATE_DONE
 and swtich to next buffer to display
 */
static void sirfsoc_vout_isr(void *pdata, unsigned int irqstatus)
{
	struct sirfsoc_vout_device *vout = pdata;
	struct sirfsoc_vout_buf *vout_buf;

	if (irqstatus | LCDC_INT_VSYNC) {
		spin_lock(&vout->vbq_lock);

		if (vout->active_frm != vout->next_frm) {
			if (vout->active_frm != NULL) {
				v4l2_get_timestamp(&vout->active_frm->
						v4l2_buf.timestamp);
				vb2_buffer_done(vout->active_frm,
						VB2_BUF_STATE_DONE);
			}
			vout->active_frm = vout->next_frm;
		}

		if (!list_empty(&vout->dma_queue)) {
			vout_buf = list_entry(vout->dma_queue.next,
				struct sirfsoc_vout_buf, list);
			list_del_init(&vout_buf->list);

			vout->next_frm = &vout_buf->vb;

			vout->next_frm->state =
				VB2_BUF_STATE_ACTIVE;
			__sirfsoc_vout_display(vout,
				vout->next_frm);
		}

		spin_unlock(&vout->vbq_lock);
	}
}

static int sirfsoc_vout_start_streaming(struct vb2_queue *vq,
	unsigned int count)
{
	int ret = 0;
	struct sirfsoc_vout_device *vout = vb2_get_drv_priv(vq);
	unsigned long flags;

	spin_lock_irqsave(&vout->vbq_lock, flags);

	__sirfsoc_vout_start_streaming(vout);

	spin_unlock_irqrestore(&vout->vbq_lock, flags);
	return ret;
}

static void sirfsoc_vout_stop_streaming(struct vb2_queue *vq)
{
	struct sirfsoc_vout_device *vout = vb2_get_drv_priv(vq);
	struct sirfsoc_vout_buf *buf = NULL;
	unsigned long flags;

	if (!vb2_is_streaming(vq))
		return;

	spin_lock_irqsave(&vout->vbq_lock, flags);

	if (vout->next_frm)
		vb2_buffer_done(vout->next_frm, VB2_BUF_STATE_ERROR);

	if (vout->active_frm && (vout->active_frm != vout->next_frm))
		vb2_buffer_done(vout->active_frm, VB2_BUF_STATE_ERROR);

	while (!list_empty(&vout->dma_queue)) {
		buf = list_entry(vout->dma_queue.next,
			struct sirfsoc_vout_buf, list);
		list_del_init(&buf->list);
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
	}

	vout->active_frm = NULL;
	vout->next_frm = NULL;

	spin_unlock_irqrestore(&vout->vbq_lock, flags);
}
/*
 * sirfsoc_vout_buf_queue()
 * This function adds the buffer to DMA queue
 */
static void sirfsoc_vout_buf_queue(struct vb2_buffer *vb)
{
	struct sirfsoc_vout_device *vout = vb2_get_drv_priv(vb->vb2_queue);
	struct sirfsoc_vout_buf *buf = container_of(vb,
		struct sirfsoc_vout_buf, vb);
	unsigned long flags;

	spin_lock_irqsave(&vout->vbq_lock, flags);
	list_add_tail(&buf->list, &vout->dma_queue);
	spin_unlock_irqrestore(&vout->vbq_lock, flags);
}
/*
 * sirfsoc_vout_buf_cleanup()
 * This function is called from the vb2 layer to free memory allocated
 */
static void sirfsoc_vout_buf_cleanup(struct vb2_buffer *vb)
{
	struct sirfsoc_vout_device *vout = vb2_get_drv_priv(vb->vb2_queue);
	struct sirfsoc_vout_buf *buf = container_of(vb,
		struct sirfsoc_vout_buf, vb);
	unsigned long flags;

	spin_lock_irqsave(&vout->vbq_lock, flags);
	if (vb->state == VB2_BUF_STATE_ACTIVE)
		list_del_init(&buf->list);
	spin_unlock_irqrestore(&vout->vbq_lock, flags);
}

static struct vb2_ops sirfsoc_vout_video_qops = {
	.queue_setup	= sirfsoc_vout_queue_setup,
	.wait_prepare	= sirfsoc_vout_wait_prepare,
	.wait_finish	= sirfsoc_vout_wait_finish,
	.buf_init	= sirfsoc_vout_buf_init,
	.buf_prepare	= sirfsoc_vout_buf_prepare,
	.start_streaming = sirfsoc_vout_start_streaming,
	.stop_streaming	 = sirfsoc_vout_stop_streaming,
	.buf_cleanup	= sirfsoc_vout_buf_cleanup,
	.buf_queue	= sirfsoc_vout_buf_queue,
};
/*
 *Video IOCTLs
 */
static int sirfsoc_vout_querycap(struct file *file, void  *priv,
			   struct v4l2_capability *cap)
{
	WARN_ON(priv != file->private_data);

	strlcpy(cap->driver, SIRFSOC_VOUT_DRV_NAME, sizeof(cap->driver));
	cap->version = SIRFSOC_VOUT_VERSION_CODE;
	cap->capabilities = V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_STREAMING |
		V4L2_CAP_VIDEO_OUTPUT_OVERLAY;

	return 0;
}

static int sirfsoc_vout_enum_fmt_vid_out(struct file *file, void  *priv,
				   struct v4l2_fmtdesc *fmt)
{
	int index = fmt->index;

	if (index >= NUM_OUTPUT_FORMATS)
		return -EINVAL;

	fmt->flags = sirfsoc_vout_formats[index].flags;
	strlcpy(fmt->description, sirfsoc_vout_formats[index].description,
		sizeof(fmt->description));
	fmt->pixelformat = sirfsoc_vout_formats[index].pixelformat;

	return 0;
}

static int sirfsoc_vout_g_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	struct sirfsoc_vout_device *vout = priv;

	fmt->fmt.pix = vout->pix_fmt;

	return 0;
}

static int sirfsoc_vout_s_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	struct sirfsoc_vout_device *vout = priv;
	struct v4l2_device *v4l2_dev = &vout->vid_dev->v4l2_dev;
	struct sirfsoc_vdss_panel *panel = vout->display;
	int ret;
	u32 hor_stride = 0;
	u32 ver_stride = 0;


	v4l2_dbg(1, debug, v4l2_dev, "Enter %s\n", __func__);

	if (vout->vb2_q.streaming) {
		v4l2_err(v4l2_dev, "device is already in streaming state\n");
		return -EBUSY;
	}

	if (V4L2_BUF_TYPE_VIDEO_OUTPUT != fmt->type) {
		v4l2_err(v4l2_dev, "invalid buffer type\n");
		return -EINVAL;
	}

	if (!panel) {
		v4l2_err(v4l2_dev, "no display device attached\n");
		return -EINVAL;
	}

	ret = __sirfsoc_vout_try_fmt(&fmt->fmt.pix, &hor_stride, &ver_stride);
	if (ret) {
		v4l2_err(v4l2_dev, "invalid pixel format:%x\n",
			fmt->fmt.pix.pixelformat);
		return -EINVAL;
	}

	vout->pix_fmt = fmt->fmt.pix;
	vout->surf_width = hor_stride;
	vout->surf_height = ver_stride;
	/* set new crop and window according to the new format?*/
	vout->src_rect.left = 0;
	vout->src_rect.top = 0;
	vout->src_rect.width = vout->pix_fmt.width;
	vout->src_rect.height = vout->pix_fmt.height;

	v4l2_dbg(1, debug, v4l2_dev, "Exit %s\n", __func__);
	return 0;
}

static int sirfsoc_vout_try_fmt_vid_out(struct file *file, void *priv,
				  struct v4l2_format *fmt)
{
	struct sirfsoc_vout_device *vout = priv;
	struct v4l2_device *v4l2_dev = &vout->vid_dev->v4l2_dev;
	u32 hor_stride = 0, ver_stride = 0;
	int ret;

	v4l2_dbg(1, debug, v4l2_dev, "Enter %s\n", __func__);

	ret = __sirfsoc_vout_try_fmt(&fmt->fmt.pix, &hor_stride, &ver_stride);
	if (ret) {
		v4l2_err(v4l2_dev, "try vout fmt error: %x\n",
			fmt->fmt.pix.pixelformat);
		return -EINVAL;
	}

	v4l2_dbg(1, debug, v4l2_dev, "Exit %s\n", __func__);
	return 0;
}

static int sirfsoc_vout_try_fmt_vid_out_overlay(struct file *file, void *priv,
			struct v4l2_format *fmt)
{
	int ret = 0;
	struct sirfsoc_vout_device *vout = priv;
	struct v4l2_device *v4l2_dev = &vout->vid_dev->v4l2_dev;
	struct v4l2_window *win = &fmt->fmt.win;

	v4l2_dbg(1, debug, v4l2_dev, "Enter %s\n", __func__);

	ret = __sirfsoc_vout_try_rect(&win->w, vout->dst_rect.width,
		vout->dst_rect.height);

	v4l2_dbg(1, debug, v4l2_dev, "Exit %s\n", __func__);

	return ret;
}

static int sirfsoc_vout_s_fmt_vid_out_overlay(struct file *file, void *priv,
			struct v4l2_format *fmt)
{
	int ret = 0;
	struct sirfsoc_vout_device *vout = priv;
	struct v4l2_device *v4l2_dev = &vout->vid_dev->v4l2_dev;
	struct v4l2_window *win = &fmt->fmt.win;

	v4l2_dbg(1, debug, v4l2_dev, "Enter %s\n", __func__);

	if (vout->vb2_q.streaming) {
		v4l2_err(v4l2_dev, "device is already in streaming state\n");
		return -EBUSY;
	}

	if (V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY != fmt->type) {
		v4l2_err(v4l2_dev, "unsupport buf type\n");
		return -EINVAL;
	}

	ret = __sirfsoc_vout_try_rect(&win->w, vout->display->timings.xres,
		vout->display->timings.yres);

	vout->dst_rect = win->w;

	v4l2_dbg(1, debug, v4l2_dev, "Exit %s\n", __func__);

	return ret;
}

static int sirfsoc_vout_g_fmt_vid_out_overlay(struct file *file, void *priv,
			struct v4l2_format *fmt)
{
	struct sirfsoc_vout_device *vout = priv;
	struct v4l2_device *v4l2_dev = &vout->vid_dev->v4l2_dev;

	v4l2_dbg(1, debug, v4l2_dev, "Enter %s\n", __func__);

	fmt->fmt.win.w.left = vout->dst_rect.left;
	fmt->fmt.win.w.top = vout->dst_rect.top;
	fmt->fmt.win.w.width = vout->dst_rect.width;
	fmt->fmt.win.w.height = vout->dst_rect.height;

	fmt->fmt.win.field = vout->pix_fmt.field;

	v4l2_dbg(1, debug, v4l2_dev, "Exit %s\n", __func__);

	return 0;
}

static int sirfsoc_vout_reqbufs(struct file *file, void *priv,
	struct v4l2_requestbuffers *req_buf)
{
	struct sirfsoc_vout_device *vout = priv;
	struct vb2_queue *vb2_q = &vout->vb2_q;
	struct v4l2_device *v4l2_dev = &vout->vid_dev->v4l2_dev;
	int ret = 0;

	v4l2_dbg(1, debug, v4l2_dev, "Enter %s\n", __func__);

	if ((req_buf->type != V4L2_BUF_TYPE_VIDEO_OUTPUT) ||
		(req_buf->count < 0)) {
		v4l2_err(v4l2_dev, "unsupported buf type\n");
		return -EINVAL;
	}

	if ((V4L2_MEMORY_MMAP != req_buf->memory) &&
		(V4L2_MEMORY_USERPTR != req_buf->memory) &&
		(V4L2_MEMORY_DMABUF != req_buf->memory)) {
		v4l2_err(v4l2_dev, "unsupported memory type\n");
		return -EINVAL;
	}

	if (vb2_q->streaming) {
		ret = -EBUSY;
		goto reqbuf_err;
	}

	if (!vout->alloc_ctx) {
		vout->alloc_ctx = vb2_dma_contig_init_ctx(
			&vout->vid_dev->pdev->dev);
		if (IS_ERR(vout->alloc_ctx)) {
			v4l2_err(&vout->vid_dev->v4l2_dev, "get context failed\n");
			return PTR_ERR(vout->alloc_ctx);
		}
	}

	vb2_q->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	vb2_q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	vb2_q->drv_priv = vout;
	vb2_q->ops = &sirfsoc_vout_video_qops;
	vb2_q->mem_ops = &vb2_dma_contig_memops;
	vb2_q->buf_struct_size = sizeof(struct sirfsoc_vout_buf);
	vb2_q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	vb2_q->min_buffers_needed = 1;

	ret = vb2_queue_init(vb2_q);
	if (ret) {
		vb2_dma_contig_cleanup_ctx(vout->alloc_ctx);
		vout->alloc_ctx = NULL;
		goto reqbuf_err;
	}

	INIT_LIST_HEAD(&vout->dma_queue);

	ret = vb2_reqbufs(vb2_q, req_buf);

reqbuf_err:
	v4l2_dbg(1, debug, v4l2_dev, "Exit %s: ret = %d\n", __func__, ret);
	return ret;

}

static int sirfsoc_vout_querybuf(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	struct sirfsoc_vout_device *vout = priv;
	struct v4l2_device *v4l2_dev = &vout->vid_dev->v4l2_dev;
	int ret = 0;

	v4l2_dbg(1, debug, v4l2_dev, "Enter %s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_OUTPUT != buf->type) {
		v4l2_err(v4l2_dev, "invalid buffer type\n");
		return -EINVAL;
	}

	ret = vb2_querybuf(&vout->vb2_q, buf);

	v4l2_dbg(1, debug, v4l2_dev, "Exit %s: ret = %d\n", __func__, ret);
	return ret;
}

static int sirfsoc_vout_qbuf(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	struct sirfsoc_vout_device *vout = priv;
	struct v4l2_device *v4l2_dev = &vout->vid_dev->v4l2_dev;
	int ret = 0;

	v4l2_dbg(1, debug, v4l2_dev, "Enter %s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_OUTPUT != buf->type) {
		v4l2_err(v4l2_dev, "invalid buffer type\n");
		return -EINVAL;
	}

	ret = vb2_qbuf(&vout->vb2_q, buf);

	v4l2_dbg(1, debug, v4l2_dev, "Exit %s: ret = %d\n", __func__, ret);

	return ret;
}

static int sirfsoc_vout_dqbuf(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	struct sirfsoc_vout_device *vout = priv;
	struct v4l2_device *v4l2_dev = &vout->vid_dev->v4l2_dev;
	int ret = 0;

	v4l2_dbg(1, debug, v4l2_dev, "Enter %s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_OUTPUT != buf->type) {
		v4l2_err(v4l2_dev, "invalid buffer type\n");
		return -EINVAL;
	}

	ret = vb2_dqbuf(&vout->vb2_q, buf, file->f_flags & O_NONBLOCK);

	v4l2_dbg(1, debug, v4l2_dev, "Exit %s, ret = %d\n", __func__, ret);

	return ret;
}

static int sirfsoc_vout_create_bufs(struct file *file, void *priv,
	struct v4l2_create_buffers *create)
{
	struct sirfsoc_vout_device *vout = priv;

	return vb2_create_bufs(&vout->vb2_q, create);
}

static int sirfsoc_vout_prepare_buf(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	struct sirfsoc_vout_device *vout = priv;

	return vb2_prepare_buf(&vout->vb2_q, buf);
}

static int sirfsoc_vout_streamon(struct file *file, void *priv,
			   enum v4l2_buf_type buf_type)
{
	int ret = 0;
	struct sirfsoc_vout_device *vout = priv;
	struct v4l2_device *v4l2_dev = &vout->vid_dev->v4l2_dev;

	v4l2_dbg(1, debug, v4l2_dev, "Enter %s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_OUTPUT != buf_type) {
		v4l2_err(v4l2_dev, "invalid buffer type : %x\n", buf_type);
		return -EINVAL;
	}

	sirfsoc_lcdc_register_isr(vout->layer->lcdc_id, sirfsoc_vout_isr,
		vout, LCDC_INT_VSYNC);

	ret = vb2_streamon(&vout->vb2_q, buf_type);
	if (ret) {
		v4l2_err(v4l2_dev, "vb2_streamon error\n");
		return ret;
	}

	v4l2_dbg(1, debug, v4l2_dev, "Exit %s: ret = %d\n", __func__, ret);

	return ret;
}

static int sirfsoc_vout_streamoff(struct file *file, void *priv,
			    enum v4l2_buf_type buf_type)
{
	int ret;
	struct sirfsoc_vout_device *vout = priv;
	struct v4l2_device *v4l2_dev = &vout->vid_dev->v4l2_dev;

	v4l2_dbg(1, debug, v4l2_dev, "Enter %s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_OUTPUT != buf_type) {
		v4l2_err(v4l2_dev, "unsupported buffer type :%x\n", buf_type);
		return -EINVAL;
	}

	sirfsoc_lcdc_unregister_isr(vout->layer->lcdc_id, sirfsoc_vout_isr,
		vout, LCDC_INT_VSYNC);

	ret = vb2_streamoff(&vout->vb2_q, buf_type);

	v4l2_dbg(1, debug, v4l2_dev, "Exit %s :ret = %d\n", __func__, ret);

	return ret;
}

static int sirfsoc_vout_g_crop(struct file *file, void *priv,
	struct v4l2_crop *crop)
{
	struct sirfsoc_vout_device *vout = priv;
	struct v4l2_device *v4l2_dev = &vout->vid_dev->v4l2_dev;

	v4l2_dbg(1, debug, v4l2_dev, "Enter %s\n", __func__);

	if (crop->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	crop->c = vout->src_rect;

	v4l2_dbg(1, debug, v4l2_dev, "Exit %s\n", __func__);

	return 0;
}

static int sirfsoc_vout_s_crop(struct file *file, void *priv,
	const struct v4l2_crop *crop)
{
	int ret = 0;
	struct sirfsoc_vout_device *vout = priv;
	struct sirfsoc_video_device *vid_dev = vout->vid_dev;
	struct v4l2_device *v4l2_dev = &vid_dev->v4l2_dev;
	struct v4l2_rect rect = crop->c;

	v4l2_dbg(1, debug, v4l2_dev, "Enter %s\n", __func__);

	if (crop->type != V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		v4l2_err(&vid_dev->v4l2_dev, "unsupport buf type\n");
		return -EINVAL;
	}

	ret = __sirfsoc_vout_try_rect(&rect, vout->pix_fmt.width,
		vout->pix_fmt.height);

	vout->src_rect = rect;

	v4l2_info(v4l2_dev, "src rect(l:%x,t:%x,w:%x,h:%x)\n",
		rect.left, rect.top, rect.width, rect.height);

	v4l2_dbg(1, debug, v4l2_dev, "Exit %s\n", __func__);

	return ret;
}

static int sirfsoc_vout_cropcap(struct file *file, void *priv,
			  struct v4l2_cropcap *cropcap)
{
	struct sirfsoc_vout_device *vout = priv;
	struct v4l2_device *v4l2_dev = &vout->vid_dev->v4l2_dev;
	struct v4l2_pix_format *fmt = &vout->pix_fmt;

	v4l2_dbg(1, debug, v4l2_dev, "Enter %s\n", __func__);

	if (cropcap->type != V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		v4l2_err(v4l2_dev, "unsupport buf type\n");
		return -EINVAL;
	}

	cropcap->bounds.left = 0;
	cropcap->bounds.top = 0;
	cropcap->bounds.width = fmt->width & ~-1;
	cropcap->bounds.height = fmt->height & ~-1;
	cropcap->defrect = cropcap->bounds;

	v4l2_dbg(1, debug, v4l2_dev, "Exit %s\n", __func__);
	return 0;
}

static int sirfsoc_vout_g_parm(struct file *file, void *priv,
			struct v4l2_streamparm *parm)
{
	struct v4l2_outputparm *op;

	if (!parm)
		return -EINVAL;

	if (parm->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;
	op = &parm->parm.output;

	memset(op, 0, sizeof(struct v4l2_outputparm));
	op->capability = V4L2_CAP_TIMEPERFRAME;
	op->outputmode = 0;
	op->timeperframe.numerator = 1;
	op->timeperframe.denominator = 60;

	return 0;
}

static int sirfsoc_vout_enum_framesizes(struct file *file, void *priv,
			struct v4l2_frmsizeenum *fsize)
{
	int i = 0;

	if (fsize->index)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(sirfsoc_vout_formats); i++)
		if (sirfsoc_vout_formats[i].pixelformat == fsize->pixel_format)
			break;

	if (i == ARRAY_SIZE(sirfsoc_vout_formats))
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_CONTINUOUS;
	fsize->stepwise.min_width = VIDEO_MIN_WIDTH;
	fsize->stepwise.min_height = VIDEO_MIN_HEIGHT;
	fsize->stepwise.max_width = VIDEO_MAX_WIDTH;
	fsize->stepwise.max_height = VIDEO_MAX_HEIGHT;
	fsize->stepwise.step_width = fsize->stepwise.step_height = 1;

	return 0;
}

static int sirfsoc_vout_enum_frameintervals(struct file *file, void *priv,
			struct v4l2_frmivalenum *fival)
{
	int i = 0;

	if (fival->index)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(sirfsoc_vout_formats); i++)
		if (sirfsoc_vout_formats[i].pixelformat == fival->pixel_format)
			break;

	if (i == ARRAY_SIZE(sirfsoc_vout_formats))
		return -EINVAL;

	fival->type = V4L2_FRMIVAL_TYPE_CONTINUOUS;

	fival->stepwise.min = fi_min;
	fival->stepwise.max = fi_max;
	fival->stepwise.step = (struct v4l2_fract) {1, 1};
	return 0;
}
/* File operations */
static int sirfsoc_vout_open(struct file *file)
{
	struct sirfsoc_vout_device *vout = video_drvdata(file);
	struct v4l2_device *v4l2_dev = &vout->vid_dev->v4l2_dev;
	struct sirfsoc_vdss_layer *l;
	struct sirfsoc_vdss_screen *scn;

	if (vout == NULL)
		return -ENODEV;

	v4l2_dbg(1, debug, v4l2_dev, "Enter %s\n", __func__);

	v4l2_dev = &vout->vid_dev->v4l2_dev;

	if (test_and_set_bit(1, &vout->device_is_open)) {
		v4l2_err(v4l2_dev, "sirfsoc_vout_device is busy\n");
		return -EBUSY;
	}

	if (mutex_lock_interruptible(&vout->lock))
		return -ERESTARTSYS;

	scn = sirfsoc_vdss_find_screen_from_panel(vout->display);

	if (!scn) {
		v4l2_err(v4l2_dev, "no screen for the panel\n");
		return -ENODEV;
	}

	l = sirfsoc_vdss_get_layer_from_screen(scn, false);

	if (!l) {
		v4l2_err(v4l2_dev, "no free layer for video output");
		return -EBUSY;
	}

	vout->layer = l;
	vout->preempted = false;

	if (__sirfsoc_setup_video_data(vout)) {
		v4l2_err(v4l2_dev, "get default output information fail\n");
		return -EBUSY;
	}

	file->private_data = vout;
	vout->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	mutex_unlock(&vout->lock);

	v4l2_dbg(1, debug, v4l2_dev, "Exit %s\n", __func__);
	return 0;
}

static int sirfsoc_vout_release(struct file *file)
{
	struct sirfsoc_vout_device *vout = file->private_data;
	struct v4l2_device *v4l2_dev;
	struct sirfsoc_vdss_layer *l = vout->layer;

	if (vout == NULL)
		return -ENODEV;

	v4l2_dev = &vout->vid_dev->v4l2_dev;

	v4l2_dbg(1, debug, v4l2_dev, "Enter %s\n", __func__);

	mutex_lock(&vout->lock);

	/*
	When closing fd, we should check and continue freeing resource,
	if it was not done before.
	*/
	if (vout->vb2_q.streaming) {
		sirfsoc_lcdc_unregister_isr(vout->layer->lcdc_id,
			sirfsoc_vout_isr, vout, LCDC_INT_VSYNC);
	}

	if (l->is_enabled(l)) {
		/*disable the overlay*/
		l->disable(l);
	}

	if (vout->vpp_handle) {
		sirfsoc_vpp_destroy_device(vout->vpp_handle);
		vout->vpp_handle = NULL;
		vout->passthrough = false;
	}

	if (vout->alloc_ctx) {
		vb2_queue_release(&vout->vb2_q);
		vb2_dma_contig_cleanup_ctx(vout->alloc_ctx);
		vout->alloc_ctx = NULL;
	}

	clear_bit(1, &vout->device_is_open);

	mutex_unlock(&vout->lock);

	v4l2_dbg(1, debug, v4l2_dev, "Exit %s\n", __func__);
	return 0;
}

static int sirfsoc_vout_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret;
	struct sirfsoc_vout_device *vout = file->private_data;
	struct v4l2_device *v4l2_dev;

	if (!vout)
		return -ENODEV;

	v4l2_dev = &vout->vid_dev->v4l2_dev;

	v4l2_dbg(1, debug, v4l2_dev, "Enter %s\n", __func__);

	if (mutex_lock_interruptible(&vout->lock))
		return -ERESTARTSYS;

	ret = vb2_mmap(&vout->vb2_q, vma);
	mutex_unlock(&vout->lock);

	v4l2_dbg(1, debug, v4l2_dev, "Exit %s: ret = %d\n", __func__, ret);
	return ret;
}

static unsigned int sirfsoc_vout_poll(struct file *file, poll_table *wait)
{
	unsigned int ret;
	struct sirfsoc_vout_device *vout = file->private_data;
	struct v4l2_device *v4l2_dev = &vout->vid_dev->v4l2_dev;

	v4l2_dbg(1, debug, v4l2_dev, "Enter %s\n", __func__);

	mutex_lock(&vout->lock);
	ret = vb2_poll(&vout->vb2_q, file, wait);
	mutex_unlock(&vout->lock);

	v4l2_dbg(1, debug, v4l2_dev, "Exit %s, ret = %d\n", __func__, ret);

	return ret;
}


/* sirfsoc_vout display ioctl operations */
static const struct v4l2_ioctl_ops sirfsoc_vout_ioctl_ops = {
	.vidioc_querycap		= sirfsoc_vout_querycap,
	.vidioc_enum_fmt_vid_out	= sirfsoc_vout_enum_fmt_vid_out,
	.vidioc_g_fmt_vid_out		= sirfsoc_vout_g_fmt_vid_out,
	.vidioc_s_fmt_vid_out		= sirfsoc_vout_s_fmt_vid_out,
	.vidioc_try_fmt_vid_out		= sirfsoc_vout_try_fmt_vid_out,
	.vidioc_try_fmt_vid_out_overlay	= sirfsoc_vout_try_fmt_vid_out_overlay,
	.vidioc_s_fmt_vid_out_overlay	= sirfsoc_vout_s_fmt_vid_out_overlay,
	.vidioc_g_fmt_vid_out_overlay	= sirfsoc_vout_g_fmt_vid_out_overlay,
	.vidioc_enum_framesizes		= sirfsoc_vout_enum_framesizes,
	.vidioc_reqbufs			= sirfsoc_vout_reqbufs,
	.vidioc_querybuf		= sirfsoc_vout_querybuf,
	.vidioc_qbuf			= sirfsoc_vout_qbuf,
	.vidioc_dqbuf			= sirfsoc_vout_dqbuf,
	.vidioc_create_bufs		= sirfsoc_vout_create_bufs,
	.vidioc_prepare_buf		= sirfsoc_vout_prepare_buf,
	.vidioc_enum_frameintervals	= sirfsoc_vout_enum_frameintervals,
	.vidioc_g_parm			= sirfsoc_vout_g_parm,
	.vidioc_streamon		= sirfsoc_vout_streamon,
	.vidioc_streamoff		= sirfsoc_vout_streamoff,
	.vidioc_cropcap			= sirfsoc_vout_cropcap,
	.vidioc_g_crop			= sirfsoc_vout_g_crop,
	.vidioc_s_crop			= sirfsoc_vout_s_crop,
};

static const struct v4l2_file_operations sirfsoc_vout_fops = {
	.owner		= THIS_MODULE,
	.open		= sirfsoc_vout_open,
	.release	= sirfsoc_vout_release,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= sirfsoc_vout_mmap,
	.poll		= sirfsoc_vout_poll,
};

static int sirfsoc_setup_video_data(struct sirfsoc_vout_device *vout)
{
	return __sirfsoc_setup_video_data(vout);
}

static int sirfsoc_setup_video_device(struct sirfsoc_vout_device *vout)
{
	struct video_device *video_dev;
	struct platform_device *pdev = vout->vid_dev->pdev;

	video_dev = vout->vd = video_device_alloc();

	if (!video_dev) {
		dev_err(&pdev->dev, "allocate video device failed\n");
		return -ENOMEM;
	}

	strlcpy(video_dev->name, SIRFSOC_VOUT_DRV_NAME,
		sizeof(video_dev->name));

	video_dev->release = video_device_release;
	video_dev->fops = &sirfsoc_vout_fops;
	video_dev->ioctl_ops = &sirfsoc_vout_ioctl_ops;

	video_dev->minor = -1;
	video_dev->v4l2_dev = &vout->vid_dev->v4l2_dev;
	video_dev->vfl_dir = VFL_DIR_TX;
	video_dev->lock = &vout->lock;

	mutex_init(&vout->lock);

	return 0;
}

static int sirfsoc_vout_create_video_devices(struct platform_device *pdev)
{
	int ret = 0;
	struct sirfsoc_vout_device *vout = NULL;
	struct video_device *video_dev = NULL;
	struct v4l2_device *v4l2_dev = platform_get_drvdata(pdev);
	struct sirfsoc_video_device *vid_dev = container_of(v4l2_dev,
			struct sirfsoc_video_device, v4l2_dev);
	int i = 0;

	for (i = 0; i < vid_dev->num_panel; i++) {
		vout = kzalloc(sizeof(struct sirfsoc_vout_device), GFP_KERNEL);
		if (!vout) {
			dev_err(&pdev->dev, "allocate memory for vout error\n");
			return -ENOMEM;
		}
		vid_dev->vouts[i] = vout;
		vout->vid_dev = vid_dev;
		vout->display = vid_dev->display[i];

		sirfsoc_setup_video_data(vout);

		sirfsoc_setup_video_device(vout);

		video_dev = vout->vd;
		if (video_register_device(video_dev,
				VFL_TYPE_GRABBER, -1) < 0) {
			dev_err(&pdev->dev, "regsiter video device failed\n");
			video_dev->minor = -1;
			ret = -ENODEV;
			goto error;
		}

		v4l2_info(v4l2_dev, "/dev/video%d created as output device\n",
			video_dev->num);

		spin_lock_init(&vout->vbq_lock);
		video_set_drvdata(video_dev, vout);

		continue;
error:
		kfree(vout);
		return ret;
	}

	return 0;
}

static int sirfsoc_vout_probe(struct platform_device *pdev)
{
	struct sirfsoc_video_device *vid_dev = NULL;
	struct sirfsoc_vdss_panel *panel = NULL;
	int ret = 0;

	if (!sirfsoc_vdss_is_initialized())
		return -ENXIO;

	vid_dev = kzalloc(sizeof(struct sirfsoc_video_device), GFP_KERNEL);
	if (vid_dev == NULL)
		return -ENOMEM;

	vid_dev->num_panel = 0;
	while ((panel = sirfsoc_vdss_get_next_panel(panel)) != NULL)
		vid_dev->display[vid_dev->num_panel++] = panel;

	vid_dev->pdev = pdev;

	pdev->num_resources = vid_dev->num_panel;

	if (vid_dev->num_panel == 0) {
		dev_err(&pdev->dev, "no display device attached\n");
		goto probe_err1;
	}

	if (v4l2_device_register(&pdev->dev, &vid_dev->v4l2_dev) < 0) {
		dev_err(&pdev->dev, "v4l2_device_register failed\n");
		ret = -ENODEV;
		goto probe_err1;
	}

	ret = sirfsoc_vout_create_video_devices(pdev);
	if (ret) {
		dev_err(&pdev->dev, "create sirfsoc_vout_device failed\n");
		goto probe_err2;
	}

	return 0;

probe_err2:
	v4l2_device_unregister(&vid_dev->v4l2_dev);
probe_err1:
	kfree(vid_dev);
	return ret;
}

static void sirfsoc_vout_free_device(struct sirfsoc_vout_device *vout)
{
	struct video_device *vd;
	struct v4l2_device *v4l2_dev;

	if (!vout)
		return;

	v4l2_dev = &vout->vid_dev->v4l2_dev;

	v4l2_dbg(1, debug, v4l2_dev, "Enter %s\n", __func__);
	vd = vout->vd;
	if (vd) {
		if (video_is_registered(vd))
			video_unregister_device(vd);
		else
			video_device_release(vd);
	}
	kfree(vout);

	v4l2_dbg(1, debug, v4l2_dev, "Exit %s\n", __func__);
}

static int sirfsoc_vout_remove(struct platform_device *pdev)
{
	struct v4l2_device *v4l2_dev = platform_get_drvdata(pdev);
	struct sirfsoc_video_device *vid_dev = container_of(v4l2_dev,
		struct sirfsoc_video_device, v4l2_dev);
	int i = 0;

	v4l2_device_unregister(v4l2_dev);
	for (i = 0; i < vid_dev->num_panel; i++)
		sirfsoc_vout_free_device(vid_dev->vouts[i]);

	kfree(vid_dev);

	return 0;
}

static struct platform_driver __refdata sirfsoc_vout = {
	.remove  = sirfsoc_vout_remove,
	.probe	 = sirfsoc_vout_probe,
	.driver  = {
		.name	= SIRFSOC_VOUT_DRV_NAME,
		.owner	= THIS_MODULE,
	},
};

static struct platform_device *vout_device;

static int __init sirfsoc_vout_init(void)
{
	int ret = 0;
	u64 mask = DMA_BIT_MASK(32);

	ret = platform_driver_register(&sirfsoc_vout);
	if (!ret) {
		vout_device  = platform_device_alloc(
						SIRFSOC_VOUT_DRV_NAME, 0);
		if (vout_device) {
			ret = dma_set_coherent_mask(&vout_device->dev, mask);
			if (!ret) {
				ret = platform_device_add(vout_device);
				if (ret)
					goto err_device_put;
			} else
				goto err_device_put;
		} else {
			ret = -ENOMEM;
			goto err_unregister_driver;
		}
	}
	return ret;

err_device_put:
	platform_device_put(vout_device);
err_unregister_driver:
	platform_driver_unregister(&sirfsoc_vout);
	return ret;
}

static void __exit sirfsoc_vout_exit(void)
{
	platform_device_unregister(vout_device);
	platform_driver_unregister(&sirfsoc_vout);
}

module_init(sirfsoc_vout_init);
module_exit(sirfsoc_vout_exit);

MODULE_DESCRIPTION("SirfSoc Video Output driver");
MODULE_AUTHOR("Renwei Wu<renwei.wu@csr.com>");
MODULE_LICENSE("GPL v2");
