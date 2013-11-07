#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/time.h>
#include <linux/of_platform.h>

#include <asm/time.h>
#include <asm/irq.h>
#include <asm/irq_cpu.h>
#include <asm/bootinfo.h>
#include <asm/ptrace.h>
#include <asm/branch.h>
#include <asm/traps.h>
#include <asm/setup.h>
#include <asm/mipsregs.h>

#include "sharedparam.h"
#include "Cop.h"

struct SCoachSharedParams *g_sharedParam = NULL;

static void sharedparam_retrieve(void *sharedParamAddr)
{
	g_sharedParam = (struct SCoachSharedParams *)readl(sharedParamAddr);
	g_sharedParam = (struct SCoachSharedParams *)
		CKSEG1ADDR((u32)g_sharedParam);

	BUG_ON(g_sharedParam == NULL);
}

static void __init coach_ebase_setup(void)
{
	struct device_node *np;
	u32 addr;

	np = of_find_node_by_path("/cpus/cpu@0");
	of_property_read_u32(np, "linux-entry", &addr);
	set_c0_status(ST0_BEV);
	ebase = addr;
	write_c0_ebase(ebase);
	clear_c0_status(ST0_BEV);
}

void __init prom_init(void)
{
	/* Nullify Epc and ErrEpc */
	__asm__ __volatile__ (
		"mtc0 $0, $14\n"
		"mtc0 $0, $30\n"
	);
#define CPU_SHARE_PARAM 0xB080200c

	sharedparam_retrieve((void *)CPU_SHARE_PARAM);

	strlcpy(arcs_cmdline, (const char *)sharedparam_get_cmdline(),
		COMMAND_LINE_SIZE);

	board_ebase_setup = coach_ebase_setup;
	/*
	 * If DRAM size is bigger than 256MB -
	 * we need special handling here for address space conversion
	 */
	if (sharedparam_get_system_mem_size() > 0x10000000) {
		u32 upper_size =
			sharedparam_get_system_mem_size() - 0x10000000;
		void *upper_cached = (void *)ioremap
			(COACH_PHYSMEM_UPPER_ALIAS_START, upper_size);
		void *upper_uncached = (void *)ioremap_nocache
			(COACH_PHYSMEM_UPPER_ALIAS_START, upper_size);

		BUG_ON(upper_cached == 0 || upper_uncached == 0);
	}
}

const char*
get_system_type(void)
{
	return "MIPS Coach";
}

void prom_free_prom_memory(void)
{
}
