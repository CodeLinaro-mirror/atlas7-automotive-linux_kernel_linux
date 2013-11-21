#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/time.h>

#include <asm/time.h>

#include "sharedparam.h"


unsigned long __init coach_get_cpu_hz(void)
{
	unsigned long cpu_config;

#define CPU_ADDR_CFG 0xb0802020
#define CPU_CFG_BOOT_FREQ_MAP 0x3

	cpu_config = readl((u32 *)CPU_ADDR_CFG);

	switch (cpu_config & CPU_CFG_BOOT_FREQ_MAP) {
	case 0: return 162*1000*1000;

	case 1: return 216*1000*1000;
	case 3: return 270*1000*1000;

	case 2: return 297*1000*1000;
	default:
		panic("oops - wrong freq cfg map\n");
	}
}


void
read_persistent_clock(struct timespec *ts)
{
	ts->tv_nsec = 0;
	ts->tv_sec = sharedparam_get_sys_time();
	pr_info("COACH: RTC time is  0x%08x\n", (unsigned int)ts->tv_sec);
}

unsigned int __cpuinit get_c0_compare_int(void)
{
	return 7;
}

void __init plat_time_init(void)
{
	mips_hpt_frequency = coach_get_cpu_hz()/2;
	write_c0_compare(read_c0_count() + mips_hpt_frequency/HZ);
}
