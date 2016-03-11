/*
 * COACH C14/C15 Interrupt controller routines.
 *
 * Copyright (c) 2013, 2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
#include <asm/gic.h>

#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include "Cop.h"

#define FLUSH_REG()

void __iomem *intc_membase;
static unsigned int coach_ier[32];

#define INTC_IER_OFFSET 0x8
#define INTC_RESET_OFFSET 0x4
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

static void __init coach_disable_irqs(int group)
{
	int i;
	struct irq_reg *irq_reg;

	for (i = 0; i < group; i++) {
		irq_reg = coach_get_irq_base(i);
		irq_reg->ier = 0;
		irq_reg->reset = 0xffffffff;
	}
}

void coach_irq_handle_group(unsigned group, unsigned group_status)
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
coach_irq_unmask(struct irq_data *d)
{
	unsigned int irq = d->irq;
	unsigned int group = coach_get_irq_group(irq);
	unsigned int irq_bit = coach_get_irq_bit(irq);
	unsigned ier;
	struct irq_reg *irq_reg;

	ier = coach_ier[group] | irq_bit;
	irq_reg = coach_get_irq_base(group);
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
	struct irq_reg *irq_reg;

	ier = coach_ier[group]  & ~irq_bit;
	irq_reg = coach_get_irq_base(group);
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

static irqreturn_t
cascade_action(int cpl, void *dev_id)
{
	(void)cpl;
	(void)dev_id;

	return IRQ_HANDLED;
}



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
	int group, ret = 0;
	struct resource res;
	struct irq_domain *domain;
	u32 l2_irq[2], status_im;

	ret = of_address_to_resource(node, 0, &res);
	if (ret < 0) {
		pr_err("%s: reg property not found!\n", node->name);
		return -EINVAL;
	}

	domain = irq_domain_add_legacy(node, COACH_INT_ID_LAST,
		COACH_IRQ_BASE, 0, &coach_irq_domain_ops, NULL);
	if (domain == NULL) {
		pr_err("%s: Creating legacy domain failed!\n", node->name);
		return -EINVAL;
	}

	intc_membase = ioremap_nocache(res.start,
					resource_size(&res));

	/* init coach irq */
	if (of_device_is_compatible(node, "csr,coach14-intc")) {
		status_im = IE_IRQ0 | IE_IRQ1 | IE_IRQ2 |
			IE_IRQ3 | IE_IRQ4 | IE_IRQ5;
		group = 16;
	} else {
		status_im = IE_IRQ0 | IE_IRQ2 | IE_IRQ5;
		group = 28;
	}
	coach_disable_irqs(group);

	clear_c0_status(STATUSF_IP0 | STATUSF_IP1
		| STATUSF_IP2 | STATUSF_IP3 | STATUSF_IP4 | STATUSF_IP5);

	clear_c0_cause(CAUSEF_IP0 | CAUSEF_IP1 | CAUSEF_IP2 | CAUSEF_IP3
		| CAUSEF_IP4 | CAUSEF_IP5 | CAUSEF_IP6 | CAUSEF_IP7);

	mips_cpu_irq_init();

	for (i = COACH_IRQ_BASE; i < COACH_IRQ_NUM(COACH_INT_ID_LAST); i++)
		irq_set_chip_and_handler_name(i, &coach_irq_chip,
			handle_level_irq, "coach general int");

	if (!of_property_read_u32_array(node, "l2_irq" , l2_irq, 2))
		for (i = l2_irq[0]; i < l2_irq[1]; i++)
			setup_irq(COACH_IRQ_NUM(i), level2);

	change_c0_status(ST0_IM, status_im);

	return ret;
}

#ifdef CONFIG_IRQ_GIC
static int
__init coach_of_gic_init(struct device_node *node,
			       struct device_node *parent)
{
	struct resource res;
	int ret = 0;

	ret = of_address_to_resource(node, 0, &res);
	if (ret < 0) {
		pr_err("%s: reg property not found!\n", node->name);
		return -EINVAL;
	}

	_gic_base = (unsigned long)ioremap_nocache(res.start,
		resource_size(&res));
	return 0;
}

void gic_platform_init(int irqs, struct irq_chip *irq_controller)
{
}

void gic_irq_ack(struct irq_data *d)
{
}

void gic_finish_irq(struct irq_data *d)
{
}
#endif

static struct of_device_id  of_irq_ids[]  __initdata = {
	{ .compatible = "csr,coach14-intc", .data = coach_of_irq_chip_init },
	{ .compatible = "csr,coach16-intc", .data = coach_of_irq_chip_init },
#ifdef CONFIG_IRQ_GIC
	{ .compatible = "mips,global-intc", .data = coach_of_gic_init },
#endif
	{},
};



void __init arch_init_irq(void)
{
	of_irq_init(of_irq_ids);
}
