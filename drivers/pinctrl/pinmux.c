/*
 * Core driver for the pin muxing portions of the pin control subsystem
 *
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 * Based on bits of regulator core, gpio core and clk core
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */
#define pr_fmt(fmt) "pinmux core: " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/radix-tree.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinmux.h>
#include "core.h"

/* Global list of pinmuxes */
static DEFINE_MUTEX(pinmux_list_mutex);
static LIST_HEAD(pinmux_list);

/**
 * struct pinmux - per-device pinmux state holder
 * @node: global list node - only for internal use
 * @dev: the device using this pinmux
 * @map: corresponding pinmux map active for this pinmux setting
 * @usecount: the number of active users of this mux setting, used to keep
 *	track of nested use cases
 * @pins: an array of discrete physical pins used in this mapping, taken
 *	from the global pin enumeration space (copied from pinmux map)
 * @num_pins: the number of pins in this mapping array, i.e. the number of
 *	elements in .pins so we can iterate over that array (copied from
 *	pinmux map)
 * @pctldev: pin control device handling this pinmux
 * @pmxdev_selector: the function selector for the pinmux device handling
 *	this pinmux
 * @pmxdev_position: the function position for the pinmux device and
 *	selector handling this pinmux
 * @mutex: a lock for the pinmux state holder
 */
struct pinmux {
	struct list_head node;
	struct device *dev;
	struct pinmux_map const *map;
	unsigned usecount;
	struct pinctrl_dev *pctldev;
	unsigned pmxdev_selector;
	unsigned pmxdev_position;
	struct mutex mutex;
};

/**
 * pin_request() - request a single pin to be muxed in, typically for GPIO
 * @pin: the pin number in the global pin space
 * @function: a functional name to give to this pin, passed to the driver
 *	so it knows what function to mux in, e.g. the string "gpioNN"
 *	means that you want to mux in the pin for use as GPIO number NN
 * @gpio: if this request concerns a single GPIO pin
 * @gpio_range: the range matching the GPIO pin if this is a request for a
 *	single GPIO pin
 */
static int pin_request(struct pinctrl_dev *pctldev,
		       int pin, const char *function, bool gpio,
		       struct pinctrl_gpio_range *gpio_range)
{
	struct pin_desc *desc;
	const struct pinmux_ops *ops;
	int status = -EINVAL;

	pr_debug("request pin %d for %s\n", pin, function);

	if (!pin_is_valid(pctldev, pin)) {
		pr_err("pin is invalid\n");
		return -EINVAL;
	}

	if (!function) {
		pr_err("no function name given\n");
		return -EINVAL;
	}

	desc = pin_desc_get(pctldev, pin);
	if (desc == NULL) {
		pr_err("pin is not registered so it cannot be requested\n");
		goto out;
	}
	if (desc->mux_requested) {
		pr_err("pin already requested\n");
		goto out;
	}
	ops = pctldev->desc->pmxops;

	/* Let each pin increase references to this module */
	if (!try_module_get(pctldev->owner)) {
		pr_err("could not increase module refcount for pin %d\n", pin);
		status = -EINVAL;
		goto out;
	}

	/*
	 * If there is no kind of request function for the pin we just assume
	 * we got it by default and proceed.
	 */
	if (gpio && ops->gpio_request_enable)
		/* This requests and enables a single GPIO pin */
		status = ops->gpio_request_enable(pctldev, gpio_range, pin);
	else if (ops->request)
		status = ops->request(pctldev, pin);
	else
		status = 0;

	if (status) {
		pr_err("->request on device %s failed "
		       "for pin %d\n",
		       pctldev->desc->name, pin);
		goto out;
	}

	desc->mux_requested = true;
	strncpy(desc->mux_function, function, sizeof(desc->mux_function));

out:
	if (status)
		pr_err("pin-%d (%s) status %d\n",
		       pin, function ? : "?", status);

	return status;
}

/**
 * pin_free() - release a single muxed in pin so something else can be muxed in
 *	instead
 * @pin: the pin to free
 */
static void pin_free(struct pinctrl_dev *pctldev, int pin)
{
	const struct pinmux_ops *ops = pctldev->desc->pmxops;
	struct pin_desc *desc;

	desc = pin_desc_get(pctldev, pin);
	if (desc == NULL) {
		pr_err("pin is not registered so it cannot be freed\n");
		return;
	}

	if (ops->free)
		ops->free(pctldev, pin);

	desc->mux_requested = false;
	desc->mux_function[0] = '\0';
	module_put(pctldev->owner);
}

/**
 * pinmux_request_gpio() - request a single pin to be muxed in to be used
 *	as a GPIO pin
 * @gpio: the GPIO pin number from the GPIO subsystem number space
 */
