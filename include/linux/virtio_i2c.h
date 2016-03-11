/*
 * Virtio-based remote processor i2c
 *
 * Copyright (c) 2014, 2016, The Linux Foundation. All rights reserved.
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

#ifndef _VIRTIO_I2C_H_
#define _VIRTIO_I2C_H_

#include <linux/i2c.h>

/* Definition of Virtio I2C Commands */
#define VIRTIO_I2C_WRITE	0x01
#define VIRTIO_I2C_READ		0x02

/* The length of I2C adapter name */
#define I2C_NAME_LENGTH		0x30
/* Definition of Virtio I2C MMIO */
#define I2C_MMIO_CLASS		MMIO_CONFIG_BASE
/* real i2c adapter number */
#define I2C_MMIO_ADAPTER_NR	(MMIO_CONFIG_BASE + 0x04)
/* real i2c adapter time out */
#define I2C_MMIO_TIME_OUT	(MMIO_CONFIG_BASE + 0x08)
/* real i2c adapter retries */
#define I2C_MMIO_RETRIES	(MMIO_CONFIG_BASE + 0x0C)
/* real i2c adapter name */
#define I2C_MMIO_NAME		(MMIO_CONFIG_BASE + 0x10)
/* real i2c adapter name */
#define I2C_MMIO_VIRT_NAME	(I2C_MMIO_NAME + I2C_NAME_LENGTH)

/* struct virtio_i2c_desc - descriptor of virtio adatper
 * @i2c_adapter_id: the real i2c adapter id
 * @name: the descriptor of this virtio i2c adapter
 * @i2c_client_num: client device attached on this adapter
 * @i2c_client_descs: the descriptor of i2c clients
 */

struct virtio_i2c_desc {
	u32 i2c_adapter_id;
	char name[I2C_NAME_LENGTH];
};

/* struct virti2c_req_outhdr - header of virtio i2c request
 * @type: the i2c request type read/write
 * @addr: the slave address, either seven or ten bits
 * @flags: flags of i2c_msg
 * @len: number of data bytes
 */
struct virti2c_req_outhdr {
	u32 type;
	u32 addr;
	u32 flags;
	u32 len;
};

/* virtio i2c request return status */
struct virti2c_req_inhdr {
	u32 status;
};

#endif /* _VIRTIO_I2C_H_ */
