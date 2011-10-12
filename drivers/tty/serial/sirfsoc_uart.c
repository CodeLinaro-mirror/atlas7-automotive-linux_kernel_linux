/*
 * Driver for CSR SiRFprimaII onboard UARTs.
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
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
#include <linux/io.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <mach/hardware.h>
#include <linux/pinctrl/pinmux.h>

#include "sirfsoc_uart.h"

static unsigned int
sirfsoc_uart_pio_tx_chars(struct sirfsoc_uart_port *sirfport, int count);
static unsigned int
sirfsoc_uart_pio_rx_chars(struct uart_port *port, unsigned int max_rx_count);
static struct uart_driver sirfsoc_uart_drv;

static inline struct sirfsoc_uart_port *to_sirfport(struct uart_port *port)
{
	return container_of(port, struct sirfsoc_uart_port, port);
}

static inline unsigned int sirfsoc_uart_tx_empty(struct uart_port *port)
{
	unsigned long reg;
	reg = rd_regl(port, SIRFUART_TX_FIFO_STATUS);
	if (reg & SIRFUART_FIFOEMPTY_MASK(port))
		return TIOCSER_TEMT;
	else
		return 0;
}

static unsigned int sirfsoc_uart_get_mctrl(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	if (!(sirfport->ms_enabled)) {
		goto cts_asserted;
	} else if (sirfport->hw_flow_ctrl) {
		if (!(rd_regl(port, SIRFUART_AFC_CTRL) &
						SIRFUART_CTS_IN_STATUS))
			goto cts_asserted;
		else
			goto cts_deasserted;
	}
cts_deasserted:
	return TIOCM_CAR | TIOCM_DSR;
cts_asserted:
	return TIOCM_CAR | TIOCM_DSR | TIOCM_CTS;
}

static void sirfsoc_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	unsigned int assert = mctrl & TIOCM_RTS;
	unsigned int val = (assert) ? SIRFUART_AFC_CTRL_RX_THD : 0x0;
	unsigned int current_val;
	if (sirfport->hw_flow_ctrl) {
		current_val = rd_regl(port, SIRFUART_AFC_CTRL) & ~(0xFF);
		val |= current_val;
		wr_regl(port, SIRFUART_AFC_CTRL, val);
	}
}

static void sirfsoc_uart_stop_tx(struct uart_port *port)
{
	unsigned int regv;
	regv = rd_regl(port, SIRFUART_INT_EN);
	wr_regl(port, SIRFUART_INT_EN, regv & ~SIRFUART_TX_INT_EN);
}

void sirfsoc_uart_start_tx(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	unsigned long regv;
	sirfsoc_uart_pio_tx_chars(sirfport, 1);
	wr_regl(port, SIRFUART_TX_FIFO_OP, SIRFUART_TX_FIFO_START);
	regv = rd_regl(port, SIRFUART_INT_EN);
	wr_regl(port, SIRFUART_INT_EN, (regv | SIRFUART_TX_INT_EN));
}

static void sirfsoc_uart_stop_rx(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	unsigned long regv;
	wr_regl(port, SIRFUART_RX_FIFO_OP, 0);
	regv = rd_regl(port, SIRFUART_INT_EN);
	wr_regl(port, SIRFUART_INT_EN, (regv & ~(sirfport->rx_intr_mask)));
}

static void sirfsoc_uart_disable_ms(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	unsigned long reg;
	sirfport->ms_enabled = 0;
	if (!sirfport->hw_flow_ctrl)
		return;
	reg = rd_regl(port, SIRFUART_AFC_CTRL);
	wr_regl(port, SIRFUART_AFC_CTRL, (reg & ~(0x3FF)));
	reg = rd_regl(port, SIRFUART_INT_EN);
	wr_regl(port, SIRFUART_INT_EN, (reg & ~SIRFUART_CTS_INT_EN));
}

static void sirfsoc_uart_enable_ms(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	unsigned long reg;
	unsigned long flg;
	if (!sirfport->hw_flow_ctrl)
		return;
	flg = SIRFUART_AFC_RX_EN | SIRFUART_AFC_TX_EN;
	reg = rd_regl(port, SIRFUART_AFC_CTRL);
	wr_regl(port, SIRFUART_AFC_CTRL, (reg | flg));
	reg = rd_regl(port, SIRFUART_INT_EN);
	wr_regl(port, SIRFUART_INT_EN, (reg | SIRFUART_CTS_INT_EN));
	uart_handle_cts_change(port,
		!(rd_regl(port, SIRFUART_AFC_CTRL) & SIRFUART_CTS_IN_STATUS));
	sirfport->ms_enabled = 1;
}

static void sirfsoc_uart_break_ctl(struct uart_port *port, int break_state)
{
	unsigned long ulcon = rd_regl(port, SIRFUART_LINE_CTRL);
	if (break_state)
		ulcon |= SIRFUART_SET_BREAK;
	else
		ulcon &= ~SIRFUART_SET_BREAK;
	wr_regl(port, SIRFUART_LINE_CTRL, ulcon);
}

static unsigned int
sirfsoc_uart_pio_rx_chars(struct uart_port *port, unsigned int max_rx_count)
{
	unsigned int ch, rx_count = 0;
	int temp;
	struct tty_struct *tty = port->state->port.tty;
	while (!(rd_regl(port, SIRFUART_RX_FIFO_STATUS) &
					SIRFUART_FIFOEMPTY_MASK(port))) {
		temp = tty_buffer_request_room(port->state->port.tty, 1);
		if (unlikely(temp == 0)) {
			port->icount.buf_overrun++;
			break;
		}
		ch = rd_regl(port, SIRFUART_RX_FIFO_DATA) | SIRFUART_DUMMY_READ;
		if (unlikely(uart_handle_sysrq_char(port, ch)))
			continue;
		uart_insert_char(port, 0, 0, ch, TTY_NORMAL);
		rx_count++;
		if (rx_count >= max_rx_count)
			break;
	}
	port->icount.rx += rx_count;
	tty_flip_buffer_push(tty);
	return rx_count;
}

static unsigned int
sirfsoc_uart_pio_tx_chars(struct sirfsoc_uart_port *sirfport, int count)
{
	struct uart_port *port = &sirfport->port;
	struct circ_buf *xmit = &port->state->xmit;
	unsigned int num_tx = 0;
	while (!uart_circ_empty(xmit)
		&&
		!(rd_regl(port, SIRFUART_TX_FIFO_STATUS) &
					SIRFUART_FIFOFULL_MASK(port))
		&&
		count--) {
		wr_regl(port, SIRFUART_TX_FIFO_DATA, xmit->buf[xmit->tail]);
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
	unsigned long intr_status;
	unsigned long cts_status;
	unsigned long flag = TTY_NORMAL;
	struct sirfsoc_uart_port *sirfport = (struct sirfsoc_uart_port *)dev_id;
	struct uart_port *port = &sirfport->port;
	struct uart_state *state = port->state;
	struct circ_buf *xmit = &port->state->xmit;
	intr_status = rd_regl(port, SIRFUART_INT_STATUS);
	wr_regl(port, SIRFUART_INT_STATUS, intr_status);
	intr_status &= rd_regl(port, SIRFUART_INT_EN);
	if (unlikely(intr_status & (SIRFUART_ERR_INT_STAT))) {
		if (intr_status & SIRFUART_RXD_BREAK) {
			if (uart_handle_break(port))
				goto recv_char;
			uart_insert_char(port, intr_status,
					SIRFUART_RX_OFLOW, 0, TTY_BREAK);
			return IRQ_HANDLED;
		}
		if (intr_status & SIRFUART_RX_OFLOW)
			port->icount.overrun++;
		if (intr_status & SIRFUART_FRM_ERR) {
			port->icount.frame++;
			flag = TTY_FRAME;
		}
		if (intr_status & SIRFUART_PARITY_ERR)
			flag = TTY_PARITY;
		wr_regl(port, SIRFUART_RX_FIFO_OP, SIRFUART_RX_FIFO_RESET);
		wr_regl(port, SIRFUART_RX_FIFO_OP, 0);
		wr_regl(port, SIRFUART_RX_FIFO_OP, SIRFUART_RX_FIFO_START);
		intr_status &= port->read_status_mask;
		uart_insert_char(port, intr_status,
					SIRFUART_RX_OFLOW_INT, 0, flag);
	}
recv_char:
	if (intr_status & SIRFUART_CTS_INT_EN) {
		cts_status = !(rd_regl(port, SIRFUART_AFC_CTRL) &
							SIRFUART_CTS_IN_STATUS);
		if (cts_status != 0) {
			uart_handle_cts_change(port, 1);
		} else {
			uart_handle_cts_change(port, 0);
			wake_up_interruptible(&state->port.delta_msr_wait);
		}
	}
	if (intr_status & SIRFUART_RX_IO_INT_EN)
		sirfsoc_uart_pio_rx_chars(port, SIRFSOC_UART_IO_RX_MAX_CNT);
	if (intr_status & SIRFUART_TX_INT_EN) {
		if (!(uart_tx_port_tty_invalid(port) ||
						uart_tx_stopped(port))) {
			sirfsoc_uart_pio_tx_chars(sirfport,
					SIRFSOC_UART_IO_TX_REASONABLE_CNT);
			if ((uart_circ_empty(xmit)) &&
				(rd_regl(port, SIRFUART_TX_FIFO_STATUS) &
						SIRFUART_FIFOEMPTY_MASK(port)))
				sirfsoc_uart_stop_tx(port);
		}
	}
	return IRQ_HANDLED;
}

static void sirfsoc_uart_start_rx(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	unsigned long regv;
	regv = rd_regl(port, SIRFUART_INT_EN);
	wr_regl(port, SIRFUART_INT_EN, regv | sirfport->rx_intr_mask);
	wr_regl(port, SIRFUART_RX_FIFO_OP, SIRFUART_RX_FIFO_RESET);
	wr_regl(port, SIRFUART_RX_FIFO_OP, 0);
	wr_regl(port, SIRFUART_RX_FIFO_OP, SIRFUART_RX_FIFO_START);
}

static void sirfsoc_uart_set_termios(struct uart_port *port,
				       struct ktermios *termios,
				       struct ktermios *old)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	unsigned long	ioclk_rate;
	unsigned long	config_reg = 0;
	unsigned long	baud_rate;
	unsigned long	flags;
	unsigned long	ic;
	unsigned int	clk_div_reg = 0;
	unsigned long	temp_reg_val;
	unsigned long	rx_time_out;
	int		threshold_div;
	int		temp;

	ioclk_rate = 150000000;
	switch (termios->c_cflag & CSIZE) {
	default:
	case CS8:
		config_reg |= SIRFUART_DATA_BIT_LEN_8;
		break;
	case CS7:
		config_reg |= SIRFUART_DATA_BIT_LEN_7;
		break;
	case CS6:
		config_reg |= SIRFUART_DATA_BIT_LEN_6;
		break;
	case CS5:
		config_reg |= SIRFUART_DATA_BIT_LEN_5;
		break;
	}
	if (termios->c_cflag & CSTOPB)
		config_reg |= SIRFUART_STOP_BIT_LEN_2;
	baud_rate = uart_get_baud_rate(port, termios, old, 0,
					sirfport->max_baud_rate);
	spin_lock_irqsave(&port->lock, flags);
	port->read_status_mask = SIRFUART_RX_OFLOW_INT;
	port->ignore_status_mask = 0;
	/* read flags */
	if (termios->c_iflag & INPCK)
		port->read_status_mask |=
			(SIRFUART_FRM_ERR_INT | SIRFUART_PARITY_ERR_INT);
	if (termios->c_iflag & (BRKINT | PARMRK))
		port->read_status_mask |= SIRFUART_RXD_BREAK_INT;
	/* ignore flags */
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |=
			(SIRFUART_FRM_ERR_INT | SIRFUART_PARITY_ERR_INT);
	if ((termios->c_cflag & CREAD) == 0)
		port->ignore_status_mask |= SIRFUART_DUMMY_READ;
	/* enable parity if PARENB is set*/
	if (termios->c_cflag & PARENB) {
		if (termios->c_cflag & CMSPAR) {
			if (termios->c_cflag & PARODD)
				config_reg |= SIRFUART_STICK_BIT_MARK;
			else
				config_reg |= SIRFUART_STICK_BIT_SPACE;
		} else if (termios->c_cflag & PARODD) {
			config_reg |= SIRFUART_STICK_BIT_ODD;
		} else {
			config_reg |= SIRFUART_STICK_BIT_EVEN;
		}
	}
	/* Hardware Flow Control Settings */
	if (UART_ENABLE_MS(port, termios->c_cflag)) {
		if (!sirfport->ms_enabled)
			sirfsoc_uart_enable_ms(port);
	} else {
		if (sirfport->ms_enabled)
			sirfsoc_uart_disable_ms(port);
	}
	for (ic = 0; ic < SIRFUART_BAUD_RATE_SUPPORT_NR; ic++)
		if (baud_rate == baud_rates_mapping[ic].baud_rate)
			clk_div_reg = baud_rates_mapping[ic].reg_val;
	if (clk_div_reg == 0)
		pr_err("SiRF UART: Cannot set Baud Rate (9600 ~ 4000000).\n");
	wr_regl(port, SIRFUART_DIVISOR, clk_div_reg);
	/* set receive timeout */
	rx_time_out = SIRFSOC_UART_RX_TIMEOUT(baud_rate,
						sirfport->rx_timeout_in_us);
	rx_time_out = (rx_time_out > 0xFFFF) ? 0xFFFF : rx_time_out;
	config_reg |= SIRFUART_RECV_TIMEOUT(rx_time_out);
	temp_reg_val = rd_regl(port, SIRFUART_TX_FIFO_OP);
	wr_regl(port, SIRFUART_RX_FIFO_OP, 0);
	wr_regl(port, SIRFUART_TX_FIFO_OP,
				temp_reg_val & ~SIRFUART_TX_FIFO_START);
	wr_regl(port, SIRFUART_TX_DMA_IO_CTRL, SIRFUART_TX_MODE_IO);
	wr_regl(port, SIRFUART_RX_DMA_IO_CTRL, SIRFUART_RX_MODE_IO);
	wr_regl(port, SIRFUART_LINE_CTRL, config_reg);
	/* Reset Rx/Tx FIFO Threshold level for proper baudrate */
	if (baud_rate < 1000000)
		threshold_div = 1;
	else
		threshold_div = 2;
	temp = port->line == 1 ? 16 : 64;
	wr_regl(port, SIRFUART_TX_FIFO_CTRL, (temp / threshold_div));
	wr_regl(port, SIRFUART_RX_FIFO_CTRL, (temp / threshold_div));
	temp_reg_val |= SIRFUART_TX_FIFO_START;
	wr_regl(port, SIRFUART_TX_FIFO_OP, temp_reg_val);
	uart_update_timeout(port, termios->c_cflag, baud_rate);
	sirfsoc_uart_start_rx(port);
	wr_regl(port, SIRFUART_TX_RX_EN, (SIRFUART_TX_EN | SIRFUART_RX_EN));
	spin_unlock_irqrestore(&port->lock, flags);
}

