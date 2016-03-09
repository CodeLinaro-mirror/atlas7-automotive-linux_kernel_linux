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

#include "noc.h"


/*qos*/
struct noc_qos_t {
	const char *name;
	u32 reg_offset;
	u32 enabled;
	u32 bw;
	u32 saturation;
	u32 priority;
	u32 mode;
	const char *clock_name;
	u32 divider;	/*divider-1, default 0 will mean no divider*/
	u32 clkfreqMhz;	/*this should be caculated dynamically*/
	struct clk *clk;
	struct noc_macro *nocm;
	/* for sysfs*/
	struct kobj_ext_attribute qos_bw;
	struct kobj_ext_attribute qos_saturation;
	struct kobj_ext_attribute qos_priority;
	struct kobj_ext_attribute qos_mode;
	struct attribute *qos_attrs[5];	/*NULL terminated*/
	struct attribute_group qos_attr_group;
};

#define DEF_PRIO	0x00000404
#define RTLL_PRIO	0x00000707
#define RT_PRIO		0x00000505
#define DEF_MODE	0


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
			pr_err("%s: failed clk_prepare_enable %s!\n",
				__func__, entry->clock_name);
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
		entry->reg_offset);
	int ret = 0;
	u32 divider = 1;

	ret = qos_enable_clk(entry);
	if (ret)
		return ret;

	if (entry->clk) {
		divider = entry->divider + 1;
		entry->clkfreqMhz =
			clk_get_rate(entry->clk) / 1000000 / divider;
	}
	BUG_ON(entry->clkfreqMhz == 0);

	bw = readl_relaxed(&qos_reg->bw);
	entry->bw = bw * entry->clkfreqMhz / 256;
	entry->mode = readl_relaxed(&qos_reg->mode);
	entry->saturation = readl_relaxed(&qos_reg->saturation);
	entry->priority = readl_relaxed(&qos_reg->priority);
	extcontrol = readl_relaxed(&qos_reg->extcontrol);
	pr_debug("get: %s qos: %d(reg=0x%x, f=%dM), 0x%x, 0x%x, 0x%x, 0x%x\n",
		entry->name, entry->bw, bw, entry->clkfreqMhz,
		entry->priority, entry->mode, entry->saturation, extcontrol);

	qos_disable_clk(entry);

	return ret;
}

static int qos_generator_set(struct noc_qos_t *entry,
		struct noc_macro *nocm)
{
	u32 bw;
	struct qos_generator_register *qos_reg =
		(struct qos_generator_register *)(nocm->mbase +
		entry->reg_offset);
	int ret = 0;
	u32 divider = 1;

	ret = qos_enable_clk(entry);
	if (ret)
		return ret;

	if (entry->clk) {
		divider = entry->divider + 1;
		entry->clkfreqMhz =
			clk_get_rate(entry->clk) / 1000000 / divider;
	}
	BUG_ON(entry->clkfreqMhz == 0);

	bw = entry->bw * 256 / entry->clkfreqMhz;
	writel_relaxed(bw, &qos_reg->bw);
	writel_relaxed(entry->mode, &qos_reg->mode);
	writel_relaxed(entry->saturation, &qos_reg->saturation);
	writel_relaxed(entry->priority, &qos_reg->priority);
	writel_relaxed(0, &qos_reg->extcontrol);
	pr_debug("set: %s qos values read:  0x%x, 0x%x, 0x%x, 0x%x\n",
		entry->name, readl_relaxed(&qos_reg->bw),
		readl_relaxed(&qos_reg->priority),
		readl_relaxed(&qos_reg->mode),
		readl_relaxed(&qos_reg->saturation));

	qos_disable_clk(entry);

	return ret;
}

static int qos_generator_init(struct noc_macro *nocm)
{
	struct noc_qos_t *entry;
	int j;
	int ret = 0;

	if (!(nocm->qos_tbl))
		return 0;

	for (j = 0; j < nocm->qos_size; j++) {
		entry = nocm->qos_tbl + j;
		if (entry->enabled) {
			ret = qos_generator_set(entry, nocm);
			if (ret)
				return ret;
		}
	}

	for (j = 0; j < nocm->qos_size; j++) {
		entry = nocm->qos_tbl + j;
		ret = qos_generator_get(entry, nocm);
		if (ret)
			return ret;
	}

	return ret;
}

