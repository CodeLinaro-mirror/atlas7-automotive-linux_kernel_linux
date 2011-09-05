/*
 *  Copyright (C) 2007 by SiRF Technology, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/sysrq.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <asm/dma.h>
#include <mach/hardware.h>
#include <asm/gpio.h>
#include <linux/pinctrl/pinmux.h>

#include "sirfsoc_serial_drv.h"

#define DEBUG_LOG		printk
#define UART_PORT_NAME 		"sirfsoc-uart"
#define UART_RX_TIMEOUT(br, to)	(((br) * (((to)+999)/1000))/1000)
#define UART_DUMMY_READ		(1 << 16)
#define UART_RX_ERR_INTRS	(UART_RX_OFLOW_INT | UART_FRM_ERR_INT | UART_RXD_BREAK_INT | UART_PARITY_ERR_INT)

/* normal ports will have rxthd intr */
#define UART_RX_XFER_INTRS	(UART_RX_TIMEOUT_INT | UART_RXFIFO_THD_INT | UART_RXFIFO_FULL_INT)

#define UART_RX_XFER_INTRS_DMA	(UART_RX_TIMEOUT_INT)

#define UART_RX_INTRS		(UART_RX_XFER_INTRS | UART_RX_ERR_INTRS)
#define UART_RX_INTRS_DMA	(UART_RX_XFER_INTRS_DMA | UART_RX_ERR_INTRS)
#define UART_TX_INTRS 		(UART_TXFIFO_EMPTY_INT)

/*
 * UART with hw flow control, 16KB DMA buffer is stable.
 * UART with DMA and flow control should remove tty buffer limit.
 */
#define RX_DMA_BUF_SIZE		1024
#define RX_DMA_WIDTH		4	/* DWORD = 4 bytes */
#define RX_DMA_BUF_A		0x1
#define RX_DMA_BUF_B		0x2

/*
 * Set threshold be 4/5 fifo size.
 * In driver, not use lower fifo threshold to determinate rts status,
 * Instead, judge high layer buffer 1/2 threshold, then call lower driver set_mctrl to (de)asserted rts.
 * For UART with DMA should set 0x40
 */
#define AFC_CTRL_RX_THD     	0x70
#define IO_BRIDGE_FLUSH		0x0804

/* Mayank: Is the following not redundant ? */
#define uart_tx_port_tty_invalid(port)   \
(((port)->state == NULL) || ((port)->state->port.tty == NULL))
#define CTS_ASSERTED(gpio) ((gpio_get_value(gpio)) ? 0 : 1)

static struct sirfsoc_uart_pdata sirfsoc_serial1_pdata = {
	/* rts/cts gpios will be updated by board(s) specific file(s). */
	.rts_gpio = -1,
	.cts_gpio = -1,

	.hw_flow_control_enabled = 0,

	.fifo_size = UART1_FIFO_SIZE,
	.fifo_level_mask = UART1_FIFO_LEVEL_MASK,
	.fifo_full_mask = UART1_FIFO_FULL,
	.fifo_empty_mask = UART1_FIFO_EMPTY,
	.rx_threshold = UART1_RX_THRESHOLD,
	.rx_timeout_in_us = 20000,
	.max_baudrate_in_bps = 921600,

	.clock = "uart1",

	.tx_dma_chan = -1,
	.rx_dma_chan = -1,
};

static struct resource sirfsoc_serial1_resources[] = {
	{
		.flags = IORESOURCE_MEM,
		.start = SIRFSOC_UART1_PA_BASE,
		.end = SIRFSOC_UART1_PA_BASE + SIRFSOC_UART1_SIZE - 1,
	}, {
		.flags = IORESOURCE_IRQ,
		.start = IRQ_UART1,
		.end = IRQ_UART1,
	},
};

struct platform_device sirfsoc_serial_device_1 = {
	.name = "sirfsoc-uart",
	.id = 1,
	.num_resources = ARRAY_SIZE(sirfsoc_serial1_resources),
	.resource = sirfsoc_serial1_resources,
	.dev = {
		.platform_data = &sirfsoc_serial1_pdata,
	},
};

struct platform_device *sirfsoc_default_console_device =  &sirfsoc_serial_device_1;

struct sirfsoc_uart_drv {
	struct platform_device *pdev;
	struct uart_port port;
	struct sirfsoc_uart_pdata *pdata;
	struct clk *clk;
	u32 cts_assertion_cnt;	/* counter */
	u32 cts_deassertion_cnt;	/* counter */
	u32 parity_err_cnt;
	u32 rx_intr_mask;	/* ports have different interrupt masks enabled */
	u32 ms_enabled:1;
	u32 loopback;
	u32 num_bytes_io;

	/* DMA support for UART. start-- */
	int tx_dma_chan;
	int tx_dma_width_reg;
	int tx_dma_len;
	dma_addr_t tx_dma_buf;
	u32 tx_dma_running:1;	/* Current state of DMA */
	u32 tx_dma_disable:1;	/* Whether to disable re-scheduling of DMA */

	int rx_dma_chan;
	int rx_dma_width_reg;
	dma_addr_t rx_dma_buf;
	void *rx_buf_cpu_addr;
	int rx_dma_len;
	int rx_dma_cur_buf;
	/* --end */

	struct tasklet_struct serial_rx_dma_tasklet;	/* Tasklet structures */
};
struct sirfsoc_uart_drv sirfsoc_serial_ports[CONFIG_SIRFSOC_UART_NR];

static irqreturn_t cts_handler(int irq, void *pvcontext);
extern struct platform_device *sirfsoc_default_console_device;	/* the serial console device */
static int sirfsoc_uart_copy_to_fifo(struct uart_port *port, int num);
static void sirfsoc_serial_dma_tx_chars(struct sirfsoc_uart_drv *sirfsoc_data);
static void sirfsoc_uart_rx_chars(struct uart_port *port, u32 num);

static unsigned int sirfsoc_tx_empty(struct uart_port *port)
{
	struct sirfsoc_uart_pdata *pdata =
		((struct sirfsoc_uart_drv *)port->private_data)->pdata;
	unsigned int ret =
		readl(port->membase + UART_TX_FIFO_STATUS) & pdata->fifo_empty_mask;
	if (ret)
		return TIOCSER_TEMT;
	else
		return 0;
}

static unsigned int sirfsoc_get_mctrl(struct uart_port *port)
{
	struct sirfsoc_uart_drv *sirfsoc_data =
		(struct sirfsoc_uart_drv *)port->private_data;
	struct sirfsoc_uart_pdata *pdata = sirfsoc_data->pdata;

	if ((!(sirfsoc_data->ms_enabled))) {
		goto cts_asserted;
	} else if ((pdata->hw_flow_control_enabled)) {
		if (!(readl(port->membase + UART_AFC_CTRL) & UART_CTS_IN_STATUS)) {
			goto cts_asserted;
		}
		goto cts_deasserted;
	} else if (CTS_ASSERTED(pdata->cts_gpio)) {
		goto cts_asserted;
	}
	/* fall through */

cts_deasserted:
	/* DEBUG_LOG(KERN_DEBUG " %s: CTS-%d is Deasserted\n", __func__, pdata->cts_gpio); */
	return TIOCM_CAR | TIOCM_DSR;

cts_asserted:
	/* DEBUG_LOG(KERN_DEBUG " %s: CTS-%d is Asserted\n", __func__, pdata->cts_gpio); */
	return TIOCM_CAR | TIOCM_DSR | TIOCM_CTS;
}

