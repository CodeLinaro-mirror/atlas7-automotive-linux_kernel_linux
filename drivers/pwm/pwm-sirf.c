/*
 * SIRF serial SoC PWM device core driver
 *
 * Copyright (c) 2014 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/pwm.h>
#include <linux/of.h>
#include <linux/io.h>

#define SIRF_PWM_SELECT_PRECLK			0x0
#define SIRF_PWM_OE				0x4
#define SIRF_PWM_ENABLE_PRECLOCK		0x8
#define SIRF_PWM_ENABLE_POSTCLOCK		0xC
#define SIRF_PWM_GET_WAIT_OFFSET(n)		(0x10 + 0x8*n)
#define SIRF_PWM_GET_HOLD_OFFSET(n)		(0x14 + 0x8*n)

#define SIRF_PWM_TR_STEP(n)			(0x48 + 0x8*n)
#define SIRF_PWM_STEP_HOLD(n)			(0x4c + 0x8*n)

#define SRC_FIELD_SIZE				3
#define BYPASS_MODE_BIT				21
#define TRANS_MODE_SELECT_BIT			7

#define SIRF_PWM_CHL_NUM			7
#define SIRF_PWM_BLS_GRP_NUM			16

struct sirf_pwm {
	void __iomem		*base;
	struct clk		*clk;
	struct pwm_chip		chip;
	unsigned long		src_clk_rate;
};

#define to_sirf_chip(chip)	container_of(chip, struct sirf_pwm, chip)

static unsigned int sirf_pwm_ns_to_cycles(struct pwm_chip *chip, unsigned int time_ns)
{
	struct sirf_pwm *spwm = to_sirf_chip(chip);
	u64 dividend;
	unsigned int cycle;

	dividend = (u64)spwm->src_clk_rate * time_ns + NSEC_PER_SEC / 2;
	do_div(dividend, NSEC_PER_SEC);

	cycle = dividend & 0xFFFFFFFFUL;

	return cycle > 1 ? cycle : 1;
}

static int sirf_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
		int duty_ns, int period_ns)
{
	unsigned int period_cycles, high_cycles, low_cycles;
	unsigned int val;
	struct sirf_pwm *spwm = to_sirf_chip(chip);

	period_cycles = sirf_pwm_ns_to_cycles(chip, period_ns);

	high_cycles = sirf_pwm_ns_to_cycles(chip, duty_ns);
	low_cycles = period_cycles - high_cycles;

	if (period_cycles == 1) {
		/* bypass mode */
		val = readl(spwm->base + SIRF_PWM_SELECT_PRECLK);
		val |= 0x1 << (BYPASS_MODE_BIT + pwm->hwpwm);
		writel(val, spwm->base + SIRF_PWM_SELECT_PRECLK);
		dev_warn(chip->dev, "period is too short!\n");
	} else {
		/* divider mode */
		val = readl(spwm->base + SIRF_PWM_SELECT_PRECLK);
		val &= ~(0x1 << (BYPASS_MODE_BIT + pwm->hwpwm));
		writel(val, spwm->base + SIRF_PWM_SELECT_PRECLK);

		if (high_cycles == period_cycles) {
			high_cycles--;
			low_cycles = 1;
		}

		writel(high_cycles, spwm->base + SIRF_PWM_GET_WAIT_OFFSET(pwm->hwpwm));
		writel(low_cycles, spwm->base + SIRF_PWM_GET_HOLD_OFFSET(pwm->hwpwm));
	}

	return 0;
}

static int sirf_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct sirf_pwm *spwm = to_sirf_chip(chip);
	unsigned int val;

	/* disable preclock */
	val = readl(spwm->base + SIRF_PWM_ENABLE_PRECLOCK);
	val &= ~(1 << pwm->hwpwm);
	writel(val, spwm->base + SIRF_PWM_ENABLE_PRECLOCK);

	/* select preclock source must after disable preclk*/
	val = readl(spwm->base + SIRF_PWM_SELECT_PRECLK);
	val &= ~(0x7 << (SRC_FIELD_SIZE * pwm->hwpwm));
	writel(val, spwm->base + SIRF_PWM_SELECT_PRECLK);
	/* wait for some time */
	udelay(100);

	/* enable preclock */
	val = readl(spwm->base + SIRF_PWM_ENABLE_PRECLOCK);
	val |= (1 << pwm->hwpwm);
	writel(val, spwm->base + SIRF_PWM_ENABLE_PRECLOCK);

	/* enable post clock*/
	val = readl(spwm->base + SIRF_PWM_ENABLE_POSTCLOCK);
	val |= (1 << pwm->hwpwm);
	writel(val, spwm->base + SIRF_PWM_ENABLE_POSTCLOCK);

	/* enable output */
	val = readl(spwm->base + SIRF_PWM_OE);
	val |= 1 << pwm->hwpwm;
	val |= 1 << (pwm->hwpwm + TRANS_MODE_SELECT_BIT);

	writel(val, spwm->base + SIRF_PWM_OE);

	return 0;
}

