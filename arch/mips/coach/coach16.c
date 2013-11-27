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
#include <asm/gic.h>

#include "sharedparam.h"
#include "Cop.h"

#define INTC_STATUS_OFFSET 0x14

struct irq_reg *coach_get_irq_base(int idx)
{
	return (struct irq_reg *)(intc_membase + (idx) * 0x4000 + 0x18);
}

unsigned long __init coach_get_cpu_hz(void)
{
	unsigned long cpu_config;

#define CPU_ADDR_CFG 0xb0802020
#define CPU_CFG_BOOT_FREQ_MAP 0x18000

	cpu_config = readl((u32 *)CPU_ADDR_CFG);

	switch ((cpu_config & CPU_CFG_BOOT_FREQ_MAP) >> 15) {
	case 0: return 216*1000*1000;

	case 1: return 288*1000*1000;
	case 2: return 345*1000*1000;

	case 3: return 432*1000*1000;
	default:
		panic("oops - wrong freq cfg map\n");
	}
}

static void
coach_ack_ipi_on_cop(void)
{
	GICWRITE(GIC_REG(SHARED, GIC_SH_WEDGE), 60);
}

static void coach_irq_dispatch(void)
{
	int group;
	u32 group_status;
	struct irq_reg *irq_reg;

	for (group = 0; group < 28; group++) {
		irq_reg = coach_get_irq_base(group);
		group_status = irq_reg->c16_irq;
		if (group_status)
			coach_irq_handle_group(group, group_status);
	}
	return;
}


asmlinkage void plat_irq_dispatch(void)
{
	int irq;
	unsigned int pending = read_c0_cause() & read_c0_status() & ST0_IM;

	if (pending & STATUSF_IP7) {
		do_IRQ(7);
		return;
	}

	if (pending) {
		irq = irq_ffs(pending);
		if (irq == 2)
			coach_irq_dispatch();
		else {
			if (irq == 3) {
				coach_ack_ipi_on_cop();
				do_IRQ(irq);
			} else
				do_IRQ(irq);
		}
	} else
		spurious_interrupt();
}

static int  __init send_ack_from_cop(void)
{
	/*
	 * This function is to avoid threadx panic
	 * If Rpmsg is added, maybe you can delete it.
	 */
	pr_info("COACH: send ipi ack now");
	coach_ack_ipi_on_cop();
	return 0;
}
arch_initcall(send_ack_from_cop);

