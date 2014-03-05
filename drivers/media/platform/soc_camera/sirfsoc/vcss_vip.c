/*
 * CSR SiRFprima2 VIP library definitions
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/io.h>
#include "vip_defs.h"

/* Global vip configuration */
struct vip_config vip_config = {
	.initialized = false,
};

static void __vip_reset(void)
{
	u32 val;

	val = vip_read_reg(CAM_CTRL);

	/* reset camera */
	vip_write_reg(CAM_CTRL, val | CAM_TS_CTRL_INIT);
	vip_write_reg(CAM_CTRL, val & ~CAM_TS_CTRL_INIT);
}

static void __vip_setup(void)
{
	u32 val;

	/* Reset camera */
	__vip_reset();

	/* Disable camera YCOUNT interrupt */
	vip_write_reg(CAM_INT_COUNT, 0x7fff7fff);
	/* Disable all camera interrupt at start */
	vip_write_reg(CAM_INT_EN, 0);

	/* Set active window with maximum values */
	vip_write_reg(CAM_START, 0x4fff4fff);
	vip_write_reg(CAM_END,   0xffffffff);

	/* Work in continnous mode, bypass yuv2rgb) */
	vip_write_reg(CAM_CTRL, 0);

	/* Default select pxd_data[7:0] as valid data */
	val = CAM_PS_PIXEL_SHIFT(VCSS_VIP_PIXELSET_DATAPIN_0TO7);
	vip_write_reg(CAM_PIXEL_SHIFT, val);

	/* Disable FIFO */
	vip_write_reg(CAM_FIFO_OP_REG, 0);

	/* Set FIFO config data, high check, low check and stop check. */
	val = CAM_FIFO_LEVEL_CHK_FIFO_SC(0x4) |
		CAM_FIFO_LEVEL_CHK_FIFO_LC(0x8) |
		CAM_FIFO_LEVEL_CHK_FIFO_HC(0x10);
	vip_write_reg(CAM_FIFO_LEVEL_CHECK, val);

	val = CAM_DMA_CTRL_DMA_FLUSH;
	/* exchange word order in DWORD of the input data from decoder */
	val |= CAM_DMA_CTRL_ENDIAN_MODE(VCSS_VIP_DMA_ENDIAN_WXDW);
	vip_write_reg(CAM_DMA_CTRL, val);
}

static void __vip_set_size(void)
{
	vip_write_reg(CAM_START,
		CAM_START_XS(vip_config.setting.src_rect.left) |
		CAM_START_YS(vip_config.setting.src_rect.top));
	vip_write_reg(CAM_END,
		CAM_END_XE(vip_config.setting.src_rect.right) |
		CAM_END_YE(vip_config.setting.src_rect.bottom));
}

