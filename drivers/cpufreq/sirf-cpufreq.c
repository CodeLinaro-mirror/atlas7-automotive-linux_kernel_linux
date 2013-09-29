
/*
 * CPU frequency scaling support for CSR SiRFSoC
 *
 * Copyright (c) 2011-2013 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/cpufreq.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/opp.h>

#define SIRFSOC_MAX_VOLTAGE	1200000

static struct {
	struct clk			*cpu_clk;
	struct device			*cpu_dev;
	struct cpufreq_freqs		freqs;
	struct cpufreq_frequency_table	*freq_tbl;
	unsigned int			transition_latency;
	struct regulator		*regulator;
} sirf_cpufreq;

static void update_voltage(int min_uV, int max_uV)
{
	if (sirf_cpufreq.regulator == NULL)
		return;

	regulator_set_voltage(sirf_cpufreq.regulator, min_uV, max_uV);
}

static int sirf_verify_speed(struct cpufreq_policy *policy)
{
	if (sirf_cpufreq.freq_tbl)
		return cpufreq_frequency_table_verify(policy,
			sirf_cpufreq.freq_tbl);
	return 0;
}

static unsigned int sirf_getspeed(unsigned int cpu)
{
	return clk_get_rate(sirf_cpufreq.cpu_clk) / 1000;
}

static int sirf_target(struct cpufreq_policy *policy,
			  unsigned int target_freq,
			  unsigned int relation)
{
	unsigned long freq, volt = 0;
	struct opp *opp;
	unsigned int index, old_index;

	if (!sirf_cpufreq.freq_tbl)
		return -EINVAL;

	sirf_cpufreq.freqs.old = sirf_getspeed(policy->cpu);

	if (cpufreq_frequency_table_target(policy, sirf_cpufreq.freq_tbl,
					sirf_cpufreq.freqs.old,
					relation, &old_index))
		return -EINVAL;

	if (cpufreq_frequency_table_target(policy, sirf_cpufreq.freq_tbl,
					   target_freq, relation, &index))
		return -EINVAL;

	sirf_cpufreq.freqs.new = sirf_cpufreq.freq_tbl[index].frequency;
	sirf_cpufreq.freqs.cpu = policy->cpu;

	if (sirf_cpufreq.freqs.new == sirf_cpufreq.freqs.old)
		return 0;

	cpufreq_notify_transition(policy, &sirf_cpufreq.freqs,
			CPUFREQ_PRECHANGE);

	freq = sirf_cpufreq.freqs.new * 1000;
	rcu_read_lock();
	opp = opp_find_freq_ceil(sirf_cpufreq.cpu_dev, &freq);
	if (IS_ERR(opp)) {
		rcu_read_unlock();
		dev_err(sirf_cpufreq.cpu_dev, "%s: unable to find MPU OPP for %d\n",
				__func__, sirf_cpufreq.freqs.new);
		return -EINVAL;
	}

	volt = opp_get_voltage(opp);
	rcu_read_unlock();
	/* control regulator: voltage up */
	if ((sirf_cpufreq.freqs.new > sirf_cpufreq.freqs.old) &&
		volt)
		update_voltage(volt, SIRFSOC_MAX_VOLTAGE);
	clk_set_rate(sirf_cpufreq.cpu_clk, sirf_cpufreq.freqs.new * 1000);

	/* control regulator: voltage down */
	if ((sirf_cpufreq.freqs.new < sirf_cpufreq.freqs.old) &&
		volt)
		update_voltage(volt, SIRFSOC_MAX_VOLTAGE);

	cpufreq_notify_transition(policy, &sirf_cpufreq.freqs,
			CPUFREQ_POSTCHANGE);
	return 0;
}

static int sirf_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	if (!sirf_cpufreq.freq_tbl)
		return -EINVAL;
	policy->cur = policy->min = policy->max = sirf_getspeed(policy->cpu);

	cpufreq_frequency_table_get_attr(sirf_cpufreq.freq_tbl, policy->cpu);

	/*
	 * set the actual transition latency for
	 * worstcase with +20% margin.
	 */
	policy->cpuinfo.transition_latency = sirf_cpufreq.transition_latency;
	cpumask_setall(policy->cpus);
	return cpufreq_frequency_table_cpuinfo(policy, sirf_cpufreq.freq_tbl);
}

static struct cpufreq_driver sirf_driver = {
	.flags		= CPUFREQ_STICKY,
	.verify		= sirf_verify_speed,
	.target		= sirf_target,
	.get		= sirf_getspeed,
	.init		= sirf_cpufreq_cpu_init,
	.name		= "sirf_cpufreq",
};

static int __init sirf_cpufreq_init(void)
{
	struct device_node *np;
	int ret;

	sirf_cpufreq.cpu_dev = get_cpu_device(0);

	np = of_find_node_by_path("/cpus/cpu@0");
	if (!np) {
		pr_err("No cpu node found");
		return -ENODEV;
	}

	sirf_cpufreq.cpu_dev->of_node = np;

	if (of_property_read_u32(np, "cpufreq_transition_latency",
				&sirf_cpufreq.transition_latency))
		sirf_cpufreq.transition_latency = CPUFREQ_ETERNAL;

	ret = of_init_opp_table(sirf_cpufreq.cpu_dev);
	if (ret) {
		pr_err("Failed init opp table.\n");
		goto out_put_node;
	}

	ret = opp_init_cpufreq_table(sirf_cpufreq.cpu_dev,
		&sirf_cpufreq.freq_tbl);
	if (ret) {
		pr_err("Failed init opp cpufreq table.\n");
		goto out_put_node;
	}

	sirf_cpufreq.cpu_clk = clk_get_sys("cpu", NULL);
	if (IS_ERR(sirf_cpufreq.cpu_clk)) {
		pr_err("Get cpu clock failed.\n");
		ret = PTR_ERR(sirf_cpufreq.cpu_clk);
		goto out_free_opp;
	}

	sirf_cpufreq.regulator = regulator_get(NULL, "vcore");
	if (IS_ERR(sirf_cpufreq.regulator))
		sirf_cpufreq.regulator = NULL;

	ret =  cpufreq_register_driver(&sirf_driver);
	if (!ret)
		goto out_put_node;

	pr_err("failed register driver: %d\n", ret);
	clk_put(sirf_cpufreq.cpu_clk);

out_free_opp:
	opp_free_cpufreq_table(sirf_cpufreq.cpu_dev, &sirf_cpufreq.freq_tbl);
out_put_node:
	of_node_put(np);
	return ret;
}
late_initcall(sirf_cpufreq_init);

MODULE_DESCRIPTION("SiRF SoC cpufreq driver");
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
MODULE_LICENSE("GPL v2");
