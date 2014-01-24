/*
* CSR Synergy for Linux Bluetooth and WLAN Enable Driver
*
* Copyright (c) 2013 Cambridge Silicon Radio Limited, a CSR plc group company.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/rfkill.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/pwm.h>
#include <linux/slab.h>

struct rfkill_gpio_data {
	struct rfkill *rfkill_dev;
	struct pwm_device *pwm;
	int power_gpio;
	int reset_gpio;
	int power_number;
};

static int rfkill_gpio_set_power(void *data, bool blocked)
{
	struct rfkill_gpio_data *rfkill = data;

	if (blocked) {
		if (rfkill->power_number <= 0)
			return 0;

		if (rfkill->power_number > 1) {
			pr_debug("%s: power_number is %d\n "
			       , __func__, 1);
			pr_debug("%s: decrease to %d and return\n "
			       , __func__, rfkill->power_number - 1);
			rfkill->power_number--;
			/*some other apps need power, just return */
			return 0;
		}

		if (gpio_is_valid(rfkill->power_gpio)) {
			pr_debug("%s: power off\n", __func__);
			gpio_direction_output(rfkill->power_gpio, 0);
			rfkill->power_number--;
		}

		pwm_disable(rfkill->pwm);
	} else {
		rfkill->power_number++;
		if (rfkill->power_number > 1) {
			pr_debug("%s: power already on, rfkill->power_number:%d\n",
			       __func__, rfkill->power_number);
			return 0;	/*power already on, just return */
		}

		if (gpio_is_valid(rfkill->power_gpio)) {
			pr_debug("%s: power on\n", __func__);
			gpio_direction_output(rfkill->power_gpio, 1);
		}

		pwm_enable(rfkill->pwm);

		msleep(20);

		if (gpio_is_valid(rfkill->reset_gpio)) {

			pr_debug("%s:reset\n", __func__);
			gpio_set_value(rfkill->reset_gpio, 0);

			msleep(60);

			gpio_direction_output(rfkill->reset_gpio, 1);
		}
	}

	return 0;
}

static const struct rfkill_ops rfkill_gpio_ops = {
	.set_block = rfkill_gpio_set_power,
};

static int bt_csr_probe(struct platform_device *pdev)
{

	struct rfkill_gpio_data *rfkill = NULL;
	struct device_node *dn = pdev->dev.of_node;

	int ret = 0;

	pr_debug("%s\n", __func__);
	rfkill = kzalloc(sizeof(*rfkill), GFP_KERNEL);
	if (!rfkill) {

		ret = -ENOMEM;
		goto fail_alloc;
	}

	rfkill->power_gpio = of_get_named_gpio(dn, "bt_gpio_power", 0);
	rfkill->reset_gpio = of_get_named_gpio(dn, "bt_gpio_reset", 0);

	if (gpio_is_valid(rfkill->power_gpio)) {
		pr_debug("%s: request power gpio\n", __func__);
		ret = gpio_request(rfkill->power_gpio, "bt power gpio");
		if (ret) {
			pr_warn("%s: failed to get power gpio.\n", __func__);
			goto fail_power;
		}
	}

	if (gpio_is_valid(rfkill->reset_gpio)) {
		pr_debug("%s: request reset gpio\n", __func__);
		ret = gpio_request(rfkill->reset_gpio, "bt reset gpio");
		if (ret) {
			pr_warn("%s: failed to get reset gpio.\n", __func__);
			goto fail_reset;
		}
	}

	rfkill->pwm = devm_pwm_get(&pdev->dev, NULL);
	if (IS_ERR(rfkill->pwm)) {
		dev_err(&pdev->dev, "unable to request PWM\n");
		ret = PTR_ERR(rfkill->pwm);
		goto fail_reset;
	}

	pwm_config(rfkill->pwm, 0, rfkill->pwm->period);
	pwm_enable(rfkill->pwm);

	rfkill->rfkill_dev = rfkill_alloc("csrbt-8311", &pdev->dev,
					  RFKILL_TYPE_BLUETOOTH,
					  &rfkill_gpio_ops, rfkill);
	if (!rfkill->rfkill_dev) {
		ret = -ENOMEM;
		goto fail_reset;
	}

	ret = rfkill_register(rfkill->rfkill_dev);
	if (ret < 0)
		goto fail_rfkill;

	platform_set_drvdata(pdev, rfkill);

	return 0;

fail_rfkill:
	rfkill_destroy(rfkill->rfkill_dev);
fail_reset:
	if (gpio_is_valid(rfkill->reset_gpio))
		gpio_free(rfkill->reset_gpio);
fail_power:
	if (gpio_is_valid(rfkill->power_gpio))
		gpio_free(rfkill->power_gpio);
fail_alloc:
	kfree(rfkill);

	return ret;
}

static int bt_csr_remove(struct platform_device *pdev)
{
	struct rfkill_gpio_data *rfkill = platform_get_drvdata(pdev);

	rfkill_unregister(rfkill->rfkill_dev);
	rfkill_destroy(rfkill->rfkill_dev);
	if (gpio_is_valid(rfkill->power_gpio))
		gpio_free(rfkill->power_gpio);
	if (gpio_is_valid(rfkill->reset_gpio))
		gpio_free(rfkill->reset_gpio);

	pwm_disable(rfkill->pwm);
	pwm_free(rfkill->pwm);

	kfree(rfkill);

	return 0;
}

static const struct of_device_id bt_csr_of_match[] = {
	{.compatible = "csr,bt-8311",},
	{},
};

MODULE_DEVICE_TABLE(of, bt_csr_of_match);

static struct platform_driver bt_csr_driver = {
	.driver = {
		   .name = "bt-csr-rfkill",
		   .owner = THIS_MODULE,
		   .of_match_table = bt_csr_of_match,
		   },
	.probe = bt_csr_probe,
	.remove = bt_csr_remove,
};

module_platform_driver(bt_csr_driver);

MODULE_AUTHOR("Xingmin Guo <xingmin.guo@csr.com>");
MODULE_DESCRIPTION("CSR Synergy for Linux Bluetooth and WLAN Enable Driver");
MODULE_LICENSE("GPL");
