/*
 * CSR sirfsoc ion driver
 *
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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

#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "../ion.h"
#include "../ion_priv.h"

static struct ion_device *sirf_ion_device;
static int num_heaps;
static struct ion_heap **heaps;

/* Wrap ion_client_create since client doesn't know sirf_ion_device */
struct ion_client *sirfsoc_ion_client_create(const char *name)
{
	if (IS_ERR_OR_NULL(sirf_ion_device))
		return NULL;

	return ion_client_create(sirf_ion_device, name);
}
EXPORT_SYMBOL(sirfsoc_ion_client_create);

static long sirfsoc_ion_custom_ioctl(struct ion_client *client,
			unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case ION_CUSTOM_CMD_PHYS:
	{
		struct ion_custom_data_phys data;
		struct ion_handle *handle;
		int ret;

		if (copy_from_user(&data, (void __user *)arg,
			sizeof(struct ion_custom_data_phys)))
			return -EFAULT;

		handle = ion_import_dma_buf(client, data.fd);
		if (IS_ERR(handle))
			return PTR_ERR(handle);

		ret = ion_phys(client, handle, &data.addr, &data.len);
		if (ret)
			return ret;

		ion_free(client, handle);

		if (copy_to_user((void __user *)arg, &data,
			sizeof(struct ion_custom_data_phys)))
			return -EFAULT;
		break;
	}
	default:
		return -ENOTTY;
	}

	return 0;
}

static int sirfsoc_ion_probe(struct platform_device *pdev)
{
	struct ion_platform_data *pdata = pdev->dev.platform_data;
	int err;
	int i;

	num_heaps = pdata->nr;

	heaps = devm_kzalloc(&pdev->dev,
			     sizeof(struct ion_heap *) * pdata->nr,
			     GFP_KERNEL);

	sirf_ion_device = ion_device_create(sirfsoc_ion_custom_ioctl);
	if (IS_ERR_OR_NULL(sirf_ion_device))
		return PTR_ERR(sirf_ion_device);

	/* create the heaps as specified in the board file */
	for (i = 0; i < num_heaps; i++) {
		struct ion_platform_heap *heap_data = &pdata->heaps[i];

		heaps[i] = ion_heap_create(heap_data);
		if (IS_ERR_OR_NULL(heaps[i])) {
			err = PTR_ERR(heaps[i]);
			goto err;
		}
		ion_device_add_heap(sirf_ion_device, heaps[i]);
	}
	platform_set_drvdata(pdev, sirf_ion_device);
	return 0;
err:
	for (i = 0; i < num_heaps; i++) {
		if (heaps[i])
			ion_heap_destroy(heaps[i]);
	}
	return err;
}

static int sirfsoc_ion_remove(struct platform_device *pdev)
{
	struct ion_device *sirf_ion_device = platform_get_drvdata(pdev);
	int i;

	ion_device_destroy(sirf_ion_device);
	for (i = 0; i < num_heaps; i++)
		ion_heap_destroy(heaps[i]);
	return 0;
}

static struct platform_driver sirfsoc_ion_driver = {
	.probe = sirfsoc_ion_probe,
	.remove = sirfsoc_ion_remove,
	.driver = { .name = "sirf-ion" }
};

static int __init sirfsoc_ion_init(void)
{
	return platform_driver_register(&sirfsoc_ion_driver);
}

static void __exit sirfsoc_ion_exit(void)
{
	platform_driver_unregister(&sirfsoc_ion_driver);
}

module_init(sirfsoc_ion_init);
module_exit(sirfsoc_ion_exit);

MODULE_DESCRIPTION("SIRF ION Driver");
MODULE_LICENSE("GPL");
