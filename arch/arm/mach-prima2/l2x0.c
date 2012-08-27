/*
 * l2 cache initialization for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <asm/hardware/cache-l2x0.h>

/*
 * fixme: make aux_val and aux_mask the private data
 * of of_device_id after we get right aux_val/mask
 */
static struct of_device_id prima2_l2x0_ids[]  = {
	{ .compatible = "sirf,prima2-pl310-cache" },
	{},
};

static struct of_device_id marco_l2x0_ids[]  = {
	{ .compatible = "sirf,marco-pl310-cache" },
	{},
};

static int __init sirfsoc_l2x0_init(void)
{
	struct device_node *np;
	np = of_find_matching_node(NULL, prima2_l2x0_ids);
	if (np) {
		pr_info("Initializing prima2 L2 cache\n");
		return l2x0_of_init(0x40000, 0);
	}

	np = of_find_matching_node(NULL, marco_l2x0_ids);
	if (np) {
		pr_info("Initializing marco L2 cache\n");
		/*
		 * fixme: set the right aux_val and aux_mask
		 * return l2x0_of_init(0x10000, 0);
		 */
	}

	return 0;
}
early_initcall(sirfsoc_l2x0_init);
