/*
 * Atlas7 evb keypad Driver
 *
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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

#include <linux/err.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/iio/consumer.h>
#include <linux/slab.h>
#include <linux/irq.h>

#define KEY_COMPARE_CTRL		0x00
#define KEY_COMPARE_EN			0x3

#define KEYS_DETECT_UP_TIME		40	/* ms*/

struct atlas7_keys_keymap {
	u32 voltage;
	u32 keycode;
	bool pressed;
};

struct atlas7_keys {
	struct device		*dev;
	struct input_dev	*input;
	void __iomem		*comp_base;
	int			irq;
	struct iio_channel	*chan;
	struct atlas7_keys_keymap *keys_map;
	u32			keys_map_count;
	u32			keys_keycode;
	u32			max_press_volt;
	struct workqueue_struct *keys_wq;
	struct delayed_work     keys_poll;
};

static void atlas7_keys_try_release_keys(struct atlas7_keys *keys)
{
	int i;

	for (i = 0; i < keys->keys_map_count; i++) {
		if (keys->keys_map[i].pressed) {
			input_report_key(keys->input,
					keys->keys_map[i].keycode, 0);
			input_sync(keys->input);
			keys->keys_map[i].pressed = false;
		}
	}
}

static void atlas7_keys_try_press_keys(struct atlas7_keys *keys, int volt)
{
	int i;

	for (i = 0; i < keys->keys_map_count; i++) {
		if (abs(keys->keys_map[i].voltage - volt) < 35
			&& keys->keys_map[i].pressed == false) {
			input_report_key(keys->input,
					keys->keys_map[i].keycode, 1);
			input_sync(keys->input);
			keys->keys_map[i].pressed = true;
		}
	}
}

#ifdef CONFIG_PM_SLEEP
static int atlas7_keys_suspend(struct device *dev)
{
	struct atlas7_keys *keys = dev_get_drvdata(dev);

	writel(0, keys->comp_base + KEY_COMPARE_CTRL);
	cancel_delayed_work(&keys->keys_poll);
	return 0;
}

static int atlas7_keys_resume(struct device *dev)
{
	struct atlas7_keys *keys = dev_get_drvdata(dev);

	writel(KEY_COMPARE_EN, keys->comp_base + KEY_COMPARE_CTRL);
	queue_delayed_work(keys->keys_wq, &keys->keys_poll, 30);
	return 0;
}

static SIMPLE_DEV_PM_OPS(atlas7_keys_pm_ops,
			atlas7_keys_suspend, atlas7_keys_resume);
#endif

static void atlas7_adc_key_func(struct work_struct *work)
{
	struct delayed_work *delay = to_delayed_work(work);
	struct atlas7_keys *keys = container_of(delay,
				   struct atlas7_keys, keys_poll);
	int volt;
	int ret;

	ret = iio_read_channel_processed(keys->chan, &volt);
	if (ret < 0)
		dev_WARN(keys->dev, "read channel error\n");

	if (volt < keys->max_press_volt)
		atlas7_keys_try_press_keys(keys, volt);
	else
		atlas7_keys_try_release_keys(keys);

	queue_delayed_work(keys->keys_wq, &keys->keys_poll, 30);
}

