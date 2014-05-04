/*
 * CSR sirfsoc BLE hardware registers
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __SIRFSOC_BLE_REGS_H
#define __SIRFSOC_BLE_REGS_H

/***************************************************************************
**
** Blt Engine Command Declare Area
****************************************************************************/

/*****************************************
 * Command OP Code define
 * Bit31   Bit30   Bit29  Bit28
 * 0	   0	   0	  0   //SKIP_COMMAND
 * 0	   0	   1	  0   //FENCE_WRITE
 * 1	   0	   0	  0   //FENCE_WRITE With Interrupt Enbled
 * 0	   1	   0	  0   //FENCE_WAIT
 * 0	   1	   1	  0   //SET_REGISTER
 * Others reserved
 *******************************************/

#define OP_SKIP_COMMAND				(0x0)
#define OP_FENCE_WRITE				(0x1)
#define OP_FENCE_WAIT				(0x2)
#define OP_SET_REGISTER				(0x3)
#define OP_FENCE_WRITE_INTERRUPT		(0x4)
/*command register mask
 *set_register */
#define CMD_REG_START_MASK			(0xFF << 0)
#define CMD_REG_FOLLOW_MASK			(0xFF << 8)
#define CMD_REG_HEAD_MASK			(0x7 << 29)
#define CMD_REG_FOLLOW(x)			(((x) & 0xFF) << 8)
#define CMD_REG_HEAD(x)				(((x) & 0x7) << 29)
/* skip */
#define CMD_SKIP_MASK				(0x7FFF << 0)
#define CMD_SKIP_HEAD_MASK			(0x7 << 29)
#define CMD_SKIP_HEAD(x)			(((x) & 0x7) << 29)
/* fence */
#define FENCE_ADDR_MASK				(0x1FFFFFFF << 0)
#define FENCE_ADDR(x)				(((x) >> 3) & 0x1FFFFFFF)
#define FENCE_HEAD_MASK				(7 << 29)
#define FENCE_HEAD(x)				(((x) & 0x7) << 29)

/***************************************************************************
**
** Blt Engine Register Declare Area
****************************************************************************/
#define FB_BASE					(0x00)

#define ENG_STATUS				(0x04)
#define ENG_CTRL				(0x08)

#define RB_OFFSET				(0x0C)
#define RB_LENGTH				(0x10)
#define RB_RD_PTR				(0x14)
#define RB_WR_PTR				(0x18)

#define DST_OFFSET				(0x1C)
#define DST_FORMAT				(0x20)
#define DST_LT					(0x24)
#define DST_RB					(0x28)
#define CLIP_LT					(0x2C)
#define CLIP_RB					(0x30)
#define SRC_OFFSET				(0x34)
#define SRC_FORMAT				(0x38)
#define SRC_LT					(0x3C)
#define SRC_RB					(0x40)
#define PAT_OFFSET				(0x44)

#define FILL_COLOR				(0x48)
#define COLOR_KEY				(0x4C)
#define GBL_ALPHA				(0x50)

#define DRAW_CTL				(0x54)

/* 0x58~0x7c reserved */
#define INTERRUPT_ENABLE			(0x80)
#define INTERRUPT_CLEAR				(0x84)
#define INTERRUPT_STATUS			(0x88)
#define HW_RESERVED				(0x8C)

#define MAX_REG_COUNT				(0x40)
#define BOUNDARY				(0x18)

#define VALID_INTERRUPT_MASK			(0x0000000F)

#define BLT_COMPLETE_INTERRUPT                  (0x00000000)
#define BLT_TIMEOUT_INTERRUPT			(0x00000001)
#define FENCE_INTERRUPT				(0x00000002)
#define CMD_BUF_EMPTY_INTERRUPT			(0x00000003)

/* reg_fb_base */
#define REG_FB_MASK				(0xFFFFFFFF << 0)
/* reg_eng_status */
#define ENG_STATUS_ENABLE_MASK			(0x1 << 0)
#define ENG_STATUS_IDLE_MASK			(0x1 << 1)
#define ENG_STATUS_IDLE(x)			(((x) & 0x2) << 1)
/* reg_eng_ctrl */
#define ENG_CTRL_ENABLE_MASK			(0x1 << 0)
#define ENG_CTRL_IDLE_MASK			(0x1 << 1)
#define ENG_CTRL_IDLE(x)			(((x) & 0x2) << 1)
/* reg_interrupt_clear */
#define INTERRUPT_CLEAR_BUF_MASK		(0x1 << 0)
#define INTERRUPT_CLEAR_FENCE_MASK		(0x1 << 1)
#define INTERRUPT_CLEAR_TIMEOUT_MASK		(0x1 << 2)
#define INTERRUPT_CLEAR_COMPLETE_MASK		(0x1 << 3)
#define INTERRUPT_CLEAR_FENCE(x)		(((x) & 0x1) << 1)
#define INTERRUPT_CLEAR_TIMEOUT(x)		(((x) & 0x1) << 2)
#define INTERRUPT_CLEAR_COMPLETET(x)		(((x) & 0x1) << 3)

