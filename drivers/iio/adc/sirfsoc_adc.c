/*
* sirfsoc ADC Driver
*
* Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
*
* Licensed under GPLv2 or later.
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/input/sirfsoc_adc.h>
#include <linux/of_platform.h>
#include <asm/irq.h>

#define DRIVER_NAME "sirfsoc_adc"

struct sirfsoc_adc {
	struct clk	*clk;
	void __iomem	*base;
	int		irq;
	struct completion	done;
	struct mutex	adc_lock;
};

static struct sirfsoc_adc *sirfsoc_adc;

int sirfsoc_adc_sync_request(struct sirfsoc_adc_request *req)
{
	int control1, control2, intr;
	int ret = -1;
	int data, reg_offset;

	mutex_lock(&sirfsoc_adc->adc_lock);

	intr = readl(sirfsoc_adc->base + ADC_INTR);
	control1 = readl(sirfsoc_adc->base + ADC_CONTROL1);
	control2 = readl(sirfsoc_adc->base + ADC_CONTROL2);
	writel(intr | DATA_INTR_EN | DATA_INTR, (sirfsoc_adc->base + ADC_INTR));

	writel(ADC_PRP_MODE3 | req->reference,
		sirfsoc_adc->base + ADC_CONTROL2);

	writel(ADC_POLL | ADC_MORE_CTL1 | req->mode |
		req->aux | req->delay_bits | ADC_RESOLUTION_12,
			sirfsoc_adc->base + ADC_CONTROL1);

	if (!wait_for_completion_timeout(&sirfsoc_adc->done,
		msecs_to_jiffies(50))) {
		ret = -EINVAL;
		goto out;
	}


	ret = 0;
	switch (req->mode) {
	case ADC_SEL(3):
		data = readl(sirfsoc_adc->base + ADC_PRESSURE);
		if ((data & DATA_Z1VALID) && (data & DATA_Z2VALID)) {
			req->adc_data.z1 = data & DATA_Z1MASK;
			req->adc_data.z2 =
			(data & DATA_Z2MASK) >> DATA_SHIFT_BITS;
			req->adc_data.datavalid = 1;
		}
		break;
	case ADC_SEL(4):
	case ADC_SEL(5):
	case ADC_SEL(6):
	case ADC_SEL(7):
	case ADC_SEL(8):
	case ADC_SEL(9):
	case ADC_SEL(11):
	case ADC_SEL(12):
	case ADC_SEL(13):
		/* FIXME, need calc correct reg index */
		if (((req->mode >> 11) == 11) ||
			((req->mode >> 11) == 12) ||
			((req->mode >> 11) == 13))
			reg_offset = 0x2C;
		else
			reg_offset = 0x14 + ((req->mode >> 11) - 0x04) * 4;
		data = readl(sirfsoc_adc->base + reg_offset);
		if ((data & DATA_AUXVALID)) {
			req->adc_data.aux = data & DATA_AUXMASK;
			req->adc_data.datavalid = 1;
		}
		break;
	default:
		break;
	}

out:
	writel(intr, sirfsoc_adc->base + ADC_INTR);
	writel(control1, sirfsoc_adc->base + ADC_CONTROL1);
	writel(control2, sirfsoc_adc->base + ADC_CONTROL2);
	mutex_unlock(&sirfsoc_adc->adc_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(sirfsoc_adc_sync_request);

void sirfsoc_adc_write_reg(u32 data, u32 offset)
{
	writel(data, sirfsoc_adc->base + offset);
}

u32 sirfsoc_adc_read_reg(u32 offset)
{
	return readl(sirfsoc_adc->base + offset);
}

int sirfsoc_adc_sync_reg(void)
{
	if (!wait_for_completion_timeout(&sirfsoc_adc->done,
		msecs_to_jiffies(50))) {
		return -1;
	}

	return 0;
}

static irqreturn_t sirfsoc_adc_data_irq(int irq, void *handle)
{
	int val;
	struct sirfsoc_adc *adc = (struct sirfsoc_adc *)handle;

	val = readl(adc->base + ADC_INTR);

	if (val & DATA_INTR) {
		writel(PEN_INTR_EN | DATA_INTR | DATA_INTR_EN,
			adc->base + ADC_INTR);
		complete(&adc->done);
	}

	return IRQ_HANDLED;
}

#ifdef CONFIG_PM
static int sirfsoc_adc_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	struct sirfsoc_adc *adc = platform_get_drvdata(pdev);
	clk_disable_unprepare(adc->clk);
	return 0;
}

