/*
 * Define Soc display device for CSR SiRFprimaII
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <video/sirfsoc_vdss.h>

/* Board specific data */
static struct sirfsoc_vdss_board_info sirfsoc_vdss_data = {
	.default_display_name = "lvds",
};

static struct platform_device sirfsoc_vdss_device = {
	.name		= "sirfsoc_vdss",
	.id		= -1,
	.dev            = {
		.platform_data = &sirfsoc_vdss_data,
	},
};


static struct platform_device *sirfsoc_display_pdevs[] __initdata = {
	&sirfsoc_vdss_device,
};

int __init sirfsoc_add_display_pdev(void)
{
	return platform_add_devices(sirfsoc_display_pdevs,
		ARRAY_SIZE(sirfsoc_display_pdevs));
}
