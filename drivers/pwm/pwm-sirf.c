/*
 * SIRF serial SoC PWM device core driver
 *
 * Copyright (c) 2014 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

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

#define SIRF_MAX_SRC_CLK			5

struct sirf_pwm_chip {
	struct pwm_chip	chip;
	struct mutex mutex;
	void __iomem *base;
	struct clk *pwmc_clk;
};

struct sirf_pwm {
	u32 sigsrc_clk_idx;
	struct clk *sigsrc_clk;
	u32 bypass_mode;
	u32 duty_ns;
};

static inline struct sirf_pwm_chip *to_sirf_pwm_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct sirf_pwm_chip, chip);
}

static int sirf_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct sirf_pwm *spwm;

	spwm = devm_kzalloc(chip->dev, sizeof(*spwm), GFP_KERNEL);
	if (!spwm)
		return -ENOMEM;

	pwm_set_chip_data(pwm, spwm);

	return 0;
}

static void sirf_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct sirf_pwm *spwm = pwm_get_chip_data(pwm);

	if (!spwm)
		devm_kfree(chip->dev, spwm);
}

static u32 sirf_pwm_ns_to_cycles(struct pwm_device *pwm, u32 time_ns)
{
	struct sirf_pwm *spwm = pwm_get_chip_data(pwm);
	u32 src_clk_rate = clk_get_rate(spwm->sigsrc_clk);
	u64 cycle;

	cycle = div_u64((u64)src_clk_rate * time_ns, NSEC_PER_SEC);

	return (u32)(cycle > 1 ? cycle : 1);
}

static void _sirf_pwm_hwconfig(struct pwm_chip *chip, struct pwm_device *pwm)
{
	u32 period_cycles, high_cycles, low_cycles;
	struct sirf_pwm_chip *spwmc = to_sirf_pwm_chip(chip);
	struct sirf_pwm *spwm = pwm_get_chip_data(pwm);

	period_cycles = sirf_pwm_ns_to_cycles(pwm, pwm_get_period(pwm));

	/*
	 * enter bypass mode, high_cycles and low_cycle
	 * do not need to config if period_cycles == 1
	 */
	if (period_cycles == 1) {
		spwm->bypass_mode = 1;
	} else {
		spwm->bypass_mode = 0;

		high_cycles = sirf_pwm_ns_to_cycles(pwm, spwm->duty_ns);
		low_cycles = period_cycles - high_cycles;

		/*
		 * high_cycles will equal to period_cycles when duty_ns
		 * is big enough, so low_cycles will be 0,
		 * a wrong value will be written to register after
		 * low_cycles minus 1 later.
		 */
		if (high_cycles == period_cycles) {
			high_cycles--;
			low_cycles = 1;
		}

		mutex_lock(&spwmc->mutex);

		writel(high_cycles - 1,
			spwmc->base + SIRF_PWM_GET_WAIT_OFFSET(pwm->hwpwm));
		writel(low_cycles - 1,
			spwmc->base + SIRF_PWM_GET_HOLD_OFFSET(pwm->hwpwm));

		mutex_unlock(&spwmc->mutex);
	}
}

static int sirf_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			int duty_ns, int period_ns)
{
	struct sirf_pwm_chip *spwmc = to_sirf_pwm_chip(chip);
	struct sirf_pwm *spwm = pwm_get_chip_data(pwm);

	if (!test_bit(PWMF_ENABLED, &pwm->flags)) {
		u32 src_clk_rate;
		u32 i;
		u64 cycles, high_cycles, low_cycles;
		u32 cycles_diff;
		u64 cycles_diff_enlarged;
		u64 diff, diff_min = ~0;
		int ret;
		char src_clk_name[10];
		struct clk *sigsrc_clk;

		/*
		 * select a best source clock for the specific PWM clock
		 * 1. select the clock with minimal error
		 * 2. select the slower clock if some of them have
		 *    the same error
		 */
		for (i = 0; i < SIRF_MAX_SRC_CLK; i++) {
			sprintf(src_clk_name, "sigsrc%d", i);
			sigsrc_clk = devm_clk_get(chip->dev, src_clk_name);
			if (IS_ERR(sigsrc_clk))
				continue;

			src_clk_rate = clk_get_rate(sigsrc_clk);

			high_cycles = (u64)src_clk_rate * duty_ns;
			high_cycles = div_u64(high_cycles, NSEC_PER_SEC);

			low_cycles = (u64)src_clk_rate * (period_ns - duty_ns);
			low_cycles = div_u64(low_cycles, NSEC_PER_SEC);

			/* only smaller than 0xffff is valid */
			if (high_cycles > 0xffff || low_cycles > 0xffff)
				continue;

			cycles = (u64)src_clk_rate * period_ns;
			cycles = div_u64_rem(cycles, NSEC_PER_SEC,
					&cycles_diff);

			/* enlarge cycle_diff for division */
			cycles_diff_enlarged = ((u64)cycles_diff) << 32;

			diff = div_u64(cycles_diff_enlarged, src_clk_rate);

			if (diff < diff_min) {
				diff_min = diff;
				spwm->sigsrc_clk_idx = i;
				spwm->sigsrc_clk = sigsrc_clk;
			} else {
				devm_clk_put(chip->dev, sigsrc_clk);
			}
		}

		/*
		 * enable PWM before writing the register
		 */
		ret = clk_prepare_enable(spwmc->pwmc_clk);
		if (ret)
			return ret;
	}

	spwm->duty_ns = duty_ns;

	_sirf_pwm_hwconfig(chip, pwm);

	/*
	 * if the PWM is not enabled, turn off the clock again
	 */
	if (!test_bit(PWMF_ENABLED, &pwm->flags))
		clk_disable_unprepare(spwmc->pwmc_clk);

	return 0;
}

