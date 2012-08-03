/*
 * CPU hotplug support for CSR Marco dual-core SMP SoCs
 *
 * Copyright (c) 2012 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/smp.h>

#include <asm/cacheflush.h>
#include <asm/smp_plat.h>

extern volatile int pen_release;

static inline void platform_do_lowpower(unsigned int cpu)
{
	flush_cache_all();

	/* we put the platform to just WFI */
	for (;;) {
		__asm__ __volatile__("dsb\n\t" "wfi\n\t"
			: : : "memory");
		if (pen_release == cpu_logical_map(cpu)) {
			/*
			 * OK, proper wakeup, we're done
			 */
			break;
		}
	}
}

int platform_cpu_kill(unsigned int cpu)
{
	return 1;
}

/*
 * platform-specific code to shutdown a CPU
 *
 * Called with IRQs disabled
 */
void platform_cpu_die(unsigned int cpu)
{
	platform_do_lowpower(cpu);
}

int platform_cpu_disable(unsigned int cpu)
{
	/*
	 * we don't allow CPU 0 to be shutdown (it is still too special
	 * e.g. clock tick interrupts)
	 */
	return cpu == 0 ? -EPERM : 0;
}
