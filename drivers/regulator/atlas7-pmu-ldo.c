/*
 * CSRAtlas7 analog regulators drivers
 *
 * Copyright (c) 2014 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

#define ANA_PMUCTL1					0x20
#define AUDIO_CTRL_SPARE_0				0x58

#define ATLAS7_LDO(_name, reg, bit, vbit, ranges)	\
{									\
	.name = _name,	\
	.of_match = of_match_ptr(_name),	\
	.ops	= &atlas7_ldo_ops,	\
	.type = REGULATOR_VOLTAGE,	\
	.id = -1,				\
	.n_voltages	= ARRAY_SIZE(ranges),	\
	.volt_table = ranges,	\
	.vsel_reg	= reg,	\
	.vsel_mask = (ARRAY_SIZE(ranges) - 1) << (vbit),	\
	.enable_reg = reg,	\
	.enable_mask = 1 << (bit),	\
	.owner = THIS_MODULE,	\
}

static const unsigned int ldo0_audio_volt_table[] = {
	1700000, 1800000, 1900000, 2000000,
};

static const unsigned int ldo1_audio_volt_table[] = {
	2400000, 2500000, 2600000, 2700000,
};

static const unsigned int ldo_bt_volt_table[] = {
	1700000, 1800000, 1900000, 2000000,
};

static struct regulator_ops atlas7_ldo_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_iterate,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,

};


static struct regulator_desc analog_regulator_info[] = {
	ATLAS7_LDO("ldo0", ANA_PMUCTL1, 1, 2, ldo0_audio_volt_table),
	ATLAS7_LDO("ldo1", ANA_PMUCTL1, 4, 5, ldo1_audio_volt_table),
	ATLAS7_LDO("ldo2", AUDIO_CTRL_SPARE_0, 3, 1, ldo_bt_volt_table),
};

static const struct regmap_config atlas7_ldo_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x100,
	.cache_type = REGCACHE_NONE,
};

static int atlas7_analog_ldo_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct regmap *regmap;
	struct resource *mem_res;
	void __iomem *base;
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct regulator_desc *rdesc;
	u32 i;

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap(&pdev->dev, mem_res->start,
		resource_size(mem_res));
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(&pdev->dev, base,
			    &atlas7_ldo_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	config.dev = &pdev->dev;
	config.regmap = regmap;
	config.of_node = pdev->dev.of_node;

	for (i = 0; i < ARRAY_SIZE(analog_regulator_info); i++) {
		rdesc = &analog_regulator_info[i];

		if (i == 2) {
			/* fix ldo2 with diff hwver */
			if (of_machine_is_compatible("sirf,atlas7-a1"))
				rdesc->enable_is_inverted = true;
			else if (of_machine_is_compatible("sirf,atlas7-b0"))
				rdesc->enable_is_inverted = true;
		}
		rdev = devm_regulator_register(&pdev->dev,
			rdesc, &config);

		if (IS_ERR(rdev)) {
			pr_err("Failed to register ldo%d supply: %d\n",
				i, PTR_ERR(rdev));
		}
	}
	return 0;
}

static struct of_device_id atlas7_analog_ldo_match[] = {
	{ .compatible = "sirf,atlas7-analog-ldo", },
	{},
};

static struct platform_driver atlas7_analog_ldo_driver = {
	.probe		= atlas7_analog_ldo_probe,
	.driver = {
		.name = "atlas7-analog-ldo",
		.of_match_table	= atlas7_analog_ldo_match,
	},
};
static int __init atlas7_analog_ldo_init(void)
{
	return platform_driver_register(&atlas7_analog_ldo_driver);
}
subsys_initcall(atlas7_analog_ldo_init);

static void __exit atlas7_analog_ldo_exit(void)
{
	platform_driver_unregister(&atlas7_analog_ldo_driver);
}
module_exit(atlas7_analog_ldo_exit);

MODULE_AUTHOR("Rongjun Ying <rongjun.ying@csr.com>");
MODULE_DESCRIPTION("CSRAtlas7 Analog ldo regulator driver");
MODULE_LICENSE("GPL v2");
