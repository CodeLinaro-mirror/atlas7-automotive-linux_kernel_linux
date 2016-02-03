/*
 * CSR sirfsoc HDMI-panel driver
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

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <video/sirfsoc_vdss.h>
#include <video/display_timing.h>
#include <video/of_display_timing.h>

#define DEFAULT_HDMI_PANEL_NAME	"hdmi"

struct panel_drv_data {
	struct sirfsoc_vdss_panel panel;
	struct sirfsoc_vdss_output *in;
	int data_lines;

	struct sirfsoc_video_timings timings;
};

#define to_panel_data(p) container_of(p, struct panel_drv_data, panel)

static int panel_hdmi_connect(struct sirfsoc_vdss_panel *panel)
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

static void panel_hdmi_disconnect(struct sirfsoc_vdss_panel *panel)
{
	struct panel_drv_data *pdata = to_panel_data(panel);
	struct sirfsoc_vdss_output *in = pdata->in;

	if (!sirfsoc_vdss_panel_is_connected(panel))
		return;

	in->ops.rgb->disconnect(in, panel);
}
static int panel_hdmi_enable(struct sirfsoc_vdss_panel *panel)
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

	if (sirfsoc_vdss_panel_find_encoder())
		sirfsoc_vdss_panel_enable_encoder();

	panel->state = SIRFSOC_VDSS_PANEL_ENABLED;

	return 0;
}

static void panel_hdmi_disable(struct sirfsoc_vdss_panel *panel)
{
	struct panel_drv_data *pdata = to_panel_data(panel);
	struct sirfsoc_vdss_output *in = pdata->in;

	if (!sirfsoc_vdss_panel_is_enabled(panel))
		return;

	in->ops.rgb->disable(in);

	panel->state = SIRFSOC_VDSS_PANEL_DISABLED;
}

static void panel_hdmi_set_timings(struct sirfsoc_vdss_panel *panel,
	struct sirfsoc_video_timings *timings)
{
	struct panel_drv_data *pdata = to_panel_data(panel);
	struct sirfsoc_vdss_output *in = pdata->in;

	pdata->timings = *timings;
	panel->timings = *timings;

	in->ops.rgb->set_timings(in, timings);
}

static void panel_hdmi_get_timings(struct sirfsoc_vdss_panel *panel,
		struct sirfsoc_video_timings *timings)
{
	struct panel_drv_data *pdata = to_panel_data(panel);

	*timings = pdata->timings;
}

static int panel_hdmi_check_timings(struct sirfsoc_vdss_panel *panel,
		struct sirfsoc_video_timings *timings)
{
	struct panel_drv_data *pdata = to_panel_data(panel);
	struct sirfsoc_vdss_output *in = pdata->in;

	return in->ops.rgb->check_timings(in, timings);
}

static struct sirfsoc_vdss_driver panel_hdmi_ops = {
	.connect	= panel_hdmi_connect,
	.disconnect	= panel_hdmi_disconnect,

	.enable		= panel_hdmi_enable,
	.disable	= panel_hdmi_disable,

	.set_timings	= panel_hdmi_set_timings,
	.get_timings	= panel_hdmi_get_timings,
	.check_timings	= panel_hdmi_check_timings,

};

static int panel_hdmi_probe_of(struct platform_device *pdev)
{
	struct panel_drv_data *pdata = platform_get_drvdata(pdev);
	struct device_node *node = pdev->dev.of_node;
	struct sirfsoc_vdss_output *in;
	struct display_timings *timings;
	struct videomode vm;
	const char *source;

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

	return 0;
}

static int panel_hdmi_probe(struct platform_device *pdev)
{
	struct panel_drv_data *pdata;
	struct sirfsoc_vdss_panel *panel;
	int ret;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	platform_set_drvdata(pdev, pdata);

	ret = panel_hdmi_probe_of(pdev);
	if (ret)
		return ret;

	panel = &pdata->panel;
	panel->dev = &pdev->dev;
	panel->driver = &panel_hdmi_ops;
	/*FIXME set the driver type to HDMI */
	panel->type = SIRFSOC_PANEL_HDMI;
	panel->owner = THIS_MODULE;
	panel->timings = pdata->timings;
	panel->phy.rgb.data_lines = pdata->data_lines;
	panel->name = DEFAULT_HDMI_PANEL_NAME;

	ret = sirfsoc_vdss_register_panel(panel);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register panel\n");
		goto err_reg;
	}

	return 0;

err_reg:
	return ret;
}

static int __exit panel_hdmi_remove(struct platform_device *pdev)
{
	struct panel_drv_data *pdata = platform_get_drvdata(pdev);
	struct sirfsoc_vdss_panel *panel = &pdata->panel;

	sirfsoc_vdss_unregister_panel(panel);

	panel_hdmi_disable(panel);
	panel_hdmi_disconnect(panel);

	return 0;
}

static const struct of_device_id panel_hdmi_of_match[] = {
	{ .compatible = "hdmi-panel", },
	{},
};

MODULE_DEVICE_TABLE(of, panel_hdmi_of_match);

static struct platform_driver panel_hdmi_driver = {
	.probe = panel_hdmi_probe,
	.remove = panel_hdmi_remove,
	.driver = {
		.name = "hdmi-panel",
		.owner = THIS_MODULE,
		.of_match_table = panel_hdmi_of_match,
	},
};

static int __init panel_hdmi_init(void)
{
	return platform_driver_probe(&panel_hdmi_driver, panel_hdmi_probe);
}

subsys_initcall(panel_hdmi_init);

MODULE_DESCRIPTION("Generic HDMI Panel Driver");
MODULE_LICENSE("GPL");
