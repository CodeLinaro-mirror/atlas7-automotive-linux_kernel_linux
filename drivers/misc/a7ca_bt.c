#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>
#include <linux/clk.h>

#define A7CA_BT_RESET           _IO('B', 0x1)
#define A7CA_BT_TRIM_READ       _IO('B', 0x2)
#define A7CA_BT_TRIM_WRITE      _IO('B', 0x3)
#define A7CA_BT_TRIM_READ_WRITE      _IO('B', 0x4)
#define A7CA_BT_SINGLE_MODE      _IO('B', 0x5)

struct a7ca_bt_trim {
	u32 reg;
	u32 val;
};

/*
* FIXME: bt driver need access atlas7 pmu for enabling BT ldo
* which will be removed later when regulator driver is ready.
*/
#define SIRFSOC_PMU_BASE		0x10E30000

#define SIRFSOC_INTC_BASE		0x10220000
#define INTC_DEV_ID_OFFSET		0x48
#define ATLAS7_CHIP_VER_A0		0x60A0

#define reg_read_write(reg, val) writel(readl(reg)|val, reg)
struct a7ca_bt_dev {
	struct cdev cdev;
	struct a7ca_bt_trim bt_trim;
	struct clk *a7ca_btss_clk;
	struct clk *a7ca_btslow_clk;
	struct clk *a7ca_io_clk;
	struct platform_device *pdev;
	struct miscdevice miscdev;
};

static int a7ca_bt_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int a7ca_bt_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static int a7ca_bt_hw_init(struct a7ca_bt_dev *dev)
{
	int err = 0;
	void __iomem *pmu_base, *intc_base;

	pmu_base = ioremap(SIRFSOC_PMU_BASE, SZ_64K);
	intc_base = ioremap(SIRFSOC_INTC_BASE, SZ_64K);

	err = clk_prepare_enable(dev->a7ca_btss_clk);
	if (err) {
		pr_debug("a7ca_btss_clk enable failed\n");
		goto out;
	}
	err = clk_prepare_enable(dev->a7ca_btslow_clk);
	if (err) {
		pr_debug("a7ca_btslow_clk enable failed\n");
		goto out;
	}
	err = clk_prepare_enable(dev->a7ca_io_clk);
	if (err) {
		pr_debug("a7ca_io_clk enable failed\n");
		goto out;
	}

	/*
	* BT LDO control bit polarity was reversed in atlas7 A1
	* comparing with A0. Check chip ID here.
	*
	* FIXME: this is a temp resolution for making BT work on
	* both A1 & A0 chips. When A0 is abandonded, the check
	* can be removed.
	*/
	if (readl(intc_base + INTC_DEV_ID_OFFSET) == ATLAS7_CHIP_VER_A0)
		reg_read_write(pmu_base + 0x58, 0xa);
	else
		reg_read_write(pmu_base + 0x58, 0x2);

out:
	return err;
}

static void a7ca_bt_reset(struct a7ca_bt_dev *dev)
{
	a7ca_bt_hw_init(dev);
}

