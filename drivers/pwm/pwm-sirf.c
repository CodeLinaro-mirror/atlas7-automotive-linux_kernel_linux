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
#include <linux/pinctrl/consumer.h>
#include <linux/io.h>

#include "pwm-sirf.h"

#define SIRF_PWM_CHL_NUM		7
#define SIRF_PWM_BLS_GRP_NUM		16

/* PWM6 is an internal channel dedicated to as the source of I2S MCLK */
#define SIRF_PWM_I2S_CHL		6

/* PWM3 supports black light scaling */
#define SIRF_PWM_BKS_CHL		3

struct bklscaling_cfg {
	unsigned int duty_ns;
	unsigned int period_ns;
};

struct sirf_pwm {
	void __iomem		*base;
	struct clk		*clk;
	struct pinctrl		*p[SIRF_PWM_CHL_NUM];
	struct pwm_chip		chip;
	int			duty_ns[SIRF_PWM_CHL_NUM];
	int			src_clk_id[SIRF_PWM_CHL_NUM];
	bool			is_step_mode[SIRF_PWM_CHL_NUM];
	unsigned int		trans_process_step[SIRF_PWM_CHL_NUM];
	unsigned int		trans_process_time[SIRF_PWM_CHL_NUM];
	bool			is_pwm3_use_bks;
	struct bklscaling_cfg	bcfg[SIRF_PWM_BLS_GRP_NUM];
};

#define to_sirf_chip(chip)	container_of(chip, struct sirf_pwm, chip)

static struct pwm_device *sirf_of_pwm_xlate_with_flags(struct pwm_chip *chip,
		const struct of_phandle_args *args)
{
	struct pwm_device *pwm;
	struct sirf_pwm *spwm = to_sirf_chip(chip);

	if (chip->of_pwm_n_cells < 4)
		return ERR_PTR(-EINVAL);

	if (args->args[0] >= chip->npwm)
		return ERR_PTR(-EINVAL);

	pwm = pwm_request_from_chip(chip, args->args[0], NULL);
	if (IS_ERR(pwm))
		return pwm;

	pwm_set_period(pwm, args->args[1]);

	spwm->duty_ns[pwm->hwpwm] = args->args[2];

	spwm->src_clk_id[pwm->hwpwm] = args->args[3];

	return pwm;
}

static int sirf_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	int hwpwm = pwm->hwpwm;
	struct sirf_pwm *spwm = to_sirf_chip(chip);
#define PIN_NAME_LEN	8
	char pins[PIN_NAME_LEN];
	int ret;

	if (hwpwm >= SIRF_PWM_CHL_NUM) {
		dev_err(chip->dev, "Not support pwm%d\n", hwpwm);
		return -EINVAL;
	}

	if (hwpwm == SIRF_PWM_I2S_CHL)
		return 0;

	snprintf(pins, PIN_NAME_LEN, "pwm%d", hwpwm);
	spwm->p[hwpwm] = pinctrl_get_select(chip->dev, pins);
	ret = IS_ERR(spwm->p[hwpwm]);
	if (ret) {
		dev_err(chip->dev, "Get %s pin failed.\n", pins);
		return ret;
	}

	return 0;
}

static void sirf_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct sirf_pwm *spwm = to_sirf_chip(chip);
	pinctrl_put(spwm->p[pwm->hwpwm]);
}

static u32 sirf_get_in_cycles_ps(struct pwm_chip *chip,
		struct pwm_device *pwm)
{
	const char *clk_name[] = {"osc", "pll1", "pll2", "rtc", "pll3"};
	struct sirf_pwm *spwm = to_sirf_chip(chip);
	struct clk *clk;
	u32 rate;

	BUG_ON(spwm->src_clk_id[pwm->hwpwm] >= ARRAY_SIZE(clk_name));

	clk = clk_get(chip->dev,
			clk_name[spwm->src_clk_id[pwm->hwpwm]]);

	BUG_ON(IS_ERR(clk));

	rate = clk_get_rate(clk);
	clk_put(clk);
	return rate;
}

static unsigned int time_to_cycle(struct pwm_chip *chip,
		struct pwm_device *pwm, unsigned int time_ns)
{
	u64 src_clk;
	unsigned int cycle;
	u64 dividend;

	src_clk = (u64) sirf_get_in_cycles_ps(chip, pwm);
	dividend = (src_clk * time_ns + NSEC_PER_SEC / 2);
	do_div(dividend, NSEC_PER_SEC);

	cycle = dividend & 0xFFFFFFFFUL;

	return cycle < 1 ? cycle : 1;
}

