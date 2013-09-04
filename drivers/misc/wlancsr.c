/*
* CSR Synergy for Linux WLAN RFKill GPIO Device Register Driver
*
* Copyright (c) 2013 Cambridge Silicon Radio Limited, a CSR plc group company.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/rfkill.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/rfkill-gpio.h>

static struct rfkill_gpio_platform_data wlan_rfkill_platform_data = {
	.name	= "csrwlan-6030",
	.type	= RFKILL_TYPE_WLAN,
};

static struct platform_device wlan_rfkill_device = {
	.name	= "rfkill_gpio",
	.id		= -1,
	.dev	= {
			.platform_data = &wlan_rfkill_platform_data,
		},
};

static int wlan_csr_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;

	wlan_rfkill_platform_data.shutdown_gpio =  of_get_named_gpio(dn,
			"wlan_gpio_power", 0);

	return platform_device_register(&wlan_rfkill_device);
}

static int wlan_csr_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id wlan_csr_of_match[] = {
	{.compatible = "csr,wlan-6030", },
	{ },
};
MODULE_DEVICE_TABLE(of, wlan_csr_of_match);

static struct platform_driver wlan_csr_driver = {
	.driver = {
		.name = "wlan-csr-rfkill",
		.owner = THIS_MODULE,
		.of_match_table = wlan_csr_of_match,
	},
	.probe = wlan_csr_probe,
	.remove = wlan_csr_remove,
};

module_platform_driver(wlan_csr_driver);

MODULE_AUTHOR("Xingmin Guo <xingmin.guo@csr.com>");
MODULE_DESCRIPTION("CSR Synergy for Linux WLAN RFKill GPIO Device Register Driver");
MODULE_LICENSE("GPL");
