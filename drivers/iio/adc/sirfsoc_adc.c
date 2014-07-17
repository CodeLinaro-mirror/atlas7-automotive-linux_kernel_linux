/*
* ADC Driver for CSR SiRFSoC
*
* Copyright (c) 2014 Cambridge Silicon Radio Limited, a CSR plc group company.
*
* Licensed under GPLv2.
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

#define SIRFSOC_PWR_WAKEEN_TSC_SHIFT	23
#define SIRFSOC_PWR_WAKEEN_TS_SHIFT	5
#define SIRFSOC_PWRC_TRIGGER_EN		0x8
#define SIRFSOC_PWRC_BASE		0x3000

/* Registers offset */
#define SIRFSOC_ADC_CONTROL1		0x00
#define SIRFSOC_ADC_CONTROL2		0x04
#define SIRFSOC_ADC_INTR		0x08
#define SIRFSOC_ADC_COORD		0x0C
#define SIRFSOC_ADC_PRESSURE		0x10
#define SIRFSOC_ADC_AUX1		0x14
/* Atlas6 AUX2 and AUX4 reserved*/
#define SIRFSOC_ADC_AUX2		0x18
#define SIRFSOC_ADC_AUX3		0x1C
#define SIRFSOC_ADC_AUX4		0x20
#define SIRFSOC_ADC_AUX5		0x24
#define SIRFSOC_ADC_AUX6		0x28
/* Read Back calibration register */
#define SIRFSOC_ADC_CB			0x2C
#define SIRFSOC_ADC_COORD2		0x30
#define SIRFSOC_ADC_COORD3		0x34
#define SIRFSOC_ADC_COORD4		0x38

/* CTRL1 defines */
#define SIRFSOC_ADC_RESET_QUANT_EN	BIT(24)
#define SIRFSOC_ADC_RST_B		BIT(23)
#define SIRFSOC_ADC_RESOLUTION_12	BIT(22)
#define SIRFSOC_ADC_RBAT_DISABLE	(0x0 << 21)
#define SIRFSOC_ADC_RBAT_ENABLE		(0x1 << 21)
#define SIRFSOC_ADC_EXTCM(x)		(((x) & 0x3) << 19)
#define SIRFSOC_ADC_SGAIN(x)		(((x) & 0x7) << 16)
#define SIRFSOC_ADC_POLL		BIT(15)
#define SIRFSOC_ADC_SEL(x)		(((x) & 0xF) << 11)
#define SIRFSOC_ADC_FREQ_6K		(0x0 << 8)
#define SIRFSOC_ADC_FREQ_13K		BIT(8)
#define SIRFSOC_ADC_DEL_SET(x)		(((x) & 0xF) << 4)
#define SIRFSOC_ADC_TP_TIME(x)		(((x) & 0x7) << 0)

/* CTRL2 defines */
/* Pen detector off, digitizer off */
#define SIRFSOC_ADC_PRP_MODE0		(0 << 14)
/* Pen detector on, digitizer off, digitizer wakes up on pen detect */
#define SIRFSOC_ADC_PRP_MODE1		BIT(14)
/* Pen detector on, digitizer off, no wake up on pen detect */
#define SIRFSOC_ADC_PRP_MODE2		(2 << 14)
/* Pen detector on, digitizer on */
#define SIRFSOC_ADC_PRP_MODE3		(3 << 14)
#define SIRFSOC_ADC_RTOUCH(x)		(((x) & 0x3) << 12)
#define SIRFSOC_ADC_DEL_PRE(x)		(((x) & 0xF) << 4)
#define SIRFSOC_ADC_DEL_DIS(x)		(((x) & 0xF) << 0)

/* INTR register defines */
#define SIRFSOC_ADC_PEN_INTR_EN		BIT(5)
#define SIRFSOC_ADC_DATA_INTR_EN	BIT(4)
#define SIRFSOC_ADC_PEN_INTR		BIT(1)
#define SIRFSOC_ADC_DATA_INTR		BIT(0)

