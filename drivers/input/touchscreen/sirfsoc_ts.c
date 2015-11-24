/*
 * sirfsoc touch controller Driver
 *
 * Copyright (c) 2013-2016, The Linux Foundation. All rights reserved.
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
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/reset.h>
#include <linux/iio/consumer.h>

#ifdef CONFIG_TOUCHSCREEN_SIRFSOC_CALIBRATE
#include "ts_linear.h"
#endif

#define DRIVER_NAME		"sirfsoc_tsc"

#define DATA_SHIFT_BITS		14
#define DATA_XMASK		(0x3FFF << 0)
#define DATA_YMASK		(0x3FFF << DATA_SHIFT_BITS)
#define GETX(val)		((val) & DATA_XMASK)
#define GETY(val)		(((val) & DATA_YMASK) >> DATA_SHIFT_BITS)

/*
 * If new coordinates are close enough to last ones, last values
 * should be used to suppress glitches.
 */
#define TS_GLITCH_GAP		60

/* Dual touch: Configurable parameters */
#define TS_PREC_BITS		10	/* Precision, must >= 2 */
#define TS_DUAL_MIN		15	/* Min UAD/UBC of dual touch points */
/* Dual touch: Fixed parameter */
#define TS_V_MAX		(1 << DATA_SHIFT_BITS)	/* Full scale AD */
/* Dual touch: Screen dependent parameters */
#define TS_RX			694	/* X-plane resister */
#define TS_RY			228	/* Y-plane resister */
#define TS_V			10800	/* Max AD (LeftUpper corner) */
#define TS_COEF_MIN		((u32)(0.100f * (1 << TS_PREC_BITS)))
#define TS_COEF_MAX		((u32)(100.0f * (1 << TS_PREC_BITS)))
#define TS_COEF_DEFAULT		((u32)(2.000f * (1 << TS_PREC_BITS)))
#define TS_RTOUCH_NORMAL	700	/* Normal touch resister */
#define TS_RTOUCH_MIN		(70 << TS_PREC_BITS)	/* 70 Ohm */
#define TS_RTOUCH_MAX		(1700 << TS_PREC_BITS)	/* 1700 Ohm */
#define TS_RTOUCH_DUAL_UP	350	/* Upper bound of dual touch */
#define TS_RTOUCH_SINGLE_LOW	450	/* Lower bound of single touch */
#define TS_SINGLE_MAXGAP_X	100	/* Max UAD of single touch point */
#define TS_SINGLE_MAXGAP_Y	50	/* Max UBC of single touch point */

/* Select AD samples to read (SEL bits in ADC_CONTROL1 register) */
#define SIRFSOC_TS_SEL_X	0x01	/* x sample */
#define SIRFSOC_TS_SEL_Y	0x02	/* y sample */
#define SIRFSOC_TS_SEL_DUAL	0x0F	/* eight samples for dual touch */
#define SIRFSOC_TS_CTL1(sel)	(ADC_POLL | ADC_SEL(sel) | ADC_DEL_SET(6) \
		| ADC_FREQ_6K | ADC_TP_TIME(0) | ADC_SGAIN(0) \
		| ADC_EXTCM(0) | ADC_RBAT_DISABLE | ADC_MORE_CTL1)

/* AD sample indexes (single touch) */
enum {
	X,
	Y,
	AD_SAMPLE_COUNT_SINGLE
};

/* AD sample indexes (dual touch) */
enum {
	XPXN_YP,
	YPYN_XP,
	XPXN_YN,
	YPYN_XN,
	XPYN_YP,
	XPYN_XN,
	YPXN_XP,
	YPXN_YN,
	AD_SAMPLE_COUNT_DUAL
};

#define AD_REG_COUNT		(AD_SAMPLE_COUNT_DUAL / 2)

struct sirfsoc_ts {
	char			phys[32];

	/* AD samples buffer of current detection */
	u32			samples[AD_SAMPLE_COUNT_DUAL];

