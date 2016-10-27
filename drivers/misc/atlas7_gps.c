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
#include <linux/of_gpio.h>
#include <linux/io.h>
#include <linux/sysfs.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>
#include <linux/mfd/sirfsoc_pwrc.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

#define ATLAS7_GPS_TIMER_INDEX		2
#define ATLAS7_GPS_TIMER_CNT_CTRL	(0 + 4 * ATLAS7_GPS_TIMER_INDEX)
#define ATLAS7_GPS_TIMER_CNT_MATCH	(0x18 + 4 * ATLAS7_GPS_TIMER_INDEX)
#define ATLAS7_GPS_TIMER_CNT		(0x48 +  4 * ATLAS7_GPS_TIMER_INDEX)
#define ATLAS7_GPS_TIMER_CNT_EN		(BIT(0) | BIT(2))

#define ATLAS7_GPS_TIMER_RATE		(1000000)

#define SIRFDRIVER_SAMPLE_FREQUENCE	50
#define BUFFER_LEN			(3 * SIRFDRIVER_SAMPLE_FREQUENCE)

struct dr_sensor_data_set {
	u32 data_set_time_tag;
	u16 valid_data_indication;
	s16 gyro[3];
	s16 acc[3];
	s16 odo;
	s16 rev;
	s16 reserved;
} __packed;

struct dr_sensor_data_buffer {
	u32 buffer_head;
	u32 reserved;
	struct dr_sensor_data_set data_set[BUFFER_LEN];
} __packed;

#define ATLAS7_GPS_DATA_SIZE		(sizeof(struct dr_sensor_data_buffer))

struct atlas7_gps_info {
	struct device *dev;
	struct regmap *regmap;
	struct sirfsoc_pwrc_register *pwrc_reg;
	spinlock_t lock;
	u32 base;
	void __iomem *timer_base;
	void __iomem *shmem_base;
	u32 shmem_size;
	unsigned int gpio;
	struct clk *clk;
	struct clk *timer_clk;
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

	sirfsoc_iobg_lock();
	regmap_read(gps_info->regmap,
		gps_info->base +
		pwrc->pwrc_gnss_ctrl, &tmp);
	val = (tmp>>4) & 0xf;

	regmap_read(gps_info->regmap,
		gps_info->base +
		pwrc->pwrc_gnss_status, &tmp);
	sirfsoc_iobg_unlock();

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
		sirfsoc_iobg_lock();
		regmap_read(gps_info->regmap,
			gps_info->base +
			pwrc->pwrc_gnss_ctrl, &tmp);
			tmp |= 1;

		regmap_write(gps_info->regmap,
				gps_info->base +
				pwrc->pwrc_gnss_ctrl,
				tmp);
		sirfsoc_iobg_unlock();
		break;

	case GNSS_FORCE_POFF:
		sirfsoc_iobg_lock();
		regmap_read(gps_info->regmap,
				gps_info->base +
				pwrc->pwrc_gnss_ctrl, &tmp);
		tmp |= (1<<1);

		regmap_write(gps_info->regmap,
				gps_info->base +
				pwrc->pwrc_gnss_ctrl, tmp);
		sirfsoc_iobg_unlock();
		break;

	case GNSS_SW_RST_OFF:
		sirfsoc_iobg_lock();
		regmap_read(gps_info->regmap,
			gps_info->base +
			pwrc->pwrc_gnss_ctrl, &tmp);

		tmp &= ~(1<<2);
		regmap_write(gps_info->regmap,
				gps_info->base +
				pwrc->pwrc_gnss_ctrl,
				tmp);
		sirfsoc_iobg_unlock();
		break;

	case GNSS_SW_RST_ON:
		sirfsoc_iobg_lock();
		regmap_read(gps_info->regmap,
			gps_info->base +
			pwrc->pwrc_gnss_ctrl, &tmp);
		tmp |= (1<<2);
		regmap_write(gps_info->regmap,
				gps_info->base +
				pwrc->pwrc_gnss_ctrl,
				tmp);
		sirfsoc_iobg_unlock();

		break;

	case GNSS_FORCE_CLR:
		sirfsoc_iobg_lock();
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
		sirfsoc_iobg_unlock();

		break;

	default:
		break;
	}

	return len;
}

