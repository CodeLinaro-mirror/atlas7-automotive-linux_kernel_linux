/*
 * power management entry for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#define pr_fmt(fmt)        "(sirfsoc_pm): " fmt

#include <linux/kernel.h>
#include <linux/suspend.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>
#include <linux/mfd/sirfsoc_pwrc.h>
#include <linux/proc_fs.h>
#include <asm/suspend.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/uaccess.h>

#include "pm.h"

struct sirfsoc_sysctl_info {
	struct device *dev;
	struct regmap *regmap;
	struct sirfsoc_pwrc_register *pwrc_reg;
	u32 ver;
	u32 base;
};

static const struct of_device_id retainreg_ids[] = {
	{ .compatible = "sirf,atlas7-retain"},
};

static const struct of_device_id memc_ids[] = {
	{
		.compatible = "sirf,prima2-memc",
		.data = sirfsoc_prima2_finish_suspend,
	}, {
		.compatible = "sirf,atlas7-memc",
		.data = sirfsoc_atlas7_finish_suspend,
	}, {
	}
};

static const struct of_device_id pmipc_ids[] = {
	{ .compatible = "sirf,atlas7-pmipc"},
};

static struct sirfsoc_sysctl_info *sinfo;

void __iomem *sirfsoc_memc_base;
void __iomem *sirfsoc_pm_ipc_base;
static int (*sirfsoc_finish_suspend)(unsigned long);

static void sirfsoc_set_wakeup_source(void)
{
	struct sirfsoc_pwrc_register *pwrc_reg = sinfo->pwrc_reg;
	u32 pwr_trigger_en_reg;

	regmap_read(sinfo->regmap, sinfo->base +
		pwrc_reg->pwrc_trigger_en_set, &pwr_trigger_en_reg);
#define X_ON_KEY_B (1 << 0)
#define RTC_ALARM0_B (1 << 2)
#define RTC_ALARM1_B (1 << 3)

	regmap_write(sinfo->regmap,
			sinfo->base + pwrc_reg->pwrc_trigger_en_set,
			pwr_trigger_en_reg |
			X_ON_KEY_B |
			RTC_ALARM0_B |
			RTC_ALARM1_B);
}

static void sirfsoc_set_sleep_mode(u32 mode)
{
	struct sirfsoc_pwrc_register *pwrc_reg = sinfo->pwrc_reg;
	u32 sleep_mode;

	regmap_read(sinfo->regmap, sinfo->base +
			pwrc_reg->pwrc_pdn_ctrl_set, &sleep_mode);

	sleep_mode &= ~(SIRFSOC_SLEEP_MODE_MASK << 1);
	sleep_mode |= mode << 1;

	regmap_write(sinfo->regmap,
			sinfo->base + pwrc_reg->pwrc_pdn_ctrl_set,
			sleep_mode);

	sirfsoc_set_wakeup_source();
}

void __iomem *sirfsoc_pm_get_base(const struct of_device_id *ids)
{
	struct device_node *np;
	void __iomem *ret;

	np = of_find_matching_node(NULL, ids);
	if (!np)
		panic("unable to find compatible sirf node in dtb\n");

	ret = of_iomap(np, 0);
	if (!ret)
		panic("unable to map base\n");

	return ret;
}

void sirfsoc_pm_enter_power_saving(void)
{
	cpu_suspend(0, sirfsoc_finish_suspend);
}

void sirfsoc_pm_get_finish_suspend(void)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, memc_ids);
	sirfsoc_finish_suspend = of_match_node(memc_ids, np)->data;
}

void sirfsoc_pm_power_off(void)
{
	struct sirfsoc_pwrc_register *pwrc_reg = sinfo->pwrc_reg;
	u32 sleep_mode;

	sirfsoc_set_sleep_mode(SIRFSOC_HIBERNATION_MODE);

	if (sinfo->ver == PWRC_ATLAS7_VER)
		/*for atlas7, M3 responsible for power off,
		**sirfsoc_finish_suspend responsible for trigger IPC
		*/
		sirfsoc_pm_ipc_base = sirfsoc_pm_get_base(pmipc_ids);
	else {
		regmap_read(sinfo->regmap, sinfo->base +
			pwrc_reg->pwrc_pdn_ctrl_set, &sleep_mode);

		regmap_write(sinfo->regmap,
				sinfo->base + pwrc_reg->pwrc_pdn_ctrl_set,
				sleep_mode |  1 << SIRFSOC_START_PSAVING_BIT);
	}

	sirfsoc_pm_get_finish_suspend();
}