	/* Calculated coordinates of current detection */
	int			sampled_x[2], sampled_y[2];
	/* Last successfully calculated and issued coordinates */
	int			issued_x[2], issued_y[2];
	bool			press_down;

	/* Fingers detected pressing on the screen
	 * always 1 for single touch driver
	 * maybe 1 or 2 for dual touch driver, depends on calculation
	 */
	int			fingers;

	/* Last touching event is a dual touch */
	bool			last_is_dual;

	struct input_dev	*input;

	/* Points to touch specific data */
	const struct sirfsoc_ts_of_data_touch	*touch;

	/* ADC data channel for ts*/
	struct iio_channel	*chan;
};

/* Single/dual touch specific data and routines */
struct sirfsoc_ts_of_data_touch {
	/* Number of continuous stable samples for debouncing */
	int	debounce_rep;
	/* Max deviation of AD values for stable samples */
	int	debounce_dev;
	/* Callback routine to read and calculate coordinates */
	int	(*get_coord_and_pen)(struct sirfsoc_ts *);
	/* Callback routine to read AD samples */
	int	(*read_samples)(struct sirfsoc_ts *, u32 *);
};

/*
 * Debounce routine shared by single and dual touch
 * sample_count: how many AD samples needs to take care
 */
static int sirfsoc_ts_debounce(struct sirfsoc_ts *ts, int sample_count)
{
	int i, j;
	u32 samples[AD_SAMPLE_COUNT_DUAL];	/* Max of single/dual samples */

	/* First sample */
	if ((*(ts->touch->read_samples))(ts, ts->samples))
		return -EBUSY;

	/* Debouncing
	 * - Condition for a stable reading: Three (debounce_rep) continuous
	 *   reading with AD samples deviation in bound (debounce_dev)
	 * - ts->samples[] stores sum of previous readings
	 * - samples[] stored current reading
	 */
	for (i = 1; i < ts->touch->debounce_rep; i++) {
		/* Get raw AD samples */
		if ((*(ts->touch->read_samples))(ts, samples))
			return -EBUSY;
		/* Verify adjacent samples deviation */
		for (j = 0; j < sample_count; j++) {
			if (abs(ts->samples[j] / i - samples[j]) >
					ts->touch->debounce_dev)
				return -EINVAL;
			ts->samples[j] += samples[j];
		}
	}

	/* Average samples, store to ts->samples */
	for (i = 0; i < ts->touch->debounce_rep; i++)
		ts->samples[i] /= ts->touch->debounce_rep;

	return 0;
}

static int sirfsoc_ts_read_samples_single(struct sirfsoc_ts *ts, u32 *samples)
{
	int raw_sample;
	int ret;

	ret = iio_read_channel_raw(ts->chan, &raw_sample);
	if (ret < 0) {
		ts->press_down = false;
		return ret;
	}

	/* x sample */
	samples[X] = GETX(raw_sample);
	/* y sample */
	samples[Y] = GETY(raw_sample);

	ts->press_down = true;
	return 0;
}

/*
 * Calculate single touch coordinate
 * coord: (ts->sampled_x[0], y[0])
 */
static int sirfsoc_ts_get_coord_and_pen_single(struct sirfsoc_ts *ts)
{
	int ret;

	ret = sirfsoc_ts_debounce(ts, AD_SAMPLE_COUNT_SINGLE);
	if (ret < 0)
		return ret;

	ts->sampled_x[0] = ts->samples[X];
	ts->sampled_y[0] = ts->samples[Y];

	return 0;
}

#ifdef CONFIG_TOUCHSCREEN_SIRFSOC_DUAL_TOUCH
/* Read eight AD samples from adc */
static int sirfsoc_ts_read_samples_dual(struct sirfsoc_ts *ts, u32 *samples)
{
	int i, ret;
	int raw_samples[AD_REG_COUNT];

	ret = iio_read_channel_raw(ts->chan, raw_samples);
	if (ret < 0) {
		ts->press_down = false;
		return ret;
	}

	for (i = 0; i < AD_REG_COUNT; i++) {
		*samples++ = GETX(raw_samples[i]);
		*samples++ = GETY(raw_samples[i]);
	}

	ts->press_down = true;
	return 0;
}

