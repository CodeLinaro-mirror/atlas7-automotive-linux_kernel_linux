/*
 * plat smp support for CSR Marco dual-core SMP SoCs
 *
 * Copyright (c) 2012 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/init.h>
#include <linux/smp.h>

void __cpuinit platform_secondary_init(unsigned int cpu)
{
}

int __cpuinit boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	return 0;
}

void __init smp_init_cpus(void)
{
}

void __init platform_smp_prepare_cpus(unsigned int max_cpus)
{
}