int pinmux_request_gpio(unsigned gpio)
{
	char gpiostr[16];
	struct pinctrl_dev *pctldev;
	struct pinctrl_gpio_range *range;
	int ret;
	int pin;

	ret = pinctrl_get_device_gpio_range(gpio, &pctldev, &range);
	if (ret)
		return -EINVAL;

	/* Convert to the pin controllers number space */
	pin = gpio - range->base;

	/* Conjure some name stating what chip and pin this is taken by */
	snprintf(gpiostr, 15, "%s:%d", range->name, gpio);

	return pin_request(pctldev, pin, gpiostr, true, range);
}
EXPORT_SYMBOL_GPL(pinmux_request_gpio);

/**
 * pinmux_free_gpio() - free a single pin, currently muxed in to be used
 *	as a GPIO pin
 * @gpio: the GPIO pin number from the GPIO subsystem number space
 */
void pinmux_free_gpio(unsigned gpio)
{
	struct pinctrl_dev *pctldev;
	struct pinctrl_gpio_range *range;
	int ret;
	int pin;

	ret = pinctrl_get_device_gpio_range(gpio, &pctldev, &range);
	if (ret)
		return;

	/* Convert to the pin controllers number space */
	pin = gpio - range->base;

	pin_free(pctldev, pin);
}
EXPORT_SYMBOL_GPL(pinmux_free_gpio);

int pinmux_register_mappings(struct pinmux_map const *maps, unsigned num_maps)
{
	int ret = 0;
	int i;

	pr_debug("add %d functions\n", num_maps);
	for (i = 0; i < num_maps; i++) {
		struct pinmux *pmx;

		/* Sanity check the mapping */
		if (!maps[i].function) {
			pr_err("failed to register map %d - no function ID given\n", i);
			ret = -EINVAL;
			goto out;
		}

		if (!maps[i].dev && !maps[i].dev_name)
			pr_debug("add anonymous function %s with no device\n",
				 maps[i].function);

		/*
		 * create the state cookie holder struct pinmux for each
		 * mapping, this is what consumers will get when requesting
		 * a pinmux handle with pinmux_get()
		 */
		pmx = kzalloc(sizeof(struct pinmux), GFP_KERNEL);
		if (pmx == NULL) {
			ret = -ENOMEM;
			goto out;
		}
		mutex_init(&pmx->mutex);
		pmx->map = &maps[i];

		/* Add the pinmux */
		mutex_lock(&pinmux_list_mutex);
		list_add(&pmx->node, &pinmux_list);
		mutex_unlock(&pinmux_list_mutex);
		pr_debug("add function %s\n", maps[i].function);
	}

out:
	return ret;
}

/**
 * acquire_pins() - acquire all the pins for a certain funcion on a certain
 *	pinmux device
 * @pctldev: the device to take the pins on
 * @selector: the function selector to acquire the pins for
 * @position: the function position to acquire the pins for
 */
static int acquire_pins(struct pinctrl_dev *pctldev, unsigned selector,
			unsigned position)
{
	const struct pinmux_ops *ops = pctldev->desc->pmxops;
	unsigned *pins;
	unsigned num_pins;
	const char *func = ops->get_function_name(pctldev, selector);
	int ret;
	int i;

	ret = ops->get_function_pins(pctldev, selector, position,
				     &pins, &num_pins);
	if (ret)
		return ret;

	/* Try to allocate all pins in this pinmux map, one by one */
	for (i = 0; i < num_pins; i++) {
		ret = pin_request(pctldev, pins[i], func, false, NULL);
		if (ret) {
			pr_err("could not get pin %d for function %s "
			       "on device %s - conflicting mux mappings?\n",
			       pins[i], func ? : "(undefined)",
			       pctldev->desc->name);
			/* On error release all taken pins */
			i--; /* this pin just failed */
			for (; i >= 0; i--)
				pin_free(pctldev, pins[i]);
			return -ENODEV;
		}
	}
	return 0;
}

/**
 * release_pins() - release pins taken by earlier acquirement
 * @pctldev: the device to free the pinx on
 * @selector: the function selector to free the pins for
 * @position: the function position to release the pins for
 */
static void release_pins(struct pinctrl_dev *pctldev, unsigned selector,
			 unsigned position)
{
	const struct pinmux_ops *ops = pctldev->desc->pmxops;
	unsigned *pins;
	unsigned num_pins;
	int ret;
	int i;

	ret = ops->get_function_pins(pctldev, selector, position,
				     &pins, &num_pins);
	if (ret) {
		dev_err(&pctldev->dev, "could not get pins to release for "
			"selector %d, position %d\n",
			selector, position);
		return;
	}
	for (i = 0; i < num_pins; i++)
		pin_free(pctldev, pins[i]);
}

