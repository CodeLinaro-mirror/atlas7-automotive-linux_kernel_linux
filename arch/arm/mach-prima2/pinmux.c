/*
 * pinmux for CSR SiRF prima2
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <asm/mach/irq.h>
#include <mach/pinmux.h>
#include <mach/regs-gpio.h>
#include "common.h"

#define SIRFSOC_RSC_PIN_MUX        0x0004

struct muxmask {
	unsigned long group;
	unsigned long mask;
};

struct padmux {
	char *name;
	unsigned long muxmask_counts;
	struct muxmask *pad_muxmask;
	unsigned long funcmask;
	unsigned long funcval;
};

void __iomem *sirfsoc_gpio_pinmux_base;

static unsigned long pad_gpio_used_map[SIRFSOC_GPIO_NO_OF_BANKS];
static unsigned long pad_module_used_map[SIRFSOC_GPIO_NO_OF_BANKS];

static DEFINE_SPINLOCK(pad_lock);

void sirfsoc_put_gpios(int group, u32 bitmask)
{
	if (group >= SIRFSOC_GPIO_NO_OF_BANKS) {
		pr_err("%d th group is not valid. Only %d groups are available\n",
			group, SIRFSOC_GPIO_NO_OF_BANKS);
	} else {
		int muxval;
		unsigned long flags;
		spin_lock_irqsave(&pad_lock, flags);
		if (pad_module_used_map[group] & bitmask) {
			pr_warning("%d th group is used by other modules\n", group);
		}
		pad_gpio_used_map[group] &= (~bitmask);
		muxval = readl(sirfsoc_gpio_pinmux_base + SIRFSOC_GPIO_PAD_EN(group));
		muxval = muxval & (~bitmask);
		writel(muxval, sirfsoc_gpio_pinmux_base + SIRFSOC_GPIO_PAD_EN(group));
		spin_unlock_irqrestore(&pad_lock, flags);
	}
}
EXPORT_SYMBOL(sirfsoc_put_gpios);

void sirfsoc_put_gpio(int group, int bitno)
{
	sirfsoc_put_gpios(group, 1 << bitno);
}
EXPORT_SYMBOL(sirfsoc_put_gpio);

void sirfsoc_get_gpios(int group, u32 bitmask)
{
	if (group >= SIRFSOC_GPIO_NO_OF_BANKS) {
		pr_err("%d th group is not valid. Only %d groups are available\n",
			group, SIRFSOC_GPIO_NO_OF_BANKS);
	} else {
		int muxval;
		unsigned long flags;
		spin_lock_irqsave(&pad_lock, flags);
		if (pad_module_used_map[group] & bitmask) {
			pr_err("%d th group is used by other modules\n", group);
			spin_unlock_irqrestore(&pad_lock, flags);
			return;
		}
		pad_gpio_used_map[group] |= bitmask;
		muxval = readl(sirfsoc_gpio_pinmux_base + SIRFSOC_GPIO_PAD_EN(group));
		muxval = muxval | (bitmask);
		writel(muxval, sirfsoc_gpio_pinmux_base + SIRFSOC_GPIO_PAD_EN(group));
		spin_unlock_irqrestore(&pad_lock, flags);
	}
}
EXPORT_SYMBOL(sirfsoc_get_gpios);

void sirfsoc_get_gpio(int group, int bitno)
{
	sirfsoc_get_gpios(group, 1 << bitno);
}
EXPORT_SYMBOL(sirfsoc_get_gpio);

static struct muxmask lcd_16bits_muxmask[] = {
	{
		.group = 3,
		.mask =
			~((1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) |
				(1 << 6) | (1 << 7)
				| (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11) | (1 << 12) | (1 << 13)
				| (1 << 14) | (1 << 15)
				| (1 << 16) | (1 << 17) | (1 << 18)),
	}, {
		.group = 2,
		.mask = ~(1 << 31),
	},
};

static struct padmux lcd_16bits_padmux = {
	.name = "lcd_16bits",
	.muxmask_counts = ARRAY_SIZE(lcd_16bits_muxmask),
	.pad_muxmask = lcd_16bits_muxmask,
	.funcmask = (1 << 4),
	.funcval = (0 << 4),
};

static struct muxmask lcd_18bits_muxmask[] = {
	{
		.group = 3,
		.mask =
			~((1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) |
				(1 << 6) | (1 << 7)
				| (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11) | (1 << 12) | (1 << 13)
				| (1 << 14) | (1 << 15)
				| (1 << 16) | (1 << 17) | (1 << 18)),
	}, {
		.group = 2,
		.mask = ~(1 << 31),
	}, {
		.group = 0,
		.mask = ~((1 << 16) | (1 << 17)),
	},
};

static struct padmux lcd_18bits_padmux = {
	.name = "lcd_18bits",
	.muxmask_counts = ARRAY_SIZE(lcd_18bits_muxmask),
	.pad_muxmask = lcd_18bits_muxmask,
	.funcmask = (1 << 4),
	.funcval = (0 << 4),
};

static struct muxmask lcd_24bits_muxmask[] = {
	{
		.group = 3,
		.mask =
			~((1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) |
				(1 << 6) | (1 << 7)
				| (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11) | (1 << 12) | (1 << 13)
				| (1 << 14) | (1 << 15)
				| (1 << 16) | (1 << 17) | (1 << 18)),
	}, {
		.group = 2,
		.mask = ~(1 << 31),
	}, {
		.group = 0,
		.mask =
			~((1 << 16) | (1 << 17) | (1 << 18) | (1 << 19) | (1 << 20) |
				(1 << 21) | (1 << 22) | (1 << 23)),
	},
};

static struct padmux lcd_24bits_padmux = {
	.name = "lcd_24bits",
	.muxmask_counts = ARRAY_SIZE(lcd_24bits_muxmask),
	.pad_muxmask = lcd_24bits_muxmask,
	.funcmask = (1 << 4),
	.funcval = (0 << 4),
};

static struct muxmask lcdrom_muxmask[] = {
	{
		.group = 3,
		.mask =
			~((1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) |
				(1 << 6) | (1 << 7)
				| (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11) | (1 << 12) | (1 << 13)
				| (1 << 14) | (1 << 15)
				| (1 << 16) | (1 << 17) | (1 << 18)),
	}, {
		.group = 2,
		.mask = ~(1 << 31),
	}, {
		.group = 0,
		.mask = ~((1 << 23)),
	},
};

static struct padmux lcdrom_padmux = {
	.name = "lcdrom",
	.muxmask_counts = ARRAY_SIZE(lcdrom_muxmask),
	.pad_muxmask = lcdrom_muxmask,
	.funcmask = (1 << 4),
	.funcval = (1 << 4),
};

static struct muxmask sdmmc3_muxmask[] = {
	{
		.group = 0,
		.mask = ~((1 << 30) | (1 << 31)),
	}, {
		.group = 1,
		.mask = ~((1 << 0) | (1 << 1) | (1 << 2) | (1 << 3)),
	},
};

static struct padmux sdmmc3_padmux = {
	.name = "sdmmc3",
	.muxmask_counts = ARRAY_SIZE(sdmmc3_muxmask),
	.pad_muxmask = sdmmc3_muxmask,
	.funcmask = (1 << 7),
	.funcval = (0 << 7),
};

static struct muxmask spi0_muxmask[] = {
	{
		.group = 1,
		.mask = ~((1 << 0) | (1 << 1) | (1 << 2) | (1 << 3)),
	},
};

static struct padmux spi0_padmux = {
	.name = "spi0",
	.muxmask_counts = ARRAY_SIZE(spi0_muxmask),
	.pad_muxmask = spi0_muxmask,
	.funcmask = (1 << 7),
	.funcval = (1 << 7),
};

static struct muxmask sdmmc4_muxmask[] = {
	{
		.group = 1,
		.mask =
			~((1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 9)),
	},
};

static struct padmux sdmmc4_padmux = {
	.name = "sdmmc4",
	.muxmask_counts = ARRAY_SIZE(sdmmc4_muxmask),
	.pad_muxmask = sdmmc4_muxmask,
};

static struct muxmask cko1_muxmask[] = {
	{
		.group = 1,
		.mask = ~(1 << 10),
	},
};

static struct padmux cko1_padmux = {
	.name = "cko1",
	.muxmask_counts = ARRAY_SIZE(cko1_muxmask),
	.pad_muxmask = cko1_muxmask,
	.funcmask = (1 << 3),
	.funcval = (0 << 3),
};

static struct muxmask i2s_muxmask[] = {
	{
		.group = 1,
		.mask =
			~((1 << 10) | (1 << 11) | (1 << 12) | (1 << 13) | (1 << 14) | (1 << 19)
				| (1 << 23) | (1 << 28)),
	},
};

static struct padmux i2s_padmux = {
	.name = "i2s",
	.muxmask_counts = ARRAY_SIZE(i2s_muxmask),
	.pad_muxmask = i2s_muxmask,
	.funcmask = (1 << 3) | (1 << 9),
	.funcval = (1 << 3) | (0 << 9),
};

static struct muxmask ac97_muxmask[] = {
	{
		.group = 1,
		.mask = ~((1 << 11) | (1 << 12) | (1 << 13) | (1 << 14)),
	},
};

static struct padmux ac97_padmux = {
	.name = "ac97",
	.muxmask_counts = ARRAY_SIZE(ac97_muxmask),
	.pad_muxmask = ac97_muxmask,
	.funcmask = (1 << 8),
	.funcval = (0 << 8),
};

static struct muxmask spi1_muxmask[] = {
	{
		.group = 1,
		.mask = ~((1 << 11) | (1 << 12) | (1 << 13) | (1 << 14)),
	},
};

static struct padmux spi1_padmux = {
	.name = "spi1",
	.muxmask_counts = ARRAY_SIZE(spi1_muxmask),
	.pad_muxmask = spi1_muxmask,
	.funcmask = (1 << 8),
	.funcval = (1 << 8),
};

static struct muxmask sdmmc1_muxmask[] = {
	{
		.group = 0,
		.mask = ~((1 << 27) | (1 << 28) | (1 << 29)),
	},
};

static struct padmux sdmmc1_padmux = {
	.name = "sdmmc1",
	.muxmask_counts = ARRAY_SIZE(sdmmc1_muxmask),
	.pad_muxmask = sdmmc1_muxmask,
};

static struct muxmask gps_muxmask[] = {
	{
		.group = 0,
		.mask = ~((1 << 24) | (1 << 25) | (1 << 26) | (1 << 4)),
	},
};

static struct padmux gps_padmux = {
	.name = "gps",
	.muxmask_counts = ARRAY_SIZE(gps_muxmask),
	.pad_muxmask = gps_muxmask,
	.funcmask = (1 << 12) | (1 << 13) | (1 << 14),
	.funcval = (1 << 12) | (0 << 13) | (0 << 14),
};

static struct muxmask sdmmc5_muxmask[] = {
	{
		.group = 0,
		.mask = ~((1 << 24) | (1 << 25) | (1 << 26)),
	}, {
		.group = 1,
		.mask = ~(1 << 29),
	}, {
		.group = 2,
		.mask = ~((1 << 0) | (1 << 1)),
	},
};

static struct padmux sdmmc5_padmux = {
	.name = "sdmmc5",
	.muxmask_counts = ARRAY_SIZE(sdmmc5_muxmask),
	.pad_muxmask = sdmmc5_muxmask,
	.funcmask = (1 << 13) | (1 << 14),
	.funcval = (1 << 13) | (1 << 14),
};

static struct muxmask uart1_muxmask[] = {
	{
		.group = 1,
		.mask = ~((1 << 15) | (1 << 17)),
	},
};

static struct padmux uart1_padmux = {
	.name = "uart1",
	.muxmask_counts = ARRAY_SIZE(uart1_muxmask),
	.pad_muxmask = uart1_muxmask,
};

static struct muxmask uart2_muxmask[] = {
	{
		.group = 1,
		.mask = ~((1 << 16) | (1 << 18) | (1 << 24) | (1 << 27)),
	},
};

static struct padmux uart2_padmux = {
	.name = "uart2",
	.muxmask_counts = ARRAY_SIZE(uart2_muxmask),
	.pad_muxmask = uart2_muxmask,
	.funcmask = (1 << 10),
	.funcval = (1 << 10),
};

static struct muxmask uart2_nostreamctrl_muxmask[] = {
	{
		.group = 1,
		.mask = ~((1 << 16) | (1 << 18)),
	},
};

static struct padmux uart2_nostreamctrl_padmux = {
	.name = "uart2_nostreamctrl",
	.muxmask_counts = ARRAY_SIZE(uart2_nostreamctrl_muxmask),
	.pad_muxmask = uart2_nostreamctrl_muxmask,
};

static struct muxmask usp0_muxmask[] = {
	{
		.group = 1,
		.mask = ~((1 << 19) | (1 << 20) | (1 << 21) | (1 << 22) | (1 << 23)),
	},
};

static struct padmux usp0_padmux = {
	.name = "usp0",
	.muxmask_counts = ARRAY_SIZE(usp0_muxmask),
	.pad_muxmask = usp0_muxmask,
	.funcmask = (1 << 1) | (1 << 2) | (1 << 6) | (1 << 9),
	.funcval = (0 << 1) | (0 << 2) | (0 << 6) | (0 << 9),
};

static struct muxmask usp1_muxmask[] = {
	{
		.group = 1,
		.mask = ~((1 << 24) | (1 << 25) | (1 << 26) | (1 << 27) | (1 << 28)),
	},
};

static struct padmux usp1_padmux = {
	.name = "usp1",
	.muxmask_counts = ARRAY_SIZE(usp1_muxmask),
	.pad_muxmask = usp1_muxmask,
	.funcmask = (1 << 1) | (1 << 9) | (1 << 10) | (1 << 11),
	.funcval = (0 << 1) | (0 << 9) | (0 << 10) | (0 << 11),
};

static struct muxmask usp2_muxmask[] = {
	{
		.group = 1,
		.mask = ~((1 << 29) | (1 << 30) | (1 << 31)),
	}, {
		.group = 2,
		.mask = ~((1 << 0) | (1 << 1)),
	},
};

static struct padmux usp2_padmux = {
	.name = "usp2",
	.muxmask_counts = ARRAY_SIZE(usp2_muxmask),
	.pad_muxmask = usp2_muxmask,
	.funcmask = (1 << 13) | (1 << 14),
	.funcval = (0 << 13) | (0 << 14),
};

static struct muxmask nand_muxmask[] = {
	{
		.group = 2,
		.mask = ~((1 << 2) | (1 << 3) | (1 << 28) | (1 << 29) | (1 << 30)),
	},
};

static struct padmux nand_padmux = {
	.name = "nand",
	.muxmask_counts = ARRAY_SIZE(nand_muxmask),
	.pad_muxmask = nand_muxmask,
	.funcmask = (1 << 5),
	.funcval = (0 << 5),
};

static struct padmux sdmmc0_padmux = {
	.name = "sdmmc0",
	.muxmask_counts = 0,
	.funcmask = (1 << 5),
	.funcval = (0 << 5),
};

static struct muxmask sdmmc2_muxmask[] = {
	{
		.group = 2,
		.mask = ~((1 << 2) | (1 << 3)),
	},
};

static struct padmux sdmmc2_padmux = {
	.name = "sdmmc2",
	.muxmask_counts = ARRAY_SIZE(sdmmc2_muxmask),
	.pad_muxmask = nand_muxmask,
	.funcmask = (1 << 5),
	.funcval = (1 << 5),
};

static struct muxmask uart0_muxmask[] = {
	{
		.group = 2,
		.mask = ~((1 << 4) | (1 << 5)),
	}, {
		.group = 1,
		.mask = ~((1 << 23) | (1 << 28)),
	},
};

static struct padmux uart0_padmux = {
	.name = "uart0",
	.muxmask_counts = ARRAY_SIZE(uart0_muxmask),
	.pad_muxmask = uart0_muxmask,
	.funcmask = (1 << 9),
	.funcval = (1 << 9),
};

static struct muxmask uart0_nostreamctrl_muxmask[] = {
	{
		.group = 2,
		.mask = ~((1 << 4) | (1 << 5)),
	},
};

static struct padmux uart0_nostreamctrl_padmux = {
	.name = "uart0_nostreamctrl",
	.muxmask_counts = ARRAY_SIZE(uart0_nostreamctrl_muxmask),
	.pad_muxmask = uart0_nostreamctrl_muxmask,
};

static struct muxmask cko0_muxmask[] = {
	{
		.group = 2,
		.mask = ~((1 << 14)),
	},
};

static struct padmux cko0_padmux = {
	.name = "cko0",
	.muxmask_counts = ARRAY_SIZE(cko0_muxmask),
	.pad_muxmask = cko0_muxmask,
};

static struct muxmask vip_muxmask[] = {
	{
		.group = 2,
		.mask = ~((1 << 15) | (1 << 16) | (1 << 17) | (1 << 18) | (1 << 19)
			| (1 << 20) | (1 << 21) | (1 << 22) | (1 << 23) | (1 << 24) |
			(1 << 25)),
	},
};

static struct padmux vip_padmux = {
	.name = "vip",
	.muxmask_counts = ARRAY_SIZE(vip_muxmask),
	.pad_muxmask = vip_muxmask,
	.funcmask = (1 << 0),
	.funcval = (0 << 0),
};

static struct muxmask i2c0_muxmask[] = {
	{
		.group = 2,
		.mask = ~((1 << 26) | (1 << 27)),
	},
};

static struct padmux i2c0_padmux = {
	.name = "i2c0",
	.muxmask_counts = ARRAY_SIZE(i2c0_muxmask),
	.pad_muxmask = i2c0_muxmask,
};

static struct muxmask i2c1_muxmask[] = {
	{
		.group = 0,
		.mask = ~((1 << 13) | (1 << 15)),
	},
};

static struct padmux i2c1_padmux = {
	.name = "i2c1",
	.muxmask_counts = ARRAY_SIZE(i2c1_muxmask),
	.pad_muxmask = i2c1_muxmask,
};

static struct muxmask viprom_muxmask[] = {
	{
		.group = 2,
		.mask = ~((1 << 15) | (1 << 16) | (1 << 17) | (1 << 18) | (1 << 19)
			| (1 << 20) | (1 << 21) | (1 << 22) | (1 << 23) | (1 << 24) |
			(1 << 25)),
	}, {
		.group = 0,
		.mask = ~(1 << 12),
	},
};

static struct padmux viprom_padmux = {
	.name = "viprom",
	.muxmask_counts = ARRAY_SIZE(viprom_muxmask),
	.pad_muxmask = viprom_muxmask,
	.funcmask = (1 << 0),
	.funcval = (1 << 0),
};

static struct muxmask pwm0_muxmask[] = {
	{
		.group = 0,
		.mask = ~(1 << 4),
	},
};

static struct padmux pwm0_padmux = {
	.name = "pwm0",
	.muxmask_counts = ARRAY_SIZE(pwm0_muxmask),
	.pad_muxmask = pwm0_muxmask,
	.funcmask = (1 << 12),
	.funcval = (0 << 12),
};

static struct muxmask pwm1_muxmask[] = {
	{
		.group = 0,
		.mask = ~(1 << 5),
	},
};

static struct padmux pwm1_padmux = {
	.name = "pwm1",
	.muxmask_counts = ARRAY_SIZE(pwm1_muxmask),
	.pad_muxmask = pwm1_muxmask,
};

static struct muxmask pwm2_muxmask[] = {
	{
		.group = 0,
		.mask = ~(1 << 6),
	},
};

static struct padmux pwm2_padmux = {
	.name = "pwm2",
	.muxmask_counts = ARRAY_SIZE(pwm2_muxmask),
	.pad_muxmask = pwm2_muxmask,
};

static struct muxmask pwm3_muxmask[] = {
	{
		.group = 0,
		.mask = ~(1 << 7),
	},
};

static struct padmux pwm3_padmux = {
	.name = "pwm3",
	.muxmask_counts = ARRAY_SIZE(pwm3_muxmask),
	.pad_muxmask = pwm3_muxmask,
};

static struct muxmask warm_rst_muxmask[] = {
	{
		.group = 0,
		.mask = ~(1 << 8),
	},
};

static struct padmux warm_rst_padmux = {
	.name = "warm_rst",
	.muxmask_counts = ARRAY_SIZE(warm_rst_muxmask),
	.pad_muxmask = warm_rst_muxmask,
};

static struct muxmask usb0_utmi_drvbus_muxmask[] = {
	{
		.group = 1,
		.mask = ~(1 << 22),
	},
};
static struct padmux usb0_utmi_drvbus_padmux = {
	.name = "usb0_utmi",
	.muxmask_counts = ARRAY_SIZE(usb0_utmi_drvbus_muxmask),
	.pad_muxmask = usb0_utmi_drvbus_muxmask,
	.funcmask = (1 << 6),
	.funcval = (1 << 6), /* refer to PAD_UTMI_DRVVBUS0_ENABLE */
};

