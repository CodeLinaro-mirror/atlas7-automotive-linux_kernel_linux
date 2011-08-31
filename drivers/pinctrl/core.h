/**
 * struct pin_desc - pin descriptor for each physical pin in the arch
 * @pctldev: corresponding pin control device
 * @name: a name for the pin, e.g. the name of the pin/pad/finger on a
 *	datasheet or such
 * @mux_requested: whether the pin is already requested by pinmux or not
 * @mux_function: a named muxing function for the pin that will be passed to
 *	subdrivers and shown in debugfs etc
 */
struct pin_desc {
	struct pinctrl_dev *pctldev;
	char	name[16];
	/* These fields only added when supporting pinmux drivers */
#ifdef CONFIG_PINMUX
	bool	mux_requested;
	char	mux_function[16];
#endif
};

struct pin_desc *pin_desc_get(struct pinctrl_dev *pctldev, int pin);
struct pinctrl_dev *get_pctrldev_for_pinmux_map(struct pinmux_map const *map);
int pinctrl_get_device_gpio_range(unsigned gpio,
				  struct pinctrl_dev **outdev,
				  struct pinctrl_gpio_range **outrange);
