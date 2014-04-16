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
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/reset.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>

#include <linux/iio/iio.h>
#include <linux/iio/machine.h>
#include <linux/iio/driver.h>

#define DRIVER_NAME "sirfsoc_adc"

#define PWR_WAKEEN_TSC_SHIFT	23
#define PWR_WAKEEN_TS_SHIFT	5
#define SIRFSOC_PWRC_TRIGGER_EN	0x8
#define SIRFSOC_PWRC_BASE	0x3000

#define DATA_SHIFT_BITS		14

#define ADC_CONTROL1		0x00
#define ADC_CONTROL2		0x04
#define ADC_INTR		0x08
#define ADC_COORD		0x0C
#define ADC_PRESSURE		0x10
#define ADC_AUX1		0x14
#define ADC_AUX2		0x18
#define ADC_AUX3		0x1C
#define ADC_AUX4		0x20
#define ADC_AUX5		0x24
#define ADC_AUX6		0x28
#define ADC_CB			0x2C
#define ADC_COORD2		0x30
#define ADC_COORD3		0x34
#define ADC_COORD4		0x38
#define ADC_CONTROL3		0x3C

/* CTRL1 defines */
#define ADC_RESET_QUANT_EN	BIT(24)
#define ADC_RST_B		BIT(23)
#define ADC_RESOLUTION_12	BIT(22)
#define ADC_RBAT_DISABLE	(0x0 << 21)
#define ADC_RBAT_ENABLE		(0x1 << 21)
#define ADC_EXTCM_MASK		(0x3 << 19)
#define ADC_EXTCM(x)		(((x) & 0x3) << 19)
#define ADC_SGAIN_MASK		(0x7 << 16)
#define ADC_SGAIN(x)		(((x) & 0x7) << 16)
#define ADC_POLL		BIT(15)
#define ADC_SEL_MASK		(0xF << 11)
#define ADC_SEL(x)		 (((x) & 0xF) << 11)
#define ADC_FREQ_6K		(0x0 << 8)
#define ADC_FREQ_13K		(0x1 << 8)
#define ADC_DEL_SET_MASK	(0xF << 4)
#define ADC_DEL_SET(x)		(((x) & 0xF) << 4)
#define ADC_TP_TIME_MASK	(0x7)
#define ADC_TP_TIME(x)		(((x) & 0x7) << 0)

/* CTRL2 defines */
#define ADC_PRP_MASK		(3 << 14)
/* Pen detector off, digitizer off */
#define ADC_PRP_MODE0		(0 << 14)
/* Pen detector on, digitizer off, digitizer wakes up on pen detect */
#define ADC_PRP_MODE1		BIT(14)
/* Pen detector on, digitizer off, no wake up on pen detect */
#define ADC_PRP_MODE2		(2 << 14)
/* Pen detector on, digitizer on */
#define ADC_PRP_MODE3		(3 << 14)
#define ADC_RTOUCH_MASK		(0x3 << 12)
#define ADC_RTOUCH(x)		(((x) & 0x3) << 12)
#define ADC_DEL_AUTO_MASK	(0xF << 8)
#define ADC_DEL_AUTO(x)		(((x) & 0xF) << 8)
#define ADC_DEL_PRE(x)		(((x) & 0xF) << 4)
#define ADC_DEL_DIS(x)		(((x) & 0xF) << 0)

/* INTR register defines */
#define PEN_INTR_EN		BIT(5)
#define DATA_INTR_EN		BIT(4)
#define PEN_INTR		BIT(1)
#define DATA_INTR		BIT(0)

/* DATA register defines */
#define PEN_DOWN		BIT(31)
#define DATA_YVALID		BIT(30)
#define DATA_XVALID		BIT(29)
#define DATA_Z2VALID		BIT(30)
#define DATA_Z1VALID		BIT(29)
#define DATA_AUXVALID		BIT(30)
#define DATA_CB_VALID		BIT(30)
#define DATA_Y2VALID		BIT(30)
#define DATA_X2VALID		BIT(29)
#define DATA_6VALID		BIT(30)
#define DATA_5VALID		BIT(29)
#define DATA_8VALID		BIT(30)
#define DATA_7VALID		BIT(29)

