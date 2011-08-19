/*
 * Machine interface for the pinctrl subsystem.
 *
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 * Based on bits of regulator core, gpio core and clk core
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */
#ifndef __LINUX_PINMUX_MACHINE_H
#define __LINUX_PINMUX_MACHINE_H

/**
 * struct pinmux_map - boards/machines shall provide this map for devices
 * @function: a functional name for this mapping so it can be passed down
 *	to the driver to invoke that function and be referenced by this ID
 *	in e.g. pinmux_get()
 * @dev: the device using this specific mapping, may be NULL if you provide
 *	.dev_name instead (this is more common)
 * @dev_name: the name of the device using this specific mapping, the name
 *	must be the same as in your struct device*
 * @ctrl_dev: the pin control device to be used by this mapping, may be NULL
 *	if you provide .ctrl_dev_name instead (this is more common)
 * @ctrl_dev_name: the name of the device controlling this specific mapping,
 *	the name must be the same as in your struct device*
 */
struct pinmux_map {
	const char *function;
	struct device *dev;
	const char *dev_name;
	struct device *ctrl_dev;
	const char *ctrl_dev_name;
};

/* Convenience macro to set a simple map from a function to a named device */
#define PINMUX_MAP(a, b, c) \
	{ .function = a, .dev_name = b, .ctrl_dev_name = c }
/*
 * Convenience macro to map a function onto the primary device pinctrl device
 * this is especially helpful on systems that have only one pin controller
 * or need to set up a lot of mappings on the primary controller.
 */
#define PINMUX_MAP_PRIMARY(a, b) \
	{ .function = a, .dev_name = b, .ctrl_dev_name = "pinctrl.0" }

#ifdef CONFIG_PINMUX

extern int pinmux_register_mappings(struct pinmux_map const *map,
				unsigned num_maps);

#else

static inline int pinmux_register_mappings(struct pinmux_map const *map,
					   unsigned num_maps)
{
	return 0;
}

#endif /* !CONFIG_PINCTRL */
#endif
