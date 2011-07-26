/*
 * arch/arm/mach-prima2/include/mach/gpio.h
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __MACH_GPIO_H
#define __MACH_GPIO_H

#include <mach/irqs.h>

#ifndef CONFIG_GPIO_SIRFCPLD
#define ARCH_NR_GPIOS	(SIRFSOC_GPIO_BANK_SIZE * SIRFSOC_GPIO_NO_OF_BANKS)
#else
#define ARCH_NR_GPIOS	(SIRFSOC_GPIO_BANK_SIZE * SIRFSOC_GPIO_NO_OF_BANKS + \
	SIRFSOC_GPIO_CPLD_SIZE + SIRFSOC_GPIO_IO_CPLD_SIZE + \
	SIRFSOC_GPIO_HS_CPLD_SIZE)
#endif

#include <linux/errno.h>
#include <asm-generic/gpio.h>

#define gpio_get_value	__gpio_get_value
#define gpio_set_value	__gpio_set_value
#define gpio_cansleep	__gpio_cansleep
#define gpio_to_irq	__gpio_to_irq

void gpio_set_pull(unsigned gpio, int enable);
void gpio_pull_down(unsigned gpio);
void gpio_pull_up(unsigned gpio);

#endif