/* DATA register defines */
#define SIRFSOC_ADC_PEN_DOWN		BIT(31)
#define SIRFSOC_ADC_DATA_AUXVALID	BIT(30)
#define SIRFSOC_ADC_DATA_CB_VALID	BIT(30)
#define SIRFSOC_ADC_ADC_DATA_MASK(x)	(0x3FFF << (x))
#define SIRFSOC_ADC_DATA_AUXMASK	SIRFSOC_ADC_ADC_DATA_MASK(0)
#define SIRFSOC_ADC_DATA_CBMASK		SIRFSOC_ADC_ADC_DATA_MASK(0)

#define SIRFSOC_ADC_MORE_CTL1	(of_machine_is_compatible("sirf,atlas6") ?\
				(SIRFSOC_ADC_RESET_QUANT_EN |\
				SIRFSOC_ADC_RST_B) : (0))

/* Select AD samples to read (SEL bits in ADC_CONTROL1 register) */
#define SIRFSOC_ADC_AUX1_SEL	0x04
#define SIRFSOC_ADC_AUX2_SEL	0x05
#define SIRFSOC_ADC_AUX3_SEL	0x06
#define SIRFSOC_ADC_AUX4_SEL	0x07
#define SIRFSOC_ADC_AUX5_SEL	0x08
#define SIRFSOC_ADC_AUX6_SEL	0x09
#define SIRFSOC_ADC_TS_SEL	0x0A    /* Sample for single touch*/
#define SIRFSOC_ADC_GROUND_SEL	0x0B    /* Gound for offset calibration */
#define SIRFSOC_ADC_BANDGAP_SEL	0x0C    /* Bandgap for gain calibration */
#define SIRFSOC_ADC_TS_SEL_DUAL	0x0F    /* Samples for dual touch */

#define SIRFSOC_ADC_TS_SAMPLE_SIZE	4
#define SIRFSOC_ADC_CTL1(sel)    (SIRFSOC_ADC_POLL | SIRFSOC_ADC_SEL(sel) |\
		SIRFSOC_ADC_DEL_SET(6) | SIRFSOC_ADC_FREQ_6K |\
		SIRFSOC_ADC_TP_TIME(0) | SIRFSOC_ADC_SGAIN(0) |\
		SIRFSOC_ADC_EXTCM(0) | SIRFSOC_ADC_RBAT_DISABLE |\
		SIRFSOC_ADC_MORE_CTL1)

enum sirfsoc_adc_chips {
	PRIMA2,
	ATLAS6,
};

struct sirfsoc_adc_chip_info {
	const struct iio_chan_spec *channels;
	unsigned int num_channels;
	unsigned int flags;

	const struct iio_info *iio_info;
	u32 (*calculate_volt)(u32, u32, u32);
	const u32 *channel_sel;
};

struct sirfsoc_adc_request {
	u16 mode;
	u16 extcm;
	u16 reference;
	u8 delay_bits;
	u32 s_gain_bits;
	u16 read_back_data;
};

struct sirfsoc_adc {
	const struct sirfsoc_adc_chip_info	*chip_info;
	struct clk	*clk;
	void __iomem	*base;
	struct sirfsoc_adc_request req;
	struct completion	done;
	struct mutex	adc_lock;
};

static const u32 sirfsoc_adc_ts_reg[SIRFSOC_ADC_TS_SAMPLE_SIZE] = {
	/* Dual touch samples read registers*/
	SIRFSOC_ADC_COORD, SIRFSOC_ADC_COORD2,
	SIRFSOC_ADC_COORD3, SIRFSOC_ADC_COORD4
};

enum sirfsoc_adc_ts_type {
	SINGLE_TOUCH,
	DUAL_TOUCH,
};

static int sirfsoc_adc_get_ts_sample(
	struct sirfsoc_adc *adc, int *sample, int touch_type)
{
	int adc_intr;
	int ret = 0;
	int i;

	mutex_lock(&adc->adc_lock);
	adc_intr = readl(adc->base + SIRFSOC_ADC_INTR);
	if (adc_intr & SIRFSOC_ADC_PEN_INTR)
		writel(SIRFSOC_ADC_PEN_INTR | SIRFSOC_ADC_PEN_INTR_EN |
			SIRFSOC_ADC_DATA_INTR_EN,
			adc->base + SIRFSOC_ADC_INTR);

	/* check pen status */
	if (!(readl(adc->base + SIRFSOC_ADC_COORD) & SIRFSOC_ADC_PEN_DOWN)) {
		ret = -EINVAL;
		goto out;
	}

