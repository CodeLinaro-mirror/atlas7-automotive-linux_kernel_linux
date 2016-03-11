/*
 * sleep mode for Atlas7
 *
 * Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
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
#define IPC_TRGT2_INIT1_2 0x10c

	.text

ENTRY(sirfsoc_atlas7_finish_suspend)
	bl v7_flush_kern_cache_all
       ldr     r0, =sirfsoc_pm_ipc_base
       ldr     r5, [r0]
       mov r2, #0x1
       str r2, [r5, #IPC_TRGT2_INIT1_2]

       wfi
loop:
       b loop