/*
 * Calculate dual touch coordinates
 *
 * - Schematic
 *
 *             RX1     A      RX2     D      RX3
 *    XP ----/\/\/\----o----/\/\/\----o----/\/\/\---- XN
 *                     |              |
 *                     /              /
 *              Rtouch \       Rtouch \
 *                     /              /
 *                     \              \
 *                     |              |
 *    YP ----/\/\/\----o----/\/\/\----o----/\/\/\---- YN
 *             RY1     B      RY2     C      RY3
 *
 * coord1: (ts->sampled_x[0], y[0]); coord2: (ts->sampled_x[1], y[1])
 */
static int sirfsoc_ts_calculate_dual(struct sirfsoc_ts *ts)
{
	u32 tmp;
	int ubc, uad;		/* |UB-UC|, |UA-UD| */
	u32 x, y, rx2, ry2;	/* Intermediate results */
	u64 a64, b64, c64;	/* Equation coefficients */
	u32 rtouch;		/* Touch resister between X & Y planes */
	bool neg_slope;		/* Slope of the line connecting dual points,
				   negative if LeftUpper and RightDown */
	u32 *samples = ts->samples;

	/*
	 * Step 1: Calculate Rtouch
	 *
	 * Rtouch depends on pressure and single/dual touch,
	 * Dual touch halves the value approximately.
	 * Two equivalent approaches:
	 * 1. TS_RY * ypyn_xp * (xpyn_xn/xpyn_yp - 1) / TS_V
	 * 2. TS_RX * xpxn_yp * (ypxn_yn/ypxn_xp - 1) / TS_V
	 *
	 * CAUTION:
	 * rtouch is multiplied by 1024 to keep enough precision bits
	 */
	a64 = TS_RY * samples[YPYN_XP];
	a64 <<= TS_PREC_BITS;	/* *1024 */
	do_div(a64, TS_V);
	b64 = a64 * samples[XPYN_XN];
	do_div(b64, samples[XPYN_YP]);
	rtouch = (u32)(b64 - a64);
	if (rtouch < TS_RTOUCH_MIN || rtouch > TS_RTOUCH_MAX)
		return -EINVAL;	/* Unstable reading */

	/*
	 * Step2: Check single touch
	 *
	 * As single touch is the dominant case, it should be handled first.
	 * Chance is prominent that we may skip all dual touch related messes.
	 */
	ubc = samples[XPXN_YP] - samples[XPXN_YN];
	uad = samples[YPYN_XP] - samples[YPYN_XN];
	neg_slope = (ubc < 0);
	ubc = abs(ubc);

	/* UA-UD should follow the same sign as UB-UC */
	if ((uad < 0) != neg_slope)
		goto single_touch;
	uad = abs(uad);
	/* Very closed samples mean single touch or vertical/horizontal */
	if (ubc <= TS_DUAL_MIN || uad <= TS_DUAL_MIN)
		goto single_touch;
	/* Fast scratching on the screen may cause misdetection */
	if (rtouch > (TS_RTOUCH_SINGLE_LOW<<TS_PREC_BITS))
		goto single_touch;

	/*
	 * Step 3: Calculate intermediate value x (slope)
	 *
	 * CAUTION:
	 * x is multiplied by 1024 to keep enough precision bits
	 */
	x = TS_COEF_DEFAULT;	/* Fixed slope at about +/-45 degree */

	/*
	 * Step4: Calculate RX2 & RY2
	 *
	 * Solve equation a*y^2 - b*y - c = 0, where:
	 * a = |UA-UD| + x * TS_V
	 * b = (1+x) * |UA-UD| * TS_RY
	 * c = 2 * |UA-UD| * TS_RY * RTouch
	 *
	 * CAUTION:
	 * RX2,RY2 is multiplied by 256 to keep enough precision bits
	 */
	a64 = x;
	a64 *= TS_V;
	a64 >>= TS_PREC_BITS;
	a64 += uad;
	b64 = uad;
	b64 *= TS_RY;
	b64 *= ((1<<(TS_PREC_BITS-1)) + (x>>1));	/* *512 */
	c64 = 2 * uad;
	c64 *= TS_RY;
	c64 *= TS_RTOUCH_NORMAL;	/* Fixed RTouch */

	/* a: normal; b: *512; c: normal */
	do_div(b64, a64);	/* (b/2a)*1024 */
	do_div(c64, a64);	/* (c/a) */

	c64 <<= (2*TS_PREC_BITS);
	a64 = int64_sqrt(b64*b64 + c64);
	a64 += b64;
	a64 >>= 2;		/* *256 */
	y = (u32)a64;

	ry2 = y;
	rx2 = (x * y) >> TS_PREC_BITS;
	/* rx2, ry2: Left shifted 8 bits */

	/* Step5: Calculate coordinates
	 *
	 * center: x0 = (UB+UC)/2, y0 = (UA+UD)/2
	 * delta : dx = 0.5 * TS_V * RX2 / TS_RX
	 *         dy = 0.5 * TS_V * RY2 / TS_RY
	 * -------------------------
	 * |points  |x     | y     |
	 * -------------------------
	 * |(x1,y1) |x0-dx | y0-dy |
	 * |(x2,y2) |x0+dx | y0+dy |
	 * -------------------------
	 */

	/* x1, x2 */
	x = (samples[XPXN_YP] + samples[XPXN_YN]) << (TS_PREC_BITS-2);
	tmp = (TS_V * rx2) / TS_RX;
	/* x = x0 * 512, tmp = dx * 512 */
	if (unlikely(x <= tmp))
		ts->sampled_x[0] = 1;
	else
		ts->sampled_x[0] = (x - tmp) >> (TS_PREC_BITS-1);
	ts->sampled_x[1] = (x + tmp) >> (TS_PREC_BITS-1);

	/* y1, y2 */
	y = (samples[YPYN_XP] + samples[YPYN_XN]) << (TS_PREC_BITS-2);
	tmp = (TS_V * ry2) / TS_RY;
	/* y = y0 * 512, tmp = dy * 512 */
	if (unlikely(y <= tmp))
		ts->sampled_y[0] = 1;
	else
		ts->sampled_y[0] = (y - tmp) >> (TS_PREC_BITS-1);
	ts->sampled_y[1] = (y + tmp) >> (TS_PREC_BITS-1);

	/* Verify data */
	if (unlikely(ts->sampled_x[0] > ts->sampled_x[1] ||
		     ts->sampled_y[0] > ts->sampled_y[1] ||
		     ts->sampled_x[1] >= TS_V_MAX ||
		     ts->sampled_y[1] >= TS_V_MAX))
		return -EINVAL;	/* Illegal data */
	if (ts->sampled_x[1] >= TS_V)
		ts->sampled_x[1] = TS_V - 1;
	if (ts->sampled_y[1] >= TS_V)
		ts->sampled_y[1] = TS_V - 1;

	/* Swap x coordinates if negative slope */
	if (neg_slope) {
		tmp = ts->sampled_x[0];
		ts->sampled_x[0] = ts->sampled_x[1];
		ts->sampled_x[1] = tmp;
	}

	/* Dual touch detected */
	ts->fingers = 2;
	return 0;

	/* Fall back to single touch */
single_touch:
	/*
	 * Make sure it's not a misdetected dual touch:
	 * - Touch resister must be above upper bound of dual touch
	 * - |UB-UC| and |UA-UD| are below upper bound of single touch
	 */
	if (rtouch >= (TS_RTOUCH_DUAL_UP<<TS_PREC_BITS)
			&& ubc <= TS_SINGLE_MAXGAP_Y
			&& uad <= TS_SINGLE_MAXGAP_X) {
		/* Get single touch coordinate */
		ts->sampled_x[0] = (samples[XPXN_YP] + samples[XPXN_YN]) / 2;
		ts->sampled_y[0] = (samples[YPYN_XP] + samples[YPYN_XN]) / 2;
		/* Single touch detected */
		ts->fingers = 1;
		return 0;
	} else {
		return -EINVAL;	/* Drop uncertainty */
	}
}

