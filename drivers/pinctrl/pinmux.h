int pinmux_check_ops(const struct pinmux_ops *ops);
void pinmux_init_device_debugfs(struct dentry *devroot,
				struct pinctrl_dev *pctldev);
void pinmux_init_debugfs(struct dentry *subsys_root);
