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
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>
#include <linux/proc_fs.h>
#include <asm/suspend.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/uaccess.h>

#include "pm.h"
struct sirfsoc_pwrc_register {
	/* hardware pwrc specific */
	u32 pwrc_pdn_ctrl_set;
	u32 pwrc_pdn_ctrl_clr;
	u32 pwrc_pon_status;
	u32 pwrc_trigger_en_set;
	u32 pwrc_trigger_en_clr;
	u32 pwrc_int_mask_set;
	u32 pwrc_int_mask_clr;
	u32 pwrc_int_status;
	u32 pwrc_pin_status;
	u32 pwrc_rtc_pll_ctrl;
	u32 pwrc_gpio3_debug;
	u32 pwrc_rtc_noc_pwrctl_set;
	u32 pwrc_rtc_noc_pwrctl_clr;
	u32 pwrc_rtc_can_ctrl;
	u32 pwrc_rtc_can_status;
	u32 pwrc_fsm_m3_ctrl;
	u32 pwrc_fsm_state;
	u32 pwrc_rtcldo_reg;
	u32 pwrc_gnss_ctrl;
	u32 pwrc_gnss_status;
	u32 pwrc_xtal_reg;
	u32 pwrc_xtal_ldo_mux_sel;
	u32 pwrc_rtc_sw_rstc_set;
	u32 pwrc_rtc_sw_rstc_clr;
	u32 pwrc_power_sw_ctrl_set;
	u32 pwrc_power_sw_ctrl_clr;
	u32 pwrc_rtc_dcog;
	u32 pwrc_m3_memories;
	u32 pwrc_can0_memory;
	u32 pwrc_rtc_gnss_memory;
	u32 pwrc_m3_clk_en;
	u32 pwrc_can0_clk_en;
	u32 pwrc_spi0_clk_en;
	u32 pwrc_rtc_sec_clk_en;
	u32 pwrc_rtc_noc_clk_en;

	/*only for prima2*/
	u32 pwrc_scratch_pad1;
	u32 pwrc_scratch_pad2;
	u32 pwrc_scratch_pad3;
	u32 pwrc_scratch_pad4;
	u32 pwrc_scratch_pad5;
	u32 pwrc_scratch_pad6;
	u32 pwrc_scratch_pad7;
	u32 pwrc_scratch_pad8;
	u32 pwrc_scratch_pad9;
	u32 pwrc_scratch_pad10;
	u32 pwrc_scratch_pad11;
	u32 pwrc_scratch_pad12;
	u32 pwrc_gpio3_clk;
	u32 pwrc_gpio_ds;

};

struct sirfsoc_pwrc_register sirfsoc_a7da_pwrc = {
	.pwrc_pdn_ctrl_set = 0x0,
	.pwrc_pdn_ctrl_clr = 0x4,
	.pwrc_pon_status = 0x8,
	.pwrc_trigger_en_set = 0xc,
	.pwrc_trigger_en_clr = 0x10,
	.pwrc_int_mask_set = 0x14,
	.pwrc_int_mask_clr = 0x18,
	.pwrc_int_status = 0x1c,
	.pwrc_pin_status = 0x20,
	.pwrc_rtc_pll_ctrl = 0x28,
	.pwrc_gpio3_debug = 0x34,
	.pwrc_rtc_noc_pwrctl_set = 0x38,
	.pwrc_rtc_noc_pwrctl_clr = 0x3c,
	.pwrc_rtc_can_ctrl = 0x48,
	.pwrc_rtc_can_status = 0x4c,
	.pwrc_fsm_m3_ctrl = 0x50,
	.pwrc_fsm_state = 0x54,
	.pwrc_rtcldo_reg = 0x58,
	.pwrc_gnss_ctrl = 0x5c,
	.pwrc_gnss_status = 0x60,
	.pwrc_xtal_reg = 0x64,
	.pwrc_xtal_ldo_mux_sel = 0x68,
	.pwrc_rtc_sw_rstc_set = 0x6c,
	.pwrc_rtc_sw_rstc_clr = 0x70,
	.pwrc_power_sw_ctrl_set = 0x74,
	.pwrc_power_sw_ctrl_clr = 0x78,
	.pwrc_rtc_dcog = 0x7c,
	.pwrc_m3_memories = 0x80,
	.pwrc_can0_memory = 0x84,
	.pwrc_rtc_gnss_memory = 0x88,
	.pwrc_m3_clk_en = 0x8c,
	.pwrc_can0_clk_en = 0x90,
	.pwrc_spi0_clk_en = 0x94,
	.pwrc_rtc_sec_clk_en = 0x98,
	.pwrc_rtc_noc_clk_en = 0x9c,
};

