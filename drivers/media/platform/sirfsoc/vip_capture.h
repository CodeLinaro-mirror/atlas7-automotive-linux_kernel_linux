/*
 * CSR SiRFSoc VIP host driver header
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef _SIRFSOC_VIP_CAPTURE_H
#define _SIRFSOC_VIP_CAPTURE_H

#include <linux/sched.h>
#include <linux/dmaengine.h>
#include <linux/pinctrl/consumer.h>
#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/videodev2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-of.h>
#include <linux/v4l2-mediabus.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>


#define VIP_MAX_SUBDEVS		2
#define SUBDEV_MAX_INPUTS	2

#define VIP_DEFAULT_WIDTH	720
#define VIP_DEFAULT_HEIGHT	480


/* Interrupt Mask definition */
#define     VIP_INTMASK_ALL            0x00000007
#define     VIP_INTMASK_SENSOR         0x00000001
#define     VIP_INTMASK_FIFO_OFLOW     0x00000002   /* FIFO overflow  */
#define     VIP_INTMASK_FIFO_UFLOW     0x00000004   /* FIFO underflow */

/* definition for parallel bus data pins configuration */
#define     VIP_PIXELSET_DATAPIN_0TO15	0
#define     VIP_PIXELSET_DATAPIN_0TO7	1
#define     VIP_PIXELSET_DATAPIN_1TO8	2
#define     VIP_PIXELSET_DATAPIN_2TO9	3
#define     VIP_PIXELSET_DATAPIN_3TO10	4
#define     VIP_PIXELSET_DATAPIN_4TO11	5
#define     VIP_PIXELSET_DATAPIN_7TO14	6
#define     VIP_PIXELSET_DATAPIN_8TO15	7

/* pixel format definition, middle layer bwtween HW and user */
enum vip_pixelfmt {
	VIP_PIXELFORMAT_UNKNOWN = 0,

	/* RGB format goes here */
	VIP_PIXELFORMAT_1BPP = 1,
	VIP_PIXELFORMAT_2BPP = 2,
	VIP_PIXELFORMAT_4BPP = 3,
	VIP_PIXELFORMAT_8BPP = 4,

	VIP_PIXELFORMAT_565 = 5,
	VIP_PIXELFORMAT_5551 = 6,
	VIP_PIXELFORMAT_4444 = 7,
	VIP_PIXELFORMAT_5550 = 8,
	VIP_PIXELFORMAT_8880 = 9,
	VIP_PIXELFORMAT_8888 = 10,

	VIP_PIXELFORMAT_556 = 11,
	VIP_PIXELFORMAT_655 = 12,
	VIP_PIXELFORMAT_0888 = 13,
	VIP_PIXELFORMAT_666 = 14,

	/* some generic types */
	VIP_PIXELFORMAT_15BPPGENERIC = 15,
	VIP_PIXELFORMAT_16BPPGENERIC = 16,
	VIP_PIXELFORMAT_24BPPGENERIC = 17,
	VIP_PIXELFORMAT_32BPPGENERIC = 18,

	/* FOURCC format goes here */
	VIP_PIXELFORMAT_UYVY = 19,
	VIP_PIXELFORMAT_UYNV = 20,
	VIP_PIXELFORMAT_YUY2 = 21,
	VIP_PIXELFORMAT_YUYV = 22,
	VIP_PIXELFORMAT_YUNV = 23,
	VIP_PIXELFORMAT_YVYU = 24,
	VIP_PIXELFORMAT_VYUY = 25,
	VIP_PIXELFORMAT_UYYV = 26,
	VIP_PIXELFORMAT_YUVY = 27,
	VIP_PIXELFORMAT_VYYU = 28,
	VIP_PIXELFORMAT_YVUY = 29,

	VIP_PIXELFORMAT_IMC2 = 30,
	VIP_PIXELFORMAT_YV12 = 31,
	VIP_PIXELFORMAT_I420 = 32,

	VIP_PIXELFORMAT_IMC1 = 33,
	VIP_PIXELFORMAT_IMC3 = 34,
	VIP_PIXELFORMAT_IMC4 = 35,
	VIP_PIXELFORMAT_NV12 = 36,
	VIP_PIXELFORMAT_NV21 = 37,
	VIP_PIXELFORMAT_UYVI = 38,
	VIP_PIXELFORMAT_VLVQ = 39,