/* This function (de)asserts RTS as specified by the higher layer. */
static void sirfsoc_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct sirfsoc_uart_drv *sirfsoc_data =
		(struct sirfsoc_uart_drv *)port->private_data;
	struct sirfsoc_uart_pdata *pdata = sirfsoc_data->pdata;

	if (pdata->hw_flow_control_enabled) {
		unsigned int assert = mctrl & TIOCM_RTS;
		/* if the AFC_CTRL_RX_THD is set to 0 then automatically
		 * flow control will be triggered and RTS will be deasserted
		 *
		 * Otherwise it is set to a reasonable threshold value
		 */
		unsigned int val = (assert) ? AFC_CTRL_RX_THD : 0x0;
		unsigned int current_val =
			readl(port->membase + UART_AFC_CTRL) & ~(0xFF);
		val |= current_val;
		writel(val, port->membase + UART_AFC_CTRL);
	} else if (pdata->rts_gpio != -1) {
		gpio_set_value(pdata->rts_gpio, !(mctrl & TIOCM_RTS));
	}
}

static void sirfsoc_stop_tx(struct uart_port *port)
{
	struct sirfsoc_uart_drv *sirfsoc_data =
		(struct sirfsoc_uart_drv *)port->private_data;
	if (sirfsoc_data->tx_dma_chan != -1) {
		sirfsoc_data->tx_dma_disable = 1;
		barrier();
	} else {
		writel(readl(port->membase + UART_INT_ENABLE) & ~UART_TX_INTRS, port->membase + UART_INT_ENABLE);	/* Disable TX EMPTY interrupt */
	}
}

void sirfsoc_start_tx(struct uart_port *port)
{
	struct sirfsoc_uart_drv *drv_data =
		(struct sirfsoc_uart_drv *)port->private_data;

	if (drv_data->tx_dma_chan != -1) {
		drv_data->tx_dma_disable = 0;
		sirfsoc_serial_dma_tx_chars(drv_data);
	} else {
		/*
		 * There is no check here for CTS status. CTS interrupt handler
		 * would already have been registered
		 * before this function is called.
		 */
		sirfsoc_uart_copy_to_fifo(port, 1);	/* Trigger FIFO empty interrupt generation */
		writel(UART_TX_FIFO_START, port->membase + UART_TX_FIFO_OP);
		writel(readl(port->membase + UART_INT_ENABLE) | UART_TX_INTRS,
			port->membase + UART_INT_ENABLE);
	}
}

static void sirfsoc_stop_rx(struct uart_port *port)
{
	struct sirfsoc_uart_drv *sirfsoc_data =
		(struct sirfsoc_uart_drv *)port->private_data;

	if (sirfsoc_data->rx_dma_chan != -1) {
	} else {
		writel(0, port->membase + UART_RX_FIFO_OP);
		writel(readl(port->membase + UART_INT_ENABLE) &
			~(sirfsoc_data->rx_intr_mask),
			port->membase + UART_INT_ENABLE);
	}
}

/* this is a local function and not a callback */
static void sirfsoc_disable_ms(struct uart_port *port)
{
	struct sirfsoc_uart_drv *drv_data =
		(struct sirfsoc_uart_drv *)port->private_data;
	struct sirfsoc_uart_pdata *pdata = drv_data->pdata;

	drv_data->ms_enabled = 0;
	if (pdata->hw_flow_control_enabled) {
		unsigned int val;

		val = readl(port->membase + UART_AFC_CTRL);
		/* UART HW Flow control is enabled/disabled
		 * via RX_EN and TX_EN bits.
		 *
		 * Clearing these will automatically disable hw flow control
		 *
		 * In the AFC_CTRL register, these bits are the lower 10 bits
		 * and hence the value 0x3FF;
		 */
		val &= ~(0x3FF);
		writel(val, port->membase + UART_AFC_CTRL);

		/* Disable interrupt for CTS */
		val = readl(port->membase + UART_INT_ENABLE);
		val &= ~(UART_CTS_INT_EN);

		writel(val, port->membase + UART_INT_ENABLE);
	} else if (pdata->cts_gpio != -1) {
		gpio_set_value(pdata->rts_gpio, 1);
		free_irq(gpio_to_irq(pdata->cts_gpio), port);
	}
}

static void sirfsoc_enable_ms(struct uart_port *port)
{
	struct sirfsoc_uart_drv *drv_data =
		(struct sirfsoc_uart_drv *)port->private_data;
	struct sirfsoc_uart_pdata *pdata = drv_data->pdata;
	int err;

	/*
	 * HW flow control enabled
	 */
	if (pdata->hw_flow_control_enabled) {
		unsigned int val = (UART_AFC_RX_EN | UART_AFC_TX_EN);

		writel(readl(port->membase + UART_AFC_CTRL) | val,
			port->membase + UART_AFC_CTRL);

		/* Enable interrupt for CTS */
		writel(readl(port->membase + UART_INT_ENABLE) |
			(UART_CTS_INT_EN), port->membase + UART_INT_ENABLE);

		drv_data->ms_enabled = 1;
		uart_handle_cts_change(port,
			(!(readl(port->membase + UART_AFC_CTRL) &
			   UART_CTS_IN_STATUS)));
	} else if (pdata->cts_gpio != -1) {
		err = request_irq(gpio_to_irq(pdata->cts_gpio), cts_handler,
			IRQF_DISABLED | IRQF_TRIGGER_FALLING |
			IRQF_TRIGGER_RISING, "uart_cts_irq", port);
		if (err) {
			printk(KERN_ERR "%s: Request IRQ for CTS-%d failed\n",
				__func__, pdata->cts_gpio);
		} else {
			drv_data->ms_enabled = 1;
			uart_handle_cts_change(port,
				CTS_ASSERTED(pdata->cts_gpio));
		}
	}
}

static void sirfsoc_break_ctl(struct uart_port *port, int break_state)
{
	u32 status = readl(port->membase + UART_LINE_CTRL);

	if (break_state)
		writel(status | SET_BREAK, port->membase + UART_LINE_CTRL);
	else
		writel(status & ~SET_BREAK, port->membase + UART_LINE_CTRL);
}

static void sirfsoc_serial_tx_dma_irq(unsigned int chan, void *data)
{
}

static void sirfsoc_serial_dma_tx_chars(struct sirfsoc_uart_drv *sirfsoc_data)
{
}

