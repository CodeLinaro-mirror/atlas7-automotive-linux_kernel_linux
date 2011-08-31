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
 * @name: the name of this specific map entry for the particular machine.
 *	This is the second parameter passed to pinmux_get() when you want
 *	to have several mappings to the same device
 * @ctrl_dev: the pin control device to be used by this mapping, may be NULL
 *	if you provide .ctrl_dev_name instead (this is more common)
 * @ctrl_dev_name: the name of the device controlling this specific mapping,
 *	the name must be the same as in your struct device*
 * @function: a function in the driver to use for this mapping, the driver
 *	will lookup the function referenced by this ID on the specified
 *	pin control device
 * @position: sometimes a function has several possible positions in the
 *	pin space, so this parameter accepts a certain position enumerator.
 *	If for example a certain port can be mapped in three different
 *	locations this could be 0, 1 or 2
 * @dev: the device using this specific mapping, may be NULL if you provide
 *	.dev_name instead (this is more common)
 * @dev_name: the name of the device using this specific mapping, the name
 *	must be the same as in your struct device*
 */
struct pinmux_map {
	const char *name;
	struct device *ctrl_dev;
	const char *ctrl_dev_name;
	const char *function;
	unsigned position;
	struct device *dev;
	const char *dev_name;
};

/*
 * Convenience macro to set a simple map from a certain pin controller and a
 * certain function to a named device
 */
#define PINMUX_MAP(a, b, c) \
	{ .ctrl_dev_name = a, .function = b, .dev_name = c }
/*
 * Convenience macro to map a function onto the primary device pinctrl device
 * this is especially helpful on systems that have only one pin controller
 * or need to set up a lot of mappings on the primary controller.
 */
#define PINMUX_MAP_PRIMARY(a, b) \
	{ .ctrl_dev_name = "pinctrl.0", .function = a, .dev_name = b }

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