struct sirfsoc_pwrc_register sirfsoc_prima2_pwrc = {
	.pwrc_pdn_ctrl_set = 0x0,
	.pwrc_pon_status = 0x4,
	.pwrc_trigger_en_set = 0x8,
	.pwrc_int_status = 0xc,
	.pwrc_int_mask_set = 0x10,
	.pwrc_pin_status = 0x14,
	.pwrc_scratch_pad1 = 0x18,
	.pwrc_scratch_pad2 = 0x1c,
	.pwrc_scratch_pad3 = 0x20,
	.pwrc_scratch_pad4 = 0x24,
	.pwrc_scratch_pad5 = 0x28,
	.pwrc_scratch_pad6 = 0x2c,
	.pwrc_scratch_pad7 = 0x30,
	.pwrc_scratch_pad8 = 0x34,
	.pwrc_scratch_pad9 = 0x38,
	.pwrc_scratch_pad10 = 0x3c,
	.pwrc_scratch_pad11 = 0x40,
	.pwrc_scratch_pad12 = 0x44,
	.pwrc_gpio3_clk = 0x54,
	.pwrc_gpio_ds = 0x78,

};

static const struct of_device_id pwrc_ids[] = {
	{ .compatible = "sirf,prima2-pwrc", .data = &sirfsoc_prima2_pwrc},
	{ .compatible = "sirf,marco-pwrc",  .data = &sirfsoc_prima2_pwrc},
	{ .compatible = "sirf,atlas7-pwrc", .data = &sirfsoc_a7da_pwrc},
	{}
};

/*
 * suspend asm codes will access these to make DRAM become self-refresh and
 * system sleep
 */
u32 sirfsoc_pwrc_base;
u32 sirfsoc_sysrtc_base;
void __iomem *sirfsoc_memc_base;
void __iomem *sirfsoc_retain_base;
void __iomem *sirfsoc_pm_ipc_base;
static struct sirfsoc_pwrc_register *sirfpwrc;

static int (*sirfsoc_finish_suspend)(unsigned long);
struct proc_dir_entry *pr_entry;

static void sirfsoc_set_wakeup_source(void)
{
	u32 pwr_trigger_en_reg;
	pwr_trigger_en_reg = sirfsoc_rtc_iobrg_readl(sirfsoc_pwrc_base +
		sirfpwrc->pwrc_trigger_en_set);
#define X_ON_KEY_B (1 << 0)
#define RTC_ALARM0_B (1 << 2)
#define RTC_ALARM1_B (1 << 3)
	sirfsoc_rtc_iobrg_writel(pwr_trigger_en_reg | X_ON_KEY_B |
		RTC_ALARM0_B | RTC_ALARM1_B,
		sirfsoc_pwrc_base + sirfpwrc->pwrc_trigger_en_set);
}

static void sirfsoc_set_sleep_mode(u32 mode)
{
	u32 sleep_mode = sirfsoc_rtc_iobrg_readl(sirfsoc_pwrc_base +
		sirfpwrc->pwrc_pdn_ctrl_set);
	sleep_mode &= ~(SIRFSOC_SLEEP_MODE_MASK << 1);
	sleep_mode |= mode << 1;
	sirfsoc_rtc_iobrg_writel(sleep_mode, sirfsoc_pwrc_base +
		sirfpwrc->pwrc_pdn_ctrl_set);
	sirfsoc_set_wakeup_source();
}