static struct muxmask usb1_utmi_drvbus_muxmask[] = {
	{
		.group = 1,
		.mask = ~(1 << 27),
	},
};
static struct padmux usb1_utmi_drvbus_padmux = {
	.name = "usb1_utmi",
	.muxmask_counts = ARRAY_SIZE(usb1_utmi_drvbus_muxmask),
	.pad_muxmask = usb1_utmi_drvbus_muxmask,
	.funcmask = (1 << 11),
	.funcval = (1 << 11), /* refer to PAD_UTMI_DRVVBUS1_ENABLE */
};

static struct padmux *prima2_all_padmux[] = {
	&lcd_16bits_padmux,
	&lcd_18bits_padmux,
	&lcd_24bits_padmux,
	&lcdrom_padmux,
	&sdmmc3_padmux,
	&spi0_padmux,
	&sdmmc4_padmux,
	&cko1_padmux,
	&i2s_padmux,
	&ac97_padmux,
	&spi1_padmux,
	&sdmmc1_padmux,
	&gps_padmux,
	&sdmmc5_padmux,
	&uart1_padmux,
	&uart2_padmux,
	&uart2_nostreamctrl_padmux,
	&usp0_padmux,
	&usp1_padmux,
	&usp2_padmux,
	&nand_padmux,
	&sdmmc0_padmux,
	&sdmmc2_padmux,
	&uart0_padmux,
	&uart0_nostreamctrl_padmux,
	&cko0_padmux,
	&vip_padmux,
	&i2c0_padmux,
	&i2c1_padmux,
	&viprom_padmux,
	&pwm0_padmux,
	&pwm1_padmux,
	&pwm2_padmux,
	&pwm3_padmux,
	&warm_rst_padmux,
	&usb0_utmi_drvbus_padmux,
	&usb1_utmi_drvbus_padmux,
};

