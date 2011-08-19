/*
 * Interface the pinctrl subsystem
 *
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 * This interface is used in the core to keep track of pins.
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */
#ifndef __LINUX_PINCTRL_PINCTRL_H
#define __LINUX_PINCTRL_PINCTRL_H

#ifdef CONFIG_PINCTRL

#include <linux/radix-tree.h>
#include <linux/spinlock.h>

struct pinmux_ops;

/**
 * struct pinctrl_pin_desc - boards/machines provide information on their
 * pins, pads or other muxable units in this struct
 * @number: unique pin number from the global pin number space
 * @name: a name for this pin
 */
struct pinctrl_pin_desc {
	unsigned number;
	const char *name;
};

/* Convenience macro to define a single named or anonymous pin descriptor */
#define PINCTRL_PIN(a, b) { .number = a, .name = b }
#define PINCTRL_PIN_ANON(a) { .number = a }

/**
 * struct pinctrl_desc - pin controller descriptor, register this to pin
 * control subsystem
 * @name: name for the pin controller
 * @pins: an array of pin descriptors describing all the pins handled by
 *	this pin controller
 * @npins: number of descriptors in the array, usually just ARRAY_SIZE()
 *	of the pins field above
 * @maxpin: since pin spaces may be sparse, there can he "holes" in the
 *	pin range, this attribute gives the maximum pin number in the
 *	total range. This should not be lower than npins for example,
 *	but may be equal to npins if you have no holes in the pin range.
 * @pmxops: pinmux operation vtable, if you support pinmuxing in your driver
 * @gpio_base: the base offset of the pin range in the GPIO subsystem that
 *	is handled by this controller, if applicable. This member is only
 *	relevant if you want to e.g. control pins from the GPIO subsystem.
 * @gpio_pins: the number of pins from (and including) the gpio_base offset
 *	handled by this pin controller.
 * @owner: module providing the pin controller, used for refcounting
 */
struct pinctrl_desc {
	const char *name;
	struct pinctrl_pin_desc const *pins;
	unsigned int npins;
	unsigned int maxpin;
	struct pinmux_ops *pmxops;
	unsigned int gpio_base;
	unsigned int gpio_pins;
	struct module *owner;
};

/**
 * struct pinctrl_dev - pin control class device
 * @desc: the pin controller descriptor supplied when initializing this pin
 *	controller
 * @node: node to include this pin controller in the global pin controller list
 * @dev: the device entry for this pin controller
 * @owner: module providing the pin controller, used for refcounting
 * @driver_data: driver data for drivers registering to the pin controller
 *	subsystem
 *
 * This should be dereferenced and used by the pin controller core ONLY
 */
struct pinctrl_dev {
	struct pinctrl_desc *desc;
	struct radix_tree_root pin_desc_tree;
	spinlock_t pin_desc_tree_lock;
	struct list_head node;
	struct device dev;
	struct module *owner;
	void *driver_data;
};

/* These should only be used from drives */
static inline const char *pctldev_get_name(struct pinctrl_dev *pctldev)
{
	/* We're not allowed to register devices without name */
	return pctldev->desc->name;
}

static inline void *pctldev_get_drvdata(struct pinctrl_dev *pctldev)
{
	return pctldev->driver_data;
}

/* External interface to pin controller */
extern struct pinctrl_dev *pinctrl_register(struct pinctrl_desc *pctldesc,
				struct device *dev, void *driver_data);
extern void pinctrl_unregister(struct pinctrl_dev *pctldev);
extern bool pin_is_valid(struct pinctrl_dev *pctldev, int pin);

#else

struct pinctrl_dev;

/* Sufficiently stupid default function when pinctrl is not in use */
static inline bool pin_is_valid(struct pinctrl_dev *pctldev, int pin)
{
	return pin >= 0;
}

#endif /* !CONFIG_PINCTRL */

#endif /* __LINUX_PINCTRL_PINCTRL_H */