static void startup_uart_controller(struct uart_port *port)
{
	unsigned long temp_regv;
	int temp;
	temp_regv = rd_regl(port, SIRFUART_TX_DMA_IO_CTRL);
	wr_regl(port, SIRFUART_TX_DMA_IO_CTRL, temp_regv | SIRFUART_TX_MODE_IO);
	temp_regv = rd_regl(port, SIRFUART_RX_DMA_IO_CTRL);
	wr_regl(port, SIRFUART_RX_DMA_IO_CTRL, temp_regv | SIRFUART_RX_MODE_IO);
	wr_regl(port, SIRFUART_TX_DMA_IO_LEN, 0);
	wr_regl(port, SIRFUART_RX_DMA_IO_LEN, 0);
	wr_regl(port, SIRFUART_TX_RX_EN, SIRFUART_RX_EN | SIRFUART_TX_EN);
	wr_regl(port, SIRFUART_TX_FIFO_OP, SIRFUART_TX_FIFO_RESET);
	wr_regl(port, SIRFUART_TX_FIFO_OP, 0);
	wr_regl(port, SIRFUART_RX_FIFO_OP, SIRFUART_RX_FIFO_RESET);
	wr_regl(port, SIRFUART_RX_FIFO_OP, 0);
	temp = port->line == 1 ? 16 : 64;
	wr_regl(port, SIRFUART_TX_FIFO_CTRL, temp);
	wr_regl(port, SIRFUART_RX_FIFO_CTRL, temp);
}

