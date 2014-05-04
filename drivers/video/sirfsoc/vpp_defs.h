/*
 * CSR sirfsoc VPP internal interface
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __SIRFSOC_VPP_DEFS_H
#define __SIRFSOC_VPP_DEFS_H

#include <linux/io.h>

#include "vdss_lcdc.h"
#include "vdss_vpp.h"
#include "vpp_regs.h"

struct sirfsoc_vpp_config {
	struct vpp_parms surf_stat;
	struct vdss_rect src_rect;
	struct vdss_rect dst_rect;

	bool continue_lock;
	bool show;
	void __iomem *vpp_regs;
	bool need_unmap;

	unsigned int ref_count;
	bool initialized;

	bool valid;
	bool usr_mode;

	bool uv_interleave;

	bool dma_interrupt_enabled;

	struct vpp_color_ctrl clr_ctrl;
	struct vpp_interlace_data interlace;

	short hue;
	short saturation;

	double hscaling_ratio_last;
	double vscaling_ratio_last;
};

struct sirfsoc_vpp_config vpp_config;

enum vpp_in_fomat {
	VPP_INFORMAT_UNKNOWN = 0,

	/* RGB format goes here */
	VPP_INFORMAT_YUV420,
	VPP_INFORMAT_Y0UY1V,
	VPP_INFORMAT_Y1UY0V,
	VPP_INFORMAT_Y0VY1U,
	VPP_INFORMAT_Y1VY0U,
	VPP_INFORMAT_UY0VY1,
	VPP_INFORMAT_UY1VY0,
	VPP_INFORMAT_VY0UY1,
	VPP_INFORMAT_VY1UY0
};

enum vpp_outformat {
	VPP_OUTFORMAT_UNKNOWN = 0,

	/* RGB format goes here */
	VPP_OUTFORMAT_RGB565,
	VPP_OUTFORMAT_RGB666,
	VPP_OUTFORMAT_RGB888,
	VPP_OUTFORMAT_Y0UY1V,
	VPP_OUTFORMAT_Y1UY0V,
	VPP_OUTFORMAT_Y0VY1U,
	VPP_OUTFORMAT_Y1VY0U,
	VPP_OUTFORMAT_UY0VY1,
	VPP_OUTFORMAT_UY1VY0,
	VPP_OUTFORMAT_VY0UY1,
	VPP_OUTFORMAT_VY1UY0
};


/*
 * Register operation
 */
static inline unsigned int vpp_read_reg(unsigned int offset)
{
	return readl(vpp_config.vpp_regs + offset);
}

static inline void vpp_write_reg(unsigned int offset, unsigned int val)
{
	writel(val, vpp_config.vpp_regs + offset);
}


#endif

