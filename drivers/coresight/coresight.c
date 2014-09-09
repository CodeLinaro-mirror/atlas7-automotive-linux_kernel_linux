/* Copyright (c) 2012, The Linux Foundation. All rights reserved.
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

#define pr_fmt(fmt) "coresight: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/semaphore.h>
#include <linux/clk.h>
#include <linux/coresight.h>
#include <linux/of_platform.h>
#include <linux/debugfs.h>
#include <linux/delay.h>

#include "coresight-priv.h"

struct dentry *cs_debugfs_parent = NULL;

static LIST_HEAD(coresight_orph_conns);
static LIST_HEAD(coresight_devs);
static DEFINE_SEMAPHORE(coresight_semaphore);

static int coresight_source_is_unique(struct coresight_device *csdev)
{
	struct coresight_device *cd;
	int trace_id = source_ops(csdev)->trace_id(csdev);

	/* this shouldn't happen */
	if (trace_id < 0)
		return 0;

	/* circle through all known components looking for sources */
	list_for_each_entry(cd, &coresight_devs, dev_link) {
		/* no need to care about oneself and components that are not
		 * sources or not enabled
		 */
		if (cd == csdev || !cd->enable ||
		    cd->type != CORESIGHT_DEV_TYPE_SOURCE)
			continue;

		/* all you need is one */
		if (trace_id == source_ops(cd)->trace_id(cd))
			return 0;
	}

	return 1;
}

static int coresight_find_link_inport(struct coresight_device *csdev)
{
	int i;
	struct coresight_device *parent;
	struct coresight_connection *conn;

	parent = container_of(csdev->path_link.next, struct coresight_device,
			     path_link);
	for (i = 0; i < parent->nr_conns; i++) {
		conn = &parent->conns[i];
		if (conn->child_dev == csdev)
			return conn->child_port;
	}

	pr_err("couldn't find inport, parent: %d, child: %d\n",
	       parent->id, csdev->id);
	return 0;
}

static int coresight_find_link_outport(struct coresight_device *csdev)
{
	int i;
	struct coresight_device *child;
	struct coresight_connection *conn;

	child = container_of(csdev->path_link.prev, struct coresight_device,
			      path_link);
	for (i = 0; i < csdev->nr_conns; i++) {
		conn = &csdev->conns[i];
		if (conn->child_dev == child)
			return conn->outport;
	}

	pr_err("couldn't find outport, parent: %d, child: %d\n",
	       csdev->id, child->id);
	return 0;
}

static int coresight_enable_sink(struct coresight_device *csdev)
{
	int ret;

	if (!csdev->enable) {
		if (sink_ops(csdev)->enable) {
			ret = sink_ops(csdev)->enable(csdev);
			if (ret)
				return ret;
		}
		kref_init(&csdev->kref);
		csdev->enable = true;
		return 0;
	}

	kref_get(&csdev->kref);

	return 0;
}

static void coresight_driver_disable_sink(struct kref *kref)
{
	struct coresight_device *csdev = container_of(kref,
					struct coresight_device, kref);

	if (sink_ops(csdev)->disable) {
		sink_ops(csdev)->disable(csdev);
		csdev->enable = false;
	}
}

static void coresight_disable_sink(struct coresight_device *csdev)
{
	kref_put(&csdev->kref, coresight_driver_disable_sink);
}

static int coresight_enable_link(struct coresight_device *csdev)
{
	int ret;
	int inport, outport;

	inport = coresight_find_link_inport(csdev);
	outport = coresight_find_link_outport(csdev);

	if (link_ops(csdev)->enable) {
		ret = link_ops(csdev)->enable(csdev, inport, outport);
		if (ret)
			return ret;
	}

	if (!csdev->enable) {
		csdev->enable = true;
		kref_init(&csdev->kref);
	} else {
		kref_get(&csdev->kref);
	}

	return 0;
}

static void coresight_driver_disable_link(struct kref *kref)
{
	struct coresight_device *csdev = container_of(kref,
					struct coresight_device, kref);
	csdev->enable = false;
}

static void coresight_disable_link(struct coresight_device *csdev)
{
	int inport, outport;

	inport = coresight_find_link_inport(csdev);
	outport = coresight_find_link_outport(csdev);

	if (link_ops(csdev)->disable)
		link_ops(csdev)->disable(csdev, inport, outport);

	kref_put(&csdev->kref, coresight_driver_disable_link);
}