static DEVICE_ATTR_RW(atlas7_gps);

static ssize_t atlas7_gps_data_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct atlas7_gps_info *gps_info =
		(struct atlas7_gps_info *) dev_get_drvdata(dev);
	ssize_t size = ATLAS7_GPS_DATA_SIZE;

	if (size > PAGE_SIZE || size > gps_info->shmem_size)
		return 0;

	memcpy_fromio(buf, gps_info->shmem_base, size);

	return size;
}

static DEVICE_ATTR_RO(atlas7_gps_data);

static ssize_t atlas7_gps_time_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct atlas7_gps_info *gps_info =
		(struct atlas7_gps_info *) dev_get_drvdata(dev);
	ssize_t size = 0;
	u32 counter;

	counter = readl(gps_info->timer_base + ATLAS7_GPS_TIMER_CNT);
	size += sprintf(&buf[size], "%x\n", counter);

	return size;
}

static DEVICE_ATTR_RO(atlas7_gps_time);

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

	ret = device_create_file(&pdev->dev, &dev_attr_atlas7_gps_data);
	if (ret)
		dev_err(&pdev->dev,
			"failed to create gps data attribute, %d\n", ret);

	ret = device_create_file(&pdev->dev, &dev_attr_atlas7_gps_time);
	if (ret)
		dev_err(&pdev->dev,
			"failed to create gps time attribute, %d\n", ret);

	return 0;
}

static irqreturn_t atlas7_gps_handler(int irq, void *dev_id)
{
	/* FIXME: requirement not clear, will implement later */

	return IRQ_HANDLED;
}

static irqreturn_t atlas7_gps_gpio_handler(int irq, void *dev_id)
{
	struct atlas7_gps_info *info = dev_id;

	kobject_uevent(&info->dev->kobj, KOBJ_CHANGE);

	return IRQ_HANDLED;
}

#ifdef CONFIG_PM_SLEEP
static int atlas7_gps_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct atlas7_gps_info *info = platform_get_drvdata(pdev);

	clk_disable_unprepare(info->clk);
	clk_disable_unprepare(info->timer_clk);
	return 0;
}

static int atlas7_gps_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct atlas7_gps_info *info = platform_get_drvdata(pdev);
	int ret;

	ret = clk_prepare_enable(info->timer_clk);
	if (ret) {
		dev_err(&pdev->dev, "Error enable timer clock\n");
		return ret;
	}

	ret = clk_prepare_enable(info->clk);
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

