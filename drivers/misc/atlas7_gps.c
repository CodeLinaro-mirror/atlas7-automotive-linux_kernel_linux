/*
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

#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/sysfs.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>
#include <linux/mfd/sirfsoc_pwrc.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

struct atlas7_gps_info {
	struct device *dev;
	struct regmap *regmap;
	struct sirfsoc_pwrc_register *pwrc_reg;
	spinlock_t lock;
	u32 base;
	struct clk *clk;
	int virq[4];
};


enum atlas7_gps_config {
	GNSS_FORCE_PON = 0,
	GNSS_SW_RST_OFF,
	GNSS_SW_RST_ON,
	GNSS_FORCE_CLR,
	GNSS_FORCE_POFF,
};

static ssize_t atlas7_gps_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct atlas7_gps_info *gps_info =
		(struct atlas7_gps_info *)dev_get_drvdata(dev);
	struct sirfsoc_pwrc_register *pwrc = gps_info->pwrc_reg;

	ssize_t count = 0;
	u32 tmp = 0;
	u32 val = 0;

	regmap_read(gps_info->regmap,
		gps_info->base +
		pwrc->pwrc_gnss_ctrl, &tmp);
	val = (tmp>>4) & 0xf;

	regmap_read(gps_info->regmap,
		gps_info->base +
		pwrc->pwrc_gnss_status, &tmp);

	tmp = (tmp>>4) & 0xffff;
	val |= (tmp<<4);

	count += sprintf(&buf[count], "%x\n", val);

	return count;
}


static ssize_t atlas7_gps_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct atlas7_gps_info *gps_info =
		(struct atlas7_gps_info *)
		dev_get_drvdata(dev);
	struct sirfsoc_pwrc_register *pwrc =
			gps_info->pwrc_reg;
	u32 gps_config = 0;
	u32 tmp = 0;

	if (sscanf(buf, "%x\n",
				&gps_config) != 1)
		return -EINVAL;

	switch (gps_config) {
	case GNSS_FORCE_PON:
		regmap_read(gps_info->regmap,
			gps_info->base +
			pwrc->pwrc_gnss_ctrl, &tmp);
			tmp |= 1;

		regmap_write(gps_info->regmap,
				gps_info->base +
				pwrc->pwrc_gnss_ctrl,
				tmp);
		break;

	case GNSS_FORCE_POFF:
		regmap_read(gps_info->regmap,
				gps_info->base +
				pwrc->pwrc_gnss_ctrl, &tmp);
		tmp |= (1<<1);

		regmap_write(gps_info->regmap,
				gps_info->base +
				pwrc->pwrc_gnss_ctrl, tmp);
		break;

	case GNSS_SW_RST_OFF:

		regmap_read(gps_info->regmap,
			gps_info->base +
			pwrc->pwrc_gnss_ctrl, &tmp);

		tmp &= ~(1<<2);
		regmap_write(gps_info->regmap,
				gps_info->base +
				pwrc->pwrc_gnss_ctrl,
				tmp);
		break;

	case GNSS_SW_RST_ON:
		regmap_read(gps_info->regmap,
			gps_info->base +
			pwrc->pwrc_gnss_ctrl, &tmp);
		tmp |= (1<<2);
		regmap_write(gps_info->regmap,
				gps_info->base +
				pwrc->pwrc_gnss_ctrl,
				tmp);

		break;

	case GNSS_FORCE_CLR:
		regmap_read(gps_info->regmap,
			gps_info->base +
			pwrc->pwrc_gnss_ctrl, &tmp);

		tmp |= (1<<14);
		regmap_write(gps_info->regmap,
				gps_info->base +
				pwrc->pwrc_gnss_ctrl,
				tmp);
		tmp &= ~(1<<14);

		regmap_write(gps_info->regmap,
				gps_info->base +
				pwrc->pwrc_gnss_ctrl,
				tmp);

		break;

	default:
		break;
	}

	return len;
}

static DEVICE_ATTR_RW(atlas7_gps);

static const struct of_device_id atlas7_gps_ids[] = {
	{ .compatible = "sirf,atlas7-gps"},
};

static int atlas7_gps_sysfs_init(struct platform_device *pdev)
{
	int ret;

	ret = device_create_file(&pdev->dev, &dev_attr_atlas7_gps);
	if (ret)
		dev_err(&pdev->dev,
			"failed to create dram firewall attribute, %d\n",
			ret);

	return 0;
}

static irqreturn_t atlas7_gps_handler(int irq, void *dev_id)
{
	/* FIXME: requirement not clear, will implement later */

	return IRQ_HANDLED;
}

