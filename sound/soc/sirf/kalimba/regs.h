/*
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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

#ifndef _KAS_REGS_H
#define _KAS_REGS_H

#define	KAS_MODE				0x0
#define	KAS_TRANSLATE				0x10
#define	KAS_DMA_MODE				0x14
#define	KAS_DMA_BASE				0x18
#define	KAS_DMA_ADDR				0x1C
#define	KAS_DMA_MODULO				0x20
#define	KAS_DMA_INC				0x24
#define	KAS_DMA_STATUS				0x28
#define	KAS_DMA_CHAIN_LINE			0x2C
#define	KAS_HWBP_SETUP				0x30
#define	KAS_EFUSE				0x34
#define	KAS_TIMER				0x38
#define	KAS_RATE_COUNTER_0			0x50
#define	KAS_RATE_COUNTER_1			0x54
#define	KAS_RATE_COUNTER_2			0x58
#define	KAS_RATE_COUNTER_3			0x5C
#define	KAS_RATE_COUNTER_4			0x60
#define	KAS_RATE_COUNTER_5			0x64
#define	KAS_RATE_COUNTER_ENABLE			0x68
#define	KAS_INTR_BOUNCE				0x84

#define KAS_DMAC_DMA_ADDR			0x400
#define KAS_DMAC_DMA_XLEN			0x404
#define KAS_DMAC_DMA_YLEN			0x408
#define KAS_DMAC_DMA_CTRL			0x40C
#define KAS_DMAC_DMA_WIDTH			0x410
#define KAS_DMAC_DMA_VALID			0x414
#define KAS_DMAC_DMA_INT			0x418
#define KAS_DMAC_DMA_INT_EN			0x41C
#define KAS_DMAC_DMA_LOOP_CTRL			0x420
#define KAS_DMAC_DMA_INT_CNT			0x424
#define KAS_DMAC_DMA_TIMEOUT_CNT		0x428
#define KAS_DMAC_DMA_PAU_TIME_CNT		0x42C
#define KAS_DMAC_DMA_CUR_TABLE_ADDR		0x430
#define KAS_DMAC_DMA_CUR_DATA_ADDR		0x434
#define KAS_DMAC_DMA_MUL			0x438
#define KAS_DMAC_DMA_STATE0			0x43C
#define KAS_DMAC_DMA_STATE1			0x440

#define KAS_CPU_KEYHOLE_ADDR			0x504
#define KAS_CPU_KEYHOLE_DATA			0x508
#define KAS_CPU_KEYHOLE_MODE			0x50C

#define KAS_REGFILE_PC				0xFFFFC0
#define KAS_DEBUG				0xFFFF8C

#define KAS_DEBUG_RUN				(1 << 0)
#define KAS_DEBUG_STOP				(0 << 0)

#define KAS_RESET_DMA_CLIENT			(1 << 0)
#define KAS_DMA_CHAIN_MODE			(1 << 1)

#define KAS_DMAC_IDLE				(1 << 0)

#define KAS_PM_SRAM_BYTES			(256 * 1024)
#define KAS_DM1_SRAM_BYTES			(96 * 1024)
#define KAS_DM2_SRAM_BYTES			KAS_DM1_SRAM_BYTES

#define KAS_PM_SRAM_SIZE			(KAS_PM_SRAM_BYTES / 4)
#define KAS_DM1_SRAM_SIZE			(KAS_DM1_SRAM_BYTES / 4)
#define KAS_DM2_SRAM_SIZE			KAS_DM1_SRAM_SIZE

#define KAS_DM1_SRAM_START_ADDR			0
#define KAS_DM1_SRAM_END_ADDR			(KAS_DM1_SRAM_START_ADDR + \
						KAS_DM1_SRAM_BYTES - 1)
#define KAS_DM2_SRAM_START_ADDR			0xFF3000
#define KAS_DM2_SRAM_END_ADDR			(KAS_DM2_SRAM_START_ADDR + \
						KAS_DM2_SRAM_BYTES - 1)

#define KAS_PM_SRAM_START_ADDR			0
#define KAS_PM_SRAM_END_ADDR			(KAS_PM_SRAM_START_ADDR + \
						KAS_PM_SRAM_BYTES - 1)

#define KAS_TRANSLATE_24BIT_RIGHT_ALIGNED	(0 << 4)
#define KAS_TRANSLATE_24BIT_LEFT_ALIGNED	(1 << 4)
#define KAS_TRANSLATE_2x16BIT			(2 << 4)
#define KAS_TRANSLATE_24BIT			(3 << 4)
#define KAS_TRANSLATE_LEFT_ALIGNED		(0 << 3)
#define KAS_TRANSLATE_RIGHT_ALIGNED		(1 << 3)

#define KAS_DMAC_FINISH_INT			(1 << 0)

#define PM_UNPACKER_PARAMS_DM_SRC(x)		(x)
#define PM_UNPACKER_PARAMS_PM_DEST(x)		(x + 1)
#define PM_UNPACKER_PARAMS_PM_LEN(x)		(x + 2)
#define PM_UNPACKER_PARAMS_IPC_TRGT_LO16(x)	(x + 3)
#define PM_UNPACKER_PARAMS_IPC_TRGT_HI16(x)	(x + 4)
#define PM_UNPACKER_PARAMS_UNPACK_STATUS(x)	(x + 5)

#define KAS_DMAC_TRANS_FIFO_TO_MEM		(0 << 4)
#define KAS_DMAC_TRANS_MEM_TO_FIFO		(1 << 4)

#endif /* _KAS_REGS_H */
