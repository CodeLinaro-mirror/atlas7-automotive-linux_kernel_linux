/*
 * SiRFSoC Real Time Clock interface for Linux
 *
 * Copyright (c) 2013-2016, The Linux Foundation. All rights reserved.
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
#include <linux/err.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>


#define RTC_CN			0x00
#define RTC_ALARM0		0x04
#define RTC_ALARM1		0x18
#define RTC_STATUS		0x08
#define SIRFSOC_RTC_AL1E	(1<<6)
#define SIRFSOC_RTC_AL1		(1<<4)
#define SIRFSOC_RTC_HZE		(1<<3)
#define SIRFSOC_RTC_AL0E	(1<<2)
#define SIRFSOC_RTC_HZ		(1<<1)
#define SIRFSOC_RTC_AL0		(1<<0)
#define RTC_DIV			0x0c
#define RTC_DEEP_CTRL		0x14
#define RTC_CLOCK_SWITCH	0x1c
#define SIRFSOC_RTC_CLK		0x03	/* others are reserved */

/* This macro is also defined in arch/arm/plat-sirfsoc/cpu.c */
#define INTR_SYSRTC_CN		0x48

struct sirfsoc_rtc_drv {
	struct rtc_device	*rtc;
	u32			rtc_base;
	u32			rtc_size;
	u32			irq;
	unsigned		irq_wake;
	spinlock_t		lock;
	struct regmap *regmap;
#ifdef CONFIG_PM
	u32		saved_counter;
#endif
};
static u32 sirfsoc_rtc_readreg(struct sirfsoc_rtc_drv *rtcdrv, u32 offset)
{
	u32 val;

	regmap_read(rtcdrv->regmap, rtcdrv->rtc_base + offset, &val);
	return val;
}

static void sirfsoc_rtc_writereg(struct sirfsoc_rtc_drv *rtcdrv,
			u32 offset, u32 val)
{
	regmap_write(rtcdrv->regmap, rtcdrv->rtc_base + offset, val);
}

static int sirfsoc_rtc_read_alarm(struct device *dev,
		struct rtc_wkalrm *alrm)
{
	unsigned long rtc_alarm, rtc_count;
	struct sirfsoc_rtc_drv *rtcdrv;

	rtcdrv = dev_get_drvdata(dev);

	memset(alrm, 0, sizeof(struct rtc_wkalrm));
	spin_lock_irq(&rtcdrv->lock);

	sirfsoc_iobg_lock();

	rtc_count = sirfsoc_rtc_readreg(rtcdrv, RTC_CN);
	rtc_alarm = sirfsoc_rtc_readreg(rtcdrv, RTC_ALARM0);

	if (sirfsoc_rtc_readreg(rtcdrv, RTC_STATUS) & SIRFSOC_RTC_AL0E)
		alrm->enabled = 1;

	sirfsoc_iobg_unlock();
	spin_unlock_irq(&rtcdrv->lock);

	rtc_time_to_tm(rtc_alarm, &alrm->time);

	return 0;
}

static int sirfsoc_rtc_set_alarm(struct device *dev,
		struct rtc_wkalrm *alrm)
{
	unsigned long rtc_status_reg, rtc_alarm;
	struct sirfsoc_rtc_drv *rtcdrv;
	rtcdrv = dev_get_drvdata(dev);

	if (alrm->enabled) {
		rtc_tm_to_time(&(alrm->time), &rtc_alarm);

		spin_lock_irq(&rtcdrv->lock);
		sirfsoc_iobg_lock();

		rtc_status_reg = sirfsoc_rtc_readreg(rtcdrv, RTC_STATUS);
		if (rtc_status_reg & SIRFSOC_RTC_AL0E) {
			/*
			 * An ongoing alarm in progress - ingore it and not
			 * to return EBUSY
			 */
			dev_info(dev, "An old alarm was set, will be replaced by a new one\n");
		}

		sirfsoc_rtc_writereg(rtcdrv, RTC_ALARM0,
				rtc_alarm);
		rtc_status_reg &= ~0x07; /* mask out the lower status bits */
		/*
		 * This bit RTC_AL sets it as a wake-up source for Sleep Mode
		 * Writing 1 into this bit will clear it
		 */
		rtc_status_reg |= SIRFSOC_RTC_AL0;
		/* enable the RTC alarm interrupt */
		rtc_status_reg |= SIRFSOC_RTC_AL0E;
		sirfsoc_rtc_writereg(rtcdrv, RTC_STATUS, rtc_status_reg);
		sirfsoc_iobg_unlock();
		spin_unlock_irq(&rtcdrv->lock);
	} else {
		/*
		 * if this function was called with enabled=0
		 * then it could mean that the application is
		 * trying to cancel an ongoing alarm
		 */
		spin_lock_irq(&rtcdrv->lock);
		sirfsoc_iobg_lock();

		rtc_status_reg = sirfsoc_rtc_readreg(rtcdrv, RTC_STATUS);
		if (rtc_status_reg & SIRFSOC_RTC_AL0E) {
			/* clear the RTC status register's alarm bit */
			rtc_status_reg &= ~0x07;
			/* write 1 into SIRFSOC_RTC_AL0 to force a clear */
			rtc_status_reg |= (SIRFSOC_RTC_AL0);
			/* Clear the Alarm enable bit */
			rtc_status_reg &= ~(SIRFSOC_RTC_AL0E);

			sirfsoc_rtc_writereg(rtcdrv, RTC_STATUS,
				rtc_status_reg);
		}

		sirfsoc_iobg_unlock();
		spin_unlock_irq(&rtcdrv->lock);
	}

