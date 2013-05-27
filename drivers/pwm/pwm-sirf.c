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
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/pwm.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>

#include "pwm-sirf.h"

#ifdef CONFIG_SIRF_PWM_DEBUG
static struct device *dev;
#define debug_info(x...) dev_info(dev, x)
#else
#define debug_info(x...)
#endif

#define PWM_NUM 7
#define PWM_BLS_GROUP_NUM		16

struct bklscaling_config {
	unsigned int duty_ns;
	unsigned int period_ns;
};

struct sirf_pwm {
	void __iomem                *base;
	struct clk                  *clk;
	struct pinctrl              *p[PWM_NUM];
	struct pwm_chip             chip;
	int                         duty_ns[PWM_NUM];
	int                         src_clk_id[PWM_NUM];
	int                         trans_mode[PWM_NUM];
	unsigned int                trans_process_step[PWM_NUM];
	unsigned int                trans_process_time[PWM_NUM];
	int                         pwm3_use_bklscaling;
	struct bklscaling_config    bklscaling_para[PWM_BLS_GROUP_NUM];

};

#define to_sirf_chip(chip)	container_of(chip, struct sirf_pwm, chip)

struct pwm_device *sirf_of_pwm_xlate_with_flags(struct pwm_chip *chip,
		const struct of_phandle_args *args)
{
	struct pwm_device *pwm;
	struct sirf_pwm *spwm = to_sirf_chip(chip);
	debug_info("of_pwm_n_cells = %d, pwm_id = %d\n",
			chip->of_pwm_n_cells, args->args[0]);
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

	debug_info("period = %d, duty_ns = %d, src_clk_id = %d\n",
			pwm->period, spwm->duty_ns[pwm->hwpwm],
			spwm->src_clk_id[pwm->hwpwm]);

	return pwm;
}

int sirf_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	int ret;
	int hwpwm = pwm->hwpwm;
	struct sirf_pwm *spwm = to_sirf_chip(chip);
	char pwm_pin_name[8];

	if (hwpwm >= PWM_NUM) {
		dev_err(chip->dev, "Not support pwm%d\n", hwpwm);
		return -EINVAL;
	}
	/*Because the PWM6 used by I2S interface internal, So we not
	 *need get the pin via pinctrl interface.
	 */
	if (hwpwm == 6)
		return 0;
	sprintf(pwm_pin_name, "pwm%d", hwpwm);
	spwm->p[hwpwm] = pinctrl_get_select(chip->dev, pwm_pin_name);
	ret = IS_ERR(spwm->p[hwpwm]);
	if (ret) {
		dev_err(chip->dev, "Get %s pin failed.\n", pwm_pin_name);
		return ret;
	}
	return 0;
}

void sirf_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
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

	clk = devm_clk_get(chip->dev,
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

	cycle = dividend & 0xFFFFFFFF;

	if (cycle < 1)
		cycle = 1;
	debug_info("time_ns = %d, cycle = %d\n",
			time_ns, cycle);
	return cycle;
}

static void sirf_get_params_from_np(struct pwm_chip *chip,
		struct pwm_device *pwm)
{
	struct sirf_pwm *spwm = to_sirf_chip(chip);
	struct device_node *np = pwm->user_dev_np;
	int ret;
	u32 trans_mode_params[2];
	u32 bklscaling_params[PWM_BLS_GROUP_NUM * 2];
	ret = of_property_read_u32(np, "sirf-pwm-transfer-mode",
			&(spwm->trans_mode[pwm->hwpwm]));
	if (ret) {
		/*Directly mode*/
		debug_info("SiRF PWM used directly-mode\n");
		spwm->trans_mode[pwm->hwpwm] = 0;
	}
	if (spwm->trans_mode[pwm->hwpwm]) {
		/*Step mode*/
		debug_info("SiRF PWM used step-mode\n");
		ret = of_property_read_u32_array(np,
				"sirf-pwm-step-mode-params",
				trans_mode_params, 2);
		if (ret)
			trans_mode_params[0] = trans_mode_params[1] = 0;
		spwm->trans_process_step[pwm->hwpwm] = trans_mode_params[0];
		spwm->trans_process_time[pwm->hwpwm] = trans_mode_params[1];
		debug_info("Step mode, trans_step = %d, trans_time = %d\n",
				spwm->trans_process_step[pwm->hwpwm],
				spwm->trans_process_time[pwm->hwpwm]);
	}

