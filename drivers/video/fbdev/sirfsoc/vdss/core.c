/*
 * linux/drivers/video/fbdev/sirfsoc/vdss/core.c
 *
 * Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
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

#define VDSS_SUBSYS_NAME "CORE"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/platform_device.h>
#include <linux/suspend.h>
#include <linux/device.h>

#include <video/sirfsoc_vdss.h>

#include "vdss.h"

static struct {
	struct platform_device *pdev;
} core;

static bool vdss_initialized;
static bool vdss_lvds_initialized;

bool sirfsoc_vdss_is_initialized(void)
{
	return vdss_initialized;
}
EXPORT_SYMBOL(sirfsoc_vdss_is_initialized);

bool sirfsoc_vdss_lvds_is_initialized(void)
{
	return vdss_lvds_initialized;
}
EXPORT_SYMBOL(sirfsoc_vdss_lvds_is_initialized);

struct platform_device *vdss_get_core_pdev(void)
{
	return core.pdev;
}

#if defined(CONFIG_DEBUG_FS)
static int vdss_debug_show(struct seq_file *s, void *data)
{
	void (*func)(struct seq_file *) = s->private;

	func(s);

	return 0;
}

static int vdss_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, vdss_debug_show, inode->i_private);
}

static const struct file_operations vdss_debug_fops = {
	.open           = vdss_debug_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static struct dentry *vdss_debugfs_dir;

static int vdss_init_debugfs(void)
{
	int err;

	vdss_debugfs_dir = debugfs_create_dir("sirfsoc_vdss", NULL);
	if (IS_ERR(vdss_debugfs_dir)) {
		err = PTR_ERR(vdss_debugfs_dir);
		vdss_debugfs_dir = NULL;
		return err;
	}

	return 0;
}

static void vdss_deinit_debugfs(void)
{
	debugfs_remove_recursive(vdss_debugfs_dir);
}

int vdss_debugfs_create_file(const char *name, void (*dump)(struct seq_file *))
{
	struct dentry *d;

	d = debugfs_create_file(name, S_IRUGO, vdss_debugfs_dir,
		dump, &vdss_debug_fops);

	return PTR_ERR_OR_ZERO(d);
}
#else
static inline int vdss_init_debugfs(void)
{
	return 0;
}
static inline void vdss_deinit_debugfs(void)
{
}
int vdss_debugfs_create_file(const char *name, void (*write)(struct seq_file *))
{
	return 0;
}
#endif

static int sirfsoc_vdss_probe(struct platform_device *pdev)
{
	int ret;

	core.pdev = pdev;

	ret = vdss_init_debugfs();
	if (ret)
		return ret;

	return 0;
}

static void sirfsoc_vdss_shutdown(struct platform_device *pdev)
{
	VDSSDBG("shutdown\n");
	vdss_disable_all_panels();
}

static int sirfsoc_vdss_remove(struct platform_device *pdev)
{
	vdss_deinit_debugfs();

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sirfsoc_vdss_suspend(struct device *dev)
{
	VDSSDBG("suspending displays\n");
	return vdss_suspend_all_panels();
}

static int sirfsoc_vdss_resume(struct device *dev)
{
	VDSSDBG("resuming displays\n");
	return vdss_resume_all_panels();
}

static SIMPLE_DEV_PM_OPS(sirf_vdss_core_pm_ops,
			 sirfsoc_vdss_suspend,
			 sirfsoc_vdss_resume);

#define SIRFVDSS_CORE_PM_OPS (&sirf_vdss_core_pm_ops)

#else

#define SIRFVDSS_CORE_PM_OPS NULL

#endif /* CONFIG_PM_SLEEP */

static struct platform_driver sirfsoc_vdss_driver = {
	.probe		= sirfsoc_vdss_probe,
	.remove         = sirfsoc_vdss_remove,
	.shutdown	= sirfsoc_vdss_shutdown,
	.driver         = {
		.name   = "sirfsoc_vdss",
		.owner  = THIS_MODULE,
		.pm	= SIRFVDSS_CORE_PM_OPS,
	},
};

static struct platform_device *sirfsoc_vdss_device;

static int __init sirfsoc_vdss_init(void)
{
	int ret;

	ret = platform_driver_register(&sirfsoc_vdss_driver);
	if (ret)
		return ret;

	sirfsoc_vdss_device = platform_device_alloc("sirfsoc_vdss", 0);
	if (!sirfsoc_vdss_device) {
		ret = -ENOMEM;
		goto err_alloc_dev;
	}
	ret = platform_device_add(sirfsoc_vdss_device);
	if (ret)
		goto err_add_dev;

	ret = lcdc_init_platform_driver();
	if (ret) {
		VDSSERR("Failed to initialize lcdc platform driver\n");
		goto err_lcdc;
	}

	ret = vpp_init_platform_driver();
	if (ret) {
		VDSSERR("Failed to initialize vpp platform driver\n");
		goto err_vpp;
	}

	ret = lvdsc_init_platform_driver();
	if (ret) {
		VDSSDBG("Failed to initialize lvdsc platform driver\n");
		vdss_lvds_initialized = false;
	} else
		vdss_lvds_initialized = true;

	ret = dcu_init_platform_driver();
	if (ret) {
		VDSSERR("Failed to initialize dcu platform driver\n");
		goto err_dcu;
	}

	vdss_initialized = true;

	return 0;

err_dcu:
	lvdsc_uninit_platform_driver();
	vpp_uninit_platform_driver();

err_vpp:
	lcdc_uninit_platform_driver();

err_lcdc:
	platform_device_del(sirfsoc_vdss_device);
err_add_dev:
	platform_device_put(sirfsoc_vdss_device);
err_alloc_dev:
	platform_driver_unregister(&sirfsoc_vdss_driver);

	return ret;
}

static void __exit sirfsoc_vdss_exit(void)
{
	platform_device_unregister(sirfsoc_vdss_device);
	platform_driver_unregister(&sirfsoc_vdss_driver);
}

subsys_initcall(sirfsoc_vdss_init);
module_exit(sirfsoc_vdss_exit);

MODULE_DESCRIPTION("SIRF Soc Video Display Subsystem");
MODULE_LICENSE("GPL v2");
