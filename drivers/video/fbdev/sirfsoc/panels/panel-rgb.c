/*
 * CSR sirfsoc rgb-panel driver
 *
 * Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
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

#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <video/sirfsoc_vdss.h>
#include <video/display_timing.h>
#include <video/of_display_timing.h>

#define DEFAULT_RGB_PANEL_NAME	"rgb"

struct panel_drv_data {
	struct sirfsoc_vdss_panel panel;
	struct sirfsoc_vdss_output *in;
	int data_lines;

	struct sirfsoc_video_timings timings;

	int power_vcc;
	int power_vdd;
	int power_vee;
	int vcc_gpio;
	struct i2c_client *client;
};

#define to_panel_data(p) container_of(p, struct panel_drv_data, panel)

static int panel_rgb_connect(struct sirfsoc_vdss_panel *panel)
{
	struct panel_drv_data *pdata = to_panel_data(panel);
	struct sirfsoc_vdss_output *in = pdata->in;
	int r;

	if (sirfsoc_vdss_panel_is_connected(panel))
		return 0;

	r = in->ops.rgb->connect(in, panel);
	if (r)
		return r;

	return 0;
}

static void panel_rgb_disconnect(struct sirfsoc_vdss_panel *panel)
{
	struct panel_drv_data *pdata = to_panel_data(panel);
	struct sirfsoc_vdss_output *in = pdata->in;

	if (!sirfsoc_vdss_panel_is_connected(panel))
		return;

	in->ops.rgb->disconnect(in, panel);
}

static int panel_rgb_enable(struct sirfsoc_vdss_panel *panel)
{
	struct panel_drv_data *pdata = to_panel_data(panel);
	struct sirfsoc_vdss_output *in = pdata->in;
	int r;

	if (!sirfsoc_vdss_panel_is_connected(panel))
		return -ENODEV;

	if (sirfsoc_vdss_panel_is_enabled(panel))
		return 0;

	if (pdata->data_lines)
		in->ops.rgb->set_data_lines(in, pdata->data_lines);
	in->ops.rgb->set_timings(in, &pdata->timings);

	r = in->ops.rgb->enable(in);
	if (r)
		return r;

	if (!of_machine_is_compatible("sirf,atlas7")) {
		if (pdata->power_vcc != 0)
			i2c_smbus_write_byte_data(pdata->client,
				pdata->power_vcc & 0xFFFF, 0x1);
		else
			gpio_set_value_cansleep(pdata->vcc_gpio, 1);

		msleep(50);

		if (pdata->power_vdd != 0)
			i2c_smbus_write_byte_data(pdata->client,
				pdata->power_vdd & 0xFFFF, 0x1);

		msleep(200);

		if (pdata->power_vee != 0)
			i2c_smbus_write_byte_data(pdata->client,
				pdata->power_vee & 0xFFFF, 0x1);
	}

	panel->state = SIRFSOC_VDSS_PANEL_ENABLED;

	return 0;
}

static void panel_rgb_disable(struct sirfsoc_vdss_panel *panel)
{
	struct panel_drv_data *pdata = to_panel_data(panel);
	struct sirfsoc_vdss_output *in = pdata->in;

	if (!sirfsoc_vdss_panel_is_enabled(panel))
		return;

	if (!of_machine_is_compatible("sirf,atlas7")) {
		if (pdata->power_vee != 0)
			i2c_smbus_write_byte_data(pdata->client,
				pdata->power_vee >> 16, 0x1);

		if (pdata->power_vdd != 0)
			i2c_smbus_write_byte_data(pdata->client,
				pdata->power_vdd >> 16, 0x1);

		if (pdata->power_vcc != 0)
			i2c_smbus_write_byte_data(pdata->client,
				pdata->power_vcc >> 16, 0x1);
		else
			gpio_set_value_cansleep(pdata->vcc_gpio, 0);
	}

	in->ops.rgb->disable(in);

	panel->state = SIRFSOC_VDSS_PANEL_DISABLED;
}

static void panel_rgb_set_timings(struct sirfsoc_vdss_panel *panel,
	struct sirfsoc_video_timings *timings)
{
	struct panel_drv_data *pdata = to_panel_data(panel);
	struct sirfsoc_vdss_output *in = pdata->in;

	pdata->timings = *timings;
	panel->timings = *timings;

	in->ops.rgb->set_timings(in, timings);
}

static void panel_rgb_get_timings(struct sirfsoc_vdss_panel *panel,
		struct sirfsoc_video_timings *timings)
{
	struct panel_drv_data *pdata = to_panel_data(panel);

	*timings = pdata->timings;
}

static int panel_rgb_check_timings(struct sirfsoc_vdss_panel *panel,
		struct sirfsoc_video_timings *timings)
{
	struct panel_drv_data *pdata = to_panel_data(panel);
	struct sirfsoc_vdss_output *in = pdata->in;

	return in->ops.rgb->check_timings(in, timings);
}

static struct sirfsoc_vdss_driver panel_rgb_ops = {
	.connect	= panel_rgb_connect,
	.disconnect	= panel_rgb_disconnect,

	.enable		= panel_rgb_enable,
	.disable	= panel_rgb_disable,

	.set_timings	= panel_rgb_set_timings,
	.get_timings	= panel_rgb_get_timings,
	.check_timings	= panel_rgb_check_timings,

};

static int panel_rgb_probe_of(struct platform_device *pdev)
{
	struct panel_drv_data *pdata = platform_get_drvdata(pdev);
	struct device_node *node;
	struct sirfsoc_vdss_output *in;
	struct display_timings *timings;
	struct videomode vm;
	int gpio;
	const char *source;
	struct i2c_client *client;

	node = pdev->dev.of_node;

	if (!of_machine_is_compatible("sirf,atlas7")) {
		of_property_read_u32(node, "power-vdd", &pdata->power_vdd);
		of_property_read_u32(node, "power-vcc", &pdata->power_vcc);
		of_property_read_u32(node, "power-vee", &pdata->power_vee);

		if (!pdata->power_vcc) {
			gpio = of_get_named_gpio(node, "vcc-gpios", 0);

			if (gpio_is_valid(gpio)) {
				pdata->vcc_gpio = gpio;
			} else {
				dev_err(&pdev->dev,
					"failed to parse vcc gpio\n");
				return gpio;
			}
		}
	}

	of_property_read_u32(node, "data-lines", &pdata->data_lines);

	timings = of_get_display_timings(node);
	if (!timings) {
		dev_err(&pdev->dev, "failed to get video timing\n");
		return -ENOENT;
	}

	videomode_from_timings(timings, &vm, timings->native_mode);
	display_timings_release(timings);
	videomode_to_sirfsoc_video_timings(&vm, &pdata->timings);

	of_property_read_string(node, "source", &source);
	in = sirfsoc_vdss_find_output(source);
	if (IS_ERR(in)) {
		dev_err(&pdev->dev, "failed to find video source\n");
		return -EINVAL;
	}
	pdata->in = in;

	if (!of_machine_is_compatible("sirf,atlas7")) {
		node = of_parse_phandle(pdev->dev.of_node, "panel-ctrl", 0);
		if (!node) {
			dev_err(&pdev->dev,
				"failed to find panel control node\n");
			return -EINVAL;
		}
		client = of_find_i2c_device_by_node(node);
		of_node_put(node);
		if (!client) {
			dev_err(&pdev->dev,
				"failed to get panel i2c client\n");
			return -EINVAL;
		}

		pdata->client = client;
	}

	return 0;
}

static int panel_rgb_probe(struct platform_device *pdev)
{
	struct panel_drv_data *pdata;
	struct sirfsoc_vdss_panel *panel;
	int r;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (pdata == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, pdata);

	if (pdev->dev.of_node) {
		r = panel_rgb_probe_of(pdev);
		if (r)
			return r;
	} else {
		return -ENODEV;
	}

	panel = &pdata->panel;
	panel->dev = &pdev->dev;
	panel->driver = &panel_rgb_ops;
	panel->type = SIRFSOC_PANEL_RGB;
	panel->owner = THIS_MODULE;
	panel->timings = pdata->timings;
	panel->phy.rgb.data_lines = pdata->data_lines;
	panel->name = DEFAULT_RGB_PANEL_NAME;

	r = sirfsoc_vdss_register_panel(panel);
	if (r) {
		dev_err(&pdev->dev, "Failed to register panel\n");
		goto err_reg;
	}

	return 0;

err_reg:
	return r;
}

static int __exit panel_rgb_remove(struct platform_device *pdev)
{
	struct panel_drv_data *pdata = platform_get_drvdata(pdev);
	struct sirfsoc_vdss_panel *panel = &pdata->panel;

	sirfsoc_vdss_unregister_panel(panel);

	panel_rgb_disable(panel);
	panel_rgb_disconnect(panel);

	return 0;
}

static const struct of_device_id panel_rgb_of_match[] = {
	{ .compatible = "rgb-panel", },
	{},
};

MODULE_DEVICE_TABLE(of, panel_rgb_of_match);

static struct platform_driver panel_rgb_driver = {
	.probe = panel_rgb_probe,
	.remove = panel_rgb_remove,
	.driver = {
		.name = "rgb-panel",
		.owner = THIS_MODULE,
		.of_match_table = panel_rgb_of_match,
	},
};

static int __init panel_rgb_init(void)
{
	return platform_driver_probe(&panel_rgb_driver, panel_rgb_probe);
}

subsys_initcall(panel_rgb_init);

MODULE_DESCRIPTION("Generic RGB Panel Driver");
MODULE_LICENSE("GPL");
