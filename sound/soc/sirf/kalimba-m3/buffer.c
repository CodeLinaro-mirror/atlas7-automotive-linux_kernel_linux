/*
 * kalimba debug & development interface
 * TODO: This module is for temporary debugging purpose, will be removed.
 *
 * Copyright (c) 2015 The Linux Foundation. All rights reserved.
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


#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/slab.h>

#include "buffer.h"

struct buff_node {
	struct list_head node;
	dma_addr_t phy_addr;
	void *virt_addr;
	unsigned long size;
};

static struct list_head buff_list = LIST_HEAD_INIT(buff_list);

unsigned long buff_alloc(struct device *dev, unsigned long size)
{
	struct buff_node *buff;

	buff = kmalloc(sizeof(struct buff_node), GFP_KERNEL);
	if (buff == NULL)
		return -ENOMEM;
	buff->virt_addr = dma_alloc_coherent(dev, size, &buff->phy_addr,
		GFP_KERNEL);
	if (buff->virt_addr == NULL) {
		kfree(buff);
		dev_err(dev, "Alloc dram failed.\n");
		return -ENOMEM;
	}
	memset(buff->virt_addr, 0, size);
	buff->size = size;
	list_add(&buff->node, &buff_list);
	dev_info(dev,
			"Alloc dram success, phy addr: %p, virt addr: %p\n",
			(void *)(buff->phy_addr), buff->virt_addr);
	return (unsigned long)(buff->phy_addr);
}

int buff_free(struct device *dev, unsigned long phy_addr)
{
	struct buff_node *buff;

	list_for_each_entry(buff, &buff_list, node) {
		if (buff->phy_addr <= phy_addr
				&& (buff->phy_addr + buff->size) > phy_addr) {
			dma_free_coherent(dev, buff->size,
					buff->virt_addr, buff->phy_addr);
			list_del(&buff->node);
			dev_info(dev, "Free dram success\n");
			return 0;
		}
	}
	dev_err(dev, "Free dram failed. phy addr: %lu\n", phy_addr);
	return -EINVAL;
}

int buff_fill(struct device *dev, unsigned long start_addr,
		unsigned long size, void *data)
{
	struct buff_node *buff;
	unsigned long virt_start_addr;

	list_for_each_entry(buff, &buff_list, node) {
		if (buff->phy_addr <= start_addr &&
			start_addr < (buff->phy_addr + buff->size) &&
			size <= (buff->size - (start_addr - buff->phy_addr))) {
			virt_start_addr = (unsigned long)buff->virt_addr +
				(start_addr - buff->phy_addr);
			memcpy((void *)virt_start_addr, data, size);
			dev_info(dev, "Write dram success\n");
			return 0;
		}
	}
	dev_err(dev,
		"The address and size is over range of buffer\n");
	return -EINVAL;
}

int buff_read(struct device *dev, unsigned long start_addr,
		unsigned long size, void *data)
{
	struct buff_node *buff;
	unsigned long virt_start_addr;

	if (data == NULL)
		return -EINVAL;

	list_for_each_entry(buff, &buff_list, node) {
		if (buff->phy_addr <= start_addr &&
			start_addr < (buff->phy_addr + buff->size) &&
			size <= (buff->size - (start_addr - buff->phy_addr))) {
			virt_start_addr = (unsigned long)buff->virt_addr +
				(start_addr - buff->phy_addr);
			memcpy(data, (void *)virt_start_addr, size);
			dev_info(dev, "Read dram success\n");
			return 0;
		}
	}
	dev_err(dev,
		"The address and size is over range of buffer\n");
	return -EINVAL;
}
