/*
 * include/linux/spi/spi_sirfsoc.h
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __SIRFSOC_SPI_H__
#define __SIRFSOC_SPI_H__

struct sirfsoc_spi_platdata {
	int bus_num;
};

struct sirfsoc_spi_ctrldata {
	int cs_type;
	void (*chip_select) (void);
	void (*chip_deselect) (void);
	int cs_hold_clk;
};

#define CS_HW_CTRL	1
#define CS_RISC_IO	2
#define CS_GPIO		3
#define CS_SW_CTRL	4

#define CS_HOLD_1	0
#define CS_HOLD_2	1

#endif
