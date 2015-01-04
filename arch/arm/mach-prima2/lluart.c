/*
 * Static memory mapping for DEBUG_LL
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/kernel.h>
#include <asm/page.h>
#include <asm/mach/map.h>
#include "common.h"

#if defined(CONFIG_DEBUG_SIRFPRIMA2_UART1)
#define SIRF_LLUART_PA_BASE	0xb0060000
#elif defined(CONFIG_DEBUG_SIRFATLAS7_UART1)
#define SIRF_LLUART_PA_BASE	0x18020000
#elif defined(CONFIG_DEBUG_SIRFATLAS7_UART0)
#define SIRF_LLUART_PA_BASE	0x18010000
#else
#define SIRF_LLUART_PA_BASE	0
#endif

#define SIRF_LLUART_VA_BASE	SIRFSOC_VA(SIRF_LLUART_PA_BASE & 0x000FFFFF)
#define SIRF_LLUART_SIZE		SZ_4K

void __init sirfsoc_map_lluart(void)
{
	struct map_desc sirfsoc_lluart_map = {
		.virtual        = SIRF_LLUART_VA_BASE,
		.pfn            = __phys_to_pfn(SIRF_LLUART_PA_BASE),
		.length         = SIRF_LLUART_SIZE,
		.type           = MT_DEVICE,
	};
	iotable_init(&sirfsoc_lluart_map, 1);
}