static void __vip_set_params(void)
{
	u32 val;
	u8 input_fmt;
	u8 input_pixel_stride;
	u8 output_fmt;
	bool yuv2rgb = false;

	switch (vip_config.setting.src_fmt) {
	case VCSS_PIXELFORMAT_YUYV:
		input_fmt = VCSS_VIP_CTRL_YUVSEQ_YUYV;
		input_pixel_stride = 2;
		break;
	case VCSS_PIXELFORMAT_UYYV:
		input_fmt = VCSS_VIP_CTRL_YUVSEQ_UYYV;
		input_pixel_stride = 2;
		break;
	case VCSS_PIXELFORMAT_YUVY:
		input_fmt = VCSS_VIP_CTRL_YUVSEQ_YUVY;
		input_pixel_stride = 2;
		break;
	case VCSS_PIXELFORMAT_UYVY:
		input_fmt = VCSS_VIP_CTRL_YUVSEQ_UYVY;
		input_pixel_stride = 2;
		break;
	case VCSS_PIXELFORMAT_YVYU:
		input_fmt = VCSS_VIP_CTRL_YUVSEQ_YVYU;
		input_pixel_stride = 2;
		break;
	case VCSS_PIXELFORMAT_VYYU:
		input_fmt = VCSS_VIP_CTRL_YUVSEQ_VYYU;
		input_pixel_stride = 2;
		break;
	case VCSS_PIXELFORMAT_YVUY:
		input_fmt = VCSS_VIP_CTRL_YUVSEQ_YVUY;
		input_pixel_stride = 2;
		break;
	case VCSS_PIXELFORMAT_VYUY:
		input_fmt = VCSS_VIP_CTRL_YUVSEQ_VYUY;
		input_pixel_stride = 2;
		break;
	default:
		VIP_ERR("%s(%d): unknown input format 0x%x\n",
			__func__, __LINE__,
			vip_config.setting.src_fmt);
		return;
	}

	switch (vip_config.setting.dst_fmt) {
	case VCSS_PIXELFORMAT_UNKNOWN:
		yuv2rgb = false;
		break;
	case VCSS_PIXELFORMAT_565:
		yuv2rgb = true;
		output_fmt = VCSS_VIP_CTRL_OUT_RGB565;
		break;
	case VCSS_PIXELFORMAT_8880:
		yuv2rgb = true;
		output_fmt   = VCSS_VIP_CTRL_OUT_RGB888;
		break;
	default:
		VIP_ERR("%s(%d): unknown output format 0x%x\n",
			__func__, __LINE__,
			vip_config.setting.dst_fmt);
		return;
	}

	val = vip_read_reg(CAM_CTRL);
	val |= CAM_CTRL_YUV_FORMAT(input_fmt);

	if (vip_config.setting.flag & VCSS_VIP_CTRL_PXCLK_CTRL)
		val |= CAM_CTRL_PXCLK_CTRL;

	if (vip_config.setting.flag & VCSS_VIP_CTRL_HSYNC_CTRL)
		val |= CAM_CTRL_HSYNC_CTRL;

	if (vip_config.setting.flag & VCSS_VIP_CTRL_VSYNC_CTRL)
		val |= CAM_CTRL_VSYNC_CTRL;

	if (vip_config.setting.flag & VCSS_VIP_CTRL_PIXCLK_INV)
		val |= CAM_CTRL_PIXCLK_INV;

	if (vip_config.setting.flag & VCSS_VIP_CTRL_HSYNC_INV)
		val |= CAM_CTRL_HSYNC_INV;

	if (vip_config.setting.flag & VCSS_VIP_CTRL_VSYNC_INV)
		val |= CAM_CTRL_VSYNC_INV;

	if (vip_config.setting.flag & VCSS_VIP_CTRL_CCIR656_EN)
		val |= CAM_CTRL_CCIR656_EN;

	if (vip_config.setting.flag & VCSS_VIP_CTRL_SINGLE_MODE)
		val |= CAM_CTRL_SINGLE;
	else
		val &= ~CAM_CTRL_SINGLE;

	if (vip_config.setting.flag & VCSS_VIP_CTRL_PAD_MUX_UPLI)
		val |= CAM_CTRL_PAD_MUX_ON_UPLI;

	if (yuv2rgb) {
		vip_write_reg(CAM_YUV_COEFR, 0x12A00198);
		vip_write_reg(CAM_YUV_COEFG, 0x12A190D0);
		vip_write_reg(CAM_YUV_COEFB, 0x12A81000);
		vip_write_reg(CAM_YUV_OFFSET, 0x115220DF);

		val |= CAM_CTRL_YUVRGB;
		val |= CAM_CTRL_OUT_FORMAT(output_fmt);
	} else {
		val &= ~CAM_CTRL_YUVRGB;
	}

	vip_write_reg(CAM_CTRL, val);
	vip_write_reg(CAM_PIXEL_SHIFT,
		CAM_PS_PIXEL_SHIFT(vip_config.setting.pixel_bit_sel));

	/* first column, first row */
	vip_write_reg(CAM_INT_COUNT, 0x00100001);
	vip_write_reg(CAM_DMA_LEN,  0);

	__vip_set_size();
}

static void __vip_start(void)
{
	u32 val;

	/* reset fifo */
	val = vip_read_reg(CAM_FIFO_OP_REG);
	vip_write_reg(CAM_FIFO_OP_REG, val | CAM_FIFO_OP_FIFO_RESET);
	vip_write_reg(CAM_FIFO_OP_REG, val & ~CAM_FIFO_OP_FIFO_RESET);

	/* clear all interrupts */
	vip_write_reg(CAM_INT_CTRL, VCSS_VIP_INTMASK_ALL);

	/* Open Overflow and underflow interrupt */
	vip_write_reg(CAM_INT_EN, VCSS_VIP_INTMASK_SENSOR |
		VCSS_VIP_INTMASK_FIFO_UFLOW |
		VCSS_VIP_INTMASK_FIFO_OFLOW);

	/* Start FIFO transfer to DMA */
	vip_write_reg(CAM_FIFO_OP_REG, CAM_FIFO_OP_FIFO_START);
}

static void __vip_stop(void)
{
	/* Stop the FIFO first */
	vip_write_reg(CAM_FIFO_OP_REG, 0);

	/* Disable camera interupt */
	vip_write_reg(CAM_INT_EN, 0);
}

