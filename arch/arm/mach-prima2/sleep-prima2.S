/*
 * sleep mode for CSR SiRFprimaII
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

#include <linux/linkage.h>
#include <asm/ptrace.h>
#include <asm/assembler.h>

#include "pm.h"

#define DENALI_CTL_22_OFF	0x58
#define DENALI_CTL_112_OFF	0x1c0

	.text

ENTRY(sirfsoc_prima2_finish_suspend)
	@ r5: 	mem controller
	ldr     r0, =sirfsoc_memc_base
	ldr	r5, [r0]
	@ r6: 	pwrc base offset
	ldr	r0, =0x3000
	ldr	r6, [r0]
	@ r7: 	rtc iobrg controller
	ldr     r0, =sirfsoc_rtciobrg_base
	ldr	r7, [r0]

	@ Read the power control register and set the
	@ sleep force bit.
	add	r0, r6, #SIRFSOC_PWRC_PDN_CTRL
	bl	__sirfsoc_rtc_iobrg_readl
	orr	r0,r0,#SIRFSOC_PWR_SLEEPFORCE
	add	r1, r6, #SIRFSOC_PWRC_PDN_CTRL
	bl	sirfsoc_rtc_iobrg_pre_writel
	mov	r1, #0x1

	@ read the MEM ctl register and set the self
	@ refresh bit

	ldr	r2, [r5, #DENALI_CTL_22_OFF]
	orr	r2, r2, #0x1

	@ Following code has to run from cache since
	@ the RAM is going to self refresh mode
	.align 5
	str	r2, [r5, #DENALI_CTL_22_OFF]

1:
	ldr	r4, [r5, #DENALI_CTL_112_OFF]
	tst	r4, #0x1
	bne	1b

	@ write SLEEPFORCE through rtc iobridge

	str	r1, [r7]
	@ wait rtc io bridge sync
1:
	ldr	r3, [r7]
	tst	r3, #0x01
	bne	1b
	b .