#define ADC_DATA_MASK(x)	(0x3FFF << (x))
#define DATA_XMASK		ADC_DATA_MASK(0)
#define DATA_YMASK		ADC_DATA_MASK(DATA_SHIFT_BITS)
#define DATA_Z1MASK		ADC_DATA_MASK(0)
#define DATA_Z2MASK		ADC_DATA_MASK(DATA_SHIFT_BITS)
#define DATA_AUXMASK		ADC_DATA_MASK(0)
#define DATA_CBMASK		ADC_DATA_MASK(0)
#define DATA_X2MASK		ADC_DATA_MASK(0)
#define DATA_Y2MASK		ADC_DATA_MASK(DATA_SHIFT_BITS)
#define DATA_5MASK		ADC_DATA_MASK(0)
#define DATA_6MASK		ADC_DATA_MASK(DATA_SHIFT_BITS)
#define DATA_7MASK		ADC_DATA_MASK(0)
#define DATA_8MASK		ADC_DATA_MASK(DATA_SHIFT_BITS)

#define ADC_IDEAL_RELA_RESULT	11597
#define ADC_IDEAL_ABSO_RESULT	9446

#define ADC_MORE_CTL1		(of_machine_is_compatible("sirf,atlas6") ?\
					(ADC_RESET_QUANT_EN | ADC_RST_B) : (0))

/* Select AD samples to read (SEL bits in ADC_CONTROL1 register) */
#define SIRFSOC_ADC_AUX1_SEL	0x04
#define SIRFSOC_ADC_AUX4_SEL	0x07
#define SIRFSOC_ADC_AUX5_SEL	0x08
#define SIRFSOC_ADC_AUX6_SEL	0x09
#define SIRFSOC_ADC_TS_SEL	0x0A    /* xy sample */
#define SIRFSOC_ADC_TS_SEL_DUAL	0x0F    /* samples for dual touch */
#define SIRFSOC_ADC_TS_SAMPLE_SIZE	4
#define SIRFSOC_ADC_CTL1(sel)    (ADC_POLL | ADC_SEL(sel) | ADC_DEL_SET(6) \
		| ADC_FREQ_6K | ADC_TP_TIME(0) | ADC_SGAIN(0) \
		| ADC_EXTCM(0) | ADC_RBAT_DISABLE | ADC_MORE_CTL1)

#define SIRFSOC_ADC_CHANNEL(_index) {			\
	.type = IIO_VOLTAGE,				\
	.indexed = 1,					\
	.channel = _index,				\
	.address = _index,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
}

/* high priority queue with the bigger value */
enum sirfsoc_adc_service_t {
	SIRFSOC_ADC_SERVICE_AUX = 0,
	SIRFSOC_ADC_SERVICE_TS_PARADOX,
	SIRFSOC_ADC_SERVICE_MAX,
};

enum sirfsoc_adc_req_status_t {
	SIRFSOC_ADC_REQ_NONE = 0,
	SIRFSOC_ADC_REQ_ACTIVE,
	SIRFSOC_ADC_REQ_BUSY,
	SIRFSOC_ADC_REQ_MAX,
};

enum sirfsoc_adc_iio_channel {
	CHANNEL_COORD,
	CHANNEL_COORD_DUAL,
	CHANNEL_AUX1,
	CHANNEL_AUX4,
	CHANNEL_AUX5,
	CHANNEL_AUX6,
};

struct sirfsoc_adc_data {
	u16 x;
	u16 y;
	u16 z1;
	u16 z2;
	u16 aux;
	u8 datavalid;
};

struct sirfsoc_adc_request {
	enum sirfsoc_adc_service_t type;
	struct sirfsoc_adc_data adc_data;
	u16 mode;
	u16 aux;
	u16 reference;
	u8 delay_bits;
	u32 s_gain_bits;
	enum sirfsoc_adc_req_status_t req_status;
};

struct sirfsoc_adc {
	struct clk	*clk;
	void __iomem	*base;
	int		irq;
	struct sirfsoc_adc_request req;
	struct completion	done;
	struct mutex	adc_lock;
};