static int sirfsoc_uart_startup(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport	= to_sirfport(port);
	unsigned int index			= port->line;
	int ret;
	set_irq_flags(port->irq, IRQF_VALID | IRQF_NOAUTOEN);
	ret = request_irq(port->irq,
				sirfsoc_uart_isr,
				0,
				SIRFUART_PORT_NAME,
				sirfport);
	if (ret != 0) {
		dev_err(port->dev, "UART%d request IRQ line (%d) failed.\n",
							index, port->irq);
		goto irq_err;
	}
	startup_uart_controller(port);
	enable_irq(port->irq);
irq_err:
	return ret;
}

static void sirfsoc_uart_shutdown(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	wr_regl(port, SIRFUART_INT_EN, 0);
	free_irq(port->irq, sirfport);
	if (sirfport->ms_enabled) {
		sirfsoc_uart_disable_ms(port);
		sirfport->ms_enabled = 0;
	}
}

static const char *sirfsoc_uart_type(struct uart_port *port)
{
	return port->type == SIRFSOC_PORT_TYPE ? SIRFUART_PORT_NAME : NULL;
}

static int sirfsoc_uart_request_port(struct uart_port *port)
{
	void *ret;
	ret = request_mem_region(port->mapbase,
				SIRFUART_MAP_SIZE, SIRFUART_PORT_NAME);
	return ret ? 0 : -EBUSY;
}

