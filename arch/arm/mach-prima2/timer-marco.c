/*
 * System timer for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/bitops.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <mach/map.h>
#include <asm/sched_clock.h>
#include <asm/mach/time.h>

#include "common.h"

#define SIRFSOC_TIMER_32COUNTER_0_CTRL			0x0000
#define SIRFSOC_TIMER_32COUNTER_1_CTRL			0x0004
#define SIRFSOC_TIMER_32COUNTER_2_CTRL			0x0008
#define SIRFSOC_TIMER_32COUNTER_3_CTRL			0x000c
#define SIRFSOC_TIMER_32COUNTER_4_CTRL			0x0010
#define SIRFSOC_TIMER_32COUNTER_5_CTRL			0x0014
#define SIRFSOC_TIMER_MATCH_0				0x0018
#define SIRFSOC_TIMER_MATCH_1				0x001c
#define SIRFSOC_TIMER_MATCH_2				0x0020
#define SIRFSOC_TIMER_MATCH_3				0x0024
#define SIRFSOC_TIMER_MATCH_4				0x0028
#define SIRFSOC_TIMER_MATCH_5				0x002c
#define SIRFSOC_TIMER_32COUNTER_0_MATCH_NUM		0x0030
#define SIRFSOC_TIMER_32COUNTER_1_MATCH_NUM		0x0034
#define SIRFSOC_TIMER_32COUNTER_2_MATCH_NUM		0x0038
#define SIRFSOC_TIMER_32COUNTER_3_MATCH_NUM		0x003c
#define SIRFSOC_TIMER_32COUNTER_4_MATCH_NUM		0x0040
#define SIRFSOC_TIMER_32COUNTER_5_MATCH_NUM		0x0044
#define SIRFSOC_TIMER_COUNTER_0				0x0048
#define SIRFSOC_TIMER_COUNTER_1				0x004c
#define SIRFSOC_TIMER_COUNTER_2				0x0050
#define SIRFSOC_TIMER_COUNTER_3				0x0054
#define SIRFSOC_TIMER_COUNTER_4				0x0058
#define SIRFSOC_TIMER_COUNTER_5				0x005c
#define SIRFSOC_TIMER_INTR_STATUS			0x0060
#define SIRFSOC_TIMER_WATCHDOG_EN			0x0064
#define SIRFSOC_TIMER_64COUNTER_CTRL			0x0068
#define SIRFSOC_TIMER_64COUNTER_LO			0x006c
#define SIRFSOC_TIMER_64COUNTER_HI			0x0070
#define SIRFSOC_TIMER_64COUNTER_LOAD_LO			0x0074
#define SIRFSOC_TIMER_64COUNTER_LOAD_HI			0x0078
#define SIRFSOC_TIMER_64COUNTER_RLATCHED_LO		0x007c
#define SIRFSOC_TIMER_64COUNTER_RLATCHED_HI		0x0080

#define SIRFSOC_TIMER_REG_CNT 5

static const u32 sirfsoc_timer_reg_list[SIRFSOC_TIMER_REG_CNT] = {
	SIRFSOC_TIMER_WATCHDOG_EN,
	SIRFSOC_TIMER_32COUNTER_0_CTRL,
	SIRFSOC_TIMER_64COUNTER_CTRL,
	SIRFSOC_TIMER_64COUNTER_RLATCHED_LO,
	SIRFSOC_TIMER_64COUNTER_RLATCHED_HI,
};

static u32 sirfsoc_timer_reg_val[SIRFSOC_TIMER_REG_CNT];

static void __iomem *sirfsoc_timer_base;
static void __init sirfsoc_of_timer_map(void);

/* timer0 interrupt handler */
static irqreturn_t sirfsoc_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *ce = dev_id;
	u32 val;

	WARN_ON(!(readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_INTR_STATUS) & BIT(0)));

	/* Stop the timer tick */
	val = readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_32COUNTER_0_CTRL);
	val &= ~(BIT(0) | BIT(1) | BIT(2));
	writel_relaxed(val, sirfsoc_timer_base + SIRFSOC_TIMER_32COUNTER_0_CTRL);

	writel_relaxed(0, sirfsoc_timer_base + SIRFSOC_TIMER_COUNTER_0);

	/* clear timer0 interrupt */
	writel_relaxed(BIT(0), sirfsoc_timer_base + SIRFSOC_TIMER_INTR_STATUS);

	ce->event_handler(ce);

	return IRQ_HANDLED;
}

