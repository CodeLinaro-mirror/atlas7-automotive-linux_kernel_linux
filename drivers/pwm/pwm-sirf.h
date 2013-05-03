/*
 * SiRF PWM controller registers
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __ASM_ARCH_PWM_H
#define __ASM_ARCH_PWM_H

#define PWM_SELECT_PRECLK		0x0
#define PWM_OE				0x4
#define PWM_ENABLE_PRECLOCK		0x8
#define PWM_ENABLE_POSTCLOCK		0xC
#define PWM_GET_WAIT_OFFSET(n)		(0x10 + 0x8*n)
#define PWM_GET_HOLD_OFFSET(n)		(0x14 + 0x8*n)

#define PWM_TR_STEP(n)			(0x48 + 0x8*n)
#define PWM_STEP_HOLD(n)		(0x4c + 0x8*n)

/* smart backlight*/
#define PWM_WAIT3(n)			(0x80 + 0x8*n)
#define PWM_HOLD3(n)			(0x84 + 0x8*n)

#define PWM_SRC_FIELD_LEN	3

#define BYPASS_MODE_BIT			21
#define TRANS_MODE_SELECT_BIT		7
#define LOOK_TABLE_EN_BIT		14

/*
 * backlight scaling related in lcd controller.
 */
#define LCD_BLS_CTRL1		0x0b00
#define LCD_BLS_CTRL2		0x0b04
#define LCD_BLS_STATUS		0x0b08
#define LCD_CRC_VALUE		0x0b0c
#define LCD_BLS_LEVEL_TB0	0x0b10
#define LCD_BLS_LEVEL_TB1	0x0b14
#define LCD_BLS_LEVEL_TB2	0x0b18
#define LCD_BLS_LEVEL_TB3	0x0b1c

#endif