static void __vip_reset_fifo(void)
{
	u32 val;

	val = vip_read_reg(CAM_FIFO_OP_REG);
	vip_write_reg(CAM_FIFO_OP_REG, val | CAM_FIFO_OP_FIFO_RESET);
	vip_write_reg(CAM_FIFO_OP_REG, val & ~CAM_FIFO_OP_FIFO_RESET);
}

static u32 vip_get_interrupts(void)
{
	VIP_ENTRY("%s\n", __func__);

	return (vip_read_reg(CAM_INT_EN) &
		vip_read_reg(CAM_INT_CTRL)) &
		VCSS_VIP_INTMASK_ALL;
}

static void vip_clear_interrupts(u32 status)
{
	VIP_ENTRY("%s\n", __func__);

	vip_write_reg(CAM_INT_CTRL, status & VCSS_VIP_INTMASK_ALL);
}

static u32 vip_get_fid(void)
{
	return (vip_read_reg(CAM_CTRL) & CAM_CTRL_FID) ? 1 : 0;
}

static bool __vip_is_busy(void)
{
	u32 val;

	val = vip_read_reg(CAM_FIFO_OP_REG);
	if (val & CAM_FIFO_OP_FIFO_START)
		return true;
	else
		return false;
}

static void vip_initialize(void *base, void *dma_base)
{
	VIP_ENTRY("%s\n", __func__);

	if (!vip_config.initialized) {
		memset(&vip_config, 0, sizeof(vip_config));
		vip_config.initialized = true;
		vip_config.base = base;
		vip_config.dma_base = dma_base;
	}

	vip_config.ref_count++;
}

static void vip_terminate(void)
{
	VIP_ENTRY("%s\n", __func__);

	vip_config.ref_count--;

	if (vip_config.ref_count == 0) {
		vip_config.initialized = false;
		vip_config.base = NULL;
		vip_config.dma_base = NULL;
	}
}

static bool vip_set_params(struct vcss_vip_params *params)
{
	VIP_ENTRY("%s\n", __func__);

	vip_config.setting = *params;
	__vip_setup();
	__vip_set_params();

	return true;
}

static void vip_set_base(u32 addr)
{

	VIP_ENTRY("%s\n", __func__);
	vip_config.setting.dst_base = addr;
	return;
}

static bool vip_start(bool loop_mode)
{
	VIP_ENTRY("%s\n", __func__);

	vip_config.setting.loop_mode = loop_mode;

	__vip_start();

	return true;
}

static bool vip_stop(void)
{
	VIP_ENTRY("%s\n", __func__);

	__vip_stop();

	return true;
}


static void vip_sleep(void)
{

	VIP_ENTRY("%s\n", __func__);
}

static void vip_wakeup(void)
{
	VIP_ENTRY("%s\n", __func__);

	__vip_setup();
	__vip_set_params();

	return;
}

static void vip_reset_fifo(void)
{
	VIP_ENTRY("%s\n", __func__);

	__vip_reset_fifo();
}
static bool vip_reset(void)
{
	VIP_ENTRY("%s\n", __func__);

	__vip_reset();

	return true;
}

static bool vip_is_busy(void)
{
	VIP_ENTRY("%s\n", __func__);

	return __vip_is_busy();
}


static void vip_save_config(void)
{
	VIP_ENTRY("%s\n", __func__);

	memcpy(&vip_config.saved_setting, &vip_config.setting,
		sizeof(vip_config.setting));
}

static void vip_restore_config(void)
{
	VIP_ENTRY("%s\n", __func__);

	memcpy(&vip_config.setting, &vip_config.saved_setting,
		sizeof(vip_config.setting));

	__vip_set_params();
}

