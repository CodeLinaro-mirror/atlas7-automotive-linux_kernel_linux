/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003, 2004 Chris Dearman
 * Copyright (C) 2005 Ralf Baechle (ralf@linux-mips.org)
 */
#ifndef __ASM_MACH_MIPS_CPU_FEATURE_OVERRIDES_H
#define __ASM_MACH_MIPS_CPU_FEATURE_OVERRIDES_H

/*
 * CPU feature overrides for MIPS boards
 */
#ifdef CONFIG_CPU_MIPS32
#define cpu_has_mips32r2 1
#define cpu_has_tlb		1
#define cpu_has_4kex		1
#define cpu_has_4k_cache	1
#define cpu_has_fpu			1
#define cpu_has_32fpr	0
#define cpu_has_counter		1
/* #define cpu_has_watch	? */
#define cpu_has_divec		0
#define cpu_has_vce		0
/* #define cpu_has_cache_cdex_p	? */
/* #define cpu_has_cache_cdex_s	? */
/* #define cpu_has_prefetch	1 ? */
#define cpu_has_mcheck		0
/* #define cpu_has_ejtag	? */
#define cpu_has_llsc		1
/* #define cpu_has_vtag_icache	? */
/* #define cpu_has_dc_aliases	? */
/* #define cpu_has_ic_fills_f_dc ? */
#define cpu_has_nofpuex		0
/* #define cpu_has_64bits	? */
/* #define cpu_has_64bit_zero_reg ? */
/* #define cpu_has_inclusive_pcaches ? */

#define cpu_icache_snoops_remote_store 0 /* chagned from 1 */
/*#define cpu_has_inclusive_pcaches   0*/
#define cpu_dcache_line_size()        32
#define cpu_icache_line_size()        32

/* temporary disabling features for c16 bring up */
#define cpu_has_mipsmt	1
#define cpu_has_veic	0
#define cpu_has_vint	0

#endif


#ifdef CONFIG_CPU_MIPS64
#error MIPS64 is not supported
#define cpu_has_tlb		1
#define cpu_has_4kex		1
#define cpu_has_4k_cache	1
/* #define cpu_has_fpu		? */
/* #define cpu_has_32fpr:	? */
#define cpu_has_counter		1
/* #define cpu_has_watch	? */
#define cpu_has_divec		1
#define cpu_has_vce		0
/* #define cpu_has_cache_cdex_p	? */
/* #define cpu_has_cache_cdex_s	? */
/* #define cpu_has_prefetch	? */
#define cpu_has_mcheck		1
/* #define cpu_has_ejtag	? */
#define cpu_has_llsc		1
/* #define cpu_has_vtag_icache	? */
/* #define cpu_has_dc_aliases	? */
/* #define cpu_has_ic_fills_f_dc ? */
#define cpu_has_nofpuex		0
/* #define cpu_has_64bits	? */
/* #define cpu_has_64bit_zero_reg ? */
/* #define cpu_has_inclusive_pcaches ? */
#define cpu_icache_snoops_remote_store 1
#endif


#endif /* __ASM_MACH_MIPS_CPU_FEATURE_OVERRIDES_H */
