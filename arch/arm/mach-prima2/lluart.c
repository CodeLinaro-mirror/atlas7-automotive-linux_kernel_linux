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

#define SIRF_LLUART_SIZE               SZ_4K

void __init sirfsoc_map_lluart(void)
{
	struct map_desc sirfsoc_lluart_map = {
		.virtual        = CONFIG_DEBUG_UART_VIRT,
		.pfn            = __phys_to_pfn(CONFIG_DEBUG_UART_PHYS),
		.length         = SIRF_LLUART_SIZE,
		.type           = MT_DEVICE,
	};
	iotable_init(&sirfsoc_lluart_map, 1);
}
