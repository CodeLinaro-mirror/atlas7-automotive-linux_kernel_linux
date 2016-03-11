/*
 *
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

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>

#define A7CA_BT_RESET           _IO('B', 0x1)
#define A7CA_BT_TRIM_READ       _IO('B', 0x2)
#define A7CA_BT_TRIM_WRITE      _IO('B', 0x3)
#define A7CA_BT_TRIM_READ_WRITE      _IO('B', 0x4)
#define A7CA_BT_SINGLE_MODE      _IO('B', 0x5)

struct a7ca_bt_trim {
	u32 reg;
	u32 val;
};

#define SIRFSOC_A7CA_XTAL_AUX	0x04

#define SIRFSOC_A7CA_ENABLE_IN	BIT(0)
#define SIRFSOC_A7CA_LPC_CLK	BIT(8)

#define reg_read_write(reg, val) writel(readl(reg)|val, reg)

/* clocks required by atlas7 A7CA */
static const char *const a7ca_clks[] = {
	"a7ca_btss",
	"a7ca_btslow",
	"a7ca_io",
	"analogtest_xin"
};

#define NUM_CLKS	ARRAY_SIZE(a7ca_clks)

struct a7ca_bt_dev {
	struct cdev cdev;
	void __iomem *base;
	struct a7ca_bt_trim bt_trim;
	struct clk *clks[NUM_CLKS];
	struct regulator *regulator;
	struct platform_device *pdev;
	struct miscdevice miscdev;
};

static int a7ca_bt_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int a7ca_bt_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static int a7ca_bt_hw_enable(struct a7ca_bt_dev *dev)
{
	int err = 0;
	int i;

	for (i = 0; i < NUM_CLKS; i++)
		clk_prepare_enable(dev->clks[i]);

	/* enable a7ca internal clks */
	writel(SIRFSOC_A7CA_ENABLE_IN | SIRFSOC_A7CA_LPC_CLK,
		dev->base + SIRFSOC_A7CA_XTAL_AUX);

	err = regulator_enable(dev->regulator);
	if (err)
		goto out;

out:
	return err;
}

static int a7ca_bt_hw_disable(struct a7ca_bt_dev *dev)
{
	int i;

	/* gate A7CA internal clocks */
	writel(0, dev->base + SIRFSOC_A7CA_XTAL_AUX);

	for (i = 0; i < NUM_CLKS; i++)
		clk_disable_unprepare(dev->clks[i]);

	regulator_disable(dev->regulator);

	return 0;
}


static void a7ca_bt_reset(struct a7ca_bt_dev *dev)
{
	a7ca_bt_hw_enable(dev);
}

static long a7ca_bt_ioctl(struct file *filp, unsigned int cmd,
			unsigned long arg)
{
	struct a7ca_bt_trim bt_trim = { 0 };
	int ret = 0;
	struct a7ca_bt_dev *dev =
	    container_of(filp->private_data, struct a7ca_bt_dev, miscdev);

	switch (cmd) {
	case A7CA_BT_RESET:
		pr_debug("a7ca_bt A7CA_BT_RESET\n");
		a7ca_bt_reset(dev);
		break;
	case A7CA_BT_TRIM_READ:
		pr_debug("a7ca_bt A7CA_BT_TRIM_READ\n");
		if (copy_from_user
		    (&dev->bt_trim, (void __user *)arg, sizeof(bt_trim))) {
			return -EFAULT;
		}
		dev->bt_trim.val = sirfsoc_rtc_iobrg_readl(dev->bt_trim.reg);
		if (copy_to_user
		    ((void __user *)arg, &dev->bt_trim, sizeof(bt_trim))) {
			return -EFAULT;
		}
		break;
	case A7CA_BT_TRIM_WRITE:
		pr_debug("a7ca_bt A7CA_BT_TRIM_WRITE\n");
		if (copy_from_user
		    (&dev->bt_trim, (void __user *)arg, sizeof(bt_trim))) {
			return -EFAULT;
		}
		sirfsoc_rtc_iobrg_writel(dev->bt_trim.val, dev->bt_trim.reg);
		break;
	case A7CA_BT_TRIM_READ_WRITE:
		pr_debug("a7ca_bt A7CA_BT_TRIM_WRITE\n");
		if (copy_from_user
		    (&dev->bt_trim, (void __user *)arg, sizeof(bt_trim))) {
			return -EFAULT;
		}
		sirfsoc_rtc_iobrg_writel(sirfsoc_rtc_iobrg_readl
					 (dev->bt_trim.reg) | dev->bt_trim.val,
					 dev->bt_trim.reg);
		break;
	case A7CA_BT_SINGLE_MODE:
		pr_debug("a7ca_bt enter single mode\n");
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return ret;
}

static const struct file_operations a7ca_bt_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = a7ca_bt_ioctl,
	.open = a7ca_bt_open,
	.release = a7ca_bt_release,
};

