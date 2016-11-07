/*
 * ADC Driver for CSR SiRFSoC
 *
 * Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
#include <linux/regulator/consumer.h>

#include <linux/iio/iio.h>
#include <linux/iio/machine.h>
#include <linux/iio/driver.h>

#define DRIVER_NAME "sirfsoc_adc"

#define SIRFSOC_PWR_WAKEEN_TSC		BIT(23)
#define SIRFSOC_PWR_WAKEEN_TS		BIT(5)
#define SIRFSOC_PWRC_TRIGGER_EN		0xc
#define SIRFSOC_PWRC_BASE		0x3000

/*
 * registers for Atlas7 to enable adc, also needed by
 * audio. which will replace by a individual driver
 */
#define SIRFSOC_ANA_BASE		0x10E30000
#define REF_CTRL0			0x64
#define REF_CTRL2			0x3c
#define TEMPSENSOR_CTRL			0x2c
#define ANA_TSADCCTL5_TSADC_OFFSET	0x14


#define OFFSET_ERR_BITS_MASK		0x3fff
#define GAIN_ERR_BITS_MASK			0xfff
#define ATLAS7_ADC_1V2_IDEAL0		1064
#define ATLAS7_ADC_1V2_IDEAL1		1975
#define ATLAS7_ADC_1V2_IDEAL2		2757
#define ATLAS7_ADC_1V2_IDEAL3		3427
#define ATLAS7_ADC_600mV_IDEAL4		2246
#define ATLAS7_ADC_600mV_IDEAL5		2638
#define ATLAS7_ADC_600mV_IDEAL6		2926
#define ATLAS7_ADC_600mV_IDEAL7		3137


#define AUDIO_ANA_REF_AUDBIAS_IREF_EN			BIT(0)
#define AUDIO_ANA_REF_AUDBIAS_VAG_RX_EN			BIT(4)
#define AUDIO_ANA_REF_AUDBIAS_VAG_TX_EN			BIT(5)
#define AUDIO_ANA_REF_AUDBIAS_VAG_TSADC_EN		BIT(6)
#define AUDIO_ANA_REF_MICBIAS_EN			BIT(7)

/* INTR register defines */
#define SIRFSOC_ADC_PEN_INTR_EN		BIT(5)
#define SIRFSOC_ADC_DATA_INTR_EN	BIT(4)
#define SIRFSOC_ADC_PEN_INTR		BIT(1)
#define SIRFSOC_ADC_DATA_INTR		BIT(0)

/* DATA register defines */
#define SIRFSOC_ADC_PEN_DOWN		BIT(31)
#define SIRFSOC_ADC_DATA_VALID		BIT(30)
#define SIRFSOC_ADC_DATA_MASK		0x3FFF

#define SIRFSOC_ADC_TS_SAMPLE_SIZE	4

enum sirfsoc_adc_chips {
	PRIMA2,
	ATLAS6,
	ATLAS7,
};

struct sirfsoc_adc_aux_register {
	u32		aux1;
	/* Atlas6 AUX2 to AUX4 reserved*/
	u32		aux2;
	u32		aux3;
	u32		aux4;
	u32		aux5;
	u32		aux6;
	/* Atlas7 add AUX7 and AUX8 */
	u32		aux7;
	u32		aux8;
	/* Read Back calibration register */
	u32		cali;
};

struct sirfsoc_adc_ts_register {
	u32		pressure;
	u32		coord1;
	u32		coord2;
	u32		coord3;
	u32		coord4;
};

struct sirfsoc_adc_mode_sel {
	u32		aux1_sel;
	/* Atlas6 AUX2 to AUX4 reserved*/
	u32		aux2_sel;
	u32		aux3_sel;
	u32		aux4_sel;
	u32		aux5_sel;
	u32		aux6_sel;
	/* Atlas7 add AUX7 and AUX8 */
	u32		aux7_sel;
	u32		aux8_sel;
	u32		temp1_sel;
	u32		temp2_sel;
	u32		single_ts_sel;
	u32		dual_ts_sel;
	u32		offset_cali_sel;
	u32		gain_cali_sel;
};

struct sirfsoc_adc_ctrl_set {
	/* ctrl1 set bits */
	u32		quant_en;
	u32		reset;
	u32		mode_shift;
	u32		mode_mask;
	u32		resolution;
	u32		sbat_en;
	u32		poll;
	u32		sgain_shift;
	u32		sgain_mask;
	u32		freq;
	u32		thold;
	/* ctrl2 set bits */
	u32		prp_mode;
	u32		rtouch;
	u32		del_pre;
	u32		del_dis;
};

struct sirfsoc_adc_register {
	u32		ctrl1;
	u32		ctrl2;
	/* Atlas7 add control3 and control4 */
	u32		ctrl3;
	u32		ctrl4;
	u32		intr_status;
	/*
	 * Atlas7 use individual registers to
	 * enable or disable intrrupts
	 */
	u32		intr_enable;
	u32		intr_disable;
	struct sirfsoc_adc_aux_register	aux_reg;
	struct sirfsoc_adc_ts_register	ts_reg;
	struct sirfsoc_adc_ctrl_set	ctrl_set;
	struct sirfsoc_adc_mode_sel	mode_sel;
};

static struct sirfsoc_adc_register prima2_adc_reg = {
	.ctrl1		= 0x00,
	.ctrl2		= 0x04,
	.intr_status	= 0x08,
	.aux_reg	= {
		.aux1		= 0x14,
		.aux2		= 0x18,
		.aux3		= 0x1C,
		.aux4		= 0x20,
		.aux5		= 0x24,
		.aux6		= 0x28,
		.cali		= 0x2C,
	},
	.ts_reg		= {
		.pressure	= 0x10,
		.coord1		= 0x0c,
		.coord2		= 0x30,
		.coord3		= 0x34,
		.coord4		= 0x38,
	},
	.ctrl_set	= {
		.quant_en	= BIT(24),
		.reset		= BIT(23),
		.mode_shift	= 11,
		.mode_mask	= 0xF,
		.resolution	= BIT(22),
		.sbat_en	= BIT(21),
		.poll		= BIT(15),
		.sgain_shift		= 16,
		.sgain_mask = 0x7,
		.freq		= BIT(8),
		.thold		= 0x4 << 4,
		.prp_mode	= 0x3 << 14,
		.rtouch		= 0x1 << 12,
		.del_pre	= 0x2 << 4,
		.del_dis	= 0x5,
	},
	.mode_sel	= {
		.aux1_sel	= 0x04,
		.aux2_sel	= 0x05,
		.aux3_sel	= 0x06,
		.aux4_sel	= 0x07,
		.aux5_sel	= 0x08,
		.aux6_sel	= 0x09,
		.single_ts_sel	= 0x0A,
		.dual_ts_sel	= 0x0F,
		.offset_cali_sel	= 0x0B,
		.gain_cali_sel	= 0x0C,
	},
};