/* This function pushes data copied from the Rx FIFO by the DMA into the TTY buffer */
static inline void sirfsoc_serial_rx_insert_char(struct uart_port *port,
	int buf)
{
}

static void sirfsoc_serial_rx_dma_irq_tl(unsigned long param)
{
}

static void sirfsoc_serial_rx_dma_irq(unsigned int chan, void *data)
{
}

static void sirfsoc_serial_rx_dma_start(struct uart_port *port)
{
}

/* Time out interrupt is enabled on starting DMA. For IO mode this interrupt is disabled */
static void sirfsoc_serial_dma_handle_rx_timeout(struct uart_port *port)
{
}

static void sirfsoc_serial_dma_handle_rx_done(struct uart_port *port)
{
}

/* This function is called from the UART interrupt handler on getting Rx Done or Rx FIFO full interrupts */
static void sirfsoc_uart_rx_chars(struct uart_port *port, u32 max_rx_count)
{
	u32 ch, rx_count = 0;
	struct tty_struct *tty = port->state->port.tty;
	struct sirfsoc_uart_drv *sirfsoc_data =
		(struct sirfsoc_uart_drv *)port->private_data;
	struct sirfsoc_uart_pdata *pdata =
		((struct sirfsoc_uart_drv *)port->private_data)->pdata;

	/* Read Characters from Rx FIFO until it becomes empty */
	while (!
		(readl(port->membase + UART_RX_FIFO_STATUS) & pdata->
		 fifo_empty_mask)) {
		if (unlikely
			(tty_buffer_request_room(port->state->port.tty, 1) == 0)) {
			port->icount.buf_overrun++;

			/* This is needed only if ms_enabled and gpio flow control is enabled
			 * and not needed for hw flow control
			 */
			if ((sirfsoc_data->ms_enabled)
				&& (pdata->rts_gpio != -1)) {

				u32 rx_fifo_sts =
					readl(port->membase + UART_RX_FIFO_STATUS);

				/* If flowcontrol is enabled and RXFIFO is near full, deassert RTS. Since the tty buffer is full,
				 * the higher layer will also de-assert RTS when reading the buffer. We are just pre-empting it.
				 * The higher layer will re-assert when enough space is available in the tty buffer. */
				if ((rx_fifo_sts &
						(pdata->fifo_full_mask | pdata->
						 fifo_level_mask)) >=
					(pdata->fifo_size / 2)) {
					DEBUG_LOG(KERN_DEBUG
						"RX FIFO Full.. Deassert RTS\n");
					gpio_set_value(pdata->rts_gpio, 1);
				}
			}
			break;
		}
		ch = readl(port->membase + UART_RX_FIFO_DATA) | UART_DUMMY_READ;

		if (unlikely(uart_handle_sysrq_char(port, ch))) {
			continue;
		}
		uart_insert_char(port, 0, 0, ch, TTY_NORMAL);
		rx_count++;
		if (rx_count >= max_rx_count)
			break;
	}
	port->icount.rx += rx_count;
	sirfsoc_data->num_bytes_io += rx_count;
	tty_flip_buffer_push(tty);

	return;
}

/* This function copies 'num' bytes of data from tty buffer to Tx FIFO. To not restrict the number of bytes
 * let num = -1.  */