static long a7ca_bt_ioctl(struct file *filp, unsigned int cmd,
			unsigned long arg)
{
	struct a7ca_bt_trim bt_trim = { 0 };
	int ret = 0;
	struct a7ca_bt_dev *dev =
	    container_of(filp->private_data, struct a7ca_bt_dev, miscdev);

	switch (cmd) {
	case A7CA_BT_RESET:
		pr_debug("a7ca_bt A7CA_BT_RESET\n");
		a7ca_bt_reset(dev);
		break;
	case A7CA_BT_TRIM_READ:
		pr_debug("a7ca_bt A7CA_BT_TRIM_READ\n");
		if (copy_from_user
		    (&dev->bt_trim, (void __user *)arg, sizeof(bt_trim))) {
			return -EFAULT;
		}
		dev->bt_trim.val = sirfsoc_rtc_iobrg_readl(dev->bt_trim.reg);
		if (copy_to_user
		    ((void __user *)arg, &dev->bt_trim, sizeof(bt_trim))) {
			return -EFAULT;
		}
		break;
	case A7CA_BT_TRIM_WRITE:
		pr_debug("a7ca_bt A7CA_BT_TRIM_WRITE\n");
		if (copy_from_user
		    (&dev->bt_trim, (void __user *)arg, sizeof(bt_trim))) {
			return -EFAULT;
		}
		sirfsoc_rtc_iobrg_writel(dev->bt_trim.val, dev->bt_trim.reg);
		break;
	case A7CA_BT_TRIM_READ_WRITE:
		pr_debug("a7ca_bt A7CA_BT_TRIM_WRITE\n");
		if (copy_from_user
		    (&dev->bt_trim, (void __user *)arg, sizeof(bt_trim))) {
			return -EFAULT;
		}
		sirfsoc_rtc_iobrg_writel(sirfsoc_rtc_iobrg_readl
					 (dev->bt_trim.reg) | dev->bt_trim.val,
					 dev->bt_trim.reg);
		break;
	case A7CA_BT_SINGLE_MODE:
		pr_debug("a7ca_bt enter single mode\n");
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return ret;
}

static const struct file_operations a7ca_bt_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = a7ca_bt_ioctl,
	.open = a7ca_bt_open,
	.release = a7ca_bt_release,
};

static struct a7ca_bt_dev a7ca_bt = {
	.miscdev.minor = MISC_DYNAMIC_MINOR,
	.miscdev.name = "a7ca_bt",
	.miscdev.fops = &a7ca_bt_fops,
};

static int a7ca_bt_probe(struct platform_device *pdev)
{
	int err;

	struct a7ca_bt_dev *dev = &a7ca_bt;

	dev->pdev = pdev;
	dev->a7ca_btss_clk = devm_clk_get(&pdev->dev, "a7ca_btss");
	if (IS_ERR(dev->a7ca_btss_clk)) {
		err = PTR_ERR(dev->a7ca_btss_clk);
		dev_err(&pdev->dev, "Clock get failed\n");
		goto out;
	}

	dev->a7ca_btslow_clk = devm_clk_get(&pdev->dev, "a7ca_btslow");
	if (IS_ERR(dev->a7ca_btslow_clk)) {
		err = PTR_ERR(dev->a7ca_btslow_clk);
		dev_err(&pdev->dev, "Clock get failed\n");
		goto out;
	}

	dev->a7ca_io_clk = devm_clk_get(&pdev->dev, "a7ca_io");
	if (IS_ERR(dev->a7ca_io_clk)) {
		err = PTR_ERR(dev->a7ca_io_clk);
		dev_err(&pdev->dev, "Clock a7ca_io get failed\n");
		goto out;
	}
	platform_set_drvdata(pdev, dev);

	a7ca_bt_hw_init(dev);
	err = misc_register(&dev->miscdev);
	pr_debug("a7ca_bt initilized\n");

out:
	return err;
}

static int a7ca_bt_remove(struct platform_device *pdev)
{
	struct a7ca_bt_dev *dev = platform_get_drvdata(pdev);

	misc_deregister(&dev->miscdev);
	clk_disable_unprepare(dev->a7ca_btss_clk);
	clk_disable_unprepare(dev->a7ca_btslow_clk);
	clk_disable_unprepare(dev->a7ca_io_clk);

	return 0;
}

static const struct of_device_id a7ca_bt_of_match[] = {
	{.compatible = "sirf,a7ca_bt",},
	{},
};

MODULE_DEVICE_TABLE(of, a7ca_bt_of_match);
static struct platform_driver a7ca_bt_driver = {
	.probe = a7ca_bt_probe,
	.remove = a7ca_bt_remove,
	.driver = {
		   .name = "sirf-a7ca_bt",
		   .owner = THIS_MODULE,
		   .of_match_table = a7ca_bt_of_match,
		   },
};

module_platform_driver(a7ca_bt_driver);

MODULE_AUTHOR("Wei Yang <wei.yang@csr.com>");
MODULE_LICENSE("GPL");
