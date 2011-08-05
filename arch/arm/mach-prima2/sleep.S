/*
 * sleep mode for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/linkage.h>
#include <asm/ptrace.h>
#include <asm/assembler.h>
#include <asm/hardware/cache-l2x0.h>

#include "pm.h"

#define DENALI_CTL_22_OFF	0x58
#define DENALI_CTL_112_OFF	0x1c0

ENTRY(sirfsoc_get_wakeup_pointer)
	stmfd	sp!, {lr}	@ save registers on stack
	adr	r0, sirfsoc_wakeup
	ldmfd	sp!, {pc}	@ restore regs and return

ENTRY(sirfsoc_sleep)
sirfsoc_sleep:
	stmdb  sp!, {r0-r12, lr}		@ Push SVC state onto our stack

	mrc     p15, 0, r2, c1, c0, 1           @ cp15 c1 auxiliary Control register
	mrc     p15, 0, r3, c1, c0, 2           @ cp15 c1 coprocessor access control register
	mrc     p15, 0, r4, c2, c0, 0           @ cp15 c2_r0 translation table base resigter 0
	mrc     p15, 0, r5, c2, c0, 1          	@ cp15 c2_r1 translation table base resigter 1

	mrc     p15, 0, r6, c2, c0, 2          	@ cp15 c2_r2 translation table base control resigter

	mrc     p15, 0, r7, c3, c0, 0           @ load r2 with domain access control.
	mrc     p15, 0, r8, c1, c0, 0           @ MMU Control
	mrc	p15, 0, r9, c10,c2, 0		@ cp15 PRRR register
	mrc     p15, 0, r10, c10,c2, 1		@ cp15 NMRR register
	mov	r11, sp
	ldr	r0, =save_context
	add	r0, r0, #124
	stmdb	r0!, {r2-r11}			@ Save CP15 and the SP to stack

	mov     r1, #FIQ_MODE|PSR_I_BIT|PSR_F_BIT 	@ Enter FIQ mode, no interrupts
	msr     cpsr, r1
	mrs     r2, spsr
	stmdb   r0!, {r2, r8-r12, sp, lr}      	@ store the FIQ Mode Registers

	mov     r1, #ABT_MODE|PSR_I_BIT|PSR_F_BIT 	@ Enter ABT mode, no interrupts
	msr     cpsr, r1
	mrs     r2, spsr
	stmdb   r0!, {r2, sp, lr}               @ store the ABT Mode Registers

	mov     r1, #IRQ_MODE|PSR_I_BIT|PSR_F_BIT 	@ Enter IRQ mode, no interrupts
	msr     cpsr, r1
	mrs     r2, spsr
	stmdb   r0!, {r2, sp, lr}               @ store the IRQ Mode Registers

	mov     r1, #UND_MODE|PSR_I_BIT|PSR_F_BIT 	@ Enter UND mode, no interrupts
	msr     cpsr, r1
	mrs     r2, spsr
	stmdb   r0!, {r2, sp, lr}               @ store the UND Mode Registers

	mov     r1, #SYSTEM_MODE|PSR_I_BIT|PSR_F_BIT 	@ Enter SYS mode, no interrupts
	msr     cpsr, r1
	mrs     r2, spsr
	stmdb   r0!, {r2, sp, lr}               @ store the SYS Mode Registers

	mov     r1, #SVC_MODE|PSR_I_BIT|PSR_F_BIT @ Enter SVC mode, no interrupts
	msr	cpsr, r1

	ldr     r1, =save_context                  @ Get address of label save_sp in r1
	sub	r0, r0, r1
	str     r0, [r1]                      @ Store context offset to label memory

	bl      sirfsoc_pre_suspend_power_off
	cmp     r0,#0
	bne     sirfsoc_sleep_exit

	@ r5: 	mem controller
	ldr     r0, =sirfsoc_memc_base
	ldr	r5, [r0]
	@ r7: 	rtc iobrg controller
	ldr     r0, =sirfsoc_rtciobrg_base
	ldr	r7, [r0]

	@ Clean and invalidate all caches
	bl v7_flush_kern_cache_all

#ifdef CONFIG_CACHE_L2X0
	ldr	r1, =sirfsoc_l2x_base
	ldr     r0, [r1]
	ldr	r1, =L2X0_CLEAN_INV_WAY
	mov	r2, #0xff
	str	r2, [r0,r1]
	mov	r2, #0
1:
	ldr	r3, [r0,r1]
	cmp	r2,r3
	bne	1b
	ldr	r1, =L2X0_CACHE_SYNC
	mov	r2, #0
	str	r2, [r0,r1]

	ldr	r1, =L2X0_CTRL
	mov	r2, #0
	str	r2, [r0,r1]
#endif

	@ Read the power control register and set the
	@ sleep force bit.
	ldr	r0, =SIRFSOC_PWRC_PDN_CTRL
	bl	__sirfsoc_rtc_iobrg_readl
	orr	r0,r0,#SIRFSOC_PWR_SLEEPFORCE
	ldr	r1, =SIRFSOC_PWRC_PDN_CTRL
	bl	sirfsoc_rtc_iobrg_pre_writel
	mov	r1, #0x1

	@ The following code is arranged in such a way
	@ that the code to be run after memory is put
	@ into self refresh fits in a cache line (32 bytes).
	@ This is done to make sure the code runs from
	@ cache after memory is put into self refresh.

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

	@ bootloader will jump here on wakeup

	.align 5
.globl sirfsoc_wakeup
sirfsoc_wakeup:

	mov r4, #0
        mcr p15,0,r4,c8,c7,0            @invalid all unified tlb
        mcr p15,0,r4,c8,c6,0            @invalid all data tlb
        mcr p15,0,r4,c8,c5,0            @invalid all instruction tlb
        mcr p15,0,r4,c7,c5,4            @flush prefetch buffer
        mcr p15,0,r4,c7,c10,4           @Drain write buffer
	mcr p15,0,r4,c7,c5,0		@invalid instruction cache

	mov	r0, #PSR_I_BIT | PSR_F_BIT | SVC_MODE
	msr	cpsr, r0			@ set SVC, irqs off

	@ Get save_context phys address
	ldr	r1, =save_context
	ldr	r3, =(.+12)
	sub	r3, r3, pc
	sub	r1, r1, r3
	ldr	r0, [r1]

	add	r0, r0, r1

	mov     r1, #SYSTEM_MODE|PSR_I_BIT|PSR_F_BIT 	@ Enter SYS mode, no interrupts
	msr     cpsr, r1
	ldmfd   r0!, {r2, sp, lr}               @ store the SYS Mode Registers
	msr     spsr, r2

	mov     r1, #UND_MODE|PSR_I_BIT|PSR_F_BIT 	@ Enter UND mode, no interrupts
	msr     cpsr, r1
	ldmfd   r0!, {r2, sp, lr}               @ store the UND Mode Registers
	msr     spsr, r2

	mov     r1, #IRQ_MODE|PSR_I_BIT|PSR_F_BIT 	@ Enter IRQ mode, no interrupts
	msr     cpsr, r1
	ldmfd   r0!, {r2, sp, lr}               @ store the IRQ Mode Registers
	msr     spsr, r2

	mov     r1, #ABT_MODE|PSR_I_BIT|PSR_F_BIT 	@ Enter ABT mode, no interrupts
	msr     cpsr, r1
	ldmfd   r0!, {r2, sp, lr}               @ store the ABT Mode Registers
	msr     spsr, r2

	mov     r1, #FIQ_MODE|PSR_I_BIT|PSR_F_BIT 	@ Enter FIQ mode, no interrupts
	msr     cpsr, r1
	ldmfd   r0!, {r2, r8-r12,sp, lr}     	@ store the FIQ Mode Registers
	msr     spsr, r2

	mov     r1, #SVC_MODE|PSR_I_BIT|PSR_F_BIT 	@ Enter SVC mode, no interrupts
	msr	cpsr, r1

	ldr	r1, =sirfsoc_sleep_exit		@ its absolute virtual address
	ldmfd	r0, {r2 - r10, sp}		@ CP regs + virt stack ptr

	mcr     p15, 0, r7, c3, c0, 0           @ load r2 with domain access control.
	mcr     p15, 0, r6, c2, c0, 2          	@ cp15 c2_r2 translation table base control resigter
	mcr     p15, 0, r4, c2, c0, 0           @ cp15 c2_r0 translation table base resigter 0
	mcr     p15, 0, r5, c2, c0, 1          	@ cp15 c2_r1 translation table base resigter 1

	mcr     p15, 0, r3, c1, c0, 2           @ cp15 c1 coprocessor access control register
	mcr     p15, 0, r2, c1, c0, 1           @ cp15 c1 auxiliary Control register
	mcr	p15, 0, r10, c10,c2, 1		@ cp15 NMRR register
	mcr	p15, 0, r9, C10,c2, 0		@ cp15 PRRR register
	b	resume_turn_on_mmu		@ cache align execution

	.align 5
resume_turn_on_mmu:
	mcr     p15, 0, r8, c1, c0, 0           @ MMU Control
	nop
	mov	pc, r1				@ jump to virtual addr
	nop
	nop
	nop

sirfsoc_sleep_exit:
	ldmfd	sp!, {r0 - r12, pc}		@ return to caller
	/* back to caller of sleep */
	nop
	nop
	nop
@	.data
	.align 5
save_context:
	.space 128,0