static int sirfsoc_uart_copy_to_fifo(struct uart_port *port, int num)
{
	struct circ_buf *xmit = &port->state->xmit;
	unsigned int num_tx = 0;	/* Num of bytes actually copied */
	struct sirfsoc_uart_pdata *pdata =
		((struct sirfsoc_uart_drv *)port->private_data)->pdata;

	/* We do not bother to check or disable the CTS GPIO here. We will rely on the CTS interrupt handler starting and stopping
	 * transmission of the data on the line and worry ourselves only about filling the FIFO. */

	/* Fill TX FIFO while there are characters in the buffer and FIFO is not full. We assume that stopping the Tx FIFO does not
	 * prevent us from filling it up. */
	while (!uart_circ_empty(xmit) &&
		!(readl(port->membase + UART_TX_FIFO_STATUS) & pdata->
			fifo_full_mask) && num--) {
		writel(xmit->buf[xmit->tail],
			port->membase + UART_TX_FIFO_DATA);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		num_tx++;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	return num_tx;
}

static irqreturn_t sirfsoc_uart_isr(int irq, void *dev_id)
{
	u32 intr_status, flag = TTY_NORMAL;
	struct uart_port *port = dev_id;
	struct sirfsoc_uart_drv *pdb =
		(struct sirfsoc_uart_drv *)port->private_data;
	struct sirfsoc_uart_pdata *pdata =
		((struct sirfsoc_uart_drv *)pdb)->pdata;
	struct circ_buf *xmit = &port->state->xmit;

	intr_status = readl(port->membase + UART_INT_STATUS);
	/* Handle only the interrupt enabled */
	intr_status &= readl(port->membase + UART_INT_ENABLE);

	/* clear source interrupt(s) w1c */
	writel(intr_status, port->membase + UART_INT_STATUS);

	if (unlikely(intr_status & (UART_RX_ERR_INTRS))) {
		/* Print to check whether RX_DONE is valid along with oflow and FrmErr */
		DEBUG_LOG(KERN_DEBUG "UART-HW-ERR: intr_status=0x%X\n",
			intr_status);

		if (intr_status & UART_RXD_BREAK_INT) {
			DEBUG_LOG(KERN_DEBUG "UART: BREAK signal detected.\n");
			if (uart_handle_break(port)) {
				DEBUG_LOG(KERN_DEBUG "UART: BREAK SAK done\n");
				goto recv_char;
			}
			uart_insert_char(port, intr_status, UART_RX_OFLOW_INT,
				0, TTY_BREAK);
			return IRQ_HANDLED;
		}

		if (intr_status & UART_RX_OFLOW_INT) {
			port->icount.overrun++;
		}
		if (intr_status & UART_FRM_ERR_INT) {
			port->icount.frame++;
			flag = TTY_FRAME;
		}
		if (intr_status & UART_PARITY_ERR_INT) {
			pdb->parity_err_cnt++;
			flag = TTY_PARITY;
		}

		writel(UART_RX_FIFO_RESET, port->membase + UART_RX_FIFO_OP);
		writel(0, port->membase + UART_RX_FIFO_OP);
		writel(UART_RX_FIFO_START, port->membase + UART_RX_FIFO_OP);

		intr_status &= port->read_status_mask;
		uart_insert_char(port, intr_status, UART_RX_OFLOW_INT, 0, flag);
	}

recv_char:
	if (intr_status & UART_CTS_INT_EN) {
		u32 cts_status =
			!(readl(port->membase + UART_AFC_CTRL) &
				UART_CTS_IN_STATUS);
		/* DEBUG_LOG("%s-cts %d\n", __FUNCTION__, cts_status); */

		if (cts_status != 0) {
			pdb->cts_assertion_cnt++;
			uart_handle_cts_change(port, 1);
		} else {
			pdb->cts_deassertion_cnt++;
			uart_handle_cts_change(port, 0);
			wake_up_interruptible(&port->state->port.
				delta_msr_wait);
		}
	}

	/* handle all intrs of DMA enabled port */
	if (pdb->rx_dma_chan != -1) {
	} else if (intr_status & UART_RX_XFER_INTRS) {
		/* read only maximum of 256 bytes so that
		   this interrupt does not add to
		   system interrupt latency */
		sirfsoc_uart_rx_chars(port, 256);
	}

	if ((intr_status & UART_TX_INTRS) && (pdb->tx_dma_chan == -1)) {
		if (!(uart_tx_port_tty_invalid(port) || uart_tx_stopped(port))) {

			/* Copy as much as possible into the Tx FIFO
			 * 1/5 TX FIFO size
			 */
			sirfsoc_uart_copy_to_fifo(port,
				4 *
				(readl
				 (port->membase +
				  UART_TX_FIFO_CTRL) &
				 UART_TX_FIFO_THD_MASK) / 5);

			/* If both tty buffer and Tx FIFO are empty, stop Tx. It will be re-started when more
			 * data are written from higher layer. */
			if ((uart_circ_empty(xmit))
				&& readl(port->membase +
					UART_TX_FIFO_STATUS) & pdata->
				fifo_empty_mask) {
				sirfsoc_stop_tx(port);
			}
		}
	}

	return IRQ_HANDLED;
}

static void sirfsoc_start_rx(struct uart_port *port)
{
	struct sirfsoc_uart_drv *drv_data =
		(struct sirfsoc_uart_drv *)port->private_data;

	/* Enable RX interrupts */
	writel(readl(port->membase + UART_INT_ENABLE) |
		(drv_data->rx_intr_mask), port->membase + UART_INT_ENABLE);

	/* Start RX FIFO */
	writel(UART_RX_FIFO_RESET, port->membase + UART_RX_FIFO_OP);
	writel(0, port->membase + UART_RX_FIFO_OP);
	writel(UART_RX_FIFO_START, port->membase + UART_RX_FIFO_OP);
}

/* We only support 8n1,8n2 data pattern */
static void sirfsoc_set_termios(struct uart_port *port,
	struct ktermios *termios,
	struct ktermios *old_term)
{
	unsigned long flags;
	s32 sample_div = 0, threshold_div = 1, config_reg = 0;
	u32 clk_div, baudrate, tx_fifo_stat, clk_rate;
	struct sirfsoc_uart_drv *sirfsoc_data =
		(struct sirfsoc_uart_drv *)port->private_data;
	struct sirfsoc_uart_pdata *pdata = sirfsoc_data->pdata;

	/* for debugging purposes */
	if (sirfsoc_data->loopback)
		config_reg |= LOOP_BACK;

	switch (termios->c_cflag & CSIZE) {
	default:
	case CS8:
		config_reg |= DATA_BIT_LEN_8;
		break;
	case CS7:
		config_reg |= DATA_BIT_LEN_7;
		break;
	case CS6:
		config_reg |= DATA_BIT_LEN_6;
		break;
	case CS5:
		config_reg |= DATA_BIT_LEN_5;
		break;
	}
	if (termios->c_cflag & CSTOPB) {
		config_reg |= STOP_BIT_LEN_2;
	}

	baudrate =
		uart_get_baud_rate(port, termios, old_term, 0,
			pdata->max_baudrate_in_bps);

	spin_lock_irqsave(&port->lock, flags);

	/* Overflow reporting is always enabled */
	port->read_status_mask = UART_RX_OFLOW_INT;
	port->ignore_status_mask = 0;

	/* read flags */
	if (termios->c_iflag & INPCK) {
		port->read_status_mask |=
			(UART_FRM_ERR_INT | UART_PARITY_ERR_INT);
	}

	if (termios->c_iflag & (BRKINT | PARMRK)) {
		port->read_status_mask |= (UART_RXD_BREAK_INT);
	}

	/* ignore flags */
	if (termios->c_iflag & IGNPAR) {
		port->ignore_status_mask |=
			(UART_FRM_ERR_INT | UART_PARITY_ERR_INT);
	}

	if ((termios->c_cflag & CREAD) == 0) {
		port->ignore_status_mask |= UART_DUMMY_READ;
	}

	/* parity */
	if (termios->c_cflag & PARENB) {
		if (termios->c_cflag & CMSPAR) {	/* Mark or Space parity */
			if (termios->c_cflag & PARODD)
				config_reg |= STICK_BIT_MARK;
			else
				config_reg |= STICK_BIT_SPACE;
		} else if (termios->c_cflag & PARODD)
			config_reg |= STICK_BIT_ODD;
		else
			config_reg |= STICK_BIT_EVEN;
	}

	/*
	 * 1. When set ctsrts in user space (stty), if ms_enabled is 0, then enable ms.
	 * 2. When dont set ctsrts, which means not use. So disable ms if it was enabled before.
	 */
	if (UART_ENABLE_MS(port, termios->c_cflag)) {
		if (!sirfsoc_data->ms_enabled) {
			sirfsoc_enable_ms(port);
		}
	} else if (sirfsoc_data->ms_enabled) {
		sirfsoc_disable_ms(port);
	}

	/* Setup clock divider */
	config_reg |=
		RECV_TIMEOUT((UART_RX_TIMEOUT(baudrate, pdata->rx_timeout_in_us) >
				0xFFFF) ? 0xFFFF : UART_RX_TIMEOUT(baudrate,
				pdata->
				rx_timeout_in_us));

	/* Stop FIFOs while changing the configurations. The FIFO status will be restored below.
	 * Rx is always started after completing the terminal settings. */
	tx_fifo_stat = readl(port->membase + UART_TX_FIFO_OP);

	writel(0, port->membase + UART_RX_FIFO_OP);
	writel(tx_fifo_stat & ~UART_TX_FIFO_START,
		port->membase + UART_TX_FIFO_OP);

	writel(UART_TX_IO_MODE, port->membase + UART_TX_DMA_IO_CTRL);

	writel(UART_RX_IO_MODE, port->membase + UART_RX_DMA_IO_CTRL);

	/* write the config */
	writel(config_reg, port->membase + UART_LINE_CTRL);

	/* Reset Rx/Tx FIFO Threshold level for proper baudrate */
	if (baudrate < 1000000)
		threshold_div = 1;
	else
		threshold_div = 2;

	writel((UART_TX_FIFO_THRESHOLD(pdata) /
			threshold_div) << UART_TX_FIFO_THD_OFFSET,
		port->membase + UART_TX_FIFO_CTRL);
	writel((UART_RX_FIFO_THRESHOLD(pdata) /
			threshold_div) << UART_RX_FIFO_THD_OFFSET,
		port->membase + UART_RX_FIFO_CTRL);

	/* Restore the Tx FIFO status */
	tx_fifo_stat |= UART_TX_FIFO_START;
	writel(tx_fifo_stat, port->membase + UART_TX_FIFO_OP);

	uart_update_timeout(port, termios->c_cflag, baudrate);

	/* enable/start RX */
	sirfsoc_start_rx(port);

	/* Enable Rx, Tx */
	writel(UART_TX_ENA | UART_RX_ENA, port->membase + UART_TX_RX_ENABLE);

	spin_unlock_irqrestore(&port->lock, flags);
}

static void startup_uart_controller(struct uart_port *port)
{
	struct sirfsoc_uart_pdata *pdata =
		((struct sirfsoc_uart_drv *)port->private_data)->pdata;

	/* Configure it for RISC mode: default is RISC, so no need to set */
	writel(readl(port->membase + UART_RISC_DSP_MODE) & ~UART_RISC_DSP_SEL,
		port->membase + UART_RISC_DSP_MODE);

	/* Set Data xfer mode to IO */
	writel(readl(port->membase + UART_TX_DMA_IO_CTRL) | UART_TX_MODE_IO,
		port->membase + UART_TX_DMA_IO_CTRL);
	/* Should we not use UART_RX_DMA_FLUSH flag as well?? */
	writel(readl(port->membase + UART_RX_DMA_IO_CTRL) | UART_RX_MODE_IO,
		port->membase + UART_RX_DMA_IO_CTRL);

	/* Continuous mode */
	writel(0, port->membase + UART_TX_DMA_IO_LEN);
	writel(0, port->membase + UART_RX_DMA_IO_LEN);

	/* Enable RX TX */
	writel(UART_RX_ENA | UART_TX_ENA, port->membase + UART_TX_RX_ENABLE);

	/* Reset FIFOs */
	writel(UART_TX_FIFO_RESET, port->membase + UART_TX_FIFO_OP);
	writel(0, port->membase + UART_TX_FIFO_OP);

	writel(UART_RX_FIFO_RESET, port->membase + UART_RX_FIFO_OP);
	writel(0, port->membase + UART_RX_FIFO_OP);

	/* Setup FIFO threshold */
	writel(UART_TX_FIFO_THRESHOLD(pdata) << UART_TX_FIFO_THD_OFFSET,
		port->membase + UART_TX_FIFO_CTRL);
	writel(UART_RX_FIFO_THRESHOLD(pdata) << UART_RX_FIFO_THD_OFFSET,
		port->membase + UART_RX_FIFO_CTRL);
}

static int sirfsoc_startup(struct uart_port *port)
{
	int err;
	struct sirfsoc_uart_drv *drv_data =
		(struct sirfsoc_uart_drv *)port->private_data;

	set_irq_flags(port->irq, IRQF_VALID | IRQF_NOAUTOEN);

	err =
		request_irq(port->irq, sirfsoc_uart_isr, IRQF_DISABLED,
			UART_PORT_NAME, port);
	if (err) {
		printk(KERN_ERR "SiRFSoC UART%d: Request IRQ failed\n",
			port->line);
		goto out;
	}

	startup_uart_controller(port);

	/* enable irq now */
	enable_irq(port->irq);

	return 0;

out3:
out2:
	free_irq(port->irq, port);

out:
	return err;
}

static void sirfsoc_shutdown(struct uart_port *port)
{
	struct sirfsoc_uart_drv *drv_data =
		(struct sirfsoc_uart_drv *)port->private_data;

	writel(0, port->membase + UART_INT_ENABLE);

	free_irq(port->irq, port);

	if (drv_data->ms_enabled) {
		sirfsoc_disable_ms(port);
		drv_data->ms_enabled = 0;
	}
}

static const char *sirfsoc_type(struct uart_port *port)
{
	return port->type == PORT_SIRFSOC ? UART_PORT_NAME : NULL;
}

#define MAP_SIZE (0x200)

static int sirfsoc_request_port(struct uart_port *port)
{
	return request_mem_region(port->mapbase, MAP_SIZE,
		UART_PORT_NAME) ? 0 : -EBUSY;
}

static void sirfsoc_release_port(struct uart_port *port)
{
	release_mem_region(port->mapbase, MAP_SIZE);
}

static void sirfsoc_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE) {
		port->type = PORT_SIRFSOC;
		sirfsoc_request_port(port);
	}
}