static int sirfsoc_adc_send_request(struct sirfsoc_adc_request *req)
{
	struct sirfsoc_adc *adc = container_of(req, struct sirfsoc_adc, req);
	int control1, control2, intr;
	int data, reg_offset;
	int ret = 0;

	mutex_lock(&adc->adc_lock);

	intr = readl(adc->base + ADC_INTR);
	control1 = readl(adc->base + ADC_CONTROL1);
	control2 = readl(adc->base + ADC_CONTROL2);
	writel(intr | DATA_INTR_EN | DATA_INTR, (adc->base + ADC_INTR));

	writel(ADC_PRP_MODE3 | req->reference,
		adc->base + ADC_CONTROL2);

	writel(ADC_POLL | ADC_MORE_CTL1 | req->mode |
		req->aux | req->delay_bits | ADC_RESOLUTION_12,
			adc->base + ADC_CONTROL1);

	if (!wait_for_completion_timeout(&adc->done,
		msecs_to_jiffies(50))) {
		ret = -EINVAL;
		goto out;
	}

	switch (req->mode) {
	case ADC_SEL(3):
		data = readl(adc->base + ADC_PRESSURE);
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
		reg_offset = 0x14 + ((req->mode >> 11) - 0x04) * 4;
		data = readl(adc->base + reg_offset);
		if ((data & DATA_AUXVALID)) {
			req->adc_data.aux = data & DATA_AUXMASK;
			req->adc_data.datavalid = 1;
		}
		break;
	case ADC_SEL(11):
	case ADC_SEL(12):
	case ADC_SEL(13):
		reg_offset = 0x2C;
		data = readl(adc->base + reg_offset);
		if ((data & DATA_AUXVALID)) {
			req->adc_data.aux = data & DATA_AUXMASK;
			req->adc_data.datavalid = 1;
		}
		break;
	default:
		break;
	}

out:
	writel(intr, adc->base + ADC_INTR);
	writel(control1, adc->base + ADC_CONTROL1);
	writel(control2, adc->base + ADC_CONTROL2);
	mutex_unlock(&adc->adc_lock);
	return ret;
}

/*struct store params to calibrate*/
struct sirfsoc_adc_cali_data {
	u32 digital_offset;
	u32 digital_again;
	u32 digital_ideal;
	bool is_calibration;
};

static u32 sirfsoc_adc_offset_cali(struct sirfsoc_adc_request *req)
{
	u32 i, digital_offset = 0, count = 0, sum = 0;
	/* To set the reigsters in order to get the ADC offset */
	req->mode = ADC_SEL(11);
	req->aux = 0x2C;
	req->s_gain_bits = ADC_SGAIN(7);
	req->delay_bits = ADC_DEL_SET(4);
	req->req_status = SIRFSOC_ADC_REQ_NONE;

	for (i = 0; i < 10; i++) {
		if (unlikely(sirfsoc_adc_send_request(req)))
			break;
		digital_offset = req->adc_data.aux;
		/* Maybe the value is wrong, so remove it use experience */
		if (digital_offset < 230 && digital_offset > 130) {
			sum += digital_offset;
			count++;
		}
	}
	if (!sum || !count)
		digital_offset = 170;
	else
		digital_offset = sum / count;

	return digital_offset;
}


/* For ADC gain calibration */
static u32 sirfsoc_adc_gain_cali(struct sirfsoc_adc_request *req)
{
	u32 i, digital_gain = 0, count = 0, sum = 0;
	/* To set the reigsters in order to get the ADC gain */
	req->mode = ADC_SEL(12);
	req->aux = 0x2C;
	req->s_gain_bits = ADC_SGAIN(0);
	req->delay_bits = ADC_DEL_SET(4);
	req->req_status = SIRFSOC_ADC_REQ_NONE;

	for (i = 0; i < 10; i++) {
		if (unlikely(sirfsoc_adc_send_request(req)))
			break;
		digital_gain = req->adc_data.aux;
		/* Maybe the value is wrong, so remove it use experience */
		if (digital_gain < 6500 && digital_gain > 5500) {
			sum += digital_gain;
			count++;
		}
	}
	if (!sum || !count)
		digital_gain = 5555;
	else
		digital_gain = sum / count;

	return digital_gain;
}