static int sirfsoc_ts_get_coord_and_pen_dual(struct sirfsoc_ts *ts)
{
	int ret;
	bool is_dual;

	/* Read samples and do debouncing */
	ret = sirfsoc_ts_debounce(ts, AD_SAMPLE_COUNT_DUAL);
	if (ret < 0)
		return ret;

	/* Calculate coordinates */
	ret = sirfsoc_ts_calculate_dual(ts);
	if (ret < 0)
		return ret;

	/*
	 * First switching from single to dual or dual to single are
	 * sometimes unstable, drop it.
	 */
	is_dual = (ts->fingers == 2);	/* Current is dual touch? */
	if (is_dual != ts->last_is_dual) {
		ts->last_is_dual = is_dual;
		return -EINVAL;
	}

	return 0;
}
#endif

/* Report touch events to event driver */
static void sirfsoc_ts_report_coord(struct sirfsoc_ts *ts)
{
	int i;

	input_report_abs(ts->input, ABS_PRESSURE, 1);
	input_report_key(ts->input, BTN_TOUCH, 1);

	for (i = 0; i < ts->fingers; i++) {
		/* Filter small glitches */
		if (abs(ts->sampled_x[i] - ts->issued_x[i]) < TS_GLITCH_GAP &&
		    abs(ts->sampled_y[i] - ts->issued_y[i]) < TS_GLITCH_GAP) {
			/*
			 * New coord is very close to last checking,
			 * adopt old coord instead
			 */
			ts->sampled_x[i] = ts->issued_x[i];
			ts->sampled_y[i] = ts->issued_y[i];
		} else {
			/* Save new coord for next time comparison */
			ts->issued_x[i] = ts->sampled_x[i];
			ts->issued_y[i] = ts->sampled_y[i];
		}

#ifdef CONFIG_TOUCHSCREEN_SIRFSOC_CALIBRATE
		ts_linear_scale(&ts->sampled_x[i], &ts->sampled_y[i]);
#endif

		input_report_abs(ts->input, ABS_MT_POSITION_X,
				ts->sampled_x[i]);
		input_report_abs(ts->input, ABS_MT_POSITION_Y,
				ts->sampled_y[i]);
		input_mt_sync(ts->input);
	}

	input_sync(ts->input);
}