#ifdef CONFIG_CONSOLE_POLL
/*
 * Console polling routines for writing and reading from the uart while
 * in an interrupt or debug context.
 */

static int sirfsoc_get_poll_char(struct uart_port *port)
{
	struct sirfsoc_uart_pdata *pdata =
		((struct sirfsoc_uart_drv *)port->private_data)->pdata;

	while ((readl(port->membase + UART_RX_FIFO_STATUS) & pdata->
			fifo_empty_mask))
		continue;	/* Wait while the RX FIFO is empty */

	return readl(port->membase + UART_RX_FIFO_DATA);	/* Read character */
}

static void sirfsoc_put_poll_char(struct uart_port *port, unsigned char ch)
{
	struct sirfsoc_uart_pdata *pdata =
		((struct sirfsoc_uart_drv *)port->private_data)->pdata;

	while ((readl(port->membase + UART_TX_FIFO_STATUS) & pdata->
			fifo_full_mask))
		continue;	/* Wait while the TX FIFO is full */

	writel(ch, port->membase + UART_TX_FIFO_DATA);	/* Write character */
	/*
	 *      Send the character out.
	 *      If a LF, also do CR...
	 */
	if (ch == 10) {
		while ((readl(port->membase + UART_TX_FIFO_STATUS) & pdata->
				fifo_full_mask))
			continue;	/* Wait while the TX FIFO is full */
		writel(13, port->membase + UART_TX_FIFO_DATA);	/* Write CR */
	}
}
#endif
static irqreturn_t cts_handler(int irq, void *pvcontext)
{
	struct uart_port *port = (struct uart_port *)pvcontext;
	struct sirfsoc_uart_drv *pdb =
		(struct sirfsoc_uart_drv *)port->private_data;
	struct sirfsoc_uart_pdata *pdata = pdb->pdata;
	int cts_value = CTS_ASSERTED(pdata->cts_gpio);

	/* DEBUG_LOG(KERN_DEBUG "CTS IRQ %d\n", cts_value); */
	if (cts_value) {
		pdb->cts_assertion_cnt++;
	} else {
		/* Stop the Tx FIFO from sending any more data immediately */
		writel(0, port->membase + UART_TX_FIFO_OP);
		pdb->cts_deassertion_cnt++;
	}
	uart_handle_cts_change(port, cts_value);

	return IRQ_HANDLED;
}