static int coresight_enable_source(struct coresight_device *csdev)
{
	int ret;

	if (!coresight_source_is_unique(csdev)) {
		pr_warn("traceID %d not unique\n",
			source_ops(csdev)->trace_id(csdev));
		return -EINVAL;
	}

	if (!csdev->enable) {
		if (source_ops(csdev)->enable) {
			ret = source_ops(csdev)->enable(csdev);
			if (ret)
				return ret;
		}
		kref_init(&csdev->kref);
		csdev->enable = true;
		return 0;
	}

	kref_get(&csdev->kref);

	return 0;
}

static void coresight_driver_disable_source(struct kref *kref)
{
	struct coresight_device *csdev = container_of(kref,
					struct coresight_device, kref);

	if (source_ops(csdev)->disable) {
		source_ops(csdev)->disable(csdev);
		csdev->enable = false;
	}
}

static void coresight_disable_source(struct coresight_device *csdev)
{
	kref_put(&csdev->kref, coresight_driver_disable_source);
}

static int coresight_enable_path(struct list_head *path)
{
	int ret = 0;
	struct coresight_device *cd;

	list_for_each_entry(cd, path, path_link) {
		if (cd == list_first_entry(path, struct coresight_device,
					   path_link)) {
			ret = coresight_enable_sink(cd);
		} else if (list_is_last(&cd->path_link, path)) {
			/* dont' enable the source just yet - this needs to
			 * happen at the very end when all links and sink
			 * along the path have been configured properly.
			 */
			;
		} else {
			ret = coresight_enable_link(cd);
		}
		if (ret)
			goto err;
	}
	return 0;
err:
	list_for_each_entry_continue_reverse(cd, path, path_link) {
		if (cd == list_first_entry(path, struct coresight_device,
					   path_link)) {
			coresight_disable_sink(cd);
		} else if (list_is_last(&cd->path_link, path)) {
			;
		} else {
			coresight_disable_link(cd);
		}
	}
	return ret;
}

static int coresight_disable_path(struct list_head *path)
{
	struct coresight_device *cd;

	list_for_each_entry_reverse(cd, path, path_link) {
		if (cd == list_first_entry(path, struct coresight_device,
					   path_link)) {
			coresight_disable_sink(cd);
		} else if (list_is_last(&cd->path_link, path)) {
			/* the source has already been stopped, no need
			 * to do it again here.
			 */
			;
		} else {
			coresight_disable_link(cd);
		}
	}

	return 0;
}

static int coresight_build_paths(struct coresight_device *csdev,
				 struct list_head *path,
				 bool enable)
{
	int i, ret = -EINVAL;
	struct coresight_connection *conn;

	list_add(&csdev->path_link, path);

	if (csdev->type == CORESIGHT_DEV_TYPE_SINK && csdev->activated) {
		if (enable)
			ret = coresight_enable_path(path);
		else
			ret = coresight_disable_path(path);
	} else {
		for (i = 0; i < csdev->nr_conns; i++) {
			conn = &csdev->conns[i];
			if (coresight_build_paths(conn->child_dev,
						    path, enable) == 0)
				ret = 0;
		}
	}

	if (list_first_entry(path, struct coresight_device, path_link) != csdev)
		pr_err("wrong device in %s\n", __func__);

	list_del(&csdev->path_link);
	return ret;
}

int coresight_enable(struct coresight_device *csdev)
{
	int ret = 0;
	LIST_HEAD(path);

	WARN_ON(IS_ERR_OR_NULL(csdev));

	down(&coresight_semaphore);
	if (csdev->type != CORESIGHT_DEV_TYPE_SOURCE) {
		ret = -EINVAL;
		pr_err("wrong device type in %s\n", __func__);
		goto out;
	}
	if (csdev->enable)
		goto out;

	if (coresight_build_paths(csdev, &path, true)) {
		pr_err("building path(s) failed\n");
		goto out;
	}

	if (coresight_enable_source(csdev))
		pr_err("source enable failed\n");
out:
	up(&coresight_semaphore);
	return ret;
}
EXPORT_SYMBOL_GPL(coresight_enable);

