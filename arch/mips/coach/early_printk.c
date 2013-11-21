#include <linux/console.h>
#include <linux/init.h>
#include <linux/serial_reg.h>
#include <asm/io.h>

#include "sharedparam.h"


static inline void coach_uart0_putc(int c)
{
#define UART_ADDR_LINE_STATUS 0xb0010154
#define UART2_ADDR_LINE_STATUS 0xb0012154
#define UART_ADDR_RBR 0xb0010140
#define UART2_ADDR_RBR 0xb0012140

	if (0 == sharedparam_get_uart_port_num()) {
		u32 *ctrl = sharedparam_get_uart_control_addr();

		if (!ctrl || *ctrl) {
			while (!(readl((u32 *)UART_ADDR_LINE_STATUS) & 0x20))
				;
			writel(c, (u32 *)UART_ADDR_RBR);
		}
	} else {
		while ((readl((u32 *)UART2_ADDR_LINE_STATUS) & 0x20) == 0)
			;

		writel(c, (u32 *)UART2_ADDR_RBR);
	}
}

void  prom_putchar(int c)
{
	return coach_uart0_putc(c);
}


void  prom_puts(const char *s)
{
	while (*s) {
		char c;

		c = *s;

		if (c == '\n')
			prom_putchar('\r');

		prom_putchar(c);
		++s;
	}
}
