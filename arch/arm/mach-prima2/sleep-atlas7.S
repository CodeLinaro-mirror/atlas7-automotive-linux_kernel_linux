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

#include "pm.h"
#define IPC_TRGT2_INIT0_2 0x0c

	.text

ENTRY(sirfsoc_atlas7_finish_suspend)
	bl v7_flush_kern_cache_all
       ldr     r0, =sirfsoc_pm_ipc_base
       ldr     r5, [r0]
       mov r2, #0x1
       str r2, [r5, #IPC_TRGT2_INIT0_2]

       wfi
loop:
       b loop
