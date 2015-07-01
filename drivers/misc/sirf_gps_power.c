/*
 *Copyright (C) 2007 SiRF Technology, Inc
 *
 *This file is licensed under the terms of the GNU General Public
 *License version 2. This program is licensed "as is" without any
 *warranty of any kind, whether express or implied.
 */

#include <linux/slab.h>
#include <linux/export.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/sysfs.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>
#include <linux/mfd/sirfsoc_pwrc.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

struct sirfsoc_gps_power_info {
	struct device *dev;
	struct regmap *regmap;
	struct sirfsoc_pwrc_register *pwrc_reg;
	spinlock_t lock;
	u32 base;
};


enum sirfsoc_gps_power_config {
	GNSS_FORCE_PON = 0,
	GNSS_SW_RST_OFF,
	GNSS_SW_RST_ON,
	GNSS_FORCE_CLR,
	GNSS_FORCE_POFF,
};

static ssize_t gps_power_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct sirfsoc_gps_power_info *gps_power_info =
		(struct sirfsoc_gps_power_info *)dev_get_drvdata(dev);
	struct sirfsoc_pwrc_register *pwrc = gps_power_info->pwrc_reg;

	ssize_t count = 0;
	u32 tmp = 0;
	u32 val = 0;

	regmap_read(gps_power_info->regmap,
		gps_power_info->base +
		pwrc->pwrc_gnss_ctrl, &tmp);
	val = (tmp>>4) & 0xf;

	regmap_read(gps_power_info->regmap,
		gps_power_info->base +
		pwrc->pwrc_gnss_status, &tmp);

	tmp = (tmp>>4) & 0xffff;
	val |= (tmp<<4);

	count += sprintf(&buf[count], "%x\n", val);

	return count;
}


static ssize_t gps_power_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct sirfsoc_gps_power_info *gps_power_info =
		(struct sirfsoc_gps_power_info *)
		dev_get_drvdata(dev);
	struct sirfsoc_pwrc_register *pwrc =
			gps_power_info->pwrc_reg;
	u32 gps_power_config = 0;
	u32 tmp = 0;

	if (sscanf(buf, "%x\n",
				&gps_power_config) != 1)
		return -EINVAL;

	switch (gps_power_config) {
	case GNSS_FORCE_PON:
		regmap_read(gps_power_info->regmap,
			gps_power_info->base +
			pwrc->pwrc_gnss_ctrl, &tmp);
			tmp |= 1;

		regmap_write(gps_power_info->regmap,
				gps_power_info->base +
				pwrc->pwrc_gnss_ctrl,
				tmp);
		break;

	case GNSS_FORCE_POFF:
		regmap_read(gps_power_info->regmap,
				gps_power_info->base +
				pwrc->pwrc_gnss_ctrl, &tmp);
		tmp |= (1<<1);

		regmap_write(gps_power_info->regmap,
				gps_power_info->base +
				pwrc->pwrc_gnss_ctrl, tmp);
		break;

	case GNSS_SW_RST_OFF:

		regmap_read(gps_power_info->regmap,
			gps_power_info->base +
			pwrc->pwrc_gnss_ctrl, &tmp);

		tmp &= ~(1<<2);
		regmap_write(gps_power_info->regmap,
				gps_power_info->base +
				pwrc->pwrc_gnss_ctrl,
				tmp);
		break;

	case GNSS_SW_RST_ON:
		regmap_read(gps_power_info->regmap,
			gps_power_info->base +
			pwrc->pwrc_gnss_ctrl, &tmp);
		tmp |= (1<<2);
		regmap_write(gps_power_info->regmap,
				gps_power_info->base +
				pwrc->pwrc_gnss_ctrl,
				tmp);

		break;

	case GNSS_FORCE_CLR:
		regmap_read(gps_power_info->regmap,
			gps_power_info->base +
			pwrc->pwrc_gnss_ctrl, &tmp);

		tmp |= (1<<14);
		regmap_write(gps_power_info->regmap,
				gps_power_info->base +
				pwrc->pwrc_gnss_ctrl,
				tmp);
		tmp &= ~(1<<14);

		regmap_write(gps_power_info->regmap,
				gps_power_info->base +
				pwrc->pwrc_gnss_ctrl,
				tmp);

		break;

	default:
		break;
	}

	return len;
}

static DEVICE_ATTR_RW(gps_power);

static const struct of_device_id sirfsoc_gps_power_ids[] = {
	{ .compatible = "sirf,gps-power"},
};

static int gps_power_sysfs_init(struct platform_device *pdev)
{
	int ret;

	ret = device_create_file(&pdev->dev, &dev_attr_gps_power);
	if (ret)
		dev_err(&pdev->dev,
			"failed to create dram firewall attribute, %d\n",
			ret);

	return 0;
}


static int sirfsoc_gps_power_probe(struct platform_device *pdev)
{

	struct sirfsoc_pwrc_info *pwrcinfo = dev_get_drvdata(pdev->dev.parent);
	struct sirfsoc_gps_power_info *info;
	int ret;

	info = kzalloc(sizeof(struct sirfsoc_gps_power_info), GFP_KERNEL);
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
		ret = -EINVAL;
		goto out;
	}

	platform_set_drvdata(pdev, info);
	gps_power_sysfs_init(pdev);

	return 0;
out:
	kfree(info);
	return ret;
}

static struct platform_driver sirfsoc_gps_power_driver = {
	.probe = sirfsoc_gps_power_probe,
	.driver = {
		.name = "gps-power",
		.owner = THIS_MODULE,
		.of_match_table = sirfsoc_gps_power_ids,

	},
};

module_platform_driver(sirfsoc_gps_power_driver);