#ifdef CONFIG_PM_SLEEP
static int atlas7_gps_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct atlas7_gps_info *info = platform_get_drvdata(pdev);
	struct clk *clk = info->clk;

	clk_disable_unprepare(clk);
	return 0;
}

static int atlas7_gps_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct atlas7_gps_info *info = platform_get_drvdata(pdev);
	struct clk *clk = info->clk;
	int ret;

	ret = clk_prepare_enable(clk);
	if (ret) {
		dev_err(&pdev->dev, "Error enable clock\n");
		return ret;
	}

	ret = device_reset(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to reset\n");
		return ret;
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(atlas7_gps_pm_ops,
		atlas7_gps_suspend, atlas7_gps_resume);

static int atlas7_gps_probe(struct platform_device *pdev)
{

	struct sirfsoc_pwrc_info *pwrcinfo = dev_get_drvdata(pdev->dev.parent);
	struct atlas7_gps_info *info;
	struct clk *clk;
	int ret, i;

	static const char * const gps_virq_name[] = {
		"GNSS_PON_REQ",
		"GNSS_POFF_REQ",
		"GNSS_PON_ACK",
		"GNSS_POFF_ACK",
	};

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	if (!pwrcinfo)
		return -ENXIO;

	info->dev = &pdev->dev;
	info->pwrc_reg = pwrcinfo->pwrc_reg;
	info->regmap  = pwrcinfo->regmap;
	info->base  = pwrcinfo->base;

	if (!info->regmap) {
		dev_err(&pdev->dev, "no regmap!\n");
		return -EINVAL;
	}

	clk = devm_clk_get(&pdev->dev, NULL);
	ret = clk_prepare_enable(clk);
	if (ret) {
		dev_err(&pdev->dev, "Error enable clock\n");
		return ret;
	}
	info->clk  = clk;

	ret = device_reset(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to reset\n");
		goto out;
	}
	for (i = 0; i < ARRAY_SIZE(gps_virq_name); i++) {

		info->virq[i] = of_irq_get(pdev->dev.of_node, i);
		if (info->virq[i] <= 0) {
			dev_info(&pdev->dev,
				"Unable to find IRQ for GPS. err=%d\n",
				info->virq[i]);
			goto out;
		}
		irq_set_status_flags(info->virq[i], IRQ_NOAUTOEN);
		ret = devm_request_threaded_irq(&pdev->dev, info->virq[i],
				NULL, atlas7_gps_handler,
				0, gps_virq_name[i], info);
		if (ret < 0) {
			dev_err(&pdev->dev, "Failed to request IRQ: #%d: %d\n",
				info->virq[i], ret);
			goto out;
		}
	}

	platform_set_drvdata(pdev, info);
	atlas7_gps_sysfs_init(pdev);

	return 0;
out:
	clk_disable_unprepare(clk);
	return ret;
}

static struct platform_driver atlas7_gps_driver = {
	.probe = atlas7_gps_probe,
	.driver = {
		.name = "atlas7_gps",
		.owner = THIS_MODULE,
		.of_match_table = atlas7_gps_ids,
		.pm = &atlas7_gps_pm_ops,
	},
};

module_platform_driver(atlas7_gps_driver);
