/*
 * linux/arch/arm/mach-prima2/include/mach/prima2_pinmux.h
 *
 * CSR SiRFprima2 Pin Mux Description File
 *
 * Copyright (C) 2010 by SiRF Technology, Inc., All Rights Reserved.
 * Author: Binghua Duan <Binghua.Duan@csr.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ASM_ARCH_PRIMA2_PINMUX_H__
#define __ASM_ARCH_PRIMA2_PINMUX_H__

extern void __iomem *sirfsoc_gpio_pinmux_base;

void sirfsoc_get_gpios(int group, u32 bitmask);
void sirfsoc_get_gpio(int group, int bitno);
void sirfsoc_put_gpios(int group, u32 bitmask);
void sirfsoc_put_gpio(int group, int bitno);

void sirfsoc_pad_get(const char *name);
void sirfsoc_pad_put(const char *name);
#endif /* __ASM_ARCH_PRIMA2_PINMUX_H__ */

