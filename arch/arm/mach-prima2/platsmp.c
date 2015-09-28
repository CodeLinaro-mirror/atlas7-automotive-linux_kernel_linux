/*
 * plat smp support for CSR Marco dual-core SMP SoCs
 *
 * Copyright (c) 2013-2016, The Linux Foundation. All rights reserved.
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
#include <linux/smp.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <asm/page.h>
#include <asm/mach/map.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>
#include <asm/cacheflush.h>
#include <asm/cputype.h>

#include "common.h"

static void __iomem *clk_base;

static DEFINE_SPINLOCK(boot_lock);

static void sirfsoc_secondary_init(unsigned int cpu)
{
	/*
	 * let the primary processor know we're out of the
	 * pen, then head off into the C entry point
	 */
	pen_release = -1;
	smp_wmb();

	/*
	 * Synchronise with the boot thread.
	 */
	spin_lock(&boot_lock);
	spin_unlock(&boot_lock);
}

static struct of_device_id clk_ids[]  = {
	{ .compatible = "sirf,atlas7-car" },
	{},
};

static int sirfsoc_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned long timeout;
	struct device_node *np;

	np = of_find_matching_node(NULL, clk_ids);
	if (!np)
		return -ENODEV;

	clk_base = of_iomap(np, 0);
	if (!clk_base)
		return -ENOMEM;

	/*
	 * write the address of secondary startup and magic number into
	 * clkc registers. This would wake up the secondary core from WFE
	 */
#define SIRFSOC_CPU1_JUMPADDR_OFFSET 0x468
	__raw_writel(virt_to_phys(sirfsoc_secondary_startup),
		clk_base + SIRFSOC_CPU1_JUMPADDR_OFFSET);

#define SIRFSOC_CPU1_WAKEMAGIC_OFFSET 0x464
	__raw_writel(0x3CAF5D62,
		clk_base + SIRFSOC_CPU1_WAKEMAGIC_OFFSET);

	/* make sure write buffer is drained */
	mb();

	spin_lock(&boot_lock);

	/*
	 * The secondary processor is waiting to be released from
	 * the holding pen - release it, then wait for it to flag
	 * that it has been released by resetting pen_release.
	 *
	 * Note that "pen_release" is the hardware CPU ID, whereas
	 * "cpu" is Linux's internal ID.
	 */
	pen_release = cpu_logical_map(cpu);
	sync_cache_w(&pen_release);

	/*
	 * Send the secondary CPU a soft interrupt, thereby causing read
	 * the JUMPADDR and WAKEMAGIC, and branch to the address found there.
	 */
	arch_send_wakeup_ipi_mask(cpumask_of(cpu));

	timeout = jiffies + (1 * HZ);
	while (time_before(jiffies, timeout)) {
		smp_rmb();
		if (pen_release == -1)
			break;

		udelay(10);
	}

	/*
	 * now the secondary core is starting up let it run its
	 * calibrations, then wait for it to finish
	 */
	spin_unlock(&boot_lock);

	return pen_release != -1 ? -ENOSYS : 0;
}

struct smp_operations sirfsoc_smp_ops __initdata = {
	.smp_secondary_init     = sirfsoc_secondary_init,
	.smp_boot_secondary     = sirfsoc_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die                = sirfsoc_cpu_die,
#endif
};