/**
 * pinmux_check_position() - check that the pinmux driver can supply the
 * function in a certain position
 * @pctldev: device to check the position for
 * @selector: the selector to check the position for
 * @position: the position to check
 */
static int pinmux_check_position(struct pinctrl_dev *pctldev,
				 unsigned selector, unsigned position)
{
	const struct pinmux_ops *ops = pctldev->desc->pmxops;
	unsigned posit = 0;

	/*
	 * If the driver does not support different positions for the
	 * functions, we only support position 0.
	 */
	if (!ops->list_positions) {
		if (position != 0)
			return -EINVAL;
		return 0;
	}

	/* Else check that we support this position */
	while (ops->list_positions(pctldev, selector, posit) >= 0) {
		if (posit == position)
			return 0;
		posit++;
	}

	pr_err("%s does not support pinmux position %d for function %s\n",
	       pctldev_get_name(pctldev), position,
	       ops->get_function_name(pctldev, selector));
	return -EINVAL;
}

/**
 * pinmux_search_function() - search the pinmux driver for an applicable
 * function in a specific position, returns the applicable selector if
 * found
 * @pctldev: device to check for function and position
 * @map: function map containing the function and position to look for
 */
static int pinmux_search_function(struct pinctrl_dev *pctldev,
				  struct pinmux_map const *map)
{
	const struct pinmux_ops *ops = pctldev->desc->pmxops;
	unsigned selector = 0;

	/* See if this pctldev has this function */
	while (ops->list_functions(pctldev, selector) >= 0) {
		const char *fname = ops->get_function_name(pctldev,
							   selector);
		int ret;

		if (!strcmp(map->function, fname)) {
			/* Found the function, check position */
			ret = pinmux_check_position(pctldev, selector,
						    map->position);
			if (ret < 0)
				return ret;
			return selector;
		}
		selector++;
	}

	pr_err("%s does not support function %s\n",
	       pctldev_get_name(pctldev), map->function);
	return -EINVAL;
}


/**
 * pinmux_get() - retrieves the pinmux for a certain device
 * @dev: the device to get the pinmux for
 * @name: an optional specific mux mapping name or NULL, the name is only
 *	needed if you want to have more than one mapping per device, or if you
 *	need an anonymous pinmux (not tied to any specific device)
 */
struct pinmux *pinmux_get(struct device *dev, const char *name)
{

	struct pinmux_map const *map = NULL;
	struct pinctrl_dev *pctldev = NULL;
	const char *devname = NULL;
	struct pinmux *pmx;
	bool found_map = false;
	int ret = -ENODEV;

	/* We must have dev or ID or both */
	if (!dev && !name)
		return ERR_PTR(-EINVAL);

	mutex_lock(&pinmux_list_mutex);

	if (dev)
		devname = dev_name(dev);

	/* Iterate over the pinmux maps to locate the right one */
	list_for_each_entry(pmx, &pinmux_list, node) {
		map = pmx->map;

		/*
		 * First, try to find the pctldev given in the map
		 */
		pctldev = get_pctrldev_for_pinmux_map(map);
		if (!pctldev) {
			const char *devname = NULL;

			if (map->ctrl_dev)
				devname = dev_name(map->ctrl_dev);
			else if (map->ctrl_dev_name)
				devname = map->ctrl_dev_name;

			pr_warning("could not find a pinctrl device for pinmux "
				   "function %s, fishy, they shall all have one\n",
				   map->function);
			pr_warning("given pinctrl device name: %s",
				   devname ? devname : "UNDEFINED");

			/* Continue to check the other mappings anyway... */
			continue;
		}

		pr_debug("found pctldev %s to handle function %s",
			 dev_name(&pctldev->dev), map->function);


		/*
		 * If we're looking for a specific named map, this must match,
		 * else we loop and look for the next.
		 */
		if (name != NULL) {
			if (map->name == NULL)
				continue;
			if (strcmp(map->name, name))
				continue;
		}

		/*
		 * This is for the case where no device name is given, we
		 * already know that the function name matches from above
		 * code.
		 */
		if (!map->dev_name && (name != NULL)) {
			found_map = true;
			break;
		}

		/* If the mapping has a device set up it must match */
		if (map->dev_name &&
		    (!devname || !strcmp(map->dev_name, devname))) {
			/* MATCH! */
			found_map = true;
			break;
		}
	}

	mutex_unlock(&pinmux_list_mutex);

