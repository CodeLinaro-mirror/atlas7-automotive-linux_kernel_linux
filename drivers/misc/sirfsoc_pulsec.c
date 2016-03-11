/*
 * Pulse Counter Driver for CSR SiRFSoC
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/of.h>

#include <misc/sirfsoc_pulsec.h>

#define SIRFSOC_PULSEC_LWH_CNT		0x00
#define SIRFSOC_PULSEC_RWH_CNT		0x04
#define SIRFSOC_PULSEC_CTRL		0x08
#define SIRFSOC_PULSEC_LWH_PRE_CNT	0x14
#define SIRFSOC_PULSEC_RWH_PRE_CNT	0x18

#define PULSEC_ENABLE			BIT(0)
#define PULSEC_LPRE_EN			BIT(1)
#define PULSEC_RPRE_EN			BIT(2)
#define PULSEC_FORWARD_LOW		BIT(6)

struct sirfsoc_pulsec {
	void __iomem	*base;
	struct clk	*clk;
	struct mutex	lock;
};

static struct sirfsoc_pulsec *pulsec;

/*
 * Set the counter increase direction
 * which will be used by rearvieaw driver
 */
void sirfsoc_pulsec_set_direction(int direction)
{
	u32 val;

	if (!pulsec)
		return;

	mutex_lock(&pulsec->lock);
	val = readl(pulsec->base + SIRFSOC_PULSEC_CTRL);
	if (direction == SIRFSOC_PULSEC_BACKWARD)
		val = val & (~PULSEC_FORWARD_LOW);
	else
		val = val | PULSEC_FORWARD_LOW;
	writel(val, pulsec->base + SIRFSOC_PULSEC_CTRL);
	mutex_unlock(&pulsec->lock);
}
EXPORT_SYMBOL(sirfsoc_pulsec_set_direction);

void sirfsoc_pulsec_get_count(struct pulsec_data *pdata)
{
	mutex_lock(&pulsec->lock);
	pdata->left_num = readl(pulsec->base + SIRFSOC_PULSEC_LWH_CNT);
	pdata->right_num = readl(pulsec->base + SIRFSOC_PULSEC_RWH_CNT);
	mutex_unlock(&pulsec->lock);
}
EXPORT_SYMBOL(sirfsoc_pulsec_get_count);

void sirfsoc_pulsec_set_count(struct pulsec_data *pdata)
{
	u32 val;

	mutex_lock(&pulsec->lock);
	writel(pdata->left_num, pulsec->base + SIRFSOC_PULSEC_LWH_PRE_CNT);
	writel(pdata->right_num, pulsec->base + SIRFSOC_PULSEC_RWH_PRE_CNT);
	val = readl(pulsec->base + SIRFSOC_PULSEC_CTRL);
	writel(val | PULSEC_LPRE_EN | PULSEC_RPRE_EN,
			pulsec->base + SIRFSOC_PULSEC_CTRL);
	msleep(100);
	writel(val & (~(PULSEC_LPRE_EN | PULSEC_RPRE_EN)),
			pulsec->base + SIRFSOC_PULSEC_CTRL);
	mutex_unlock(&pulsec->lock);
}
EXPORT_SYMBOL(sirfsoc_pulsec_set_count);

/*
 * Driver who will set and get the count of pulse counter
 * must use the function in probe to check if the pulse
 * counter driver has been inited.
 */
bool sirfsoc_pulsec_inited(void)
{
	if (pulsec)
		return true;

	return false;
}
EXPORT_SYMBOL(sirfsoc_pulsec_inited);

#ifdef DEBUG
static ssize_t pulsec_show_lnum(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sirfsoc_pulsec *pulsec_dev = platform_get_drvdata(pdev);
	u32 num;

	num = readl(pulsec_dev->base + SIRFSOC_PULSEC_LWH_CNT);

	return sprintf(buf, "%d\n", num);
}

static ssize_t pulsec_store_lnum(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sirfsoc_pulsec *pulsec_dev = platform_get_drvdata(pdev);
	u32 num;
	u32 val;
	int ret;

	ret = kstrtou32(buf, 0, &num);
	if (ret)
		return ret;

	mutex_lock(&pulsec_dev->lock);
	writel(num, pulsec_dev->base + SIRFSOC_PULSEC_LWH_PRE_CNT);
	val = readl(pulsec_dev->base + SIRFSOC_PULSEC_CTRL);
	writel(val | PULSEC_LPRE_EN, pulsec_dev->base + SIRFSOC_PULSEC_CTRL);
	msleep(100);
	writel(val & (~PULSEC_LPRE_EN), pulsec_dev->base + SIRFSOC_PULSEC_CTRL);
	mutex_unlock(&pulsec_dev->lock);

	return count;
}

static ssize_t pulsec_show_rnum(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sirfsoc_pulsec *pulsec_dev = platform_get_drvdata(pdev);
	u32 num;

	num = readl(pulsec_dev->base + SIRFSOC_PULSEC_RWH_CNT);

	return sprintf(buf, "%d\n", num);
}

