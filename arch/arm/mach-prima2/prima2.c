/*
 * Defines machines for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/sizes.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/machine.h>
#include "common.h"

/* Padmux settings */
static struct pinctrl_map prima2_padmux_map[] = {
	PIN_MAP_MUX_GROUP_DEFAULT("UART1", "b0120000.gpio-controller", "uart1", "b0060000.uart"),
	PIN_MAP_MUX_GROUP_DEFAULT("SPI0", "b0120000.gpio-controller", "spi0", "b00d0000.spi"),
	PIN_MAP_MUX_GROUP_DEFAULT("SPI1", "b0120000.gpio-controller", "spi1", "b0170000.spi"),
};

static struct of_device_id sirfsoc_of_bus_ids[] __initdata = {
	{ .compatible = "simple-bus", },
	{},
};

void __init sirfsoc_mach_init(void)
{
	of_platform_bus_probe(NULL, sirfsoc_of_bus_ids, NULL);
	pinctrl_register_mappings(prima2_padmux_map, ARRAY_SIZE(prima2_padmux_map));
}

void __init sirfsoc_init_late(void)
{
	sirfsoc_pm_init();
}

static const char *prima2cb_dt_match[] __initdata = {
       "sirf,prima2-cb",
       NULL
};

MACHINE_START(PRIMA2_EVB, "prima2cb")
	/* Maintainer: Barry Song <baohua.song@csr.com> */
	.atag_offset	= 0x100,
	.init_early     = sirfsoc_of_clk_init,
	.map_io         = sirfsoc_map_lluart,
	.init_irq	= sirfsoc_of_irq_init,
	.timer		= &sirfsoc_timer,
	.dma_zone_size	= SZ_256M,
	.init_machine	= sirfsoc_mach_init,
	.init_late	= sirfsoc_init_late,
	.dt_compat      = prima2cb_dt_match,
	.restart	= sirfsoc_restart,
MACHINE_END