static void sirfsoc_uart_release_port(struct uart_port *port)
{
	release_mem_region(port->mapbase, SIRFUART_MAP_SIZE);
}

static void sirfsoc_uart_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE) {
		port->type = SIRFSOC_PORT_TYPE;
		sirfsoc_uart_request_port(port);
	}
}

static struct uart_ops sirfsoc_uart_ops = {
	.tx_empty	= sirfsoc_uart_tx_empty,
	.get_mctrl	= sirfsoc_uart_get_mctrl,
	.set_mctrl	= sirfsoc_uart_set_mctrl,
	.stop_tx	= sirfsoc_uart_stop_tx,
	.start_tx	= sirfsoc_uart_start_tx,
	.stop_rx	= sirfsoc_uart_stop_rx,
	.enable_ms	= sirfsoc_uart_enable_ms,
	.break_ctl	= sirfsoc_uart_break_ctl,
	.startup	= sirfsoc_uart_startup,
	.shutdown	= sirfsoc_uart_shutdown,
	.set_termios	= sirfsoc_uart_set_termios,
	.type		= sirfsoc_uart_type,
	.release_port	= sirfsoc_uart_release_port,
	.request_port	= sirfsoc_uart_request_port,
	.config_port	= sirfsoc_uart_config_port,
};

