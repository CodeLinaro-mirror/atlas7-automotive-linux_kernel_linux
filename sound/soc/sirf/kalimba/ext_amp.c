/*
 * external amplifier driver for CSR SiRFAtlas7
 *
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
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

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/mutex.h>
#include "dsp.h"

struct extamp_info {
	struct delayed_work work;
	int gpio;
	int t1; /* delay time in msec*/
	int t2; /* restore time in msec*/
	int gain; /* master gain*/
	struct mutex  lock;
};

#define MGAIN_DB_DOWN	(-3)
#define MGAIN_DB_UP	(1)

static irqreturn_t cdgpio_handler(int irq, void *data)
{
	struct extamp_info *info = data;

	mutex_lock(&info->lock);

	cancel_delayed_work(&info->work);
	info->gain += MGAIN_DB_DOWN;
	kalimba_set_master_gain(info->gain);
	schedule_delayed_work(&info->work,
				msecs_to_jiffies(info->t1));

	mutex_unlock(&info->lock);
	return IRQ_HANDLED;
}


static ssize_t cdgpio_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct extamp_info *info = dev_get_drvdata(dev);
	int t1, t2;

	if (sscanf(buf, "%d %d\n", &t1, &t2) != 2)
		return -EINVAL;

	info->t1 = t1;
	info->t2 = t2;

	return len;
}

static ssize_t cdgpio_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct extamp_info *info = dev_get_drvdata(dev);
	int len = 0;

		len = scnprintf(buf, PAGE_SIZE,
				"T1[%d]T2[%d]\n",
				info->t1,
				info->t2);

	return len;
}

static DEVICE_ATTR_RW(cdgpio);

static void cdgpio_delay_work(struct work_struct *work)
{
	struct extamp_info *info = container_of(work,
		struct extamp_info, work.work);

	if (info->gain == 0)
		return;

	mutex_lock(&info->lock);
	info->gain += MGAIN_DB_UP;
	kalimba_set_master_gain(info->gain);
	mutex_unlock(&info->lock);
	schedule_delayed_work(&info->work,
				msecs_to_jiffies(info->t2));
}

static int extamp_probe(struct platform_device *pdev)
{
	struct extamp_info *info;
	int ret;

	info = devm_kzalloc(&pdev->dev,
		sizeof(struct extamp_info),
		GFP_KERNEL);

	if (!info)
		return -ENOMEM;

	info->gpio = of_get_named_gpio(pdev->dev.of_node,
								"cd-gpio", 0);

	if (!gpio_is_valid(info->gpio))
		return -ENODEV;

	ret = devm_gpio_request_one(&pdev->dev,
					info->gpio,
					GPIOF_IN,
					"cd-gpio");
	if (ret) {
		dev_err(&pdev->dev, "failed to request gpio for anti-clipping\n");
		goto err;
	}
	info->t1 = 2000;
	info->t2 = 2000;
	info->gain = 0;

	mutex_init(&info->lock);
	kalimba_set_master_gain(0);
	platform_set_drvdata(pdev, info);

	ret = devm_request_threaded_irq(&pdev->dev,
				   gpio_to_irq(info->gpio),
				   NULL,
				   cdgpio_handler,
				   IRQF_ONESHOT | IRQF_TRIGGER_FALLING,
				   dev_name(&pdev->dev),
				   info);
	if (ret) {
		dev_err(&pdev->dev, "failed to request gpio irq for clip detect\n");
		goto err;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_cdgpio);
	if (ret) {
		device_remove_file(&pdev->dev, &dev_attr_cdgpio);
		goto err;
	}

	INIT_DELAYED_WORK(&info->work, cdgpio_delay_work);
	return 0;
err:
	return ret;
}


static int extamp_remove(struct platform_device *pdev)
{
	device_remove_file(&pdev->dev, &dev_attr_cdgpio);
	return 0;
}

static const struct of_device_id extamp_of_match[] = {
	{ .compatible = "sirf,ext-amp", },
	{}
};
MODULE_DEVICE_TABLE(of, extamp_of_match);

static struct platform_driver extamp_driver = {
	.driver = {
		.name = "extamp",
		.of_match_table = extamp_of_match,
	},
	.probe = extamp_probe,
	.remove = extamp_remove,
};

module_platform_driver(extamp_driver);

MODULE_DESCRIPTION("SiRF SoC external amplifier driver");
MODULE_LICENSE("GPL v2");