	if (SINGLE_TOUCH == touch_type)
		writel(SIRFSOC_ADC_CTL1(SIRFSOC_ADC_TS_SEL),
			adc->base + SIRFSOC_ADC_CONTROL1);
	else
		writel(SIRFSOC_ADC_CTL1(SIRFSOC_ADC_TS_SEL_DUAL),
			adc->base + SIRFSOC_ADC_CONTROL1);

	if (!wait_for_completion_timeout(&adc->done,
		msecs_to_jiffies(50))) {
		ret = -EIO;
		goto out;
	}

	if (SINGLE_TOUCH == touch_type)
		*sample = readl(adc->base + SIRFSOC_ADC_COORD);
	else
		for (i = 0; i < SIRFSOC_ADC_TS_SAMPLE_SIZE; i++)
			sample[i] = readl(adc->base + sirfsoc_adc_ts_reg[i]);

out:
	mutex_unlock(&adc->adc_lock);
	return ret;
}

/* get touchscreen coordinates for single touch */
static int sirfsoc_adc_single_ts_sample(struct sirfsoc_adc *adc, int *sample)
{
	return sirfsoc_adc_get_ts_sample(adc, sample, SINGLE_TOUCH);
}

/* get touchscreen coordinates for dual touch */
static int sirfsoc_adc_dual_ts_sample(struct sirfsoc_adc *adc, int *samples)
{
	return sirfsoc_adc_get_ts_sample(adc, samples, DUAL_TOUCH);
}

static int sirfsoc_adc_send_request(struct sirfsoc_adc_request *req)
{
	struct sirfsoc_adc *adc = container_of(req, struct sirfsoc_adc, req);
	int control1, control2, intr;
	int data, reg_offset;
	int ret = 0;

	mutex_lock(&adc->adc_lock);

	/* Store registers for recover */
	intr = readl(adc->base + SIRFSOC_ADC_INTR);
	control1 = readl(adc->base + SIRFSOC_ADC_CONTROL1);
	control2 = readl(adc->base + SIRFSOC_ADC_CONTROL2);

	writel(intr | SIRFSOC_ADC_DATA_INTR_EN | SIRFSOC_ADC_DATA_INTR,
		adc->base + SIRFSOC_ADC_INTR);
	writel(SIRFSOC_ADC_PRP_MODE3 | req->reference,
		adc->base + SIRFSOC_ADC_CONTROL2);
	writel(SIRFSOC_ADC_POLL | SIRFSOC_ADC_MORE_CTL1 | req->mode |
		req->extcm | req->delay_bits | SIRFSOC_ADC_RESOLUTION_12,
		adc->base + SIRFSOC_ADC_CONTROL1);

	if (!wait_for_completion_timeout(&adc->done,
		msecs_to_jiffies(50))) {
		ret = -EIO;
		goto out;
	}

	switch (req->mode) {
	case SIRFSOC_ADC_SEL(SIRFSOC_ADC_AUX1_SEL):
	case SIRFSOC_ADC_SEL(SIRFSOC_ADC_AUX2_SEL):
	case SIRFSOC_ADC_SEL(SIRFSOC_ADC_AUX3_SEL):
	case SIRFSOC_ADC_SEL(SIRFSOC_ADC_AUX4_SEL):
	case SIRFSOC_ADC_SEL(SIRFSOC_ADC_AUX5_SEL):
	case SIRFSOC_ADC_SEL(SIRFSOC_ADC_AUX6_SEL):
		/* Calculate aux offset from mode */
		reg_offset = 0x14 + ((req->mode >> 11) - 0x04) * 4;
		data = readl(adc->base + reg_offset);
		if (data & SIRFSOC_ADC_DATA_AUXVALID)
			req->read_back_data = data & SIRFSOC_ADC_DATA_AUXMASK;
		break;
	case SIRFSOC_ADC_SEL(SIRFSOC_ADC_GROUND_SEL):
	case SIRFSOC_ADC_SEL(SIRFSOC_ADC_BANDGAP_SEL):
		reg_offset = SIRFSOC_ADC_CB;
		data = readl(adc->base + reg_offset);
		if (data & SIRFSOC_ADC_DATA_CB_VALID)
			req->read_back_data = data & SIRFSOC_ADC_DATA_CBMASK;
		break;
	default:
		break;
	}

out:
	writel(intr, adc->base + SIRFSOC_ADC_INTR);
	writel(control1, adc->base + SIRFSOC_ADC_CONTROL1);
	writel(control2, adc->base + SIRFSOC_ADC_CONTROL2);
	mutex_unlock(&adc->adc_lock);
	return ret;
}