/* reg_interrupt_enable */
#define INTERRUPT_ENABLE_BUF_MASK		(0x1 << 0)
#define INTERRUPT_ENABLE_FENCE_MASK		(0x1 << 1)
#define INTERRUPT_ENABLE_TIMEOUT_MASK		(0x1 << 2)
#define INTERRUPT_ENABLE_COMPLETE_MASK		(0x1 << 3)
#define INTERRUPT_ENABLE_FENCE(x)		(((x) & 0x1) << 1)
#define INTERRUPT_ENABLE_TIMEOUT(x)		(((x) & 0x1) << 2)
#define INTERRUPT_ENABLE_COMPLETET(x)		(((x) & 0x1) << 3)
/* reg_interrupt_status */
#define INTERRUPT_STATUS_BUF_MASK		(0x1 << 0)
#define INTERRUPT_STATUS_FENCE_MASK		(0x1 << 1)
#define INTERRUPT_STATUS_TIMEOUT_MASK		(0x1 << 2)
#define INTERRUPT_STATUS_COMPLETE_MASK		(0x1 << 3)
#define INTERRUPT_STATUS_FENCE(x)		(((x) & 0x1) << 1)
#define INTERRUPT_STATUS_TIMEOUT(x)		(((x) & 0x1) << 2)
#define INTERRUPT_STATUS_COMPLETET(x)		(((x) & 0x1) << 3)
#define INTERRUPT_STATUS_OUT_MASK		(0x1 << 0)
/* reg_rb_offset */
#define RB_OFFSET_MASK				(0xFFFFFFFF << 0)
/* reg_rb_length */
#define RB_LENGTH_MASK				(0xFFFF << 0)
/* reg_rb_rd_ptr */
#define RB_RD_OFFSET_MASK			(0xFFFF << 0)
/* reg_pat_offset
 * reg_dst_offset
 * reg_src_offset */
#define PAT_OFFSET_MASK				(0xFFFF << 0)

/* reg_dst_format */
#define DST_FORMAT_STRIDE_MASK			(0x3FFF << 0)
#define DST_FORMAT_FORMAT_MASK			(0xF << 16)
#define DST_FORMAT_FORMAT(x)			(((x) & 0xF) << 16)
/* reg_src_format */
#define SRC_FORMAT_STRIDE_MASK			(0x3FFF << 0)
#define SRC_FORMAT_FORMAT_MASK			(0xF << 16)
#define SRC_FORMAT_NONPREMUL_MASK		(0x1 << 20)
#define SRC_FORMAT_FORMAT(x)			(((x) & 0xF) << 16)
#define SRC_FORMAT_NONPREMUL(x)			(((x) & 0x1) << 20)

/* reg_draw_ctl */
#define DRAW_CTL_ROP3_MASK			(0xFF << 0)
#define DRAW_CTL_COLORFILL_MASK			(0x1 << 16)
#define DRAW_CTL_ALPHA_MASK			(0x3 << 17)
#define DRAW_CTL_FLIPH_MASK			(0x1 << 19)
#define DRAW_CTL_FLIPV_MASK			(0x1 << 20)
#define DRAW_CTL_ROTATION_MASK			(0x3 << 21)
#define DRAW_CTL_CLIP_MASK			(0x1 << 23)
#define DRAW_CTL_TRANSEN_MASK			(0x1 << 24)
#define DRAW_CTL_COLORKEY_MASK			(0x1 << 25)
#define DRAW_CTL_COLORFILL(x)			(((x) & 0x1) << 16)
#define DRAW_CTL_ALPHA(x)			(((x) & 0x3) << 17)
#define DRAW_CTL_FLIPH(x)			(((x) & 0x1) << 19)
#define DRAW_CTL_FLIPV(x)			(((x) & 0x1) << 20)
#define DRAW_CTL_ROTATION(x)			(((x) & 0x3) << 21)
#define DRAW_CTL_CLIP(x)			(((x) & 0x3) << 23)
#define DRAW_CTL_TRANSEN(x)			(((x) & 0x1) << 24)
#define DRAW_CTL_COLORKEY(x)			(((x) & 0x1) << 25)
/* COLOR
 * reg_fillcolor
 * reg_colorkey */
#define COLOR_B_MASK				(0xFF << 0)
#define COLOR_G_MASK				(0xFF << 8)
#define COLOR_R_MASK				(0xFF << 16)
#define COLOR_A_MASK				(0xFF << 24)
#define COLOR_G(x)				(((x) & 0xFF) << 8)
#define COLOR_R(x)				(((x) & 0xFF) << 16)
#define COLOR_A(x)				(((x) & 0xFF) << 24)

/*  reg_gbl_alpha */
#define GBL_ALPHA_MASK				(0xFF << 0)

/* rect_lt
 * reg_dst_lt,reg_src_lt,reg_clip_lt */
#define RECT_LEFT_MASK				(0xFFF << 0)
#define RECT_TOP_MASK				(0xFFF << 16)
#define RECT_TOP(x)				(((x) & 0xFFF) << 16)
/*  rect_rb
 *reg_dst_rb,reg_src_rb,reg_clip_rb */
#define RECT_RIGHT_MASK				(0xFFF << 0)
#define RECT_BOTTOM_MASK			(0xFFF << 16)
#define RECT_BOTTOM(x)				(((x) & 0xFFF) << 16)

#endif
