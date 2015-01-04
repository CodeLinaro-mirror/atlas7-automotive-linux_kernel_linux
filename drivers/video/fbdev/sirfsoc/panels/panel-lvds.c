/*
 * CSR sirfsoc lvds-panel driver
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <video/sirfsoc_vdss.h>
#include <video/display_timing.h>
#include <video/of_display_timing.h>

#define DEFAULT_LVDS_PANEL_NAME	"lvds"

struct panel_drv_data {
	struct sirfsoc_vdss_panel panel;
	struct sirfsoc_vdss_output *in;
	int bl_gpio;
	int data_lines;

	struct sirfsoc_video_timings timings;

};

#define to_panel_data(p) container_of(p, struct panel_drv_data, panel)

static int panel_lvds_connect(struct sirfsoc_vdss_panel *panel)
{
	struct panel_drv_data *pdata = to_panel_data(panel);
	struct sirfsoc_vdss_output *in = pdata->in;
	int r = 0;

	if (sirfsoc_vdss_panel_is_connected(panel))
		return 0;

	r = in->ops.lvds->connect(in, panel);
	if (r)
		return r;

	return 0;
}

static void panel_lvds_disconnect(struct sirfsoc_vdss_panel *panel)
{
	struct panel_drv_data *pdata = to_panel_data(panel);
	struct sirfsoc_vdss_output *in = pdata->in;

	if (!sirfsoc_vdss_panel_is_connected(panel))
		return;

	in->ops.lvds->disconnect(in, panel);
}

static int panel_lvds_enable(struct sirfsoc_vdss_panel *panel)
{
	struct panel_drv_data *pdata = to_panel_data(panel);
	struct sirfsoc_vdss_output *in = pdata->in;
	int r;

	if (!sirfsoc_vdss_panel_is_connected(panel))
		return -ENODEV;

	if (sirfsoc_vdss_panel_is_enabled(panel))
		return 0;

	if (pdata->data_lines) {
		enum vdss_lvdsc_fmt fmt;

		in->ops.lvds->set_data_lines(in, pdata->data_lines);

		if (pdata->data_lines == 24)
			fmt = SIRFSOC_VDSS_LVDSC_FMT_VESA_8BIT;
		else if (pdata->data_lines == 18)
			fmt = SIRFSOC_VDSS_LVDSC_FMT_VESA_6BIT;
		else
			return -EINVAL;

		in->ops.lvds->set_fmt(in, fmt);
	}

	in->ops.lvds->set_timings(in, &pdata->timings);

	r = in->ops.lvds->enable(in);
	if (r)
		return r;

	if (gpio_is_valid(pdata->bl_gpio))
		gpio_set_value_cansleep(pdata->bl_gpio, 1);

	panel->state = SIRFSOC_VDSS_PANEL_ENABLED;

	return 0;
}

static void panel_lvds_disable(struct sirfsoc_vdss_panel *panel)
{
	struct panel_drv_data *pdata = to_panel_data(panel);
	struct sirfsoc_vdss_output *in = pdata->in;

	if (!sirfsoc_vdss_panel_is_enabled(panel))
		return;

	if (gpio_is_valid(pdata->bl_gpio))
		gpio_set_value_cansleep(pdata->bl_gpio, 0);

	in->ops.lvds->disable(in);

	panel->state = SIRFSOC_VDSS_PANEL_DISABLED;
}

static void panel_lvds_set_timings(struct sirfsoc_vdss_panel *panel,
	struct sirfsoc_video_timings *timings)
{
	struct panel_drv_data *pdata = to_panel_data(panel);
	struct sirfsoc_vdss_output *in = pdata->in;

	pdata->timings = *timings;
	panel->timings = *timings;

	in->ops.lvds->set_timings(in, timings);
}

static void panel_lvds_get_timings(struct sirfsoc_vdss_panel *panel,
		struct sirfsoc_video_timings *timings)
{
	struct panel_drv_data *pdata = to_panel_data(panel);

	*timings = pdata->timings;
}

static int panel_lvds_check_timings(struct sirfsoc_vdss_panel *panel,
		struct sirfsoc_video_timings *timings)
{
	struct panel_drv_data *pdata = to_panel_data(panel);
	struct sirfsoc_vdss_output *in = pdata->in;

	return in->ops.lvds->check_timings(in, timings);
}

static struct sirfsoc_vdss_driver panel_lvds_ops = {
	.connect	= panel_lvds_connect,
	.disconnect	= panel_lvds_disconnect,

	.enable		= panel_lvds_enable,
	.disable	= panel_lvds_disable,

	.set_timings	= panel_lvds_set_timings,
	.get_timings	= panel_lvds_get_timings,
	.check_timings	= panel_lvds_check_timings,

};

static int panel_lvds_probe_of(struct platform_device *pdev)
{
	struct panel_drv_data *pdata = platform_get_drvdata(pdev);
	struct device_node *node = pdev->dev.of_node;
	struct sirfsoc_vdss_output *in;
	struct display_timings *timings;
	struct videomode vm;
	int gpio;
	const char *source;
	int ret = 0;

	of_property_read_u32(node, "data-lines", &pdata->data_lines);

	gpio = of_get_named_gpio(node, "bl-gpios", 0);
	if (gpio_is_valid(gpio))
		pdata->bl_gpio = gpio;
	else {
		dev_err(&pdev->dev, "failed to parse backlight gpio\n");
		return gpio;
	}

	timings = of_get_display_timings(node);
	if (!timings) {
		dev_err(&pdev->dev, "failed to get video timing\n");
		return -ENOENT;
	}
	videomode_from_timings(timings, &vm, timings->native_mode);
	display_timings_release(timings);

	videomode_to_sirfsoc_video_timings(&vm, &pdata->timings);
	ret = of_property_read_string(node, "source", &source);
	if (ret) {
		dev_err(&pdev->dev, "failed to find video source\n");
		return ret;
	}

	in = sirfsoc_vdss_find_output(source);
	if (IS_ERR(in)) {
		dev_err(&pdev->dev, "failed to find video source\n");
		return PTR_ERR(in);
	}


	pdata->in = in;

	return 0;
}

static int panel_lvds_probe(struct platform_device *pdev)
{
	struct panel_drv_data *pdata;
	struct sirfsoc_vdss_panel *panel;
	int r;

	r = sirfsoc_vdss_set_default_panel_name(DEFAULT_LVDS_PANEL_NAME);
	if (r)
		return r;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (pdata == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, pdata);
	if (pdev->dev.of_node) {
		r = panel_lvds_probe_of(pdev);
		if (r)
			return r;
	} else
		return -ENODEV;

	if (gpio_is_valid(pdata->bl_gpio)) {
		r = devm_gpio_request_one(&pdev->dev, pdata->bl_gpio,
			GPIOF_OUT_INIT_LOW, "panel backlight");
		if (r)
			goto err_gpio;
	}

	panel = &pdata->panel;
	panel->dev = &pdev->dev;
	panel->driver = &panel_lvds_ops;
	panel->type = SIRFSOC_PANEL_LVDS;
	panel->owner = THIS_MODULE;
	panel->timings = pdata->timings;
	panel->phy.lvds.data_lines = pdata->data_lines;
	panel->name = "lvds";

	r = sirfsoc_vdss_register_panel(panel);
	if (r) {
		dev_err(&pdev->dev, "Failed to register panel\n");
		goto err_reg;
	}

	return 0;

err_reg:
err_gpio:
	dev_err(&pdev->dev, "lvds probe error\n");
	return r;
}

static int __exit panel_lvds_remove(struct platform_device *pdev)
{
	struct panel_drv_data *pdata = platform_get_drvdata(pdev);
	struct sirfsoc_vdss_panel *panel = &pdata->panel;

	sirfsoc_vdss_unregister_panel(panel);

	panel_lvds_disable(panel);
	panel_lvds_disconnect(panel);

	return 0;
}

static const struct of_device_id panel_lvds_of_match[] = {
	{ .compatible = "lvds-panel", },
	{},
};

MODULE_DEVICE_TABLE(of, panel_lvds_of_match);

static struct platform_driver panel_lvds_driver = {
	.probe = panel_lvds_probe,
	.remove = __exit_p(panel_lvds_remove),
	.driver = {
		.name = "lvds-panel",
		.owner = THIS_MODULE,
		.of_match_table = panel_lvds_of_match,
	},
};

static int __init panel_lvds_init(void)
{
	return platform_driver_probe(&panel_lvds_driver, panel_lvds_probe);
}

subsys_initcall(panel_lvds_init);

MODULE_AUTHOR("Renwei Wu <Renwei.wu@csr.com>");
MODULE_DESCRIPTION("Generic LVDS Panel Driver");
MODULE_LICENSE("GPL");