static ssize_t sirfsoc_dump_counters(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct uart_port *port = dev_get_drvdata(dev);
	struct sirfsoc_uart_drv *pdb =
		(struct sirfsoc_uart_drv *)port->private_data;
	int ret;

	ret =
		sprintf(buf,
			"tty-buf-oflow:%d, rx-fifo-oflow:%d, frm-err:%d, Tx:%d, "
			"Rx:%d, cts-assertion:%d, cts-deassertion:%d parity_err_cnt:%d\n",
			port->icount.buf_overrun, port->icount.overrun,
			port->icount.frame, port->icount.tx, port->icount.rx,
			pdb->cts_assertion_cnt, pdb->cts_deassertion_cnt,
			pdb->parity_err_cnt);

	return ret;
}

static ssize_t sirfsoc_store_counters(struct device *dev,
	struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct uart_port *port = dev_get_drvdata(dev);
	struct sirfsoc_uart_drv *pdb =
		(struct sirfsoc_uart_drv *)port->private_data;
	port->icount.buf_overrun = 0;
	port->icount.overrun = 0;
	port->icount.frame = 0;
	port->icount.tx = 0;
	port->icount.rx = 0;
	pdb->cts_assertion_cnt = 0;
	pdb->cts_deassertion_cnt = 0;
	pdb->parity_err_cnt = 0;
	return count;

}

static ssize_t sirfsoc_show_loopback(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int ret;
	struct uart_port *port = dev_get_drvdata(dev);
	struct sirfsoc_uart_drv *pdb =
		(struct sirfsoc_uart_drv *)port->private_data;

	ret = sprintf(buf, "%d", pdb->loopback);
	return ret;
}

static ssize_t sirfsoc_store_loopback(struct device *dev,
	struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct uart_port *port = dev_get_drvdata(dev);
	struct sirfsoc_uart_drv *pdb =
		(struct sirfsoc_uart_drv *)port->private_data;
	pdb->loopback = simple_strtol(buf, NULL, 10);
	return count;
}

static DEVICE_ATTR(counters, S_IRUGO | S_IWUGO, sirfsoc_dump_counters,
	sirfsoc_store_counters);
static DEVICE_ATTR(loopback, S_IRUGO | S_IWUGO, sirfsoc_show_loopback,
	sirfsoc_store_loopback);

static struct uart_ops sirfsoc_ops = {
	.tx_empty = sirfsoc_tx_empty,
	.set_mctrl = sirfsoc_set_mctrl,
	.get_mctrl = sirfsoc_get_mctrl,
	.stop_tx = sirfsoc_stop_tx,
	.start_tx = sirfsoc_start_tx,
	.stop_rx = sirfsoc_stop_rx,
	.enable_ms = sirfsoc_enable_ms,
	.break_ctl = sirfsoc_break_ctl,
	.startup = sirfsoc_startup,
	.shutdown = sirfsoc_shutdown,
	.set_termios = sirfsoc_set_termios,
	.type = sirfsoc_type,
	.release_port = sirfsoc_release_port,
	.request_port = sirfsoc_request_port,
	.config_port = sirfsoc_config_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_get_char = sirfsoc_get_poll_char,
	.poll_put_char = sirfsoc_put_poll_char,
#endif
};

static struct uart_driver sirfsoc_port_driver;

/*
 * Configure the port from the platform device resource info.
 */
static int sirfsoc_init_port(struct sirfsoc_uart_drv *pdb,
	struct platform_device *pdev)
{
	struct sirfsoc_uart_pdata *pdata = pdev->dev.platform_data;
	struct uart_port *port = &pdb->port;

	port->mapbase = pdev->resource[0].start;
	port->irq = pdev->resource[1].start;
	port->membase = (unsigned char __iomem *)pdata->va_addr;

	port->iotype = SERIAL_IO_MEM;
	port->fifosize = pdata->fifo_size;
	port->ops = &sirfsoc_ops;
	port->line = pdev->id;
	port->flags = ASYNC_BOOT_AUTOCONF;
	port->dev = &pdev->dev;

	pdb->pdata = pdata;
	pdb->pdev = pdev;
	port->private_data = pdb;

	pdb->tx_dma_chan = pdata->tx_dma_chan;
	pdb->rx_dma_chan = pdata->rx_dma_chan;
	pdb->tx_dma_width_reg = pdata->tx_dma_width_reg;
	pdb->rx_dma_width_reg = pdata->rx_dma_width_reg;

	pdb->rx_intr_mask = 0;

	if (pdata->rx_dma_chan != -1)
		pdb->rx_intr_mask |= UART_RX_INTRS_DMA;
	else
		pdb->rx_intr_mask |= UART_RX_INTRS;

	return 0;
}

#ifdef CONFIG_SERIAL_SIRFSOC_CONSOLE

static int __init sirfsoc_serial_console_setup(struct console *co,
	char *options)
{
	s32 baud = 115200;
	s32 bits = 8;
	s32 parity = 'n';
	s32 flow = 'n';

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index >= CONFIG_SIRFSOC_UART_NR)
		co->index = 0;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	sirfsoc_serial_ports[co->index].port.cons = co;
	return uart_set_options(&sirfsoc_serial_ports[co->index].port, co, baud,
		parity, bits, flow);
}

static void sirfsoc_serial_console_putchar(struct uart_port *port, int ch)
{
	struct sirfsoc_uart_pdata *pdata =
		((struct sirfsoc_uart_drv *)port->private_data)->pdata;

	while ((readl(port->membase + UART_TX_FIFO_STATUS) & pdata->
			fifo_full_mask))
		continue;	/* Wait while the TX FIFO is full */

	writel(ch, port->membase + UART_TX_FIFO_DATA);	/* Write character */
}

static void
sirfsoc_serial_console_write(struct console *co, const char *s,
	unsigned int count)
{
	struct uart_port *port = &sirfsoc_serial_ports[co->index].port;
	uart_console_write(port, s, count, sirfsoc_serial_console_putchar);
}

static struct console sirfsoc_serial_console = {
	.name = "ttyS",
	.device = uart_console_device,
	.flags = CON_PRINTBUFFER,
	.index = -1,
	.write = sirfsoc_serial_console_write,
	.setup = sirfsoc_serial_console_setup,
	.data = &sirfsoc_port_driver,
};

static int __init sirfsoc_serial_console_init(void)
{
	printk("%s sirfsoc_default_console_device->id:%d\n", __func__, sirfsoc_default_console_device->id);

	if (sirfsoc_default_console_device) {
		sirfsoc_serial1_pdata.va_addr = ioremap(SIRFSOC_UART1_PA_BASE,
			SIRFSOC_UART1_SIZE);
		add_preferred_console(sirfsoc_serial_console.name,
			sirfsoc_default_console_device->id, NULL);
		sirfsoc_init_port(&sirfsoc_serial_ports
			[sirfsoc_default_console_device->id],
			sirfsoc_default_console_device);
		register_console(&sirfsoc_serial_console);
	}
	return 0;
}

