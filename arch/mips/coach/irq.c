/*
 *  COACH C14/C15 Interrupt controller routines.
 *
 *  Author: Artem Leonenko <artem.leonenko@csr.com>
 *          Minda Chen <minda.chen@csr.com>
 *  Copyright (C) 2011 2013 CSR plc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 */


#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <asm/irq.h>
#include <asm/irq_cpu.h>
#include <asm/time.h>

#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include "Cop.h"

#define FLUSH_REG()

static void __iomem *intc_membase;

struct cpu_irq {
	u32 set;
	u32 reset;
	u32 ier;
	u32 isr;
	u32 irq;
};


#define COACH_IRQ(idx)  \
	(struct cpu_irq *)(intc_membase + (idx) * 0x20)

/*
 * IRQ# -> IRQ group Mapping:
 * ISR1 -> 0
 * ISR2 -> 1
 * .  .  .
 * ISR7 -> 6
 * MMU0 -> 7
 * MMU1 -> 8
 * MMU2 -> 9
 * .  .  .
 * MMU7 -> 14
 * NISR -> 15
 *
 */


/*
 * interrupt source to IRQ num mapping:
 *
 * IRQ num <- group# * 32 + irq_bit_num  + COACH_IRQ_BASE
 *
 */

void __init coach_disable_irqs(void)
{
	int i;
	struct cpu_irq *irq_reg;

	for (i = 0; i < 16; i++) {
		irq_reg = COACH_IRQ(i);
		irq_reg->ier = 0;
		irq_reg->reset = 0xffffffff;

	}
}



static inline int clz(unsigned long x)
{
	__asm__(
	"	.set	push\n"
	"	.set	mips32\n"
	"	clz	%0, %1\n"
	"	.set	pop\n"
	: "=r" (x)
	: "r" (x));

	return x;
}

/*
 * Version of ffs that only looks at bits 12..15.
 */
static inline unsigned int irq_ffs(unsigned int pending)
{
	return 32 - clz(pending) - CAUSEB_IP - 1;
}

static inline void coach_irq_handle_group(unsigned group, unsigned group_status)
{
	do {
		unsigned irq_num;
		int irq_idx;

		irq_idx = __ffs(group_status);

		irq_num = COACH_IRQ_BASE + 32 * group + irq_idx;

		do_IRQ(irq_num);
		group_status &= ~(1u << irq_idx);
	} while (group_status);
}


static void
coach_irq_handle_second_level(unsigned group_idx)
{
	unsigned group_status;
	struct cpu_irq *irq_reg;

	irq_reg = COACH_IRQ(group_idx);

	group_status = irq_reg->irq;

	coach_irq_handle_group(group_idx, group_status);
}


static void
coach_irq_dispatch(int group)
{
	unsigned int group_status;
	struct cpu_irq *irq_reg;
	const unsigned second_level_mask = (1u<<31u) | (1u<<30u) | (1u<<29u)
		| (1u<<28u) | (1u<<27u) | (1u<<26u)
		| (1u<<25u) | (1u<<24u) | (1u<<23u) | (1u<<22u);

	irq_reg = COACH_IRQ(group);
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
	unsigned int pending = read_c0_cause() & read_c0_status() & ST0_IM;

	if (pending & STATUSF_IP7) {
		do_IRQ(7);
		return;
	}

	if (pending) {
		int irq;

		irq = irq_ffs(pending);

		if (irq >= 2) {
			coach_irq_dispatch(irq - 2);
		} else {
			printk("do_IRQ(%i)2\n", irq);
			do_IRQ(irq);
		}
	} else
		spurious_interrupt();
}


static inline unsigned int coach_get_irq_group(unsigned int irq)
{
	return (irq - COACH_IRQ_BASE) / 32;
}


static inline unsigned int coach_get_irq_bit(unsigned int irq)
{
	return 1u << (irq % 32);
}


static unsigned int coach_ier[32];


static void
coach_irq_unmask(struct irq_data *d)
{
	unsigned int irq = d->irq;
	unsigned int group = coach_get_irq_group(irq);
	unsigned int irq_bit = coach_get_irq_bit(irq);
	unsigned ier;
	struct cpu_irq *irq_reg;

	ier = coach_ier[group] | irq_bit;

	irq_reg = COACH_IRQ(group);
	irq_reg->ier = ier;
	coach_ier[group] = ier;
	FLUSH_REG();

	mmiowb();
}


static void
coach_irq_mask(struct irq_data *d)
{
	unsigned int irq = d->irq;
	unsigned int group = coach_get_irq_group(irq);
	unsigned int irq_bit = coach_get_irq_bit(irq);
	unsigned int ier;
	struct cpu_irq *irq_reg;

	ier = coach_ier[group]  & ~irq_bit;

	irq_reg = COACH_IRQ(group);
	irq_reg->ier = ier;
	coach_ier[group] = ier;
	FLUSH_REG();

	irq_reg->reset = irq_bit;
	mmiowb();
}


