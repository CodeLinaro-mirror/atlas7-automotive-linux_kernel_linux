/*
 * CSR sirfsoc vdss core file
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc
 * group company.
 * Licensed under GPLv2 or later.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>

#include <video/sirfsoc_vdss.h>

#include "vdss.h"

static ssize_t layer_enable_show(struct sirfsoc_vdss_layer *l,
	char *buf)
{
	bool e = l->is_enabled(l);

	return snprintf(buf, PAGE_SIZE, "%d\n", e);
}

static ssize_t layer_enable_store(struct sirfsoc_vdss_layer *l,
	const char *buf, size_t size)
{
	int r;
	bool e;

	r = strtobool(buf, &e);
	if (r)
		return r;

	if (e)
		r = l->enable(l);
	else
		r = l->disable(l);

	if (r)
		return r;

	return size;
}

struct layer_attribute {
	struct attribute attr;
	ssize_t (*show)(struct sirfsoc_vdss_layer *, char *);
	ssize_t (*store)(struct sirfsoc_vdss_layer *, const char *, size_t);
};

#define LAYER_ATTR(_name, _mode, _show, _store) \
	struct layer_attribute layer_attr_##_name = \
	__ATTR(_name, _mode, _show, _store)

static LAYER_ATTR(layer_enable, S_IRUGO|S_IWUSR,
	layer_enable_show, layer_enable_store);

static struct attribute *layer_sysfs_attrs[] = {
	&layer_attr_layer_enable.attr,
	NULL
};

static ssize_t layer_attr_show(struct kobject *kobj, struct attribute *attr,
		char *buf)
{
	struct sirfsoc_vdss_layer *l;
	struct layer_attribute *layer_attr;

	l = container_of(kobj, struct sirfsoc_vdss_layer, kobj);
	layer_attr = container_of(attr, struct layer_attribute, attr);

	if (!layer_attr->show)
		return -ENOENT;

	return layer_attr->show(l, buf);
}

static ssize_t layer_attr_store(struct kobject *kobj, struct attribute *attr,
		const char *buf, size_t size)
{
	struct sirfsoc_vdss_layer *l;
	struct layer_attribute *layer_attr;

	l = container_of(kobj, struct sirfsoc_vdss_layer, kobj);
	layer_attr = container_of(attr, struct layer_attribute, attr);

	if (!layer_attr->store)
		return -ENOENT;

	return layer_attr->store(l, buf, size);
}

static const struct sysfs_ops layer_sysfs_ops = {
	.show = layer_attr_show,
	.store = layer_attr_store,
};

static struct kobj_type layer_ktype = {
	.sysfs_ops = &layer_sysfs_ops,
	.default_attrs = layer_sysfs_attrs,
};

int vdss_init_layers_sysfs(u32 lcdc_index)
{
	int i;
	int r;
	int num_layer = sirfsoc_vdss_get_num_layers(lcdc_index);
	struct platform_device *pdev = vdss_get_core_pdev();

	for (i = 0; i < num_layer; ++i) {
		struct sirfsoc_vdss_layer *l =
			sirfsoc_vdss_get_layer(lcdc_index, i);

		r = kobject_init_and_add(&l->kobj, &layer_ktype,
				&pdev->dev.kobj, "lcd%d-%s",
				lcdc_index, l->name);

		if (r) {
			VDSSERR("failed to create layer sysfs files\n");
			goto err;
		}
	}

	return 0;

err:
	vdss_uninit_layers_sysfs(lcdc_index);

	return r;
}

void vdss_uninit_layers_sysfs(u32 lcdc_index)
{
	int i;
	const int num_layer = sirfsoc_vdss_get_num_layers(lcdc_index);

	for (i = 0; i < num_layer; ++i) {
		struct sirfsoc_vdss_layer *l =
			sirfsoc_vdss_get_layer(lcdc_index, i);

		kobject_del(&l->kobj);
		kobject_put(&l->kobj);
	}
}


