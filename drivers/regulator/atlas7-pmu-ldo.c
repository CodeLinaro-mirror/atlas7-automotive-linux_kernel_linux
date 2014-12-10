/*
 * CSRAtlas7 PMU regulators drivers
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

#define ANA_PMUCTL1				0

#define PMUCTL1_LDO_VAUDIO1V8_EN		(1 << 1)
#define PMUCTL1_LDO_VAUDIO2V5_EN		(1 << 4)
#define PMUCTL1_VOUT_SEL_LDO_VAUDIO1V8_MASK	(0x3 << 2)
#define PMUCTL1_VOUT_SEL_LDO_VAUDIO2V5_MASK	(0x3 << 5)
#define PMUCTL1_VOUT_SEL_LDO_VAUDIO1V8_SHIFT	2
#define PMUCTL1_VOUT_SEL_LDO_VAUDIO2V5_SHIFT	5

static int atlas7_pmu_reg_enable(struct regulator_dev *rdev)
{
	int ret;

	ret = regulator_enable_regmap(rdev);
	if (ret < 0)
		return ret;

	return regmap_update_bits(rdev->regmap, ANA_PMUCTL1,
		PMUCTL1_VOUT_SEL_LDO_VAUDIO1V8_MASK
		| PMUCTL1_VOUT_SEL_LDO_VAUDIO2V5_MASK,
		(1 << PMUCTL1_VOUT_SEL_LDO_VAUDIO1V8_SHIFT)
		| (1 << PMUCTL1_VOUT_SEL_LDO_VAUDIO2V5_SHIFT));
	return 0;
}

static struct regulator_ops atlas7_pmu_reg_ops = {
	.enable = atlas7_pmu_reg_enable,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

static struct regulator_desc atlas7_pmu_reg_desc = {
	.name = "ldo",
	.of_match = "ldo",
	.id = -1,
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.ops = &atlas7_pmu_reg_ops,
	.enable_reg = ANA_PMUCTL1,
	.enable_mask = PMUCTL1_LDO_VAUDIO2V5_EN | PMUCTL1_LDO_VAUDIO1V8_EN,
};

static const struct regmap_config atlas7_pmu_reg_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = ANA_PMUCTL1,
	.cache_type = REGCACHE_NONE,
};

static int atlas7_pmu_reg_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	struct resource *mem_res;
	void __iomem *base;
	struct regulator_config config = { };
	struct regulator_dev *rdev;

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_res) {
		dev_err(&pdev->dev, "no mem resource?\n");
		return -ENODEV;
	}

	base = devm_ioremap(&pdev->dev, mem_res->start,
		resource_size(mem_res));
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(&pdev->dev, base,
			    &atlas7_pmu_reg_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	config.dev = &pdev->dev;
	config.regmap = regmap;
	config.of_node = pdev->dev.of_node;
	rdev = devm_regulator_register(&pdev->dev,
			&atlas7_pmu_reg_desc, &config);
	if (IS_ERR(rdev)) {
		dev_err(&pdev->dev, "regulator init failed\n");
		return PTR_ERR(rdev);
	}

	return 0;
}

static struct of_device_id atlas7_pmu_reg_match[] = {
	{ .compatible = "sirf,atlas7-pmu-ldo", },
	{},
};

static struct platform_driver atlas7_pmu_reg_driver = {
	.probe		= atlas7_pmu_reg_probe,
	.driver = {
		.name = "atlas7-pmu-ldo",
		.owner = THIS_MODULE,
		.of_match_table	= atlas7_pmu_reg_match,
	},
};
static int __init atlas7_pmu_reg_init(void)
{
	return platform_driver_register(&atlas7_pmu_reg_driver);
}
subsys_initcall(atlas7_pmu_reg_init);

static void __exit atlas7_pmu_reg_exit(void)
{
	platform_driver_unregister(&atlas7_pmu_reg_driver);
}
module_exit(atlas7_pmu_reg_exit);

MODULE_AUTHOR("Rongjun Ying <rongjun.ying@csr.com>");
MODULE_DESCRIPTION("CSRAtlas7 PMU ldo regulator driver");
MODULE_LICENSE("GPL v2");