static struct sirfsoc_adc_register atlas6_adc_reg = {
	.ctrl1		= 0x00,
	.ctrl2		= 0x04,
	.intr_status	= 0x08,
	.aux_reg	= {
		.aux1		= 0x14,
		.aux4		= 0x20,
		.aux5		= 0x24,
		.aux6		= 0x28,
		.cali		= 0x2C,
	},
	.ts_reg		= {
		.pressure	= 0x10,
		.coord1		= 0x0c,
		.coord2		= 0x30,
		.coord3		= 0x34,
		.coord4		= 0x38,
	},
	.ctrl_set	= {
		.quant_en	= BIT(24),
		.reset		= BIT(23),
		.mode_shift	= 11,
		.mode_mask	= 0xF,
		.resolution	= BIT(22),
		.sbat_en	= BIT(21),
		.poll		= BIT(15),
		.sgain_shift		= 16,
		.sgain_mask = 0x7,
		.freq		= BIT(8),
		.thold		= 0x4 << 4,
		.prp_mode	= 0x3 << 14,
		.rtouch		= 0x1 << 12,
		.del_pre	= 0x2 << 4,
		.del_dis	= 0x5,
	},
	.mode_sel	= {
		.aux1_sel	= 0x04,
		.aux5_sel	= 0x08,
		.aux6_sel	= 0x09,
		.single_ts_sel	= 0x0A,
		.dual_ts_sel	= 0x0F,
		.offset_cali_sel	= 0x0B,
		.gain_cali_sel	= 0x0C,
	},
};

static struct sirfsoc_adc_register atlas7_adc_reg = {
	.ctrl1		= 0x00,
	.ctrl2		= 0x04,
	.ctrl3		= 0x08,
	.ctrl4		= 0x0c,
	.intr_status	= 0x10,
	.intr_enable	= 0x54,
	.intr_disable	= 0x58,
	.aux_reg	= {
		.aux1		= 0x1c,
		.aux2		= 0x20,
		.aux3		= 0x24,
		.aux4		= 0x28,
		.aux5		= 0x2c,
		.aux6		= 0x30,
		.aux7		= 0x34,
		.aux8		= 0x38,
		.cali		= 0x3c,
	},
	.ts_reg		= {
		.pressure	= 0x18,
		.coord1		= 0x14,
		.coord2		= 0x40,
		.coord3		= 0x44,
		.coord4		= 0x48,
	},
	.ctrl_set	= {
		.quant_en	= BIT(24),
		.reset		= BIT(23),
		.mode_shift	= 10,
		.mode_mask	= 0x1F,
		.resolution	= BIT(22),
		.sbat_en	= BIT(21),
		.poll		= BIT(15),
		.sgain_shift		= 16,
		.sgain_mask = 0x7,
		.freq		= BIT(8),
		.thold		= 0x4 << 4,
		.prp_mode	= 0x3 << 14,
		.rtouch		= 0x1 << 12,
		.del_pre	= 0x2 << 4,
		.del_dis	= 0x5,
	},
	.mode_sel	= {
		.aux1_sel	= 0x04,
		.aux2_sel	= 0x05,
		.aux3_sel	= 0x06,
		.aux4_sel	= 0x07,
		.aux5_sel	= 0x08,
		.aux6_sel	= 0x09,
		.aux7_sel	= 0x0A,
		.aux8_sel	= 0x0B,
		.temp1_sel	= 0x12,
		.temp2_sel	= 0x13,
		.single_ts_sel	= 0x0C,
		.dual_ts_sel	= 0x11,
		.offset_cali_sel	= 0x0D,
		.gain_cali_sel	= 0x0E,
	},
};

struct sirfsoc_adc_chip_info {
	struct sirfsoc_adc_register *adc_reg;
	const struct iio_chan_spec *channels;
	unsigned int num_channels;
	unsigned int flags;

	const struct iio_info *iio_info;
	u32 (*calculate_volt)(u32, u32, u32);
	const u32 *channel_sel;
	const u32 *sgain_sel;
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
	struct clk			*clk;
	/* atlas7 need enable extra 2 clock to enable adc */
	struct clk			*clk_io;
	struct clk			*clk_analog;
	struct clk			*clk_ds;
	struct mutex			mutex;
	void __iomem			*base;
	/*
	 * FIXME: atlas7 need ctrl analog for enable adc
	 * which will be removed when have independent driver
	 * to do this.
	 */
	void __iomem			*ana_base;
	struct sirfsoc_adc_request	req;
	struct completion		done;
	unsigned int			offset_cali0;
	unsigned int			offset_cali2;
	unsigned int			gain_cali2_ch1_2;
	unsigned int			gain_cali3_ch1_2;
	unsigned int			gain_cali4_ch1_2;
	unsigned int			gain_cali5_ch1_2;
	unsigned int			gain_cali6_ch1_2;
	unsigned int			gain_cali7_ch1_2;
	unsigned int			gain_cali0_ch3_4_5_6;
	unsigned int			gain_cali1_ch3_4_5_6;
	unsigned int			gain_cali2_ch3_4_5_6;
	unsigned int			gain_cali3_ch3_4_5_6;
	unsigned int			gain_cali4_ch3_4_5_6;
	unsigned int			gain_cali5_ch3_4_5_6;
	unsigned int			gain_cali6_ch3_4_5_6;
	unsigned int			gain_cali7_ch3_4_5_6;
};

/* Dual touch samples read registers*/
static u32 sirfsoc_adc_ts_reg[SIRFSOC_ADC_TS_SAMPLE_SIZE];

enum sirfsoc_adc_ts_type {
	SINGLE_TOUCH,
	DUAL_TOUCH,
};

static int sirfsoc_adc_get_ts_sample(
	struct sirfsoc_adc *adc, int *sample, int touch_type)
{
	struct sirfsoc_adc_register *adc_reg = adc->chip_info->adc_reg;
	struct sirfsoc_adc_ctrl_set *ctrl_set = &adc_reg->ctrl_set;
	struct sirfsoc_adc_mode_sel *mode_sel = &adc_reg->mode_sel;
	int control1;
	int adc_intr;
	int ret = 0;
	int i;

