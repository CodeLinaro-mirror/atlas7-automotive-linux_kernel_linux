
/*
 * CSR SiRFprima2 VIP library interface
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __SIRFSOC_VCSS_VIP_H
#define __SIRFSOC_VCSS_VIP_H

struct vcss_rect {
	int    left;
	int    top;
	int    right;
	int    bottom;
};

/*
 * pixel format definition for CSR video capture subsystem
 */
enum vcss_pixelformat {
	VCSS_PIXELFORMAT_UNKNOWN = 0,

	/* RGB format goes here */
	VCSS_PIXELFORMAT_1BPP = 1,
	VCSS_PIXELFORMAT_2BPP = 2,
	VCSS_PIXELFORMAT_4BPP = 3,
	VCSS_PIXELFORMAT_8BPP = 4,

	VCSS_PIXELFORMAT_565 = 5,
	VCSS_PIXELFORMAT_5551 = 6,
	VCSS_PIXELFORMAT_4444 = 7,
	VCSS_PIXELFORMAT_5550 = 8,
	VCSS_PIXELFORMAT_8880 = 9,
	VCSS_PIXELFORMAT_8888 = 10,

	VCSS_PIXELFORMAT_556 = 11,
	VCSS_PIXELFORMAT_655 = 12,
	VCSS_PIXELFORMAT_0888 = 13,
	VCSS_PIXELFORMAT_666 = 14,

	/* some generic types */
	VCSS_PIXELFORMAT_15BPPGENERIC = 15,
	VCSS_PIXELFORMAT_16BPPGENERIC = 16,
	VCSS_PIXELFORMAT_24BPPGENERIC = 17,
	VCSS_PIXELFORMAT_32BPPGENERIC = 18,

	/* FOURCC format goes here */
	VCSS_PIXELFORMAT_UYVY = 19,
	VCSS_PIXELFORMAT_UYNV = 20,
	VCSS_PIXELFORMAT_YUY2 = 21,
	VCSS_PIXELFORMAT_YUYV = 22,
	VCSS_PIXELFORMAT_YUNV = 23,
	VCSS_PIXELFORMAT_YVYU = 24,
	VCSS_PIXELFORMAT_VYUY = 25,
	VCSS_PIXELFORMAT_UYYV = 26,
	VCSS_PIXELFORMAT_YUVY = 27,
	VCSS_PIXELFORMAT_VYYU = 28,
	VCSS_PIXELFORMAT_YVUY = 29,

	VCSS_PIXELFORMAT_IMC2 = 30,
	VCSS_PIXELFORMAT_YV12 = 31,
	VCSS_PIXELFORMAT_I420 = 32,

	VCSS_PIXELFORMAT_IMC1 = 33,
	VCSS_PIXELFORMAT_IMC3 = 34,
	VCSS_PIXELFORMAT_IMC4 = 35,
	VCSS_PIXELFORMAT_NV12 = 36,
	VCSS_PIXELFORMAT_NV21 = 37,
	VCSS_PIXELFORMAT_UYVI = 38,
	VCSS_PIXELFORMAT_VLVQ = 39,

	VCSS_PIXELFORMAT_CUSTOMFORMAT = 0X1000
};

/* VIP Control Register Definition */
#define     VCSS_VIP_CTRL_PXCLK_CTRL        0x00000001
#define     VCSS_VIP_CTRL_HSYNC_CTRL        0x00000002
#define     VCSS_VIP_CTRL_VSYNC_CTRL        0x00000004
#define     VCSS_VIP_CTRL_PIXCLK_INV        0x00000008
#define     VCSS_VIP_CTRL_HSYNC_INV         0x00000010
#define     VCSS_VIP_CTRL_VSYNC_INV         0x00000020

#define     VCSS_VIP_CTRL_HOR_MIRROR        0x00100000
#define     VCSS_VIP_CTRL_CAP_FROM_ODD      0x00200000
#define     VCSS_VIP_CTRL_CAP_FROM_EVEN     0x00400000
#define     VCSS_VIP_CTRL_PAD_MUX_UPLI      0x00800000

#define     VCSS_VIP_CTRL_CCIR656_EN        0x01000000
#define     VCSS_VIP_CTRL_FID               0x02000000
#define     VCSS_VIP_CTRL_SINGLE_MODE       0x04000000

#define     VCSS_VIP_CTRL_OUT_RGB888        0   /* RGB 8:8:8 */
#define     VCSS_VIP_CTRL_OUT_RGB655        1   /* RGB 6:5:5 */
#define     VCSS_VIP_CTRL_OUT_RGB556        2   /* RGB 5:5:6 */
#define     VCSS_VIP_CTRL_OUT_RGB565        3   /* RGB 5:6:5 */

