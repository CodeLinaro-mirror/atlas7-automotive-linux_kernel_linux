/*
* CSR Synergy for Linux Bluetooth and WLAN Enable Driver
*
* Copyright (c) 2014 Cambridge Silicon Radio Limited, a CSR plc group company.
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


#define GPIO_POWER_9300_PIN_NAME		"amber9300_gpio_power"
#define GPIO_POWER_9300_PIN_LABEL		"amber9300 power gpio"
#define GPIO_RESET_9300_PIN_NAME		"amber9300_gpio_reset"
#define GPIO_RESET_9300_PIN_LABEL		"amber9300 reset gpio"

#define GPIO_POWER_PIN_NAME     "bt_gpio_power"
#define GPIO_POWER_PIN_LABEL    "bt power gpio"
#define GPIO_RESET_PIN_NAME     "bt_gpio_reset"
#define GPIO_RESET_PIN_LABEL    "bt reset gpio"



struct rfkill_gpio_data {
	struct rfkill *rfkill_bt_dev;
	struct rfkill *rfkill_wlan_dev;
	struct pwm_device *pwm;
	int power_gpio;
	int reset_gpio;
	int power_number;
	int power_delay;
	int reset_delay;
	void (*power_on)(struct rfkill_gpio_data *rfkill);
	void (*power_off)(struct rfkill_gpio_data *rfkill);
};

struct csr_connectivity_register {
	int power_delay;
	int reset_delay;
	int (*probe)(struct rfkill_gpio_data *rfkill,
					struct platform_device *pdev);
	void (*remove)(struct rfkill_gpio_data *rfkill,
					struct platform_device *pdev);
	void (*power_on)(struct rfkill_gpio_data *rfkill);
	void (*power_off)(struct rfkill_gpio_data *rfkill);
};

static void csr_9300_power_on(struct rfkill_gpio_data *rfkill)
{
	if (gpio_is_valid(rfkill->power_gpio)) {
		gpio_direction_output(rfkill->power_gpio, 0);
		gpio_set_value_cansleep(rfkill->power_gpio, 1);
	}

	msleep(rfkill->power_delay);

	if (gpio_is_valid(rfkill->reset_gpio))
		gpio_direction_output(rfkill->reset_gpio, 1);

	msleep(rfkill->power_delay);

	if (gpio_is_valid(rfkill->reset_gpio)) {
		gpio_direction_output(rfkill->reset_gpio, 0);
		msleep(rfkill->reset_delay);
		gpio_direction_output(rfkill->reset_gpio, 1);
	}
}

static void csr_8311_power_on(struct rfkill_gpio_data *rfkill)
{
	if (gpio_is_valid(rfkill->power_gpio))
		gpio_direction_output(rfkill->power_gpio, 1);

	pwm_enable(rfkill->pwm);

	msleep(rfkill->power_delay);

	if (gpio_is_valid(rfkill->reset_gpio)) {
		gpio_direction_output(rfkill->reset_gpio, 0);
		msleep(rfkill->reset_delay);
		gpio_direction_output(rfkill->reset_gpio, 1);
	}
}

static void csr_9300_power_off(struct rfkill_gpio_data *rfkill)
{
	if (gpio_is_valid(rfkill->power_gpio)) {
		gpio_direction_output(rfkill->power_gpio, 0);
		gpio_set_value_cansleep(rfkill->power_gpio, 0);
		rfkill->power_number--;
	}
}

static void csr_8311_power_off(struct rfkill_gpio_data *rfkill)
{
	if (gpio_is_valid(rfkill->power_gpio)) {
		gpio_direction_output(rfkill->power_gpio, 0);
		rfkill->power_number--;
	}
	pwm_disable(rfkill->pwm);
}

static int csr_9300_probe(struct rfkill_gpio_data *rfkill,
					struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	int ret = 0;

	rfkill->power_gpio = of_get_named_gpio(dn, GPIO_POWER_9300_PIN_NAME, 0);
	if (gpio_is_valid(rfkill->power_gpio)) {
		ret = gpio_request(rfkill->power_gpio,
				GPIO_POWER_9300_PIN_LABEL);
		if (ret) {
			pr_warn("%s: failed to get power gpio.\n",
				__func__);
			return ret;
		}
	}

	rfkill->reset_gpio = of_get_named_gpio(dn, GPIO_RESET_9300_PIN_NAME, 0);
	if (gpio_is_valid(rfkill->reset_gpio)) {
		ret = gpio_request(rfkill->reset_gpio,
				GPIO_RESET_9300_PIN_LABEL);
		if (ret) {
			pr_warn("%s: failed to get reset gpio.\n",
				__func__);
			return ret;
		}
	}

	return 0;
}

static int csr_8311_probe(struct rfkill_gpio_data *rfkill,
					struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	int ret = 0;

	rfkill->power_gpio = of_get_named_gpio(dn, GPIO_POWER_PIN_NAME, 0);
	if (gpio_is_valid(rfkill->power_gpio)) {
		ret = gpio_request(rfkill->power_gpio, GPIO_POWER_PIN_LABEL);
		if (ret) {
			pr_warn("%s: failed to get power gpio.\n",
				__func__);
			return ret;
		}
	}

	rfkill->reset_gpio = of_get_named_gpio(dn, GPIO_RESET_PIN_NAME, 0);
	if (gpio_is_valid(rfkill->reset_gpio)) {
		ret = gpio_request(rfkill->reset_gpio, GPIO_RESET_PIN_LABEL);
		if (ret) {
			pr_warn("%s: failed to get reset gpio.\n",
				__func__);
			return ret;
		}
	}

	rfkill->pwm = devm_pwm_get(&pdev->dev, NULL);
	if (IS_ERR(rfkill->pwm)) {
		dev_err(&pdev->dev, "unable to request PWM\n");
		ret = PTR_ERR(rfkill->pwm);
		return ret;
	}

	pwm_config(rfkill->pwm, 0, rfkill->pwm->period);
	pwm_enable(rfkill->pwm);

	return 0;
}

static void csr_9300_remove(struct rfkill_gpio_data *rfkill,
					struct platform_device *pdev)
{
	if (gpio_is_valid(rfkill->power_gpio))
		gpio_free(rfkill->power_gpio);

	if (gpio_is_valid(rfkill->reset_gpio))
		gpio_free(rfkill->reset_gpio);

}

static void csr_8311_remove(struct rfkill_gpio_data *rfkill,
					struct platform_device *pdev)
{
	if (gpio_is_valid(rfkill->power_gpio))
		gpio_free(rfkill->power_gpio);

	if (gpio_is_valid(rfkill->reset_gpio))
		gpio_free(rfkill->reset_gpio);

	if (rfkill->pwm != NULL) {
		pwm_disable(rfkill->pwm);
		pwm_free(rfkill->pwm);
	}

}

static struct csr_connectivity_register csr_amber_9300 = {
	.power_delay = 10,
	.reset_delay = 400,
	.probe = csr_9300_probe,
	.remove = csr_9300_remove,
	.power_on = csr_9300_power_on,
	.power_off = csr_9300_power_off,
};


static struct csr_connectivity_register csr_bt_8311 = {
	.power_delay = 0,
	.reset_delay = 0,
	.probe = csr_8311_probe,
	.remove = csr_8311_remove,
	.power_on = csr_8311_power_on,
	.power_off = csr_8311_power_off,
};

static const struct of_device_id csr_rfkill_of_match[] = {
	{ .compatible = "sirf,amber-9300", .data = &csr_amber_9300 },
	{ .compatible = "sirf,bt-8311", .data = &csr_bt_8311 },
	{ .compatible = "sirf,bt-8311-evb" },
	{},
};

MODULE_DEVICE_TABLE(of, csr_bt_of_match);


static int rfkill_gpio_set_power(void *data, bool blocked)
{
	struct rfkill_gpio_data *rfkill = data;

	if (blocked) {
		if (rfkill->power_number <= 0)
			return 0;

		if (rfkill->power_number > 1) {
			pr_info("%s: decrease to %d and return\n "
				, __func__, rfkill->power_number - 1);
			rfkill->power_number--;
			/* some other apps need power, just return */
			return 0;
		}

		if (rfkill->power_off)
			rfkill->power_off(rfkill);
	} else {
		rfkill->power_number++;
		if (rfkill->power_number > 1) {
			pr_info("%s: power already on, rfkill->power_number:%d\n",
				__func__, rfkill->power_number);
			return 0;	/* power already on, just return */
		}

		if (rfkill->power_on)
			rfkill->power_on(rfkill);
	}

	return 0;
}