static int sirfsoc_init_port(struct sirfsoc_uart_port *sirfport)
{
	struct uart_port *port = &sirfport->port;
	int ret = 0;
	port->private_data = sirfport;
	port->ops = &sirfsoc_uart_ops;
	port->membase = ioremap(port->mapbase, SIRFUART_MAP_SIZE);
	if (!port->membase) {
		dev_err(port->dev, "Cannot ioremap resource.\n");
		ret = -ENOMEM;
		goto init_port_err;
	}
	spin_lock_init(&port->lock);
	sirfport->rx_intr_mask |= SIRFUART_RX_IO_INT_EN;
init_port_err:
	return ret;
}

#ifdef CONFIG_SERIAL_SIRFSOC_CONSOLE
static int __init sirfsoc_uart_console_setup(struct console *co, char *options)
{
	unsigned int baud = 115200;
	unsigned int bits = 8;
	unsigned int parity = 'n';
	unsigned int flow = 'n';

	sirfsoc_init_port(&sirfsoc_uart_ports[co->index]);
	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	sirfsoc_uart_ports[co->index].port.cons = co;
	return uart_set_options(&sirfsoc_uart_ports[co->index].port, co,
						baud, parity, bits, flow);
}

static void sirfsoc_uart_console_putchar(struct uart_port *port, int ch)
{
	while (rd_regl(port,
		SIRFUART_TX_FIFO_STATUS) & SIRFUART_FIFOFULL_MASK(port))
		cpu_relax();
	wr_regb(port, SIRFUART_TX_FIFO_DATA, ch);
}

static void sirfsoc_uart_console_write(struct console *co, const char *s,
							unsigned int count)
{
	struct uart_port *port = &sirfsoc_uart_ports[co->index].port;
	uart_console_write(port, s, count, sirfsoc_uart_console_putchar);
}

static struct console sirfsoc_uart_console = {
	.name		= "ttyS",
	.device		= uart_console_device,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.write		= sirfsoc_uart_console_write,
	.setup		= sirfsoc_uart_console_setup,
	.data           = &sirfsoc_uart_drv,
};

static int __init sirfsoc_uart_console_init(void)
{
/*
	add_preferred_console(sirfsoc_uart_console.name, 1, NULL);
	sirfsoc_init_port(&sirfsoc_uart_ports[1]);
*/
	register_console(&sirfsoc_uart_console);
	return 0;
}
console_initcall(sirfsoc_uart_console_init);
#endif