/* read 64-bit timer counter */
static cycle_t sirfsoc_timer_read(struct clocksource *cs)
{
	u64 cycles;

	writel_relaxed((readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_CTRL) |
			BIT(0)) & ~BIT(1), sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_CTRL);

	cycles = readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_RLATCHED_HI);
	cycles = (cycles << 32) | readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_RLATCHED_LO);

	return cycles;
}

static int sirfsoc_timer_set_next_event(unsigned long delta,
	struct clock_event_device *ce)
{
	u32 val = readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_32COUNTER_0_CTRL);
	val |= BIT(0) | BIT(1) | BIT(2);

	writel_relaxed(0, sirfsoc_timer_base + SIRFSOC_TIMER_COUNTER_0);
#if 0
	writel_relaxed(delta, sirfsoc_timer_base + SIRFSOC_TIMER_MATCH_0);
#else
	/* Fix FPGA: divider of 32counter_0 doesn't work */
	writel_relaxed(delta * 13, sirfsoc_timer_base + SIRFSOC_TIMER_MATCH_0);
#endif

	/* enable the tick */
	writel_relaxed(val, sirfsoc_timer_base + SIRFSOC_TIMER_32COUNTER_0_CTRL);
	return 0;
}

static void sirfsoc_timer_set_mode(enum clock_event_mode mode,
	struct clock_event_device *ce)
{
	u32 val = readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_32COUNTER_0_CTRL);
	val &= ~(BIT(0) | BIT(1) | BIT(2));

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		WARN_ON(1);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		/* enable in set_next_event */
		break;
	case CLOCK_EVT_MODE_SHUTDOWN:
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_RESUME:
		break;
	}

	writel_relaxed(val, sirfsoc_timer_base + SIRFSOC_TIMER_32COUNTER_0_CTRL);
}

static void sirfsoc_clocksource_suspend(struct clocksource *cs)
{
	int i;

	for (i = 0; i < SIRFSOC_TIMER_REG_CNT; i++)
		sirfsoc_timer_reg_val[i] = readl_relaxed(sirfsoc_timer_base + sirfsoc_timer_reg_list[i]);
}

static void sirfsoc_clocksource_resume(struct clocksource *cs)
{
	int i;

	for (i = 0; i < SIRFSOC_TIMER_REG_CNT - 2; i++)
		writel_relaxed(sirfsoc_timer_reg_val[i], sirfsoc_timer_base + sirfsoc_timer_reg_list[i]);

	writel_relaxed(sirfsoc_timer_reg_val[SIRFSOC_TIMER_REG_CNT - 2],
		sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_LOAD_LO);
	writel_relaxed(sirfsoc_timer_reg_val[SIRFSOC_TIMER_REG_CNT - 1],
		sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_LOAD_HI);

	writel_relaxed(readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_CTRL) |
		BIT(1) | BIT(0), sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_CTRL);
}

static struct clock_event_device sirfsoc_clockevent = {
	.name = "sirfsoc_clockevent",
	.rating = 200,
	.features = CLOCK_EVT_FEAT_ONESHOT,
	.set_mode = sirfsoc_timer_set_mode,
	.set_next_event = sirfsoc_timer_set_next_event,
};

