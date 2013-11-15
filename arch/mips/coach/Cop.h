/**

2002 ZORAN Corporation, All Rights Reserved THIS IS PROPRIETARY SOURCE CODE OF
ZORAN CORPORATION

*/

#ifndef __COP_H
#define __COP_H
#include <asm/irq.h>

struct irq_reg {
	u32 set;
	u32 reset;
	u32 ier;
	u32 isr;
	u32 irq;
	u32 c16_irq;
};

#define IsCop() (1)
extern void coach_early_console_setup(void);
extern unsigned long coach_get_cpu_hz(void);
extern struct boot_param_header __dtb_start;
extern struct irq_reg *coach_get_irq_base(int);
extern void coach_irq_handle_group(unsigned, unsigned);
extern void __iomem *intc_membase;
extern void prom_puts(const char *);


static inline int clz(unsigned long x)
{
	__asm__(
	"	.set	push\n"
	"	.set	mips32\n"
	"	clz	%0, %1\n"
	"	.set	pop\n"
	: "=r" (x)
	: "r" (x));

	return x;
}

/*
 * Version of ffs that only looks at bits 12..15.
 */
static inline unsigned int irq_ffs(unsigned int pending)
{
	return 32 - clz(pending) - CAUSEB_IP - 1;
}

static inline unsigned int coach_get_irq_group(unsigned int irq)
{
	return (irq - COACH_IRQ_BASE) / 32;
}


static inline unsigned int coach_get_irq_bit(unsigned int irq)
{
	return 1u << (irq % 32);
}

#endif /*__COP_H */

