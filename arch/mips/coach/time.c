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
#include "Cop.h"



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
