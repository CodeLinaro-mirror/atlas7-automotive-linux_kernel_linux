/*
 * Atlas7 CSRVISOR Tester
 */

#define pr_fmt(fmt) "csrvisor_tester: " fmt

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <asm/cacheflush.h>

static int csrvisor_test_thread(void *data)
{
	static unsigned long cnt;
	int ret;

	ret = sched_setaffinity(current->pid, cpumask_of(0));
	if (!ret)
		pr_err("Couldn't set init affinity to cpu0\n");

	while (!kthread_should_stop()) {
		register unsigned long r0 asm("r0") = 0x80000000;
		register unsigned long r1 asm("r1") = __pa(&cnt);
		__asm__ __volatile__(".arch_extension sec\n\t"
			"smc #0" :                      /* no output */
			: "r"(r0), "r"(r1)
			: "memory");
		msleep(2000);

		cnt++;
		/*
		 * Secure API needs physical address
		 * pointer for the parameters
		 */
		flush_cache_all();
		outer_clean_range(__pa(&cnt), __pa(&cnt + 1));
	}

	return 0;
}

static __init int csrvisor_test_init(void)
{
	if (of_machine_is_compatible("sirf,atlas7")) {
		kthread_run(csrvisor_test_thread,
			NULL,
			"csrvisor_test");
	}
	return 0;
}
arch_initcall(csrvisor_test_init);