int sirfsoc_pre_suspend_power_off(void)
{
	struct sirfsoc_pwrc_register *pwrc_reg = sinfo->pwrc_reg;
	void __iomem *sirfsoc_retain_base;
	u32 wakeup_entry;

	wakeup_entry = virt_to_phys(cpu_resume);
	if (sinfo->ver == PWRC_ATLAS7_VER) {
		sirfsoc_retain_base = sirfsoc_pm_get_base(retainreg_ids);
		writel_relaxed(wakeup_entry,
			sirfsoc_retain_base + SIRFSOC_PWRC_SCRATCH_PAD1);
		writel_relaxed(1,
			sirfsoc_retain_base + SIRFSOC_PWRC_SCRATCH_PAD8);


		/*for atlas7, M3 responsible for enter deep sleep,
		**sirfsoc_finish_suspend responsible for trigger IPC
		*/

		sirfsoc_pm_ipc_base = sirfsoc_pm_get_base(pmipc_ids);
	} else {
		regmap_write(sinfo->regmap,
				sinfo->base + pwrc_reg->pwrc_scratch_pad1,
				wakeup_entry);
		sirfsoc_memc_base = sirfsoc_pm_get_base(memc_ids);
	}


	sirfsoc_set_wakeup_source();
	sirfsoc_set_sleep_mode(SIRFSOC_DEEP_SLEEP_MODE);
	sirfsoc_pm_get_finish_suspend();

	return 0;
}

ssize_t sirfsoc_boot_stat_proc_read(struct file *file,
		char __user *buf, size_t size, loff_t *ppos)
{

	int i;
	u32 boot_stat;
	void __iomem *sirfsoc_retain_base;
	struct sirfsoc_pwrc_register *pwrc_reg = sinfo->pwrc_reg;

	if (sinfo->ver == PWRC_ATLAS7_VER) {
		sirfsoc_retain_base = sirfsoc_pm_get_base(retainreg_ids);
		boot_stat = readl_relaxed(sirfsoc_retain_base
				+ SIRFSOC_BOOT_STATUS);
	} else
		boot_stat = sirfsoc_rtc_iobrg_readl(sinfo->base +
			pwrc_reg->pwrc_scratch_pad3);

	if (size < SIRFSOC_BOOT_STATUS_BITS) {
		pr_err("Failed to read boot status, mask bits is %d, but read size is %d\n",
			SIRFSOC_BOOT_STATUS_BITS, size);
		return -EINVAL;
	}

	for (i = 0; i < SIRFSOC_BOOT_STATUS_BITS; i++)
		put_user("01"[(boot_stat >> i) & 0x1], buf + i);
	return size;
}

ssize_t sirfsoc_boot_stat_proc_write(struct file *file,
		const char __user *buf, size_t size, loff_t *ppos)
{
	u32 boot_stat = 0;
	char data[SIRFSOC_BOOT_STATUS_BITS];
	void __iomem *sirfsoc_retain_base;
	struct sirfsoc_pwrc_register *pwrc_reg = sinfo->pwrc_reg;
	int i;

	if (size < SIRFSOC_BOOT_STATUS_BITS) {
		pr_err("Failed to write boot status, mask bits is %d, but write size is %d\n",
			SIRFSOC_BOOT_STATUS_BITS, size);
		return -EINVAL;
	}

	if (copy_from_user(data, buf, SIRFSOC_BOOT_STATUS_BITS))
		return -EINVAL;

	for (i = 0; i < SIRFSOC_BOOT_STATUS_BITS; i++)
		boot_stat |= (((data[i] - '0') & 0x1) << i);


	if (sinfo->ver == PWRC_ATLAS7_VER) {
		sirfsoc_retain_base = sirfsoc_pm_get_base(retainreg_ids);
		writel_relaxed(boot_stat,
			sirfsoc_retain_base + SIRFSOC_BOOT_STATUS);
	} else
		regmap_write(sinfo->regmap,
			sinfo->base + pwrc_reg->pwrc_scratch_pad3,
			boot_stat);
	return size;
}