console_initcall(sirfsoc_serial_console_init);
static inline bool sirfsoc_is_console_port(struct uart_port *port)
{
	return port->cons && port->cons->index == port->line;
}
#else
static inline bool sirfsoc_is_console_port(struct uart_port *port)
{
	return false;
}
#endif				/*CONFIG_SERIAL_SIRFSOC_CONSOLE */

static struct uart_driver sirfsoc_port_driver = {
	.owner = THIS_MODULE,
	.driver_name = UART_PORT_NAME,
	.dev_name = "ttyS",
	.major = TTY_MAJOR,
	.minor = 64,
	.nr = CONFIG_SIRFSOC_UART_NR,
#ifdef CONFIG_SERIAL_SIRFSOC_CONSOLE
	.cons = &sirfsoc_serial_console,
#else
	.cons = NULL,
#endif
};

static int sirfsoc_serial_probe(struct platform_device *pdev)
{
	struct sirfsoc_uart_pdata *pdata;
	struct sirfsoc_uart_drv *pdb;
	struct uart_port *port;
	struct resource *r_mem;
	int err = 0;
	static u32 line;

#ifndef CONFIG_OF
	/* using platform data from board files */
	pdata = pdev->dev.platform_data;
	pdb = &sirfsoc_serial_ports[pdev->id];
	port = &pdb->port;
#else
	/* using platform data from device tree */
	const unsigned int *prop = NULL;

	pdata = (struct sirfsoc_uart_pdata *)kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (pdata == NULL)
		return -ENOMEM;
	pdev->dev.platform_data = pdata;

	prop = of_get_property(pdev->dev.of_node, "fifo_size", NULL);
	if (!prop) {
		kfree(pdata);
		return -ENODEV;
	}
	pdata->fifo_size = be32_to_cpup(prop);

	prop = of_get_property(pdev->dev.of_node, "fifo_level_mask", NULL);
	if (!prop) {
		kfree(pdata);
		return -ENODEV;
	}
	pdata->fifo_level_mask = be32_to_cpup(prop);

	prop = of_get_property(pdev->dev.of_node, "fifo_full_mask", NULL);
	if (!prop) {
		kfree(pdata);
		return -ENODEV;
	}
	pdata->fifo_full_mask = be32_to_cpup(prop);

	prop = of_get_property(pdev->dev.of_node, "fifo_empty_mask", NULL);
	if (!prop) {
		kfree(pdata);
		return -ENODEV;
	}
	pdata->fifo_empty_mask = be32_to_cpup(prop);

	prop = of_get_property(pdev->dev.of_node, "rx_threshold", NULL);
	if (!prop) {
		kfree(pdata);
		return -ENODEV;
	}
	pdata->rx_threshold = be32_to_cpup(prop);

	prop = of_get_property(pdev->dev.of_node, "rx_timeout_in_us", NULL);
	if (!prop) {
		kfree(pdata);
		return -ENODEV;
	}
	pdata->rx_timeout_in_us = be32_to_cpup(prop);

	prop = of_get_property(pdev->dev.of_node, "max_baudrate_in_bps", NULL);
	if (!prop) {
		kfree(pdata);
		return -ENODEV;
	}
	pdata->max_baudrate_in_bps = be32_to_cpup(prop);

	prop = of_get_property(pdev->dev.of_node, "tx_dma_chan", NULL);
	if (!prop) {
		kfree(pdata);
		return -ENODEV;
	}
	pdata->tx_dma_chan = be32_to_cpup(prop);

	prop = of_get_property(pdev->dev.of_node, "rx_dma_chan", NULL);
	if (!prop) {
		kfree(pdata);
		return -ENODEV;
	}
	pdata->rx_dma_chan = be32_to_cpup(prop);

	prop = of_get_property(pdev->dev.of_node, "tx_dma_width_reg", NULL);
	if (!prop) {
		kfree(pdata);
		return -ENODEV;
	}
	pdata->tx_dma_width_reg = be32_to_cpup(prop);

	prop = of_get_property(pdev->dev.of_node, "rx_dma_width_reg", NULL);
	if (!prop) {
		kfree(pdata);
		return -ENODEV;
	}
	pdata->rx_dma_width_reg = be32_to_cpup(prop);

	prop = of_get_property(pdev->dev.of_node, "hw_flow_control_enabled", NULL);
	if (!prop) {
		kfree(pdata);
		return -ENODEV;
	}
	pdata->hw_flow_control_enabled = be32_to_cpup(prop);

	prop = of_get_property(pdev->dev.of_node, "rts_gpio", NULL);
	if (!prop) {
		kfree(pdata);
		return -ENODEV;
	}
	pdata->rts_gpio = be32_to_cpup(prop);

	prop = of_get_property(pdev->dev.of_node, "cts_gpio", NULL);
	if (!prop) {
		kfree(pdata);
		return -ENODEV;
	}
	pdata->cts_gpio = be32_to_cpup(prop);

	prop = of_get_property(pdev->dev.of_node, "platform_id", NULL);
	if (!prop) {
		kfree(pdata);
		return -ENODEV;
	}
	pdev->id = be32_to_cpup(prop);

	prop = of_get_property(pdev->dev.of_node, "clock", NULL);
	if (!prop) {
		kfree(pdata);
		return -ENODEV;
	}
	pdata->clock = (char *)prop;

	pdb = &sirfsoc_serial_ports[pdev->id];
	port = &pdb->port;
#endif

	if (sirfsoc_init_port(pdb, pdev)) {
		dev_err(&pdev->dev, "SIRFSOC_SPI: Failed to init port\n");
		return -EFAULT;
	}

	r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (r_mem == NULL) {
		printk("SiRFSOC UART: Insufficient resources\n");
		err = -EFAULT;
		goto out2;
	}

	port->mapbase = r_mem->start;
	port->membase = ioremap(r_mem->start, r_mem->end - r_mem->start + 1);
	if (!port->membase) {
		printk("SiRFSOC UART: Cannot remap resource\n");
		err = -ENOMEM;
		goto out2;
	}

	if (pdev->id == 1) {
		/*
		 * fixme: for the moment it is only for testing pinmux API
		 * refine it later
		 */
		struct pinmux *pmx = pinmux_get(&pdev->dev, NULL);
		pinmux_enable(pmx);
	}

	port->irq = platform_get_irq(pdev, 0);

	if (port->irq == 0) {
		printk("SiRFSOC UART: Insufficient resources\n");
		err = -EFAULT;
		goto out3;
	}

	platform_set_drvdata(pdev, port);

	if ((pdata->hw_flow_control_enabled == 1) &&
		((pdata->rts_gpio != -1) || (pdata->cts_gpio != -1))) {
		printk(KERN_ERR
			"SiRFSOC UART%d: Hw & Sw Flow control cannot be enabled on the same UART\n",
			line);
		goto out3;
	}

	if ((pdata->rts_gpio != -1) && (pdata->cts_gpio != -1)) {
		err = gpio_request(pdata->cts_gpio, NULL);
		if (err) {
			printk(KERN_ERR "%s: CTS %d GPIO Request failed\n",
				__func__, pdata->cts_gpio);
			goto out3;
		} else {
			err = gpio_request(pdata->rts_gpio, NULL);
			if (err) {
				printk(KERN_ERR
					"%s: RTS %d GPIO Request failed\n",
					__func__, pdata->rts_gpio);
				goto out4;
			} else {
				DEBUG_LOG(KERN_DEBUG
					"%s: RTS-%d GPIO is registered\n",
					__func__, pdata->rts_gpio);
			}
		}
		gpio_direction_output(pdata->rts_gpio, 1);
		gpio_direction_input(pdata->cts_gpio);
	} else {
		/* Make sure both values are -1 */
		pdata->rts_gpio = -1;
		pdata->cts_gpio = -1;
	}

	if (((pdata->tx_dma_chan != -1) || (pdata->rx_dma_chan != -1)) &&
		((pdata->rts_gpio != -1) || (pdata->cts_gpio != -1))) {
		err = -ENODEV;
		printk(KERN_ERR
			"SiRFSOC UART%d: Flow control and DMA not supported on the same UART\n",
			line);
		goto out6;
	}

	if (pdb->rx_dma_chan != -1) {
		pdb->rx_buf_cpu_addr =
			dma_alloc_coherent(&pdev->dev, RX_DMA_BUF_SIZE,
				&pdb->rx_dma_buf, GFP_KERNEL);
		if (!pdb->rx_buf_cpu_addr) {
			printk(KERN_ERR
				"SiRFSOC UART%d: Cant allocate RX buffer\n",
				line);
			err = -ENOMEM;
			goto out5;
		}
	}

	err = uart_add_one_port(&sirfsoc_port_driver, port);
	if (err) {
		platform_set_drvdata(pdev, NULL);
		goto out6;
	}

	if (device_create_file(&(pdev->dev), &dev_attr_counters) < 0) {
		printk(KERN_ERR
			"SiRFSOC UART: Error in Creating sys attribute\n");
	}
	if (device_create_file(&(pdev->dev), &dev_attr_loopback) < 0) {
		printk(KERN_ERR
			"SiRFSOC UART: Error in Creating sys attribute\n");
	}

	/* Init serial rx tasklet */
	tasklet_init(&pdb->serial_rx_dma_tasklet, sirfsoc_serial_rx_dma_irq_tl,
		(unsigned long)pdb);
	return 0;

out6:
	if (pdb->rx_dma_chan != -1)
		dma_free_coherent(&pdev->dev, RX_DMA_BUF_SIZE,
			pdb->rx_buf_cpu_addr, pdb->rx_dma_buf);
out5:
	if (pdata->rts_gpio != -1)
		gpio_free(pdata->rts_gpio);
out4:
	if (pdata->cts_gpio != -1)
		gpio_free(pdata->cts_gpio);
out3:
	iounmap(port->membase);
out2:
#ifdef CONFIG_OF
	kfree(pdata);
#endif
	return err;

}

