/*
 * Interface the pinmux subsystem
 *
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 * Based on bits of regulator core, gpio core and clk core
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */
#ifndef __LINUX_PINCTRL_PINMUX_H
#define __LINUX_PINCTRL_PINMUX_H

#include <linux/list.h>
#include <linux/seq_file.h>
#include "pinctrl.h"

/* This struct is private to the core and should be regarded as a cookie */
struct pinmux;

#ifdef CONFIG_PINMUX

struct pinctrl_dev;

/**
 * struct pinmux_ops - pinmux operations, to be implemented by pin controller
 * drivers that support pinmuxing
 * @request: called by the core to see if a certain pin can be made available
 *	available for muxing. This is called by the core to acquire the pins
 *	before selecting any actual mux setting across a function. The driver
 *	is allowed to answer "no" by returning a negative error code
 * @free: the reverse function of the request() callback, frees a pin after
 *	being requested
 * @list_functions: list the number of selectable named functions available
 *	in this pinmux driver, the core will begin on 0 and call this
 *	repeatedly as long as it returns >= 0 to enumerate mux settings
 * @list_positions: list the number of selectable positions for a certain
 *	function selector, the core will begin on 0 and call this repeatedly
 *	as long as it returns >= 0 to enumerate positions
 * @get_function_name: return the function name of the muxing selector,
 *	called by the core to figure out which mux setting it shall map a
 *	certain device to
 * @get_function_pins: return an array of pins corresponding to a certain
 *	function selector and position in @pins, and the size of the array
 *	in @num_pins
 * @enable: enable a certain muxing function on a certain position. The driver
 *	does not need to figure out whether enabling this function conflicts
 *	some other use of the pins, such collisions are handled by the pinmux
 *	subsystem
 * @disable: disable a certain muxing selector on a certain position.
 * @config: custom configuration function for a certain muxing selector -
 *	this works a bit like an ioctl() and can pass in and return arbitrary
 *	configuration data to the pinmux. Currently we do not pass in the
 *	position to this call, refactor if need be
 * @gpio_request_enable: requests and enables GPIO on a certain pin.
 *	Implement this only if you can mux every pin individually as GPIO. The
 *	affected GPIO range is passed along with an offset into that
 *	specific GPIO range - function selectors and positions are orthogonal
 *	to this, the core will however make sure the pins do not collide
 * @dbg_show: optional debugfs display hook that will provide per-device
 *	info for a certain pin in debugfs
 */
struct pinmux_ops {
	int (*request) (struct pinctrl_dev *pctldev, unsigned offset);
	int (*free) (struct pinctrl_dev *pctldev, unsigned offset);
	int (*list_functions) (struct pinctrl_dev *pctldev, unsigned selector);
	int (*list_positions) (struct pinctrl_dev *pctldev, unsigned selector,
			       unsigned position);
	const char *(*get_function_name) (struct pinctrl_dev *pctldev,
					  unsigned selector);
	int (*get_function_pins) (struct pinctrl_dev *pctldev,
				  unsigned selector, unsigned position,
				  unsigned ** const pins,
				  unsigned * const num_pins);
	int (*enable) (struct pinctrl_dev *pctldev, unsigned selector,
		       unsigned position);
	void (*disable) (struct pinctrl_dev *pctldev, unsigned selector,
			 unsigned position);
	int (*config) (struct pinctrl_dev *pctldev, unsigned selector,
		       u16 param, unsigned long *data);
	int (*gpio_request_enable) (struct pinctrl_dev *pctldev,
				    struct pinctrl_gpio_range *range,
				    unsigned offset);
	void (*dbg_show) (struct pinctrl_dev *pctldev, struct seq_file *s,
			  unsigned offset);
};

/* External interface to pinmux */
extern int pinmux_request_gpio(unsigned gpio);
extern void pinmux_free_gpio(unsigned gpio);
extern struct pinmux *pinmux_get(struct device *dev, const char *name);
extern void pinmux_put(struct pinmux *pmx);
extern int pinmux_enable(struct pinmux *pmx);
extern void pinmux_disable(struct pinmux *pmx);
extern int pinmux_config(struct pinmux *pmx, u16 param, unsigned long *data);

#else /* !CONFIG_PINMUX */

static inline int pinmux_request_gpio(unsigned gpio)
{
	return 0;
}

static inline void pinmux_free_gpio(unsigned gpio)
{
}

static inline struct pinmux *pinmux_get(struct device *dev, const char *name)
{
	return NULL;
}

static inline void pinmux_put(struct pinmux *pmx)
{
}

static inline int pinmux_enable(struct pinmux *pmx)
{
	return 0;
}

static inline void pinmux_disable(struct pinmux *pmx)
{
}

static inline int pinmux_config(struct pinmux *pmx, u16 param,
				unsigned long *data)
{
	return 0;
}

#endif /* CONFIG_PINMUX */

#endif /* __LINUX_PINCTRL_PINMUX_H */