static ssize_t all_qos_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	struct kobj_ext_attribute *ea = to_ext_attr(attr);
	struct noc_macro *nocm = (struct noc_macro *)ea->var;
	struct noc_qos_t *entry;
	int j, pos = 0;

	if (!(nocm->qos_tbl))
		return pos;

	for (j = 0; j < nocm->qos_size; j++) {
		entry = nocm->qos_tbl + j;
		if (qos_generator_get(entry, nocm))
			return pos;
	}

	pos += scnprintf(buf + pos,
		PAGE_SIZE - pos,
		"Niu:\tbw\tpriority\tmode\tsaturation\tclkfreqMhz\n");

	pos += scnprintf(buf + pos,
		PAGE_SIZE - pos,
		"%s->:\n",
		nocm->name);

	for (j = 0; j < nocm->qos_size; j++) {
		entry = nocm->qos_tbl + j;
		pos += scnprintf(buf + pos,
			PAGE_SIZE - pos,
			"%20s\t%10dMBps\t0x%x\t%d\t0x%x\t%dM\n",
			entry->name,
			entry->bw,
			entry->priority,
			entry->mode,
			entry->saturation,
			entry->clkfreqMhz);

	}

	return pos;
}

static ssize_t qos_bw_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	struct kobj_ext_attribute *ea = to_ext_attr(attr);
	struct noc_qos_t *entry = (struct noc_qos_t *)ea->var;
	struct qos_generator_register *qos_reg =
		(struct qos_generator_register *)(entry->nocm->mbase +
		entry->reg_offset);
	u32 bw;
	u32 divider = 1;
	int pos = 0;
	int ret;

	ret = qos_enable_clk(entry);
	if (ret)
		return ret;
	if (entry->clk) {
		divider = entry->divider + 1;
		entry->clkfreqMhz =
			clk_get_rate(entry->clk) / 1000000 / divider;
	}
	BUG_ON(entry->clkfreqMhz == 0);

	bw = readl_relaxed(&qos_reg->bw);
	entry->bw = bw * entry->clkfreqMhz / 256;

	pos += scnprintf(buf + pos,
		PAGE_SIZE - pos,
		"%d\n",
		entry->bw);

	qos_disable_clk(entry);

	return pos;
}

static ssize_t qos_bw_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t len)
{
	struct kobj_ext_attribute *ea = to_ext_attr(attr);
	struct noc_qos_t *entry = (struct noc_qos_t *)ea->var;
	struct qos_generator_register *qos_reg =
		(struct qos_generator_register *)(entry->nocm->mbase +
		entry->reg_offset);
	u32 bw;
	u32 divider = 1;
	int ret;

	ret = kstrtou32(buf, 0, &entry->bw);
	if (ret)
		return ret;

	ret = qos_enable_clk(entry);
	if (ret)
		return ret;
	if (entry->clk) {
		divider = entry->divider + 1;
		entry->clkfreqMhz =
			clk_get_rate(entry->clk) / 1000000 / divider;
	}
	BUG_ON(entry->clkfreqMhz == 0);

	bw = entry->bw * 256 / entry->clkfreqMhz;
	writel_relaxed(bw, &qos_reg->bw);

	qos_disable_clk(entry);

	return len;
}

static ssize_t qos_saturation_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	struct kobj_ext_attribute *ea = to_ext_attr(attr);
	struct noc_qos_t *entry = (struct noc_qos_t *)ea->var;
	struct qos_generator_register *qos_reg =
		(struct qos_generator_register *)(entry->nocm->mbase +
		entry->reg_offset);
	int pos = 0;
	int ret;

	ret = qos_enable_clk(entry);
	if (ret)
		return ret;

	entry->saturation = readl_relaxed(&qos_reg->saturation);

	pos += scnprintf(buf + pos,
		PAGE_SIZE - pos,
		"0x%x\n",
		entry->saturation);

	qos_disable_clk(entry);

	return pos;
}

