/*
 * arch/arm/mach-prima2/include/mach/debug-macro.S
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#if defined(CONFIG_DEBUG_SIRFPRIMA2_UART1)
#define SIRF_LLUART_PA_BASE          0xb0060000
#elif defined(CONFIG_DEBUG_SIRFATLAS7_UART1)
#define SIRF_LLUART_PA_BASE          0x18020000
#elif defined(CONFIG_DEBUG_SIRFATLAS7_UART0)
#define SIRF_LLUART_PA_BASE          0x18010000
#else
#define SIRF_LLUART_PA_BASE          0
#endif

#define SIRF_LLUART_VA_BASE	(0xFEC00000 \
			| (SIRF_LLUART_PA_BASE & 0x000FFFFF))

#define SIRF_LLUART_TXFIFO_STATUS	0x0114
#define SIRF_LLUART_TXFIFO_DATA	0x0118

#define SIRF_LLUART_TXFIFO_FULL                       (1 << 5)

#ifdef CONFIG_DEBUG_SIRFATLAS7_UART0
#define SIRF_LLUART_TXFIFO_EMPTY			(1 << 8)
#else
#define SIRF_LLUART_TXFIFO_EMPTY			(1 << 6)
#endif


	.macro	addruart, rp, rv, tmp
	ldr	\rp, =SIRF_LLUART_PA_BASE		@ physical
	ldr	\rv, =SIRF_LLUART_VA_BASE		@ virtual
	.endm

	.macro	senduart,rd,rx
	str	\rd, [\rx, #SIRF_LLUART_TXFIFO_DATA]
	.endm

	.macro	busyuart,rd,rx
	.endm

	.macro	waituart,rd,rx
1001:	ldr	\rd, [\rx, #SIRF_LLUART_TXFIFO_STATUS]
	tst	\rd, #SIRF_LLUART_TXFIFO_EMPTY
	beq	1001b
	.endm