	if (!found_map) {
		pr_err("could not find mux map for device %s, ID %s\n",
		       devname ? devname : "(anonymous)",
		       name ? name : "(undefined)");
		goto out;
	}

	/* Make sure that noone else is using this pinmux */
	mutex_lock(&pmx->mutex);
	if (pmx->dev) {
		if (pmx->dev != dev) {
			mutex_unlock(&pmx->mutex);
			pr_err("mapping already in use device %s, ID %s\n",
			       devname ? devname : "(anonymous)",
			       name ? name : "(undefined)");
			goto out;
		} else {
			/* We already fetched this and requested pins */
			mutex_unlock(&pmx->mutex);
			ret = 0;
			goto out;
		}
	}
	mutex_unlock(&pmx->mutex);

	/* Now go into the driver and try to locate function @position */
	ret = pinmux_search_function(pctldev, map);
	if (ret < 0)
		goto out;
	else {
		/* Found function @position */
		unsigned selector = ret;

		ret = acquire_pins(pctldev, selector, map->position);
		if (ret)
			goto out;
		/* Found it! */
		mutex_lock(&pmx->mutex);
		pmx->dev = dev;
		pmx->pctldev = pctldev;
		pmx->pmxdev_selector = selector;
		pmx->pmxdev_position = map->position;
		mutex_unlock(&pmx->mutex);
		ret = 0;
		goto out;
	}

	/* We couldn't find the driver for this pinmux */
	ret = -ENODEV;

out:
	if (ret)
		pmx = ERR_PTR(ret);

	return pmx;
}
EXPORT_SYMBOL_GPL(pinmux_get);

/**
 * pinmux_put() - release a previously claimed pinmux
 * @pmx: a pinmux previously claimed by pinmux_get()
 */
void pinmux_put(struct pinmux *pmx)
{
	if (pmx == NULL)
		return;
	mutex_lock(&pmx->mutex);
	if (pmx->usecount)
		pr_warn("releasing pinmux with active users!\n");
	/* Release all pins taken on pinmux_get() */
	release_pins(pmx->pctldev, pmx->pmxdev_selector, pmx->pmxdev_position);
	pmx->dev = NULL;
	pmx->pctldev = NULL;
	pmx->pmxdev_selector = 0;
	mutex_unlock(&pmx->mutex);
}
EXPORT_SYMBOL_GPL(pinmux_put);

/**
 * pinmux_enable() - enable a certain pinmux setting
 * @pmx: the pinmux to enable, previously claimed by pinmux_get()
 */