/* Store params to calibrate */
struct sirfsoc_adc_cali_data {
	u32 digital_offset;
	u32 digital_again;
	bool is_calibration;
};

/* Offset Calibration calibrates the ADC offset error */
static u32 sirfsoc_adc_offset_cali(struct sirfsoc_adc_request *req)
{
	u32 i, digital_offset = 0, count = 0, sum = 0;
	/* To set the registers in order to get the ADC offset */
	req->mode = SIRFSOC_ADC_SEL(SIRFSOC_ADC_GROUND_SEL);
	req->extcm = SIRFSOC_ADC_EXTCM(0);
	req->s_gain_bits = SIRFSOC_ADC_SGAIN(7);
	req->delay_bits = SIRFSOC_ADC_DEL_SET(4);

	for (i = 0; i < 10; i++) {
		if (sirfsoc_adc_send_request(req))
			break;
		digital_offset = req->read_back_data;
		sum += digital_offset;
		count++;
	}
	if (!count)
		digital_offset = 0;
	else
		digital_offset = sum / count;

	return digital_offset;
}


/* Gain Calibration calibrates the ADC gain error */
static u32 sirfsoc_adc_gain_cali(struct sirfsoc_adc_request *req)
{
	u32 i, digital_gain = 0, count = 0, sum = 0;
	/* To set the registers in order to get the ADC gain */
	req->mode = SIRFSOC_ADC_SEL(SIRFSOC_ADC_BANDGAP_SEL);
	req->extcm = SIRFSOC_ADC_EXTCM(0);
	req->s_gain_bits = SIRFSOC_ADC_SGAIN(1);
	req->delay_bits = SIRFSOC_ADC_DEL_SET(4);

	for (i = 0; i < 10; i++) {
		if (sirfsoc_adc_send_request(req))
			break;
		digital_gain = req->read_back_data;
		sum += digital_gain;
		count++;
	}
	if (!count)
		digital_gain = 0;
	else
		digital_gain = sum / count;

	return digital_gain;
}

/* Absolute gain calibration */
static int sirfsoc_adc_adc_cali(struct sirfsoc_adc_request *req,
				struct sirfsoc_adc_cali_data *cali_data)
{
	cali_data->digital_offset = sirfsoc_adc_offset_cali(req);
	if (!cali_data->digital_offset)
		return -EINVAL;
	cali_data->digital_again = sirfsoc_adc_gain_cali(req);
	if (!cali_data->digital_again)
		return -EINVAL;

	return 0;
}

/* Get voltage after ADC conversion */
static u32 sirfsoc_adc_get_adc_volt(struct sirfsoc_adc *adc,
				struct sirfsoc_adc_cali_data *cali_data)
{
	u32 digital_out, volt;
	struct sirfsoc_adc_request *req = &adc->req;

	req->s_gain_bits = SIRFSOC_ADC_SGAIN(0);
	req->delay_bits = SIRFSOC_ADC_DEL_SET(4);

	/*
	 * First read original data
	 * then read calibrate errors
	 */
	sirfsoc_adc_send_request(req);
	if (req->read_back_data) {
		digital_out = req->read_back_data;
		/* Get errors for each sample */
		if (!cali_data->is_calibration) {
			if (sirfsoc_adc_adc_cali(req, cali_data))
				return 0;
			cali_data->is_calibration = true;
		}

		volt = adc->chip_info->calculate_volt(digital_out,
			cali_data->digital_offset,
			cali_data->digital_again);
	} else {
		return 0;
	}

	return volt;
}