/* For ADC digital IDEAL */
static int sirfsoc_adc_adc_cali(struct sirfsoc_adc_request *req,
				struct sirfsoc_adc_cali_data *cali_data)
{
	cali_data->digital_offset = sirfsoc_adc_offset_cali(req);
	if (!(cali_data->digital_offset))
		return -EINVAL;
	cali_data->digital_again = sirfsoc_adc_gain_cali(req);
	if (!(cali_data->digital_again))
		return -EINVAL;
	/* the ideal ADC conversion result for
		PrimaII A1 which show in the ADC spec */
	cali_data->digital_ideal = (16384 * 1200) / (14 * 333);
	return 0;
}


/* For Get voltage after ADC conversion */
static u32 sirfsoc_adc_get_adc_volt(struct sirfsoc_adc *adc,
				struct sirfsoc_adc_cali_data *cali_data)
{
	u32 digital_out, digital_convert, volt;
	struct sirfsoc_adc_request *req = &adc->req;
	if (!(cali_data->is_calibration)) {
		if (sirfsoc_adc_adc_cali(req, cali_data))
			return 0;
		cali_data->is_calibration = true;
	}
	/* To set the reigsters in order to get the ADC output */
	req->s_gain_bits = ADC_SGAIN(0);
	req->delay_bits = ADC_DEL_SET(4);

	sirfsoc_adc_send_request(req);
	if (req->adc_data.aux) {
		digital_out = req->adc_data.aux;
		/*
		 * The equation is to calibration the digital value out form
		 * ADC using the offset and absolute gain for PrmaII A1
		 * which can find in the adc spec
		 */
		digital_convert = ((digital_out - 2 * cali_data->digital_offset
			* 11645 / 140000) * cali_data->digital_ideal)
			/ (cali_data->digital_again -
			2 * cali_data->digital_offset
			* 11645 / 140000);
		volt = (1200 * digital_convert) / cali_data->digital_ideal;
		volt = volt * 2;

	} else {
		return 0;
	}

	return volt;
}

/*Get the single touch coord*/
static int sirfsoc_adc_single_ts_sample(struct sirfsoc_adc *adc, int *sample)
{
	int adc_intr;

	adc_intr = readl(adc->base + ADC_INTR);
	if (adc_intr & PEN_INTR)
		writel(PEN_INTR | PEN_INTR_EN | DATA_INTR_EN,
			adc->base + ADC_INTR);

	/*check if the touch lift up*/
	if (!(readl(adc->base + ADC_COORD) & PEN_DOWN))
		return -EINVAL;

	writel(SIRFSOC_ADC_CTL1(SIRFSOC_ADC_TS_SEL),
		adc->base + ADC_CONTROL1);

	if (!wait_for_completion_timeout(&adc->done,
		msecs_to_jiffies(50))) {
		return -EBUSY;
	}

	*sample = readl(adc->base + ADC_COORD);
	return 0;
}

static const u32 sirfsoc_adc_ts_reg[SIRFSOC_ADC_TS_SAMPLE_SIZE] = {
	ADC_COORD, ADC_COORD2, ADC_COORD3, ADC_COORD4
};

/*Get the dual touch coords*/
static int sirfsoc_adc_dual_ts_sample(struct sirfsoc_adc *adc, int *samples)
{
	int adc_intr;
	int i;

	adc_intr = readl(adc->base + ADC_INTR);
	if (adc_intr & PEN_INTR)
		writel(PEN_INTR | PEN_INTR_EN | DATA_INTR_EN,
			adc->base + ADC_INTR);

	/*check if the touch lift up*/
	if (!(readl(adc->base + ADC_COORD) & PEN_DOWN))
		return -EINVAL;

	writel(SIRFSOC_ADC_CTL1(SIRFSOC_ADC_TS_SEL_DUAL),
		adc->base + ADC_CONTROL1);

	if (!wait_for_completion_timeout(&adc->done,
		msecs_to_jiffies(50))) {
		return -EBUSY;
	}

	for (i = 0; i < SIRFSOC_ADC_TS_SAMPLE_SIZE; i++)
		samples[i] = readl(adc->base + sirfsoc_adc_ts_reg[i]);

	return 0;
}

