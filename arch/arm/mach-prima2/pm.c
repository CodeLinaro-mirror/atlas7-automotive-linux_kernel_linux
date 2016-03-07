/*
 * power management entry for CSR SiRFprimaII
 *
 * Copyright (c) 2011-2016, The Linux Foundation. All rights reserved.
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
#include <linux/regulator/consumer.h>
#include <linux/proc_fs.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/pm_opp.h>
#include <asm/suspend.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/uaccess.h>
#include <asm/system_misc.h>
#include "pm.h"

struct sirfsoc_sysctl_info {
	struct device *dev;
	struct regmap *regmap;
	struct sirfsoc_pwrc_register *pwrc_reg;
	u32 ver;
	u32 base;
	u32 svm;
	struct regulator *core_reg;
	void __iomem *retain_base;
	void __iomem *clkc_base;
	void __iomem *timer_base;
};

struct private_data {
	struct device *cpu_dev;
	struct regulator *cpu_reg;
	struct thermal_cooling_device *cdev;
	unsigned int voltage_tolerance; /* in percentage */
};

enum SIRFSOC_SYSCTL_IDX {
	IPC_IDX,
	RETAIN_IDX,
	MEMC_ATLAS7_IDX,
	TICK_IDX,
	CLK_IDX,
	MEMC_PRIMA2_IDX,
	MAX_IDX
};

enum SIRFSOC_PM_STATE {
	SIRFSOC_PM_DEFAULT,
	SIRFSOC_PM_SLEEP,
	SIRFSOC_PM_RESET,
	SIRFSOC_PM_SHUTDOWN,
};

struct sirfsoc_pm_init_t {
	char *name;
	u32 idx;
	void __iomem *base;
	int (*init_pm)(struct sirfsoc_pm_init_t *);
};

static struct sirfsoc_sysctl_info *sinfo;

void __iomem *sirfsoc_memc_base;
void __iomem *sirfsoc_pm_ipc_base;
static int (*sirfsoc_finish_suspend)(unsigned long);

static int atlas7_pm_retain_init(struct sirfsoc_pm_init_t *);
static int atlas7_pm_clk_init(struct sirfsoc_pm_init_t *);
static int atlas7_pm_ipc_init(struct sirfsoc_pm_init_t *);
static int atlas7_pm_memc_init(struct sirfsoc_pm_init_t *);
static int prima2_pm_memc_init(struct sirfsoc_pm_init_t *);
static int atlas7_pm_tick_init(struct sirfsoc_pm_init_t *);

static struct sirfsoc_pm_init_t sirfsoc_pm_init_table[] = {
	{
		.name = "IPC_IDX",
		.idx = IPC_IDX,
		.init_pm = atlas7_pm_ipc_init,
	}, {
		.name = "RETAIN_IDX",
		.idx = RETAIN_IDX,
		.init_pm = atlas7_pm_retain_init,
	}, {
		.name = "MEMC_ATLAS7_IDX",
		.idx = MEMC_ATLAS7_IDX,
		.init_pm = atlas7_pm_memc_init,
	}, {
		.name = "TICK_IDX",
		.idx = TICK_IDX,
		.init_pm = atlas7_pm_tick_init,
	}, {
		.name = "CLK_IDX",
		.idx = CLK_IDX,
		.init_pm = atlas7_pm_clk_init,
	}, {
		.name = "MEMC_PRIMA2_IDX",
		.idx = MEMC_PRIMA2_IDX,
		.init_pm = prima2_pm_memc_init,
	},
};

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

static void sirfsoc_pm_notity_m3(u32 state)
{
#define IPC_M3_OFS 0x10c
#define IPC_M3_TRIG 1

	writel(state & 0xf, sinfo->retain_base +
		SIRFSOC_PWRC_SCRATCH_PAD8);

	writel(IPC_M3_TRIG, sirfsoc_pm_ipc_base + IPC_M3_OFS);
	while (1)
		;
}

void sirfsoc_pm_power_off(void)
{
	struct sirfsoc_pwrc_register *pwrc_reg = sinfo->pwrc_reg;
	u32 sleep_mode;

	if (sinfo->ver == PWRC_ATLAS7_VER)
		sirfsoc_pm_notity_m3(SIRFSOC_PM_SHUTDOWN);


	else if (sinfo->ver == PWRC_PRIMA2_VER) {
		sirfsoc_set_sleep_mode(SIRFSOC_HIBERNATION_MODE);
		regmap_read(sinfo->regmap, sinfo->base +
			pwrc_reg->pwrc_pdn_ctrl_set, &sleep_mode);

		regmap_write(sinfo->regmap,
				sinfo->base + pwrc_reg->pwrc_pdn_ctrl_set,
				sleep_mode |  1 << SIRFSOC_START_PSAVING_BIT);
	}
}