void coresight_disable(struct coresight_device *csdev)
{
	LIST_HEAD(path);

	WARN_ON(IS_ERR_OR_NULL(csdev));

	down(&coresight_semaphore);
	if (csdev->type != CORESIGHT_DEV_TYPE_SOURCE) {
		pr_err("wrong device type in %s\n", __func__);
		goto out;
	}
	if (!csdev->enable)
		goto out;

	coresight_disable_source(csdev);
	if (coresight_build_paths(csdev, &path, false))
		pr_err("releasing path(s) failed\n");

out:
	up(&coresight_semaphore);
}
EXPORT_SYMBOL_GPL(coresight_disable);

static ssize_t debugfs_active_get(void *data, u64 *val)
{
	struct coresight_device *csdev = data;

	*val = csdev->activated;
	return 0;
}

static ssize_t debugfs_active_set(void *data, u64 val)
{
	struct coresight_device *csdev = data;

	csdev->activated = !!val;
	return 0;
}
CORESIGHT_DEBUGFS_ENTRY(debugfs_active, "enable",
			S_IRUGO | S_IWUSR, debugfs_active_get,
			debugfs_active_set, "%llx\n");

static ssize_t debugfs_enable_get(void *data, u64 *val)
{
	struct coresight_device *csdev = data;

	*val = csdev->enable;
	return 0;
}

static ssize_t debugfs_enable_set(void *data, u64 val)
{
	struct coresight_device *csdev = data;

	if (val)
		return coresight_enable(csdev);

	coresight_disable(csdev);
	return 0;
}
CORESIGHT_DEBUGFS_ENTRY(debugfs_enable, "enable",
			S_IRUGO | S_IWUSR, debugfs_enable_get,
			debugfs_enable_set, "%llx\n");


static const struct coresight_ops_entry *coresight_grps_sink[] = {
	&debugfs_active_entry,
	NULL,
};

static const struct coresight_ops_entry *coresight_grps_source[] = {
	&debugfs_enable_entry,
	NULL,
};

struct coresight_group_entries {
	const char *name;
	const struct coresight_ops_entry **entries;
};

struct coresight_group_entries coresight_debugfs_entries[] = {
	{
		.name = "none",
	},
	{
		.name = "sink",
		.entries = coresight_grps_sink,
	},
	{
		.name = "link",
	},
	{
		.name = "linksink",
	},
	{
		.name = "source",
		.entries = coresight_grps_source,
	},
};

static void coresight_device_release(struct device *dev)
{
	struct coresight_device *csdev = to_coresight_device(dev);

	kfree(csdev);
}

static void coresight_fixup_orphan_conns(struct coresight_device *csdev)
{
	struct coresight_connection *conn, *temp;

	list_for_each_entry_safe(conn, temp, &coresight_orph_conns, link) {
		if (conn->child_id == csdev->id) {
			conn->child_dev = csdev;
			list_del(&conn->link);
		}
	}
}

static void coresight_fixup_device_conns(struct coresight_device *csdev)
{
	int i;
	struct coresight_device *cd;
	bool found;

	for (i = 0; i < csdev->nr_conns; i++) {
		found = false;
		list_for_each_entry(cd, &coresight_devs, dev_link) {
			if (csdev->conns[i].child_id == cd->id) {
				csdev->conns[i].child_dev = cd;
				found = true;
				break;
			}
		}
		if (!found)
			list_add_tail(&csdev->conns[i].link,
				      &coresight_orph_conns);
	}
}

static int debugfs_coresight_init(void)
{
	if (!cs_debugfs_parent) {
		cs_debugfs_parent = debugfs_create_dir("coresight", 0);
		if (IS_ERR(cs_debugfs_parent))
			return PTR_ERR(cs_debugfs_parent);
	}

	return 0;
}

static struct dentry *coresight_debugfs_desc_init(
				struct coresight_device *csdev,
				const struct coresight_ops_entry **debugfs_ops)
{
	int i = 0;
	struct dentry *parent;
	struct device *dev = &csdev->dev;
	const struct coresight_ops_entry *ops_entry, **ops_entries;

	parent = debugfs_create_dir(dev_name(dev), cs_debugfs_parent);
	if (IS_ERR(parent))
		return NULL;

	/* device-specific ops */
	while (debugfs_ops && debugfs_ops[i]) {
		ops_entry = debugfs_ops[i];
		if (!debugfs_create_file(ops_entry->name, ops_entry->mode,
					 parent, dev_get_drvdata(dev->parent),
					 ops_entry->ops)) {
			debugfs_remove_recursive(parent);
			return NULL;
		}
		i++;
	}

	/* group-specific ops */
	i = 0;
	ops_entries = coresight_debugfs_entries[csdev->type].entries;

	while (ops_entries && ops_entries[i]) {
		if (!debugfs_create_file(ops_entries[i]->name,
					 ops_entries[i]->mode,
					 parent, csdev, ops_entries[i]->ops)) {
			debugfs_remove_recursive(parent);
			return NULL;
		}
		i++;
	}

	return parent;
}

