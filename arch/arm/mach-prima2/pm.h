/*
 * arch/arm/mach-prima2/pm.h
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef _MACH_PRIMA2_PM_H_
#define _MACH_PRIMA2_PM_H_

#define SIRFSOC_PWR_SLEEPFORCE		0x01

#define SIRFSOC_SLEEP_MODE_MASK         0x3
#define SIRFSOC_HIBERNATION_MODE	0x0
#define SIRFSOC_DEEP_SLEEP_MODE         0x1

#define SIRFSOC_PWRC_PDN_CTRL           0x0
#define SIRFSOC_BOOT_STATUS		0x20
#define SIRFSOC_PWRC_SCRATCH_PAD1       0x18
#define SIRFSOC_PWRC_SCRATCH_PAD8       0x1C
#define SIRFSOC_PWRC_SCRATCH_PAD11       0x28

#define SIRFSOC_START_PSAVING_BIT	0x0
#define SIRFSOC_BOOT_STATUS_BITS	6
#define RECOVERY_MODE			(1 << 4)

#ifndef __ASSEMBLY__
extern int sirfsoc_prima2_finish_suspend(unsigned long);
extern int sirfsoc_atlas7_finish_suspend(unsigned long);
extern int sirfsoc_pre_suspend_power_off(void);
extern void sirfsoc_pm_enter_power_saving(void);
extern int sirfsoc_pwrc_init(void);
extern void sirfsoc_atlas7_restart(enum reboot_mode mode, const char *cmd);

#endif

#endif

