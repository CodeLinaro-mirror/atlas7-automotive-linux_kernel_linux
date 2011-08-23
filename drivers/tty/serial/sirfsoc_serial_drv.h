/*
* Copyright (c) 2007 Nomad Global Solutions, Inc.
* Copyright (C) 2007 Centrality Communication Inc.
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License as
* published by the Free Software Foundation; either version 2 of
* the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston,
* MA 02111-1307 USA
*/
#ifndef __SIRFSOC_SERIAL_DRV__
#define __SIRFSOC_SERIAL_DRV__

#define UART_LINE_CTRL		0x0040 /* UART line control register */
#define UART_TX_RX_ENABLE	0x004c /* UART transmit / receive enable register */
#define UART_DIVISOR		0x0050 /* UART baud rate divisor register */
#define UART_INT_ENABLE		0x0054 /* UART interrupt enable register */
#define UART_INT_STATUS		0x0058 /* UART interrupt status register */
#define UART_RISC_DSP_MODE	0x005c /* UART accessing select register */
#define UART_TX_DMA_IO_CTRL	0x0100 /* UART TXFIFO DMA/IO register */
#define UART_TX_DMA_IO_LEN	0x0104 /* UART transmit data length register */
#define UART_TX_FIFO_CTRL	0x0108 /* UART TXFIFO control register */
#define UART_TX_FIFO_LEVEL_CHK	0x010C /* UART TXFIFO check level register */
#define UART_TX_FIFO_OP		0x0110 /* UART TXFIFO operation register */
#define UART_TX_FIFO_STATUS	0x0114 /* UART TXFIFO status register */
#define UART_TX_FIFO_DATA	0x0118 /* UART TXFIFO bottom */
#define UART_RX_DMA_IO_CTRL	0x0120 /* UART RXFIFO DMA/IO register */
#define UART_RX_DMA_IO_LEN	0x0124 /* UART receive length register */
#define UART_RX_FIFO_CTRL	0x0128 /* UART RXFIFO control register */
#define UART_RX_FIFO_LEVEL_CHK	0x012C /* UART RXFIFO check level register */
#define UART_RX_FIFO_OP		0x0130 /* UART RXFIFO operation register */
#define UART_RX_FIFO_STATUS	0x0134 /* UART RXFIFO status register */
#define UART_RX_FIFO_DATA	0x0138 /* UART RXFIFO bottom */
#define UART_AFC_CTRL       0x0140 /* UART Auto flow control - Atlas4/5 Only */ 

/* UART line ctrl definitions */
#define DATA_BIT_LEN_MASK	(3<<0)
#define DATA_BIT_LEN_5		(0<<0)
#define DATA_BIT_LEN_6		(1<<0)
#define DATA_BIT_LEN_7		(2<<0)
#define DATA_BIT_LEN_8		(3<<0)
#define STOP_BIT_LEN_1		(0<<2)
#define STOP_BIT_LEN_2		(1<<2)
#define PARITY_EN		(1<<3)
#define EVEN_BIT		(1<<4)
#define STICK_BIT_MASK		(7<<3)
#define STICK_BIT_NONE		(0<<3)
#define STICK_BIT_EVEN		(1<<3)
#define STICK_BIT_ODD		(3<<3)
#define STICK_BIT_MARK		(5<<3)
#define STICK_BIT_SPACE		(7<<3)
#define SET_BREAK		(1<<6)
#define LOOP_BACK		(1<<7)
#define RECV_TIMEOUT_MASK	(0xFFFF << 16)
#define RECV_TIMEOUT(x)		(((x)&0xFFFF)<<16)

#define UART_CLK_DIV_MASK	(0xFFFF)
#define UART_SAMPLE_DIV_MASK	(0x3F<<16)

#define UART_RX_DONE_INT			(1<<0)
#define UART_TX_DONE_INT			(1<<1)
#define UART_RX_OFLOW_INT			(1<<2)
#define UART_TX_ALLOUT_INT			(1<<3)
#define UART_RX_IO_DMA_INT			(1<<4)
#define UART_TX_IO_DMA_INT			(1<<5)
#define UART_RXFIFO_FULL_INT			(1<<6)
#define UART_TXFIFO_EMPTY_INT			(1<<7)
#define UART_RXFIFO_THD_INT			(1<<8)
#define UART_TXFIFO_THD_INT			(1<<9)
#define UART_FRM_ERR_INT			(1<<10)
#define UART_RXD_BREAK_INT			(1<<11)
#define UART_RX_TIMEOUT_INT			(1<<12)
#define UART_PARITY_ERR_INT			(1<<13)
#define UART_CTS_INT_EN             (1<<14) 
#define UART_RTS_INT_EN             (1<<15) 

#define UART_TX_IO_MODE                         (1<<0)
#define UART_RX_IO_MODE                         (1<<0)

/* UART AFC definitions - Atlas4 Only */ 
#define UART_AFC_RX_THD_MASK        0x00FF 
#define UART_AFC_RX_EN              (1<<8) 
#define UART_AFC_TX_EN              (1<<9) 
#define UART_CTS_CTRL               (1<<10) 
#define UART_RTS_CTRL               (1<<11) 
#define UART_CTS_IN_STATUS          (1<<12) 
#define UART_RTS_OUT_STATUS         (1<<13) 

#define UART_MODEM_CTRL        0x44
#define UART_MODEM_STATUS      0x48

#define FIFO_SC(x)				((x)<<0)
#define FIFO_LC(x)				((x)<<10)
#define FIFO_HC(x)				((x)<<20)

#define UART_TX_FIFO_RESET                       0x1
#define UART_TX_FIFO_START                       0x2
#define UART_RX_FIFO_RESET                       0x1
#define UART_RX_FIFO_START                       0x2

