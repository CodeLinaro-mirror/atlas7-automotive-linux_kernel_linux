/*
 * linux/drivers/video/fbdev/sirfsoc/vdss/core.c
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc
 * group company.
 *
 * Licensed under GPLv2 or later.
 */
#define VDSS_SUBSYS_NAME "CORE"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/suspend.h>
#include <linux/device.h>

#include <video/sirfsoc_vdss.h>

#include "vdss.h"

static struct {
	struct platform_device *pdev;
	const char *default_display_name;
} core;

static char *def_disp_name;
module_param_named(def_disp, def_disp_name, charp, 0);
MODULE_PARM_DESC(def_disp, "default display name");

static bool vdss_initialized;

const char *sirfsoc_vdss_get_default_panel_name(void)
{
	return core.default_display_name;
}
EXPORT_SYMBOL(sirfsoc_vdss_get_default_panel_name);

bool sirfsoc_vdss_is_initialized(void)
{
	return vdss_initialized;
}
EXPORT_SYMBOL(sirfsoc_vdss_is_initialized);

static int sirfsoc_vdss_pm_notif(struct notifier_block *b,
	unsigned long v, void *d)
{
	VDSSDBG("pm notif %lu\n", v);

	switch (v) {
	case PM_SUSPEND_PREPARE:
		VDSSDBG("suspending displays\n");
		return vdss_suspend_all_panels();

	case PM_POST_SUSPEND:
		VDSSDBG("resuming displays\n");
		return vdss_resume_all_panels();

	default:
		return 0;
	}
}

static struct notifier_block sirfsoc_vdss_pm_notif_block = {
	.notifier_call = sirfsoc_vdss_pm_notif,
};

static int __init sirfsoc_vdss_probe(struct platform_device *pdev)
{
	struct sirfsoc_vdss_board_info *pdata = pdev->dev.platform_data;

	core.pdev = pdev;

	if (def_disp_name)
		core.default_display_name = def_disp_name;
	else if (pdata->default_display_name)
		core.default_display_name = pdata->default_display_name;

	register_pm_notifier(&sirfsoc_vdss_pm_notif_block);

	return 0;
}

static void sirfsoc_vdss_shutdown(struct platform_device *pdev)
{
	VDSSDBG("shutdown\n");
	vdss_disable_all_panels();
}

static int sirfsoc_vdss_remove(struct platform_device *pdev)
{
	unregister_pm_notifier(&sirfsoc_vdss_pm_notif_block);

	return 0;
}

static struct platform_driver sirfsoc_vdss_driver = {
	.remove         = sirfsoc_vdss_remove,
	.shutdown	= sirfsoc_vdss_shutdown,
	.driver         = {
		.name   = "sirfsoc_vdss",
		.owner  = THIS_MODULE,
	},
};

static int __init sirfsoc_vdss_init(void)
{
	int ret;

	ret = platform_driver_probe(&sirfsoc_vdss_driver, sirfsoc_vdss_probe);
	if (ret)
		return ret;

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

	vdss_initialized = true;

	return 0;
err_vpp:
	lcdc_uninit_platform_driver();

err_lcdc:
	platform_driver_unregister(&sirfsoc_vdss_driver);

	return 0;
}

static void __exit sirfsoc_vdss_exit(void)
{
	platform_driver_unregister(&sirfsoc_vdss_driver);
}

subsys_initcall(sirfsoc_vdss_init);
module_exit(sirfsoc_vdss_exit);

MODULE_AUTHOR("Jiansong Chen <Jiansong.Chen@csr.com>");
MODULE_DESCRIPTION("SIRF Soc Video Display Subsystem");
MODULE_LICENSE("GPL v2");
