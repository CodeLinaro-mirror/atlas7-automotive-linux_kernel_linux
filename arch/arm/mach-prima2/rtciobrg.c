/*
 * RTC I/O Bridge interfaces for CSR SiRFprimaII
 * ARM access the registers of SYSRTC, GPSRTC and PWRC through this module
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#define SIRFSOC_CPUIOBRG_CTRL           0x00
#define SIRFSOC_CPUIOBRG_WRBE           0x04
#define SIRFSOC_CPUIOBRG_ADDR           0x08
#define SIRFSOC_CPUIOBRG_DATA           0x0c

void __iomem *sirfsoc_rtciobrg_base;
static DEFINE_SPINLOCK(rtciobrg_lock);

void sirfsoc_rtc_iobrg_besyncing(void)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&rtciobrg_lock, flags);

	while (readl(sirfsoc_rtciobrg_base))
		cpu_relax();

	spin_unlock_irqrestore(&rtciobrg_lock, flags);
}
EXPORT_SYMBOL(sirfsoc_rtc_iobrg_besyncing);

u32 sirfsoc_rtc_iobrg_readl(u32 addr)
{
	unsigned long flags = 0, val = 0;

	spin_lock_irqsave(&rtciobrg_lock, flags);

	while (readl(sirfsoc_rtciobrg_base + SIRFSOC_CPUIOBRG_CTRL))
		cpu_relax();

	writel(0x00, sirfsoc_rtciobrg_base + SIRFSOC_CPUIOBRG_WRBE);
	writel(addr, sirfsoc_rtciobrg_base + SIRFSOC_CPUIOBRG_ADDR);
	writel(0x01, sirfsoc_rtciobrg_base + SIRFSOC_CPUIOBRG_CTRL);

	while (readl(sirfsoc_rtciobrg_base + SIRFSOC_CPUIOBRG_CTRL))
		cpu_relax();

	val = readl(sirfsoc_rtciobrg_base + SIRFSOC_CPUIOBRG_DATA);

	spin_unlock_irqrestore(&rtciobrg_lock, flags);

	return val;
}
EXPORT_SYMBOL(sirfsoc_rtc_iobrg_readl);

void sirfsoc_rtc_iobrg_writel(u32 val, u32 addr)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&rtciobrg_lock, flags);

	while (readl(sirfsoc_rtciobrg_base + SIRFSOC_CPUIOBRG_CTRL))
		cpu_relax();

	writel(0xf1, sirfsoc_rtciobrg_base + SIRFSOC_CPUIOBRG_WRBE);
	writel(addr, sirfsoc_rtciobrg_base + SIRFSOC_CPUIOBRG_ADDR);

	writel(val, sirfsoc_rtciobrg_base + SIRFSOC_CPUIOBRG_DATA);
	writel(0x01, sirfsoc_rtciobrg_base + SIRFSOC_CPUIOBRG_CTRL);

	while (readl(sirfsoc_rtciobrg_base + SIRFSOC_CPUIOBRG_CTRL))
		cpu_relax();

	spin_unlock_irqrestore(&rtciobrg_lock, flags);
}
EXPORT_SYMBOL(sirfsoc_rtc_iobrg_writel);

static struct of_device_id rtciobrg_ids[] = {
	{ .compatible = "sirf,prima2-rtciobg" },
};

static void __init sirfsoc_of_rtciobrg_map(void)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, rtciobrg_ids);
	if (!np)
		panic("unable to find compatible rtc iobrg node in dtb\n");
	sirfsoc_rtciobrg_base = of_iomap(np, 0);
	if (!sirfsoc_rtciobrg_base)
		panic("unable to map rtc iobrg registers\n");

	of_node_put(np);
}
early_initcall(sirfsoc_of_rtciobrg_map);