static int atlas7_keys_probe(struct platform_device *pdev)
{
	struct atlas7_keys *keys;
	struct device_node *pp, *np, *key_comp_np;
	int ret;
	int i = 0;

	keys = devm_kzalloc(&pdev->dev,
			sizeof(struct atlas7_keys), GFP_KERNEL);
	if (!keys)
		return -ENOMEM;

	np = pdev->dev.of_node;

	keys->keys_map_count = of_get_child_count(np);
	keys->keys_map = devm_kzalloc(&pdev->dev, keys->keys_map_count *
				sizeof(struct atlas7_keys_keymap), GFP_KERNEL);
	if (!keys->keys_map)
		return -ENOMEM;

	/* when keys release, the volt greater then the max_press_volt value */
	ret = of_property_read_u32(np, "max-press-volt", &keys->max_press_volt);
	if (ret) {
		dev_err(&pdev->dev,
			"%s: no max valid press voltage prop\n", np->name);
		return -EINVAL;
	}

	for_each_child_of_node(np, pp) {
		struct atlas7_keys_keymap *map = &keys->keys_map[i];

		ret = of_property_read_u32(pp, "voltage", &map->voltage);
		if (ret) {
			dev_err(&pdev->dev, "%s: no voltage prop\n", pp->name);
			return -EINVAL;
		}

		ret = of_property_read_u32(pp, "linux,code", &map->keycode);
		if (ret) {
			dev_err(&pdev->dev,
				"%s: no linux,code prop\n", pp->name);
			return -EINVAL;
		}

		i++;
	}

	keys->chan = iio_channel_get(&pdev->dev, "adc_keys");
	if (IS_ERR(keys->chan)) {
		dev_err(&pdev->dev,
			"atlas7 keys: Unable to get the adc channel\n");
		return PTR_ERR(keys->chan);
	}

	/*
	 * The key comparator is  to compare the key's voltage with a reference
	 * voltage. when the key's voltage lower then the reference voltage,
	 * the comparator is tripping and the change is delivered to the SOC
	 * and then the key interrupt occurring.
	 */
	key_comp_np = of_find_compatible_node(NULL,
				NULL, "sirf,atlas7-key-comparator");
	if (!key_comp_np) {
		dev_err(&pdev->dev,
			"atlas7 keys: Unable to get key comparator node\n");
		ret = -EINVAL;
		goto out;
	}

	keys->comp_base = of_iomap(key_comp_np, 0);
	if (!keys->comp_base) {
		dev_err(&pdev->dev,
			"atlas7 keys: Unable to remap key comparator resource\n");
		ret = -ENOMEM;
		goto out;
	}
	writel(KEY_COMPARE_EN, keys->comp_base + KEY_COMPARE_CTRL);

	keys->keys_wq = create_singlethread_workqueue("atlas7_adckeys");
	if (!keys->keys_wq) {
		dev_err(&pdev->dev,
			"atlas7 keys: Unable to create workqueue\n");
		ret = -EFAULT;
		goto out;
	}
	INIT_DELAYED_WORK(&keys->keys_poll, atlas7_adc_key_func);

	keys->dev = &pdev->dev;
	keys->input = devm_input_allocate_device(&pdev->dev);
	if (!keys->input) {
		ret = -ENOMEM;
		goto out;
	}

	keys->input->name = pdev->name;
	keys->input->evbit[0] = BIT(EV_SYN) | BIT(EV_KEY);
	for (i = 0; i < keys->keys_map_count; i++)
		set_bit(keys->keys_map[i].keycode, keys->input->keybit);

	input_set_drvdata(keys->input, keys);

	ret = input_register_device(keys->input);
	if (ret)
		goto out;

	platform_set_drvdata(pdev, keys);
	queue_delayed_work(keys->keys_wq, &keys->keys_poll, 0);

	return 0;
out:
	iio_channel_release(keys->chan);

	return ret;
}

static int atlas7_keys_remove(struct platform_device *pdev)
{
	struct atlas7_keys *keys = platform_get_drvdata(pdev);

	writel(0, keys->comp_base + KEY_COMPARE_CTRL);
	iounmap(keys->comp_base);
	input_unregister_device(keys->input);
	iio_channel_release(keys->chan);
	destroy_workqueue(keys->keys_wq);

	return 0;
}

static const struct of_device_id atlas7_keys_of_match[] = {
	{ .compatible = "sirf,atlas7-adc-keys", },
	{}
};
MODULE_DEVICE_TABLE(of, atlas7_keys_of_match);

static struct platform_driver atlas7_keys_driver = {
	.driver = {
#ifdef CONFIG_PM_SLEEP
		.pm = &atlas7_keys_pm_ops,
#endif
		.name = "atlas7-keys",
		.of_match_table = of_match_ptr(atlas7_keys_of_match),
	},
	.probe = atlas7_keys_probe,
	.remove	= atlas7_keys_remove,
};

module_platform_driver(atlas7_keys_driver);

MODULE_DESCRIPTION("Adc keys on atlas7 evb driver");
MODULE_LICENSE("GPL v2");
