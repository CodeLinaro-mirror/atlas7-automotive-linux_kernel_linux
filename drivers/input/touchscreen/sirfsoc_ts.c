/*
 * sirfsoc touch controller Driver
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
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
#include <linux/rtc/sirfsoc_rtciobrg.h>
#include <linux/input/sirfsoc_adc.h>

#include "sirfsoc_ts_linear.h"

#define DRIVER_NAME "sirfsoc_tsc"

#define PWR_WAKEEN_TSC_SHIFT	23
#define PWR_WAKEEN_TS_SHIFT	5
#define SIRFSOC_PWRC_TRIGGER_EN	0x8
#define SIRFSOC_PWRC_BASE	0x3000

/* If new coordinates are close enough to last ones, last values
 * should be used to suppress glitches.
 */
#define TS_GLITCH_GAP		60

/* Dual touch: Configurable parameters */
#define TS_PREC_BITS		10	/* Precision, must >= 2 */
#define TS_DUAL_MIN		15	/* Min UAD/UBC of dual touch points */
/* Dual touch: Fixed parameter */
#define TS_V_MAX		(1<<DATA_SHIFT_BITS)	/* Full scale AD */
/* Dual touch: Screen dependent parameters */
#define TS_RX			694	/* X-plane resister */
#define TS_RY			228	/* Y-plane resister */
#define TS_V			10800	/* Max AD (LeftUpper corner) */
#define TS_COEF_MIN		((u32)(0.100f * (1<<TS_PREC_BITS)))
#define TS_COEF_MAX		((u32)(100.0f * (1<<TS_PREC_BITS)))
#define TS_COEF_DEFAULT		((u32)(2.000f * (1<<TS_PREC_BITS)))
#define TS_RTOUCH_NORMAL	700	/* Normal touch resister */
#define TS_RTOUCH_MIN		(70<<TS_PREC_BITS)	/* 70 Ohm */
#define TS_RTOUCH_MAX		(1700<<TS_PREC_BITS)	/* 1700 Ohm */
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

/* AD registers */
#define AD_REG_COUNT (AD_SAMPLE_COUNT_DUAL / 2)
static const u32 sirfsoc_ts_reg[AD_REG_COUNT] = {
	ADC_COORD, ADC_COORD2, ADC_COORD3, ADC_COORD4
};

struct sirfsoc_ts {
	char			phys[32];

	/* AD samples buffer of current detection */
	u32			samples[AD_SAMPLE_COUNT_DUAL];

	/* Calculated coordinates of current detection */
	int			sampled_x[2], sampled_y[2];
	/* Last successfully calculated and issued coordinates */
	int			issued_x[2], issued_y[2];

	/* Fingers detected pressing on the screen
	 * always 1 for single touch driver
	 * maybe 1 or 2 for dual touch driver, depends on calculation
	 */
	int			fingers;

	/* Last touching event is a dual touch */
	bool			last_is_dual;

	struct delayed_work	report_work;

	struct input_dev	*input;

	/* Points to touch specific data */
	const struct sirfsoc_ts_of_data_touch	*touch;
};

/* Single/dual touch specific data and routines */
struct sirfsoc_ts_of_data_touch {
	/* Number of continuous stable samples for debouncing */
	int	debounce_rep;
	/* Max deviation of AD values for stable samples */
	int	debounce_dev;
	/* Callback routine to read and calculate coordinates */
	int	(*get_coord)(struct sirfsoc_ts *);
	/* Callback routine to read AD samples */
	int	(*read_samples)(struct sirfsoc_ts *, u32 *);
};

static inline int sirfsoc_ts_get_pendown(struct sirfsoc_ts *ts)
{
	return sirfsoc_adc_read_reg(ADC_COORD) & PEN_DOWN;
}

/* Debounce routine shared by single and dual touch
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
	 *   reading with AD samples deviation less than 50 (debounce_dev)
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
	u32 coord;

	/* x sample */
	sirfsoc_adc_write_reg(SIRFSOC_TS_CTL1(SIRFSOC_TS_SEL_X), ADC_CONTROL1);
	if (sirfsoc_adc_sync_reg() < 0)
		return -EBUSY;
	coord = sirfsoc_adc_read_reg(ADC_COORD);
	coord &= DATA_XMASK;	/* Bits 13~0 */
	samples[X] = coord;

	/* y sample */
	sirfsoc_adc_write_reg(SIRFSOC_TS_CTL1(SIRFSOC_TS_SEL_Y), ADC_CONTROL1);
	if (sirfsoc_adc_sync_reg() < 0)
		return -EBUSY;
	coord = sirfsoc_adc_read_reg(ADC_COORD);
	coord &= DATA_YMASK;	/* Bits 27~14 */
	coord >>= DATA_SHIFT_BITS;
	samples[Y] = coord;

	return 0;
}

/* Calculate single touch coordinate
 * coord: (ts->sampled_x[0], y[0])
 */