int pinmux_enable(struct pinmux *pmx)
{
	int ret = 0;

	if (pmx == NULL)
		return -EINVAL;
	mutex_lock(&pmx->mutex);
	if (pmx->usecount++ == 0) {
		struct pinctrl_dev *pctldev = pmx->pctldev;
		const struct pinmux_ops *ops = pctldev->desc->pmxops;

		ret = ops->enable(pctldev, pmx->pmxdev_selector,
				  pmx->pmxdev_position);
		if (ret)
			pmx->usecount--;
	}
	mutex_unlock(&pmx->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(pinmux_enable);

/**
 * pinmux_disable() - disable a certain pinmux setting
 * @pmx: the pinmux to disable, previously claimed by pinmux_get()
 */
void pinmux_disable(struct pinmux *pmx)
{
	if (pmx == NULL)
		return;

	mutex_lock(&pmx->mutex);
	if (--pmx->usecount == 0) {
		struct pinctrl_dev *pctldev = pmx->pctldev;
		const struct pinmux_ops *ops = pctldev->desc->pmxops;

		ops->disable(pctldev, pmx->pmxdev_selector,
			     pmx->pmxdev_position);
	}
	mutex_unlock(&pmx->mutex);
}
EXPORT_SYMBOL_GPL(pinmux_disable);

/**
 * pinmux_config() - configure a certain pinmux setting
 * @pmx: the pinmux setting to configure
 * @param: the parameter to configure
 * @data: extra data to be passed to the configuration, also works as a
 *	pointer to data returned from the function on success
 */
int pinmux_config(struct pinmux *pmx, u16 param, unsigned long *data)
{
	struct pinctrl_dev *pctldev;
	const struct pinmux_ops *ops;
	int ret = 0;

	if (pmx == NULL)
		return -ENODEV;

	pctldev = pmx->pctldev;
	ops = pctldev->desc->pmxops;

	/* This operation is not mandatory to implement */
	if (ops->config) {
		mutex_lock(&pmx->mutex);
		ret = ops->config(pctldev, pmx->pmxdev_selector, param, data);
		mutex_unlock(&pmx->mutex);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(pinmux_config);

int pinmux_check_ops(const struct pinmux_ops *ops)
{
	/* Check that we implement required operations */
	if (!ops->list_functions ||
	    !ops->get_function_name ||
	    !ops->enable ||
	    !ops->disable)
		return -EINVAL;

	return 0;
}

#ifdef CONFIG_DEBUG_FS

/* Called from pincontrol core */
static int pinmux_functions_show(struct seq_file *s, void *what)
{
	struct pinctrl_dev *pctldev = s->private;
	const struct pinmux_ops *ops = pctldev->desc->pmxops;
	unsigned selector = 0;

	while (ops->list_functions(pctldev, selector) >= 0) {
		unsigned *pins;
		unsigned num_pins;
		const char *func = ops->get_function_name(pctldev, selector);
		unsigned position = 0;
		int ret;
		int i;

		while (ops->list_positions(pctldev, selector, position) >= 0) {
			ret = ops->get_function_pins(pctldev, selector,
						     position,
						     &pins, &num_pins);

			if (ret)
				seq_printf(s, "%s [ERROR GETTING PINS]\n",
					   func);

			else {
				seq_printf(s, "function: %s, position: %d pins = [ ",
					   func, position);
				for (i = 0; i < num_pins; i++)
					seq_printf(s, "%d ", pins[i]);
				seq_puts(s, "]\n");
			}
			position++;
		}

		selector++;

	}

	return 0;
}

static int pinmux_show(struct seq_file *s, void *what)
{
	struct pinmux *pmx;
	const struct pinmux_map *map;

	seq_puts(s, "System pinmuxes and their maps:\n");
	list_for_each_entry(pmx, &pinmux_list, node) {
		map = pmx->map;

		seq_printf(s, "device: %s function: %s (%u), "
			   "pos %u users: %u map-> %s\n",
			   pmx->pctldev ? pctldev_get_name(pmx->pctldev) : "(no controller)",
			   map->function,
			   pmx->pmxdev_selector,
			   map->position,
			   pmx->usecount,
			   pmx->dev ? dev_name(pmx->dev) : "(no device)");
	}

	return 0;
}

static int pinmux_pins_show(struct seq_file *s, void *what)
{
	struct pinctrl_dev *pctldev = s->private;
	unsigned pin;

	if (pctldev == NULL) {
		seq_puts(s, "device is gone\n");
		return 0;
	}

	if (pctldev->desc == NULL) {
		seq_puts(s, "device is lacking descriptor\n");
		return 0;
	}

	seq_puts(s, "Pinmux settings per pin\n");
	seq_puts(s, "Format: pin (name): pinmuxfunction [driver specifics]\n");

	/* The highest pin number need to be included in the loop, thus <= */
	for (pin = 0; pin <= pctldev->desc->maxpin; pin++) {

		struct pin_desc *desc;

		desc = pin_desc_get(pctldev, pin);
		/* Pin space may be sparse */
		if (desc == NULL)
			continue;

		else {
			seq_printf(s, "pin %d (%s): %s", pin,
				   desc->name ? desc->name : "unnamed",
				   desc->mux_requested ? desc->mux_function : "UNCLAIMED");

			if (pctldev->desc->pmxops->dbg_show)
				pctldev->desc->pmxops->dbg_show(pctldev, s, pin);
		}
		seq_puts(s, "\n");
	}

	return 0;
}

static int pinmux_functions_open(struct inode *inode, struct file *file)
{
	return single_open(file, pinmux_functions_show, inode->i_private);
}

static int pinmux_open(struct inode *inode, struct file *file)
{
	return single_open(file, pinmux_show, NULL);
}

static int pinmux_pins_open(struct inode *inode, struct file *file)
{
	return single_open(file, pinmux_pins_show, inode->i_private);
}

static const struct file_operations pinmux_functions_ops = {
	.open		= pinmux_functions_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations pinmux_ops = {
	.open		= pinmux_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations pinmux_pins_ops = {
	.open		= pinmux_pins_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void pinmux_init_device_debugfs(struct dentry *devroot,
			 struct pinctrl_dev *pctldev)
{
	debugfs_create_file("pinmux-functions", S_IFREG | S_IRUGO,
			    devroot, pctldev, &pinmux_functions_ops);
	debugfs_create_file("pinmux-pins", S_IFREG | S_IRUGO,
			    devroot, pctldev, &pinmux_pins_ops);
}

void pinmux_init_debugfs(struct dentry *subsys_root)
{
	debugfs_create_file("pinmuxes", S_IFREG | S_IRUGO,
			    subsys_root, NULL, &pinmux_ops);
}

#endif /* CONFIG_DEBUG_FS */