static void _sirf_pwm_hwenable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct sirf_pwm_chip *spwmc = to_sirf_pwm_chip(chip);
	struct sirf_pwm *spwm = pwm_get_chip_data(pwm);
	u32 val;

	mutex_lock(&spwmc->mutex);

	/* disable preclock */
	val = readl(spwmc->base + SIRF_PWM_ENABLE_PRECLOCK);
	val &= ~(1 << pwm->hwpwm);
	writel(val, spwmc->base + SIRF_PWM_ENABLE_PRECLOCK);

	/* select preclock source must after disable preclk */
	val = readl(spwmc->base + SIRF_PWM_SELECT_PRECLK);
	val &= ~(0x7 << (SRC_FIELD_SIZE * pwm->hwpwm));
	val |= (spwm->sigsrc_clk_idx << (SRC_FIELD_SIZE * pwm->hwpwm));

	if (spwm->bypass_mode == 1)
		val |= (0x1 << (BYPASS_MODE_BIT + pwm->hwpwm));
	else
		val &= ~(0x1 << (BYPASS_MODE_BIT + pwm->hwpwm));

	writel(val, spwmc->base + SIRF_PWM_SELECT_PRECLK);

	/* wait for some time */
	usleep_range(100, 200);

	/* enable preclock */
	val = readl(spwmc->base + SIRF_PWM_ENABLE_PRECLOCK);
	val |= (1 << pwm->hwpwm);
	writel(val, spwmc->base + SIRF_PWM_ENABLE_PRECLOCK);

	/* enable post clock*/
	val = readl(spwmc->base + SIRF_PWM_ENABLE_POSTCLOCK);
	val |= (1 << pwm->hwpwm);
	writel(val, spwmc->base + SIRF_PWM_ENABLE_POSTCLOCK);

	/* enable output */
	val = readl(spwmc->base + SIRF_PWM_OE);
	val |= 1 << pwm->hwpwm;
	val |= 1 << (pwm->hwpwm + TRANS_MODE_SELECT_BIT);

	writel(val, spwmc->base + SIRF_PWM_OE);

	mutex_unlock(&spwmc->mutex);
}

static int sirf_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct sirf_pwm_chip *spwmc = to_sirf_pwm_chip(chip);
	struct sirf_pwm *spwm = pwm_get_chip_data(pwm);
	u32 ret;

	ret = clk_prepare_enable(spwm->sigsrc_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(spwmc->pwmc_clk);
	if (ret)
		return ret;

	_sirf_pwm_hwenable(chip, pwm);

	return 0;
}

static void sirf_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	u32 val;
	struct sirf_pwm_chip *spwmc = to_sirf_pwm_chip(chip);
	struct sirf_pwm *spwm = pwm_get_chip_data(pwm);

	mutex_lock(&spwmc->mutex);

	/* disable output */
	val = readl(spwmc->base + SIRF_PWM_OE);
	val &= ~(1 << pwm->hwpwm);
	writel(val, spwmc->base + SIRF_PWM_OE);

	/* disable postclock */
	val = readl(spwmc->base + SIRF_PWM_ENABLE_POSTCLOCK);
	val &= ~(1 << pwm->hwpwm);
	writel(val, spwmc->base + SIRF_PWM_ENABLE_POSTCLOCK);

	/* disable preclock */
	val = readl(spwmc->base + SIRF_PWM_ENABLE_PRECLOCK);
	val &= ~(1 << pwm->hwpwm);
	writel(val, spwmc->base + SIRF_PWM_ENABLE_PRECLOCK);

	mutex_unlock(&spwmc->mutex);

	clk_disable_unprepare(spwm->sigsrc_clk);

	clk_disable_unprepare(spwmc->pwmc_clk);
}