static u32 prima2_adc_calculate_volt(u32 digital_out,
				u32 digital_offset, u32 digital_again)
{
	u32 volt, digital_ideal, digital_convert;

	/*
	 * see Equation 3.2 of SiRFPrimaII™ Internal ADC and Touch
	 * User Guide
	 */
	digital_ideal = (5522 * 1200) / (7 * 333);
	/*
	 * see Equation 3.3 of SiRFatlasVI™ Internal ADC and Touch
	 */
	digital_convert = (digital_out - 11645 * digital_offset /
		140000) * digital_ideal / (digital_again
		- digital_offset * 11645 / 70000);
	volt = 14 * 333 * digital_convert / 5522;

	return volt;
}

static u32 atlas6_adc_calculate_volt(u32 digital_out,
				u32 digital_offset, u32 digital_again)
{
	u32 volt, digital_ideal, digital_convert;

	/*
	 * see Equation 3.2 of SiRFatlasVI™ Internal ADC and Touch
	 * User Guide
	 */
	digital_ideal = (3986 * 12100) / (7 * 3333);
	/*
	 * see Equation 3.3 of SiRFatlasVI™ Internal ADC and Touch
	 */
	digital_offset &= 0xfff;
	digital_convert = abs(digital_out - 2 * digital_offset)
		* digital_ideal / (digital_again
		- digital_offset * 2);
	volt = 14 * 333 * digital_convert / 3986;
	volt = volt / 2;
	if (volt > 1500)
		volt = volt - (volt - 1500) / 15;
	else
		volt = volt + (1500 - volt) / 28;

	return volt;
}

static irqreturn_t sirfsoc_adc_data_irq(int irq, void *handle)
{
	struct iio_dev *indio_dev = handle;
	struct sirfsoc_adc *adc = iio_priv(indio_dev);
	int val;

	val = readl(adc->base + SIRFSOC_ADC_INTR);

	writel(SIRFSOC_ADC_PEN_INTR | SIRFSOC_ADC_PEN_INTR_EN |
		SIRFSOC_ADC_DATA_INTR | SIRFSOC_ADC_DATA_INTR_EN,
		adc->base + SIRFSOC_ADC_INTR);

	if (val & SIRFSOC_ADC_DATA_INTR)
		complete(&adc->done);

	return IRQ_HANDLED;
}

static int sirfsoc_adc_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val,
				int *val2,
				long mask)
{
	struct sirfsoc_adc *adc = iio_priv(indio_dev);
	struct sirfsoc_adc_cali_data cali_data;
	int ret;

	cali_data.is_calibration = false;
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (0 == chan->channel)
			ret = sirfsoc_adc_single_ts_sample(adc, val);
		else
			ret = sirfsoc_adc_dual_ts_sample(adc, val);

		if (ret)
			return ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_PROCESSED:
		adc->req.mode = SIRFSOC_ADC_SEL(
			adc->chip_info->channel_sel[chan->channel]);
		adc->req.extcm = SIRFSOC_ADC_EXTCM(0);
		*val = sirfsoc_adc_get_adc_volt(adc, &cali_data);
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sirfsoc_adc_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct sirfsoc_adc *adc = iio_priv(indio_dev);

	sirfsoc_rtc_iobrg_writel(sirfsoc_rtc_iobrg_readl(
		SIRFSOC_PWRC_BASE + SIRFSOC_PWRC_TRIGGER_EN)
		& ~BIT(SIRFSOC_PWR_WAKEEN_TSC_SHIFT),
		SIRFSOC_PWRC_BASE + SIRFSOC_PWRC_TRIGGER_EN);

	clk_disable_unprepare(adc->clk);

	return 0;
}

static int sirfsoc_adc_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct sirfsoc_adc *adc = iio_priv(indio_dev);
	int ret;
	int val;

	clk_prepare_enable(adc->clk);

	sirfsoc_rtc_iobrg_writel(sirfsoc_rtc_iobrg_readl(
		SIRFSOC_PWRC_BASE + SIRFSOC_PWRC_TRIGGER_EN) |
		BIT(SIRFSOC_PWR_WAKEEN_TS_SHIFT),
		SIRFSOC_PWRC_BASE + SIRFSOC_PWRC_TRIGGER_EN);

	ret = device_reset(dev);
	if (ret) {
		dev_err(dev, "Failed to reset\n");
		return ret;
	}

	writel(SIRFSOC_ADC_PRP_MODE3 | SIRFSOC_ADC_RTOUCH(1) |
		SIRFSOC_ADC_DEL_PRE(2) | SIRFSOC_ADC_DEL_DIS(5),
		adc->base + SIRFSOC_ADC_CONTROL2);

	val = readl(adc->base + SIRFSOC_ADC_INTR);

	/* Clear interrupts and enable PEN interrupt */
	writel(val | SIRFSOC_ADC_PEN_INTR | SIRFSOC_ADC_DATA_INTR |
		SIRFSOC_ADC_PEN_INTR_EN | SIRFSOC_ADC_DATA_INTR_EN,
		adc->base + SIRFSOC_ADC_INTR);

	return 0;
}
#endif