	VIP_PIXELFORMAT_CUSTOMFORMAT = 0x1000
};

struct vip_rect {
	int    left;
	int    top;
	int    right;
	int    bottom;
};

struct vip_control {
	enum vip_pixelfmt	input_fmt;
	enum vip_pixelfmt	output_fmt;
	bool	pixclk_internal;
	bool	hsync_internal;
	bool	vsync_internal;
	bool	pixclk_invert;
	bool	hsync_invert;
	bool	vsync_invert;
	bool	ccir565_en;
	bool	single_cap;
	bool	pad_mux_on_upli;
};


/* buffer for one video frame */
struct vip_buffer {
	/* common v4l buffer stuff -- must be first */
	struct vb2_buffer	vb;
	struct list_head	list;
	struct v4l2_pix_format	*fmt;
};


/* VIP supported format definition */
struct vip_format {
	unsigned char		*desc;
	unsigned int		pixelformat;
	unsigned int		bpp;	/* Bytes per pixel */
	enum v4l2_mbus_pixelcode mbus_code;
};


/* source subdev information */
struct vip_subdev_info {
	bool			interlaced;
	struct video_device	*vdev;
	struct v4l2_subdev	*sd;
	struct v4l2_ctrl_handler ctrl_handler;
	unsigned int		num_inputs;
	unsigned int		cur_input;
	struct v4l2_input	inputs[SUBDEV_MAX_INPUTS];
	struct v4l2_of_endpoint	endpoint;
	struct vip_dev		*host;
};

/*
 *  abstraction for sirfsoc VIP(Video Input Port) hardware module
 */
struct vip_dev {
	struct device		*dev;
	struct v4l2_device	v4l2_dev;

	struct vip_subdev_info	subdev[VIP_MAX_SUBDEVS];
	unsigned int		num_subdev;

	struct clk		*clk;

	unsigned int		irq;
	struct resource		*res;
	void __iomem		*io_base;
	unsigned int		video_limit;

	dma_addr_t		dst_start;
	struct dma_chan		*dma_chan;

	struct list_head	capture;

	spinlock_t		lock;
	struct mutex		host_lock;

	struct vb2_buffer	*vb2_active;
	struct vb2_queue	vb2_vidq;
	struct vb2_alloc_ctx	*alloc_ctx;

	struct vip_rect		target_rect;

	unsigned int		use_count;

	/*
	 * Video format information.
	 * subdev_format is kept in a form that we can use to pass to the
	 * tv_decoder/hdmi_receiver.
	 * We always run the sub devices in VIP default resolution if they
	 * can be set, or run them in their default format.
	 * user_format is requested by end user, output format from VIP.
	 */
	struct v4l2_pix_format	subdev_format;
	struct v4l2_pix_format	user_format;
	enum v4l2_mbus_pixelcode mbus_code;
};

/*
 * hw regisger definition
 */

/* Camera COUNT register */
#define CAM_COUNT			0x00
#define CAM_COUNT_XC_MASK		(0xFFFF << 0)
#define CAM_COUNT_YC_MASK		(0xFFFF << 16)

/* Camera INT_COUNT register */
#define CAM_INT_COUNT			0x04
#define CAM_INT_COUNT_XI_MASK		(0xFFFF << 0)
#define CAM_INT_COUNT_YI_MASK		(0xFFFF << 16)
#define CAM_INT_COUNT_XI(x)		(((x) & 0xFFFF) << 0)
#define CAM_INT_COUNT_YI(x)		(((x) & 0xFFFF) << 16)

/* Camera start x and start y register */
#define CAM_START			0x08
#define CAM_START_XS_MASK		(0xFFFF << 0)
#define CAM_START_YS_MASK		(0xFFFF << 16)
#define CAM_START_XS(x)			(((x) & 0xFFFF) << 0)
#define CAM_START_YS(x)			(((x) & 0xFFFF) << 16)

/* Camera end x and end y register */
#define CAM_END				0x0C
#define CAM_END_XE_MASK			(0xFFFF << 0)
#define CAM_END_YE_MASK			(0xFFFF << 16)
#define CAM_END_XE(x)			(((x) & 0xFFFF) << 0)
#define CAM_END_YE(x)			(((x) & 0xFFFF) << 16)