static void sirf_pwm_get_cfg_from_user(struct pwm_chip *chip,
		struct pwm_device *pwm)
{
	struct sirf_pwm *spwm = to_sirf_chip(chip);
	struct device_node *np = pwm->user_dev_np;
	int ret;
	u32 trans_mode_params[2];
	u32 bcfg_params[SIRF_PWM_BLS_GRP_NUM * 2];

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

static int sirf_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
		int duty_ns, int period_ns)
{
	unsigned int period_cycles, period_high, period_low;
	unsigned int step_value, step_hold;
	unsigned int val;
	struct sirf_pwm *spwm = to_sirf_chip(chip);

	if (duty_ns > period_ns) {
		dev_err(chip->dev, "pwm config error: duty_ns > period_ns\n");
		return -EINVAL;
	}

	period_cycles = time_to_cycle(chip, pwm, period_ns);
	if (period_cycles == 1) {
		dev_err(chip->dev, "pwm config warning: period_ns is too short!"
				" bypass this channel!\n");
	}

	period_high = time_to_cycle(chip, pwm, duty_ns);
	period_low = period_cycles - period_high;

	if (period_cycles == 1) {
		/* bypass mode */
		val = readl(spwm->base + PWM_SELECT_PRECLK);
		val |= (0x1 << (BYPASS_MODE_BIT + pwm->hwpwm));
		writel(val, spwm->base + PWM_SELECT_PRECLK);
	} else {
		/* divder mode */
		val = readl(spwm->base + PWM_SELECT_PRECLK);
		val &= ~(0x1 << (BYPASS_MODE_BIT + pwm->hwpwm));
		writel(val, spwm->base + PWM_SELECT_PRECLK);

		if (period_high < 1) {
			dev_err(chip->dev, "pwm config error: invalid duty,"
					"lowest one is %d\n",
					period_high * 100 / period_cycles);
			period_high = 1;
			period_low = period_cycles - period_high;
		}
		if (period_high == period_cycles) {
			period_high--;
			period_low = 1;
		}
		if (spwm->is_step_mode[pwm->hwpwm]) {
			step_value = ((spwm->duty_ns[pwm->hwpwm] > duty_ns) ?
					(spwm->duty_ns[pwm->hwpwm] - duty_ns) :
					(duty_ns - spwm->duty_ns[pwm->hwpwm])) / spwm->trans_process_step[pwm->hwpwm];
			step_value = time_to_cycle(chip, pwm, step_value);
			step_hold = time_to_cycle(chip, pwm, spwm->trans_process_time[pwm->hwpwm]);

			writel(step_value, spwm->base + PWM_TR_STEP(pwm->hwpwm));
			writel(step_hold, spwm->base + PWM_STEP_HOLD(pwm->hwpwm));
			period_high--;
			period_low--;
		}

		writel(period_high, (spwm->base + PWM_GET_WAIT_OFFSET(pwm->hwpwm)));
		writel(period_low, (spwm->base + PWM_GET_HOLD_OFFSET(pwm->hwpwm)));
	}

	spwm->duty_ns[pwm->hwpwm] = duty_ns;
	pwm_set_period(pwm, period_ns);
	sirf_pwm_get_cfg_from_user(chip, pwm);

	return 0;
}

static int sirf_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	int i;
	unsigned int val;
	unsigned int cycle, high, low;
	struct sirf_pwm *spwm = to_sirf_chip(chip);

	sirf_pwm_config(chip, pwm, spwm->duty_ns[pwm->hwpwm], pwm->period);
	/* disable preclock */
	val = readl(spwm->base + PWM_ENABLE_PRECLOCK);
	val &= ~(1 << pwm->hwpwm);
	writel(val, spwm->base + PWM_ENABLE_PRECLOCK);

	/* select preclock source must after disable preclk*/
	val = readl(spwm->base + PWM_SELECT_PRECLK);
	val &= ~(0x7 << (PWM_SRC_FIELD_LEN * pwm->hwpwm));
	val |= (spwm->src_clk_id[pwm->hwpwm] << (PWM_SRC_FIELD_LEN * pwm->hwpwm));
	writel(val, spwm->base + PWM_SELECT_PRECLK);
	/* wait for some time */
	udelay(100);

	/* enable preclock */
	val = readl(spwm->base + PWM_ENABLE_PRECLOCK);
	val |= (1 << pwm->hwpwm);
	writel(val, spwm->base + PWM_ENABLE_PRECLOCK);

	/* enable post clock*/
	val = readl(spwm->base + PWM_ENABLE_POSTCLOCK);
	val |= (1 << pwm->hwpwm);
	writel(val, spwm->base + PWM_ENABLE_POSTCLOCK);

	/* enable output */
	val = readl(spwm->base + PWM_OE);
	val |= (1 << pwm->hwpwm);
	val &= ~(1 << (pwm->hwpwm + TRANS_MODE_SELECT_BIT));
	val |= (!(spwm->is_step_mode[pwm->hwpwm]) <<
			(pwm->hwpwm + TRANS_MODE_SELECT_BIT));

	if (pwm->hwpwm == SIRF_PWM_BKS_CHL) {
		if (spwm->is_pwm3_use_bks) {
			val |= (1 << LOOK_TABLE_EN_BIT);
			for (i = 0; i < SIRF_PWM_BLS_GRP_NUM; i++) {
				cycle = time_to_cycle(chip, pwm,
						spwm->bcfg[i].period_ns);
				high = time_to_cycle(chip, pwm,
						spwm->bcfg[i].duty_ns);
				low = cycle - high;
				if (cycle == 1) {
					dev_info(spwm->chip.dev, "pwm scaling config warning:"
							"period_ns is too short!\n");
					high = 2;
					low = 2;
				}
				writel(high - 1, spwm->base + PWM_WAIT3(i));
				writel(low - 1, spwm->base + PWM_HOLD3(i));
			}
		} else {
			val &= ~(1 << LOOK_TABLE_EN_BIT);
		}
	}

	writel(val, spwm->base + PWM_OE);

	return 0;
}