#define     VCSS_VIP_CTRL_NO_X_SCALE        0   /* no contraction  */
#define     VCSS_VIP_CTRL_X_SCALE_1_2       1   /* 1:2 contraction */
#define     VCSS_VIP_CTRL_X_SCALE_1_4       2   /* 1:4 contraction */
#define     VCSS_VIP_CTRL_X_SCALE_1_8       3   /* 1:8 contraction */

#define     VCSS_VIP_CTRL_NO_Y_SCALE        0   /* no contraction  */
#define     VCSS_VIP_CTRL_Y_SCALE_1_2       1   /* 1:2 contraction */
#define     VCSS_VIP_CTRL_Y_SCALE_1_4       2   /* 1:4 contraction */
#define     VCSS_VIP_CTRL_Y_SCALE_1_8       3   /* 1:8 contraction */

/* DMA control register */
#define     VCSS_VIP_DMA_ENDIAN_NOT_CHG     0   /* endian mode not change */
#define     VCSS_VIP_DMA_ENDIAN_BXDW        1   /* byte exchange in dword */
#define     VCSS_VIP_DMA_ENDIAN_WXDW        2   /* word exchange in dword */
#define     VCSS_VIP_DMA_ENDIAN_BXW         3   /* byte exchange in word  */


/* Value definitions for the bit fields of videoPort registers */

/* Control register, bit field yuv_format */
#define     VCSS_VIP_CTRL_YUVSEQ_YUYV       0
#define     VCSS_VIP_CTRL_YUVSEQ_UYYV       1
#define     VCSS_VIP_CTRL_YUVSEQ_YUVY       2
#define     VCSS_VIP_CTRL_YUVSEQ_UYVY       3
#define     VCSS_VIP_CTRL_YUVSEQ_YVYU       4
#define     VCSS_VIP_CTRL_YUVSEQ_VYYU       5
#define     VCSS_VIP_CTRL_YUVSEQ_YVUY       6
#define     VCSS_VIP_CTRL_YUVSEQ_VYUY       7


/* Pixel shift Setting register, bit field yuv_format */
#define     VCSS_VIP_PIXELSET_DATAPIN_0TO15 0
#define     VCSS_VIP_PIXELSET_DATAPIN_0TO7  1
#define     VCSS_VIP_PIXELSET_DATAPIN_1TO8  2
#define     VCSS_VIP_PIXELSET_DATAPIN_2TO9  3
#define     VCSS_VIP_PIXELSET_DATAPIN_3TO10 4
#define     VCSS_VIP_PIXELSET_DATAPIN_4TO11 5
#define     VCSS_VIP_PIXELSET_DATAPIN_7TO14 6
#define     VCSS_VIP_PIXELSET_DATAPIN_8TO15 7

/* Interrupt Mask definition */
#define     VCSS_VIP_INTMASK_ALL            0x00000007
#define     VCSS_VIP_INTMASK_SENSOR         0x00000001
#define     VCSS_VIP_INTMASK_FIFO_OFLOW     0x00000002   /* FIFO overflow  */
#define     VCSS_VIP_INTMASK_FIFO_UFLOW     0x00000004   /* FIFO underflow */

struct vcss_vip_params {
	enum vcss_pixelformat src_fmt; /* source format */
	u32 src_wstride_pixel; /* width stride in pixel unit */
	u32 src_hstride_pixel; /* heigth stride in pixel unit */
	enum vcss_pixelformat dst_fmt; /* output format*/
	u32 dst_base; /* physical address of dest buffer */
	u32 dst_wstride_pixel; /* width stride in pixel unit */
	u32 dst_hstride_pixel; /* heigth stride in pixel unit */
	u32 pixel_bit_sel;
	u32 flag;
	bool frame_interrupt;
	bool loop_mode;
	struct vcss_rect src_rect;
	struct vcss_rect dst_rect;
};

struct vcss_vip_ops {
	void (*initialize)(void *base, void *dma_base);
	void (*terminate)(void);
	bool (*set_params)(struct vcss_vip_params *params);
	void (*set_base)(u32 addr);
	bool (*start)(bool);
	bool (*stop)(void);
	void (*sleep)(void);
	void (*wakeup)(void);
	bool (*reset)(void);
	bool (*is_busy)(void);
	void (*save_config)(void);
	void (*restore_config)(void);
	void (*reset_fifo)(void);
	u32 (*get_interrupts)(void);
	void (*clear_interrupts)(u32);
	u32 (*get_fid)(void);
	void (*print_registers)(void);
	void (*lock)(void);
	void (*unlock)(void);
	void (*reserved)(void);
};

void vcss_install_vip_ops(struct vcss_vip_ops *vip_ops);

#endif
