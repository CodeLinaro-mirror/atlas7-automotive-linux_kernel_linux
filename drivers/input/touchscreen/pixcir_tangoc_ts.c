/*
 * Pixcir Tango C series 5 points touch controller Driver
 *
 * Copyright (c) 2013-2014, 2016, The Linux Foundation. All rights reserved.
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
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/slab.h>

#define TOUCHSCREEN_MINX 0
#define TOUCHSCREEN_MAXX 1024
#define TOUCHSCREEN_MINY 0
#define TOUCHSCREEN_MAXY 600
#define TOUCH_MAJOR_MAX 50
#define WIDTH_MAJOR_MAX 15
#define TRACKING_ID_MAX 5

#define RESET_REG 0x3A
#define RESET_CODE 0x3

struct pixcir_ts_point_data {
	u16 posx;  /* x coordinate */
	u16 posy;  /* y coordinate */
	u8 id;    /* finger ID */
} __packed;

struct pixcir_ts_touch_data {
	u16 touch_fingers;
	struct pixcir_ts_point_data point[5];
} __packed;

struct pixcir_ts_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	unsigned int touch_pin;
	struct pixcir_ts_touch_data touch_data;
};

static void pixcir_ts_reset(struct i2c_client *client)
{
	u8 buf[2] = {RESET_REG, RESET_CODE};
	i2c_master_send(client, buf, 2);
}

/*Report the touch position*/
static void  pixcir_ts_report_event(struct pixcir_ts_data *ts)
{
	int ret, i;
	int fingers;
	u8 addr;

	addr = 0;
	ret = i2c_master_send(ts->client, &addr, sizeof(addr));
	if (ret != sizeof(addr)) {
		dev_err(&ts->client->dev,
				"pixcir_ts:Unable to reset pixcir_ts!\n");
		return;
	}

	ret = i2c_master_recv(ts->client, (char *)&ts->touch_data,
					sizeof(struct pixcir_ts_touch_data));
	if (ret != sizeof(struct pixcir_ts_touch_data)) {
		dev_err(&ts->client->dev,
				"pixcir_ts:Unable get the touch info\n");
		return;
	}

	fingers = ts->touch_data.touch_fingers & 0x07;
	if (fingers > 0) {
		for (i = 0; i < fingers; i++) {
			/*Get the touch position*/
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X,
					le16_to_cpu(ts->touch_data.point[i].posx));
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y,
					le16_to_cpu(ts->touch_data.point[i].posy));

			input_report_key(ts->input_dev, ABS_MT_TRACKING_ID,
					ts->touch_data.point[i].id);
			input_report_abs(ts->input_dev,
					ABS_MT_TOUCH_MAJOR, TOUCH_MAJOR_MAX);
			input_report_abs(ts->input_dev,
					ABS_MT_WIDTH_MAJOR, WIDTH_MAJOR_MAX);
			input_report_key(ts->input_dev, BTN_TOUCH, 1);
			input_mt_sync(ts->input_dev);
		}
	} else {
		input_report_key(ts->input_dev, BTN_TOUCH, 0);
		input_mt_sync(ts->input_dev);
	}

	input_sync(ts->input_dev);
}

static irqreturn_t pixcir_ts_irq_handler(int irq, void *dev_id)
{
	struct pixcir_ts_data *ts = dev_id;

	pixcir_ts_report_event(ts);

	return IRQ_HANDLED;
}

#ifdef CONFIG_PM_SLEEP
static int pixcir_ts_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	if (device_may_wakeup(&client->dev))
		enable_irq_wake(client->irq);

	return 0;
}

static int pixcir_ts_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	if (device_may_wakeup(&client->dev))
		disable_irq_wake(client->irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(pixcir_dev_pm_ops,
			pixcir_ts_suspend, pixcir_ts_resume);
#endif

static int pixcir_ts_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct pixcir_ts_data *ts;
	struct input_dev *input_dev;
	u8 tmp = 0;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	ts = devm_kzalloc(&client->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;
	ts->client = client;
	i2c_set_clientdata(client, ts);

	input_dev = devm_input_allocate_device(&client->dev);
	if (!input_dev)
		return -ENOMEM;
	ts->input_dev = input_dev;

	/* if the client exists, this i2c transfer should be ok */
	ret = i2c_master_send(ts->client, &tmp, 1);
	if (ret != 1)
		return -ENODEV;

	i2c_set_clientdata(client, ts);
	input_set_drvdata(input_dev, ts);
	input_dev->name = "tangoc-touchscreen";
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;
	set_bit(EV_SYN, input_dev->evbit);
	set_bit(EV_ABS, input_dev->evbit);
	set_bit(EV_KEY, input_dev->evbit);
	set_bit(ABS_MT_TOUCH_MAJOR, input_dev->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, input_dev->absbit);
	set_bit(ABS_MT_POSITION_X, input_dev->absbit);
	set_bit(ABS_MT_POSITION_Y, input_dev->absbit);
	set_bit(ABS_MT_TRACKING_ID, input_dev->absbit);
	set_bit(BTN_TOUCH, input_dev->keybit);
	set_bit(INPUT_PROP_DIRECT, input_dev->propbit);
	input_set_abs_params(input_dev, ABS_MT_POSITION_X,
			TOUCHSCREEN_MINX, TOUCHSCREEN_MAXX, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y,
			TOUCHSCREEN_MINY, TOUCHSCREEN_MAXY, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR,
			0, TOUCH_MAJOR_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR,
			0, WIDTH_MAJOR_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TRACKING_ID,
			0, TRACKING_ID_MAX, 0, 0);

	ret = devm_request_threaded_irq(&client->dev,
		client->irq,
		NULL, pixcir_ts_irq_handler,
		IRQF_ONESHOT,
		client->name, ts);
	if (ret) {
		dev_err(&client->dev, "\nFailed to register interrupt\n");
		return ret;
	}

	ret = input_register_device(ts->input_dev);
	if (ret) {
		dev_err(&client->dev, "Unable to register %s input device\n",
			input_dev->name);
		return ret;
	}

	device_init_wakeup(&client->dev, 1);

	pixcir_ts_reset(ts->client);

	return 0;
}

static int pixcir_ts_remove(struct i2c_client *client)
{
	device_init_wakeup(&client->dev, 0);
	return 0;
}

static const struct i2c_device_id pixcir_ts_id[] = {
	{ "tangoc-ts", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, pixcir_ts_id);

static struct i2c_driver pixcir_ts_driver = {
	.driver = {
		.name	= "tangoc-ts",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM_SLEEP
		.pm	= &pixcir_dev_pm_ops,
#endif
	},
	.id_table	= pixcir_ts_id,
	.probe		= pixcir_ts_probe,
	.remove		= pixcir_ts_remove,
};

module_i2c_driver(pixcir_ts_driver);

MODULE_DESCRIPTION("PIXCIR-TangoC 5 points touch controller Driver");
MODULE_LICENSE("GPL v2");
