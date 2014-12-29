/*
 * sleep mode for CSR SiRFmarco
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/linkage.h>
#include <asm/ptrace.h>
#include <asm/assembler.h>

#include "pm.h"

#define MEMC_SCFG_OFFSET	0x00
#define MEMC_SCTL_OFFSET	0x04
#define MEMC_STAT_OFFSET	0x08
#define MEMC_INTRSTAT_OFFSET	0x0C
#define MEMC_DFILPCFG0_OFFSET	0x2F0
#define MEMC_MCFG1_OFFSET	0x7C
#define MEMC_MCFG_OFFSET	0x80
#define MEMC_CSTAT_OFFSET	0x488

	.text

ENTRY(sirfsoc_marco_finish_suspend)
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

	@ move to Config state from Init_mem or Access
	mov	r4, #0x1
	str	r4, [r5, #MEMC_SCTL_OFFSET]

	@ wait till enter config state
1:
	ldr	r4, [r5, #MEMC_STAT_OFFSET]
	and 	r4, r4, #0xf
	cmp	r4, #0x1
	bne	1b

	@ DFI low power configuration
	ldr     r4, [r5, #MEMC_DFILPCFG0_OFFSET]
	orr 	r4, r4, #0x1
	orr 	r4, r4, #0x100
	orr 	r4, r4, #0x1000000
	str	r4, [r5, #MEMC_DFILPCFG0_OFFSET]

	@ disable automatically power down/self-refresh
	mov	r4, #0x0
	str     r4, [r5, #MEMC_MCFG1_OFFSET]

	@ memory configuration ddr3
	mov     r4, #0x0
	orr	r4, r4, #0x40000
	orr	r4, r4, #0x20
	orr	r4, r4, #0x1
	str     r4, [r5, #MEMC_MCFG_OFFSET]

	@ move to access state from config
	mov	r4, #0x2
	str	r4, [r5, #MEMC_SCTL_OFFSET]

	@ wait till enter access state
1:
	ldr	r4, [r5, #MEMC_STAT_OFFSET]
	and 	r4, r4, #0xf
	cmp	r4, #0x3
	bne	1b

	@ wait memc idle
1:	ldr	r4, [r5, #MEMC_CSTAT_OFFSET]
	tst	r4, #0x01
	bne	1b

	@ move to low_power from access
	mov	r4, #0x3

	@ Following code has to run from cache since
	@ the RAM is going to self refresh mode

	.align 5
	str	r4, [r5, #MEMC_SCTL_OFFSET]
	@ wait till enter low_power state
1:
	ldr	r4, [r5, #MEMC_STAT_OFFSET]
	and 	r4, r4, #0xf
	cmp	r4, #0x5
	bne	1b

	@ write SLEEPFORCE through rtc iobridge

	str	r1, [r7]
	@ wait rtc io bridge sync
1:
	ldr	r3, [r7]
	tst	r3, #0x01
	bne	1b
	b .