static const struct dev_pm_ops sirfsoc_adc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sirfsoc_adc_suspend, sirfsoc_adc_resume)
};

#define SIRFSOC_ADC_CHANNEL(_channel, _type, _name, _mask) {	\
	.type = _type,					\
	.indexed = 1,					\
	.channel = _channel,				\
	.datasheet_name = _name,			\
	.info_mask_separate = BIT(_mask),		\
}

#define SIRFSOC_ADC_TS_CHANNEL(_channel, _name)		\
	SIRFSOC_ADC_CHANNEL(_channel, IIO_VOLTAGE,	\
	_name, IIO_CHAN_INFO_RAW)

#define SIRFSOC_ADC_AUX_CHANNEL(_channel, _name)	\
	SIRFSOC_ADC_CHANNEL(_channel, IIO_VOLTAGE,	\
	_name, IIO_CHAN_INFO_PROCESSED)

static const struct iio_chan_spec prima2_adc_iio_channels[] = {
	/* Channels to get the touch data */
	SIRFSOC_ADC_TS_CHANNEL(0, "touch_coord"),
	/* Channels to get the pin input */
	SIRFSOC_ADC_AUX_CHANNEL(1, "auxiliary1"),
	SIRFSOC_ADC_AUX_CHANNEL(2, "auxiliary2"),
	SIRFSOC_ADC_AUX_CHANNEL(3, "auxiliary3"),
	SIRFSOC_ADC_AUX_CHANNEL(4, "auxiliary4"),
	SIRFSOC_ADC_AUX_CHANNEL(5, "auxiliary5"),
	SIRFSOC_ADC_AUX_CHANNEL(6, "auxiliary6"),
};

static const u32 prima2_adc_channel_sel[] = {
	0, SIRFSOC_ADC_AUX1_SEL, SIRFSOC_ADC_AUX2_SEL,
	SIRFSOC_ADC_AUX3_SEL, SIRFSOC_ADC_AUX4_SEL,
	SIRFSOC_ADC_AUX5_SEL, SIRFSOC_ADC_AUX6_SEL
};

static const struct iio_chan_spec atlas6_adc_iio_channels[] = {
	/* Channels to get the touch data */
	SIRFSOC_ADC_TS_CHANNEL(0, "touch_coord"),
	SIRFSOC_ADC_TS_CHANNEL(1, "dual_touch_coord"),
	/* Channels to get the pin input */
	SIRFSOC_ADC_AUX_CHANNEL(2, "auxiliary1"),
	/* Atlas6 has no auxiliary2 and auxiliary3 */
	SIRFSOC_ADC_AUX_CHANNEL(3, "auxiliary4"),
	SIRFSOC_ADC_AUX_CHANNEL(4, "auxiliary5"),
	SIRFSOC_ADC_AUX_CHANNEL(5, "auxiliary6"),
};

static const u32 atlas6_adc_channel_sel[] = {
	0, 0, SIRFSOC_ADC_AUX1_SEL, SIRFSOC_ADC_AUX4_SEL,
	SIRFSOC_ADC_AUX5_SEL, SIRFSOC_ADC_AUX6_SEL
};

static const struct iio_info sirfsoc_adc_info = {
	.read_raw = &sirfsoc_adc_read_raw,
	.driver_module = THIS_MODULE,
};

