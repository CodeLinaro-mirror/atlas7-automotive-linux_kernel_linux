/*
 * arch/arm/mach-prima2/include/mach/regs-gpio.h
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __REGS_GPIO_H_
#define __REGS_GPIO_H_

#define SIRFSOC_GPIO_CTRL(g, i)			((g)*0x100 + (i)*4)
#define SIRFSOC_GPIO_DSP_EN0			(0x80)
#define SIRFSOC_GPIO_PAD_EN(g)			((g)*0x100 + 0x84)
#define SIRFSOC_GPIO_INT_STATUS(g) 		((g)*0x100 + 0x8C)

#define SIRFSOC_GPIO_CTL_INTR_LOW_MASK		0x1
#define SIRFSOC_GPIO_CTL_INTR_HIGH_MASK		0x2
#define SIRFSOC_GPIO_CTL_INTR_TYPE_MASK		0x4
#define SIRFSOC_GPIO_CTL_INTR_EN_MASK		0x8
#define SIRFSOC_GPIO_CTL_INTR_STS_MASK		0x10
#define SIRFSOC_GPIO_CTL_OUT_EN_MASK		0x20
#define SIRFSOC_GPIO_CTL_DATAOUT_MASK		0x40
#define SIRFSOC_GPIO_CTL_DATAIN_MASK		0x80
#define SIRFSOC_GPIO_CTL_PULL_MASK		0x100
#define SIRFSOC_GPIO_CTL_PULL_HIGH      	0x200
#define SIRFSOC_GPIO_CTL_DSP_INT		0x400

#define SIRFSOC_GPIO_NUM(bank, index)	(((bank)*(32)) + (index))
#endif
