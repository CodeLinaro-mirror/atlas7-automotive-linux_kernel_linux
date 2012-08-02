/*
 * plat smp support for CSR Marco dual-core SMP SoCs
 *
 * Copyright (c) 2012 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <asm/page.h>
#include <asm/mach/map.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>
#include <asm/cacheflush.h>
#include <asm/cputype.h>
#include <asm/hardware/gic.h>
#include <mach/map.h>

static void __iomem *scu_base;

static struct map_desc scu_io_desc __initdata = {
	/* .virtual and .pfn are run-time assigned */
	.length		= SZ_4K,
	.type		= MT_DEVICE,
};

void __init marco_scu_map_io(void)
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
}

int __cpuinit boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	return 0;
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