void sirfsoc_pm_power_off(void)
{
	sirfsoc_set_sleep_mode(SIRFSOC_HIBERNATION_MODE);
	sirfsoc_rtc_iobrg_writel(
			(sirfsoc_rtc_iobrg_readl(
			sirfsoc_pwrc_base + sirfpwrc->pwrc_pdn_ctrl_set) |
			1 << SIRFSOC_START_PSAVING_BIT),
			sirfsoc_pwrc_base + sirfpwrc->pwrc_pdn_ctrl_set);
}

static int sirfsoc_pre_suspend_power_off(void)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, pwrc_ids);
	if (!np) {
		pr_err("unable to find compatible sirf pwrc node in dtb\n");
		return -ENOENT;
	}

	u32 wakeup_entry = virt_to_phys(cpu_resume);
	if (of_device_is_compatible(np, "sirf,atlas7-pwrc"))
		writel_relaxed(wakeup_entry,
			sirfsoc_retain_base + SIRFSOC_PWRC_SCRATCH_PAD1);

	else
		sirfsoc_rtc_iobrg_writel(wakeup_entry, sirfsoc_pwrc_base +
			sirfpwrc->pwrc_scratch_pad1);
	sirfsoc_set_wakeup_source();
	sirfsoc_set_sleep_mode(SIRFSOC_DEEP_SLEEP_MODE);

	return 0;
}

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

ssize_t sirfsoc_boot_stat_proc_read(struct file *file,
		char __user *buf, size_t size, loff_t *ppos)
{

	int i;
	u32 boot_stat;
	struct device_node *np;

	np = of_find_matching_node(NULL, pwrc_ids);
	if (!np) {
		pr_err("unable to find compatible sirf pwrc node in dtb\n");
		return -ENOENT;
	}

	if (of_device_is_compatible(np, "sirf,atlas7-pwrc"))
		boot_stat = readl_relaxed(sirfsoc_retain_base
				+ SIRFSOC_BOOT_STATUS);
	else
		boot_stat = sirfsoc_rtc_iobrg_readl(sirfsoc_pwrc_base +
			sirfpwrc->pwrc_scratch_pad3);

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
	struct device_node *np;
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

	np = of_find_matching_node(NULL, pwrc_ids);
	if (!np) {
		pr_err("unable to find compatible sirf pwrc node in dtb\n");
		return -ENOENT;
	}

	if (of_device_is_compatible(np, "sirf,atlas7-pwrc"))
		writel_relaxed(boot_stat,
			sirfsoc_retain_base + SIRFSOC_BOOT_STATUS);
	else
		sirfsoc_rtc_iobrg_writel(boot_stat,
			sirfsoc_pwrc_base + sirfpwrc->pwrc_scratch_pad3);

	return size;
}
#ifdef CONFIG_A7DA_PM_PWRC_DEBUG
ssize_t sirfsoc_pwrc_proc_read(struct file *file,
		char __user *buffer, size_t size, loff_t *ppos)
{
	int ret, val, i, pos = 0;
	char *buf;

	buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (i = 0; i < 0x9c && strlen(buf) < PAGE_SIZE; i = i + 4) {
		val = sirfsoc_rtc_iobrg_readl(sirfsoc_pwrc_base + i);
		pos += scnprintf(buf + pos,
			PAGE_SIZE - pos,
			"0x%x:0x%x\n", i, val);
	}

	ret = simple_read_from_buffer(buffer, size, ppos, buf, pos);
	kfree(buf);
	return ret;
}


