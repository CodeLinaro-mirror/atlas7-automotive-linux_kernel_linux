/*
 * arch/arm/mach-prima2/include/mach/irqs.h
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __ASM_ARCH_IRQS_H
#define __ASM_ARCH_IRQS_H

#define SIRFSOC_INTENAL_IRQ_START  0
#define SIRFSOC_INTENAL_IRQ_END    59

#define SIRFSOC_GPIO_IO_CPLD_SIZE	(5 * 8)
#define SIRFSOC_GPIO_HS_CPLD_SIZE	(16 * 8)

#define SIRFSOC_GPIO_IRQ_START     (SIRFSOC_INTENAL_IRQ_END + 1)

#define SIRFSOC_GPIO_NO_OF_BANKS        5
#define SIRFSOC_GPIO_BANK_SIZE          32

#define SIRFSOC_GPIO_IRQ_END       (SIRFSOC_GPIO_IRQ_START + \
		SIRFSOC_GPIO_NO_OF_BANKS * SIRFSOC_GPIO_BANK_SIZE)

#define NR_IRQS	220

#endif
