/*
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __SIRFSOC_TS_H__
#define __SIRFSOC_TS_H__

#define PWR_WAKEEN_TSC_SHIFT 23
#define PWR_WAKEEN_TS_SHIFT 5
#define SIRFSOC_PWRC_TRIGGER_EN 0x8
#define SIRFSOC_PWRC_BASE 0x3000

extern int ts_linear_scale(int *x, int *y, int swap_xy);

#endif                         /* __SIRFSOC_TS_H__ */