int sirfsoc_pre_suspend_power_off(void)
{
	struct sirfsoc_pwrc_register *pwrc_reg = sinfo->pwrc_reg;
	u32 wakeup_entry;

	wakeup_entry = virt_to_phys(cpu_resume);
	if (sinfo->ver == PWRC_ATLAS7_VER) {
		writel_relaxed(wakeup_entry,
			sinfo->retain_base + SIRFSOC_PWRC_SCRATCH_PAD1);

		writel(SIRFSOC_PM_SLEEP, sinfo->retain_base +
			SIRFSOC_PWRC_SCRATCH_PAD8);
	} else {
		regmap_write(sinfo->regmap,
				sinfo->base + pwrc_reg->pwrc_scratch_pad1,
				wakeup_entry);

		sirfsoc_set_sleep_mode(SIRFSOC_DEEP_SLEEP_MODE);
	}

	sirfsoc_set_wakeup_source();
	return 0;
}

ssize_t sirfsoc_boot_stat_proc_read(struct file *file,
		char __user *buf, size_t size, loff_t *ppos)
{

	int i;
	u32 boot_stat;

	struct sirfsoc_pwrc_register *pwrc_reg = sinfo->pwrc_reg;

	if (sinfo->ver == PWRC_ATLAS7_VER)
		boot_stat = readl_relaxed(sinfo->retain_base
				+ SIRFSOC_PWRC_SCRATCH_PAD11);
	else
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


	if (sinfo->ver == PWRC_ATLAS7_VER)
		writel_relaxed(boot_stat,
			sinfo->retain_base + SIRFSOC_PWRC_SCRATCH_PAD11);
	else
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
		cpu_suspend(0, sirfsoc_finish_suspend);
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

static const struct of_device_id sirfsoc_pm_ids[] = {
	{ .compatible = "sirf,atlas7-pmipc",
		.data = &sirfsoc_pm_init_table[0]},
	{ .compatible = "sirf,atlas7-retain",
		.data = &sirfsoc_pm_init_table[1]},
	{ .compatible = "sirf,atlas7-memc",
		.data = &sirfsoc_pm_init_table[2]},
	{ .compatible = "sirf,atlas7-tick",
		.data = &sirfsoc_pm_init_table[3]},
	{ .compatible = "sirf,atlas7-car",
		.data = &sirfsoc_pm_init_table[4]},
	{ .compatible = "sirf,prima2-memc",
		.data = &sirfsoc_pm_init_table[5]},
};

void sirfsoc_atlas7_restart(enum reboot_mode mode, const char *cmd)
{
	/* support standand android recovery mode */
	if ((cmd != NULL) && !strncmp(cmd, "recovery", 8))
		writel(readl(sinfo->retain_base + SIRFSOC_PWRC_SCRATCH_PAD11)
			| RECOVERY_MODE,
			sinfo->retain_base + SIRFSOC_PWRC_SCRATCH_PAD11);

	sirfsoc_pm_notity_m3(SIRFSOC_PM_RESET);
}

static int atlas7_pm_retain_init(struct sirfsoc_pm_init_t *pinit)
{

	sinfo->retain_base =  pinit->base;
	return 0;
}

static int atlas7_pm_clk_init(struct sirfsoc_pm_init_t *pinit)
{

	sinfo->clkc_base =  pinit->base;
	return 0;
}

static int atlas7_pm_ipc_init(struct sirfsoc_pm_init_t *pinit)
{

	sirfsoc_pm_ipc_base = pinit->base;

	return 0;
}

static int atlas7_pm_memc_init(struct sirfsoc_pm_init_t *pinit)
{

	sirfsoc_finish_suspend = sirfsoc_atlas7_finish_suspend;
	sirfsoc_memc_base = pinit->base;

	return 0;
}

static int prima2_pm_memc_init(struct sirfsoc_pm_init_t *pinit)
{
	sirfsoc_finish_suspend = sirfsoc_prima2_finish_suspend;
	sirfsoc_memc_base = pinit->base;

	return 0;
}

static int atlas7_pm_tick_init(struct sirfsoc_pm_init_t *pinit)
{
	sinfo->timer_base =  pinit->base;
	return 0;
}

static int atlas7_svm_core_idx(u32 svm_config)
{
	u32 svm_index;

#define	CORE_REDUNDANCY 0
#define	CORE_REDUNDANCY_1 8
#define	VCORE_LEVEL 4
#define	VCORE_LEVEL_1 12

	/*
	* 0: Use primary core SVM fields,
	* 1: Use secondary core SVM fields
	*/
	if (!(svm_config & BIT(CORE_REDUNDANCY)))
		svm_index = svm_config>>VCORE_LEVEL & 0xf;
	else if (svm_config & BIT(CORE_REDUNDANCY_1))
		svm_index = 0;
	else
		/*
		* Only valid if Core_redundancy = 1
		* 0: Use secondary core SVM fields
		* 1: Do not use secondary core SVM fields (disable
		* core SVM)
		*/
		svm_index = svm_config>>VCORE_LEVEL_1 & 0xf;
	return svm_index;
}


static int atlas7_svm_cpu_idx(u32 svm_config)
{
	u32 svm_index;

#define	CPU_REDUNDANCY 16
#define	CPU_REDUNDANCY_1 24
#define	VCPU_LEVEL 20
#define	VCPU_LEVEL_1 28

	/*
	*0: Use primary CPU SVM fields
	*1: Use secondary CPU SVM fields
	*/
	if (!(svm_config & BIT(CPU_REDUNDANCY)))
		svm_index = svm_config>>VCPU_LEVEL & 0xf;
	else if (svm_config & BIT(CPU_REDUNDANCY_1))
		svm_index = 0;
	else
		/*
		*Only valid if CPU_redundancy = 1
		*0: Use secondary CPU SVM fields
		*1: Do not use secondary CPU SVM fields (disable
		*CPU SVM)
		*/
		svm_index = svm_config>>VCPU_LEVEL_1 & 0xf;
	return svm_index;
}


#define SVM_CPU 0
#define SVM_CORE 1
static void atlas7_pm_svm(int dev_type)
{
	struct dev_pm_opp *opp;
	struct cpufreq_policy *policy = cpufreq_cpu_get(0);
	struct cpufreq_frequency_table *freq_table = policy->freq_table;
	struct private_data *priv = policy->driver_data;
	struct device *cpu_dev;
	struct regulator *reg;
	unsigned long volt = 0;
	long freq_Hz;
	int ret, index;

	if (dev_type == SVM_CORE) {
		reg = sinfo->core_reg;
		index = atlas7_svm_core_idx(sinfo->svm);
	} else if (dev_type == SVM_CPU) {
		reg = priv->cpu_reg;
		index = atlas7_svm_cpu_idx(sinfo->svm);
	} else
		goto out;

	freq_Hz = freq_table[15-index].frequency * 1000;
	if (!index)
		goto out;

	if (!IS_ERR(reg)) {
		/* vdd_core share same voltage table as vdd_cpu*/
		cpu_dev = get_cpu_device(policy->cpu);
		opp = dev_pm_opp_find_freq_ceil(cpu_dev, &freq_Hz);
		volt = dev_pm_opp_get_voltage(opp);
		regulator_set_voltage(reg, volt, volt);
		ret = regulator_enable(reg);
		if (ret)
			goto out;
	}

out:
	return;
}

static int sirfsoc_sysctl_probe(struct platform_device *pdev)
{

	struct sirfsoc_pwrc_info *pwrcinfo = dev_get_drvdata(pdev->dev.parent);
	struct sirfsoc_sysctl_info *info;
	struct device_node *np;
	const struct of_device_id *match;
	void __iomem *base;
	struct sirfsoc_pm_init_t *pinit;
	struct regulator *core_reg;
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

	/* handle pm related bases & callbacks*/
	for_each_matching_node_and_match(np, sirfsoc_pm_ids, &match) {
		if (!of_device_is_available(np))
			continue;

		pinit = (struct sirfsoc_pm_init_t *)match->data;
		base = of_iomap(np, 0);
		if (!base)
			panic("unable to find compatible sirf node in dtb\n");

		pinit->base = base;

		if (pinit->init_pm)
			pinit->init_pm(pinit);
	}

	core_reg = regulator_get_optional(&pdev->dev, "core");
	if (IS_ERR(core_reg)) {
		dev_err(&pdev->dev, "no regulator for core: %ld\n",
			PTR_ERR(core_reg));
		goto out;
	}

	info->core_reg = core_reg;
	atlas7_otp_get_svm(&info->svm);
	atlas7_pm_svm(SVM_CORE);
	atlas7_pm_svm(SVM_CPU);

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
	struct platform_device_info devinfo = { .name = "cpufreq-dt", };
	platform_device_register_full(&devinfo);

	platform_driver_register(&sirfsoc_sysctl_driver);
	pm_power_off = sirfsoc_pm_power_off;
	suspend_set_ops(&sirfsoc_pm_ops);
	return 0;
}
