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

#ifndef _SIRF_I2S_H
#define _SIRF_I2S_H

#define AUDIO_CTRL_TX_FIFO_LEVEL_CHECK_MASK		0x3F
#define AUDIO_CTRL_TX_FIFO_LEVEL_CHECK_MASK_ATLAS7	0x7F
#define AUDIO_CTRL_TX_FIFO_SC_OFFSET    0
#define AUDIO_CTRL_TX_FIFO_LC_OFFSET    10
#define AUDIO_CTRL_TX_FIFO_HC_OFFSET    20

#define TX_FIFO_SC(x)           ((x) << AUDIO_CTRL_TX_FIFO_SC_OFFSET)
#define TX_FIFO_LC(x)           ((x) << AUDIO_CTRL_TX_FIFO_LC_OFFSET)
#define TX_FIFO_HC(x)           ((x) << AUDIO_CTRL_TX_FIFO_HC_OFFSET)

#define AUDIO_CTRL_RX_FIFO_LEVEL_CHECK_MASK		0x0F
#define AUDIO_CTRL_RX_FIFO_LEVEL_CHECK_MASK_ATLAS7	0x1F
#define AUDIO_CTRL_RX_FIFO_SC_OFFSET    0
#define AUDIO_CTRL_RX_FIFO_LC_OFFSET    10
#define AUDIO_CTRL_RX_FIFO_HC_OFFSET    20

#define RX_FIFO_SC(x)           (((x) & AUDIO_CTRL_RX_FIFO_LEVEL_CHECK_MASK) \
				<< AUDIO_CTRL_RX_FIFO_SC_OFFSET)
#define RX_FIFO_LC(x)           (((x) & AUDIO_CTRL_RX_FIFO_LEVEL_CHECK_MASK) \
				<< AUDIO_CTRL_RX_FIFO_LC_OFFSET)
#define RX_FIFO_HC(x)           (((x) & AUDIO_CTRL_RX_FIFO_LEVEL_CHECK_MASK) \
				<< AUDIO_CTRL_RX_FIFO_HC_OFFSET)

#define AUDIO_CTRL_MODE_SEL			(0x0000)
#define AUDIO_CTRL_I2S_CTRL			(0x0020)
#define AUDIO_CTRL_I2S_TX_RX_EN			(0x0024)
#define AUDIO_CTRL_I2S_TDM_CTRL			(0x0030)

#define AUDIO_CTRL_I2S_TXFIFO_OP		(0x0040)
#define AUDIO_CTRL_I2S_TXFIFO_LEV_CHK		(0x0044)
#define AUDIO_CTRL_I2S_TXFIFO_STS		(0x0048)
#define AUDIO_CTRL_I2S_TXFIFO_INT		(0x004C)
#define AUDIO_CTRL_I2S_TXFIFO_INT_MSK		(0x0050)

#define AUDIO_CTRL_I2S_RXFIFO_OP		(0x00B8)
#define AUDIO_CTRL_I2S_RXFIFO_LEV_CHK		(0x00BC)
#define AUDIO_CTRL_I2S_RXFIFO_STS		(0x00C0)
#define AUDIO_CTRL_I2S_RXFIFO_INT		(0x00C4)
#define AUDIO_CTRL_I2S_RXFIFO_INT_MSK		(0x00C8)

#define I2S_MODE				(1<<0)

#define I2S_LOOP_BACK				(1<<3)
#define	I2S_MCLK_DIV_SHIFT			15
#define I2S_MCLK_DIV_MASK			(0x1FF<<I2S_MCLK_DIV_SHIFT)
#define I2S_BITCLK_DIV_SHIFT			24
#define I2S_BITCLK_DIV_MASK			(0xFF<<I2S_BITCLK_DIV_SHIFT)