/* Camera Control register */
#define CAM_CTRL			0x10
#define CAM_CTRL_INIT			(1 << 31)
#define CAM_CTRL_FID			(1 << 25)
#define CAM_CTRL_CCIR656_EN		(1 << 24)
#define CAM_CTRL_PAD_MUX_ON_UPLI	(1 << 23)
#define CAM_CTRL_CAP_FROM_EVEN		(1 << 22)
#define CAM_CTRL_CAP_FROM_ODD		(1 << 21)
#define CAM_CTRL_HOR_MIRROR		(1 << 20)
#define CAM_CTRL_Y_SCA_MASK		(0x3 << 18)
#define CAM_CTRL_Y_SCA_0		(0 << 18)
#define CAM_CTRL_Y_SCA_1VS2		(1 << 18)
#define CAM_CTRL_Y_SCA_1VS4		(2 << 18)
#define CAM_CTRL_Y_SCA_1VS8		(3 << 18)
#define CAM_CTRL_X_SCA_MASK		(0x3 << 16)
#define CAM_CTRL_X_SCA_0		(0 << 16)
#define CAM_CTRL_X_SCA_1VS2		(1 << 16)
#define CAM_CTRL_X_SCA_1VS4		(2 << 16)
#define CAM_CTRL_X_SCA_1VS8		(3 << 16)
#define CAM_CTRL_YUV_TO_RGB		(1 << 14)
#define CAM_CTRL_OUT_FORMAT_MASK	(0x3 << 12)
#define CAM_CTRL_OUT_RGB888		(0 << 12)
#define CAM_CTRL_OUT_RGB655		(1 << 12)
#define CAM_CTRL_OUT_RGB556		(2 << 12)
#define CAM_CTRL_OUT_RGB565		(3 << 12)
#define CAM_CTRL_YUV_FORMAT_MASK	(0x7 << 9)
#define CAM_CTRL_INPUT_YUYV		(0 << 9)
#define CAM_CTRL_INPUT_UYYV		(1 << 9)
#define CAM_CTRL_INPUT_YUVY		(2 << 9)
#define CAM_CTRL_INPUT_UYVY		(3 << 9)
#define CAM_CTRL_INPUT_YVYU		(4 << 9)
#define CAM_CTRL_INPUT_VYYU		(5 << 9)
#define CAM_CTRL_INPUT_YVUY		(6 << 9)
#define CAM_CTRL_INPUT_VYUY		(7 << 9)
#define CAM_CTRL_INPUT_YUV		(1 << 8)
#define CAM_CTRL_INPUT_YCRCB		(0 << 8)
#define CAM_CTRL_PIXCLK_PS		(0 << 7)
#define CAM_CTRL_PIXCLK_IOCLK		(1 << 7)
#define CAM_CTRL_SINGLE			(1 << 6)
#define CAM_CTRL_VSYNC_INV		(1 << 5)
#define CAM_CTRL_HSYNC_INV		(1 << 4)
#define CAM_CTRL_PIXCLK_INV		(1 << 3)
#define CAM_CTRL_VSYNC_INTERNAL		(1 << 2)
#define CAM_CTRL_HSYNC_INTERNAL		(1 << 1)
#define CAM_CTRL_PXCLK_INTERNAL		(1 << 0)

/* Pixel data bit select setting */
#define CAM_PIXEL_SHIFT			0x14
#define CAM_PIXEL_SHIFT_MASK		(0x7 << 0)
#define CAM_PIXEL_SHIFT_0TO7		(1 << 0)


/* Red coefficent of YUV to RGB */
#define CAM_YUV_COEF1			0x18
#define CAM_YUV_COEF1_C3_MASK		(0x3FF << 20)
#define CAM_YUV_COEF1_C3(x)		(((x) & 0x3FF) << 20)
#define CAM_YUV_COEF1_C2_MASK		(0x3FF << 10)
#define CAM_YUV_COEF1_C2(x)		(((x) & 0x3FF) << 10)
#define CAM_YUV_COEF1_C1_MASK		(0x3FF << 0)
#define CAM_YUV_COEF1_C1(x)		(((x) & 0x3FF) << 0)

