/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2013 by Artem Leonenko
 */
#ifndef __ASM_MACH_COACH16_IRQ_H
#define __ASM_MACH_COACH16_IRQ_H


#define COACH_IRQ_BASE     32
#define COACH_VIRT_IRQ_NUM 255
#define COACH_INT_ID_LAST 897

#define NR_IRQS (COACH_INT_ID_LAST + COACH_IRQ_BASE)

/* Convert from CPU_INT_ID_* to kernel IRQ number. */
#define COACH_IRQ_NUM(x) ((x) + COACH_IRQ_BASE)

#include_next <irq.h>

#endif /* __ASM_MACH_COACH16_IRQ_H */
