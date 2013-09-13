
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

static struct {
	struct clk		*cpu_clk;
	struct cpufreq_freqs	freqs;
	struct cpufreq_frequency_table *freq_tbl;
	unsigned int transition_latency;
} sirf_cpufreq;

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

	clk_set_rate(sirf_cpufreq.cpu_clk, sirf_cpufreq.freqs.new * 1000);

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
	const struct property *prop;
	struct cpufreq_frequency_table *freq_tbl;
	const __be32 *val;
	int cnt, i, ret;

	np = of_find_node_by_path("/cpus/cpu@0");
	if (!np) {
		pr_err("No cpu node found");
		return -ENODEV;
	}

	if (of_property_read_u32(np, "cpufreq_transition_latency",
				&sirf_cpufreq.transition_latency))
		sirf_cpufreq.transition_latency = CPUFREQ_ETERNAL;

	prop = of_find_property(np, "cpufreq_tbl", NULL);
	if (!prop || !prop->value) {
		pr_err("Invalid cpufreq_tbl");
		ret = -ENODEV;
		goto out_put_node;
	}

	cnt = prop->length / sizeof(u32);
	val = prop->value;

	freq_tbl = kzalloc(sizeof(*freq_tbl) * (cnt + 1), GFP_KERNEL);
	if (!freq_tbl) {
		ret = -ENOMEM;
		goto out_put_node;
	}

	for (i = 0; i < cnt; i++) {
		freq_tbl[i].index = i;
		freq_tbl[i].frequency = be32_to_cpup(val++);
	}

	freq_tbl[i].index = i;
	freq_tbl[i].frequency = CPUFREQ_TABLE_END;

	sirf_cpufreq.freq_tbl = freq_tbl;

	of_node_put(np);

	sirf_cpufreq.cpu_clk = clk_get_sys("cpu", NULL);
	if (IS_ERR(sirf_cpufreq.cpu_clk)) {
		pr_err("Get cpu clock failed.\n");
		ret = PTR_ERR(sirf_cpufreq.cpu_clk);
		goto out_put_mem;
	}

	ret =  cpufreq_register_driver(&sirf_driver);
	if (!ret)
		return 0;

	pr_err("failed register driver: %d\n", ret);
	clk_put(sirf_cpufreq.cpu_clk);

out_put_mem:
	kfree(freq_tbl);
	return ret;

out_put_node:
	of_node_put(np);
	return ret;

}
late_initcall(sirf_cpufreq_init);

MODULE_DESCRIPTION("SiRF SoC cpufreq driver");
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
MODULE_LICENSE("GPL v2");
