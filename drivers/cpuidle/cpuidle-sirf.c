/*
 * CPU idle drivers for CSR SiRFprimaII
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
#include <linux/cpuidle.h>
#include <linux/io.h>
#include <linux/time.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>
#include <linux/cpu.h>
#include <linux/pm_opp.h>
#include <asm/proc-fns.h>
#include <asm/cpuidle.h>

#define SIRFSOC_MAX_VOLTAGE	1200000

/*
 * now we have only 2 C state WFI
 * 1. ARM WFI
 * 2. WFI + switch to 26Mhz clock source + lower volatge
 */

static struct {
	struct clk		*cpu_clk;
	struct clk		*osc_clk;
	struct regulator	*vcore_regulator;
	struct device		*cpu_dev;
} sirf_cpuidle;

static int sirf_enter_idle(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	struct clk *parent_clk;
	unsigned long volt_old = 0, volt_new, freq;
	struct dev_pm_opp *opp;

	local_irq_disable();
	parent_clk = clk_get_parent(sirf_cpuidle.cpu_clk);
	clk_set_parent(sirf_cpuidle.cpu_clk, sirf_cpuidle.osc_clk);

	if (sirf_cpuidle.vcore_regulator) {
		volt_old = regulator_get_voltage(sirf_cpuidle.vcore_regulator);

		freq = clk_get_rate(sirf_cpuidle.osc_clk);
		rcu_read_lock();
		opp = dev_pm_opp_find_freq_ceil(sirf_cpuidle.cpu_dev, &freq);
		if (IS_ERR(opp)) {
			rcu_read_unlock();
			return -EINVAL;
		}

		volt_new = dev_pm_opp_get_voltage(opp);
		rcu_read_unlock();

		regulator_set_voltage(sirf_cpuidle.vcore_regulator, volt_new,
				SIRFSOC_MAX_VOLTAGE);
	}
	/* Wait for interrupt state */
	cpu_do_idle();

	if (sirf_cpuidle.vcore_regulator)
		regulator_set_voltage(sirf_cpuidle.vcore_regulator, volt_old,
				SIRFSOC_MAX_VOLTAGE);

	clk_set_parent(sirf_cpuidle.cpu_clk, parent_clk);
	/* Todo: other C states */

	local_irq_enable();
	return index;
}

static struct cpuidle_driver sirf_cpuidle_driver = {
	.name = "sirf_cpuidle",
	.states = {
		ARM_CPUIDLE_WFI_STATE,
		{
			.name = "WFI-LP",
			.desc = "WFI low power",
			.flags = CPUIDLE_FLAG_TIME_VALID,
			.exit_latency = 10,
			.target_residency = 10000,
			.enter = sirf_enter_idle,
		},
	},
	.state_count = 2,
};

/* Initialize CPU idle by registering the idle states */
static int sirfsoc_init_cpuidle(void)
{
	int ret = 0;

	sirf_cpuidle.cpu_clk = clk_get_sys("cpu", NULL);
	if (IS_ERR(sirf_cpuidle.cpu_clk))
		return PTR_ERR(sirf_cpuidle.cpu_clk);

	sirf_cpuidle.osc_clk = clk_get_sys("osc", NULL);
	if (IS_ERR(sirf_cpuidle.osc_clk)) {
		ret = PTR_ERR(sirf_cpuidle.osc_clk);
		goto out_put_osc_clk;
	}

	sirf_cpuidle.vcore_regulator = regulator_get(NULL, "vcore");
	if (IS_ERR(sirf_cpuidle.vcore_regulator))
		sirf_cpuidle.vcore_regulator = NULL;

	sirf_cpuidle.cpu_dev = get_cpu_device(0);
	if (IS_ERR(sirf_cpuidle.cpu_dev)) {
		ret = PTR_ERR(sirf_cpuidle.cpu_dev);
		goto out_put_clk;
	}

	ret = cpuidle_register(&sirf_cpuidle_driver, NULL);
	if (!ret)
		return ret;

out_put_clk:
	clk_put(sirf_cpuidle.cpu_clk);
out_put_osc_clk:
	clk_put(sirf_cpuidle.osc_clk);
	return ret;
}
late_initcall(sirfsoc_init_cpuidle);