static const struct file_operations sirfsoc_boot_stat_proc_fops = {
	.read		= sirfsoc_boot_stat_proc_read,
	.write		= sirfsoc_boot_stat_proc_write,
};

#ifdef CONFIG_A7DA_PM_PWRC_DEBUG
static ssize_t pwrc_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct sirfsoc_sysctl_info *info;
	u32 offset, val;

	info = (struct sirfsoc_sysctl_info *)dev_get_drvdata(dev);

	if (sscanf(buf, "%x %x\n", &offset, &val) != 2)
		return -EINVAL;
	regmap_write(info->regmap, info->base + offset, val);
	return len;
}
static ssize_t pwrc_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct sirfsoc_sysctl_info *info;
	int val, i, pos = 0;

	info = (struct sirfsoc_sysctl_info *)dev_get_drvdata(dev);
	for (i = 0; i < 0x9c && strlen(buf) < PAGE_SIZE; i = i + 4) {
		regmap_read(info->regmap, info->base + i, &val);
		pos += scnprintf(buf + pos,
			PAGE_SIZE - pos,
			"0x%x:0x%x\n", i, val);
	}

	return pos;
}

static DEVICE_ATTR_RW(pwrc);

#endif


/*
 * suspend asm codes will access these to make DRAM become self-refresh and
 * system sleep
 */
static int sirfsoc_pm_enter(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_MEM:
		sirfsoc_pre_suspend_power_off();
		outer_disable();
		/* go zzz */
		sirfsoc_pm_enter_power_saving();
		outer_resume();
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct platform_suspend_ops sirfsoc_pm_ops = {
	.enter = sirfsoc_pm_enter,
	.valid = suspend_valid_only_mem,
};

static int sirfsoc_sysctl_probe(struct platform_device *pdev)
{

	struct sirfsoc_pwrc_info *pwrcinfo = dev_get_drvdata(pdev->dev.parent);
	struct sirfsoc_sysctl_info *info;
	int ret;

	info = kzalloc(sizeof(struct sirfsoc_sysctl_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	info->pwrc_reg = pwrcinfo->pwrc_reg;
	info->regmap  = pwrcinfo->regmap;
	info->base  = pwrcinfo->base;
	info->ver  = pwrcinfo->ver;

	if (!info->regmap) {
		dev_err(&pdev->dev, "no regmap!\n");
		ret = -EINVAL;
		goto out;
	}

	platform_set_drvdata(pdev, info);

	proc_create_data("boot_status",
			S_IRUSR | S_IWUSR ,
			NULL,
			&sirfsoc_boot_stat_proc_fops,
			NULL);

#ifdef CONFIG_A7DA_PM_PWRC_DEBUG
	ret = device_create_file(&pdev->dev, &dev_attr_pwrc);
	if (ret)
		goto out;


#endif
	sinfo = info;
	return 0;
out:
	kfree(info);
	return ret;
}

static const struct of_device_id sysctl_ids[] = {
	{ .compatible = "sirf,sirf-sysctl"},
	{}
};

static struct platform_driver sirfsoc_sysctl_driver = {
	.driver = {
		   .name = "sirf-sysctl",
		   .owner = THIS_MODULE,
		   .of_match_table = sysctl_ids,
		   },
	.probe = sirfsoc_sysctl_probe,
};

int __init sirfsoc_pm_init(void)
{
	platform_driver_register(&sirfsoc_sysctl_driver);
	pm_power_off = sirfsoc_pm_power_off;
	suspend_set_ops(&sirfsoc_pm_ops);

	return 0;
}
