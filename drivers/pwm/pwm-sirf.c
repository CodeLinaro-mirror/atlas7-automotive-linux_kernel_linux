/*
 * SIRF serial SoC PWM device core driver
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
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

#ifdef CONFIG_PWM_SIRF_COMPLEX_MODE
/* PWM3 supports black light scaling */
#define SIRF_PWM_BKS_CHL		3
/* smart backlight*/
#define SIRF_PWM_WAIT3(n)			(0x80 + 0x8*n)
#define SIRF_PWM_HOLD3(n)			(0x84 + 0x8*n)
#define LOOK_TABLE_EN_BIT		14

struct bklscaling_cfg {
	unsigned int duty_ns;
	unsigned int period_ns;
};
#endif

struct sirf_pwm {
	void __iomem		*base;
	struct clk		*clk;
	struct pwm_chip		chip;
#ifdef CONFIG_PWM_SIRF_COMPLEX_MODE
	bool			is_step_mode[SIRF_PWM_CHL_NUM];
	unsigned int		trans_process_step[SIRF_PWM_CHL_NUM];
	unsigned int		trans_process_time[SIRF_PWM_CHL_NUM];
	bool			is_pwm3_use_bks;
	struct bklscaling_cfg	bcfg[SIRF_PWM_BLS_GRP_NUM];
#endif
};

#define to_sirf_chip(chip)	container_of(chip, struct sirf_pwm, chip)

static unsigned int sirf_pwm_ns_to_cycles(struct pwm_chip *chip, unsigned int time_ns)
{
	struct clk *clk;
	u64 dividend;
	u64 rate;
	unsigned int cycle;

	/*
	 * clock parent of pwm controller is different with pwm channel
	 * parent of pwm controller is IO, but the parent of pwm channel
	 * can be osc, pll1-pll3 and rtc, here we use pll1
	 */
	clk = clk_get(chip->dev, "pll1");
	rate = clk_get_rate(clk);
	clk_put(clk);

	dividend = rate * time_ns + NSEC_PER_SEC / 2;
	do_div(dividend, NSEC_PER_SEC);

	cycle = dividend & 0xFFFFFFFFUL;

	return cycle > 1 ? cycle : 1;
}

#ifdef CONFIG_PWM_SIRF_COMPLEX_MODE
/*
 * The SiRF SoC's PWM device has some special features.
 * Such as step mode and bklscaling mode. So if any devices
 * need use the these modes, they need write the configuration
 * in the dts file. The configuration specify mode enable/disable
 * and mode parameters.
 */
static void sirf_pwm_get_cfg_from_user(struct pwm_chip *chip,
		struct pwm_device *pwm)
{
	struct sirf_pwm *spwm = to_sirf_chip(chip);
	struct device_node *np = pwm->user_dev_np;
	int ret;
	u32 trans_mode_params[2];
	u32 bcfg_params[SIRF_PWM_BLS_GRP_NUM * 2];

	/*
	 * In step mode, If the user change the PWM output period or duty.
	 * The PWM module isn't changed directly. It will change the output
	 * step-by-step. The first specifies the change the steps and the
	 * second specifies the time interval of each steps in nanoseconds.
	 */
	spwm->is_step_mode[pwm->hwpwm] = of_property_read_bool(np, "sirf-pwm-step-mode");

	if (spwm->is_step_mode[pwm->hwpwm]) {
		/*Step mode*/
		ret = of_property_read_u32_array(np,
				"sirf-pwm-step-mode-params",
				trans_mode_params, 2);
		if (ret)
			trans_mode_params[0] = trans_mode_params[1] = 0;
		spwm->trans_process_step[pwm->hwpwm] = trans_mode_params[0];
		spwm->trans_process_time[pwm->hwpwm] = trans_mode_params[1];
	}

	if (pwm->hwpwm != SIRF_PWM_BKS_CHL)
		return;

	/*
	 * The bklscaling mode is used to support back light scaling function.
	 * Set 16 groups parameters look table is used by LCD driver.
	 * Every group includes one wait state (number of pre-clock for high
	 * level of output waveform) and one hold state (number of pre-clock
	 * for low level of output wavefrom). The  wait_hold_sel register in
	 * the LCD module can be used to choose one of them. In dts script file,
	 * these parameters specify with period and duty in nanoseconds.
	 */
	spwm->is_pwm3_use_bks = of_property_read_bool(np, "sirf-pwm-bklscaling-mode");
	if (spwm->is_pwm3_use_bks) {
		/*bklscaling mode*/
		int i;
		ret = of_property_read_u32_array(np,
				"sirf-pwm-bklscaling-params",
				bcfg_params, SIRF_PWM_BLS_GRP_NUM * 2);
		if (ret)
			memset(bcfg_params, 0, sizeof(u32) * SIRF_PWM_BLS_GRP_NUM * 2);
		for (i = 0; i < SIRF_PWM_BLS_GRP_NUM; i++) {
			spwm->bcfg[i].period_ns = bcfg_params[i * 2];
			spwm->bcfg[i].duty_ns = bcfg_params[i * 2 + 1];
		}
	}
}
#endif

