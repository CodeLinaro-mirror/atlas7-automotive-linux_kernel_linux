/*
 * power management entry for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/suspend.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>
#include <asm/suspend.h>

#include "common.h"
#include "pm.h"

static u32 sirfsoc_pwrc_base;
void __iomem *sirfsoc_memc_base;

static void sirfsoc_set_wakeup_source(void)
{
        u32 pwr_trigger_en_reg;
        pwr_trigger_en_reg = sirfsoc_rtc_iobrg_readl(sirfsoc_pwrc_base +
		SIRFSOC_PWRC_TRIGGER_EN);
#define X_ON_KEY_B (1 << 0)
        sirfsoc_rtc_iobrg_writel(pwr_trigger_en_reg | X_ON_KEY_B,
                        sirfsoc_pwrc_base + SIRFSOC_PWRC_TRIGGER_EN);
}

static void sirfsoc_set_sleep_mode(u32 mode)
{
        u32 sleep_mode = sirfsoc_rtc_iobrg_readl(sirfsoc_pwrc_base +
		SIRFSOC_PWRC_PDN_CTRL);
        sleep_mode &= ~(SIRFSOC_SLEEP_MODE_MASK << 1);
        sleep_mode |= ((mode << 1));
        sirfsoc_rtc_iobrg_writel(sleep_mode, sirfsoc_pwrc_base +
		SIRFSOC_PWRC_PDN_CTRL);
}

int sirfsoc_pre_suspend_power_off(void)
{
	u32 wakeup_entry = virt_to_phys(cpu_resume);

	sirfsoc_rtc_iobrg_writel(wakeup_entry,
		SIRFSOC_PWRC_SCRATCH_PAD1);

	sirfsoc_set_wakeup_source();

	sirfsoc_set_sleep_mode(SIRFSOC_DEEP_SLEEP_MODE);

	return 0;
}

static void sirfsoc_save_register(u32 *ptr)
{
	/* todo: save necessary system registers here */
}

static void sirfsoc_restore_regs(u32 *ptr)
{
	/* todo: restore saved system registers here */
}

static int sirfsoc_pm_enter(suspend_state_t state)
{
	u32 *saved_regs;

	switch (state) {
	case PM_SUSPEND_MEM:
		saved_regs = kmalloc(1024, GFP_ATOMIC);
		if (!saved_regs)
			return -ENOMEM;

		sirfsoc_save_register(saved_regs);
		cpu_suspend(0, sirfsoc_finish_suspend);
		cpu_init();

#ifdef CONFIG_CACHE_L2X0
		sirfsoc_l2x_init();
#endif
		sirfsoc_restore_regs(saved_regs);
		kfree(saved_regs);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct platform_suspend_ops sirfsoc_pm_ops = {
	.enter = sirfsoc_pm_enter,
	.valid = suspend_valid_only_mem,
};

static struct of_device_id pwrc_ids[] = {
	{ .compatible = "sirf,prima2-pwrc" },
};

static void __init sirfsoc_of_pwrc_init(void)
{
	struct device_node *np;
	const __be32    *addrp;

	np = of_find_matching_node(NULL, pwrc_ids);
	if (!np)
		panic("unable to find compatible pwrc node in dtb\n");

	addrp = of_get_property(np, "reg", NULL);
	if (!addrp)
		panic("unable to find base address of pwrc node in dtb\n");

	sirfsoc_pwrc_base = be32_to_cpup(addrp);

	of_node_put(np);
}

static struct of_device_id memc_ids[] = {
	{ .compatible = "sirf,prima2-memc" },
};

static void __init sirfsoc_of_memc_map(void)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, memc_ids);
	if (!np)
		panic("unable to find compatible memc node in dtb\n");

	sirfsoc_memc_base = of_iomap(np, 0);
	if (!np)
		panic("unable to map compatible memc node in dtb\n");

	of_node_put(np);
}

static int __init sirfsoc_pm_init(void)
{
	sirfsoc_of_memc_map();
	sirfsoc_of_pwrc_init();
	suspend_set_ops(&sirfsoc_pm_ops);
	return 0;
}
late_initcall(sirfsoc_pm_init);