static ssize_t pulsec_store_rnum(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sirfsoc_pulsec *pulsec_dev = platform_get_drvdata(pdev);
	u32 num;
	u32 val;
	int ret;

	ret = kstrtou32(buf, 0, &num);
	if (ret)
		return ret;

	mutex_lock(&pulsec_dev->lock);
	writel(num, pulsec_dev->base + SIRFSOC_PULSEC_RWH_PRE_CNT);
	val = readl(pulsec_dev->base + SIRFSOC_PULSEC_CTRL);
	writel(val | PULSEC_RPRE_EN, pulsec_dev->base + SIRFSOC_PULSEC_CTRL);
	msleep(100);
	writel(val & (~PULSEC_RPRE_EN), pulsec_dev->base + SIRFSOC_PULSEC_CTRL);
	mutex_unlock(&pulsec_dev->lock);

	return count;
}

static DEVICE_ATTR(l_num, S_IWUSR | S_IRUGO,
			pulsec_show_lnum, pulsec_store_lnum);
static DEVICE_ATTR(r_num, S_IWUSR | S_IRUGO,
			pulsec_show_rnum, pulsec_store_rnum);

static struct attribute *pulsec_attributes[] = {
	&dev_attr_l_num.attr,
	&dev_attr_r_num.attr,
	NULL
};

static const struct attribute_group pulsec_attr_group = {
	.attrs = pulsec_attributes,
};
#endif /* DEBUG */

#ifdef CONFIG_PM_SLEEP
static int sirfsoc_pulsec_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sirfsoc_pulsec *pulsec_dev = platform_get_drvdata(pdev);
	u32 val;

	val = readl(pulsec_dev->base + SIRFSOC_PULSEC_CTRL);
	writel(val & (~PULSEC_ENABLE), pulsec_dev->base + SIRFSOC_PULSEC_CTRL);
	clk_disable_unprepare(pulsec_dev->clk);

	return 0;
}

static int sirfsoc_pulsec_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sirfsoc_pulsec *pulsec_dev = platform_get_drvdata(pdev);
	u32 val;

	clk_prepare_enable(pulsec_dev->clk);
	val = readl(pulsec_dev->base + SIRFSOC_PULSEC_CTRL);
	writel(val | PULSEC_ENABLE, pulsec_dev->base + SIRFSOC_PULSEC_CTRL);

	return 0;
}
#endif

static const struct dev_pm_ops sirfsoc_pulsec_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sirfsoc_pulsec_suspend, sirfsoc_pulsec_resume)
};

static int sirfsoc_pulsec_probe(struct platform_device *pdev)
{
	struct sirfsoc_pulsec *pulsec_dev;
	struct resource *mem_res;
	int ret;
	u32 val;

	pulsec_dev = devm_kzalloc(&pdev->dev,
			sizeof(struct sirfsoc_pulsec), GFP_KERNEL);
	if (!pulsec_dev)
		return -ENOMEM;

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pulsec_dev->base = devm_ioremap_resource(&pdev->dev, mem_res);
	if (!pulsec_dev->base) {
		dev_err(&pdev->dev, "Failed to map mem region\n");
		return -ENOMEM;
	}

	mutex_init(&pulsec_dev->lock);

	pulsec_dev->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pulsec_dev->clk)) {
		dev_err(&pdev->dev, "Failed to get clk\n");
		return PTR_ERR(pulsec_dev->clk);
	}

	ret = clk_prepare_enable(pulsec_dev->clk);
	if (ret) {
		dev_err(&pdev->dev, "Fail to enable clk\n");
		return ret;
	}

	val = readl(pulsec_dev->base + SIRFSOC_PULSEC_CTRL);
	writel(val | PULSEC_ENABLE | PULSEC_FORWARD_LOW,
		pulsec_dev->base + SIRFSOC_PULSEC_CTRL);

#ifdef DEBUG
	ret = sysfs_create_group(&pdev->dev.kobj, &pulsec_attr_group);
	if (ret) {
		dev_err(&pdev->dev, "Fail to create sysfs group\n");
		clk_disable_unprepare(pulsec_dev->clk);
		return ret;
	}
#endif

	platform_set_drvdata(pdev, pulsec_dev);
	pulsec = pulsec_dev;

	return 0;
}

static int sirfsoc_pulsec_remove(struct platform_device *pdev)
{
	struct sirfsoc_pulsec *pulsec_dev = platform_get_drvdata(pdev);
	u32 val;

	pulsec = NULL;
#ifdef DEBUG
	sysfs_remove_group(&pdev->dev.kobj, &pulsec_attr_group);
#endif

	val = readl(pulsec_dev->base + SIRFSOC_PULSEC_CTRL);
	writel(val & (~PULSEC_ENABLE), pulsec_dev->base + SIRFSOC_PULSEC_CTRL);

	clk_disable_unprepare(pulsec_dev->clk);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id sirfsoc_pulsec_of_match[] = {
	{.compatible = "sirf,prima2-pulsec", },
	{}
};
MODULE_DEVICE_TABLE(of, sirfsoc_pulsec_of_match);

static struct platform_driver sirfsoc_pulsec_driver = {
	.driver	= {
		.owner		= THIS_MODULE,
		.name		= "sirfsoc_pulsec",
		.pm		= &sirfsoc_pulsec_pm_ops,
		.of_match_table	= sirfsoc_pulsec_of_match,
	},
	.probe		= sirfsoc_pulsec_probe,
	.remove		= sirfsoc_pulsec_remove,
};

module_platform_driver(sirfsoc_pulsec_driver);

MODULE_DESCRIPTION("SiRFSoC Pulse Counter driver");
MODULE_LICENSE("GPL v2");
