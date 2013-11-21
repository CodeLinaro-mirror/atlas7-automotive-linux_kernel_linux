/*
 *	include/asm-mips/mach-generic/ioremap.h
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */
#ifndef __ASM_MACH_COACH10_IOREMAP_H
#define __ASM_MACH_COACH10_IOREMAP_H

#include <linux/types.h>

#define COACH_IOMEM_START 0xB0000000ul
#define COACH_IOMEM_END   0xBF100000ul
#define COACH_PHYSMEM_ALIAS_START KSEG2
#define COACH_PHYSMEM_UPPER_ALIAS_START (KSEG3 + 0x10000000)

/*
 * Allow physical addresses to be fixed up to help peripherals located
 * outside the low 32-bit range -- generic pass-through version.
 */
static inline phys_t fixup_bigphys_addr(phys_t phys_addr, phys_t size)
{
	return phys_addr;
}

static inline void __iomem *plat_ioremap(phys_t offset, unsigned long size,
		unsigned long flags)
{
	if (offset >= COACH_IOMEM_START && (offset + size) <= COACH_IOMEM_END)
		return (void __iomem *)offset;

	return NULL;
}

static inline int plat_iounmap(const volatile void __iomem *addr)
{
	return ((unsigned long)addr >= COACH_IOMEM_START &&
		(unsigned long)addr <= COACH_IOMEM_END);
}

#endif /* __ASM_MACH_GENERIC_IOREMAP_H */
