/*
 * Atlas7 NoC support
 *
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
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
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/sysfs.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <asm/div64.h>
#include "noc.h"
#include "trace.h"

#define SHOW_BW_VALUE_0		0	/*show the bw even it is 0*/
#define PROBE_SINGLE_PORT	((u32)-1)
struct noc_probe_t;

struct probe_regs_t {
	u32 id_core_id;
	u32 id_revision_Id;
	u32 main_ctl;
	u32 cfg_ctl;
	u32 trace_port_sel;
	u32 filter_lut;
	u32 reserved[3];
	u32 stat_period;
	u32 stat_go;
	u32 stat_alarm_min;
	u32 stat_alarm_max;
	u32 stat_alarm_status;
	u32 stat_alarm_clr;
	u32 stat_alarm_en;
	u32 reserved1[61];
	u32 counters_0_portsel;
	u32 counters_0_src;
	u32 counters_0_alarm_mode;
	u32 counters_0_val;
	u32 reserved2;
	u32 counters_1_portsel;
	u32 counters_1_src;
	u32 counters_1_alarm_mode;
	u32 counters_1_val;
};

/*probe*/
struct noc_macro_bw_t {
	u32 peak;
	u32 cur;
	u32 avg;
	u32 cnt;
	u64 sum;
};

struct noc_probe_t {
	char *name;
	u32 offset;
	u32 port;
	u32 period;
	u32 mhz;
	u32 probe_enable;
	struct clk *clk;
	struct noc_macro *nocm;
	struct noc_macro_bw_t bw;
};



static inline int probe_enable_clk(struct noc_probe_t *entry)
{
	int ret = 0;

	if (entry->clk) {
		ret = clk_prepare_enable(entry->clk);
		if (ret) {
			pr_err("%s: failed clk_prepare_enable\n",
				__func__);
			return ret;
		}
	}

	return ret;
}

static inline void probe_disable_clk(struct noc_probe_t *entry)
{
	if (entry->clk)
		clk_disable_unprepare(entry->clk);
}

/*
 * get appropriate period for different nocms according to their clock
 * compared to ddrm clock, to make them have approximately the same alram time
 */
#define NOC_PROBE_REF_FREQ 400
#define NOC_PROBE_REF_PERIOD 0x1a

static u32 noc_probe_get_period(u32 mhz)
{
	u32 i;
	u32 period;

	period = NOC_PROBE_REF_FREQ / mhz;
	for (i = 1; i < 32; i++)
		if (period>>i == 0)
			break;
	period = NOC_PROBE_REF_PERIOD - i + 1;

	return period;
}

#define NOC_PROBE_MODE_MAX 0x2
#define NOC_PROBE_EVENT 0x8

static void noc_probe_stop(struct noc_probe_t *entry)
{
	struct probe_regs_t	 *probe_reg;
	struct noc_macro *nocm = entry->nocm;

	if (!entry->probe_enable)
		return;

	probe_reg = (struct probe_regs_t	*)
			(nocm->mbase + entry->offset);

	/*clear field GlobalEn enable the counting of bytes.*/
	writel_relaxed(0, &probe_reg->cfg_ctl);

	/*clear staten, alarmen*/
	writel_relaxed(readl_relaxed(&probe_reg->main_ctl) & ~0x18,
			&probe_reg->main_ctl);
	writel_relaxed(0, &probe_reg->stat_alarm_en);

	probe_disable_clk(entry);
	entry->probe_enable = 0;
	memset(&entry->bw, 0, sizeof(entry->bw));
}

