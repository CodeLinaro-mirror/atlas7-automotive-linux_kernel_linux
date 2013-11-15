/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2010 by A.Leo.
 */
#ifndef __ASM_MACH_COACH14_IRQ_H
#define __ASM_MACH_COACH14_IRQ_H


#define COACH_INT_ID_LAST  453
#define COACH_IRQ_BASE     32
#define COACH_VIRT_IRQ_NUM 255

#define COACH_VIRT_IRQ_OFFSET COACH_IRQ_NUM(COACH_INT_ID_LAST)

#define NR_IRQS (512 + COACH_VIRT_IRQ_NUM)

/* Convert from CPU_INT_ID_* to kernel IRQ number. */
#define COACH_IRQ_NUM(x) ((x) + COACH_IRQ_BASE)

#include_next <irq.h>

#endif /* __ASM_MACH_COACH14_IRQ_H */
