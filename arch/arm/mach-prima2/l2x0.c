/*
 * l2 cache initialization for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/hardware/cache-l2x0.h>

int sirfsoc_l2x0_init(void)
{
	l2x0_of_init(0x40000, 0);
	return 0;
}
early_initcall(sirfsoc_l2x0_init);

#ifdef CONFIG_PM
#include <linux/syscore_ops.h>

static int sirfsoc_l2x0_pm_suspend(void)
{
	return 0;
}

static void sirfsoc_l2x0_pm_resume(void)
{
	sirfsoc_l2x0_init();
}

static struct syscore_ops sirfsoc_l2x0_pm_syscore_ops = {
	.suspend	= sirfsoc_l2x0_pm_suspend,
	.resume		= sirfsoc_l2x0_pm_resume,
};

static int sirfsoc_l2x0_pm_init(void)
{
	register_syscore_ops(&sirfsoc_l2x0_pm_syscore_ops);
	return 0;
}
late_initcall(sirfsoc_l2x0_pm_init);

#endif