	return 0;
}

static int sirfsoc_rtc_read_time(struct device *dev,
		struct rtc_time *tm)
{
	unsigned long tmp_rtc = 0;
	struct sirfsoc_rtc_drv *rtcdrv;
	rtcdrv = dev_get_drvdata(dev);
	/*
	 * This patch is taken from WinCE - Need to validate this for
	 * correctness. To work around sirfsoc RTC counter double sync logic
	 * fail, read several times to make sure get stable value.
	 */
	sirfsoc_iobg_lock();
	do {
		tmp_rtc = sirfsoc_rtc_readreg(rtcdrv, RTC_CN);
		cpu_relax();
	} while (tmp_rtc != sirfsoc_rtc_readreg(rtcdrv, RTC_CN));
	sirfsoc_iobg_unlock();

	rtc_time_to_tm(tmp_rtc, tm);
	return 0;
}

static int sirfsoc_rtc_set_time(struct device *dev,
		struct rtc_time *tm)
{
	unsigned long rtc_time;
	struct sirfsoc_rtc_drv *rtcdrv;
	rtcdrv = dev_get_drvdata(dev);

	rtc_tm_to_time(tm, &rtc_time);
	sirfsoc_iobg_lock();
	sirfsoc_rtc_writereg(rtcdrv, RTC_CN, rtc_time);
	sirfsoc_iobg_unlock();

	return 0;
}

static int sirfsoc_rtc_ioctl(struct device *dev, unsigned int cmd,
		unsigned long arg)
{
	switch (cmd) {
	case RTC_PIE_ON:
	case RTC_PIE_OFF:
	case RTC_UIE_ON:
	case RTC_UIE_OFF:
	case RTC_AIE_ON:
	case RTC_AIE_OFF:
		return 0;

	default:
		return -ENOIOCTLCMD;
	}
}

static int sirfsoc_rtc_alarm_irq_enable(struct device *dev,
		unsigned int enabled)
{
	unsigned long rtc_status_reg = 0x0;
	struct sirfsoc_rtc_drv *rtcdrv;

	rtcdrv = dev_get_drvdata(dev);

	spin_lock_irq(&rtcdrv->lock);
	sirfsoc_iobg_lock();

	rtc_status_reg = sirfsoc_rtc_readreg(rtcdrv, RTC_STATUS);
	if (enabled)
		rtc_status_reg |= SIRFSOC_RTC_AL0E;
	else
		rtc_status_reg &= ~SIRFSOC_RTC_AL0E;

	sirfsoc_rtc_writereg(rtcdrv, RTC_STATUS, rtc_status_reg);
	sirfsoc_iobg_unlock();
	spin_unlock_irq(&rtcdrv->lock);

	return 0;

}

static const struct rtc_class_ops sirfsoc_rtc_ops = {
	.read_time = sirfsoc_rtc_read_time,
	.set_time = sirfsoc_rtc_set_time,
	.read_alarm = sirfsoc_rtc_read_alarm,
	.set_alarm = sirfsoc_rtc_set_alarm,
	.ioctl = sirfsoc_rtc_ioctl,
	.alarm_irq_enable = sirfsoc_rtc_alarm_irq_enable
};