static const struct pwm_ops sirf_pwm_ops = {
	.request = sirf_pwm_request,
	.free = sirf_pwm_free,
	.enable = sirf_pwm_enable,
	.disable = sirf_pwm_disable,
	.config = sirf_pwm_config,
	.owner = THIS_MODULE,
};

static int sirf_pwm_probe(struct platform_device *pdev)
{
	struct sirf_pwm_chip *spwmc;
	struct resource *mem_res;
	int ret;

	spwmc = devm_kzalloc(&pdev->dev, sizeof(*spwmc),
			GFP_KERNEL);
	if (!spwmc)
		return -ENOMEM;

	platform_set_drvdata(pdev, spwmc);

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	spwmc->base = devm_ioremap_resource(&pdev->dev, mem_res);
	if (IS_ERR(spwmc->base))
		return PTR_ERR(spwmc->base);

	/*
	 * get clock for PWM controller
	 */
	spwmc->pwmc_clk = devm_clk_get(&pdev->dev, "pwmc");
	if (IS_ERR(spwmc->pwmc_clk)) {
		dev_err(&pdev->dev, "failed to get PWM controller clock\n");
		return PTR_ERR(spwmc->pwmc_clk);
	}

	spwmc->chip.dev = &pdev->dev;
	spwmc->chip.ops = &sirf_pwm_ops;
	spwmc->chip.base = -1;
	spwmc->chip.npwm = 7;

	mutex_init(&spwmc->mutex);

	ret = pwmchip_add(&spwmc->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register PWM chip\n");
		return ret;
	}

	return 0;
}

static int sirf_pwm_remove(struct platform_device *pdev)
{
	struct sirf_pwm_chip *spwmc = platform_get_drvdata(pdev);

	return pwmchip_remove(&spwmc->chip);
}

#ifdef CONFIG_PM_SLEEP
static int sirf_pwm_suspend(struct device *dev)
{
	struct sirf_pwm_chip *spwmc = dev_get_drvdata(dev);
	struct pwm_device *pwm;
	int i;

	for (i = 0; i < spwmc->chip.npwm; i++) {
		pwm = &spwmc->chip.pwms[i];
		/*
		 * disable PWM which is not disabled when user suspend
		 */
		if (test_bit(PWMF_REQUESTED, &pwm->flags) &&
				test_bit(PWMF_ENABLED, &pwm->flags))
			sirf_pwm_disable(pwm->chip, pwm);
	}

	return 0;
}

static int sirf_pwm_resume(struct device *dev)
{
	struct sirf_pwm_chip *spwmc = dev_get_drvdata(dev);
	struct pwm_device *pwm;
	struct sirf_pwm *spwm;
	int i;

	for (i = 0; i < spwmc->chip.npwm; i++) {
		pwm = &spwmc->chip.pwms[i];
		spwm = pwm_get_chip_data(pwm);

		if (test_bit(PWMF_REQUESTED, &pwm->flags) &&
				test_bit(PWMF_ENABLED, &pwm->flags)) {
			sirf_pwm_config(&spwmc->chip, pwm, spwm->duty_ns,
					pwm_get_period(pwm));
			sirf_pwm_enable(&spwmc->chip, pwm);
		}
	}

	return 0;
}

static int sirf_pwm_restore(struct device *dev)
{
	struct sirf_pwm_chip *spwmc = dev_get_drvdata(dev);
	struct pwm_device *pwm;
	struct sirf_pwm *spwm;
	int i;

	for (i = 0; i < spwmc->chip.npwm; i++) {
		pwm = &spwmc->chip.pwms[i];
		spwm = pwm_get_chip_data(pwm);
		/*
		 * while restoring from hibernation, state of PWM is enabled,
		 * but PWM hardware is not re-enabled, register about config
		 * and enable should be restored here
		 */
		if (test_bit(PWMF_REQUESTED, &pwm->flags) &&
				test_bit(PWMF_ENABLED, &pwm->flags)) {
			_sirf_pwm_hwconfig(&spwmc->chip, pwm);
			_sirf_pwm_hwenable(&spwmc->chip, pwm);
		}
	}

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
		.name = "sirf-pwm",
		.pm = &sirf_pwm_pm_ops,
		.of_match_table = sirf_pwm_of_match,
	},
	.probe = sirf_pwm_probe,
	.remove = sirf_pwm_remove,
};
module_platform_driver(sirf_pwm_driver);

MODULE_DESCRIPTION("SIRF serial SoC PWM device core driver");
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
MODULE_AUTHOR("Huayi Li <huayi.li@csr.com>");
MODULE_LICENSE("GPL v2");
