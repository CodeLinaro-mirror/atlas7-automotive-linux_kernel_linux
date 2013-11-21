/**

2002 ZORAN Corporation, All Rights Reserved THIS IS PROPRIETARY SOURCE CODE OF
ZORAN CORPORATION

*/

#ifndef __COP_H
#define __COP_H

#define IsCop() (1)
extern void coach_early_console_setup(void);
extern void coach_disable_irqs(void);
extern void arch_init_irq_(void);
extern unsigned long coach_get_cpu_hz(void);
extern struct boot_param_header __dtb_start;

#endif /*__COP_H */