static struct irq_chip coach_irq_chip = {
	.name	= "coach_irq",
	.irq_ack	= coach_irq_mask,
	.irq_mask	= coach_irq_mask,
	.irq_mask_ack = coach_irq_mask,
	.irq_unmask	= coach_irq_unmask,
	.irq_eoi	= coach_irq_unmask,
};

/*-----------------------------------------*/
static int coach_irq_chip_irq_state[COACH_VIRT_IRQ_NUM];

static void
coach_irq_mask_virt(struct irq_data *d)
{
	unsigned int irq_num = d->irq;
	coach_irq_chip_irq_state[irq_num] = 1;
}

static void
coach_irq_unmask_virt(struct irq_data *d)
{
	unsigned int irq_num = d->irq;
	coach_irq_chip_irq_state[irq_num] = 0;
}

static struct irq_chip coach_irq_chip_virt = {
	.name   = "coach_irq_virt",
	.irq_ack    = coach_irq_mask_virt,
	.irq_mask   = coach_irq_mask_virt,
	.irq_mask_ack = coach_irq_mask_virt,
	.irq_unmask = coach_irq_unmask_virt,
	.irq_eoi    = coach_irq_unmask_virt,
};

void
coach_irq_trigger_virt(unsigned irq_num)
{
	if (!irqs_disabled()) {
		unsigned long irqflags;

		local_irq_save(irqflags);

		if (!coach_irq_chip_irq_state[irq_num])
			do_IRQ(irq_num);

		local_irq_restore(irqflags);
	}

}
/*-----------------------------------------*/


static irqreturn_t
cascade_action(int cpl, void *dev_id)
{
	(void)cpl;
	(void)dev_id;

	return IRQ_HANDLED;
}


static struct irqaction cascade_irqaction = {
	.handler = cascade_action,
	.name = "cascade",
};


static struct irqaction cascade_irqaction2 = {
	.handler = cascade_action,
	.name = "cascade lvl2",
};


static const struct irq_domain_ops coach_irq_domain_ops = {
	.xlate = irq_domain_xlate_onecell,
};

static int
__init coach_of_irq_chip_init(struct device_node *node,
			       struct device_node *parent)
{
	u32 i;
	struct irqaction *level2 = &cascade_irqaction2;
	int irq_num, ret = 0;
	struct resource res;
	struct irq_domain *domain;
	u32 l2_irq[2];

	ret = of_address_to_resource(node, 0, &res);
	if (ret < 0) {
		pr_err("%s: reg property not found!\n", node->name);
		return -EINVAL;
	}

	domain = irq_domain_add_linear(node, COACH_INT_ID_LAST,
		&coach_irq_domain_ops, NULL);
	if (domain == NULL) {
		pr_err("%s: Creating legacy domain failed!\n", node->name);
		return -EINVAL;
	}

	intc_membase = ioremap_nocache(res.start,
					resource_size(&res));

	/* init coach irq */

	coach_disable_irqs();

	clear_c0_status(STATUSF_IP0 | STATUSF_IP1
		| STATUSF_IP2 | STATUSF_IP3 | STATUSF_IP4 | STATUSF_IP5);

	clear_c0_cause(CAUSEF_IP0 | CAUSEF_IP1 | CAUSEF_IP2 | CAUSEF_IP3
		| CAUSEF_IP4 | CAUSEF_IP5 | CAUSEF_IP6 | CAUSEF_IP7);

	mips_cpu_irq_init();

	for (i = COACH_IRQ_BASE; i < COACH_IRQ_NUM(COACH_INT_ID_LAST); i++)
		irq_set_chip_and_handler_name(i, &coach_irq_chip,
			handle_level_irq, "coach general int");

	for (i = 2; i < 7; i++)
		setup_irq(MIPS_CPU_IRQ_BASE + i, &cascade_irqaction);

	if (!of_property_read_u32_array(node, "l2_irq" , l2_irq, 2))
		for (i = l2_irq[0]; i < l2_irq[1]; i++)
			setup_irq(COACH_IRQ_NUM(i), level2);

	irq_num = COACH_VIRT_IRQ_OFFSET + COACH_VIRT_IRQ_NUM;
	for (i = COACH_VIRT_IRQ_OFFSET; i < irq_num; i++)
		irq_set_chip_and_handler_name(i, &coach_irq_chip_virt,
			handle_level_irq, "coach virt int");

#define ALLINTS (IE_IRQ0 | IE_IRQ1 | IE_IRQ2 | IE_IRQ3 | IE_IRQ4 | IE_IRQ5)
	change_c0_status(ST0_IM, ALLINTS);

	return ret;
}

static struct of_device_id  of_irq_ids[]  __initdata = {
	{ .compatible = "csr,coach14-intc", .data = coach_of_irq_chip_init },
	{},
};



void __init arch_init_irq(void)
{
	of_irq_init(of_irq_ids);
}