	/*
	 * Atlas7 and M3 may share the ADC concurrently,
	 * and now use the mode bits in control1 register
	 * to insulate operations from the two core. If the
	 * mode bits are nonzero, the adc is occupied by
	 * the other core, and the driver can't operate
	 * the adc now. If zero, the adc could be use now,
	 * and driver can operate the adc to response the
	 * request. After using, the mode bit must be set
	 * to zero for using by the other core.
	 */
	control1 = readl(adc->base + adc_reg->ctrl1);
	if (control1 & (ctrl_set->mode_mask << ctrl_set->mode_shift))
		return -EBUSY;

	adc_intr = readl(adc->base + adc_reg->intr_status);
	if (adc_intr & SIRFSOC_ADC_PEN_INTR)
		writel(adc_intr | SIRFSOC_ADC_PEN_INTR,
			adc->base + adc_reg->intr_status);

	/* check pen status */
	if (!(readl(adc->base + adc_reg->ts_reg.coord1) &
		SIRFSOC_ADC_PEN_DOWN)) {
		ret = -EINVAL;
		goto out;
	}

	if (SINGLE_TOUCH == touch_type)
		writel(((mode_sel->single_ts_sel & ctrl_set->mode_mask) <<
			ctrl_set->mode_shift) | ctrl_set->poll |
			ctrl_set->quant_en | ctrl_set->reset |
			ctrl_set->thold, adc->base + adc_reg->ctrl1);
	else
		writel(((mode_sel->dual_ts_sel & ctrl_set->mode_mask) <<
			ctrl_set->mode_shift) | ctrl_set->poll |
			ctrl_set->quant_en | ctrl_set->reset |
			ctrl_set->thold, adc->base + adc_reg->ctrl1);

	if (!wait_for_completion_timeout(&adc->done, msecs_to_jiffies(50))) {
		ret = -EIO;
		goto out;
	}

	if (SINGLE_TOUCH == touch_type)
		*sample = readl(adc->base + adc_reg->ts_reg.coord1);
	else
		for (i = 0; i < SIRFSOC_ADC_TS_SAMPLE_SIZE; i++)
			sample[i] = readl(adc->base + sirfsoc_adc_ts_reg[i]);

out:
	writel(control1, adc->base + adc_reg->ctrl1);

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
	struct sirfsoc_adc_register *adc_reg = adc->chip_info->adc_reg;
	struct sirfsoc_adc_ctrl_set *ctrl_set = &adc_reg->ctrl_set;
	struct sirfsoc_adc_mode_sel *mode_sel = &adc_reg->mode_sel;
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	struct device_node *np = indio_dev->dev.parent->of_node;
	int control1;
	int data, reg_offset;
	u32 sel_bits;
	int ret = 0;


	/*
	 * Atlas7 and M3 may share the ADC concurrently,
	 * and now use the mode bits in control1 register
	 * to insulate operations from the two core. If the
	 * mode bits are nonzero, the adc is occupied by
	 * the other core, and the driver can't operate
	 * the adc now. If zero, the adc could be use now,
	 * and driver can operate the adc to response the
	 * request. After using, the mode bit must be set
	 * to zero for using by the other core.
	 */
	control1 = readl(adc->base + adc_reg->ctrl1);
	if (control1 & (ctrl_set->mode_mask << ctrl_set->mode_shift))
		return -EBUSY;

	if (of_device_is_compatible(np, "sirf,atlas7-adc")) {
		/* some channels have sgain set to 2, and some have set to 0.
		 * they need to be applied with different cali data.
		 */
		if (((req->s_gain_bits >> ctrl_set->sgain_shift) &
					ctrl_set->sgain_mask) == 2)
			writel(adc->offset_cali2, adc->ana_base +
						 ANA_TSADCCTL5_TSADC_OFFSET);
		else
			writel(adc->offset_cali0, adc->ana_base +
						 ANA_TSADCCTL5_TSADC_OFFSET);
		writel(SIRFSOC_ADC_DATA_INTR, adc->base + adc_reg->intr_status);
		writel(ctrl_set->poll | req->mode | req->delay_bits |
			req->s_gain_bits | ctrl_set->quant_en |
			ctrl_set->reset | ctrl_set->resolution,
			adc->base + adc_reg->ctrl1);
	} else {
		writel(SIRFSOC_ADC_DATA_INTR_EN | SIRFSOC_ADC_DATA_INTR,
			adc->base + adc_reg->intr_status);
		writel(ctrl_set->poll | req->mode | req->extcm |
			req->delay_bits | ctrl_set->quant_en |
			ctrl_set->reset | ctrl_set->resolution,
			adc->base + adc_reg->ctrl1);
	}

	if (!wait_for_completion_timeout(&adc->done, msecs_to_jiffies(50))) {
		ret = -EIO;
		goto out;
	}

	sel_bits = (req->mode >> ctrl_set->mode_shift) & ctrl_set->mode_mask;
	if ((sel_bits >= mode_sel->aux1_sel &&
		sel_bits <= mode_sel->aux6_sel) ||
		sel_bits == mode_sel->aux7_sel ||
		sel_bits == mode_sel->aux8_sel) {
		/* Calculate aux offset from mode, which is aux in aux_reg */
		if (of_device_is_compatible(np, "sirf,atlas7-adc"))
			reg_offset = 0x1C + (sel_bits - 0x04) * 4;
		else
			reg_offset = 0x14 + (sel_bits - 0x04) * 4;
		data = readl(adc->base + reg_offset);
		if (data & SIRFSOC_ADC_DATA_VALID)
			req->read_back_data = data & SIRFSOC_ADC_DATA_MASK;
	} else if (sel_bits == mode_sel->temp1_sel ||
			sel_bits == mode_sel->temp2_sel) {
		reg_offset = 0x14 + (sel_bits - 0x04) * 4;
		data = readl(adc->base + reg_offset);
		if (data & SIRFSOC_ADC_DATA_VALID)
			req->read_back_data = data & SIRFSOC_ADC_DATA_MASK;
	} else if (sel_bits == mode_sel->offset_cali_sel ||
			sel_bits == mode_sel->gain_cali_sel) {
		data = readl(adc->base + adc_reg->aux_reg.cali);
		if (data & SIRFSOC_ADC_DATA_VALID)
			req->read_back_data = data & SIRFSOC_ADC_DATA_MASK;
	}

out:
	writel(control1, adc->base + adc_reg->ctrl1);
	return ret;
}

/* Store params to calibrate */
struct sirfsoc_adc_cali_data {
	u32 digital_offset;
	u32 digital_again;
	u32 sgain;
	bool is_calibration;
};

