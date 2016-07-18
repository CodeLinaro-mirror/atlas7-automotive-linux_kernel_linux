/*
 * SIRF hardware spinlock driver
 *
 * Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
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
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/hwspinlock.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "hwspinlock_internal.h"

struct sirf_hwspinlock {
	void __iomem *io_base;
	uint32_t number;
	struct hwspinlock_device bank;
};

/* Enable Hardware Spinlocks */
#define HW_SPINLOCK_ENABLE	0x0

/* Hardware Spinlock TestAndSet Registers */
#define HW_SPINLOCK_BASE(base)		(base + 0x04)
#define HW_SPINLOCK_REG(base, x)	(HW_SPINLOCK_BASE(base) + 0x4 * (x))

static int sirf_hwspinlock_trylock(struct hwspinlock *lock)
{
	void __iomem *lock_addr = lock->priv;

	/* attempt to acquire the lock by reading value == 1 from it */
	return !!readl(lock_addr);
}

static void sirf_hwspinlock_unlock(struct hwspinlock *lock)
{
	void __iomem *lock_addr = lock->priv;

	/* release the lock by writing 0 to it */
	writel(0, lock_addr);
}

static const struct hwspinlock_ops sirf_hwspinlock_ops = {
	.trylock = sirf_hwspinlock_trylock,
	.unlock = sirf_hwspinlock_unlock,
};

static int sirf_hwspinlock_probe(struct platform_device *pdev)
{
	struct sirf_hwspinlock *hwspin;
	struct hwspinlock *hwlock;
	struct resource *res;
	u32 num_of_locks, base_id;
	int idx, ret;

	ret = of_property_read_u32(pdev->dev.of_node,
			"num-spinlocks", &num_of_locks);
	if (ret) {
		dev_err(&pdev->dev,
			"Unable to find hwspinlock number. ret=%d\n", ret);
		return -ENODEV;
	}

	ret = of_property_read_u32(pdev->dev.of_node,
			"base-id", &base_id);
	if (ret) {
		dev_err(&pdev->dev,
			"Unable to find base id. ret=%d\n", ret);
		return -ENODEV;
	}

	hwspin = devm_kzalloc(&pdev->dev, sizeof(*hwspin) +
			sizeof(*hwlock) * num_of_locks, GFP_KERNEL);
	if (!hwspin)
		return -ENOMEM;

	/* retrieve io base */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hwspin->io_base = devm_ioremap_resource(&pdev->dev, res);
	if (!hwspin->io_base)
		return -ENOMEM;

	hwspin->number = num_of_locks;
	for (idx = 0; idx < hwspin->number; idx++) {
		hwlock = &hwspin->bank.lock[idx];
		hwlock->priv = HW_SPINLOCK_REG(hwspin->io_base, idx);
	}

	platform_set_drvdata(pdev, hwspin);

	pm_runtime_enable(&pdev->dev);

	ret = hwspin_lock_register(&hwspin->bank, &pdev->dev,
				   &sirf_hwspinlock_ops, base_id,
				   hwspin->number);
	if (ret)
		goto reg_failed;

	return 0;

reg_failed:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int sirf_hwspinlock_remove(struct platform_device *pdev)
{
	struct sirf_hwspinlock *hwspin = platform_get_drvdata(pdev);
	int ret;

	ret = hwspin_lock_unregister(&hwspin->bank);
	if (ret) {
		dev_err(&pdev->dev, "%s failed: %d\n", __func__, ret);
		return ret;
	}

	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct of_device_id sirf_hwpinlock_ids[] = {
	{ .compatible = "sirf,hwspinlock", },
	{},
};
MODULE_DEVICE_TABLE(of, sirf_hwpinlock_ids);

static struct platform_driver sirf_hwspinlock_driver = {
	.probe = sirf_hwspinlock_probe,
	.remove = sirf_hwspinlock_remove,
	.driver = {
		.name = "atlas7_hwspinlock",
		.of_match_table = of_match_ptr(sirf_hwpinlock_ids),
	},
};

static int __init sirf_hwspinlock_init(void)
{
	return platform_driver_register(&sirf_hwspinlock_driver);
}

/*
 * We have to put this driver's init level to arch init, because
 * this driver will be used by iobridge driver which begins to
 * work during subsys init
 */
arch_initcall(sirf_hwspinlock_init);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("SIRF Hardware spinlock driver");