static int sirfsoc_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val,
				int *val2,
				long mask)
{
	struct sirfsoc_adc *adc = iio_priv(indio_dev);
	struct sirfsoc_adc_cali_data cali_data;
	int ret = 0;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->channel) {
		case CHANNEL_COORD:
			ret = sirfsoc_adc_single_ts_sample(adc, val);
			break;
		case CHANNEL_COORD_DUAL:
			ret = sirfsoc_adc_dual_ts_sample(adc, val);
			break;
		case CHANNEL_AUX1:
			/* TO set reigsters to get corresponding output */
			adc->req.mode = ADC_SEL(SIRFSOC_ADC_AUX1_SEL);
			adc->req.aux = ADC_AUX1;
			*val = sirfsoc_adc_get_adc_volt(adc,
						&cali_data);
			break;
		case CHANNEL_AUX4:
			adc->req.mode = ADC_SEL(SIRFSOC_ADC_AUX4_SEL);
			adc->req.aux = ADC_AUX4;
			*val = sirfsoc_adc_get_adc_volt(adc,
						&cali_data);
			break;
		case CHANNEL_AUX5:
			adc->req.mode = ADC_SEL(SIRFSOC_ADC_AUX5_SEL);
			adc->req.aux = ADC_AUX5;
			*val = sirfsoc_adc_get_adc_volt(adc,
						&cali_data);
			break;
		case CHANNEL_AUX6:
			adc->req.mode = ADC_SEL(SIRFSOC_ADC_AUX6_SEL);
			adc->req.aux = ADC_AUX6;
			*val = sirfsoc_adc_get_adc_volt(adc,
						&cali_data);
			break;
		}

		if (ret)
			return ret;

		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
	return 0;
}

static irqreturn_t sirfsoc_adc_data_irq(int irq, void *handle)
{
	struct iio_dev *indio_dev = handle;
	struct sirfsoc_adc *adc = iio_priv(indio_dev);
	int val;

	val = readl(adc->base + ADC_INTR);

	if (val & DATA_INTR) {
		writel(PEN_INTR_EN | DATA_INTR | DATA_INTR_EN,
			adc->base + ADC_INTR);
		complete(&adc->done);
	}

	return IRQ_HANDLED;
}

#ifdef CONFIG_PM_SLEEP
static int sirfsoc_adc_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct sirfsoc_adc *adc = iio_priv(indio_dev);

	sirfsoc_rtc_iobrg_writel(sirfsoc_rtc_iobrg_readl(
		SIRFSOC_PWRC_BASE + SIRFSOC_PWRC_TRIGGER_EN)
		& ~(1 << PWR_WAKEEN_TSC_SHIFT),
		SIRFSOC_PWRC_BASE + SIRFSOC_PWRC_TRIGGER_EN);

	clk_disable_unprepare(adc->clk);
	return 0;
}

static int sirfsoc_adc_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct sirfsoc_adc *adc = iio_priv(indio_dev);
	int val;

	clk_prepare_enable(adc->clk);

	sirfsoc_rtc_iobrg_writel(sirfsoc_rtc_iobrg_readl(
		SIRFSOC_PWRC_BASE + SIRFSOC_PWRC_TRIGGER_EN)
		| (1 << PWR_WAKEEN_TS_SHIFT),
		SIRFSOC_PWRC_BASE + SIRFSOC_PWRC_TRIGGER_EN);

	device_reset(dev);

	writel(ADC_PRP_MODE3 | ADC_RTOUCH(1) | ADC_DEL_PRE(2) |
		ADC_DEL_DIS(5),  adc->base + ADC_CONTROL2);

	val = readl(adc->base + ADC_INTR);

	/* Clear interrupts and enable PEN INTR */
	writel(val | PEN_INTR | DATA_INTR | PEN_INTR_EN |
		DATA_INTR_EN,  adc->base + ADC_INTR);

	return 0;
}
#endif