/* Offset Calibration calibrates the ADC offset error */
static u32 sirfsoc_adc_offset_cali(struct sirfsoc_adc_request *req)
{
	struct sirfsoc_adc *adc = container_of(req, struct sirfsoc_adc, req);
	struct sirfsoc_adc_register *adc_reg = adc->chip_info->adc_reg;
	struct sirfsoc_adc_ctrl_set *ctrl_set = &adc_reg->ctrl_set;
	struct sirfsoc_adc_mode_sel *mode_sel = &adc_reg->mode_sel;
	u32 i, digital_offset = 0, count = 0, sum = 0;

	/* To set the registers in order to get the ADC offset */
	req->mode = (mode_sel->offset_cali_sel &
		ctrl_set->mode_mask) << ctrl_set->mode_shift;
	req->delay_bits = ctrl_set->thold;

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
	struct sirfsoc_adc *adc = container_of(req, struct sirfsoc_adc, req);
	struct sirfsoc_adc_register *adc_reg = adc->chip_info->adc_reg;
	struct sirfsoc_adc_ctrl_set *ctrl_set = &adc_reg->ctrl_set;
	struct sirfsoc_adc_mode_sel *mode_sel = &adc_reg->mode_sel;
	u32 i, digital_gain = 0, count = 0, sum = 0;

	/* To set the registers in order to get the ADC gain */
	req->mode = (mode_sel->gain_cali_sel &
		ctrl_set->mode_mask) << ctrl_set->mode_shift;
	req->delay_bits = ctrl_set->thold;

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
/*Get the gain error for atlas7*/
static int atlas7_adc_get_gain_cali(struct sirfsoc_adc_request *req,
				struct sirfsoc_adc *adc)
{
	u32 sgain, gain_err;
	u16 mode;

	sgain = (req->s_gain_bits >> 16) & 0x7;
	mode = (req->mode >> 10) & 0x1F;
	gain_err = -EINVAL;
	/*
	*channel 1 and 2 only support s_gain value 2,3,4,5,6 and 7.
	*Meanwhile the gain error values may vary from the ones of
	*channel 3,4,5 and 6
	*/
	if ((mode == 0x04) || (mode == 0x05)) {
		switch (sgain) {
		case 2:
			gain_err = adc->gain_cali2_ch1_2 ?
				adc->gain_cali2_ch1_2 : ATLAS7_ADC_1V2_IDEAL2;
			break;
		case 3:
			gain_err = adc->gain_cali3_ch1_2 ?
				adc->gain_cali3_ch1_2 : ATLAS7_ADC_1V2_IDEAL3;
			break;
		case 4:
			gain_err = adc->gain_cali4_ch1_2 ?
				adc->gain_cali4_ch1_2 : ATLAS7_ADC_600mV_IDEAL4;
			break;
		case 5:
			gain_err = adc->gain_cali5_ch1_2 ?
				adc->gain_cali5_ch1_2 : ATLAS7_ADC_600mV_IDEAL5;
			break;
		case 6:
			gain_err = adc->gain_cali6_ch1_2 ?
				adc->gain_cali6_ch1_2 : ATLAS7_ADC_600mV_IDEAL6;
			break;
		case 7:
			gain_err = adc->gain_cali7_ch1_2 ?
				adc->gain_cali7_ch1_2 : ATLAS7_ADC_600mV_IDEAL7;
			break;
		default:
			return -EINVAL;
		}
	} else if ((mode > 0x05) && (mode < 0x0A)) {
		/*
		* gain error values of different s_gain for channel 3, 4,
		*5 and 6
		*/
		switch (sgain) {
		case 0:
			gain_err = adc->gain_cali0_ch3_4_5_6 ?
				adc->gain_cali0_ch3_4_5_6 :
				ATLAS7_ADC_1V2_IDEAL0;
			break;
		case 1:
			gain_err = adc->gain_cali1_ch3_4_5_6 ?
				adc->gain_cali1_ch3_4_5_6 :
				ATLAS7_ADC_1V2_IDEAL1;
			break;
		case 2:
			gain_err = adc->gain_cali2_ch3_4_5_6 ?
				adc->gain_cali2_ch3_4_5_6 :
				ATLAS7_ADC_1V2_IDEAL2;
			break;
		case 3:
			gain_err = adc->gain_cali3_ch3_4_5_6 ?
				adc->gain_cali3_ch3_4_5_6 :
				ATLAS7_ADC_1V2_IDEAL3;
			break;
		case 4:
			gain_err = adc->gain_cali4_ch3_4_5_6 ?
				adc->gain_cali4_ch3_4_5_6 :
				ATLAS7_ADC_600mV_IDEAL4;
			break;
		case 5:
			gain_err = adc->gain_cali5_ch3_4_5_6 ?
				adc->gain_cali5_ch3_4_5_6 :
				ATLAS7_ADC_600mV_IDEAL5;
			break;
		case 6:
			gain_err = adc->gain_cali6_ch3_4_5_6 ?
				adc->gain_cali6_ch3_4_5_6 :
				ATLAS7_ADC_600mV_IDEAL6;
			break;
		case 7:
			gain_err = adc->gain_cali7_ch3_4_5_6 ?
				adc->gain_cali7_ch3_4_5_6 :
				ATLAS7_ADC_600mV_IDEAL7;
			break;
		default:
			return -EINVAL;
		}
	}
	return gain_err;
}
/* Absolute gain calibration */
static int sirfsoc_adc_adc_cali(struct sirfsoc_adc_request *req,
				struct sirfsoc_adc_cali_data *cali_data)
{
	struct sirfsoc_adc *adc = container_of(req, struct sirfsoc_adc, req);
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	struct device_node *np = indio_dev->dev.parent->of_node;

	if (of_device_is_compatible(np, "sirf,atlas7-adc")) {
		cali_data->digital_offset = 0;
		cali_data->digital_again = atlas7_adc_get_gain_cali(req,
				adc);
	} else {
		cali_data->digital_offset = sirfsoc_adc_offset_cali(req);
		if (!cali_data->digital_offset)
			return -EINVAL;
		cali_data->digital_again = sirfsoc_adc_gain_cali(req);
		if (!cali_data->digital_again)
			return -EINVAL;
	}

	return 0;
}

/* Get voltage after ADC conversion */
static u32 sirfsoc_adc_get_adc_volt(struct sirfsoc_adc *adc,
				struct sirfsoc_adc_cali_data *cali_data)
{
	struct sirfsoc_adc_request *req = &adc->req;
	struct sirfsoc_adc_register *adc_reg = adc->chip_info->adc_reg;
	struct sirfsoc_adc_ctrl_set *ctrl_set = &adc_reg->ctrl_set;
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	struct device_node *np = indio_dev->dev.parent->of_node;
	u32 digital_out, volt;
	u16 mode;

	req->delay_bits = ctrl_set->thold;

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

		if (of_device_is_compatible(np, "sirf,atlas7-adc")) {
			mode = (req->mode >> 10) & 0x1F;
			if ((mode == 0x12) || (mode == 0x13)) {
				volt = digital_out;
			} else {
			volt = adc->chip_info->calculate_volt(digital_out,
				cali_data->sgain,
				cali_data->digital_again);
			}
		} else {
			volt = adc->chip_info->calculate_volt(digital_out,
				cali_data->digital_offset,
				cali_data->digital_again);
		}
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

/* FIXME: the formula to calculate voltage will be update */
static u32 atlas7_adc_calculate_volt(u32 digital_out,
				u32 sgain, u32 digital_again)
{
#if 0
	pr_info("cal volt: out: %x  sgain: %x gain: %x\n",
			 digital_out, sgain, digital_again);
#endif
	/* Vin=(Codeout * 0.001128/Gain), where Gain is 1 for SGAIN[2..0]=0
	 * Gain is 2.59 for SGAIN[2..0]=2 */

	switch (sgain) {
	case 0:
		digital_out = digital_out * 141 * ATLAS7_ADC_1V2_IDEAL0;
		digital_out = digital_out / (125 * digital_again);
		break;
	case 1:
		digital_out = digital_out * 141 * ATLAS7_ADC_1V2_IDEAL1;
		digital_out = digital_out / (125 * digital_again);
		digital_out = digital_out * 100 / 190;
		break;
	case 2:
		digital_out = digital_out * 141 * ATLAS7_ADC_1V2_IDEAL2;
		digital_out = digital_out / (125 * digital_again);
		digital_out = digital_out * 100 / 259;
		break;
	case 3:
		digital_out = digital_out * 141 * ATLAS7_ADC_1V2_IDEAL3;
		digital_out = digital_out / (125 * digital_again);
		digital_out = digital_out * 100 / 320;
		break;
	case 4:
		digital_out = digital_out * 141 * ATLAS7_ADC_600mV_IDEAL4;
		digital_out = digital_out / (125 * digital_again);
		digital_out = digital_out * 100 / 420;
		break;
	case 5:
		digital_out = digital_out * 141 * ATLAS7_ADC_600mV_IDEAL5;
		digital_out = digital_out / (125 * digital_again);
		digital_out = digital_out * 100 / 500;
		break;
	case 6:
		digital_out = digital_out * 141 * ATLAS7_ADC_600mV_IDEAL6;
		digital_out = digital_out / (125 * digital_again);
		digital_out = digital_out * 100 / 550;
		break;
	case 7:
		digital_out = digital_out * 141 * ATLAS7_ADC_600mV_IDEAL7;
		digital_out = digital_out / (125 * digital_again);
		digital_out = digital_out * 100 / 590;
		break;

	default:
		digital_out = digital_out * 141 * ATLAS7_ADC_1V2_IDEAL0;
		digital_out = digital_out / (125 * digital_again);
		break;
	}

	return digital_out;
}

static irqreturn_t sirfsoc_adc_data_irq(int irq, void *handle)
{
	struct iio_dev *indio_dev = handle;
	struct sirfsoc_adc *adc = iio_priv(indio_dev);
	struct sirfsoc_adc_register *adc_reg = adc->chip_info->adc_reg;
	struct device_node *np = indio_dev->dev.parent->of_node;
	int val;

	val = readl(adc->base + adc->chip_info->adc_reg->intr_status);

	if (of_device_is_compatible(np, "sirf,atlas7-adc")) {
		writel(SIRFSOC_ADC_PEN_INTR | SIRFSOC_ADC_DATA_INTR,
			adc->base + adc_reg->intr_status);
	} else {
		writel(SIRFSOC_ADC_PEN_INTR | SIRFSOC_ADC_PEN_INTR_EN |
			SIRFSOC_ADC_DATA_INTR | SIRFSOC_ADC_DATA_INTR_EN,
			adc->base + adc_reg->intr_status);
	}

	if (val & SIRFSOC_ADC_DATA_INTR)
		complete(&adc->done);

	return IRQ_HANDLED;
}

/*
 * FIXME: For use adc on atlas7, analog need enable before.
 * audio codec also operate it and may disable analog
 * so check if it's enabled when get data from adc
 * channel, if not, enable the analog to make adc work.
 * which will be removed when have independent driver
 * to manage this.
 */
static void sirfsoc_adc_enable_analog(struct sirfsoc_adc *adc)
{
	u32 read_data;

	read_data =  readl(adc->ana_base + REF_CTRL0);
	/* if analog be enabled before */
	if ((read_data & 0xf1) == 0xf1)
		return;

	/*
	 * some register bits can't update on most atals7 board
	 * when enable analog. the follow operations are to
	 * workaround the bug.
	 */
	mutex_lock(&adc->mutex);
	read_data =  readl(adc->ana_base + 0x58);
	writel(read_data | (0x2 << 5) , adc->ana_base + 0x58);
	read_data =  readl(adc->ana_base + 0x50);
	writel(read_data | 0x1 , adc->ana_base + 0x50);
	read_data =  readl(adc->ana_base + 0x50);
	writel(read_data & (~0x1) , adc->ana_base + 0x50);
	read_data =  readl(adc->ana_base + 0x58);
	writel(read_data & (~(0x3 << 5)) , adc->ana_base + 0x58);

	read_data =  readl(adc->ana_base + REF_CTRL0);
	writel(read_data | AUDIO_ANA_REF_MICBIAS_EN |
		AUDIO_ANA_REF_AUDBIAS_VAG_TSADC_EN |
		AUDIO_ANA_REF_AUDBIAS_VAG_TX_EN |
		AUDIO_ANA_REF_AUDBIAS_VAG_RX_EN |
		AUDIO_ANA_REF_AUDBIAS_IREF_EN,
		adc->ana_base + REF_CTRL0);

	read_data =  readl(adc->ana_base + REF_CTRL0);
	writel(0x84, adc->ana_base + REF_CTRL2);

#define TMPS1_OUT_EN	BIT(0)
#define TMPS1_EN		BIT(1)
#define TMPS2_OUT_EN	BIT(4)
#define TMPS2_EN		BIT(5)

	writel(TMPS1_OUT_EN | TMPS1_EN | TMPS2_OUT_EN |
			TMPS2_OUT_EN,
		adc->ana_base + TEMPSENSOR_CTRL);
	mutex_unlock(&adc->mutex);
}

static int sirfsoc_adc_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val,
				int *val2,
				long mask)
{
	struct sirfsoc_adc *adc = iio_priv(indio_dev);
	struct sirfsoc_adc_register *adc_reg = adc->chip_info->adc_reg;
	struct sirfsoc_adc_ctrl_set *ctrl_set = &adc_reg->ctrl_set;
	struct device_node *np = indio_dev->dev.parent->of_node;
	struct sirfsoc_adc_cali_data cali_data;
	u32 sgain = 0;
	u32 msel;
	int ret;

	/* check if analog enabled, if not enable it */
	if (of_device_is_compatible(np, "sirf,atlas7-adc"))
		sirfsoc_adc_enable_analog(adc);

	cali_data.is_calibration = false;
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&adc->mutex);
		if (0 == chan->channel)
			ret = sirfsoc_adc_single_ts_sample(adc, val);
		else
			ret = sirfsoc_adc_dual_ts_sample(adc, val);
		mutex_unlock(&adc->mutex);
		if (ret)
			return ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_PROCESSED:
		mutex_lock(&adc->mutex);
		msel = adc->chip_info->channel_sel[chan->channel];
		if (of_device_is_compatible(np, "sirf,atlas7-adc"))
			sgain = adc->chip_info->sgain_sel[chan->channel];
		adc->req.mode = (msel & ctrl_set->mode_mask) <<
				ctrl_set->mode_shift;
		adc->req.s_gain_bits = (sgain & ctrl_set->sgain_mask)<<
				ctrl_set->sgain_shift;
		cali_data.sgain = sgain;
		*val = sirfsoc_adc_get_adc_volt(adc, &cali_data);
		mutex_unlock(&adc->mutex);
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
	struct device_node *np = dev->of_node;

	if (!of_device_is_compatible(np, "sirf,atlas7-adc")) {
		sirfsoc_rtc_iobrg_writel(sirfsoc_rtc_iobrg_readl(
			SIRFSOC_PWRC_BASE + SIRFSOC_PWRC_TRIGGER_EN)
			& ~SIRFSOC_PWR_WAKEEN_TSC,
			SIRFSOC_PWRC_BASE + SIRFSOC_PWRC_TRIGGER_EN);
		clk_disable_unprepare(adc->clk_analog);
		clk_disable_unprepare(adc->clk_io);
	}

	clk_disable_unprepare(adc->clk);

	return 0;
}