static irqreturn_t sirfsoc_ts_thread_irq(int irq, void *handle)
{
	struct sirfsoc_ts *ts = (struct sirfsoc_ts *)handle;
	struct input_dev *input = ts->input;
	int ret;

	/*
	 * we have pen down interrupt, but before pen up, no more will come
	 */
	do {
		ret = (*(ts->touch->get_coord_and_pen))(ts);
		if (!ret)
			sirfsoc_ts_report_coord(ts);

		usleep_range(5000, 8000);
	} while (ts->press_down);

	input_report_key(input, BTN_TOUCH, 0);
	input_report_abs(input, ABS_PRESSURE, 0);
	input_sync(input);

	return IRQ_HANDLED;
}

static const struct sirfsoc_ts_of_data_touch sirfsoc_ts_of_data_single = {
	.debounce_rep		= 3,
	.debounce_dev		= 200,
	.get_coord_and_pen	= sirfsoc_ts_get_coord_and_pen_single,
	.read_samples		= sirfsoc_ts_read_samples_single,
};

#ifdef CONFIG_TOUCHSCREEN_SIRFSOC_DUAL_TOUCH
static const struct sirfsoc_ts_of_data_touch sirfsoc_ts_of_data_dual = {
	.debounce_rep		= 4,
	.debounce_dev		= 500,
	.get_coord_and_pen	= sirfsoc_ts_get_coord_and_pen_dual,
	.read_samples		= sirfsoc_ts_read_samples_dual,
};
#endif

static const struct of_device_id sirfsoc_ts_of_match[] = {
	{ .compatible = "sirf,prima2-tsc", },
	{}
};
MODULE_DEVICE_TABLE(of, sirfsoc_ts_of_match);

