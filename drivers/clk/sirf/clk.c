/*
 * iomem mapping and initilization for SiRF SoC clock tree
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/syscore_ops.h>
#include "clk.h"

void *sirfsoc_clk_vbase, *sirfsoc_rsc_vbase;

struct clk_onecell_data clk_data;

static const struct of_device_id sirfsoc_clkc_ids[] = {
	{ .compatible = "sirf,prima2-clkc", .data = prima2_clk_init, },
	{ .compatible = "sirf,atlas6-clkc", .data = atlas6_clk_init, },
	{},
};

static const struct of_device_id rsc_ids[] = {
	{ .compatible = "sirf,prima2-rsc" },
	{},
};

void __init sirfsoc_of_clk_init(void)
{
	struct device_node *np;
	void (*clk_init)(void);

	np = of_find_matching_node(NULL, rsc_ids);
	if (!np)
		panic("unable to find compatible rsc node in dtb\n");

	sirfsoc_rsc_vbase = of_iomap(np, 0);
	if (!sirfsoc_rsc_vbase)
		panic("unable to map rsc registers\n");

	of_node_put(np);

	np = of_find_matching_node(NULL, sirfsoc_clkc_ids);
	if (!np)
		return;

	sirfsoc_clk_vbase = of_iomap(np, 0);
	if (!sirfsoc_clk_vbase)
		panic("unable to map clkc registers\n");

	clk_init = (void (*)(void))of_match_node(sirfsoc_clkc_ids, np)->data;
	clk_init();

	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);
}
