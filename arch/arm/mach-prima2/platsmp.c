/*
 * plat smp support for CSR Marco dual-core SMP SoCs
 *
 * Copyright (c) 2012 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/delay.h>
#include <asm/page.h>
#include <asm/mach/map.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>
#include <asm/cacheflush.h>
#include <asm/cputype.h>
#include <asm/hardware/gic.h>
#include <mach/map.h>

#include "common.h"

static void __iomem *scu_base;
/*
 * control for which core is the next to come out of the secondary
 * boot "holding pen".
 */
volatile int pen_release = -1;

static DEFINE_SPINLOCK(boot_lock);

static struct map_desc scu_io_desc __initdata = {
	.length		= SZ_4K,
	.type		= MT_DEVICE,
};

void __init sirfsoc_scu_map_io(void)
{
	unsigned long base;

	/* Get SCU base */
	asm("mrc p15, 4, %0, c15, c0, 0" : "=r" (base));

	scu_io_desc.virtual = SIRFSOC_VA(base);
	scu_io_desc.pfn = __phys_to_pfn(base);
	iotable_init(&scu_io_desc, 1);

	scu_base = (void __iomem *)SIRFSOC_VA(base);
}

void __cpuinit platform_secondary_init(unsigned int cpu)
{
	/*
	 * if any interrupts are already enabled for the primary
	 * core (e.g. timer irq), then they will not have been enabled
	 * for us: do so
	 */
	gic_secondary_init(0);

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

int __cpuinit boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned long timeout;
	static void __iomem *sram_base;

	/* for the moment in FPGA we use DSP SRAM */
#define SIRFSOC_DSP_SRAM_BASE 0xCA008000UL
	sram_base = ioremap_nocache(SIRFSOC_DSP_SRAM_BASE, SZ_4K);

	/*
	 * write the address of secondary startup into the sram register
	 * at offset 0x34, then write the magic number 0x12345678 to the
	 * sram register at offset 0x30, which is what boot rom code is
	 * waiting for. This would wake up the secondary core from WFI
	 */
#define SIRFSOC_CPU1_JUMPADDR_OFFSET 0x34
	__raw_writel(virt_to_phys(sirfsoc_secondary_startup),
		sram_base + SIRFSOC_CPU1_JUMPADDR_OFFSET);

#define SIRFSOC_CPU1_WAKEMAGIC_OFFSET 0x30
	__raw_writel(0x12345678,
		sram_base + SIRFSOC_CPU1_WAKEMAGIC_OFFSET);

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
	__cpuc_flush_dcache_area((void *)&pen_release, sizeof(pen_release));
	outer_clean_range(__pa(&pen_release), __pa(&pen_release + 1));

	/*
	 * Send the secondary CPU a soft interrupt ID15, thereby causing
	 * the boot monitor to read the JUMPADDR and WAKEMAGIC, and branch
	 * to the address found there.
	 */
	gic_raise_softirq(cpumask_of(cpu), 15);

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

void __init smp_init_cpus(void)
{
	int i, ncores;

	ncores = scu_get_core_count(scu_base);

	for (i = 0; i < ncores; i++)
		set_cpu_possible(i, true);

	set_smp_cross_call(gic_raise_softirq);
}

void __init platform_smp_prepare_cpus(unsigned int max_cpus)
{
	scu_enable(scu_base);
}
