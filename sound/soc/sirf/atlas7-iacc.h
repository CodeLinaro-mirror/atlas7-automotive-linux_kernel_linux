/*
 * Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
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

#ifndef _ATLAS7_IACC_H
#define _ATLAS7_IACC_H

#define INTCODECCTL_MODE_CTRL			0x0
#define INTCODECCTL_TX_RX_EN			0x04

#define INTCODECCTL_TXFIFO0_OP			0x40
#define INTCODECCTL_TXFIFO0_LEV_CHK		0x44
#define INTCODECCTL_TXFIFO0_STS			0X48
#define INTCODECCTL_TXFIFO0_INT			0x4C
#define INTCODECCTL_TXFIFO0_INT_MSK		0x50

#define INTCODECCTL_TXFIFO1_OP			0x54
#define INTCODECCTL_TXFIFO1_LEV_CHK		0x58
#define INTCODECCTL_TXFIFO1_STS			0X5C
#define INTCODECCTL_TXFIFO1_INT			0x60
#define INTCODECCTL_TXFIFO1_INT_MSK		0x64

#define INTCODECCTL_TXFIFO2_OP			0x68
#define INTCODECCTL_TXFIFO2_LEV_CHK		0x6C
#define INTCODECCTL_TXFIFO2_STS			0X70
#define INTCODECCTL_TXFIFO2_INT			0x74
#define INTCODECCTL_TXFIFO2_INT_MSK		0x78

#define INTCODECCTL_TXFIFO3_OP			0x7C
#define INTCODECCTL_TXFIFO3_LEV_CHK		0x80
#define INTCODECCTL_TXFIFO3_STS			0X84
#define INTCODECCTL_TXFIFO3_INT			0x88
#define INTCODECCTL_TXFIFO3_INT_MSK		0x78

#define INTCODECCTL_RXFIFO0_OP			0xB8
#define INTCODECCTL_RXFIFO0_LEV_CHK		0xBC
#define INTCODECCTL_RXFIFO0_STS			0XC0
#define INTCODECCTL_RXFIFO0_INT			0xC4
#define INTCODECCTL_RXFIFO0_INT_MSK		0xC8

#define INTCODECCTL_RXFIFO1_OP			0xCC
#define INTCODECCTL_RXFIFO1_LEV_CHK		0xD0
#define INTCODECCTL_RXFIFO1_STS			0XD4
#define INTCODECCTL_RXFIFO1_INT			0xD8
#define INTCODECCTL_RXFIFO1_INT_MSK		0xDC

#define INTCODECCTL_RXFIFO2_OP			0xE0
#define INTCODECCTL_RXFIFO2_LEV_CHK		0xE4
#define INTCODECCTL_RXFIFO2_STS			0XE8
#define INTCODECCTL_RXFIFO2_INT			0xEC
#define INTCODECCTL_RXFIFO2_INT_MSK		0xF0

#define TX_24BIT				1
#define RX_24BIT				(1 << 1)
#define RX0_24BIT				(1 << 1)
#define TX_START_SYNC_EN			(1 << 2)
#define TX_SYNC_EN				(1 << 3)
#define RX1_24BIT				(1 << 4)
#define RX2_24BIT				(1 << 5)
#define RX3_24BIT				(1 << 6)
#define RX_SYNC_TIMEOUT_BIT_MASK		(0xFFF << 16)

#define DAC_EN					1
#define DAC0_EN					1
#define DAC1_EN					(1 << 1)
#define DAC2_EN					(1 << 2)
#define DAC3_EN					(1 << 3)

#define ADC_EN					(1 << 8)
#define ADC0_EN					(1 << 8)
#define ADC1_EN					(1 << 9)
#define ADC2_EN					(1 << 10)
#define ADC3_EN					(1 << 11)

#define RX_DMA_CTRL_MASK			(0xF << 8)
#define RX_DMA_CTRL_SHIFT			8

#define RX_DMA_SYNC_EN_MASK			(1 << 4)
#define RX_DMA_SYNC_EN_SHIFT			4

#define FIFO_START				1
#define FIFO_RESET				(1 << 1)

#define FIFO_LEV_CHECK_STOP_MASK		0x1F
#define FIFO_LEV_CHECK_LOW_MASK			(0x1F << 10)
#define FIFO_LEV_CHECK_HIGH_MASK		(0x1F << 20)

#endif /* _ATLAS7_IACC_H */
