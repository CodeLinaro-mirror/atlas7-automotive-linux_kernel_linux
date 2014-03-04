/*
 * CSR SiRFprima2 VIP library internal definitions
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __SIRFSOC_VIP_DEFS_H
#define __SIRFSOC_VIP_DEFS_H

#include "vcss_vip.h"
#include "vip_regs.h"

struct vip_config {
	bool initialized;
	u32 ref_count;
	struct vcss_vip_params setting;
	void __iomem *base;
	void __iomem *dma_base;
	struct vcss_vip_params saved_setting;
};

extern struct vip_config vip_config;

/*
** Register operation
*/
static inline u32 vip_read_reg(u32 offset)
{
	return readl(vip_config.base + offset);
}

static inline void vip_write_reg(u32 offset, u32 val)
{
	writel(val, vip_config.base + offset);
}

static inline u32 vip_read_dma_reg(u32 offset)
{
	return readl(vip_config.dma_base + offset);
}

static inline void vip_write_dma_reg(u32 offset, u32 val)
{
	writel(val, vip_config.dma_base + offset);
}

#define VIP_DUMP(fmt, ...) \
	pr_debug(fmt, ## __VA_ARGS__)

#define VIP_ENTRY(fmt, ...)

#define VIP_INFO(fmt, ...) \
	pr_info(fmt, ## __VA_ARGS__)

#define VIP_ERR(fmt, ...) \
	pr_err(fmt, ## __VA_ARGS__)
#endif