	/*Only PWM3 can use bklscaling mode*/
	if (pwm->hwpwm != 3)
		return;
	ret = of_property_read_u32(np, "sirf-pwm-bklscaling-mode",
			&(spwm->pwm3_use_bklscaling));
	if (ret)
		spwm->pwm3_use_bklscaling = 0;
	if (spwm->pwm3_use_bklscaling) {
		/*bklscaling mode*/
		int i;
		debug_info("SiRF PWM used bklscaing mode\n");
		ret = of_property_read_u32_array(np,
				"sirf-pwm-bklscaling-params",
				bklscaling_params, PWM_BLS_GROUP_NUM * 2);
		if (ret)
			memset(bklscaling_params, 0, sizeof(u32) * PWM_BLS_GROUP_NUM * 2);
		for (i = 0; i < PWM_BLS_GROUP_NUM; i++) {
			spwm->bklscaling_para[i].period_ns = bklscaling_params[i * 2];
			spwm->bklscaling_para[i].duty_ns = bklscaling_params[i * 2 + 1];
		}
#ifdef CONFIG_SIRF_PWM_DEBUG
		debug_info("PWM3 set bklscaling mode\n");
		for (i = 0; i < PWM_BLS_GROUP_NUM; i++)
			debug_info("bklscaling_para: %d: period_ns = %d, duty_ns = %d\n",
					i, spwm->bklscaling_para[i].period_ns,
					spwm->bklscaling_para[i].duty_ns);
#endif
	}
}

int sirf_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
		int duty_ns, int period_ns)
{
	unsigned int period_cycles, period_high, period_low;
	unsigned int step_value, step_hold;
	unsigned int val;
	struct sirf_pwm *spwm = to_sirf_chip(chip);

	if (pwm == NULL) {
		dev_err(chip->dev, "pwm config error: no pwm device!\n");
		return -EINVAL;
	}
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
		if (spwm->trans_mode[pwm->hwpwm]) {
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
	sirf_get_params_from_np(chip, pwm);
	return 0;
}

int sirf_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	int i;
	unsigned int val;
	unsigned int cycle, high, low;
	struct sirf_pwm *spwm = to_sirf_chip(chip);

	debug_info("%s: lable = %s\n", __func__, pwm->label);
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
	val |= (!(spwm->trans_mode[pwm->hwpwm]) <<
			(pwm->hwpwm + TRANS_MODE_SELECT_BIT));
	if (pwm->hwpwm == 3) {
		if (spwm->pwm3_use_bklscaling) {
			val |= (1 << LOOK_TABLE_EN_BIT);
			for (i = 0; i < PWM_BLS_GROUP_NUM; i++) {
				cycle = time_to_cycle(chip, pwm,
						spwm->bklscaling_para[i].period_ns);
				high = time_to_cycle(chip, pwm,
						spwm->bklscaling_para[i].duty_ns);
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

void sirf_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
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
#ifdef CONFIG_SIRF_PWM_DEBUG
	dev = &pdev->dev;
#endif
	spwm = devm_kzalloc(&pdev->dev, sizeof(struct sirf_pwm),
			GFP_KERNEL);
	if (spwm == NULL)
		return -ENOMEM;
	platform_set_drvdata(pdev, spwm);

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_res) {
		dev_err(&pdev->dev, "Unable to get IO resource\n");
		ret = -ENODEV;
		goto err_free_spwm;
	}
	spwm->base = devm_ioremap_resource(&pdev->dev, mem_res);
	if (spwm->base == NULL) {
		ret = -ENOMEM;
		goto err_free_spwm;
	}
	spwm->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(spwm->clk)) {
		dev_err(&pdev->dev, "Get clock failed.\n");
		ret = PTR_ERR(spwm->clk);
		goto err_free_spwm;
	}
	clk_prepare_enable(spwm->clk);

	spwm->chip.dev = &pdev->dev;
	spwm->chip.ops = &sirf_pwm_ops;
	spwm->chip.base = 0;
	spwm->chip.npwm = PWM_NUM;
	spwm->chip.of_xlate = sirf_of_pwm_xlate_with_flags;
	spwm->chip.of_pwm_n_cells = 4;

	ret = pwmchip_add(&spwm->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register pwm\n");
		goto err_free_clk;
	}
	return 0;
err_free_clk:
	clk_disable_unprepare(spwm->clk);
err_free_spwm:
	devm_kfree(&pdev->dev, spwm);
	return ret;
}

static int sirf_pwm_remove(struct platform_device *pdev)
{
	struct sirf_pwm *spwm;
#ifdef CONFIG_SIRF_PWM_DEBUG
	dev = NULL;
#endif
	spwm = platform_get_drvdata(pdev);
	clk_disable_unprepare(spwm->clk);
	clk_put(spwm->clk);
	devm_kfree(&pdev->dev, spwm);
	return 0;
}

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
};

module_platform_driver(sirf_pwm_driver);

MODULE_DESCRIPTION("SIRF serial SoC PWM device core driver");
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
MODULE_LICENSE("GPL v2");
