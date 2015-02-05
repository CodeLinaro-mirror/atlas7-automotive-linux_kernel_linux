/*
 * Power key driver for SiRF PrimaII
 *
 * Copyright (c) 2013 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
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
	struct delayed_work	work;
	u32 base;
	int virq;
	int exton_virq;
};

#define PWRC_KEY_DETECT_UP_TIME		320	/* ms*/

static int sirfsoc_onkey_down(struct sirfsoc_onkey_info *info)
{
	struct sirfsoc_pwrc_register *pwrc = info->pwrc_reg;
	u32 state;

	regmap_read(info->regmap,
					info->base +
					pwrc->pwrc_pin_status,
					&state);
	/* active low */
	return !(state & BIT(PWRC_IRQ_ONKEY)) ||
		!(state & BIT(PWRC_IRQ_EXT_ONKEY));
}

static void sirfsoc_onkey_event(struct work_struct *work)
{
	struct sirfsoc_onkey_info *info =
		container_of(work, struct sirfsoc_onkey_info, work.work);

	/*
	* FIXME: we need to define event for EXT_ONKEY,
	* but since requirement is not clear
	* for now just report same event as ONKEY
	*/
	if (sirfsoc_onkey_down(info)) {
		schedule_delayed_work(&info->work,
			msecs_to_jiffies(PWRC_KEY_DETECT_UP_TIME));
	} else {
		input_event(info->input, EV_KEY, KEY_POWER, 0);
		input_sync(info->input);
	}
}

static irqreturn_t sirfsoc_onkey_handler(int irq, void *dev_id)
{
	struct sirfsoc_onkey_info *info = dev_id;

	input_event(info->input, EV_KEY, KEY_POWER, 1);
	input_sync(info->input);
	schedule_delayed_work(&info->work,
			      msecs_to_jiffies(PWRC_KEY_DETECT_UP_TIME));

	return IRQ_HANDLED;
}

static int sirfsoc_onkey_open(struct input_dev *input)
{
	struct sirfsoc_onkey_info *info = input_get_drvdata(input);

	enable_irq(info->virq);
	return 0;
}

static void sirfsoc_onkey_close(struct input_dev *input)
{
	struct sirfsoc_onkey_info *info = input_get_drvdata(input);

	disable_irq(info->virq);
	cancel_delayed_work_sync(&info->work);
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

	INIT_DELAYED_WORK(&info->work, sirfsoc_onkey_event);

	info->input->open = sirfsoc_onkey_open;
	info->input->close = sirfsoc_onkey_close;

	input_set_drvdata(info->input, info);

	info->virq = regmap_irq_get_virq(pwrcinfo->irq_data, PWRC_IRQ_ONKEY);

	irq_set_status_flags(info->virq, IRQ_NOAUTOEN);
	ret = request_threaded_irq(info->virq, NULL, sirfsoc_onkey_handler,
					    0, "onkey", info);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request IRQ: #%d: %d\n",
			info->virq, ret);
		goto err;
	}

	irq_set_status_flags(info->exton_virq,
			IRQ_NOAUTOEN);
	info->exton_virq = regmap_irq_get_virq(pwrcinfo->irq_data,
			PWRC_IRQ_EXT_ONKEY);

	ret = request_threaded_irq(info->exton_virq, NULL,
			sirfsoc_onkey_handler,
			0, "ext_onkey", info);

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

	/*
	 * Do not mask pwrc interrupt as we want pwrc work as a wakeup source
	 * if users touch X_ONKEY_B, see arch/arm/mach-prima2/pm.c
	 */
	mutex_lock(&input->mutex);
	if (input->users)
		enable_irq(info->virq);

	mutex_unlock(&input->mutex);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(sirfsoc_onkey_pm_ops, NULL, sirfsoc_onkey_resume);

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
MODULE_AUTHOR("Binghua Duan <Binghua.Duan@csr.com>, Xianglong Du <Xianglong.Du@csr.com>");
MODULE_DESCRIPTION("CSR Prima2 onkey Driver");
MODULE_ALIAS("platform:onkey");
