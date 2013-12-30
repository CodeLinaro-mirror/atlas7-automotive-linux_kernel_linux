#ifndef _ASM_ARM_PARAVIRT_H
#define _ASM_ARM_PARAVIRT_H
#ifdef CONFIG_PARAVIRT

#include <linux/types.h>
#include <linux/jump_label.h>
#include <linux/sizes.h>

struct static_key;
extern struct static_key paravirt_steal_enabled;
extern struct static_key paravirt_steal_rq_enabled;

#define NT_SMC_STEAL_GET 0x10
static u64 smc_get_steal_time(void)
{
#define NT_SMC_STEAL_GET 0x10
#define SMC_PARAM (0xD0000000 + SZ_128K)
	register unsigned long r0 asm("r0") = NT_SMC_STEAL_GET;
	register unsigned long r1 asm("r1") = SMC_PARAM;

	asm volatile(".arch_extension sec\n\t"
		"smc #0\n\t" :
		: "r"(r0), "r"(r1)
		: "memory");

	return *(u64 *)SMC_PARAM;
}

static inline u64 paravirt_steal_clock(int cpu)
{
	return !cpu && !smp_processor_id() ? smc_get_steal_time() : 0;
}
#endif /* __ASSEMBLY__ */

#endif /* _ASM_ARM_PARAVIRT_H */