static int sirfsoc_ts_get_coord_single(struct sirfsoc_ts *ts)
{
	int ret;

	ret = sirfsoc_ts_debounce(ts, AD_SAMPLE_COUNT_SINGLE);
	if (ret < 0)
		return ret;

	ts->sampled_x[0] = ts->samples[X];
	ts->sampled_y[0] = ts->samples[Y];

	return 0;
}

/* Read eight AD samples from TS controller */
static int sirfsoc_ts_read_samples_dual(struct sirfsoc_ts *ts, u32 *samples)
{
	int i;

	sirfsoc_adc_write_reg(SIRFSOC_TS_CTL1(SIRFSOC_TS_SEL_DUAL),
			ADC_CONTROL1);
	if (sirfsoc_adc_sync_reg() < 0)
		return -EBUSY;

	for (i = 0; i < AD_REG_COUNT; i++) {
		u32 tmp;
		tmp = sirfsoc_adc_read_reg(sirfsoc_ts_reg[i]);
		*samples++ = tmp & DATA_XMASK;
		*samples++ = (tmp & DATA_YMASK) >> DATA_SHIFT_BITS;
	}

	return 0;
}

/* Calculate dual touch coordinates
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

	/* Step 1: Calculate Rtouch
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

	/* Step2: Check single touch
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

	/* Step 3: Calculate intermediate value x (slope)
	 *
	 * CAUTION:
	 * x is multiplied by 1024 to keep enough precision bits
	 */
	x = TS_COEF_DEFAULT;	/* Fixed slope at about +/-45 degree */

	/* Step4: Calculate RX2 & RY2
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
	/* Make sure it's not a misdetected dual touch:
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

static int sirfsoc_ts_get_coord_dual(struct sirfsoc_ts *ts)
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

	/* First switching from single to dual or dual to single are
	 * sometimes unstable, drop it.
	 */
	is_dual = (ts->fingers == 2);	/* Current is dual touch? */
	if (is_dual != ts->last_is_dual) {
		ts->last_is_dual = is_dual;
		return -EINVAL;
	}

	return 0;
}

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
			/* New coord is very close to last checking,
			 * adopt old coord instead
			 */
			ts->sampled_x[i] = ts->issued_x[i];
			ts->sampled_y[i] = ts->issued_y[i];
		} else {
			/* Save new coord for next time comparison */
			ts->issued_x[i] = ts->sampled_x[i];
			ts->issued_y[i] = ts->sampled_y[i];
		}

		ts_linear_scale(&ts->sampled_x[i], &ts->sampled_y[i]);

		input_report_abs(ts->input, ABS_MT_POSITION_X,
				ts->sampled_x[i]);
		input_report_abs(ts->input, ABS_MT_POSITION_Y,
				ts->sampled_y[i]);
		input_mt_sync(ts->input);
	}

	input_sync(ts->input);
}

static void sirfsoc_ts_report_work(struct work_struct *work)
{
	struct delayed_work *dw = container_of(work, struct delayed_work, work);
	struct sirfsoc_ts *ts = container_of(dw,
				struct sirfsoc_ts, report_work);
	struct input_dev *input = ts->input;

	if (sirfsoc_ts_get_pendown(ts)) {
		if ((*(ts->touch->get_coord))(ts) == 0)
			sirfsoc_ts_report_coord(ts);

		schedule_delayed_work(&ts->report_work, msecs_to_jiffies(10));
	} else {
		input_report_key(input, BTN_TOUCH, 0);
		input_report_abs(input, ABS_PRESSURE, 0);
		input_sync(input);
	}
}

static irqreturn_t sirfsoc_ts_hard_irq(int irq, void *handle)
{
	struct sirfsoc_ts *ts = (struct sirfsoc_ts *)handle;
	int adc_intr;

	adc_intr = sirfsoc_adc_read_reg(ADC_INTR);
	if (adc_intr & PEN_INTR)
		sirfsoc_adc_write_reg(PEN_INTR | PEN_INTR_EN | DATA_INTR_EN,
			ADC_INTR);

	schedule_delayed_work(&ts->report_work, msecs_to_jiffies(10));

	return IRQ_HANDLED;
}

static const struct sirfsoc_ts_of_data_touch sirfsoc_ts_of_data_single = {
	.debounce_rep	= 3,
	.debounce_dev	= 50,
	.get_coord	= sirfsoc_ts_get_coord_single,
	.read_samples	= sirfsoc_ts_read_samples_single,
};

static const struct sirfsoc_ts_of_data_touch sirfsoc_ts_of_data_dual = {
	.debounce_rep	= 4,
	.debounce_dev	= 500,
	.get_coord	= sirfsoc_ts_get_coord_dual,
	.read_samples	= sirfsoc_ts_read_samples_dual,
};

