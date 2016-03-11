/*
 * Virtio-based remote processor clock controller
 *
 * Copyright (c) 2014, 2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _VIRTIO_CLK_H_
#define _VIRTIO_CLK_H_


#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>

/* Define the mmio offset of virtio clock */
#define VCLK_MMIO_UNIT_NUM	MMIO_CONFIG_BASE
#define VCLK_MMIO_UNIT_LEN	(MMIO_CONFIG_BASE + 0x04)
#define VCLK_MMIO_UNIT_DATA	(MMIO_CONFIG_BASE + 0x08)

/* Define the opeation codes of virtio clock */
#define VIRT_CLK_PREPARE		0x1000
#define VIRT_CLK_UNPREPARE		0x1001
#define VIRT_CLK_IS_PREPARED		0x1002
#define VIRT_CLK_UNPREPARE_UNUSED	0x1003
#define VIRT_CLK_ENABLE			0x1004
#define VIRT_CLK_DISABLE		0x1005
#define VIRT_CLK_IS_ENABLED		0x1006
#define VIRT_CLK_DISABLE_UNUSED		0x1007
#define VIRT_CLK_RECALC_RATE		0x1008
#define VIRT_CLK_ROUND_RATE		0x1009
#define VIRT_CLK_DETERMINE_RATE		0x100A
#define VIRT_CLK_SET_PARENT		0x100B
#define VIRT_CLK_GET_PARENT		0x100C
#define VIRT_CLK_SET_RATE		0x100D
#define VIRT_CLK_SET_RATE_AND_PARENT	0x100E
#define VIRT_CLK_RECALC_ACCURACY	0x100F
#define VIRT_CLK_INIT			0x1010
#define VIRT_CLK_DEBUG_INIT		0x1011

/* definition of clock hardware characteristics */
#define VIRTIO_CLK_F_PREPARE		0x00
#define VIRTIO_CLK_F_UNPREPARE		0x01
#define VIRTIO_CLK_F_IS_PREPARED	0x02
#define VIRTIO_CLK_F_UNPREPARE_UNUSED	0x03
#define VIRTIO_CLK_F_ENABLE		0x04
#define VIRTIO_CLK_F_DISABLE		0x05
#define VIRTIO_CLK_F_IS_ENABLED		0x06
#define VIRTIO_CLK_F_DISABLE_UNUSED	0x07
#define VIRTIO_CLK_F_RECALC_RATE	0x08
#define VIRTIO_CLK_F_ROUND_RATE		0x09
#define VIRTIO_CLK_F_DETERMINE_RATE	0x0A
#define VIRTIO_CLK_F_SET_RATE		0x0B
#define VIRTIO_CLK_F_SET_PARENT		0x0C
#define VIRTIO_CLK_F_GET_PARENT		0x0D
#define VIRTIO_CLK_F_RECALC_ACCURACY	0x0E
#define VIRTIO_CLK_F_INIT		0x0F

/* data structure of virtio clock request */
struct virtio_clk_req {
	u32 clk_index;
	int clk_op_code;
	int status;
	union {
		bool enabled;
		bool prepared;
		unsigned long rate;
		unsigned long accuracy;
		unsigned long parent_rate;
		unsigned long parent_accuracy;
		u8 parent;
		unsigned long raw[4];
	} data;
};

#endif /* _VIRTIO_I2C_H_ */
