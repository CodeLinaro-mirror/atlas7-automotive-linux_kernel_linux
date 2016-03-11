/*
 * Copyright (c) 2013, 2016, The Linux Foundation. All rights reserved.
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

#include <linux/io.h>
#include <linux/clk.h>
#include <linux/init.h>
#include <linux/sizes.h>
#include <linux/of_fdt.h>
#include <linux/kernel.h>
#include <linux/bootmem.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>

#include <asm/prom.h>
#include "sharedparam.h"
#include "Cop.h"



void __init device_tree_init(void)
{
	unsigned long base, size;

	if (!initial_boot_params)
		return;

	base = virt_to_phys((void *)initial_boot_params);
	size = be32_to_cpu(initial_boot_params->totalsize);

	/* Before we do anything, lets reserve the dt blob */
	reserve_bootmem(base, size, BOOTMEM_DEFAULT);

	unflatten_device_tree();
	coach_early_console_setup();
}

void __init plat_mem_setup(void)
{
	set_io_port_base(KSEG1);

	/*
	 * Load the builtin devicetree. This causes the chosen node to be
	 * parsed resulting in our memory appearing
	 */
	__dt_setup_arch(&__dtb_start);
	of_scan_flat_dt(early_init_dt_scan_chosen, arcs_cmdline);

}

static int __init plat_of_setup(void)
{
	static struct of_device_id of_ids[3];
	int len = sizeof(of_ids[0].compatible);

	if (!of_have_populated_dt())
		panic("device tree not present");

	strlcpy(of_ids[0].compatible, "csr,coach14-soc", len);
	strncpy(of_ids[1].compatible, "cbus", len);

	if (of_platform_populate(NULL, of_ids, NULL, NULL))
		panic("failed to populate DT\n");

	return 0;
}

arch_initcall(plat_of_setup);