static int sirfsoc_adc_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct sirfsoc_adc *adc = iio_priv(indio_dev);
	struct sirfsoc_adc_register *adc_reg = adc->chip_info->adc_reg;
	struct sirfsoc_adc_ctrl_set *ctrl_set = &adc_reg->ctrl_set;
	struct device_node *np = dev->of_node;
	int ret;

	clk_prepare_enable(adc->clk);

	if (!of_device_is_compatible(np, "sirf,atlas7-adc")) {
		sirfsoc_rtc_iobrg_writel(sirfsoc_rtc_iobrg_readl(
			SIRFSOC_PWRC_BASE + SIRFSOC_PWRC_TRIGGER_EN) |
			SIRFSOC_PWR_WAKEEN_TS,
			SIRFSOC_PWRC_BASE + SIRFSOC_PWRC_TRIGGER_EN);

		ret = device_reset(dev);
		if (ret) {
			dev_err(dev, "Failed to reset\n");
			return ret;
		}
	} else {
		clk_prepare_enable(adc->clk_io);
		clk_prepare_enable(adc->clk_analog);
	}

	writel(ctrl_set->prp_mode | ctrl_set->rtouch | ctrl_set->del_pre |
		ctrl_set->del_dis, adc->base + adc_reg->ctrl2);

	/* Clear interrupts and enable PEN interrupt */
	if (of_device_is_compatible(np, "sirf,atlas7-adc")) {
		writel(SIRFSOC_ADC_PEN_INTR | SIRFSOC_ADC_DATA_INTR,
			adc->base + adc_reg->intr_status);
		writel(SIRFSOC_ADC_PEN_INTR | SIRFSOC_ADC_DATA_INTR,
			adc->base + adc_reg->intr_enable);
	} else {
		writel(SIRFSOC_ADC_PEN_INTR | SIRFSOC_ADC_PEN_INTR_EN |
			SIRFSOC_ADC_DATA_INTR | SIRFSOC_ADC_DATA_INTR_EN,
			adc->base + adc_reg->intr_status);
	}

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

