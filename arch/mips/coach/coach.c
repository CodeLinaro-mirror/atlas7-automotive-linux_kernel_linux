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

static void sharedparam_retrieve(u32 sharedParamAddr)
{
	g_sharedParam = (struct SCoachSharedParams *)
		CKSEG1ADDR((u32)sharedParamAddr);

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

	sharedparam_retrieve(fw_arg1);

	board_ebase_setup = coach_ebase_setup;
}

const char*
get_system_type(void)
{
	return "MIPS Coach";
}

void prom_free_prom_memory(void)
{
}