static void vip_print_registers(void)
{
	VIP_DUMP("VIP registers\n");
	VIP_DUMP("CAM_COUNT             = 0x%.8x\n",
		vip_read_reg(CAM_COUNT));
	VIP_DUMP("CAM_INT_COUNT         = 0x%.8x\n",
		vip_read_reg(CAM_INT_COUNT));
	VIP_DUMP("CAM_START             = 0x%.8x\n",
		vip_read_reg(CAM_START));
	VIP_DUMP("CAM_END               = 0x%.8x\n",
		vip_read_reg(CAM_END));
	VIP_DUMP("CAM_CTRL              = 0x%.8x\n",
		vip_read_reg(CAM_CTRL));
	VIP_DUMP("CAM_PIXEL_SHIFT       = 0x%.8x\n",
		vip_read_reg(CAM_PIXEL_SHIFT));
	VIP_DUMP("CAM_YUV_COEFR         = 0x%.8x\n",
		vip_read_reg(CAM_YUV_COEFR));
	VIP_DUMP("CAM_YUV_COEFG         = 0x%.8x\n",
		vip_read_reg(CAM_YUV_COEFG));
	VIP_DUMP("CAM_YUV_COEFB         = 0x%.8x\n",
		vip_read_reg(CAM_YUV_COEFB));
	VIP_DUMP("CAM_YUV_OFFSET        = 0x%.8x\n",
		vip_read_reg(CAM_YUV_OFFSET));
	VIP_DUMP("CAM_INT_EN            = 0x%.8x\n",
		vip_read_reg(CAM_INT_EN));
	VIP_DUMP("CAM_INT_CTRL          = 0x%.8x\n",
		vip_read_reg(CAM_INT_CTRL));
	VIP_DUMP("CAM_VSYNC_CTRL        = 0x%.8x\n",
		vip_read_reg(CAM_VSYNC_CTRL));
	VIP_DUMP("CAM_HSYNC_CTRL        = 0x%.8x\n",
		vip_read_reg(CAM_HSYNC_CTRL));
	VIP_DUMP("CAM_PXCLK_CTRL        = 0x%.8x\n",
		vip_read_reg(CAM_PXCLK_CTRL));
	VIP_DUMP("CAM_VSYNC_HSYNC       = 0x%.8x\n",
		vip_read_reg(CAM_VSYNC_HSYNC));
	VIP_DUMP("CAM_TIMING_CTRL       = 0x%.8x\n",
		vip_read_reg(CAM_TIMING_CTRL));
	VIP_DUMP("CAM_DMA_CTRL          = 0x%.8x\n",
		vip_read_reg(CAM_DMA_CTRL));
	VIP_DUMP("CAM_DMA_LEN           = 0x%.8x\n",
		vip_read_reg(CAM_DMA_LEN));
	VIP_DUMP("CAM_FIFO_CTRL_REG     = 0x%.8x\n",
		vip_read_reg(CAM_FIFO_CTRL_REG));
	VIP_DUMP("CAM_FIFO_LEVEL_CHECK  = 0x%.8x\n",
		vip_read_reg(CAM_FIFO_LEVEL_CHECK));
	VIP_DUMP("CAM_FIFO_OP_REG       = 0x%.8x\n",
		vip_read_reg(CAM_FIFO_OP_REG));
	VIP_DUMP("CAM_FIFO_STATUS_REG   = 0x%.8x\n",
		vip_read_reg(CAM_FIFO_STATUS_REG));
	VIP_DUMP("CAM_RD_FIFO_DATA      = 0x%.8x\n",
		vip_read_reg(CAM_RD_FIFO_DATA));
	VIP_DUMP("CAM_TS_CTRL           = 0x%.8x\n",
		vip_read_reg(CAM_TS_CTRL));

}

static void vip_lock(void)
{
	VIP_ENTRY("%s\n", __func__);
}

static void vip_unlock(void)
{
	VIP_ENTRY("%s\n", __func__);
}

static void vip_reserved(void)
{
	VIP_ENTRY("%s\n", __func__);
}

void vcss_install_vip_ops(struct vcss_vip_ops *vip_ops)
{
	VIP_ENTRY("%s\n", __func__);

	memset(vip_ops, 0, sizeof(*vip_ops));

	vip_ops->initialize = vip_initialize;
	vip_ops->terminate  = vip_terminate;
	vip_ops->set_params = vip_set_params;
	vip_ops->set_base = vip_set_base;
	vip_ops->start = vip_start;
	vip_ops->stop = vip_stop;
	vip_ops->sleep = vip_sleep;
	vip_ops->wakeup = vip_wakeup;

	vip_ops->reset = vip_reset;
	vip_ops->is_busy = vip_is_busy;
	vip_ops->save_config = vip_save_config;
	vip_ops->restore_config = vip_restore_config;
	vip_ops->get_interrupts = vip_get_interrupts;
	vip_ops->clear_interrupts = vip_clear_interrupts;

	vip_ops->reset_fifo = vip_reset_fifo;
	vip_ops->get_fid = vip_get_fid;

	vip_ops->print_registers = vip_print_registers;

	/* Optional */
	vip_ops->lock = vip_lock;
	vip_ops->unlock = vip_unlock;
	vip_ops->reserved = vip_reserved;
}