static struct a7ca_bt_dev a7ca_bt = {
	.miscdev.minor = MISC_DYNAMIC_MINOR,
	.miscdev.name = "a7ca_bt",
	.miscdev.fops = &a7ca_bt_fops,
};

static int a7ca_bt_probe(struct platform_device *pdev)
{
	int err, i;

	struct a7ca_bt_dev *dev = &a7ca_bt;
	struct resource *mem_res;

	dev->pdev = pdev;

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dev->base = devm_ioremap_resource(&pdev->dev, mem_res);
	if (IS_ERR(dev->base)) {
		err = PTR_ERR(dev->base);
		goto out;
	}

	for (i = 0; i < NUM_CLKS; i++) {
		dev->clks[i] = devm_clk_get(&pdev->dev, a7ca_clks[i]);
		if (IS_ERR(dev->clks[i])) {
			err = PTR_ERR(dev->clks[i]);
			dev_err(&pdev->dev, "Clock %s get failed\n",
				a7ca_clks[i]);
			goto out;
		}
	}

	dev->regulator = devm_regulator_get(&pdev->dev, "ldo2");
	if (IS_ERR(dev->regulator)) {
		err = PTR_ERR(dev->regulator);
		dev_err(&pdev->dev, "Regulator ldo2 (BT ldo) get failed\n");
		goto out;
	}

	platform_set_drvdata(pdev, dev);

	a7ca_bt_hw_enable(dev);
	err = misc_register(&dev->miscdev);
	pr_debug("a7ca_bt initilized\n");

out:
	return err;
}

static int a7ca_bt_remove(struct platform_device *pdev)
{
	struct a7ca_bt_dev *dev = platform_get_drvdata(pdev);

	misc_deregister(&dev->miscdev);
	a7ca_bt_hw_disable(dev);

	return 0;
}

static const struct of_device_id a7ca_bt_of_match[] = {
	{.compatible = "sirf,a7ca_bt",},
	{},
};

#ifdef CONFIG_PM_SLEEP
static int a7ca_bt_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct a7ca_bt_dev *cdev = platform_get_drvdata(pdev);

	a7ca_bt_hw_disable(cdev);
	return 0;
}

static int a7ca_bt_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct a7ca_bt_dev *cdev = platform_get_drvdata(pdev);

	a7ca_bt_hw_enable(cdev);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(a7ca_bt_pm_ops, a7ca_bt_suspend, a7ca_bt_resume);

MODULE_DEVICE_TABLE(of, a7ca_bt_of_match);
static struct platform_driver a7ca_bt_driver = {
	.probe = a7ca_bt_probe,
	.remove = a7ca_bt_remove,
	.driver = {
		   .name = "sirf-a7ca_bt",
		   .owner = THIS_MODULE,
		   .of_match_table = a7ca_bt_of_match,
		   .pm = &a7ca_bt_pm_ops,
		   },
};

module_platform_driver(a7ca_bt_driver);

MODULE_LICENSE("GPL");