static const struct sirfsoc_adc_chip_info sirfsoc_adc_chip_info_tbl[] = {
	[PRIMA2] = {
		.channels = prima2_adc_iio_channels,
		.num_channels = ARRAY_SIZE(prima2_adc_iio_channels),
		.iio_info = &sirfsoc_adc_info,
		.calculate_volt = prima2_adc_calculate_volt,
		.channel_sel = prima2_adc_channel_sel,
	},
	[ATLAS6] = {
		.channels = atlas6_adc_iio_channels,
		.num_channels = ARRAY_SIZE(atlas6_adc_iio_channels),
		.iio_info = &sirfsoc_adc_info,
		.calculate_volt = atlas6_adc_calculate_volt,
		.channel_sel = atlas6_adc_channel_sel,
	},
};

static const struct of_device_id sirfsoc_adc_of_match[] = {
	{ .compatible = "sirf,prima2-adc",
	  .data = &sirfsoc_adc_chip_info_tbl[PRIMA2] },
	{ .compatible = "sirf,atlas6-adc",
	  .data = &sirfsoc_adc_chip_info_tbl[ATLAS6] },
	{}
};
MODULE_DEVICE_TABLE(of, sirfsoc_adc_of_match);

static int sirfsoc_adc_probe(struct platform_device *pdev)
{
	struct resource	*mem_res;
	struct sirfsoc_adc *adc;
	struct iio_dev *indio_dev;
	const struct of_device_id *match;
	int irq;
	int ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev,
			sizeof(struct sirfsoc_adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);

	/* ADC specific data */
	match = of_match_device(
		of_match_ptr(sirfsoc_adc_of_match), &pdev->dev);
	adc->chip_info = match->data;

	indio_dev->info = adc->chip_info->iio_info;
	indio_dev->channels = adc->chip_info->channels;
	indio_dev->num_channels = adc->chip_info->num_channels;
	indio_dev->name = "sirfsoc adc";
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->modes = INDIO_DIRECT_MODE;

	platform_set_drvdata(pdev, indio_dev);

	adc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(adc->clk)) {
		dev_err(&pdev->dev, "Get adc clk failed\n");
		ret = -ENOMEM;
		goto err;
	}
	clk_prepare_enable(adc->clk);

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_res) {
		dev_err(&pdev->dev, "Unable to get io resource\n");
		ret = -ENODEV;
		goto err;
	}

	adc->base = devm_request_and_ioremap(&pdev->dev, mem_res);
	if (!adc->base) {
		dev_err(&pdev->dev, "IO remap failed!\n");
		ret = -ENOMEM;
		goto err;
	}

	init_completion(&adc->done);
	mutex_init(&adc->adc_lock);

	sirfsoc_rtc_iobrg_writel(sirfsoc_rtc_iobrg_readl(SIRFSOC_PWRC_BASE +
		SIRFSOC_PWRC_TRIGGER_EN) | BIT(SIRFSOC_PWR_WAKEEN_TS_SHIFT),
		SIRFSOC_PWRC_BASE + SIRFSOC_PWRC_TRIGGER_EN);

	ret = device_reset(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to reset\n");
		goto err;
	}

	writel(SIRFSOC_ADC_PRP_MODE3 | SIRFSOC_ADC_RTOUCH(1) |
		SIRFSOC_ADC_DEL_PRE(2) | SIRFSOC_ADC_DEL_DIS(5),
		adc->base + SIRFSOC_ADC_CONTROL2);

	/* Clear interrupts and enable PEN INTR */
	writel(readl(adc->base + SIRFSOC_ADC_INTR) | SIRFSOC_ADC_PEN_INTR |
		SIRFSOC_ADC_DATA_INTR | SIRFSOC_ADC_PEN_INTR_EN |
		SIRFSOC_ADC_DATA_INTR_EN,  adc->base + SIRFSOC_ADC_INTR);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "Failed to get IRQ!\n");
		ret = -ENOMEM;
		goto err;
	}

	ret = devm_request_irq(&pdev->dev, irq, sirfsoc_adc_data_irq,
		0, DRIVER_NAME, indio_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register irq handler\n");
		ret = -ENODEV;
		goto err;
	}

	ret = of_platform_populate(pdev->dev.of_node, sirfsoc_adc_of_match,
		NULL, &pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to add child nodes\n");
		goto err;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register adc iio dev\n");
		goto err;
	}

	return 0;

err:
	clk_disable_unprepare(adc->clk);

	return ret;
}

static int sirfsoc_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct sirfsoc_adc *adc = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	clk_disable_unprepare(adc->clk);

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
MODULE_LICENSE("GPL v2");
