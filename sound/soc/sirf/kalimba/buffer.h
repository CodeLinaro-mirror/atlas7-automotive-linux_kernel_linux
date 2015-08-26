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