static struct clocksource sirfsoc_clocksource = {
	.name = "sirfsoc_clocksource",
	.rating = 200,
	.mask = CLOCKSOURCE_MASK(64),
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
	.read = sirfsoc_timer_read,
	.suspend = sirfsoc_clocksource_suspend,
	.resume = sirfsoc_clocksource_resume,
};

static struct irqaction sirfsoc_timer_irq = {
	.name = "sirfsoc_timer0",
	.flags = IRQF_TIMER,
	.handler = sirfsoc_timer_interrupt,
	.dev_id = &sirfsoc_clockevent,
};

static void __init sirfsoc_clockevent_init(void)
{
	clockevents_calc_mult_shift(&sirfsoc_clockevent, CLOCK_TICK_RATE, 60);

	sirfsoc_clockevent.max_delta_ns =
		clockevent_delta2ns(-2, &sirfsoc_clockevent);
	sirfsoc_clockevent.min_delta_ns =
		clockevent_delta2ns(2, &sirfsoc_clockevent);

	sirfsoc_clockevent.cpumask = cpumask_of(0);
	clockevents_register_device(&sirfsoc_clockevent);
}

/* initialize the kernel jiffy timer source */
static void __init sirfsoc_timer_init(void)
{
	unsigned long rate;
	u32 timer_div;
	struct clk *clk;

	/* initialize clocking early, we want to set the OS timer */
	sirfsoc_of_clk_init();

	/* timer's input clock is io clock */
	clk = clk_get_sys("io", NULL);

	BUG_ON(IS_ERR(clk));
#if defined(CONFIG_SIRFMARCO_FPGA)
	/* For FPGA, the io clk is fixed to 26Mhz */
	rate = 26000000;
#else
	rate = clk_get_rate(clk);
#endif

	BUG_ON(rate < CLOCK_TICK_RATE);
	BUG_ON(rate % CLOCK_TICK_RATE);

	sirfsoc_of_timer_map();

	/* Initialize the timer divider */
	timer_div = rate / CLOCK_TICK_RATE / 2 - 1;
	writel_relaxed((timer_div << 16) | readl_relaxed(sirfsoc_timer_base +
				SIRFSOC_TIMER_64COUNTER_CTRL),
			sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_CTRL);

	writel_relaxed((timer_div << 16) | readl_relaxed(sirfsoc_timer_base +
				SIRFSOC_TIMER_32COUNTER_0_CTRL) | BIT(0),
			sirfsoc_timer_base + SIRFSOC_TIMER_32COUNTER_0_CTRL);

	/* Initialize the timer counter to 0 */

	writel_relaxed(0, sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_LOAD_LO);
	writel_relaxed(0, sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_LOAD_HI);
	writel_relaxed(readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_CTRL) |
		BIT(1) | BIT(0), sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_CTRL);
	writel_relaxed(0, sirfsoc_timer_base + SIRFSOC_TIMER_COUNTER_0);

	writel_relaxed(BIT(0), sirfsoc_timer_base + SIRFSOC_TIMER_INTR_STATUS);

	BUG_ON(clocksource_register_hz(&sirfsoc_clocksource, CLOCK_TICK_RATE));

	BUG_ON(setup_irq(sirfsoc_timer_irq.irq, &sirfsoc_timer_irq));

	sirfsoc_clockevent_init();
}

static struct of_device_id timer_ids[] = {
	{ .compatible = "sirf,marco-tick" },
	{},
};

static void __init sirfsoc_of_timer_map(void)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, timer_ids);
	if (!np)
		return;
	sirfsoc_timer_base = of_iomap(np, 0);
	if (!sirfsoc_timer_base)
		panic("unable to map timer cpu registers\n");

	sirfsoc_timer_irq.irq = irq_of_parse_and_map(np, 0);
	if (!sirfsoc_timer_irq.irq)
		panic("No irq passed for timer via DT\n");

	of_node_put(np);
}

struct sys_timer sirfsoc_marco_timer = {
	.init = sirfsoc_timer_init,
};
