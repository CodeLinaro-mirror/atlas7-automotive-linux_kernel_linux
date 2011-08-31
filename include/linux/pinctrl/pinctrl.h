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
#include <linux/list.h>

struct pinmux_ops;
struct gpio_chip;

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
 * struct pinctrl_gpio_range - each pin controller can provide subranges of
 * the GPIO number space to be handled by the controller
 * @name: a name for the chip in this range
 * @id: an ID number for the chip in this range
 * @base: base offset of the GPIO range
 * @npins: number of pins in the GPIO range, including the base number
 * @gc: an optional pointer to a gpio_chip
 * @node: list node for internal use
 */
struct pinctrl_gpio_range {
	const char name[16];
	unsigned int id;
	unsigned int base;
	unsigned int npins;
	struct gpio_chip *gc;
	struct list_head node;
};

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
 * @owner: module providing the pin controller, used for refcounting
 */
struct pinctrl_desc {
	const char *name;
	struct pinctrl_pin_desc const *pins;
	unsigned int npins;
	unsigned int maxpin;
	struct pinmux_ops *pmxops;
	struct module *owner;
};

/**
 * struct pinctrl_dev - pin control class device
 * @desc: the pin controller descriptor supplied when initializing this pin
 *	controller
 * @pin_desc_tree: each pin descriptor for this pin controller is stored in
 *	this radix tree
 * @pin_desc_tree_lock: lock for the descriptor tree
 * @gpio_ranges: a list of GPIO ranges that is handled by this pin controller,
 *	ranges are added to this list at runtime
 * @gpio_ranges_lock: lock for the GPIO ranges list
 * @dev: the device entry for this pin controller
 * @owner: module providing the pin controller, used for refcounting
 * @driver_data: driver data for drivers registering to the pin controller
 *	subsystem
 * @node: node to include this pin controller in the global pin controller list
 *
 * This should be dereferenced and used by the pin controller core ONLY
 */
struct pinctrl_dev {
	struct pinctrl_desc *desc;
	struct radix_tree_root pin_desc_tree;
	spinlock_t pin_desc_tree_lock;
	struct list_head gpio_ranges;
	spinlock_t gpio_ranges_lock;
	struct device dev;
	struct module *owner;
	void *driver_data;
	struct list_head node;
};

/* These should only be used from drivers */
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
extern void pinctrl_add_gpio_range(struct pinctrl_dev *pctldev,
				   struct pinctrl_gpio_range *range);
#else

struct pinctrl_dev;

/* Sufficiently stupid default function when pinctrl is not in use */
static inline bool pin_is_valid(struct pinctrl_dev *pctldev, int pin)
{
	return pin >= 0;
}

#endif /* !CONFIG_PINCTRL */

#endif /* __LINUX_PINCTRL_PINCTRL_H */