#define UART_FIFO_SIZE(pdata)           	((pdata)->fifo_size)

/* Tx - RX Enable Register */
#define UART_RX_ENA		0x00000001
#define UART_TX_ENA		0x00000002

/* UART RISC/DSP Mode Register */
#define UART_RISC_DSP_SEL	0x00000001

/* UART TX FIFO Control Register */
#define UART_TX_FIFO_WIDTH_MASK		0x00000003
#define UART_TX_FIFO_WIDTH_OFFSET	0

#define UART_TX_FIFO_THD_MASK		0x0000007F
#define UART_TX_FIFO_THD_OFFSET		0

#define UART_TX_FIFO_THRESHOLD(pdata)		((pdata)->fifo_size/2)



/* UART TX DMA I/O MODE Register */
#define UART_TX_MODE_IO			0x00000001

/* UART TX FIFO Level Check Register */
#define UART_TX_FIFO_LEVEL_CHECK_MASK(pdata)	((pdata)->fifo_level_mask)
#define UART_TX_FIFO_SC_OFFSET	0
#define UART_TX_FIFO_LC_OFFSET	10
#define UART_TX_FIFO_HC_OFFSET	20

#define UART_TX_FIFO_SC(pdata, x)		(((x) & UART_TX_FIFO_LEVEL_CHECK_MASK(pdata)) << UART_TX_FIFO_SC_OFFSET)
#define UART_TX_FIFO_LC(pdata, x)		(((x) & UART_TX_FIFO_LEVEL_CHECK_MASK(pdata)) << UART_TX_FIFO_LC_OFFSET)
#define UART_TX_FIFO_HC(pdata, x)		(((x) & UART_TX_FIFO_LEVEL_CHECK_MASK(pdata)) << UART_TX_FIFO_HC_OFFSET)

/*********************************************************************************************/

/* UART RX DMA I/O MODE Register */
#define UART_RX_MODE_IO			0x00000001
#define UART_RX_DMA_FLUSH		0x00000004

/* UART RX FIFO Level Check Register */
#define UART_RX_FIFO_LEVEL_CHECK_MASK	UART_TX_FIFO_LEVEL_CHECK_MASK
#define UART_RX_FIFO_SC_OFFSET	0
#define UART_RX_FIFO_LC_OFFSET	10
#define UART_RX_FIFO_HC_OFFSET	20

#define UART_RX_FIFO_SC		UART_TX_FIFO_SC
#define UART_RX_FIFO_LC		UART_TX_FIFO_LC
#define UART_RX_FIFO_HC		UART_TX_FIFO_HC

/* USP RX FIFO Control Register */
#define UART_RX_FIFO_WIDTH_MASK		0x00000003
#define UART_RX_FIFO_WIDTH_OFFSET	0

#define UART_RX_FIFO_THD_MASK		0x000001FC
#define UART_RX_FIFO_THD_OFFSET		0

#define UART_RX_FIFO_THRESHOLD		UART_TX_FIFO_THRESHOLD

/* UART RX FIFO Status Register */
#define UART_RX_FIFO_LEVEL_MASK		0x0000007F
#define UART_RX_FIFO_LEVEL_OFFSET	0

/* These UART RX FIFO Status is only for UART0 */
#define UART_RX_FIFO_FULL		0x00000080
#define UART_RX_FIFO_EMPTY		0x00000100

#define UART0_FIFO_SIZE			128
#define UART0_FIFO_LEVEL_MASK		(0x7F)
#define UART0_FIFO_EMPTY		(1<<8)
#define UART0_FIFO_FULL			(1<<7)
#define UART0_RX_THRESHOLD		0x40

#define UART1_FIFO_SIZE			32
#define UART1_FIFO_LEVEL_MASK		(0x1F)
#define UART1_FIFO_EMPTY		(1<<6)
#define UART1_FIFO_FULL			(1<<5)
#define UART1_RX_THRESHOLD		0x10

#ifndef CONFIG_ARCH_PRIMA2
#define UART2_FIFO_SIZE			32
#define UART2_FIFO_LEVEL_MASK		(0x1F)
#define UART2_FIFO_EMPTY		(1<<6)
#define UART2_FIFO_FULL			(1<<5)
#define UART2_RX_THRESHOLD		0x10
#define	CLK_BASE			SIRFSOC_CLOCK_VA_BASE
#else
#define UART2_FIFO_SIZE			128
#define UART2_FIFO_LEVEL_MASK		(0x7F)
#define UART2_FIFO_EMPTY		(1<<8)
#define UART2_FIFO_FULL			(1<<7)
#define UART2_RX_THRESHOLD		0x40
#define CLK_BASE			SIRFSOC_CLOCK_VA_BASE
#endif

#define CONFIG_SIRFSOC_UART_NR 4
#define PORT_SIRFSOC 98
/* UART-1: used as serial debug port */
#define SIRFSOC_UART1_PA_BASE           0xb0060000
#define SIRFSOC_UART1_SIZE              SZ_4K
#define IRQ_UART0 17
#define IRQ_UART1 18
#define IRQ_UART1 19

struct sirfsoc_uart_pdata {
	int rts_gpio;
	int cts_gpio;

	int hw_flow_control_enabled;

	u32 fifo_size;
	u32 fifo_level_mask;
	u32 fifo_full_mask;
	u32 fifo_empty_mask;
	u32 rx_threshold;

	u32 rx_timeout_in_us;
	u32 max_baudrate_in_bps;

	char *clock;

	int tx_dma_chan;
	int rx_dma_chan;
	int tx_dma_width_reg;
	int rx_dma_width_reg;

	u32 va_addr;
	void (*platform_init) (void);
};

#endif