static irqreturn_t sirfsoc_rtc_irq_handler(int irq, void *pdata)
{
	struct sirfsoc_rtc_drv *rtcdrv = pdata;
	unsigned long rtc_status_reg = 0x0;
	unsigned long events = 0x0;

	spin_lock(&rtcdrv->lock);
	sirfsoc_iobg_lock();

	rtc_status_reg = sirfsoc_rtc_readreg(rtcdrv, RTC_STATUS);
	/* this bit will be set ONLY if an alarm was active
	 * and it expired NOW
	 * So this is being used as an ASSERT
	 */
	if (rtc_status_reg & SIRFSOC_RTC_AL0) {
		/*
		 * clear the RTC status register's alarm bit
		 * mask out the lower status bits
		 */
		rtc_status_reg &= ~0x07;
		/* write 1 into SIRFSOC_RTC_AL0 to ACK the alarm interrupt */
		rtc_status_reg |= (SIRFSOC_RTC_AL0);
		/* Clear the Alarm enable bit */
		rtc_status_reg &= ~(SIRFSOC_RTC_AL0E);
	}

	sirfsoc_rtc_writereg(rtcdrv, RTC_STATUS, rtc_status_reg);

	sirfsoc_iobg_unlock();
	spin_unlock(&rtcdrv->lock);

	/* this should wake up any apps polling/waiting on the read
	 * after setting the alarm
	 */
	events |= RTC_IRQF | RTC_AF;
	rtc_update_irq(rtcdrv->rtc, 1, events);

	return IRQ_HANDLED;
}

static const struct of_device_id sirfsoc_rtc_of_match[] = {
	{ .compatible = "sirf,prima2-sysrtc"},
	{},
};
#ifdef CONFIG_A7DA_PM_SYSRTC_DEBUG
static ssize_t sysrtc_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct sirfsoc_rtc_drv *rtcdrv;
	u32 offset, val;

	rtcdrv = (struct sirfsoc_rtc_drv *)dev_get_drvdata(dev);

	if (sscanf(buf, "%x %x\n", &offset, &val) != 2)
		return -EINVAL;
	if (offset >= rtcdrv->rtc_size)
		return -EINVAL;
	sirfsoc_iobg_lock();
	sirfsoc_rtc_writereg(rtcdrv, offset, val);
	sirfsoc_iobg_unlock();

	return len;
}
static ssize_t sysrtc_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct sirfsoc_rtc_drv *rtcdrv;
	int val, i, pos = 0;

	rtcdrv = (struct sirfsoc_rtc_drv *)dev_get_drvdata(dev);

	for (i = 0; i < 0x20 && strlen(buf) < PAGE_SIZE; i = i + 4) {
		sirfsoc_iobg_lock();
		val = sirfsoc_rtc_readreg(rtcdrv, i);
		sirfsoc_iobg_unlock();
		pos += scnprintf(buf + pos,
			PAGE_SIZE - pos,
			"0x%x:0x%x\n", i, val);
	}

	return pos;
}
static DEVICE_ATTR_RW(sysrtc);
#endif
const struct regmap_config sysrtc_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.fast_io = true,
};

MODULE_DEVICE_TABLE(of, sirfsoc_rtc_of_match);

static int sirfsoc_rtc_probe(struct platform_device *pdev)
{
	int err;
	unsigned long rtc_div;
	struct sirfsoc_rtc_drv *rtcdrv;
	struct device_node *np = pdev->dev.of_node;
	u32 subreg_info[2];

	rtcdrv = devm_kzalloc(&pdev->dev,
		sizeof(struct sirfsoc_rtc_drv), GFP_KERNEL);
	if (rtcdrv == NULL)
		return -ENOMEM;

	spin_lock_init(&rtcdrv->lock);
	err = of_property_read_u32_array(np, "sub-reg", &subreg_info[0], 2);
	if (err) {
		dev_err(&pdev->dev, "unable to find sub-reg of rtc node in dtb\n");
		return err;
	}

	rtcdrv->rtc_base = subreg_info[0];
	rtcdrv->rtc_size = subreg_info[1];

	platform_set_drvdata(pdev, rtcdrv);
	/* Register rtc alarm as a wakeup source */
	device_init_wakeup(&pdev->dev, 1);
	rtcdrv->regmap = devm_regmap_init_iobg(&pdev->dev,
			&sysrtc_regmap_config);
	if (IS_ERR(rtcdrv->regmap)) {
		err = PTR_ERR(rtcdrv->regmap);
		dev_err(&pdev->dev, "Failed to allocate register map: %d\n",
			err);
		return err;
	}

#ifdef CONFIG_A7DA_PM_SYSRTC_DEBUG
		err = device_create_file(&pdev->dev, &dev_attr_sysrtc);
		if (err)
			dev_err(&pdev->dev,
				"failed to create spram firewall attribute, %d\n",
				err);
#endif

	/*
	 * Set SYS_RTC counter in 1 HZ Units
	 * We are using 32K RTC crystal (32768 /2) -1
	 */
	rtc_div = (32768 / 2) - 1;

	sirfsoc_iobg_lock();

	sirfsoc_rtc_writereg(rtcdrv, RTC_DIV, rtc_div);

	/* 0x3 -> RTC_CLK */
	sirfsoc_rtc_writereg(rtcdrv, RTC_CLOCK_SWITCH, SIRFSOC_RTC_CLK);

	/* reset SYS RTC ALARM0 */
	sirfsoc_rtc_writereg(rtcdrv, RTC_ALARM0, 0x0);

	/* reset SYS RTC ALARM1 */
	sirfsoc_rtc_writereg(rtcdrv, RTC_ALARM1, 0x0);

	sirfsoc_iobg_unlock();

	rtcdrv->rtc = devm_rtc_device_register(&pdev->dev, pdev->name,
			&sirfsoc_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtcdrv->rtc)) {
		err = PTR_ERR(rtcdrv->rtc);
		dev_err(&pdev->dev, "can't register RTC device\n");
		return err;
	}

	rtcdrv->irq = platform_get_irq(pdev, 0);
	err = devm_request_irq(
			&pdev->dev,
			rtcdrv->irq,
			sirfsoc_rtc_irq_handler,
			IRQF_SHARED,
			pdev->name,
			rtcdrv);
	if (err) {
		dev_err(&pdev->dev, "Unable to register for the SiRF SOC RTC IRQ\n");
		return err;
	}

	return 0;
}

