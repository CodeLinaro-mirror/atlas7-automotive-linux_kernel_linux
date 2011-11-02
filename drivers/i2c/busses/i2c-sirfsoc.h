/*
 * I2C bus drivers for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef _SIRFSOC_I2C_BUS_H_
#define _SIRFSOC_I2C_BUS_H_

#include <linux/bitops.h>

#define SIRFSOC_I2C_CLK_CTRL		0x00
#define SIRFSOC_I2C_STATUS		0x0C
#define SIRFSOC_I2C_CTRL		0x10
#define SIRFSOC_I2C_IO_CTRL		0x14
#define SIRFSOC_I2C_SDA_DELAY		0x18
#define SIRFSOC_I2C_CMD_START		0x1C
#define SIRFSOC_I2C_CMD_BUF		0x30
#define SIRFSOC_I2C_DATA_BUF		0x80

#define SIRFSOC_I2C_CMD_BUF_MAX		16
#define SIRFSOC_I2C_DATA_BUF_MAX	16

#define SIRFSOC_I2C_CMD(x)		(SIRFSOC_I2C_CMD_BUF + (x)*0x04)
#define SIRFSOC_I2C_DATA_MASK(x)        (0xFF<<(((x)&3)*8))
#define SIRFSOC_I2C_DATA_SHIFT(x)       (((x)&3)*8)

#define SIRFSOC_I2C_DIV_MASK		(0xFFFF)

/* I2C status flags */
#define SIRFSOC_I2C_STAT_BUSY		BIT(0)
#define SIRFSOC_I2C_STAT_TIP		BIT(1)
#define SIRFSOC_I2C_STAT_NACK		BIT(2)
#define SIRFSOC_I2C_STAT_TR_INT		BIT(4)
#define SIRFSOC_I2C_STAT_STOP		BIT(6)
#define SIRFSOC_I2C_STAT_CMD_DONE	BIT(8)
#define SIRFSOC_I2C_STAT_ERR		BIT(9)
#define SIRFSOC_I2C_CMD_INDEX		(0x1F<<16)

/* I2C control flags */
#define SIRFSOC_I2C_RESET		BIT(0)
#define SIRFSOC_I2C_CORE_EN		BIT(1)
#define SIRFSOC_I2C_MASTER_MODE		BIT(2)
#define SIRFSOC_I2C_CMD_DONE_EN		BIT(11)
#define SIRFSOC_I2C_ERR_INT_EN		BIT(12)

#define SIRFSOC_I2C_SDA_DELAY_MASK	(0xFF)
#define SIRFSOC_I2C_SCLF_FILTER		(3<<8)

#define SIRFSOC_I2C_START_CMD		BIT(0)

#define SIRFSOC_I2C_CMD_RP(x)		((x)&0x7)
#define SIRFSOC_I2C_NACK		BIT(3)
#define SIRFSOC_I2C_WRITE		BIT(4)
#define SIRFSOC_I2C_READ		BIT(5)
#define SIRFSOC_I2C_STOP		BIT(6)
#define SIRFSOC_I2C_START		BIT(7)

#endif