static const struct of_device_id sirfsoc_ts_of_match[] = {
	{ .compatible = "sirf,prima2-tsc",
	  .data = &sirfsoc_ts_of_data_single },
	{ .compatible = "sirf,marco-tsc",
	  .data = &sirfsoc_ts_of_data_single },
	{ .compatible = "sirf,dualtouch-tsc",
	  .data = &sirfsoc_ts_of_data_dual },
	{}
};

static int sirfsoc_ts_probe(struct platform_device *pdev)
{
	struct input_dev		*input_dev;
	struct sirfsoc_ts		*ts;
	int				ret;
	int				i;
	int				irq;
	const struct of_device_id	*match;

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

	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&pdev->dev,
			"sirfsoc ts: Unable to allocate input device\n");
		ret = -ENOMEM;
		goto out1;
	}

	INIT_DELAYED_WORK(&ts->report_work, sirfsoc_ts_report_work);

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

	sirfsoc_rtc_iobrg_writel(sirfsoc_rtc_iobrg_readl(SIRFSOC_PWRC_BASE +
		SIRFSOC_PWRC_TRIGGER_EN) | (1 << PWR_WAKEEN_TS_SHIFT),
		SIRFSOC_PWRC_BASE + SIRFSOC_PWRC_TRIGGER_EN);

	device_reset(&pdev->dev);

	sirfsoc_adc_write_reg(ADC_PRP_MODE3 | ADC_RTOUCH(1) |
		ADC_DEL_PRE(2) | ADC_DEL_DIS(5), ADC_CONTROL2);

	/* Clear interrupts and enable PEN INTR */
	sirfsoc_adc_write_reg(sirfsoc_adc_read_reg(ADC_INTR) |
		PEN_INTR | DATA_INTR | PEN_INTR_EN | DATA_INTR_EN, ADC_INTR);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "sirfsoc tsc: get irq failed!\n");
		ret = -ENOMEM;
		goto out3;
	}

	ret = devm_request_irq(&pdev->dev, irq, sirfsoc_ts_hard_irq,
		IRQF_ONESHOT, DRIVER_NAME, ts);

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

	/* Touch specific data */
	match = of_match_device(of_match_ptr(sirfsoc_ts_of_match), &pdev->dev);
	ts->touch = match->data;

	return 0;
out3:
	input_unregister_device(input_dev);
out2:
	input_free_device(input_dev);
out1:
	return ret;
}

static int sirfsoc_ts_remove(struct platform_device *pdev)
{
	struct sirfsoc_ts *ts = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&ts->report_work);

	input_unregister_device(ts->input);

	return 0;
}

#ifdef CONFIG_PM
static int sirfsoc_ts_suspend(struct device *device)
{
	sirfsoc_rtc_iobrg_writel(sirfsoc_rtc_iobrg_readl(
		SIRFSOC_PWRC_BASE + SIRFSOC_PWRC_TRIGGER_EN)
		& ~(1 << PWR_WAKEEN_TSC_SHIFT),
		SIRFSOC_PWRC_BASE + SIRFSOC_PWRC_TRIGGER_EN);

	return 0;
}

static int sirfsoc_ts_resume(struct device *device)
{
	int val;

	sirfsoc_rtc_iobrg_writel(sirfsoc_rtc_iobrg_readl(
		SIRFSOC_PWRC_BASE + SIRFSOC_PWRC_TRIGGER_EN)
		| (1 << PWR_WAKEEN_TS_SHIFT),
		SIRFSOC_PWRC_BASE + SIRFSOC_PWRC_TRIGGER_EN);

	device_reset(device);

	sirfsoc_adc_write_reg(ADC_PRP_MODE3 | ADC_RTOUCH(1) |
		ADC_DEL_PRE(2) | ADC_DEL_DIS(5), ADC_CONTROL2);

	val = sirfsoc_adc_read_reg(ADC_INTR);

	/* Clear interrupts and enable PEN INTR */
	sirfsoc_adc_write_reg(val | PEN_INTR | DATA_INTR |
		PEN_INTR_EN | DATA_INTR_EN, ADC_INTR);
	return 0;
}
#endif

static void sirfsoc_ts_shutdown(struct platform_device *dev)
{
	sirfsoc_ts_remove(dev);
}

static const struct dev_pm_ops sirfsoc_ts_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sirfsoc_ts_suspend, sirfsoc_ts_resume)
};

static struct platform_driver tsc_sirfsoc_driver = {
	.driver	 = {
		.name   = DRIVER_NAME,
#ifdef CONFIG_PM
		.pm     = &sirfsoc_ts_pm_ops,
#endif
		.of_match_table = sirfsoc_ts_of_match,
	},
	.probe	  = sirfsoc_ts_probe,
	.remove	 = sirfsoc_ts_remove,
	.shutdown       = sirfsoc_ts_shutdown,
};

module_platform_driver(tsc_sirfsoc_driver);

MODULE_AUTHOR("Sober Song <Zhiwu.Song@csr.com>");
MODULE_DESCRIPTION("SiRF SoC On-chip Touch screen driver");
MODULE_LICENSE("GPLv2");