static ssize_t qos_saturation_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t len)
{
	struct kobj_ext_attribute *ea = to_ext_attr(attr);
	struct noc_qos_t *entry = (struct noc_qos_t *)ea->var;
	struct qos_generator_register *qos_reg =
		(struct qos_generator_register *)(entry->nocm->mbase +
		entry->reg_offset);
	int ret;

	ret = kstrtou32(buf, 0, &entry->saturation);
	if (ret)
		return ret;

	ret = qos_enable_clk(entry);
	if (ret)
		return ret;

	writel_relaxed(entry->saturation, &qos_reg->saturation);

	qos_disable_clk(entry);

	return len;
}

static ssize_t qos_priority_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	struct kobj_ext_attribute *ea = to_ext_attr(attr);
	struct noc_qos_t *entry = (struct noc_qos_t *)ea->var;
	struct qos_generator_register *qos_reg =
		(struct qos_generator_register *)(entry->nocm->mbase +
		entry->reg_offset);
	int pos = 0;
	int ret;

	ret = qos_enable_clk(entry);
	if (ret)
		return ret;

	entry->priority = readl_relaxed(&qos_reg->priority);

	pos += scnprintf(buf + pos,
		PAGE_SIZE - pos,
		"0x%x\n",
		entry->priority);

	qos_disable_clk(entry);

	return pos;
}

static ssize_t qos_priority_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t len)
{
	struct kobj_ext_attribute *ea = to_ext_attr(attr);
	struct noc_qos_t *entry = (struct noc_qos_t *)ea->var;
	struct qos_generator_register *qos_reg =
		(struct qos_generator_register *)(entry->nocm->mbase +
		entry->reg_offset);
	int ret;

	ret = kstrtou32(buf, 0, &entry->priority);
	if (ret)
		return ret;

	ret = qos_enable_clk(entry);
	if (ret)
		return ret;

	writel_relaxed(entry->priority, &qos_reg->priority);

	qos_disable_clk(entry);

	return len;
}

static ssize_t qos_mode_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	struct kobj_ext_attribute *ea = to_ext_attr(attr);
	struct noc_qos_t *entry = (struct noc_qos_t *)ea->var;
	struct qos_generator_register *qos_reg =
		(struct qos_generator_register *)(entry->nocm->mbase +
		entry->reg_offset);
	int pos = 0;
	int ret;

	ret = qos_enable_clk(entry);
	if (ret)
		return ret;

	entry->mode = readl_relaxed(&qos_reg->mode);

	pos += scnprintf(buf + pos,
		PAGE_SIZE - pos,
		"%d\n",
		entry->mode);

	qos_disable_clk(entry);

	return pos;
}

static ssize_t qos_mode_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t len)
{
	struct kobj_ext_attribute *ea = to_ext_attr(attr);
	struct noc_qos_t *entry = (struct noc_qos_t *)ea->var;
	struct qos_generator_register *qos_reg =
		(struct qos_generator_register *)(entry->nocm->mbase +
		entry->reg_offset);
	int ret;

	ret = kstrtou32(buf, 0, &entry->mode);
	if (ret)
		return ret;

	ret = qos_enable_clk(entry);
	if (ret)
		return ret;

	writel_relaxed(entry->mode, &qos_reg->mode);

	qos_disable_clk(entry);

	return len;
}

