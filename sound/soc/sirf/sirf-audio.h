/*
 * SiRF inner codec controllers define
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef _SIRF_INNER_AUDIO_CTRL_H
#define _SIRF_INNER_AUDIO_CTRL_H

#define AUDIO_CTRL_TX_FIFO_LEVEL_CHECK_MASK     0x3F
#define AUDIO_CTRL_TX_FIFO_SC_OFFSET    0
#define AUDIO_CTRL_TX_FIFO_LC_OFFSET    10
#define AUDIO_CTRL_TX_FIFO_HC_OFFSET    20

#define TX_FIFO_SC(x)           (((x) & AUDIO_CTRL_TX_FIFO_LEVEL_CHECK_MASK) \
				<< AUDIO_CTRL_TX_FIFO_SC_OFFSET)
#define TX_FIFO_LC(x)           (((x) & AUDIO_CTRL_TX_FIFO_LEVEL_CHECK_MASK) \
				<< AUDIO_CTRL_TX_FIFO_LC_OFFSET)
#define TX_FIFO_HC(x)           (((x) & AUDIO_CTRL_TX_FIFO_LEVEL_CHECK_MASK) \
				<< AUDIO_CTRL_TX_FIFO_HC_OFFSET)

#define AUDIO_CTRL_RX_FIFO_LEVEL_CHECK_MASK     0x0F
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
#define AUDIO_CTRL_AC97_CTRL			(0x0004)
#define AUDIO_CTRL_AC97_CMD			(0x0008)
#define AUDIO_CTRL_AC97_OP_STATUS		(0x000C)
#define AUDIO_CTRL_AC97_RD_CODEC_REG		(0x0010)
#define AUDIO_CTRL_TXSLOT_EN			(0x0014)
#define AUDIO_CTRL_RXSLOT_EN			(0x0018)
#define AUDIO_CTRL_AC97_AUX_SLOT_EN		(0x001c)
#define AUDIO_CTRL_I2S_CTRL			(0x0020)
#define AUDIO_CTRL_I2S_TX_RX_EN			(0x0024)

#define AUDIO_CTRL_EXT_TXFIFO1_OP		(0x0040)
#define AUDIO_CTRL_EXT_TXFIFO1_LEV_CHK		(0x0044)
#define AUDIO_CTRL_EXT_TXFIFO1_STS		(0x0048)
#define AUDIO_CTRL_EXT_TXFIFO1_INT		(0x004C)
#define AUDIO_CTRL_EXT_TXFIFO1_INT_MSK		(0x0050)

#define AUDIO_CTRL_EXT_TXFIFO2_OP		(0x0054)
#define AUDIO_CTRL_EXT_TXFIFO2_LEV_CHK		(0x0058)
#define AUDIO_CTRL_EXT_TXFIFO2_STS		(0x005C)
#define AUDIO_CTRL_EXT_TXFIFO2_INT		(0x0060)
#define AUDIO_CTRL_EXT_TXFIFO2_INT_MSK		(0x0064)

#define AUDIO_CTRL_EXT_TXFIFO3_OP		(0x0068)
#define AUDIO_CTRL_EXT_TXFIFO3_LEV_CHK		(0x006C)
#define AUDIO_CTRL_EXT_TXFIFO3_STS		(0x0070)
#define AUDIO_CTRL_EXT_TXFIFO3_INT		(0x0074)
#define AUDIO_CTRL_EXT_TXFIFO3_INT_MSK		(0x0078)

#define AUDIO_CTRL_EXT_TXFIFO4_OP		(0x007C)
#define AUDIO_CTRL_EXT_TXFIFO4_LEV_CHK		(0x0080)
#define AUDIO_CTRL_EXT_TXFIFO4_STS		(0x0084)
#define AUDIO_CTRL_EXT_TXFIFO4_INT		(0x0088)
#define AUDIO_CTRL_EXT_TXFIFO4_INT_MSK		(0x008C)

#define AUDIO_CTRL_EXT_TXFIFO5_OP		(0x0090)
#define AUDIO_CTRL_EXT_TXFIFO5_LEV_CHK		(0x0094)
#define AUDIO_CTRL_EXT_TXFIFO5_STS		(0x0098)
#define AUDIO_CTRL_EXT_TXFIFO5_INT		(0x009C)
#define AUDIO_CTRL_EXT_TXFIFO5_INT_MSK		(0x00A0)

#define AUDIO_CTRL_EXT_TXFIFO6_OP		(0x00A4)
#define AUDIO_CTRL_EXT_TXFIFO6_LEV_CHK		(0x00A8)
#define AUDIO_CTRL_EXT_TXFIFO6_STS		(0x00AC)
#define AUDIO_CTRL_EXT_TXFIFO6_INT		(0x00B0)
#define AUDIO_CTRL_EXT_TXFIFO6_INT_MSK		(0x00B4)

#define AUDIO_CTRL_RXFIFO_OP			(0x00B8)
#define AUDIO_CTRL_RXFIFO_LEV_CHK		(0x00BC)
#define AUDIO_CTRL_RXFIFO_STS			(0x00C0)
#define AUDIO_CTRL_RXFIFO_INT			(0x00C4)
#define AUDIO_CTRL_RXFIFO_INT_MSK		(0x00C8)

#define AUDIO_CTRL_AUXFIFO_OP			(0x00CC)
#define AUDIO_CTRL_AUXFIFO_LEV_CHK		(0x00D0)
#define AUDIO_CTRL_AUXFIFO_STS			(0x00D4)
#define AUDIO_CTRL_AUXFIFO_INT			(0x00D8)
#define AUDIO_CTRL_AUXFIFO_INT_MSK		(0x00DC)

#define AUDIO_IC_CODEC_PWR			(0x00E0)
#define AUDIO_IC_CODEC_CTRL0			(0x00E4)
#define AUDIO_IC_CODEC_CTRL1			(0x00E8)
#define AUDIO_IC_CODEC_CTRL2			(0x00EC)
#define AUDIO_IC_CODEC_CTRL3			(0x00F0)

#define AUDIO_CTRL_IC_CODEC_TX_CTRL		(0x00F4)
#define AUDIO_CTRL_IC_CODEC_RX_CTRL		(0x00F8)

#define AUDIO_CTRL_IC_TXFIFO_OP			(0x00FC)
#define AUDIO_CTRL_IC_TXFIFO_LEV_CHK		(0x0100)
#define AUDIO_CTRL_IC_TXFIFO_STS		(0x0104)
#define AUDIO_CTRL_IC_TXFIFO_INT		(0x0108)
#define AUDIO_CTRL_IC_TXFIFO_INT_MSK		(0x010C)

#define AUDIO_CTRL_IC_RXFIFO_OP			(0x0110)
#define AUDIO_CTRL_IC_RXFIFO_LEV_CHK		(0x0114)
#define AUDIO_CTRL_IC_RXFIFO_STS		(0x0118)
#define AUDIO_CTRL_IC_RXFIFO_INT		(0x011C)
#define AUDIO_CTRL_IC_RXFIFO_INT_MSK		(0x0120)

#define I2S_MODE				(1<<0)
#define AC97_TX_SLOT3_WIDTH_MASK		(3<<1)
#define AC97_TX_SLOT4_WIDTH_MASK		(3<<3)
#define AC97_TX_SLOT6_WIDTH_MASK		(3<<5)
#define AC97_TX_SLOT7_WIDTH_MASK		(3<<7)
#define AC97_TX_SLOT8_WIDTH_MASK		(3<<9)
#define AC97_TX_SLOT9_WIDTH_MASK		(3<<11)
#define AC97_FIFO_SYNC_MASK			(3<<13)
#define AC97_FIFO_SYNC_ALL			(1<<13)

#define SYNC_START				(1<<0)
#define AC97_START				(1<<1)
#define WARM_WAKEUP				(1<<2)

#define AC97_CMD_TYPE_MASK			(1<<7)
#define AC97_CMD_ADDR_MASK			(0x7F)
#define AC97_CMD_TYPE_WRITE			(0<<7)
#define AC97_CMD_TYPE_READ			(1<<7)

#define CMD_ISSUE_BIT				(1<<0)
#define	RD_CMD_FINISH_BIT			(1<<1)
#define	CODEC_READY_BIT				(1<<2)

#define	AC97_RDBACK_ADDR_BITS			(0x7F)
#define	AC97_RDBACK_DATA_BITS			(0xFF<<16)

#define AC97_TX_SLOT3_EN			(1<<0)
#define AC97_TX_SLOT4_EN			(1<<1)
#define AC97_TX_SLOT6_EN			(1<<2)
#define AC97_TX_SLOT7_EN			(1<<3)
#define AC97_TX_SLOT8_EN			(1<<4)
#define AC97_TX_SLOT9_EN			(1<<5)

#define AC97_RX_SLOT3_EN			(1<<0)
#define AC97_RX_SLOT4_EN			(1<<1)
#define AC97_RX_SLOT5_EN			(1<<2)
#define AC97_RX_SLOT6_EN			(1<<3)
#define AC97_RX_SLOT7_EN			(1<<4)
#define AC97_RX_SLOT8_EN			(1<<5)
#define AC97_RX_SLOT9_EN			(1<<6)
#define AC97_RX_SLOT10_EN			(1<<7)
#define AC97_RX_SLOT11_EN			(1<<8)
#define AC97_RX_SLOT12_EN			(1<<9)

#define AC97_AUX_SLOT3_EN			(1<<0)
#define AC97_AUX_SLOT4_EN			(1<<1)
#define AC97_AUX_SLOT5_EN			(1<<2)
#define AC97_AUX_SLOT6_EN			(1<<3)
#define AC97_AUX_SLOT7_EN			(1<<4)
#define AC97_AUX_SLOT8_EN			(1<<5)
#define AC97_AUX_SLOT9_EN			(1<<6)
#define AC97_AUX_SLOT10_EN			(1<<7)
#define AC97_AUX_SLOT11_EN			(1<<8)
#define AC97_AUX_SLOT12_EN			(1<<9)

#define I2S_LOOP_BACK				(1<<3)
#define	I2S_MCLK_DIV_SHIFT			15
#define I2S_MCLK_DIV_MASK			(0x1FF<<I2S_MCLK_DIV_SHIFT)
#define I2S_BITCLK_DIV_SHIFT			24
#define I2S_BITCLK_DIV_MASK			(0xFF<<I2S_BITCLK_DIV_SHIFT)

#define I2S_MCLK_EN				(1<<2)
#define I2S_REF_CLK_SEL_EXT			(1<<3)
#define I2S_DOUT_OE				(1<<4)
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
#define I2S_L_CHAN_LEN_MASK		(0x1f << 4)
#define I2S_FRAME_LEN_MASK		(0x3f << 9)

#define AC97_WRITE_FRAME_VALID    0X10
#define AC97_WRITE_SLOT1_VALID    0X08
#define AC97_WRITE_SLOT2_VALID    0X04
#define AC97_WRITE_SLOT3_VALID    0X02
#define AC97_WRITE_SLOT4_VALID    0X01

#define IC_TX_ENABLE		(0x03)
#define IC_RX_ENABLE		(0x03)

#define MICBIASEN		(1 << 3)

#define IC_RDACEN		(1 << 0)
#define IC_LDACEN		(1 << 1)
#define IC_HSREN		(1 << 2)
#define IC_HSLEN		(1 << 3)
#define IC_SPEN			(1 << 4)
#define IC_CPEN			(1 << 5)

#define IC_HPRSELR		(1 << 6)
#define IC_HPLSELR		(1 << 7)
#define IC_HPRSELL		(1 << 8)
#define IC_HPLSELL		(1 << 9)
#define IC_SPSELR		(1 << 10)
#define IC_SPSELL		(1 << 11)

#define IC_MONOR		(1 << 12)
#define IC_MONOL		(1 << 13)

#define IC_RXOSRSEL		(1 << 28)
#define IC_CPFREQ		(1 << 29)
#define IC_HSINVEN		(1 << 30)

#define IC_MICINREN		(1 << 0)
#define IC_MICINLEN		(1 << 1)
#define IC_MICIN1SEL		(1 << 2)
#define IC_MICIN2SEL		(1 << 3)
#define IC_MICDIFSEL		(1 << 4)
#define	IC_LINEIN1SEL		(1 << 5)
#define	IC_LINEIN2SEL		(1 << 6)
#define	IC_RADCEN		(1 << 7)
#define	IC_LADCEN		(1 << 8)
#define	IC_ALM			(1 << 9)

#define IC_DIGMICEN             (1 << 22)
#define IC_DIGMICFREQ           (1 << 23)
#define IC_ADC14B_12            (1 << 24)
#define IC_FIRDAC_HSL_EN        (1 << 25)
#define IC_FIRDAC_HSR_EN        (1 << 26)
#define IC_FIRDAC_LOUT_EN       (1 << 27)
#define IC_POR                  (1 << 28)
#define IC_CODEC_CLK_EN         (1 << 29)
#define IC_HP_3DB_BOOST         (1 << 30)

#define IC_ADC_LEFT_GAIN_SHIFT	16
#define IC_ADC_RIGHT_GAIN_SHIFT 10
#define IC_ADC_GAIN_MASK	0x3F
#define IC_MIC_MAX_GAIN		0x39

#define IC_RXPGAR_MASK		0x3F
#define IC_RXPGAR_SHIFT		14
#define IC_RXPGAL_MASK		0x3F
#define IC_RXPGAL_SHIFT		21
#define IC_RXPGAR		0x7B
#define IC_RXPGAL		0x7B

#endif /*__SIRF_INNER_AUDIO_CTRL_H*/
