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

#include "noc.h"


/*qos*/
struct noc_qos_t {
	const char *name;
	u32 offset;
	u32 bw;
	u32 saturation;
	u32 priority;
	u32 mode;
	u32 mhz;
	struct clk *clk;
	struct noc_macro *nocm;
};

struct qos_generator_register {
	u32	id_coreid;
	u32	id_revisionid;
	u32	priority;
	u32	mode;
	u32	bw;
	u32	saturation;
	u32	extcontrol;
};

static inline int qos_enable_clk(struct noc_qos_t *entry)
{
	int ret = 0;

	if (entry->clk) {
		ret = clk_prepare_enable(entry->clk);
		if (ret) {
			pr_err("%s: failed clk_prepare_enable\n", __func__);
			return ret;
		}
	}

	return ret;
}

static inline void qos_disable_clk(struct noc_qos_t *entry)
{
	if (entry->clk)
		clk_disable_unprepare(entry->clk);
}

static int qos_generator_get(struct noc_qos_t *entry,
		struct noc_macro *nocm)
{
	u32 bw, extcontrol;
	struct qos_generator_register *qos_reg =
		(struct qos_generator_register *)(nocm->mbase +
		entry->offset);
	int ret = 0;
	ret = qos_enable_clk(entry);
	if (ret)
		return ret;

	entry->mhz = clk_get_rate(entry->clk) / 1000000;
	bw = readl_relaxed(&qos_reg->bw);
	entry->bw = bw * entry->mhz / 256;
	entry->mode = readl_relaxed(&qos_reg->mode);
	entry->saturation = readl_relaxed(&qos_reg->saturation);
	entry->priority = readl_relaxed(&qos_reg->priority);
	extcontrol = readl_relaxed(&qos_reg->extcontrol);
	qos_disable_clk(entry);

	return ret;
}

static int qos_generator_set(struct noc_qos_t *entry,
		struct noc_macro *nocm)
{
	u32 bw;
	struct qos_generator_register *qos_reg =
		(struct qos_generator_register *)(nocm->mbase +
		entry->offset);
	int ret = 0;

	ret = qos_enable_clk(entry);
	if (ret)
		return ret;

	entry->mhz = clk_get_rate(entry->clk) / 1000000;

	bw = entry->bw * 256 / entry->mhz;
	writel_relaxed(bw, &qos_reg->bw);
	writel_relaxed(entry->mode, &qos_reg->mode);
	writel_relaxed(entry->saturation, &qos_reg->saturation);
	writel_relaxed(entry->priority, &qos_reg->priority);
	writel_relaxed(0, &qos_reg->extcontrol);
	qos_disable_clk(entry);
	return ret;
}

static ssize_t qos_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct noc_macro *nocm = (struct noc_macro *)dev_get_drvdata(dev);
	struct noc_qos_t *entry;
	int i, pos = 0;

	if (!(nocm->qos_tbl))
		return pos;

	pos += scnprintf(buf + pos,
		PAGE_SIZE - pos,
		"Niu:\t\t\tmode\tbw\tpriority\tsaturation\n");

	for (i = 0; i < nocm->qos_size; i++) {
		entry = nocm->qos_tbl + i;
		if (qos_generator_get(entry, nocm))
			return pos;

		pos += scnprintf(buf + pos,
			PAGE_SIZE - pos,
			"%-24s%d\t%d\t0x%x\t0x%x\n",
			entry->name,
			entry->mode,
			entry->bw,
			entry->priority,
			entry->saturation);
	}
	return pos;
}

static ssize_t qos_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct noc_macro *nocm = (struct noc_macro *)dev_get_drvdata(dev);
	struct noc_qos_t *entry;
	struct noc_qos_t params;
	char *name;
	u32 i;

	memset(&params, 0, sizeof(params));

	name = kzalloc(len + 1, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	if (sscanf(buf, "%s %u %u %x %x\n",
		name,
		&params.mode,
		&params.bw,
		&params.priority,
		&params.saturation) != 5) {
		kfree(name);
		return -EINVAL;
	}

	for (i = 0; i < nocm->qos_size; i++) {
		entry = nocm->qos_tbl + i;
		if (!strcmp(entry->name, name))
			break;
	}

	if (i >= nocm->qos_size || entry == NULL) {
		kfree(name);
		return -EINVAL;
	}
	entry->mode = params.mode;
	entry->bw = params.bw;
	entry->mode = params.mode;
	entry->priority = params.priority;
	entry->saturation = params.saturation;

	qos_generator_set(entry, nocm);

	kfree(name);

	return len;
}

static DEVICE_ATTR_RW(qos);


int noc_qos_parse(struct noc_macro *nocm)
{
	struct platform_device *pdev = nocm->pdev;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *bnp, *pp;
	struct noc_qos_t *entry;
	const char *clock_name;
	u32 i = 0;
	int ret = 0;

	bnp = of_get_child_by_name(np, "qos");
	if (!bnp)
		return 0;

	nocm->qos_size = of_get_child_count(bnp);
	nocm->qos_tbl = devm_kzalloc(&pdev->dev, nocm->qos_size *
				sizeof(struct noc_qos_t), GFP_KERNEL);
	if (!nocm->qos_tbl)
		return -ENOMEM;

	pr_debug("qos table[%d]\n", nocm->qos_size);

	for_each_child_of_node(bnp, pp) {
		entry = nocm->qos_tbl + i;
		i++;

		entry->name = strrchr(of_node_full_name(pp), '/') + 1;
		entry->nocm = nocm;

		ret = of_property_read_u32(pp, "offset",
			&entry->offset);
		if (ret)
			goto err;

		ret = of_property_read_u32(pp, "bw", &entry->bw);
		if (ret)
			goto err;

		ret = of_property_read_u32(pp, "saturation",
			&entry->saturation);
		if (ret)
			goto err;

		ret = of_property_read_u32(pp, "priority", &entry->priority);
		if (ret)
			goto err;

		ret = of_property_read_u32(pp, "mode", &entry->mode);
		if (ret)
			goto err;

		ret = of_property_read_string(pp, "clock_name",
			&clock_name);
		if (ret)
			goto err;

		entry->clk = devm_clk_get(&nocm->pdev->dev,
					clock_name);
		if (IS_ERR(entry->clk)) {
			pr_err("%s: failed get clk of %s!try later\n",
				__func__, clock_name);
			entry->clk = NULL;
			ret = -EPROBE_DEFER;
			goto err;
		}

		pr_debug("%s\t0x%x\t%d\t0x%x\t0x%x\t%d\t%s\n",
			entry->name, entry->offset, entry->bw,
			entry->saturation, entry->priority,
			entry->mode, clock_name);
	}

	return 0;
err:
	if (nocm->qos_tbl)
		devm_kfree(&pdev->dev, nocm->qos_tbl);
	return ret;
}

int noc_qos_init(struct noc_macro *nocm)
{
	struct noc_qos_t *entry;
	int j;
	int ret = 0;
	struct platform_device *pdev = nocm->pdev;

	ret = noc_qos_parse(nocm);
	if (ret)
		return ret;

	if (!(nocm->qos_tbl))
		return 0;

	for (j = 0; j < nocm->qos_size; j++) {
		entry = nocm->qos_tbl + j;
		ret = qos_generator_set(entry, nocm);
		if (ret)
			goto err;
	}

	for (j = 0; j < nocm->qos_size; j++) {
		entry = nocm->qos_tbl + j;
		ret = qos_generator_get(entry, nocm);
		if (ret)
			goto err;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_qos);
	if (ret)
		goto err;

	return 0;
err:
	return ret;
}