static int sirfsoc_ts_probe(struct platform_device *pdev)
{
	struct input_dev		*input_dev;
	struct sirfsoc_ts		*ts;
	int				ret;
	int				i;
	int				irq;

	const unsigned int codes[] = {
		KEY_HOME, KEY_MENU, KEY_BACK, KEY_SEARCH,
	};

	ts = devm_kzalloc(&pdev->dev, sizeof(struct sirfsoc_ts), GFP_KERNEL);
	if (!ts) {
		dev_err(&pdev->dev,
			"sirfsoc ts: Cant allocate driver private data\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, ts);

/* Touch mode specific */
#ifdef CONFIG_TOUCHSCREEN_SIRFSOC_DUAL_TOUCH
	ts->touch = &sirfsoc_ts_of_data_dual;
	ts->chan = iio_channel_get(&pdev->dev, "dual_ts");
#else
	ts->touch = &sirfsoc_ts_of_data_single;
	ts->chan = iio_channel_get(&pdev->dev, "single_ts");
#endif
	if (IS_ERR(ts->chan)) {
		dev_err(&pdev->dev, "sirfsoc ts: Unable to get the adc channel\n");
		ret = PTR_ERR(ts->chan);
		goto out0;
	}

	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&pdev->dev,
			"sirfsoc ts: Unable to allocate input device\n");
		ret = -ENOMEM;
		goto out1;
	}

	snprintf(ts->phys, sizeof("SIRFSOC-TS"), "SIRFSOC-TS");
	input_dev->name = "sirfsoc_touchscreen";
	input_dev->phys = ts->phys;
	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS)
				| BIT_MASK(EV_SYN);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);

	input_set_abs_params(input_dev, ABS_PRESSURE, 0, 1, 0, 0);
	input_set_abs_params(input_dev, ABS_TOOL_WIDTH, 0, 15, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, 1, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR, 0, 15, 0, 0);

	for (i = 0; i < ARRAY_SIZE(codes); i++)
		input_set_capability(input_dev, EV_KEY, codes[i]);

	ret = input_register_device(input_dev);
	if (ret) {
		dev_err(&pdev->dev,
			"sirfsoc ts: Unable to register input device\n");
		goto out2;
	}
	ts->input = input_dev;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "sirfsoc tsc: get irq failed!\n");
		ret = -ENOMEM;
		goto out3;
	}

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
		sirfsoc_ts_thread_irq, IRQF_ONESHOT, DRIVER_NAME, ts);
	if (ret < 0) {
		dev_err(&pdev->dev, "sirfsoc ts: regist irq handler failed!\n");
		ret = -ENODEV;
		goto out3;
	}

	input_set_abs_params(input_dev, ABS_X, 0, 0x3FFF, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, 0x3FFF, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0, 0x3FFF, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, 0x3FFF, 0, 0);

	/* Default to single touch */
	ts->fingers = 1;

	return 0;
out3:
	input_unregister_device(input_dev);
out2:
	input_free_device(input_dev);
out1:
	iio_channel_release(ts->chan);
out0:
	return ret;
}

static int sirfsoc_ts_remove(struct platform_device *pdev)
{
	struct sirfsoc_ts *ts = platform_get_drvdata(pdev);

	input_unregister_device(ts->input);
	iio_channel_release(ts->chan);

	return 0;
}

static void sirfsoc_ts_shutdown(struct platform_device *dev)
{
	sirfsoc_ts_remove(dev);
}

static struct platform_driver tsc_sirfsoc_driver = {
	.driver	 = {
		.name   = DRIVER_NAME,
		.of_match_table = sirfsoc_ts_of_match,
	},
	.probe	  = sirfsoc_ts_probe,
	.remove	 = sirfsoc_ts_remove,
	.shutdown       = sirfsoc_ts_shutdown,
};

module_platform_driver(tsc_sirfsoc_driver);

MODULE_DESCRIPTION("SiRF SoC On-chip Touch screen driver");
MODULE_LICENSE("GPL v2");