ssize_t sirfsoc_pwrc_proc_write(struct file *file,
		const char __user *buf, size_t size, loff_t *ppos)
{
	char local_buf[32];
	u32 offset;
	u32 val;

	memset(local_buf, 0, 32);
	if (size >= sizeof(local_buf))
		return -ENOMEM;
	if (copy_from_user(local_buf, buf, size))
		return -EFAULT;
	local_buf[size] = '\0';
	if (sscanf(local_buf, "%x %x\n", &offset, &val) != 2)
		return -EINVAL;
	sirfsoc_rtc_iobrg_writel(val, sirfsoc_pwrc_base + offset);
	return size;
}

static const struct file_operations sirfsoc_pwrc_proc_fops = {
	.read		= sirfsoc_pwrc_proc_read,
	.write		= sirfsoc_pwrc_proc_write,
};
#endif



static const struct file_operations sirfsoc_boot_stat_proc_fops = {
	.read		= sirfsoc_boot_stat_proc_read,
	.write		= sirfsoc_boot_stat_proc_write,
};

#ifdef CONFIG_A7DA_PM_SYSRTC_DEBUG
ssize_t sirfsoc_sysrtc_proc_read(struct file *file,
		char __user *buffer, size_t size, loff_t *ppos)
{
	int ret, val, i, pos = 0;
	char *buf;

	buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (i = 0; i < 0x20 && strlen(buf) < PAGE_SIZE; i = i + 4) {
		val = sirfsoc_rtc_iobrg_readl(sirfsoc_sysrtc_base + i);
		pos += scnprintf(buf + pos,
			PAGE_SIZE - pos,
			"0x%x:0x%x\n", i, val);
	}

	ret = simple_read_from_buffer(buffer, size, ppos, buf, pos);
	kfree(buf);
	return ret;
}


ssize_t sirfsoc_sysrtc_proc_write(struct file *file,
		const char __user *buf, size_t size, loff_t *ppos)
{
	char local_buf[32];
	u32 offset;
	u32 val;

	memset(local_buf, 0, 32);
	if (size >= sizeof(local_buf))
		return -ENOMEM;
	if (copy_from_user(local_buf, buf, size))
		return -EFAULT;
	local_buf[size] = '\0';
	if (sscanf(local_buf, "%x %x\n", &offset, &val) != 2)
		return -EINVAL;
	sirfsoc_rtc_iobrg_writel(val, sirfsoc_sysrtc_base + offset);
	return size;
}

static const struct file_operations sirfsoc_sysrtc_proc_fops = {
	.read		= sirfsoc_sysrtc_proc_read,
	.write		= sirfsoc_sysrtc_proc_write,
};
#endif

static int __init sirfsoc_of_pwrc_init(void)
{
	struct device_node *np;
	const struct of_device_id *match;

	np = of_find_matching_node(NULL, pwrc_ids);
	if (!np) {
		pr_err("unable to find compatible sirf pwrc node in dtb\n");
		return -ENOENT;
	}
	/*
	 * pwrc behind rtciobrg is not located in memory space
	 * though the property is named reg. reg only means base
	 * offset for pwrc. then of_iomap is not suitable here.
	 */
	if (of_property_read_u32(np, "reg", &sirfsoc_pwrc_base))
		panic("unable to find base address of pwrc node in dtb\n");

	/*
	 * pwrc behind rtciobrg offset is diff between prima2 and a7da
	 * here match to each ids data for it.
	 */
	match = of_match_node(pwrc_ids, np);
	if (!match) {
		pr_err("Unknown device model\n");
		return -EINVAL;
	}
	sirfpwrc = (struct sirfsoc_pwrc_register *)match->data;

	of_node_put(np);

	proc_create_data("boot_status",
			S_IRUSR | S_IWUSR ,
			NULL,
			&sirfsoc_boot_stat_proc_fops,
			NULL);

#ifdef CONFIG_A7DA_PM_PWRC_DEBUG
	proc_create_data("pwrc",
			S_IRUSR | S_IWUSR ,
			NULL,
			&sirfsoc_pwrc_proc_fops,
			NULL);
#endif
	return 0;
}

