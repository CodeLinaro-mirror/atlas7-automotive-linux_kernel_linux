/*
 * kailimba dsp driver for CSR SiRFAtlas7
 *
 * Copyright (c) 2015 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/clk.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#ifdef CONFIG_SND_SOC_SIRF_KALIMBA_DEBUG
#include "debug.h"
#endif
#include "dsp.h"
#include "firmware.h"
#include "ipc.h"
#include "regs.h"

static const struct regmap_config kalimba_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = KAS_CPU_KEYHOLE_MODE,
	.cache_type = REGCACHE_NONE,
};

static int kalimba_probe(struct platform_device *pdev)
{
	int ret;
	void __iomem *base;
	struct resource *mem_res;
	struct kalimba *kalimba;
	const struct firmware *fw;

	ret = request_firmware(&fw, "kalimba/kalimba.fw", &pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"could not upgrade firmware: unable to load\n");
		return ret;
	}

	kalimba = devm_kzalloc(&pdev->dev, sizeof(struct kalimba),
			GFP_KERNEL);
	if (kalimba == NULL)
		return -ENOMEM;

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap(&pdev->dev, mem_res->start,
			resource_size(mem_res));
	if (base == NULL)
		return -ENOMEM;

	kalimba->regmap = devm_regmap_init_mmio(&pdev->dev, base,
			&kalimba_regmap_config);

	if (IS_ERR(kalimba->regmap))
		return PTR_ERR(kalimba->regmap);

	kalimba->clk_kas = devm_clk_get(&pdev->dev, "kas_kas");
	if (IS_ERR(kalimba->clk_kas)) {
		dev_err(&pdev->dev, "Get clock(kas) failed.\n");
		return PTR_ERR(kalimba->clk_kas);
	}
	ret = clk_prepare_enable(kalimba->clk_kas);
	if (ret) {
		dev_err(&pdev->dev, "Enable clock(kas) failed.\n");
		return ret;
	}

	kalimba->clk_audmscm = devm_clk_get(&pdev->dev, "audmscm_nocd");
	if (IS_ERR(kalimba->clk_audmscm)) {
		dev_err(&pdev->dev, "Get clock(audmscm) failed.\n");
		ret = PTR_ERR(kalimba->clk_audmscm);
		goto clk_get_audmscm_failed;
	}
	ret = clk_prepare_enable(kalimba->clk_audmscm);
	if (ret) {
		dev_err(&pdev->dev, "Enable clock(audmscm failed.\n");
		goto clk_get_audmscm_failed;
	}

	kalimba->clk_gpum = devm_clk_get(&pdev->dev, "gpum_nocd");
	if (IS_ERR(kalimba->clk_gpum)) {
		dev_err(&pdev->dev, "Get clock(gpum) failed.\n");
		ret = PTR_ERR(kalimba->clk_gpum);
		goto clk_get_gpum_failed;
	}
	ret = clk_prepare_enable(kalimba->clk_gpum);
	if (ret) {
		dev_err(&pdev->dev, "Enable clock(gpum failed.\n");
		goto clk_get_gpum_failed;
	}

	ret = device_reset(&pdev->dev);
	if (ret != 0) {
		dev_err(&pdev->dev, "Reset kalimba failed: %d\n", ret);
		goto kalimba_reset_failed;
	}

	firmware_download(kalimba->regmap, (u32 *)(fw->data));
	release_firmware(fw);

	platform_set_drvdata(pdev, kalimba);
	ret = ipc_init(pdev);
	if (ret != 0)
		goto kalimba_reset_failed;
#ifdef CONFIG_SND_SOC_SIRF_KALIMBA_DEBUG
	ret = debug_init(pdev);
	if (ret != 0) {
		dev_err(&pdev->dev, "Initialize debug interface failed.\n");
		goto kalimba_reset_failed;
	}
#endif
	return 0;

kalimba_reset_failed:
	clk_disable_unprepare(kalimba->clk_gpum);
clk_get_gpum_failed:
	clk_disable_unprepare(kalimba->clk_audmscm);
clk_get_audmscm_failed:
	clk_disable_unprepare(kalimba->clk_kas);
	return ret;
}

static int kalimba_remove(struct platform_device *pdev)
{
	struct kalimba *kalimba = platform_get_drvdata(pdev);

	clk_disable_unprepare(kalimba->clk_gpum);
	clk_disable_unprepare(kalimba->clk_audmscm);
	clk_disable_unprepare(kalimba->clk_kas);

#ifdef CONFIG_SND_SOC_SIRF_KALIMBA_DEBUG
	debug_deinit(pdev);
#endif
	return 0;
}

static const struct of_device_id kalimba_of_match[] = {
	{ .compatible = "csr,kalimba", },
	{}
};
MODULE_DEVICE_TABLE(of, kalimba_of_match);

static struct platform_driver kalimba_driver = {
	.driver = {
		.name = "kalimba",
		.of_match_table = kalimba_of_match,
	},
	.probe = kalimba_probe,
	.remove = kalimba_remove,
};

module_platform_driver(kalimba_driver);

MODULE_DESCRIPTION("SiRF SoC Kalimba DSP driver");
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
MODULE_LICENSE("GPL v2");