static int sirfsoc_adc_resume(struct platform_device *pdev)
{
	struct sirfsoc_adc *adc = platform_get_drvdata(pdev);
	clk_prepare_enable(adc->clk);
	return 0;
}
#else
#define sirfsoc_adc_resume NULL
#define sirfsoc_adc_suspend NULL
#endif

static const struct of_device_id sirfsoc_adc_of_match[] = {
	{ .compatible = "sirf,prima2-adc",},
	{}
};

static int sirfsoc_adc_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct resource	*mem_res;
	struct sirfsoc_adc *adc;

	adc = devm_kzalloc(&pdev->dev, sizeof(struct sirfsoc_adc), GFP_KERNEL);
	if (!adc) {
		dev_err(&pdev->dev, "sirfsoc adc: Cant allocate driver private data\n");
		return -ENOMEM;
	}

	sirfsoc_adc = adc;
	platform_set_drvdata(pdev, adc);

	adc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(adc->clk)) {
		dev_err(&pdev->dev, "sirfsoc adc: get adc clk err\n");
		ret = -ENOMEM;
		goto err;
	}
	clk_prepare_enable(adc->clk);

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_res) {
		dev_err(&pdev->dev, "sirfsoc adc: Unalbe to get io resource\n");
		ret = -ENODEV;
		goto err;
	}

	adc->base = devm_request_and_ioremap(&pdev->dev, mem_res);
	if (adc->base == NULL) {
		dev_err(&pdev->dev, "sirfsoc adc: IO remap failed!\n");
		ret = -ENOMEM;
		goto err;
	}

	init_completion(&adc->done);
	mutex_init(&adc->adc_lock);

	adc->irq = platform_get_irq(pdev, 0);
	if (adc->irq < 0) {
		dev_err(&pdev->dev, "sirfsoc adc: get irq failed!\n");
		ret = -ENOMEM;
		goto err;
	}

	ret = devm_request_irq(&pdev->dev, adc->irq, sirfsoc_adc_data_irq,
		0, DRIVER_NAME, adc);

	if (ret < 0) {
		dev_err(&pdev->dev, "sirfsoc adc: regist irq handler failed!\n");
		ret = -ENODEV;
		goto err;
	}

	ret = of_platform_populate(pdev->dev.of_node, sirfsoc_adc_of_match, NULL, &pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed adding child nodes\n");
		goto err;
	}

	return 0;

err:
	return ret;
}

static int sirfsoc_adc_remove(struct platform_device *pdev)
{
	struct sirfsoc_adc *adc = platform_get_drvdata(pdev);

	clk_disable_unprepare(adc->clk);

	return 0;
}

static struct platform_driver sirfsoc_adc_driver = {
	.driver = {
		.name   = DRIVER_NAME,
		.of_match_table = sirfsoc_adc_of_match,
	},
	.probe          = sirfsoc_adc_probe,
	.remove         = sirfsoc_adc_remove,
	.suspend        = sirfsoc_adc_suspend,
	.resume         = sirfsoc_adc_resume,
};

module_platform_driver(sirfsoc_adc_driver);

MODULE_AUTHOR("sober song <zhiwu.song@csr.com>");
MODULE_DESCRIPTION("SiRF SoC On-chip ADC driver");
MODULE_LICENSE("GPL");