static int noc_probe_start(struct noc_probe_t *entry)
{
	struct probe_regs_t	 *probe_reg;
	struct noc_macro *nocm = entry->nocm;
	int ret = 0;

	probe_reg = (struct probe_regs_t	*)
			(nocm->mbase + entry->offset);

	if (entry->probe_enable)
		goto out;

	ret = probe_enable_clk(entry);
	if (ret)
		goto out;

	/*re-statistics*/
	memset(&entry->bw, 0, sizeof(entry->bw));

	/*StatEn */
	writel_relaxed(readl_relaxed(&probe_reg->main_ctl) | BIT(3),
		&probe_reg->main_ctl);

	/*
	* Only if The table above contain port number:
	* Set register counters_0_portsel to the value
	* corresponding to the probe point of interest.
	* no need , A& probe doesnt have more than one port
	*/
	if (entry->port != PROBE_SINGLE_PORT)
		writel_relaxed(entry->port,
			&probe_reg->counters_0_portsel);

	/* Set register counters_0_src to 0x8 (BYTES) to count bytes.*/
	writel_relaxed(NOC_PROBE_EVENT,
		&probe_reg->counters_0_src);

	/*
	* Set register counters_1_src to 0x10 (CHAIN)
	* to increment when counter 0 wraps.
	*/
	writel_relaxed(0x10, &probe_reg->counters_1_src);

	entry->mhz = clk_get_rate(entry->clk) / 1000000;

	entry->period = noc_probe_get_period(entry->mhz);

	pr_debug("%s(%dMHz), period=0x%x\n",
		entry->name, (int)(entry->mhz), entry->period);
	/*
	* Setting register stat_period to 2^period cycles.
	* also can config to 0x00 ( manual mode )
	*/
	writel_relaxed(entry->period, &probe_reg->stat_period);

	/*alarm mode, chained*/
	writel_relaxed(NOC_PROBE_MODE_MAX,
		&probe_reg->counters_0_alarm_mode);
	writel_relaxed(0, &probe_reg->counters_1_alarm_mode);

	/*set alarmMax and Min*/
	if (!strcmp(entry->name, "ddrm"))
		writel_relaxed(0xFFFFF, &probe_reg->stat_alarm_max);
	else
		writel_relaxed(0xFFF, &probe_reg->stat_alarm_max);
	writel_relaxed(0, &probe_reg->stat_alarm_min);

	/*enable alm*/
	writel_relaxed(readl_relaxed(&probe_reg->main_ctl) | 0x10,
			&probe_reg->main_ctl);
	writel_relaxed(1, &probe_reg->stat_alarm_en);


	/*Set field GlobalEn enable the counting of bytes.*/
	writel(1, &probe_reg->cfg_ctl);
	entry->probe_enable = 1;

out:
	return 0;
}

void noc_handle_probe(struct noc_macro *nocm)
{
	struct probe_regs_t	 *probe_reg;
	struct noc_probe_t *entry;
	struct noc_macro_bw_t *bw;
	u32 val, i;
	u64  mult, sum;

	for (i = 0; i < nocm->probe_size; i++) {

		entry = nocm->probe_tbl + i;
		probe_reg = (struct probe_regs_t	*)
			(nocm->mbase + entry->offset);

		if (!entry->probe_enable)
			continue;

		if (!readl(&probe_reg->stat_alarm_status))
			continue;

		val = (readl_relaxed(&probe_reg->counters_1_val) << 16) |
				readl_relaxed(&probe_reg->counters_0_val);
		if (!val)
			continue;
		trace_noc_bw_data(entry->name, val);
		bw = &entry->bw;
		/*overflow?*/
		if (bw->cnt + 1 < bw->cnt || bw->sum + val < bw->sum) {
			bw->cnt = 0;
			bw->sum = 0;
		}
		/*calculate cur*/
		mult = (u64)val * entry->mhz;
		do_div(mult, 1<<entry->period);
		bw->cur = (u32)mult;

		/*calculate peak*/
		bw->peak = max(bw->cur, bw->peak);

		/*calculate avg*/
		bw->sum += val;
		bw->cnt++;
		sum = bw->sum;

		mult = (u64)sum * entry->mhz;
		do_div(mult, bw->cnt);
		do_div(mult, 1<<entry->period);
		bw->avg = (u32)mult;

		/*clr the alm*/
		writel(1, &probe_reg->stat_alarm_clr);
	}

}

#ifdef CONFIG_PM_SLEEP
int noc_probe_suspend(struct noc_macro *nocm)
{
	struct noc_probe_t *entry;
	int i;

	for (i = 0; i < nocm->probe_size; i++) {
		entry = nocm->probe_tbl + i;

		if (entry->probe_enable && !IS_ERR(entry->clk))
			probe_disable_clk(entry);
	}
	return 0;
}