static const struct rfkill_ops rfkill_gpio_ops = {
	.set_block = rfkill_gpio_set_power,
};

static int csr_rfkill_probe(struct platform_device *pdev)
{
	struct rfkill_gpio_data *rfkill = NULL;
	const struct of_device_id *match;
	struct csr_connectivity_register *reg_ops;
	int ret = 0;

	match = of_match_node(csr_rfkill_of_match, pdev->dev.of_node);
	if (!match) {
		dev_err(&pdev->dev, "no matching node\n");
		ret = -ENODEV;
		return ret;
	}

	reg_ops = (struct csr_connectivity_register *)match->data;

	rfkill = kzalloc(sizeof(*rfkill), GFP_KERNEL);
	if (!rfkill) {
		ret = -ENOMEM;
		goto fail_alloc;
	}

	if (!(reg_ops->probe)) {
		ret = -EINVAL;
		goto fail_alloc;
	}

	ret = reg_ops->probe(rfkill, pdev);

	if (ret != 0)
		goto fail_reset;

	/* register RFKILL_TYPE_BLUETOOTH for rfkill_bt_dev */
	rfkill->rfkill_bt_dev = rfkill_alloc("csr-bt-chip", &pdev->dev,
					  RFKILL_TYPE_BLUETOOTH,
					  &rfkill_gpio_ops, rfkill);

	if (!rfkill->rfkill_bt_dev) {
		ret = -ENOMEM;
		goto fail_reset;
	}

	ret = rfkill_register(rfkill->rfkill_bt_dev);
	if (ret < 0)
		goto fail_bt_rfkill;

	/* register RFKILL_TYPE_WLAN for rfkill_wlan_dev */
	rfkill->rfkill_wlan_dev = rfkill_alloc("csr-wlan-chip", &pdev->dev,
					  RFKILL_TYPE_WLAN,
					  &rfkill_gpio_ops, rfkill);

	if (!rfkill->rfkill_wlan_dev) {
		ret = -ENOMEM;
		goto fail_reset;
	}

	ret = rfkill_register(rfkill->rfkill_wlan_dev);
	if (ret < 0)
		goto fail_wlan_rfkill;

	rfkill->power_on = reg_ops->power_on;
	rfkill->power_off = reg_ops->power_off;

	rfkill->power_delay = reg_ops->power_delay;
	rfkill->reset_delay = reg_ops->reset_delay;

	platform_set_drvdata(pdev, rfkill);

	return 0;

fail_wlan_rfkill:
	if (rfkill->rfkill_wlan_dev != NULL)
		rfkill_destroy(rfkill->rfkill_wlan_dev);

fail_bt_rfkill:
	if (rfkill->rfkill_bt_dev != NULL)
		rfkill_destroy(rfkill->rfkill_bt_dev);
fail_reset:
	if (gpio_is_valid(rfkill->reset_gpio))
		gpio_free(rfkill->reset_gpio);
	if (gpio_is_valid(rfkill->power_gpio))
		gpio_free(rfkill->power_gpio);
fail_alloc:
	kfree(rfkill);

	return ret;
}