/* Green coefficent of YUV to RGB */
#define CAM_YUV_COEF2			0x1C
#define CAM_YUV_COEF2_C6_MASK		(0x3FF << 20)
#define CAM_YUV_COEF2_C6(x)		(((x) & 0x3FF) << 20)
#define CAM_YUV_COEF2_C5_MASK		(0x3FF << 10)
#define CAM_YUV_COEF2_C5(x)		(((x) & 0x3FF) << 10)
#define CAM_YUV_COEF2_C4_MASK		(0x3FF << 0)
#define CAM_YUV_COEF2_C4(x)		(((x) & 0x3FF) << 0)

/* Blue coefficent of YUV to RGB */
#define CAM_YUV_COEF3			0x20
#define CAM_YUV_COEF3_C9_MASK		(0x3FF << 20)
#define CAM_YUV_COEF3_C9(x)		(((x) & 0x3FF) << 20)
#define CAM_YUV_COEF3_C8_MASK		(0x3FF << 10)
#define CAM_YUV_COEF3_C8(x)		(((x) & 0x3FF) << 10)
#define CAM_YUV_COEF3_C7_MASK		(0x3FF << 0)
#define CAM_YUV_COEF3_C7(x)		(((x) & 0x3FF) << 0)

/* Offset for each coefficient for the YUV to RGB */
#define CAM_YUV_OFFSET			0x24
#define CAM_YUV_OFFSET_OFF3_MASK	(0x3FF << 20)
#define CAM_YUV_OFFSET_OFF3(x)		(((x) & 0x3FF) << 20)
#define CAM_YUV_OFFSET_OFF2_MASK	(0x3FF << 10)
#define CAM_YUV_OFFSET_OFF2(x)		(((x) & 0x3FF) << 10)
#define CAM_YUV_OFFSET_OFF1_MASK	(0x3FF << 0)
#define CAM_YUV_OFFSET_OFF1(x)		(((x) & 0x3FF) << 0)

/* Camera interrupt enable register */
#define CAM_INT_EN			0x28
#define CAM_INT_EN_TS_OVER		(1 << 3)
#define CAM_INT_EN_FIFO_UFLOW		(1 << 2)
#define CAM_INT_EN_FIFO_OFLOW		(1 << 1)
#define CAM_INT_EN_SENSOR_INT		(1 << 0)

/* Camera interrupt control(status&reset) register */
#define CAM_INT_CTRL			0x2C
#define CAM_INT_CTRL_TS_OVER		(1 << 3)
#define CAM_INT_CTRL_FIFO_UFLOW		(1 << 2)
#define CAM_INT_CTRL_FIFO_OFLOW		(1 << 1)
#define CAM_INT_CTRL_SENSOR_INT		(1 << 0)
#define CAM_INT_CTRL_MASK		(0x7 << 0)

/* VSYNC control register */
#define CAM_VSYNC_CTRL			0x30
#define CAM_VSYNC_CTRL_BLANK_NUM_MASK	(0xFFFF << 16)
#define CAM_VSYNC_CTRL_BLANK_NUM(x)	(((x) & 0xFFFF) << 16)
#define CAM_VSYNC_CTRL_ACT_NUM_MASK	(0xFFFF << 0)
#define CAM_VSYNC_CTRL_ACT_NUM(x)	(((x) & 0xFFFF) << 0)

/* HSYNC control register */
#define CAM_HSYNC_CTRL			0x34
#define CAM_HSYNC_CTRL_BLANK_NUM_MASK	(0xFFFF << 16)
#define CAM_HSYNC_CTRL_BLANK_NUM(x)	(((x) & 0xFFFF) << 16)
#define CAM_HSYNC_CTRL_ACT_NUM_MASK	(0xFFFF << 0)
#define CAM_HSYNC_CTRL_ACT_NUM(x)	(((x) & 0xFFFF) << 0)

/* PXCLK control register */
#define CAM_PXCLK_CTRL			0x38
#define CAM_PIXCLK_CTRL_NUM_MASK	(0xFFFF << 0)
#define CAM_PIXCLK_CTRL_NUM(x)		(((x) & 0xFFFF) << 0)