int noc_probe_resume(struct noc_macro *nocm)
{
	struct noc_probe_t *entry;
	int i;

	for (i = 0; i < nocm->probe_size; i++) {
		entry = nocm->probe_tbl + i;

		if (entry->probe_enable && !IS_ERR(entry->clk))
			probe_enable_clk(entry);
	}
	return 0;
}
#endif

static ssize_t probe_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct noc_macro *nocm = (struct noc_macro *)dev_get_drvdata(dev);
	struct noc_probe_t *entry;
	struct noc_macro_bw_t *bw;
	int pos = 0, i;

	pos += scnprintf(buf + pos,
		PAGE_SIZE - pos,
		"Niu:\tcur\tpeak\tavgMBps\n");

	for (i = 0; i < nocm->probe_size; i++) {
		entry = nocm->probe_tbl + i;
		bw = &entry->bw;
		pos += scnprintf(buf + pos,
			PAGE_SIZE - pos,
			"%s\t%d\t%d\t%d\n",
			entry->name,
			bw->cur,
			bw->peak,
			bw->avg);
	}
	return pos;
}

static ssize_t probe_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct noc_macro *nocm = (struct noc_macro *)dev_get_drvdata(dev);
	struct noc_probe_t *entry;
	int i;
	char *name;

	name = kzalloc(len + 1, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	if (sscanf(buf, "%s\n", name) != 1) {
		kfree(name);
		return -EINVAL;
	}

	for (i = 0; i < nocm->probe_size; i++) {
		entry = nocm->probe_tbl + i;
		if (!strcmp(entry->name, name)) {
			noc_probe_stop(entry);
			noc_probe_start(entry);
		}
	}

	if (strcmp(name, "stop"))
		goto out;

	for (i = 0; i < nocm->probe_size; i++) {
		entry = nocm->probe_tbl + i;
		noc_probe_stop(entry);
	}
out:
	kfree(name);
	return len;
}

static DEVICE_ATTR_RW(probe);

int noc_probe_init(struct noc_macro *nocm)
{
	struct platform_device *pdev = nocm->pdev;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *bnp, *pp;
	struct noc_probe_t *entry;
	u32 i = 0;
	int ret = 0;
	const char *clock_name;

	bnp = of_get_child_by_name(np, "bw_probe");
	if (!bnp) {
		pr_debug("bw_probe not found\n");
		return -ENODEV;
	}

	nocm->probe_size = of_get_child_count(bnp);
	nocm->probe_tbl = devm_kzalloc(&pdev->dev, nocm->probe_size *
				sizeof(struct noc_probe_t), GFP_KERNEL);
	if (!nocm->probe_tbl)
		return -ENOMEM;

	for_each_child_of_node(bnp, pp) {
		entry = nocm->probe_tbl + i;
		i++;

		entry->name = strrchr(of_node_full_name(pp), '/') + 1;
		entry->nocm = nocm;

		ret = of_property_read_u32(pp, "offset",
			&entry->offset);
		if (ret) {
			pr_err("of_property_read_u32 off failed\n");
			goto err;
		}

		ret = of_property_read_string(pp, "clock_name",
			&clock_name);
		if (ret) {
			pr_err("of_property_read_u32 off failed\n");
			goto err;
		}

		entry->clk = devm_clk_get(&nocm->pdev->dev,
					clock_name);
		if (IS_ERR(entry->clk)) {
			ret = PTR_ERR(entry->clk);
			pr_err("%s: failed get clk of %s!%d\n",
				__func__, clock_name, ret);
			goto err;
		}

		ret = of_property_read_u32(pp, "port", &entry->port);
		if (ret)
			entry->port = PROBE_SINGLE_PORT;
	}
	ret = device_create_file(&pdev->dev, &dev_attr_probe);
	if (ret)
		goto err;

	return 0;
err:
	devm_kfree(&pdev->dev, nocm->probe_tbl);
	return ret;
}
