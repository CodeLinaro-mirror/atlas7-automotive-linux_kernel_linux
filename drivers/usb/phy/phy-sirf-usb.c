/*
 * USB PHY Driver for CSR SiRF SoC
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 * Rong Wang<Rong.Wang@csr.com>
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/usb/otg.h>
#include <linux/stmp_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_i2c.h>
#include <linux/of_platform.h>
#include <linux/workqueue.h>

struct sirf_phy {
	struct usb_phy		phy;
	struct clk		*clk;
	int			gpio_vbus;
	struct delayed_work	work;
};

#define DRIVER_NAME	"sirf-usbphy"
#define to_sirf_phy(p)	container_of((p), struct sirf_phy, phy)
#define USBPHY_POR	BIT(27)

static void sirf_vbus_pullup_work(struct work_struct *work)
{
	struct sirf_phy *sirf_phy =
		container_of(work, struct sirf_phy, work.work);

	if (gpio_is_valid(sirf_phy->gpio_vbus))
		gpio_set_value(sirf_phy->gpio_vbus, 1);
}

static inline void sirf_phy_por(void __iomem *base)
{
	writel(readl(base) | USBPHY_POR, base);
	udelay(15);
	writel(readl(base) & ~USBPHY_POR, base);
}

static int sirf_phy_init(struct usb_phy *phy)
{
	struct sirf_phy *sirf_phy = to_sirf_phy(phy);

	clk_prepare_enable(sirf_phy->clk);
	sirf_phy_por(phy->io_priv);

	return 0;
}

static void sirf_phy_shutdown(struct usb_phy *phy)
{
	struct sirf_phy *sirf_phy = to_sirf_phy(phy);
	clk_disable_unprepare(sirf_phy->clk);
}

static int sirf_phy_set_suspend(struct usb_phy *phy, int suspend)
{
	struct sirf_phy *sirf_phy = to_sirf_phy(phy);

	if (suspend) {
		if (gpio_is_valid(sirf_phy->gpio_vbus))
			gpio_set_value(sirf_phy->gpio_vbus, 0);
		clk_disable_unprepare(sirf_phy->clk);
	} else {
		clk_prepare_enable(sirf_phy->clk);
		sirf_phy_por(phy->io_priv);
		schedule_delayed_work(&sirf_phy->work, msecs_to_jiffies(100));
	}

	return 0;
}

static int sirf_phy_on_connect(struct usb_phy *phy,
		enum usb_device_speed speed)
{
	return 0;
}

static int sirf_phy_on_disconnect(struct usb_phy *phy,
		enum usb_device_speed speed)
{
	return 0;
}

static int
sirf_phy_set_peripheral(struct usb_otg *otg, struct usb_gadget *gadget)
{
	dev_info(otg->phy->dev, "set_peripheral\n");
	if (!otg)
		return -ENODEV;

	if (!gadget) {
		otg->gadget = NULL;
		return -ENODEV;
	}

	otg->gadget = gadget;
	otg->phy->state = OTG_STATE_B_IDLE;
	return 0;
}

static int sirf_phy_set_host(struct usb_otg *otg, struct usb_bus *host)
{
	dev_info(otg->phy->dev, "set_host\n");
	if (!otg)
		return -ENODEV;

	if (!host) {
		otg->host = NULL;
		return -ENODEV;
	}

	otg->host = host;
	return 0;
}

static int sirf_phy_probe(struct platform_device *pdev)
{
	struct resource *res;
	void __iomem *base;
	struct clk *clk;
	struct sirf_phy *sirf_phy;
	int ret;
	int gpio_vbus = -1;

	/*
	 * PrimaII
	 *	USB0/USB1 with UTMI interface (integrated PHY)
	 * AtlasVI
	 *	USB0 with ULPI interface (external PHY)
	 *	USB1 with UTMI interface (integrated PHY) and gpio simulated VBus
	 */
	if (of_device_is_compatible(pdev->dev.of_node, "sirf,atlas6-usbphy")) {
		const char *phy_type;
		phy_type = of_get_property(pdev->dev.of_node, "phy_type", NULL);
		if (!phy_type) {
			dev_err(&pdev->dev, "UTMI or ULPI ?\n");
			return -ENODEV;
		} else if (!strcasecmp(phy_type, "utmi")) {
			gpio_vbus = of_get_named_gpio(pdev->dev.of_node, "vbus-gpios", 0);
			if (gpio_is_valid(gpio_vbus)) {
				ret = devm_gpio_request(&pdev->dev, gpio_vbus, "ci13xxx_sirf");
				if (ret) {
					dev_err(&pdev->dev, "Failed to request GPIO VBus\n");
					return -ENODEV;
				}
				gpio_direction_output(gpio_vbus, 1);
			} else {
				dev_err(&pdev->dev, "Invalid GPIO VBus\n");
				return -ENODEV;
			}
		} else if (!strcasecmp(phy_type, "ulpi")) {
			struct pinctrl *pinctrl;
			struct i2c_client *phy_client;
			struct device_node *np;

			pinctrl = devm_pinctrl_get_select_default(&pdev->dev);
			if (IS_ERR(pinctrl)) {
				dev_err(&pdev->dev, "Failed to request pinctrl\n");
				return PTR_ERR(pinctrl);
			}

			np = of_find_compatible_node(NULL, NULL, "sirf,phy");
			if (!np) {
				dev_err(&pdev->dev, "Fail to find PHY i2c node\n");
				return -EINVAL;
			}

			phy_client = of_find_i2c_device_by_node(np);
			if (!phy_client) {
				dev_err(&pdev->dev, "Fail to get PHY i2c client\n");
				return -EINVAL;
			}

			ret = i2c_smbus_write_byte_data(phy_client, 0x7, 0x1);
			if (ret) {
				dev_err(&pdev->dev, "Fail to write i2c client\n");
				return -EINVAL;
			}
		} else {
			dev_err(&pdev->dev, "Unknown PHY type!\n");
			return -ENODEV;
		}
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev,
			"Can't get the clock, err=%ld", PTR_ERR(clk));
		return PTR_ERR(clk);
	}

	sirf_phy = devm_kzalloc(&pdev->dev, sizeof(*sirf_phy), GFP_KERNEL);
	if (!sirf_phy) {
		dev_err(&pdev->dev, "Failed to allocate USB PHY structure!\n");
		return -ENOMEM;
	}

	sirf_phy->phy.otg = devm_kzalloc(&pdev->dev,
				sizeof(*sirf_phy->phy.otg), GFP_KERNEL);
	if (!sirf_phy->phy.otg) {
		dev_err(&pdev->dev, "Failed to allocate USB OTG structure!\n");
		return -ENOMEM;
	}

	sirf_phy->phy.io_priv		= base;
	sirf_phy->gpio_vbus		= gpio_vbus;
	sirf_phy->phy.dev		= &pdev->dev;
	sirf_phy->phy.label		= DRIVER_NAME;
	sirf_phy->phy.init		= sirf_phy_init;
	sirf_phy->phy.shutdown		= sirf_phy_shutdown;
	sirf_phy->phy.set_suspend	= sirf_phy_set_suspend;
	sirf_phy->phy.notify_connect	= sirf_phy_on_connect;
	sirf_phy->phy.notify_disconnect	= sirf_phy_on_disconnect;

	sirf_phy->phy.otg->phy			= &sirf_phy->phy;
	sirf_phy->phy.otg->set_host		= sirf_phy_set_host;
	sirf_phy->phy.otg->set_peripheral	= sirf_phy_set_peripheral;

	ATOMIC_INIT_NOTIFIER_HEAD(&sirf_phy->phy.notifier);
	INIT_DELAYED_WORK(&sirf_phy->work, sirf_vbus_pullup_work);

	sirf_phy->clk = clk;

	platform_set_drvdata(pdev, &sirf_phy->phy);

	ret = usb_add_phy_dev(&sirf_phy->phy);
	if (ret)
		return ret;
	dev_info(&pdev->dev, "Ready\n");
	return 0;
}

static int sirf_phy_remove(struct platform_device *pdev)
{
	struct sirf_phy *sirf_phy = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&sirf_phy->work);
	usb_remove_phy(&sirf_phy->phy);
	return 0;
}

static const struct of_device_id sirf_phy_dt_ids[] = {
	{ .compatible = "sirf,prima2-usbphy", },
	{ .compatible = "sirf,atlas6-usbphy", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sirf_phy_dt_ids);

static struct platform_driver sirf_phy_driver = {
	.probe = sirf_phy_probe,
	.remove = sirf_phy_remove,
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = sirf_phy_dt_ids,
	 },
};

module_platform_driver(sirf_phy_driver);

MODULE_AUTHOR("Rong Wang <Rong.Wang@csr.com>");
MODULE_DESCRIPTION("SiRF CI13XXX USB PHY driver");
MODULE_LICENSE("GPL v2");
