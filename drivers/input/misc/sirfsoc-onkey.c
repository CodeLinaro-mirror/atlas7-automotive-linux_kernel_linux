/*
 * Power key driver for SiRF PrimaII
 *
 * Copyright (c) 2013-2016, The Linux Foundation. All rights reserved.
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

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/regmap.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>
#include <linux/mfd/core.h>
#include <linux/mfd/sirfsoc_pwrc.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/workqueue.h>

struct sirfsoc_onkey_info {
	struct device *dev;
	struct regmap *regmap;
	struct sirfsoc_pwrc_register *pwrc_reg;
	struct input_dev	*input;
	u32 base;
	int virq;
	int exton_virq;
};

#define PWRC_KEY_DETECT_UP_TIME		20	/* ms*/

static int sirfsoc_onkey_down(struct sirfsoc_onkey_info *info)
{
	struct sirfsoc_pwrc_register *pwrc = info->pwrc_reg;
	u32 state;

	sirfsoc_iobg_lock();
	regmap_read(info->regmap,
					info->base +
					pwrc->pwrc_pin_status,
					&state);
	sirfsoc_iobg_unlock();
	/* active low for onkey, but active high for ext_onkey*/
	return !(state & BIT(PWRC_IRQ_ONKEY)) ||
		(state & BIT(PWRC_IRQ_EXT_ONKEY));
}

static irqreturn_t sirfsoc_onkey_handler(int irq, void *dev_id)
{
	struct sirfsoc_onkey_info *info = dev_id;

	if (!sirfsoc_onkey_down(info))
		return IRQ_NONE;

	input_event(info->input, EV_KEY, KEY_POWER, 1);
	input_sync(info->input);

	/* poll key-up since key-up has no interrupt */
	do {
		msleep(PWRC_KEY_DETECT_UP_TIME);
	} while (sirfsoc_onkey_down(info));

	input_event(info->input, EV_KEY, KEY_POWER, 0);
	input_sync(info->input);

	return IRQ_HANDLED;
}

static int sirfsoc_onkey_open(struct input_dev *input)
{
	struct sirfsoc_onkey_info *info = input_get_drvdata(input);

	enable_irq(info->virq);
	enable_irq(info->exton_virq);
	return 0;
}

static void sirfsoc_onkey_close(struct input_dev *input)
{
	struct sirfsoc_onkey_info *info = input_get_drvdata(input);

	disable_irq(info->virq);
	disable_irq(info->exton_virq);
}

static const struct of_device_id sirfsoc_onkey_of_match[] = {
	{ .compatible = "sirf,prima2-onkey" },
	{},
}
MODULE_DEVICE_TABLE(of, sirfsoc_onkey_of_match);


static int sirfsoc_onkey_probe(struct platform_device *pdev)
{
	struct sirfsoc_pwrc_info *pwrcinfo = dev_get_drvdata(pdev->dev.parent);
	struct sirfsoc_onkey_info *info;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	info->pwrc_reg = pwrcinfo->pwrc_reg;
	info->regmap  = pwrcinfo->regmap;
	info->base  = pwrcinfo->base;

	if (!info->regmap) {
		dev_err(&pdev->dev,
			"no regmap from parent mfd, should never happen\n");
		ret = -ENXIO;
		goto err;
	}

	info->input = devm_input_allocate_device(&pdev->dev);
	if (!info->input)
		return -ENOMEM;

	info->input->name = "sirfsoc pwrckey";
	info->input->phys = "pwrc/input0";
	info->input->evbit[0] = BIT_MASK(EV_KEY);
	input_set_capability(info->input, EV_KEY, KEY_POWER);
	info->input->open = sirfsoc_onkey_open;
	info->input->close = sirfsoc_onkey_close;
	input_set_drvdata(info->input, info);

	info->virq = of_irq_get(pdev->dev.of_node, 0);
	if (info->virq <= 0) {
		dev_info(&pdev->dev,
			"Unable to find IRQ for onkey. err=%d\n", info->virq);
		ret = -ENXIO;
		goto err;
	}
	irq_set_status_flags(info->virq, IRQ_NOAUTOEN);
	ret = request_threaded_irq(info->virq, NULL, sirfsoc_onkey_handler,
					    IRQF_ONESHOT, "onkey", info);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request IRQ: #%d: %d\n",
			info->virq, ret);
		goto err;
	}

	info->exton_virq = of_irq_get(pdev->dev.of_node, 1);
	if (info->exton_virq <= 0) {
		dev_info(&pdev->dev,
			"Unable to find IRQ for exton_key. ret=%d\n",
			info->exton_virq);
		ret = -ENXIO;
		goto err;
	}
	irq_set_status_flags(info->exton_virq,
			IRQ_NOAUTOEN);
	ret = request_threaded_irq(info->exton_virq, NULL,
			sirfsoc_onkey_handler,
			IRQF_ONESHOT, "ext_onkey", info);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request IRQ: #%d: %d\n",
			info->virq, ret);
		goto err;
	}

	ret = input_register_device(info->input);
	if (ret) {
		dev_err(&pdev->dev,
			"unable to register input device, error: %d\n",
			ret);
		goto err;
	}

	dev_set_drvdata(&pdev->dev, info);
	device_init_wakeup(&pdev->dev, 1);
	return 0;
err:
	return ret;

}


static int sirfsoc_onkey_remove(struct platform_device *pdev)
{
	device_init_wakeup(&pdev->dev, 0);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sirfsoc_onkey_resume(struct device *dev)
{
	struct sirfsoc_onkey_info *info = dev_get_drvdata(dev);
	struct input_dev *input = info->input;

	mutex_lock(&input->mutex);
	if (input->users)
		enable_irq(info->virq);

	mutex_unlock(&input->mutex);

#ifdef CONFIG_ANDROID
	/*
	 * For android suspend/resume, after resume back, a POWER
	 * key event is needed by power management to set related
	 * flag to block "autosleep"
	 */
	input_event(info->input, EV_KEY, KEY_POWER, 1);
	input_sync(info->input);
	input_event(info->input, EV_KEY, KEY_POWER, 0);
	input_sync(info->input);
#endif

	return 0;
}


static int sirfsoc_onkey_supend(struct device *dev)
{
	struct sirfsoc_onkey_info *info = dev_get_drvdata(dev);
	struct input_dev *input = info->input;

	mutex_lock(&input->mutex);
	if (input->users)
		disable_irq(info->virq);

	mutex_unlock(&input->mutex);
	return 0;
}

#endif
static SIMPLE_DEV_PM_OPS(sirfsoc_onkey_pm_ops, sirfsoc_onkey_supend,
				sirfsoc_onkey_resume);

static struct platform_driver sirfsoc_onkey_driver = {
	.probe		= sirfsoc_onkey_probe,
	.remove		= sirfsoc_onkey_remove,
	.driver		= {
		.name	= "onkey",
		.owner	= THIS_MODULE,
		.pm	= &sirfsoc_onkey_pm_ops,
		.of_match_table = sirfsoc_onkey_of_match,
	}
};

module_platform_driver(sirfsoc_onkey_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("CSR Prima2 onkey Driver");
MODULE_ALIAS("platform:onkey");