#define I2S_MCLK_EN				(1<<2)
#define I2S_REF_CLK_SEL_EXT			(1<<3)
#define I2S_DOUT_OE				(1<<4)
#define I2S_TX_24BIT_ATLAS7			(1<<5)
#define I2S_RX_24BIT_ATLAS7			(1<<6)
#define i2s_R2X_LP_TO_TX0			(1<<30)
#define i2s_R2X_LP_TO_TX1			(2<<30)
#define i2s_R2X_LP_TO_TX2			(3<<30)

#define AUDIO_FIFO_START		(1 << 0)
#define AUDIO_FIFO_RESET		(1 << 1)

#define AUDIO_FIFO_FULL			(1 << 0)
#define AUDIO_FIFO_EMPTY		(1 << 1)
#define AUDIO_FIFO_OFLOW		(1 << 2)
#define AUDIO_FIFO_UFLOW		(1 << 3)

#define I2S_RX_ENABLE			(1 << 0)
#define I2S_TX_ENABLE			(1 << 1)

/* Codec I2S Control Register defines */
#define I2S_SLAVE_MODE			(1 << 0)
#define I2S_SIX_CHANNELS		(1 << 1)
#define I2S_L_CHAN_LEN_SHIFT		(4)
#define I2S_L_CHAN_LEN_MASK		(0x1f << I2S_L_CHAN_LEN_SHIFT)
#define I2S_FRAME_LEN_SHIFT		(9)
#define I2S_FRAME_LEN_MASK		(0x3f << I2S_FRAME_LEN_SHIFT)

/* TDM control */
#define I2S_TDM_ENA			1
/* Frame sync mode */
#define I2S_TDM_FRAME_SYNC_I2S		(0 << 1)
#define I2S_TDM_FRAME_SYNC_DSP0		(1 << 1)
#define I2S_TDM_FRAME_SYNC_DSP1		(2 << 1)
/* Frame start sync polarity */
#define I2S_TDM_FRAME_POLARITY_HIGH	(0 << 3)
#define I2S_TDM_FRAME_POLARITY_LOW	(1 << 3)
/* Word alignment, record */
#define I2S_TDM_WORD_ALIGN_RX_LEFT_J	(0 << 4)
#define I2S_TDM_WORD_ALIGN_RX_I2S0	(1 << 4)
#define I2S_TDM_WORD_ALIGN_RX_I2S1	(2 << 4)
/* Bit alignment in one word, record */
#define I2S_TDM_DATA_ALIGN_RX_LEFT_J	(0 << 6)
#define I2S_TDM_DATA_ALIGN_RX_I2S	(1 << 6)
/* Word alignment, playback */
#define I2S_TDM_WORD_ALIGN_TX_LEFT_J	(0 << 7)
#define I2S_TDM_WORD_ALIGN_TX_I2S0	(1 << 7)
#define I2S_TDM_WORD_ALIGN_TX_I2S1	(2 << 7)
/* Bit alignment in one word, playback */
#define I2S_TDM_DATA_ALIGN_TX_LEFT_J	(0 << 9)
#define I2S_TDM_DATA_ALIGN_TX_I2S	(1 << 9)
/* Channel count (0,1,2,3 -> 2,4,6,8) */
#define I2S_TDM_ADC_CH(c)		((((c) / 2) - 1) << 10)
#define I2S_TDM_DAC_CH(c)		((((c) / 2) - 1) << 12)
/* Number of bits in one word */
#define I2S_TDM_WORD_SIZE_RX(v)		(((v) - 1) << 14)
#define I2S_TDM_WORD_SIZE_TX(v)		(((v) - 1) << 19)
/* Reset */
#define I2S_TDM_RX_RESET		(1 << 26)
#define I2S_TDM_TX_RESET		(1 << 27)
/* Bitmask */
#define I2S_TDM_MASK_RX			0x0007CC7F
#define I2S_TDM_MASK_TX			0x00F8338F

#define SIRF_I2S_EXT_CLK	0x0
#define SIRF_I2S_PWM_CLK	0x1
#define SIRF_I2S_DTO_CLK	0x2
#endif /*__SIRF_I2S_H*/