static void sirf_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	unsigned int val;
	struct sirf_pwm *spwm = to_sirf_chip(chip);
	/* disable output */
	val = readl(spwm->base + SIRF_PWM_OE);
	val &= ~(1 << pwm->hwpwm);
	writel(val, spwm->base + SIRF_PWM_OE);

	/* disable postclock */
	val = readl(spwm->base + SIRF_PWM_ENABLE_POSTCLOCK);
	val &= ~(1 << pwm->hwpwm);
	writel(val, spwm->base + SIRF_PWM_ENABLE_POSTCLOCK);

	/* disable preclock */
	val = readl(spwm->base + SIRF_PWM_ENABLE_PRECLOCK);
	val &= ~(1 << pwm->hwpwm);
	writel(val, spwm->base + SIRF_PWM_ENABLE_PRECLOCK);
}

static struct pwm_ops sirf_pwm_ops = {
	.enable = sirf_pwm_enable,
	.disable = sirf_pwm_disable,
	.config = sirf_pwm_config,
	.owner = THIS_MODULE,
};

static int sirf_pwm_probe(struct platform_device *pdev)
{
	struct sirf_pwm *spwm;
	struct resource *mem_res;
	struct clk *clk_pwm_src;
	int ret;

	spwm = devm_kzalloc(&pdev->dev, sizeof(struct sirf_pwm),
			GFP_KERNEL);
	if (!spwm)
		return -ENOMEM;

	platform_set_drvdata(pdev, spwm);

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	spwm->base = devm_ioremap_resource(&pdev->dev, mem_res);
	if (!spwm->base)
		return -ENOMEM;

	/*
	 * the 1st clock is for PWM controller
	 */
	spwm->clk = of_clk_get(pdev->dev.of_node, 0);
	if (IS_ERR(spwm->clk)) {
		dev_err(&pdev->dev, "Get PWM controller clock failed.\n");
		return PTR_ERR(spwm->clk);
	}
	clk_prepare_enable(spwm->clk);

	/*
	 * the 2nd clock is the source to generate PWM waves
	 * it is the OSC on SiRFSoC
	 */
	clk_pwm_src = of_clk_get(pdev->dev.of_node, 1);
	if (IS_ERR(clk_pwm_src)) {
		dev_err(&pdev->dev, "Get PWM source clock failed.\n");
		return PTR_ERR(clk_pwm_src);
	}
	spwm->src_clk_rate = clk_get_rate(clk_pwm_src);
	clk_put(clk_pwm_src);

	spwm->chip.dev = &pdev->dev;
	spwm->chip.ops = &sirf_pwm_ops;
	spwm->chip.base = 0;
	spwm->chip.npwm = SIRF_PWM_CHL_NUM;

	ret = pwmchip_add(&spwm->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register pwm\n");
		clk_disable_unprepare(spwm->clk);
		return ret;
	}

	return 0;
}

static int sirf_pwm_remove(struct platform_device *pdev)
{
	struct sirf_pwm *spwm = platform_get_drvdata(pdev);
	clk_disable_unprepare(spwm->clk);
	clk_put(spwm->clk);

	pwmchip_remove(&spwm->chip);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sirf_pwm_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sirf_pwm *spwm = platform_get_drvdata(pdev);

	clk_disable_unprepare(spwm->clk);

	return 0;
}

static void sirf_pwm_config_restore(struct sirf_pwm *spwm)
{
	struct pwm_device *pwm;
	int i;

	for (i = 0; i < spwm->chip.npwm; i++) {
		pwm = &spwm->chip.pwms[i];
		/*
		 * while restoring from hibernation, state of pwm is enabled,
		 * but PWM hardware is not re-enabled
		 */
		if (test_bit(PWMF_REQUESTED, &pwm->flags) &&
		     test_bit(PWMF_ENABLED, &pwm->flags))
			sirf_pwm_enable(&spwm->chip, pwm);
	}
}

static int sirf_pwm_resume(struct device *dev)
{
	struct sirf_pwm *spwm = dev_get_drvdata(dev);

	clk_prepare_enable(spwm->clk);

	sirf_pwm_config_restore(spwm);

	return 0;
}

static int sirf_pwm_restore(struct device *dev)
{
	struct sirf_pwm *spwm = dev_get_drvdata(dev);

	/* back from hibernation, clock is already enabled */
	sirf_pwm_config_restore(spwm);

	return 0;
}

#else
#define sirf_pwm_resume NULL
#define sirf_pwm_suspend NULL
#define sirf_pwm_restore NULL
#endif

static const struct dev_pm_ops sirf_pwm_pm_ops = {
	.suspend = sirf_pwm_suspend,
	.resume = sirf_pwm_resume,
	.restore = sirf_pwm_restore,
};

static const struct of_device_id sirf_pwm_of_match[] = {
	{ .compatible = "sirf,prima2-pwm", },
	{}
};
MODULE_DEVICE_TABLE(of, sirf_pwm_of_match);

static struct platform_driver sirf_pwm_driver = {
	.driver = {
		.name = "prima2-pwm",
		.owner = THIS_MODULE,
		.pm = &sirf_pwm_pm_ops,
		.of_match_table = sirf_pwm_of_match,
	},
	.probe = sirf_pwm_probe,
	.remove = sirf_pwm_remove,
};

module_platform_driver(sirf_pwm_driver);

MODULE_DESCRIPTION("SIRF serial SoC PWM device core driver");
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>, "
	"Huayi Li <huayi.li@csr.com>");
MODULE_LICENSE("GPL v2");