static inline int atlas7_gps_get_shmem_info(struct platform_device *pdev,
		u64 *base, u64 *size)
{
	struct device_node *np;
	const __be32 *addrp;
	u64 cm3_base, cm3_size;
	u32 mem_offset, mem_size;
	int ret;

	np = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (!np) {
		dev_err(&pdev->dev, "get memory-region failed\n");
		return -EINVAL;
	}

	addrp = of_get_address(np, 0, &cm3_size, NULL);
	if (addrp == NULL) {
		dev_err(&pdev->dev, "get cm3 address failed\n");
		return -EINVAL;
	}

	cm3_base = of_translate_address(np, addrp);
	if (cm3_base == OF_BAD_ADDR) {
		dev_err(&pdev->dev, "translate cm3 address failed\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(pdev->dev.of_node, "mem-offset",
			&mem_offset);
	if (ret) {
		dev_err(&pdev->dev, "get memory offset failed\n");
		return ret;
	}

	ret = of_property_read_u32(pdev->dev.of_node, "mem-size",
			&mem_size);
	if (ret) {
		dev_err(&pdev->dev, "get memory size failed\n");
		return ret;
	}

	if ((mem_offset + mem_size) >= cm3_size) {
		dev_err(&pdev->dev, "memory value is out of range\n");
		return -EINVAL;
	}

	*base = cm3_base + mem_offset;
	*size = mem_size;

	return 0;
}

static inline int atlas7_gps_get_timer_info(struct platform_device *pdev,
		u64 *base, u64 *size, struct clk **clk)
{
	struct device_node *np;
	const __be32 *addrp;

	np = of_find_compatible_node(NULL, NULL, "sirf,atlas7-tick");
	if (!np) {
		dev_err(&pdev->dev, "find atlas7-tick node failed\n");
		return -ENODEV;
	}

	addrp = of_get_address(np, 0, size, NULL);
	if (addrp == NULL) {
		dev_err(&pdev->dev, "get timer address failed\n");
		return -EINVAL;
	}

	*base = of_translate_address(np, addrp);
	if (*base == OF_BAD_ADDR) {
		dev_err(&pdev->dev, "translate timer address failed\n");
		return -EINVAL;
	}

	*clk = of_clk_get(np, 0);
	if (IS_ERR(*clk)) {
		dev_err(&pdev->dev, "get timer clock failed\n");
		return PTR_ERR(*clk);
	}

	return 0;
}

static inline void atlas7_gps_timer_init(struct atlas7_gps_info *info)
{
	unsigned long timer_ioclk = clk_get_rate(info->timer_clk);
	unsigned long div = timer_ioclk / ATLAS7_GPS_TIMER_RATE - 1;

	writel(U32_MAX, info->timer_base + ATLAS7_GPS_TIMER_CNT_MATCH);
	writel((div << 16) | ATLAS7_GPS_TIMER_CNT_EN,
			info->timer_base + ATLAS7_GPS_TIMER_CNT_CTRL);
}

static int atlas7_gps_probe(struct platform_device *pdev)
{

	struct sirfsoc_pwrc_info *pwrcinfo = dev_get_drvdata(pdev->dev.parent);
	struct atlas7_gps_info *info;
	struct clk *clk;
	int ret, i;
	u64 timer_base, timer_size, shmem_base, shmem_size;

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

	ret = atlas7_gps_get_shmem_info(pdev, &shmem_base, &shmem_size);
	if (ret)
		return ret;

	info->shmem_base = devm_ioremap(&pdev->dev, shmem_base, shmem_size);
	if (!info->shmem_base) {
		dev_err(&pdev->dev, "ioremap share memory failed\n");
		return ret;
	}

	info->shmem_size = (u32)shmem_size;
	info->dev = &pdev->dev;
	info->pwrc_reg = pwrcinfo->pwrc_reg;
	info->regmap  = pwrcinfo->regmap;
	info->base  = pwrcinfo->base;

	if (!info->regmap) {
		dev_err(&pdev->dev, "no regmap!\n");
		return -EINVAL;
	}

	info->gpio = of_get_named_gpio(pdev->dev.of_node, "gps-int-gpio", 0);
	if (!gpio_is_valid(info->gpio)) {
		dev_err(&pdev->dev, "invalid gpio supplied\n");
		return -EINVAL;
	}

	ret = atlas7_gps_get_timer_info(pdev, &timer_base, &timer_size,
			&info->timer_clk);
	if (ret)
		return ret;
	info->timer_base = devm_ioremap(&pdev->dev, timer_base, timer_size);
	if (!info->timer_base) {
		dev_err(&pdev->dev, "ioremap failed\n");
		return -ENOMEM;
	}
	ret = clk_prepare_enable(info->timer_clk);
	if (ret) {
		dev_err(&pdev->dev, "Enable timer clock failed\n");
		return ret;
	}

	clk = devm_clk_get(&pdev->dev, NULL);
	ret = clk_prepare_enable(clk);
	if (ret) {
		dev_err(&pdev->dev, "Error enable clock\n");
		goto err_clk_enable;
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

	ret = devm_gpio_request_one(&pdev->dev, info->gpio, GPIOF_IN,
			"gps-int-gpio");
	if (ret) {
		dev_err(&pdev->dev, "request gpio failed\n");
		goto out;
	}

	ret = devm_request_threaded_irq(&pdev->dev, gpio_to_irq(info->gpio),
			NULL, atlas7_gps_gpio_handler,
			IRQF_TRIGGER_RISING | IRQF_ONESHOT,
			"atlas7_gps_gpio", info);
	if (ret) {
		dev_err(&pdev->dev, "request gpio irq failed\n");
		goto out;
	}

	platform_set_drvdata(pdev, info);
	atlas7_gps_sysfs_init(pdev);
	atlas7_gps_timer_init(info);

	return 0;
out:
	clk_disable_unprepare(clk);
err_clk_enable:
	clk_disable_unprepare(info->timer_clk);
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
