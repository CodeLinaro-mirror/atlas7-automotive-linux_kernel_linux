/*
 * CSR sirfsoc BLE internal definitions
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __SIRFSOC_BLE_DEFS_H
#define __SIRFSOC_BLE_DEFS_H

#include <linux/io.h>
#include "ble_regs.h"
#include "vdss_ble.h"

static inline u32 set_ble_register(u32 start_offset, u32 num_reg)
{
	u32 tmp = (start_offset & CMD_REG_START_MASK) |
	    CMD_REG_FOLLOW(num_reg) | CMD_REG_HEAD(OP_SET_REGISTER);
	return tmp;
}

static inline u32 ble_read_reg(struct ble_context *dcontext, u32 offset)
{
	return readl(dcontext->ble_reg_base + offset);
}

static inline void ble_write_reg(struct ble_context *dcontext,
				 u32 offset, u32 value)
{
	writel(value, dcontext->ble_reg_base + offset);
}

struct ble_registers {
	u32 reg_fb_base;
	u32 reg_eng_status;
	u32 reg_eng_ctrl;

	u32 reg_rb_base;
	u32 reg_rb_length;
	u32 reg_rb_read;
	u32 reg_rb_write;

	u32 reg_dst_offset;
	u32 reg_dst_format;
	u32 reg_dst_lt;
	u32 reg_dst_rb;
	u32 reg_clip_lt;
	u32 reg_clip_rb;

	u32 reg_src_offset;
	u32 reg_src_format;
	u32 reg_src_lt;
	u32 reg_src_rb;

	u32 reg_pat_offset;

	u32 reg_fill_color;
	u32 reg_color_key;
	u32 reg_gbl_alpha;

	u32 reg_draw_ctrl;
	u32 reg_intr_clear;
	u32 reg_intr_enable;
	u32 reg_intr_status;
};

#ifndef BLE_DEBUG
#define BLE_DEBUG 0
#endif
#define BLEOUTPUTMSG(fmt, args...) pr_err("BLE: " fmt, ##args)

#if BLE_DEBUG
#define BLE_MSG(X)  (BLEOUTPUTMSG X)
#else
#define BLE_MSG(X)
#endif
#define BLE_ERR(X)  (BLEOUTPUTMSG X)

#endif