#define SIRFSOC_ADC_TEMP_CHANNEL(_channel, _name)	\
	SIRFSOC_ADC_CHANNEL(_channel, IIO_TEMP,	\
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

static u32 prima2_adc_channel_sel[] = {
	0,
	0x04, /* aux1 */
	0x05, /* aux2 */
	0x06, /* aux3 */
	0x07, /* aux4 */
	0x08, /* aux5 */
	0x09 /* aux6 */
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

static u32 atlas6_adc_channel_sel[] = {
	0, 0,
	0x04, /* aux1 */
	0x07, /* aux4 */
	0x08, /* aux5 */
	0x09 /* aux6 */
};

static const struct iio_chan_spec atlas7_adc_iio_channels[] = {
	/* Channels to get the touch data */
	SIRFSOC_ADC_TS_CHANNEL(0, "touch_coord"),
	SIRFSOC_ADC_TS_CHANNEL(1, "dual_touch_coord"),
	/* Channels to get the pin input */
	SIRFSOC_ADC_AUX_CHANNEL(2, "auxiliary1"),
	SIRFSOC_ADC_AUX_CHANNEL(3, "auxiliary2"),
	SIRFSOC_ADC_AUX_CHANNEL(4, "auxiliary3"),
	SIRFSOC_ADC_AUX_CHANNEL(5, "auxiliary4"),
	SIRFSOC_ADC_AUX_CHANNEL(6, "auxiliary5"),
	SIRFSOC_ADC_AUX_CHANNEL(7, "auxiliary6"),
	SIRFSOC_ADC_AUX_CHANNEL(8, "auxiliary7"),
	SIRFSOC_ADC_AUX_CHANNEL(9, "auxiliary8"),
	SIRFSOC_ADC_TEMP_CHANNEL(10, "temp1"),
	SIRFSOC_ADC_TEMP_CHANNEL(11, "temp2"),
};

static u32 atlas7_adc_channel_sel[] = {
	0, 0,
	0x04, /* aux1 */
	0x05, /* aux2 */
	0x06, /* aux3 */
	0x07, /* aux4 */
	0x08, /* aux5 */
	0x09, /* aux6 */
	0x0A, /* aux7 */
	0x0B, /* aux8 */
	0x12, /* temp1 */
	0x13  /* temp2 */
};

static u32 atlas7_adc_sgain_sel[] = {
	0, 0,
	2, /* aux1 */
	2, /* aux2 */
	0, /* aux3 */
	0, /* aux4 */
	0, /* aux5 */
	0, /* aux6 */
	0, /* aux7 */
	0, /* aux8 */
	0, /* temp1 */
	0  /* temp2 */
};

static const struct iio_info sirfsoc_adc_info = {
	.read_raw = &sirfsoc_adc_read_raw,
	.driver_module = THIS_MODULE,
};

static const struct sirfsoc_adc_chip_info sirfsoc_adc_chip_info_tbl[] = {
	[PRIMA2] = {
		.adc_reg	= &prima2_adc_reg,
		.channels	= prima2_adc_iio_channels,
		.num_channels	= ARRAY_SIZE(prima2_adc_iio_channels),
		.iio_info	= &sirfsoc_adc_info,
		.calculate_volt	= prima2_adc_calculate_volt,
		.channel_sel	= prima2_adc_channel_sel,
	},
	[ATLAS6] = {
		.adc_reg	= &atlas6_adc_reg,
		.channels	= atlas6_adc_iio_channels,
		.num_channels	= ARRAY_SIZE(atlas6_adc_iio_channels),
		.iio_info	= &sirfsoc_adc_info,
		.calculate_volt	= atlas6_adc_calculate_volt,
		.channel_sel	= atlas6_adc_channel_sel,
	},
	[ATLAS7] = {
		.adc_reg	= &atlas7_adc_reg,
		.channels	= atlas7_adc_iio_channels,
		.num_channels	= ARRAY_SIZE(atlas7_adc_iio_channels),
		.iio_info	= &sirfsoc_adc_info,
		.calculate_volt	= atlas7_adc_calculate_volt,
		.channel_sel	= atlas7_adc_channel_sel,
		.sgain_sel = atlas7_adc_sgain_sel,
	},
};

static const struct of_device_id sirfsoc_adc_of_match[] = {
	{ .compatible = "sirf,prima2-adc",
	  .data = &sirfsoc_adc_chip_info_tbl[PRIMA2] },
	{ .compatible = "sirf,atlas6-adc",
	  .data = &sirfsoc_adc_chip_info_tbl[ATLAS6] },
	{ .compatible = "sirf,atlas7-adc",
	  .data = &sirfsoc_adc_chip_info_tbl[ATLAS7] },
	{}
};
MODULE_DEVICE_TABLE(of, sirfsoc_adc_of_match);

static int sirfsoc_adc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct sirfsoc_adc_register *adc_reg;
	struct sirfsoc_adc_ctrl_set *ctrl_set;
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
	match = of_match_device(of_match_ptr(sirfsoc_adc_of_match), &pdev->dev);
	if (WARN_ON(!match))
		return -ENODEV;
	adc->chip_info = match->data;
	adc_reg = adc->chip_info->adc_reg;
	ctrl_set = &adc_reg->ctrl_set;

	indio_dev->info = adc->chip_info->iio_info;
	indio_dev->channels = adc->chip_info->channels;
	indio_dev->num_channels = adc->chip_info->num_channels;
	indio_dev->name = "sirfsoc adc";
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->modes = INDIO_DIRECT_MODE;

	platform_set_drvdata(pdev, indio_dev);

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	adc->base = devm_ioremap_resource(&pdev->dev, mem_res);
	if (!adc->base) {
		dev_err(&pdev->dev, "IO remap failed!\n");
		return -ENOMEM;
	}

	init_completion(&adc->done);
	/* some register need set on atlas7 */
	if (of_device_is_compatible(np, "sirf,atlas7-adc")) {
		struct regulator *da_regulator;
		struct regulator *ad_regulator;

		adc->clk = devm_clk_get(&pdev->dev, "xin");
		if (IS_ERR(adc->clk)) {
			dev_err(&pdev->dev, "Get adc clk failed\n");
			return -ENOMEM;
		}

		adc->clk_io = devm_clk_get(&pdev->dev, "io");
		if (IS_ERR(adc->clk_io)) {
			dev_err(&pdev->dev, "Get adc io clk failed\n");
			return -ENOMEM;
		}

		adc->clk_analog = devm_clk_get(&pdev->dev, "analog");
		if (IS_ERR(adc->clk_analog)) {
			dev_err(&pdev->dev, "Get adc analog clk failed\n");
			return -ENOMEM;
		}

		da_regulator = devm_regulator_get(&pdev->dev, "ldo0");
		if (IS_ERR(da_regulator)) {
			dev_err(&pdev->dev, "Failed to obtain da_regulator\n");
			return PTR_ERR(da_regulator);
		}

		ad_regulator = devm_regulator_get(&pdev->dev, "ldo1");
		if (IS_ERR(ad_regulator)) {
			dev_err(&pdev->dev, "Failed to obtain ad_regulator\n");
			return PTR_ERR(ad_regulator);
		}

		clk_prepare_enable(adc->clk);
		clk_prepare_enable(adc->clk_io);

		ret = regulator_enable(da_regulator);
		if (ret) {
			dev_err(&pdev->dev,
				"da_regulator enable failed: %d\n", ret);
			clk_disable_unprepare(adc->clk_io);
			clk_disable_unprepare(adc->clk);
			return ret;
		}

		ret = regulator_enable(ad_regulator);
		if (ret) {
			dev_err(&pdev->dev,
				"ad_regulator enable failed: %d\n", ret);
			clk_disable_unprepare(adc->clk_io);
			clk_disable_unprepare(adc->clk);
			return ret;
		}

		mutex_init(&adc->mutex);
		clk_prepare_enable(adc->clk_analog);

		adc->ana_base = ioremap(SIRFSOC_ANA_BASE, SZ_64K);
		sirfsoc_adc_enable_analog(adc);

		ret = of_property_read_u32(np, "cali-gain2-ch1-2",
				&adc->gain_cali2_ch1_2);
		if (ret)
			adc->gain_cali2_ch1_2 = 0;
		ret = of_property_read_u32(np, "cali-gain3-ch1-2",
				&adc->gain_cali3_ch1_2);
		if (ret)
			adc->gain_cali3_ch1_2 = 0;
		ret = of_property_read_u32(np, "cali-gain4-ch1-2",
				&adc->gain_cali4_ch1_2);
		if (ret)
			adc->gain_cali4_ch1_2 = 0;
		ret = of_property_read_u32(np, "cali-gain5-ch1-2",
				&adc->gain_cali5_ch1_2);
		if (ret)
			adc->gain_cali5_ch1_2 = 0;
		ret = of_property_read_u32(np, "cali-gain6-ch1-2",
				&adc->gain_cali6_ch1_2);
		if (ret)
			adc->gain_cali6_ch1_2 = 0;
		ret = of_property_read_u32(np, "cali-gain7-ch1-2",
				&adc->gain_cali7_ch1_2);
		if (ret)
			adc->gain_cali7_ch1_2 = 0;

		ret = of_property_read_u32(np, "cali-gain0-ch3-4-5-6",
				&adc->gain_cali0_ch3_4_5_6);
		if (ret)
			adc->gain_cali0_ch3_4_5_6 = 0;
		ret = of_property_read_u32(np, "cali-gain1-ch3-4-5-6",
				&adc->gain_cali1_ch3_4_5_6);
		if (ret)
			adc->gain_cali1_ch3_4_5_6 = 0;
		ret = of_property_read_u32(np, "cali-gain2-ch3-4-5-6",
				&adc->gain_cali2_ch3_4_5_6);
		if (ret)
			adc->gain_cali2_ch3_4_5_6 = 0;
		ret = of_property_read_u32(np, "cali-gain3-ch3-4-5-6",
				&adc->gain_cali3_ch3_4_5_6);
		if (ret)
			adc->gain_cali3_ch3_4_5_6 = 0;
		ret = of_property_read_u32(np, "cali-gain4-ch3-4-5-6",
				&adc->gain_cali4_ch3_4_5_6);
		if (ret)
			adc->gain_cali4_ch3_4_5_6 = 0;
		ret = of_property_read_u32(np, "cali-gain5-ch3-4-5-6",
				&adc->gain_cali5_ch3_4_5_6);
		if (ret)
			adc->gain_cali5_ch3_4_5_6 = 0;
		ret = of_property_read_u32(np, "cali-gain6-ch3-4-5-6",
				&adc->gain_cali6_ch3_4_5_6);
		if (ret)
			adc->gain_cali6_ch3_4_5_6 = 0;
		ret = of_property_read_u32(np, "cali-gain7-ch3-4-5-6",
				&adc->gain_cali7_ch3_4_5_6);
		if (ret)
			adc->gain_cali7_ch3_4_5_6 = 0;

		ret = of_property_read_u32(np, "cali-offset0",
			&adc->offset_cali0);
		if (ret)
			adc->offset_cali0 = 0;
		ret = of_property_read_u32(np, "cali-offset2",
					&adc->offset_cali2);
		if (ret)
			adc->offset_cali2 = 0;
		/* fixup cali value according to ATE bug */
		if (adc->offset_cali0 > 0x2400)
			adc->offset_cali0 = (((~(adc->offset_cali0 & 0x1FFF)) +
					      1) & 0x1FFF) | 0x2000;
		if (adc->offset_cali2 > 0x2400)
			adc->offset_cali2 = (((~(adc->offset_cali2 & 0x1FFF)) +
					      1) & 0x1FFF) | 0x2000;
		dev_info(&pdev->dev, "cali offset: %x - %x\n",
					adc->offset_cali0, adc->offset_cali2);
	} else {
		adc->clk = devm_clk_get(&pdev->dev, NULL);
		if (IS_ERR(adc->clk)) {
			dev_err(&pdev->dev, "Get adc clk failed\n");
			ret = -ENOMEM;
			goto err;
		}

		clk_prepare_enable(adc->clk);

		sirfsoc_rtc_iobrg_writel(sirfsoc_rtc_iobrg_readl(
			SIRFSOC_PWRC_BASE + SIRFSOC_PWRC_TRIGGER_EN)
			| SIRFSOC_PWR_WAKEEN_TS,
			SIRFSOC_PWRC_BASE + SIRFSOC_PWRC_TRIGGER_EN);

		ret = device_reset(&pdev->dev);
		if (ret) {
			dev_err(&pdev->dev, "Failed to reset\n");
			goto err;
		}
	}

	/* Clear interrupts and enable PEN INTR */
	if (of_device_is_compatible(np, "sirf,atlas7-adc")) {
		writel(SIRFSOC_ADC_PEN_INTR | SIRFSOC_ADC_DATA_INTR,
			adc->base + adc_reg->intr_status);
		writel(SIRFSOC_ADC_PEN_INTR | SIRFSOC_ADC_DATA_INTR,
			adc->base + adc_reg->intr_enable);
	} else {
		writel(SIRFSOC_ADC_PEN_INTR | SIRFSOC_ADC_PEN_INTR_EN |
			SIRFSOC_ADC_DATA_INTR | SIRFSOC_ADC_DATA_INTR_EN,
			adc->base + adc_reg->intr_status);
	}

	writel(ctrl_set->reset, adc->base + adc_reg->ctrl1);
	writel(ctrl_set->prp_mode | ctrl_set->rtouch | ctrl_set->del_pre |
		ctrl_set->del_dis, adc->base + adc_reg->ctrl2);

	sirfsoc_adc_ts_reg[0] = adc_reg->ts_reg.coord1;
	sirfsoc_adc_ts_reg[1] = adc_reg->ts_reg.coord2;
	sirfsoc_adc_ts_reg[2] = adc_reg->ts_reg.coord3;
	sirfsoc_adc_ts_reg[3] = adc_reg->ts_reg.coord4;

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

	ret = of_platform_populate(np, sirfsoc_adc_of_match, NULL, &pdev->dev);
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
	if (of_device_is_compatible(np, "sirf,atlas7-adc")) {
		iounmap(adc->ana_base);
		clk_disable_unprepare(adc->clk_analog);
		clk_disable_unprepare(adc->clk_io);
	}
	clk_disable_unprepare(adc->clk);
	mutex_destroy(&adc->mutex);
	return ret;
}

static int sirfsoc_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct sirfsoc_adc *adc = iio_priv(indio_dev);
	struct device_node *np = pdev->dev.of_node;

	iio_device_unregister(indio_dev);

	if (of_device_is_compatible(np, "sirf,atlas7-adc")) {
		clk_disable_unprepare(adc->clk_analog);
		clk_disable_unprepare(adc->clk_io);
		iounmap(adc->ana_base);
	}
	clk_disable_unprepare(adc->clk);
	mutex_destroy(&adc->mutex);
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

MODULE_DESCRIPTION("SiRF SoC On-chip ADC driver");
MODULE_LICENSE("GPL v2");
