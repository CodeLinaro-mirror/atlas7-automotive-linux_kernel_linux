/*
 * CSR SiRF RTC alarm1 Driver
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
#include <linux/moduleparam.h>
#include <linux/err.h>
#include <linux/rtc.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/proc_fs.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>

#define RTC_CN			0x00
#define RTC_ALARM1		0x18
#define RTC_STATUS		0x08
#define SIRFSOC_RTC_AL1E	(1<<6)
#define SIRFSOC_RTC_AL1		(1<<4)

/*
 * RTC alarm1 is specially used for minigps, below are related
 *ioctl definitions:
 */
#define SIRFSOC_MINIGPSRTC_ALM_SET      _IOR('p', 0x13, unsigned long)
#define SIRFSOC_MINIGPSRTC_CNT_READ     _IOR('p', 0x14, unsigned long)
#define SIRFSOC_MINIGPSRTC_ALM_READ     _IOR('p', 0x15, unsigned long)

u32 sirf_sysrtc_base;
int sirf_rtc_alarm1_irq;

static unsigned int  minigpsdata_mmc_base = 0x00b00000;

#undef MODULE_PARAM_PREFIX
#define MODULE_PARAM_PREFIX

static int param_set_minigpsinfo(const char *val, struct kernel_param *kp)
{
	return kstrtoul(val, 0, (unsigned long *)kp->arg);
}
module_param_call(minigps_data_offset,
		param_set_minigpsinfo, NULL, &minigpsdata_mmc_base, 0);

static int sirf_minigpsrtc_alarm_set(unsigned long count)
{
	unsigned long rtc_status_reg, rtc_alarm;

	local_irq_disable();

	rtc_status_reg = sirfsoc_rtc_iobrg_readl(sirf_sysrtc_base + RTC_STATUS);

	/*
	 * AL1E set, which means alarm1 is under way, so first clean it
	 */
	if (rtc_status_reg & SIRFSOC_RTC_AL1E) {
		/*
		 * Clear the RTC status register's alarm bit
		 * Mask out the lower status bits
		 */
		rtc_status_reg &= ~0x50;
		/*
		 * Write 1 into SIRFSOC_RTC_AL1 to force a clear
		 */
		rtc_status_reg |= (SIRFSOC_RTC_AL1);
		/*
		 * Clear the Alarm enable bit
		 */
		rtc_status_reg &= ~(SIRFSOC_RTC_AL1E);

		pr_info("STATUS_REG AL1E clear %lx\n", rtc_status_reg);

		sirfsoc_rtc_iobrg_writel(rtc_status_reg,
					sirf_sysrtc_base + RTC_STATUS);
	}

	/*
	 * Read current RTC_COUNT
	 */
	do {
		rtc_alarm =
			sirfsoc_rtc_iobrg_readl(sirf_sysrtc_base + RTC_CN);
	} while (rtc_alarm !=
			sirfsoc_rtc_iobrg_readl(sirf_sysrtc_base + RTC_CN));

	/*
	 * Set useful alarm1 Count
	 */
	sirfsoc_rtc_iobrg_writel(rtc_alarm + count,
				sirf_sysrtc_base + RTC_ALARM1);
	/*
	 * Mask out the alarm1 status bits
	 */
	rtc_status_reg &= ~0x50;
	/*
	 * Enable the RTC alarm interrupt
	 */
	rtc_status_reg |= SIRFSOC_RTC_AL1E;

	pr_info("STATUS_REG AL1E set %lx\n", rtc_status_reg);

	sirfsoc_rtc_iobrg_writel(rtc_status_reg, sirf_sysrtc_base + RTC_STATUS);
	local_irq_enable();

	return 0;
}

static int sirf_minigpsrtc_cnt_read(unsigned long *count)
{
	/*
	 * TODO: This patch is taken from WinCE - Need to validate this for
	 * correctness. To work around sirfsoc RTC counter double sync logic
	 * fail, read several times to make sure get stable value.
	 */
	do {
		*count = sirfsoc_rtc_iobrg_readl(sirf_sysrtc_base + RTC_CN);
	} while (*count != sirfsoc_rtc_iobrg_readl(sirf_sysrtc_base + RTC_CN));

	return 0;
}

static int sirf_minigpsrtc_alm_read(unsigned long *rtc_alarm)
{
	local_irq_disable();

	do {
		*rtc_alarm =
			sirfsoc_rtc_iobrg_readl(sirf_sysrtc_base + RTC_ALARM1);
	} while (*rtc_alarm !=
			sirfsoc_rtc_iobrg_readl(sirf_sysrtc_base + RTC_ALARM1));

	local_irq_enable();

	return 0;
}