/* VSYNC and HSYNC width register */
#define CAM_VSYNC_HSYNC			0x3C
#define CAM_VH_VSYNC_WIDTH_MASK		(0xFFFF << 16)
#define CAM_VH_VSYNC_WIDTH(x)		(((x) & 0xFFFF) << 16)
#define CAM_VH_VSYNC_HSYNC_MASK		(0xFFFF << 0)
#define CAM_VH_VSYNC_HSYNC(x)		(((x) & 0xFFFF) << 0)

/* The timing control */
#define CAM_TIMING_CTRL			0x40
#define CAM_TIMING_CTRL_HSYNC_MASK	(1 << 3)
#define CAM_TIMING_CTRL_VSYNC_POLAR	(1 << 2)
#define CAM_TIMING_CTRL_HSYNC_POLAR	(1 << 1)
#define CAM_TIMING_CTRL_PCLK_POLAR	(1 << 0)

/* DMA control register */
#define CAM_DMA_CTRL			0x44
#define CAM_DMA_CTRL_ENDIAN_MODE_MASK	(0x3 << 4)
#define CAM_DMA_CTRL_ENDIAN_NO_CHG	(0 << 4)
#define CAM_DMA_CTRL_ENDIAN_BXDW	(1 << 4)
#define CAM_DMA_CTRL_ENDIAN_WXDW	(2 << 4)
#define CAM_DMA_CTRL_ENDIAN_BXW		(3 << 4)
#define CAM_DMA_CTRL_DMA_FLUSH		(1 << 2)
#define CAM_DMA_CTRL_DMA_OP		(0 << 0)
#define CAM_DMA_CTRL_IO_OP		(1 << 0)

/* DMA length register */
#define CAM_DMA_LEN			0x48

/* FIFO control register */
#define CAM_FIFO_CTRL_REG		0x4C
#define CAM_FIFO_CTRL_FIFO_THRES_MASK	(0xFFF << 2)
#define CAM_FIFO_CTRL_FIFO_THRES(x)	(((x) & 0xFFF) << 2)
#define CAM_FIFO_CTRL_FIFO_WIDTH_MASK	(0x3 << 0)
#define CAM_FIFO_CTRL_BYTE_MODE_FIFO	(0 << 0)
#define CAM_FIFO_CTRL_WORD_MODE_FIFO	(1 << 0)
#define CAM_FIFO_CTRL_DWORD_MODE_FIFO	(2 << 0)

/* FIFO level check register */
#define CAM_FIFO_LEVEL_CHECK		0x50
#define CAM_FIFO_LEVEL_CHK_FIFO_HC_MASK	(0x7F << 20)
#define CAM_FIFO_LEVEL_CHK_FIFO_HC(x)	(((x) & 0x7F) << 20)
#define CAM_FIFO_LEVEL_CHK_FIFO_LC_MASK	(0x7F << 10)
#define CAM_FIFO_LEVEL_CHK_FIFO_LC(x)	(((x) & 0x7F) << 10)
#define CAM_FIFO_LEVEL_CHK_FIFO_SC_MASK	(0x7F << 0)
#define CAM_FIFO_LEVEL_CHK_FIFO_SC(x)	(((x) & 0x7F) << 0)

/* FIFO operation register */
#define CAM_FIFO_OP_REG			0x54
#define CAM_FIFO_OP_FIFO_RESET		(1 << 1)
#define CAM_FIFO_OP_FIFO_START		(1 << 0)

/* FIFO status register */
#define CAM_FIFO_STATUS_REG		0x58
#define CAM_FIFO_STATUS_FIFO_EMPTY	(1 << 10)
#define CAM_FIFO_STATUS_FIFO_FULL	(1 << 9)
#define CAM_FIFO_STATUS_FIFO_LEVEL_MASK	(0x1FF << 0)
#define CAM_FIFO_STATUS_FIFO_LEVEL(x)	(((x) & 0x1FF) << 0)

/* FIFO data */
#define CAM_RD_FIFO_DATA		0x5C

/* TS control register */
#define CAM_TS_CTRL			0x60
#define CAM_TS_CTRL_INIT		(1 << 31)
#define CAM_TS_CTRL_BIG_ENDIAN		(1 << 7)
#define CAM_TS_CTRL_SINGLE		(1 << 6)
#define CAM_TS_CTRL_NEG_SAMPLE		(1 << 5)
#define CAM_TS_CTRL_VIP_TS		(1 << 4)

#endif