void sirfsoc_pad_get(const char *name)
{
	int i, j;
	for (i = 0; i < ARRAY_SIZE(prima2_all_padmux); i++) {
		struct padmux *mux = prima2_all_padmux[i];
		struct muxmask *mask = mux->pad_muxmask;
		unsigned long flags;

		if (strcmp(mux->name, name))
			continue;

		spin_lock_irqsave(&pad_lock, flags);
		for (j = 0; j < mux->muxmask_counts; j++) {
			int muxval;
			if (pad_gpio_used_map[mask[j].group] &
				(~(mask[j].mask))) {
				pr_err(
					"This pad is used by gpio\n");
				spin_unlock_irqrestore(&pad_lock,
					flags);
				return;
			}
			pad_module_used_map[mask[j].group] |=
				(~(mask[j].mask));
			muxval =
				readl(sirfsoc_gpio_pinmux_base +
					SIRFSOC_GPIO_PAD_EN(mask[j].group));
			muxval = muxval & (mask[j].mask);
			writel(muxval,
				sirfsoc_gpio_pinmux_base + SIRFSOC_GPIO_PAD_EN(mask[j].group));
		}
		if (mux->funcmask) {
			unsigned long func_en_val;
			func_en_val =
				readl(sirfsoc_rstc_base + SIRFSOC_RSC_PIN_MUX);
			func_en_val =
				(func_en_val & (~(mux->funcmask))) | (mux->
					funcval);
			writel(func_en_val,
				sirfsoc_rstc_base + SIRFSOC_RSC_PIN_MUX);
		}
		spin_unlock_irqrestore(&pad_lock, flags);
	}
}
EXPORT_SYMBOL(sirfsoc_pad_get);