static int sirfsoc_rtc_remove(struct platform_device *pdev)
{
	device_init_wakeup(&pdev->dev, 0);
	return 0;
}


#ifdef CONFIG_PM_SLEEP
static int sirfsoc_rtc_suspend(struct device *dev)
{
	struct sirfsoc_rtc_drv *rtcdrv = dev_get_drvdata(dev);

	sirfsoc_iobg_lock();
	rtcdrv->saved_counter =
		sirfsoc_rtc_readreg(rtcdrv, RTC_CN);
	sirfsoc_iobg_unlock();

	if (device_may_wakeup(dev) && !enable_irq_wake(rtcdrv->irq))
		rtcdrv->irq_wake = 1;

	return 0;
}

static int sirfsoc_rtc_resume(struct device *dev)
{
	struct sirfsoc_rtc_drv *rtcdrv = dev_get_drvdata(dev);

	/*
	 * if resume from snapshot and the rtc power is lost,
	 * restroe the rtc settings
	 */
	sirfsoc_iobg_lock();
	if (SIRFSOC_RTC_CLK != sirfsoc_rtc_readreg(rtcdrv, RTC_CLOCK_SWITCH)) {
		u32 rtc_div;
		/* 0x3 -> RTC_CLK */
		sirfsoc_rtc_writereg(rtcdrv, RTC_CLOCK_SWITCH, SIRFSOC_RTC_CLK);
		/*
		 * Set SYS_RTC counter in 1 HZ Units
		 * We are using 32K RTC crystal (32768 /2) -1
		 */
		rtc_div = (32768 / 2) - 1;

		sirfsoc_rtc_writereg(rtcdrv, RTC_DIV, rtc_div);

		/* reset SYS RTC ALARM0 */
		sirfsoc_rtc_writereg(rtcdrv, RTC_ALARM0, 0x0);

		/* reset SYS RTC ALARM1 */
		sirfsoc_rtc_writereg(rtcdrv, RTC_ALARM1, 0x0);
	}
	sirfsoc_iobg_unlock();

	/*
	 *PWRC Value Be Changed When Suspend, Restore Overflow
	 * In Memory To Register
	 */
	if (device_may_wakeup(dev) && rtcdrv->irq_wake) {
		disable_irq_wake(rtcdrv->irq);
		rtcdrv->irq_wake = 0;
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(sirfsoc_rtc_pm_ops,
		sirfsoc_rtc_suspend, sirfsoc_rtc_resume);

static struct platform_driver sirfsoc_rtc_driver = {
	.driver = {
		.name = "sirfsoc-rtc",
		.owner = THIS_MODULE,
		.pm = &sirfsoc_rtc_pm_ops,
		.of_match_table = sirfsoc_rtc_of_match,
	},
	.probe = sirfsoc_rtc_probe,
	.remove = sirfsoc_rtc_remove,
};
module_platform_driver(sirfsoc_rtc_driver);

MODULE_DESCRIPTION("SiRF SoC rtc driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:sirfsoc-rtc");
