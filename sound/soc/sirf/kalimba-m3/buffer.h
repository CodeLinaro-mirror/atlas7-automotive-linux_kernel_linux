/*
 * Copyright (c) 2015, 2016 The Linux Foundation. All rights reserved.
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
#ifndef __BUFFER_H
#define __BUFFER_H

#include <linux/platform_device.h>

unsigned long buff_alloc(struct device *dev, unsigned long size);
int buff_free(struct device *dev, unsigned long phy_addr);
int buff_fill(struct device *dev, unsigned long start_addr,
		unsigned long size, void *data);
int buff_read(struct device *dev, unsigned long start_addr,
		unsigned long size, void *data);

#endif /* __BUFFER_H */