int noc_qos_sysfs_init(struct noc_macro *nocm)
{
	struct platform_device *pdev = nocm->pdev;
	u32 i = 0, j = 0;
	int ret = 0;

	nocm->qos_kobj = kobject_create_and_add("qos", &pdev->dev.kobj);
	if (!nocm->qos_kobj)
		return -ENOMEM;

	nocm->all_qos_attr.attr.attr.name = "all_qos";
	nocm->all_qos_attr.attr.attr.mode = S_IRUGO;
	nocm->all_qos_attr.attr.show = all_qos_show;
	nocm->all_qos_attr.var = (void *)nocm;
	ret = sysfs_create_file(nocm->qos_kobj, &nocm->all_qos_attr.attr.attr);
	if (ret)
		return ret;

	for (i = 0; i < nocm->qos_size; i++) {
		struct noc_qos_t *entry = nocm->qos_tbl + i;

		j = 0;

		entry->qos_bw.attr.attr.name = "bw";
		entry->qos_bw.attr.attr.mode = (S_IWUSR | S_IRUGO);
		entry->qos_bw.attr.show = qos_bw_show;
		entry->qos_bw.attr.store = qos_bw_store;
		entry->qos_bw.var = (void *)entry;
		entry->qos_attrs[j++] = &entry->qos_bw.attr.attr;

		entry->qos_saturation.attr.attr.name = "saturation";
		entry->qos_saturation.attr.attr.mode = (S_IWUSR | S_IRUGO);
		entry->qos_saturation.attr.show = qos_saturation_show;
		entry->qos_saturation.attr.store = qos_saturation_store;
		entry->qos_saturation.var = (void *)entry;
		entry->qos_attrs[j++] = &entry->qos_saturation.attr.attr;

		entry->qos_priority.attr.attr.name = "priority";
		entry->qos_priority.attr.attr.mode = (S_IWUSR | S_IRUGO);
		entry->qos_priority.attr.show = qos_priority_show;
		entry->qos_priority.attr.store = qos_priority_store;
		entry->qos_priority.var = (void *)entry;
		entry->qos_attrs[j++] = &entry->qos_priority.attr.attr;

		entry->qos_mode.attr.attr.name = "mode";
		entry->qos_mode.attr.attr.mode = (S_IWUSR | S_IRUGO);
		entry->qos_mode.attr.show = qos_mode_show;
		entry->qos_mode.attr.store = qos_mode_store;
		entry->qos_mode.var = (void *)entry;
		entry->qos_attrs[j++] = &entry->qos_mode.attr.attr;

		entry->qos_attr_group.name = entry->name;
		entry->qos_attr_group.attrs = entry->qos_attrs;

		ret = sysfs_create_group(nocm->qos_kobj,
			&entry->qos_attr_group);
		if (ret)
			return ret;
	}

	return ret;
}

int noc_qos_init(struct noc_macro *nocm)
{
	struct platform_device *pdev = nocm->pdev;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *bnp, *pp;
	struct noc_qos_t *entry;
	u32 i = 0;
	int ret = 0;

	bnp = of_get_child_by_name(np, "qos");
	if (!bnp) {
		pr_debug("qos not found\n");
		return 0;
	}

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

		ret = of_property_read_u32(pp, "reg_offset",
			&entry->reg_offset);
		if (ret) {
			pr_err("of_property_read_u32 reg_offset failed\n");
			return ret;
		}

		ret = of_property_read_u32(pp, "enabled", &entry->enabled);
		if (ret) {
			pr_debug("of_property_read_u32 enabled failed\n");
			entry->enabled = 1;
		}

		ret = of_property_read_u32(pp, "bw", &entry->bw);
		if (ret)
			pr_debug("of_property_read_u32 bw failed\n");

		ret = of_property_read_u32(pp, "saturation",
			&entry->saturation);
		if (ret) {
			pr_debug("of_property_read_u32 saturation failed\n");
			entry->saturation = 0x40;
		}

		ret = of_property_read_u32(pp, "priority", &entry->priority);
		if (ret) {
			pr_debug("of_property_read_u32 saturation failed\n");
			entry->priority = DEF_PRIO;
		}

		ret = of_property_read_u32(pp, "mode", &entry->mode);
		if (ret) {
			pr_debug("of_property_read_u32 mode failed\n");
			entry->mode = DEF_MODE;
		}

		ret = of_property_read_string(pp, "clock_name",
			&entry->clock_name);
		if (ret)
			pr_debug("read_string clock_name failed\n");

		ret = of_property_read_u32(pp, "divider", &entry->divider);
		ret = of_property_read_u32(pp, "clkfreqMhz",
			&entry->clkfreqMhz);

		pr_debug("%s\t0x%x\t%d\t%d\t0x%x\t0x%x\t%d\t%s\t%d\n",
			entry->name, entry->reg_offset, entry->enabled,
			entry->bw, entry->saturation, entry->priority,
			entry->mode, entry->clock_name, entry->divider);
	}

	for (i = 0; i < nocm->qos_size; i++) {
		entry = nocm->qos_tbl + i;

		if (entry->clock_name && entry->clk == NULL) {
			entry->clk = devm_clk_get(&nocm->pdev->dev,
				entry->clock_name);
			if (IS_ERR(entry->clk)) {
				pr_err("%s: failed get clk of %s!\n",
					__func__, entry->clock_name);
				entry->clk = NULL;
				return -1;
			}
		}
	}

	ret = noc_qos_sysfs_init(nocm);
	if (ret)
		return ret;

	return qos_generator_init(nocm);
}
