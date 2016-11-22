/*
 * sirfsoc temperature sensor driver
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
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/iio/consumer.h>
#include <linux/module.h>
#include <linux/thermal.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>


struct sirfsoc_sensor {
	struct iio_channel	*chan;
	struct device *hwmon_dev;
	struct thermal_zone_device *tz;
};

/* convert left adjusted 13-bit sirfsoc_sensor register value to milliCelsius*/
static int sirfsoc_sensor_reg_to_mC(int val)
{
	return (((10*val-14799)/54))*1000 + 26000;
}

static int sirfsoc_sensor_read_temp(void *sensor_data, long *temp)
{
	struct sirfsoc_sensor *sirfsoc_sensor = sensor_data;
	int ret, val;

	ret = iio_read_channel_processed(sirfsoc_sensor->chan, &val);
	if (ret < 0)
		dev_WARN(sirfsoc_sensor->hwmon_dev, "read channel error\n");
	*temp = sirfsoc_sensor_reg_to_mC(val);

	return 0;
}

static ssize_t sirfsoc_sensor_show_temp(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct sirfsoc_sensor *sirfsoc_sensor = dev_get_drvdata(dev);
	int ret, val;

	ret = iio_read_channel_processed(sirfsoc_sensor->chan, &val);
	if (ret < 0)
		dev_WARN(sirfsoc_sensor->hwmon_dev, "read channel error\n");

	return sprintf(buf, "%d\n", sirfsoc_sensor_reg_to_mC(val));
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO,
		sirfsoc_sensor_show_temp, NULL , 0);

static struct attribute *sirfsoc_sensor_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(sirfsoc_sensor);

static int sirfsoc_sensor_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device *hwmon_dev;
	struct sirfsoc_sensor *sirfsoc_sensor;
	struct thermal_zone_device *tz;
	int status;

	sirfsoc_sensor = devm_kzalloc(dev, sizeof(*sirfsoc_sensor), GFP_KERNEL);
	if (!sirfsoc_sensor)
		return -ENOMEM;
	sirfsoc_sensor->chan = iio_channel_get(&pdev->dev, "temp");
	if (IS_ERR(sirfsoc_sensor->chan)) {
		dev_err(dev, "atlas7 sensor: Unable to get the adc channel\n");
		status = PTR_ERR(sirfsoc_sensor->chan);
		goto err;
	}

	hwmon_dev = hwmon_device_register_with_groups(dev,
		"sirfsoc_sensor",
		sirfsoc_sensor,
		sirfsoc_sensor_groups);
	if (IS_ERR(hwmon_dev)) {
		dev_dbg(dev, "unable to register hwmon device\n");
		status = PTR_ERR(hwmon_dev);
		goto err;
	}
	sirfsoc_sensor->hwmon_dev = hwmon_dev;
	tz = thermal_zone_of_sensor_register(dev, 0,
			sirfsoc_sensor,
			sirfsoc_sensor_read_temp,
			NULL);
	if (IS_ERR(tz))
		sirfsoc_sensor->tz = NULL;
	else
		sirfsoc_sensor->tz = tz;

	dev_info(dev, "sirfsoc_sensor initialized\n");
	platform_set_drvdata(pdev, sirfsoc_sensor);

	return 0;
err:
	return status;
}

static int sirfsoc_sensor_remove(struct platform_device *pdev)
{
	struct sirfsoc_sensor *sirfsoc_sensor = platform_get_drvdata(pdev);

	thermal_zone_of_sensor_unregister(sirfsoc_sensor->hwmon_dev,
			sirfsoc_sensor->tz);
	hwmon_device_unregister(sirfsoc_sensor->hwmon_dev);

	return 0;
}

static const struct of_device_id sirfsoc_sensor_ids[] = {
	{ .compatible = "sirf,atlas7-sensor"},
	{}
};

static struct platform_driver sirfsoc_sensor_driver = {
	.driver = {
		   .name = "sirfsoc_sensor",
		   .of_match_table = sirfsoc_sensor_ids,
		   },
	.probe = sirfsoc_sensor_probe,
	.remove = sirfsoc_sensor_remove,
};
module_platform_driver(sirfsoc_sensor_driver);