void sirfsoc_pad_put(const char *name)
{
	int i, j;
	for (i = 0; i < ARRAY_SIZE(prima2_all_padmux); i++) {
		struct padmux *mux = prima2_all_padmux[i];
		struct muxmask *mask = mux->pad_muxmask;
		unsigned long flags;

		if (strcmp(mux->name, name))
			continue;

		spin_lock_irqsave(&pad_lock, flags);
		for (j = 0; j < mux->muxmask_counts; j++) {
			int muxval;
			if (pad_gpio_used_map[mask[j].group] &
				(~(mask[j].mask)))
				pr_warning("This pad is used by gpio\n");
			pad_module_used_map[mask[j].group] &=
				mask[j].mask;
			muxval =
				readl(sirfsoc_gpio_pinmux_base +
					SIRFSOC_GPIO_PAD_EN(mask[j].group));
			muxval = muxval | (~(mask[j].mask));
			writel(muxval,
				sirfsoc_gpio_pinmux_base + SIRFSOC_GPIO_PAD_EN(mask[j].group));
		}
		spin_unlock_irqrestore(&pad_lock, flags);
	}
}
EXPORT_SYMBOL(sirfsoc_pad_put);

static struct of_device_id pinmux_ids[]  = {
	{ .compatible = "sirf,prima2-pinmux" },
};

static int __init sirfsoc_of_pinmux_init(void)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, pinmux_ids);
	if (!np)
		panic("unable to find compatible pinmux node in dtb\n");

	sirfsoc_gpio_pinmux_base = of_iomap(np, 0);
	if (!sirfsoc_gpio_pinmux_base)
		panic("unable to map gpio/pinmux cpu registers\n");

	of_node_put(np);

	return 0;
}
early_initcall(sirfsoc_of_pinmux_init);