static int csr_rfkill_remove(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct csr_connectivity_register *reg_ops;
	int ret = 0;
	struct rfkill_gpio_data *rfkill = platform_get_drvdata(pdev);

	if (!rfkill)
		return ret;

	match = of_match_node(csr_rfkill_of_match, pdev->dev.of_node);
	if (!match) {
		dev_err(&pdev->dev, "no matching node\n");
		ret = -ENODEV;
		return ret;
	}

	reg_ops = (struct csr_connectivity_register *)match->data;

	/* remove rfkill bt device */
	rfkill_unregister(rfkill->rfkill_bt_dev);
	rfkill_destroy(rfkill->rfkill_bt_dev);
	/* remove rfkill wlan device */
	rfkill_unregister(rfkill->rfkill_wlan_dev);
	rfkill_destroy(rfkill->rfkill_wlan_dev);

	if (reg_ops->remove)
		reg_ops->remove(rfkill, pdev);

	kfree(rfkill);

	return ret;
}

static struct platform_driver csr_rfkill_driver = {
	.driver = {
		.name = "csr-connectivity-rfkill",
		.owner = THIS_MODULE,
		.of_match_table = csr_rfkill_of_match,
		},
	.probe = csr_rfkill_probe,
	.remove = csr_rfkill_remove,
};

module_platform_driver(csr_rfkill_driver);

MODULE_AUTHOR("Cambridge Silicon Radio Ltd");
MODULE_DESCRIPTION("CSR Synergy for Bluetooth and WLAN Enable Driver");
MODULE_LICENSE("GPL");