static struct uart_driver sirfsoc_uart_drv = {
	.owner		= THIS_MODULE,
	.driver_name	= SIRFUART_PORT_NAME,
	.nr		= SIRFSOC_UART_NR,
	.dev_name	= SIRFSOC_UART_NAME,
	.major		= SIRFSOC_UART_MAJOR,
	.minor		= SIRFSOC_UART_MINOR,
#ifdef CONFIG_SERIAL_SIRFSOC_CONSOLE
	.cons			= &sirfsoc_uart_console,
#else
	.cons			= NULL,
#endif
};

int sirfsoc_uart_platform_probe(struct platform_device *pdev)
{
	int probe_index;
	struct sirfsoc_uart_port *sirfport;
	struct uart_port *port;
	struct pinmux *pmx;
	const unsigned int *prop = NULL;
	int ret;

	prop = of_get_property(pdev->dev.of_node, "platform_id", NULL);
	if (!prop) {
		ret = -ENODEV;
		goto probe_out;
	}
	pdev->id = be32_to_cpup(prop);
	probe_index = pdev->id;
	sirfport = &sirfsoc_uart_ports[probe_index];
	port = &sirfport->port;

	if (sirfport->hw_flow_ctrl) {
		pmx = pinmux_get(&pdev->dev, NULL);
		if (!IS_ERR(pmx))
			pinmux_enable(pmx);
	}

	if (sirfsoc_init_port(sirfport)) {
		dev_err(&pdev->dev, "Failed to init port(%d).\n", probe_index);
		ret = -EFAULT;
		goto probe_out;
	}
	platform_set_drvdata(pdev, sirfport);
	ret = uart_add_one_port(&sirfsoc_uart_drv, port);
	if (ret != 0) {
		platform_set_drvdata(pdev, NULL);
		dev_err(&pdev->dev, "Cannot add UART port(%d).\n", probe_index);
		goto probe_out;
	}
probe_out:
	return ret;
}

static int sirfsoc_uart_platform_remove(struct platform_device *pdev)
{
	struct sirfsoc_uart_port *sirfport = platform_get_drvdata(pdev);
	struct uart_port *port = &sirfport->port;
	platform_set_drvdata(pdev, NULL);
	iounmap(port->membase);
	uart_remove_one_port(&sirfsoc_uart_drv, port);
	return 0;
}

static int
sirfsoc_uart_platform_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct sirfsoc_uart_port *sirfport = platform_get_drvdata(pdev);
	struct uart_port *port = &sirfport->port;
	uart_suspend_port(&sirfsoc_uart_drv, port);
	return 0;
}

static int sirfsoc_uart_platform_resume(struct platform_device *pdev)
{
	struct sirfsoc_uart_port *sirfport = platform_get_drvdata(pdev);
	struct uart_port *port = &sirfport->port;
	uart_resume_port(&sirfsoc_uart_drv, port);
	return 0;
}

static struct of_device_id sirfsoc_serial_of_match[] __devinitdata = {
	{ .compatible = "sirf,prima2-uart", },
	{}
};
MODULE_DEVICE_TABLE(of, sirfsoc_serial_of_match);

static struct platform_driver sirfsoc_uart_driver = {
	.probe		= sirfsoc_uart_platform_probe,
	.remove		= __devexit_p(sirfsoc_uart_platform_remove),
	.suspend	= sirfsoc_uart_platform_suspend,
	.resume		= sirfsoc_uart_platform_resume,
	.driver		= {
		.name	= SIRFUART_PORT_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = sirfsoc_serial_of_match,
	},
};

static int __init sirfsoc_uart_init(void)
{
	int ret = 0;

	ret = uart_register_driver(&sirfsoc_uart_drv);
	if (ret)
		goto out;

	ret = platform_driver_register(&sirfsoc_uart_driver);
	if (ret)
		uart_unregister_driver(&sirfsoc_uart_drv);
out:
	return ret;
}
module_init(sirfsoc_uart_init);

static void __exit sirfsoc_uart_exit(void)
{
	platform_driver_unregister(&sirfsoc_uart_driver);
	uart_unregister_driver(&sirfsoc_uart_drv);
}
module_exit(sirfsoc_uart_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Bin Shi <Bin.Shi@csr.com>, Rong Wang<Rong.Wang@csr.com>");
MODULE_DESCRIPTION("CSR SiRFprimaII Uart Driver");