/**
 * coresight_timeout - loop until a bit has changed to a specific state.
 * @addr: base address of the area of interest.
 * @offset: address of a register, starting from @addr.
 * @position: the position of the bit of interest.
 * @value: the value the bit should have.
 *
 * Returns as soon as the bit has taken the desired state or TIMEOU_US has
 * elapsed, which ever happens first.
 */

void coresight_timeout(void __iomem *addr, u32 offset, int position, int value)
{
	int i;
	u32 val;

	for (i = TIMEOUT_US; i > 0; i--) {
		val = __raw_readl(addr + offset);
		/* waiting on the bit to go from 0 to 1 */
		if (value) {
			if (val & BIT(position))
				return;
		/* waiting on the bit to go from 1 to 0 */
		} else {
			if (!(val & BIT(position)))
				return;
		}

		/* The specification doesn't say how long we are expected
		 * to wait.
		 */
		udelay(1);
	}

	WARN(1,
	     "coresight: timeout observed when proving at offset %#x\n",
	     offset);
}

struct coresight_device *coresight_register(struct coresight_desc *desc)
{
	int i;
	int ret;
	int link_subtype;
	struct coresight_device *csdev;
	struct coresight_connection *conns;

	WARN_ON(IS_ERR_OR_NULL(desc));

	csdev = kzalloc(sizeof(*csdev), GFP_KERNEL);
	if (!csdev) {
		ret = -ENOMEM;
		goto err_kzalloc_csdev;
	}

	csdev->id = desc->pdata->id;

	if (desc->type == CORESIGHT_DEV_TYPE_LINK ||
	    desc->type == CORESIGHT_DEV_TYPE_LINKSINK)
		link_subtype = desc->subtype.link_subtype;

	csdev->nr_conns = desc->pdata->nr_outports;
	conns = kcalloc(csdev->nr_conns, sizeof(*conns), GFP_KERNEL);
	if (!conns) {
		ret = -ENOMEM;
		goto err_kzalloc_conns;
	}

	for (i = 0; i < csdev->nr_conns; i++) {
		conns[i].outport = desc->pdata->outports[i];
		conns[i].child_id = desc->pdata->child_ids[i];
		conns[i].child_port = desc->pdata->child_ports[i];
	}
	csdev->conns = conns;

	csdev->type = desc->type;
	csdev->subtype = desc->subtype;
	csdev->ops = desc->ops;
	csdev->owner = desc->owner;

	csdev->dev.parent = desc->dev;
	csdev->dev.release = coresight_device_release;
	dev_set_name(&csdev->dev, "%s", desc->pdata->name);


	down(&coresight_semaphore);

	coresight_fixup_device_conns(csdev);

	ret = debugfs_coresight_init();
	if (ret < 0)
		goto err_coresight_init;

	csdev->de = coresight_debugfs_desc_init(csdev, desc->debugfs_ops);

	coresight_fixup_orphan_conns(csdev);

	list_add_tail(&csdev->dev_link, &coresight_devs);
	up(&coresight_semaphore);

	return csdev;

err_coresight_init:
	up(&coresight_semaphore);
	kfree(conns);
err_kzalloc_conns:
	kfree(csdev);
err_kzalloc_csdev:
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(coresight_register);

void coresight_unregister(struct coresight_device *csdev)
{
	WARN_ON(IS_ERR_OR_NULL(csdev));

	down(&coresight_semaphore);

	list_del(&csdev->dev_link);
	debugfs_remove_recursive(csdev->de);
	kfree(csdev->conns);
	if (list_empty(&coresight_devs))
		kfree(cs_debugfs_parent);

	up(&coresight_semaphore);
}
EXPORT_SYMBOL_GPL(coresight_unregister);

MODULE_LICENSE("GPL v2");