static int sirfsoc_serial_remove(struct platform_device *pdev)
{
	struct uart_port *port = platform_get_drvdata(pdev);
	struct sirfsoc_uart_drv *pdb;
	struct sirfsoc_uart_pdata *pdata;

	if (port == NULL) {
		printk(KERN_WARNING "Serial: Port pointer is NULL\n");
		return -EINVAL;
	}

	pdb = (struct sirfsoc_uart_drv *)port->private_data;
	pdata = pdb->pdata;

	if (pdb->rx_dma_chan != -1)
		dma_free_coherent(&pdev->dev, RX_DMA_BUF_SIZE,
			pdb->rx_buf_cpu_addr, pdb->rx_dma_buf);

	device_remove_file(&(pdev->dev), &dev_attr_counters);
	device_remove_file(&(pdev->dev), &dev_attr_loopback);

	if (pdata->cts_gpio != -1)
		gpio_free(pdata->cts_gpio);
	if (pdata->rts_gpio != -1)
		gpio_free(pdata->rts_gpio);

	platform_set_drvdata(pdev, NULL);
	/* it is allocated statically, so do not need to free port */
	uart_remove_one_port(&sirfsoc_port_driver, port);
	iounmap(port->membase);

#ifdef CONFIG_OF
	kfree(pdata);
#endif
	return 0;
}

#ifdef CONFIG_PM
static int sirfsoc_serial_suspend(struct platform_device *pdev,
	pm_message_t msg)
{
	struct uart_port *port = platform_get_drvdata(pdev);
	struct sirfsoc_uart_drv *pdb;

	if (port == NULL) {
		printk(KERN_WARNING "Serial-UART: Port pointer is NULL\n");
		return -EINVAL;
	}

	pdb = (struct sirfsoc_uart_drv *)port->private_data;
	if (port)
		uart_suspend_port(&sirfsoc_port_driver, port);
	return 0;
}

static int sirfsoc_serial_resume(struct platform_device *pdev)
{
	struct uart_port *port = platform_get_drvdata(pdev);
	struct sirfsoc_uart_drv *pdb;

	if (port == NULL) {
		printk(KERN_WARNING "Serial-UART: Port pointer is NULL\n");
		return -EINVAL;
	}

	pdb = (struct sirfsoc_uart_drv *)port->private_data;
	if (port)
		uart_resume_port(&sirfsoc_port_driver, port);

	return 0;
}
#else
#define sirfsoc_serial_suspend NULL
#define sirfsoc_serial_resume NULL
#endif

#ifdef CONFIG_OF
static struct of_device_id sirfsoc_serial_of_match[] __devinitdata = {
	{ .compatible = "sirf,prima2-uart", },
	{}
};
MODULE_DEVICE_TABLE(of, sirfsoc_serial_of_match);
#else
#define sirfsoc_serial_of_match NULL
#endif

static struct platform_driver sirfsoc_serial_driver = {
	.probe = sirfsoc_serial_probe,
	.remove = sirfsoc_serial_remove,
	.suspend = sirfsoc_serial_suspend,
	.resume = sirfsoc_serial_resume,
	.driver = {
		.name = "sirfsoc-uart",
		.owner = THIS_MODULE,
		.of_match_table = sirfsoc_serial_of_match,
	},
};

static int __init sirfsoc_serial_init(void)
{
	int err = 0;

	err = uart_register_driver(&sirfsoc_port_driver);
	if (err)
		goto out;

	err = platform_driver_register(&sirfsoc_serial_driver);
	if (err)
		uart_unregister_driver(&sirfsoc_port_driver);
out:
	return err;
}

static void __exit sirfsoc_serial_exit(void)
{
	platform_driver_unregister(&sirfsoc_serial_driver);
	uart_unregister_driver(&sirfsoc_port_driver);
}

module_init(sirfsoc_serial_init);
module_exit(sirfsoc_serial_exit);

MODULE_LICENSE("GPL");
