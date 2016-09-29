/*
 * power management mfd for CSR SiRFSoC chips
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

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/io.h>
#include <linux/regmap.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>
#include <linux/mfd/core.h>
#include <linux/mfd/sirfsoc_pwrc.h>

struct sirfsoc_pwrc_register sirfsoc_a7da_pwrc = {
	.pwrc_pdn_ctrl_set = 0x0,
	.pwrc_pdn_ctrl_clr = 0x4,
	.pwrc_pon_status = 0x8,
	.pwrc_trigger_en_set = 0xc,
	.pwrc_trigger_en_clr = 0x10,
	.pwrc_int_mask_set = 0x14,
	.pwrc_int_mask_clr = 0x18,
	.pwrc_int_status = 0x1c,
	.pwrc_pin_status = 0x20,
	.pwrc_rtc_pll_ctrl = 0x28,
	.pwrc_gpio3_debug = 0x34,
	.pwrc_rtc_noc_pwrctl_set = 0x38,
	.pwrc_rtc_noc_pwrctl_clr = 0x3c,
	.pwrc_rtc_can_ctrl = 0x48,
	.pwrc_rtc_can_status = 0x4c,
	.pwrc_fsm_m3_ctrl = 0x50,
	.pwrc_fsm_state = 0x54,
	.pwrc_rtcldo_reg = 0x58,
	.pwrc_gnss_ctrl = 0x5c,
	.pwrc_gnss_status = 0x60,
	.pwrc_xtal_reg = 0x64,
	.pwrc_xtal_ldo_mux_sel = 0x68,
	.pwrc_rtc_sw_rstc_set = 0x6c,
	.pwrc_rtc_sw_rstc_clr = 0x70,
	.pwrc_power_sw_ctrl_set = 0x74,
	.pwrc_power_sw_ctrl_clr = 0x78,
	.pwrc_rtc_dcog = 0x7c,
	.pwrc_m3_memories = 0x80,
	.pwrc_can0_memory = 0x84,
	.pwrc_rtc_gnss_memory = 0x88,
	.pwrc_m3_clk_en = 0x8c,
	.pwrc_can0_clk_en = 0x90,
	.pwrc_spi0_clk_en = 0x94,
	.pwrc_rtc_sec_clk_en = 0x98,
	.pwrc_rtc_noc_clk_en = 0x9c,
};

struct sirfsoc_pwrc_register sirfsoc_prima2_pwrc = {
	.pwrc_pdn_ctrl_set = 0x0,
	.pwrc_pon_status = 0x4,
	.pwrc_trigger_en_set = 0x8,
	.pwrc_int_status = 0xc,
	.pwrc_int_mask_set = 0x10,
	.pwrc_pin_status = 0x14,
	.pwrc_scratch_pad1 = 0x18,
	.pwrc_scratch_pad2 = 0x1c,
	.pwrc_scratch_pad3 = 0x20,
	.pwrc_scratch_pad4 = 0x24,
	.pwrc_scratch_pad5 = 0x28,
	.pwrc_scratch_pad6 = 0x2c,
	.pwrc_scratch_pad7 = 0x30,
	.pwrc_scratch_pad8 = 0x34,
	.pwrc_scratch_pad9 = 0x38,
	.pwrc_scratch_pad10 = 0x3c,
	.pwrc_scratch_pad11 = 0x40,
	.pwrc_scratch_pad12 = 0x44,
	.pwrc_gpio3_clk = 0x54,
	.pwrc_gpio_ds = 0x78,
};

static const struct regmap_irq pwrc_irqs[] = {
	[PWRC_IRQ_ONKEY] = {
		.mask = BIT(PWRC_IRQ_ONKEY),
	},
	[PWRC_IRQ_EXT_ONKEY] = {
		.mask = BIT(PWRC_IRQ_EXT_ONKEY),
	},
	[PWRC_IRQ_LOWBAT] = {
		.mask = BIT(PWRC_IRQ_LOWBAT),
	},
	[PWRC_IRQ_MULT_BUTTON1] = {
		.mask = BIT(PWRC_IRQ_MULT_BUTTON1),
	},
	[PWRC_IRQ_MULT_BUTTON2] = {
		.mask = BIT(PWRC_IRQ_MULT_BUTTON2),
	},
	[PWRC_IRQ_GNSS_PON_REQ] = {
		.mask = BIT(PWRC_IRQ_GNSS_PON_REQ),
	},
	[PWRC_IRQ_GNSS_POFF_REQ] = {
		.mask = BIT(PWRC_IRQ_GNSS_POFF_REQ),
	},
	[PWRC_IRQ_GNSS_PON_ACK] = {
		.mask = BIT(PWRC_IRQ_GNSS_PON_ACK),
	},
	[PWRC_IRQ_GNSS_POFF_ACK] = {
		.mask = BIT(PWRC_IRQ_GNSS_POFF_ACK),
	},

};

static struct regmap_irq_chip pwrc_irq_chip = {
	.name = "pwrc_irq",
	.irqs = pwrc_irqs,
	.num_irqs = ARRAY_SIZE(pwrc_irqs),
	.num_regs = 1,
	.unmask_separate = 1,
	.ack_invert = 1,
	.init_ack_masked = true,
};

static const struct of_device_id pwrc_ids[] = {
	{ .compatible = "sirf,prima2-pwrc", .data = &sirfsoc_prima2_pwrc},
	{ .compatible = "sirf,atlas7-pwrc", .data = &sirfsoc_a7da_pwrc},
	{}
};

static const struct mfd_cell pwrc_devs[] = {
	{
		.name = "rtcmclk",
		.of_compatible = "sirf,atlas7-rtcmclk",
	}, {
		.name = "sirf-sysctl",
		.of_compatible = "sirf,sirf-sysctl",
	}, {
		.name = "atlas7-gps",
		.of_compatible = "sirf,atlas7-gps",
	}, {
		.name = "onkey",
		.of_compatible = "sirf,prima2-onkey",
	},
};

static const struct regmap_config pwrc_regmap_config = {
	.fast_io = true,
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static int sirfsoc_pwrc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	struct sirfsoc_pwrc_info *pwrcinfo;
	struct regmap_irq_chip *regmap_irq_chip;
	struct sirfsoc_pwrc_register *pwrc_reg;
	struct regmap *map;
	int ret;
	u32 subreg_info[2];

	if (of_property_read_u32_array(np, "sub-reg", &subreg_info[0], 2))
		panic("unable to find sub-reg of pwrc node in dtb\n");

	pwrcinfo = devm_kzalloc(&pdev->dev,
			sizeof(struct sirfsoc_pwrc_info), GFP_KERNEL);
	if (!pwrcinfo)
		return -ENOMEM;
	pwrcinfo->base = subreg_info[0];
	pwrcinfo->size = subreg_info[1];

	/*
	 * pwrc behind rtciobrg offset is diff between prima2 and atlas7
	 * here match to each ids data for it.
	 */
	match = of_match_node(pwrc_ids, np);
	if (WARN_ON(!match))
		return -ENODEV;

	pwrcinfo->pwrc_reg = (struct sirfsoc_pwrc_register *)match->data;

	if (of_device_is_compatible(np, "sirf,atlas7-pwrc"))
		pwrcinfo->ver = PWRC_ATLAS7_VER;
	else if (of_device_is_compatible(np, "sirf,prima2-pwrc"))
		pwrcinfo->ver = PWRC_PRIMA2_VER;
	else
		return -EINVAL;

	of_node_put(np);

	map = (struct regmap *)devm_regmap_init_iobg(&pdev->dev,
			&pwrc_regmap_config);
	if (IS_ERR(map)) {
		ret = PTR_ERR(map);
		dev_err(&pdev->dev, "Failed to allocate register map: %d\n",
			ret);
		goto err;
	}

	pwrcinfo->regmap = map;
	pwrcinfo->dev = &pdev->dev;
	platform_set_drvdata(pdev, pwrcinfo);

	ret = mfd_add_devices(pwrcinfo->dev, 0, pwrc_devs,
		ARRAY_SIZE(pwrc_devs), NULL, 0, NULL);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add all pwrc subdev\n");
		goto err;
	}

	ret = of_irq_get(pdev->dev.of_node, 0);
	if (ret <= 0) {
		dev_info(&pdev->dev,
			"Unable to find IRQ for pwrc. ret=%d\n", ret);
		goto err_irq;
	}

	pwrcinfo->irq = ret;
	regmap_irq_chip = &pwrc_irq_chip;
	pwrcinfo->regmap_irq_chip = regmap_irq_chip;

	pwrc_reg = pwrcinfo->pwrc_reg;
	regmap_irq_chip->mask_base = pwrcinfo->base +
						pwrc_reg->pwrc_int_mask_set;
	regmap_irq_chip->unmask_base = pwrcinfo->base +
				pwrc_reg->pwrc_int_mask_clr;
	regmap_irq_chip->status_base = pwrcinfo->base +
						pwrc_reg->pwrc_int_status;
	regmap_irq_chip->ack_base = pwrcinfo->base +
						pwrc_reg->pwrc_int_status;

	sirfsoc_iobg_lock();
	/* enable irq trigger capability for onkey/extonkey/lowbat/multi-butt */
	ret = regmap_update_bits(map,
			pwrcinfo->base +
			pwrc_reg->pwrc_trigger_en_set, 0x1F, 0x1F);
	sirfsoc_iobg_unlock();

	if (ret < 0)
		goto err_irq;

	/* add irq controller for pwrc */
	ret = regmap_add_irq_chip(map, pwrcinfo->irq, IRQF_ONESHOT,
				-1, pwrcinfo->regmap_irq_chip,
				&pwrcinfo->irq_data);

	if (ret) {
		dev_err(&pdev->dev, "Failed to add regmap irq controller for pwrc\n");
		goto err;
	}

	return 0;
err_irq:
	mfd_remove_devices(pwrcinfo->dev);
err:
	return ret;
}

static struct platform_driver sirfsoc_pwrc_driver = {
	.probe	= sirfsoc_pwrc_probe,
	.driver	= {
		.name = "sirfsoc_pwrc",
		.of_match_table = pwrc_ids,
	},
};

static int __init atlas7_pwrc_init(void)
{
	return platform_driver_register(&sirfsoc_pwrc_driver);
}
subsys_initcall(atlas7_pwrc_init);