static const struct dev_pm_ops sirfsoc_adc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sirfsoc_adc_suspend, sirfsoc_adc_resume)
};

static const struct of_device_id sirfsoc_adc_of_match[] = {
	{ .compatible = "sirf,prima2-adc",},
	{}
};

static struct iio_chan_spec const sirfsoc_adc_iio_channels[] = {
	SIRFSOC_ADC_CHANNEL(CHANNEL_COORD),
	SIRFSOC_ADC_CHANNEL(CHANNEL_COORD_DUAL),
	SIRFSOC_ADC_CHANNEL(CHANNEL_AUX1),
	SIRFSOC_ADC_CHANNEL(CHANNEL_AUX4),
	SIRFSOC_ADC_CHANNEL(CHANNEL_AUX5),
	SIRFSOC_ADC_CHANNEL(CHANNEL_AUX6),
};

static const struct iio_info sirfsoc_adc_info = {
	.read_raw = &sirfsoc_read_raw,
	.driver_module = THIS_MODULE,
};

static int sirfsoc_adc_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct resource	*mem_res;
	struct sirfsoc_adc *adc;
	struct iio_dev *indio_dev;

	indio_dev = iio_device_alloc(sizeof(struct sirfsoc_adc));
	if (!indio_dev)
		return -ENOMEM;

	indio_dev->info = &sirfsoc_adc_info;
	indio_dev->channels = sirfsoc_adc_iio_channels;
	indio_dev->num_channels = ARRAY_SIZE(sirfsoc_adc_iio_channels);
	indio_dev->name = "sirfsoc adc";
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->modes = INDIO_DIRECT_MODE;
	adc = iio_priv(indio_dev);

	platform_set_drvdata(pdev, indio_dev);

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

	sirfsoc_rtc_iobrg_writel(sirfsoc_rtc_iobrg_readl(SIRFSOC_PWRC_BASE +
		SIRFSOC_PWRC_TRIGGER_EN) | (1 << PWR_WAKEEN_TS_SHIFT),
		SIRFSOC_PWRC_BASE + SIRFSOC_PWRC_TRIGGER_EN);

	device_reset(&pdev->dev);

	writel(ADC_PRP_MODE3 | ADC_RTOUCH(1) | ADC_DEL_PRE(2) |
		ADC_DEL_DIS(5),  adc->base + ADC_CONTROL2);

	/* Clear interrupts and enable PEN INTR */
	writel(readl(adc->base + ADC_INTR) | PEN_INTR | DATA_INTR |
		PEN_INTR_EN | DATA_INTR_EN,  adc->base + ADC_INTR);

	adc->irq = platform_get_irq(pdev, 0);
	if (adc->irq < 0) {
		dev_err(&pdev->dev, "sirfsoc adc: get irq failed!\n");
		ret = -ENOMEM;
		goto err;
	}

	ret = devm_request_irq(&pdev->dev, adc->irq, sirfsoc_adc_data_irq,
		0, DRIVER_NAME, indio_dev);

	if (ret < 0) {
		dev_err(&pdev->dev, "sirfsoc adc: regist irq handler failed!\n");
		ret = -ENODEV;
		goto err;
	}

	ret = of_platform_populate(pdev->dev.of_node, sirfsoc_adc_of_match,
		NULL, &pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed adding child nodes\n");
		goto err;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "failed register adc iio dev\n");
		goto err;
	}

	return 0;

err:
	iio_device_free(indio_dev);
	return ret;
}

static int sirfsoc_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct sirfsoc_adc *adc = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	clk_disable_unprepare(adc->clk);
	iio_device_free(indio_dev);

	return 0;
}

static struct platform_driver sirfsoc_adc_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.of_match_table = sirfsoc_adc_of_match,
		.pm	= &sirfsoc_adc_pm_ops,
	},
	.probe		= sirfsoc_adc_probe,
	.remove		= sirfsoc_adc_remove,
};

module_platform_driver(sirfsoc_adc_driver);

MODULE_AUTHOR("Guoying Zhang <Guoying.Zhang@csr.com>");
MODULE_DESCRIPTION("SiRF SoC On-chip ADC driver");
MODULE_LICENSE("GPL");
