#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serial_reg.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>
#include <linux/time.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include "sharedparam.h"
#include "Cop.h"

static void __init serial_init(void);
unsigned long *uart_ctrl_addr;

static int is_console_disabled(struct uart_port *port)
{
	unsigned int *addr = (unsigned int *)port->private_data;

	if ((addr != 0) && (*addr == 0))
		return 1;
	return 0;
}

void serial8250_turn_on(unsigned int on)
{
	if (uart_ctrl_addr != 0)
		*uart_ctrl_addr = on;
}
EXPORT_SYMBOL(serial8250_turn_on);

unsigned int serial8250_is_on(void)
{
	if (uart_ctrl_addr != 0)
		return *uart_ctrl_addr;
	return 1;
}
EXPORT_SYMBOL(serial8250_is_on);

void __init
coach_early_console_setup(void)
{
	serial_init();
}

static unsigned int coach_serial_in(struct uart_port *p, int offset)
{
	if (is_console_disabled(p))
		return -1;
	offset = offset << p->regshift;
	return readl(p->membase + offset);
}

static void coach_serial_out(struct uart_port *p, int offset, int value)
{
	if (is_console_disabled(p))
		return;
	offset = offset << p->regshift;
	writel(value, p->membase + offset);
}

static int coach_8250_handle_irq(struct uart_port *port)
{
	unsigned char status;
	unsigned long flags;
	struct uart_8250_port *up =
		container_of(port, struct uart_8250_port, port);
	unsigned int iir = serial_port_in(port, UART_IIR);

	if (iir & UART_IIR_NO_INT)
		return 0;

	if (is_console_disabled(port))
		return 1;

	spin_lock_irqsave(&port->lock, flags);

	status = serial_port_in(port, UART_LSR);

	pr_debug("status = %x...", status);

	serial_port_in(port, UART_IIR);
	if (8 - ((serial_port_in(port, 0xc) & 0x1f00) >> 8) != 0)
		serial8250_rx_chars(up, status);

	serial8250_modem_status(up);

	if ((serial_port_in(port, 0xc) & 0x1f) > 0)
		serial8250_tx_chars(up);
	spin_unlock_irqrestore(&port->lock, flags);
	return 0;
}

static void __init
serial_init(void)
{

#ifdef CONFIG_SERIAL_8250
	/*
	 * The first uart is optionally enabled with the 'toggle'
	 * uart mechanism is used.
	 * The second uart is initialized all the time.
	 * The default port for the console could be selected
	 * using the kernel command line.
	 */
	static struct uart_port s[2] __initdata;
	struct uart_port  *s1, *s2;
	struct device_node *np;
	struct resource res;
	unsigned int uart_base, ret, irq;

	memset(&s, 0, sizeof(s));

	s1 = s;
	s2 = s + 1;
	uart_ctrl_addr = (unsigned long *)sharedparam_get_uart_control_addr();

	if (0 == sharedparam_get_uart_port_num()) {
		np = of_find_node_by_path("/uart@0");
		ret = of_address_to_resource(np, 0, &res);
		uart_base = (unsigned int)ioremap_nocache(res.start,
					resource_size(&res));

		ret = of_property_read_u32(np, "interrupts", &irq);
		/* Using Uart1 (shared with ThreadX)*/
		s1->membase = (void *) uart_base;
		s1->mapbase =         uart_base;
		writel(1, s1->membase + 0x28);
		s1->irq = COACH_IRQ_NUM(irq);
		s1->line = 0;
		s1->type = PORT_CSR_COACH;
		s1->uartclk = sharedparam_get_uart_clk();
		s1->flags = 0;
		s1->iotype = UPIO_MEM32;
		s1->regshift = 2;
		s1->serial_out = coach_serial_out;
		s1->serial_in = coach_serial_in;
		s1->private_data = (void *)uart_ctrl_addr;
		s1->handle_irq = coach_8250_handle_irq;

		if (early_serial_setup(s1) != 0)
			pr_err("early_serial_setup() has failed for the 1st uart\n");
	}

	/* Using Uart2 (own uart port - default selection)*/
	np = of_find_node_by_path("/uart@1");
	if (np) {
		ret = of_address_to_resource(np, 0, &res);
		uart_base = (unsigned int)ioremap_nocache(res.start,
				resource_size(&res));

		ret = of_property_read_u32(np, "interrupts", &irq);

		s2->membase = (void *) uart_base;
		s2->mapbase =         uart_base;
		writel(1, s2->membase + 0x28);
		s2->irq = COACH_IRQ_NUM(irq);
		s2->line = 1;
		s2->type = PORT_CSR_COACH;
		s2->uartclk = sharedparam_get_uart_clk();
		s2->flags = 0;
		s2->iotype = UPIO_MEM32;
		s2->regshift = 2;
		s2->serial_out = coach_serial_out;
		s2->serial_in = coach_serial_in;
		s2->private_data = NULL;
		s2->handle_irq = coach_8250_handle_irq;

		if (early_serial_setup(s2) != 0)
			pr_err("early_serial_setup() failed for the 2nd uart\n");
	}
#endif
}