static const struct of_device_id sysrtc_ids[] = {
	{ .compatible = "sirf,prima2-sysrtc" },
	{}
};

static int __init sirfsoc_of_sysrtc_init(void)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, sysrtc_ids);
	if (!np) {
		pr_err("unable to find compatible sirf pwrc node in dtb\n");
		return -ENOENT;
	}

	if (of_property_read_u32(np, "reg", &sirfsoc_sysrtc_base))
		panic("unable to find base address of sysrtc node in dtb\n");

	of_node_put(np);

#ifdef CONFIG_A7DA_PM_SYSRTC_DEBUG
	proc_create_data("sysrtc",
			S_IRUSR | S_IWUSR ,
			NULL,
			&sirfsoc_sysrtc_proc_fops,
			NULL);
#endif
	return 0;
}


static const struct of_device_id memc_ids[] = {
	{
		.compatible = "sirf,prima2-memc",
		.data = sirfsoc_prima2_finish_suspend,
	}, {
		.compatible = "sirf,marco-memc",
		.data = sirfsoc_marco_finish_suspend,
	}, {
		.compatible = "sirf,atlas7-memc",
		.data = sirfsoc_atlas7_finish_suspend,
	}, {
	}
};


static int sirfsoc_memc_probe(struct platform_device *op)
{
	struct device_node *np = op->dev.of_node;

	sirfsoc_memc_base = of_iomap(np, 0);
	if (!sirfsoc_memc_base)
		panic("unable to map memc registers\n");

	sirfsoc_finish_suspend = of_match_node(memc_ids, np)->data;

	return 0;
}

static struct platform_driver sirfsoc_memc_driver = {
	.probe		= sirfsoc_memc_probe,
	.driver = {
		.name = "sirfsoc-memc",
		.owner = THIS_MODULE,
		.of_match_table	= memc_ids,
	},
};

static int __init sirfsoc_memc_init(void)
{
	return platform_driver_register(&sirfsoc_memc_driver);
}

static const struct of_device_id retainreg_ids[] = {
	{ .compatible = "sirf,atlas7-retain"},
};

static int sirfsoc_retain_probe(struct platform_device *op)
{
	struct device_node *np = op->dev.of_node;

	sirfsoc_retain_base = of_iomap(np, 0);
	if (!sirfsoc_retain_base)
		panic("unable to map retain registers\n");

	return 0;
}

static struct platform_driver sirfsoc_retain_driver = {
	.probe		= sirfsoc_retain_probe,
	.driver = {
		.name = "sirfsoc-retain",
		.owner = THIS_MODULE,
		.of_match_table	= retainreg_ids,
	},
};

static int __init sirfsoc_retain_init(void)
{
	return platform_driver_register(&sirfsoc_retain_driver);
}

static const struct of_device_id ipc_ids[] = {
	{ .compatible = "sirf,atlas7-pmipc"},
};

static int sirfsoc_ipc_probe(struct platform_device *op)
{
	struct device_node *np = op->dev.of_node;

	sirfsoc_pm_ipc_base = of_iomap(np, 0);
	if (!sirfsoc_pm_ipc_base)
		panic("unable to map ipc registers\n");
	return 0;
}

static struct platform_driver sirfsoc_ipc_driver = {
	.probe		= sirfsoc_ipc_probe,
	.driver = {
		.name = "sirfsoc-pmipc",
		.owner = THIS_MODULE,
		.of_match_table	= ipc_ids,
	},
};

static int __init sirfsoc_ipc_init(void)
{
	return platform_driver_register(&sirfsoc_ipc_driver);
}

int __init sirfsoc_pm_init(void)
{
	sirfsoc_of_pwrc_init();
	sirfsoc_of_sysrtc_init();
	sirfsoc_memc_init();
	sirfsoc_retain_init();
	sirfsoc_ipc_init();
	suspend_set_ops(&sirfsoc_pm_ops);
	pm_power_off = sirfsoc_pm_power_off;
	return 0;
}