static int sirf_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
		int duty_ns, int period_ns)
{
	unsigned int period_cycles, high_cycles, low_cycles;
#ifdef CONFIG_PWM_SIRF_COMPLEX_MODE
	unsigned int step_value, step_hold;
#endif
	unsigned int val;
	struct sirf_pwm *spwm = to_sirf_chip(chip);

	period_cycles = sirf_pwm_ns_to_cycles(chip, period_ns);
	if (period_cycles == 1)
		dev_warn(chip->dev, "period_ns is too short!\n");

	high_cycles = sirf_pwm_ns_to_cycles(chip, duty_ns);
	low_cycles = period_cycles - high_cycles;

	if (period_cycles == 1) {
		/* bypass mode */
		val = readl(spwm->base + SIRF_PWM_SELECT_PRECLK);
		val |= 0x1 << (BYPASS_MODE_BIT + pwm->hwpwm);
		writel(val, spwm->base + SIRF_PWM_SELECT_PRECLK);
	} else {
		/* divider mode */
		val = readl(spwm->base + SIRF_PWM_SELECT_PRECLK);
		val &= ~(0x1 << (BYPASS_MODE_BIT + pwm->hwpwm));
		writel(val, spwm->base + SIRF_PWM_SELECT_PRECLK);

		if (high_cycles == period_cycles) {
			high_cycles--;
			low_cycles = 1;
		}

#ifdef CONFIG_PWM_SIRF_COMPLEX_MODE
		if (spwm->is_step_mode[pwm->hwpwm]) {
			step_value = ((spwm->duty_ns[pwm->hwpwm] > duty_ns) ?
					(spwm->duty_ns[pwm->hwpwm] - duty_ns) :
					(duty_ns - spwm->duty_ns[pwm->hwpwm])) / spwm->trans_process_step[pwm->hwpwm];
			step_value = sirf_pwm_ns_to_cycles(chip, step_value);
			step_hold = sirf_pwm_ns_to_cycles(chip, spwm->trans_process_time[pwm->hwpwm]);

			writel(step_value, spwm->base + SIRF_PWM_TR_STEP(pwm->hwpwm));
			writel(step_hold, spwm->base + SIRF_PWM_STEP_HOLD(pwm->hwpwm));
		}
#endif

		writel(high_cycles, spwm->base + SIRF_PWM_GET_WAIT_OFFSET(pwm->hwpwm));
		writel(low_cycles, spwm->base + SIRF_PWM_GET_HOLD_OFFSET(pwm->hwpwm));
	}

	pwm_set_period(pwm, period_ns);

	return 0;
}

static int sirf_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct sirf_pwm *spwm = to_sirf_chip(chip);
	unsigned int val;

#ifdef CONFIG_PWM_SIRF_COMPLEX_MODE
	unsigned int cycle, high, low;
	int i;

	sirf_pwm_get_cfg_from_user(chip, pwm);
#endif

	/* disable preclock */
	val = readl(spwm->base + SIRF_PWM_ENABLE_PRECLOCK);
	val &= ~(1 << pwm->hwpwm);
	writel(val, spwm->base + SIRF_PWM_ENABLE_PRECLOCK);

	/* select preclock source must after disable preclk*/
	val = readl(spwm->base + SIRF_PWM_SELECT_PRECLK);
	val &= ~(0x7 << (SRC_FIELD_SIZE * pwm->hwpwm));
	val |= 1 << (SRC_FIELD_SIZE * pwm->hwpwm);
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
	val &= ~(1 << (pwm->hwpwm + TRANS_MODE_SELECT_BIT));

#ifdef CONFIG_PWM_SIRF_COMPLEX_MODE
	val |= (!(spwm->is_step_mode[pwm->hwpwm]) <<
			(pwm->hwpwm + TRANS_MODE_SELECT_BIT));

	if (pwm->hwpwm == SIRF_PWM_BKS_CHL) {
		if (spwm->is_pwm3_use_bks) {
			val |= (1 << LOOK_TABLE_EN_BIT);
			for (i = 0; i < SIRF_PWM_BLS_GRP_NUM; i++) {
				cycle = sirf_pwm_ns_to_cycles(chip, pwm,
						spwm->bcfg[i].period_ns);
				high = sirf_pwm_ns_to_cycles(chip, pwm,
						spwm->bcfg[i].duty_ns);
				low = cycle - high;
				if (cycle == 1) {
					dev_info(spwm->chip.dev, "pwm scaling config warning:"
							"period_ns is too short!\n");
					high = 2;
					low = 2;
				}
				writel(high - 1, spwm->base + SIRF_PWM_WAIT3(i));
				writel(low - 1, spwm->base + SIRF_PWM_HOLD3(i));
			}
		} else {
			val &= ~(1 << LOOK_TABLE_EN_BIT);
		}
	}
#endif

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

	spwm->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(spwm->clk)) {
		dev_err(&pdev->dev, "Get clock failed.\n");
		return PTR_ERR(spwm->clk);
	}

	clk_prepare_enable(spwm->clk);

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
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
MODULE_AUTHOR("Huayi Li <huayi.li@csr.com>");
MODULE_LICENSE("GPL v2");