static void sirf_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	unsigned int val;
	struct sirf_pwm *spwm = to_sirf_chip(chip);
	/* disable output */
	val = readl(spwm->base + PWM_OE);
	val &= ~(1 << pwm->hwpwm);
	writel(val, spwm->base + PWM_OE);

	/* disable postclock */
	val = readl(spwm->base + PWM_ENABLE_POSTCLOCK);
	val &= ~(1 << pwm->hwpwm);
	writel(val, spwm->base + PWM_ENABLE_POSTCLOCK);

	/* disable preclock */
	val = readl(spwm->base + PWM_ENABLE_PRECLOCK);
	val &= ~(1 << pwm->hwpwm);
	writel(val, spwm->base + PWM_ENABLE_PRECLOCK);
}

static struct pwm_ops sirf_pwm_ops = {
	.request = sirf_pwm_request,
	.free = sirf_pwm_free,
	.enable = sirf_pwm_enable,
	.disable = sirf_pwm_disable,
	.config = sirf_pwm_config,
	.owner = THIS_MODULE,
};

static int sirf_pwm_probe(struct platform_device *pdev)
{
	struct sirf_pwm *spwm;
	struct resource *mem_res;
	int ret = 0;

	spwm = devm_kzalloc(&pdev->dev, sizeof(struct sirf_pwm),
			GFP_KERNEL);
	if (spwm == NULL)
		return -ENOMEM;
	platform_set_drvdata(pdev, spwm);

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_res) {
		dev_err(&pdev->dev, "Unable to get IO resource\n");
		return -ENODEV;
	}
	spwm->base = devm_ioremap_resource(&pdev->dev, mem_res);
	if (spwm->base == NULL) {
		return -ENOMEM;
	}
	spwm->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(spwm->clk)) {
		dev_err(&pdev->dev, "Get clock failed.\n");
		return PTR_ERR(spwm->clk);
	}

	clk_prepare_enable(spwm->clk);

	spwm->chip.dev = &pdev->dev;
	spwm->chip.ops = &sirf_pwm_ops;
	spwm->chip.base = 0;
	spwm->chip.npwm = SIRF_PWM_CHL_NUM;
	spwm->chip.of_xlate = sirf_of_pwm_xlate_with_flags;
	spwm->chip.of_pwm_n_cells = 4;

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
	struct sirf_pwm *spwm;

	spwm = platform_get_drvdata(pdev);
	clk_disable_unprepare(spwm->clk);
	clk_put(spwm->clk);
	return 0;
}

#ifdef CONFIG_PM
static int sirf_pwm_suspend(struct platform_device *pdev,
		pm_message_t state)
{
	struct sirf_pwm *spwm = platform_get_drvdata(pdev);
	clk_disable_unprepare(spwm->clk);
	return 0;
}

static int sirf_pwm_resume(struct platform_device *pdev)
{
	struct sirf_pwm *spwm = platform_get_drvdata(pdev);
	clk_prepare_enable(spwm->clk);
	return 0;
}
#else
#define sirf_pwm_resume NULL
#define sirf_pwm_suspend NULL
#endif

static const struct of_device_id sirf_pwm_of_match[] = {
	{ .compatible = "sirf,prima2-pwm", },
	{}
};
MODULE_DEVICE_TABLE(of, sirf_pwm_of_match);

static struct platform_driver sirf_pwm_driver = {
	.driver = {
		.name = "prima2-pwm",
		.owner = THIS_MODULE,
		.of_match_table = sirf_pwm_of_match,
	},
	.probe = sirf_pwm_probe,
	.remove = sirf_pwm_remove,
	.suspend = sirf_pwm_suspend,
	.resume = sirf_pwm_resume,
};

module_platform_driver(sirf_pwm_driver);

MODULE_DESCRIPTION("SIRF serial SoC PWM device core driver");
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
MODULE_LICENSE("GPL v2");