static long sirf_minigpsrtc_ioctl(struct file *filp, unsigned int cmd,
						unsigned long arg)
{
	switch	(cmd) {
	case SIRFSOC_MINIGPSRTC_ALM_SET:
		return sirf_minigpsrtc_alarm_set(arg);

	case SIRFSOC_MINIGPSRTC_CNT_READ:
		return sirf_minigpsrtc_cnt_read((unsigned long *)arg);

	case SIRFSOC_MINIGPSRTC_ALM_READ:
		return sirf_minigpsrtc_alm_read((unsigned long *)arg);

	default:
		return -ENOTTY;
	}

	return 0;
}


static const struct file_operations minigpsrtc_fops = {
	.owner =    THIS_MODULE,
	.unlocked_ioctl = sirf_minigpsrtc_ioctl,
};

static int sirf_proc_minigpsrtc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%x\n", minigpsdata_mmc_base);
	return 0;
}

static int sirf_proc_minigpsrtc_open(struct inode *inode, struct file *file)
{
	return single_open(file, sirf_proc_minigpsrtc_show, PDE_DATA(inode));
}

static const struct file_operations sirf_proc_minigpsrtc_fops = {
	.owner =    THIS_MODULE,
	.open = sirf_proc_minigpsrtc_open,
	.read = seq_read,
};


static struct miscdevice sirf_minigpsrtc_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "minigpsrtc",
	.fops = &minigpsrtc_fops,
};

static const struct of_device_id sirf_minigpsrtc_of_match[] = {
	{ .compatible = "sirf,prima2-minigpsrtc"},
	{},
};
MODULE_DEVICE_TABLE(of, sirf_minigpsrtc_of_match);

static int sirf_minigpsrtc_probe(struct platform_device *pdev)
{
	int ret = 0;

	struct device_node *np = pdev->dev.of_node;

	ret = of_property_read_u32(np, "reg", &sirf_sysrtc_base);
	if (ret) {
		dev_err(&pdev->dev,
			"unable to find base address of rtc node in dtb\n");
		return ret;
	}

	ret = misc_register(&sirf_minigpsrtc_misc);
	if (unlikely(ret)) {
		dev_err(&pdev->dev,
			"sirf_minigpsrtc: failed to register misc device!\n");
		return ret;
	}
	sirf_rtc_alarm1_irq = platform_get_irq(pdev, 2);
	device_init_wakeup(&pdev->dev, 1);
	dev_info(&pdev->dev, "sirf_minigpsrtc registered.\n");

	proc_create_data("minigpsdata_mmc_base",
			S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH,
			proc_mkdir("minigps", NULL),
			&sirf_proc_minigpsrtc_fops,
			&minigpsdata_mmc_base);

	return ret;
}


static int sirf_minigpsrtc_remove(struct platform_device *pdev)
{
	misc_deregister(&sirf_minigpsrtc_misc);
	device_init_wakeup(&pdev->dev, 0);

	return 0;
}

static int sirf_minigpsrtc_suspend(struct device *dev)
{
	if (device_may_wakeup(dev))
		enable_irq_wake(sirf_rtc_alarm1_irq);

	return 0;
}

static int sirf_minigpsrtc_resume(struct device *dev)
{
	if (device_may_wakeup(dev))
		disable_irq_wake(sirf_rtc_alarm1_irq);

	return 0;
}

static int sirf_minigpsrtc_freeze(struct device *dev)
{
	if (device_may_wakeup(dev))
		enable_irq_wake(sirf_rtc_alarm1_irq);

	return 0;
}

static int sirf_minigpsrtc_thaw(struct device *dev)
{
	return 0;
}

static int sirf_minigpsrtc_restore(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops sirf_minigpsrtc_pm_ops = {
	.suspend = sirf_minigpsrtc_suspend,
	.resume = sirf_minigpsrtc_resume,
	.freeze = sirf_minigpsrtc_freeze,
	.thaw = sirf_minigpsrtc_thaw,
	.restore = sirf_minigpsrtc_restore,
};

static struct platform_driver sirf_minigpsrtc_driver = {
	.driver = {
		.name = "sirf-minigpsrtc",
		.owner = THIS_MODULE,
		.pm = &sirf_minigpsrtc_pm_ops,
		.of_match_table = of_match_ptr(sirf_minigpsrtc_of_match),
	},
	.probe = sirf_minigpsrtc_probe,
	.remove = sirf_minigpsrtc_remove,
};
module_platform_driver(sirf_minigpsrtc_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("RTC Alarm1 Driver Module For MINIGPS");
MODULE_ALIAS("RTC Alarm1 Module");
