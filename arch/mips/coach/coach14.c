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

#define INTC_STATUS_OFFSET 0x10

struct irq_reg *coach_get_irq_base(int idx)
{
	return (struct irq_reg *)(intc_membase + idx * 0x20);
}

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

static void
coach_irq_handle_second_level(unsigned group_idx)
{
	unsigned group_status;
	struct irq_reg *irq_reg;

	irq_reg = coach_get_irq_base(group_idx);
	group_status = irq_reg->irq;

	coach_irq_handle_group(group_idx, group_status);
}


static void
coach_irq_dispatch(int group)
{
	unsigned int group_status;
	struct irq_reg *irq_reg;
	const unsigned second_level_mask = (1u<<31u) | (1u<<30u) | (1u<<29u)
		| (1u<<28u) | (1u<<27u) | (1u<<26u)
		| (1u<<25u) | (1u<<24u) | (1u<<23u) | (1u<<22u);

	irq_reg = coach_get_irq_base(group);
	group_status = irq_reg->irq;

	if (unlikely(group_status == 0)) {
		spurious_interrupt();
		return;
	}

	if (group == 3) {
		unsigned group_lvl2 = group_status & second_level_mask;
		int group_lvl2_sub_idx;

		while (group_lvl2) {

			group_lvl2_sub_idx = __ffs(group_lvl2);
			coach_irq_handle_second_level(group_lvl2_sub_idx +
				5 - __ffs(second_level_mask));
			group_lvl2 &= ~(1u<<group_lvl2_sub_idx);
		}
	}

	coach_irq_handle_group(group, group_status);

	return;
}

asmlinkage void
plat_irq_dispatch(void)
{
	int irq;
	unsigned int pending = read_c0_cause() & read_c0_status() & ST0_IM;

	if (pending & STATUSF_IP7) {
		do_IRQ(7);
		return;
	}

	if (pending) {
		irq = irq_ffs(pending);

		if (irq >= 2)
			coach_irq_dispatch(irq - 2);
		else {
			pr_info("do_IRQ(%i)2\n", irq);
			do_IRQ(irq);
		}
	} else
		spurious_interrupt();
}


