/*
 * reset controller for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/reset-controller.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>
#include "pm.h"

#define SIRFSOC_RSTBIT_NUM	64

void __iomem *sirfsoc_rstc_base;
static u32 sirfsoc_pwrc_base;
static DEFINE_MUTEX(rstc_lock);

static int sirfsoc_reset_module(struct reset_controller_dev *rcdev,
					unsigned long sw_reset_idx)
{
	u32 reset_bit = sw_reset_idx;

	if (reset_bit >= SIRFSOC_RSTBIT_NUM)
		return -EINVAL;

	mutex_lock(&rstc_lock);

	if (of_device_is_compatible(rcdev->of_node, "sirf,prima2-rstc")) {
		/*
		 * Writing 1 to this bit resets corresponding block. Writing 0 to this
		 * bit de-asserts reset signal of the corresponding block.
		 * datasheet doesn't require explicit delay between the set and clear
		 * of reset bit. it could be shorter if tests pass.
		 */
		writel(readl(sirfsoc_rstc_base + (reset_bit / 32) * 4) | (1 << reset_bit),
			sirfsoc_rstc_base + (reset_bit / 32) * 4);
		msleep(10);
		writel(readl(sirfsoc_rstc_base + (reset_bit / 32) * 4) & (~(1 << reset_bit)),
			sirfsoc_rstc_base + (reset_bit / 32) * 4);
	} else {
		/*
		 * For MARCO and POLO
		 * Writing 1 to SET register resets corresponding block. Writing 1 to CLEAR
		 * register de-asserts reset signal of the corresponding block.
		 * datasheet doesn't require explicit delay between the set and clear
		 * of reset bit. it could be shorter if tests pass.
		 */
		writel(reset_bit, sirfsoc_rstc_base + (reset_bit / 32) * 8);
		msleep(10);
		writel(reset_bit, sirfsoc_rstc_base + (reset_bit / 32) * 8 + 4);
	}

	mutex_unlock(&rstc_lock);

	return 0;
}

static struct reset_control_ops sirfsoc_rstc_ops = {
	.reset = sirfsoc_reset_module,
};

static struct reset_controller_dev sirfsoc_reset_controller = {
	.ops = &sirfsoc_rstc_ops,
	.nr_resets = SIRFSOC_RSTBIT_NUM,
};

static struct of_device_id rstc_ids[]  = {
	{ .compatible = "sirf,prima2-rstc" },
	{ .compatible = "sirf,marco-rstc" },
	{},
};

void __init sirfsoc_of_rstc_init(void)
{
	struct device_node *np = of_find_matching_node(NULL, rstc_ids);
	if (!np)
		panic("unable to find compatible rstc node in dtb\n");

	sirfsoc_rstc_base = of_iomap(np, 0);
	if (!sirfsoc_rstc_base)
		panic("unable to map rstc cpu registers\n");

	sirfsoc_reset_controller.of_node = np;

	if (of_property_read_u32(np, "sirf,pwrc-base", &sirfsoc_pwrc_base))
		panic("unable to find pwrc-base offset\n");

	if (IS_ENABLED(CONFIG_RESET_CONTROLLER))
		reset_controller_register(&sirfsoc_reset_controller);
}

#define SIRFSOC_SYS_RST_BIT  BIT(31)

void sirfsoc_restart(char mode, const char *cmd)
{
	if ((cmd != NULL) && !strncmp(cmd, "recovery", 8))
		sirfsoc_rtc_iobrg_writel(
			sirfsoc_rtc_iobrg_readl(
			sirfsoc_pwrc_base + SIRFSOC_BOOT_STATUS)
			| RECOVERY_MODE,
			sirfsoc_pwrc_base + SIRFSOC_BOOT_STATUS);

	writel(SIRFSOC_SYS_RST_BIT, sirfsoc_rstc_base);
}
