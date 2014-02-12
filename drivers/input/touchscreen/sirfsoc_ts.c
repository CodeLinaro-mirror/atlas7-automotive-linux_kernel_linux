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
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/reset.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>
#include <linux/input/sirfsoc_adc.h>

#include "sirfsoc_ts_linear.h"

#define DRIVER_NAME "sirfsoc_tsc"

#define PWR_WAKEEN_TSC_SHIFT 23
#define PWR_WAKEEN_TS_SHIFT 5
#define SIRFSOC_PWRC_TRIGGER_EN 0x8
#define SIRFSOC_PWRC_BASE 0x3000

enum sirfsoc_ts_filter {
	SIRFSOC_TS_FILTER_OK,
	SIRFSOC_TS_FILTER_REPEAT,
	SIRFSOC_TS_FILTER_IGNORE,
};

struct sirfsoc_ts {
	int				x, y;
	char				phys[32];
	int				read_cnt;
	int				read_rep;
	int				last_read;
	/*last_x, last_y store the last valid pos read from adc */
	int				last_x, last_y;
	/*reported_x, reported_y store the last reported pos*/
	int				reported_x, reported_y;
	int				press_hold_cnt;

	int				debounce_max;
	int				debounce_tol;
	int				debounce_rep;
	struct delayed_work		report_work;

	struct input_dev		*input;
};

static int sirfsoc_ts_get_pendown(struct sirfsoc_ts *ts)
{
	return sirfsoc_adc_read_reg(ADC_COORD) & PEN_DOWN;
}

static int sirfsoc_ts_debounce_filter(void *ads, int val)
{
	struct sirfsoc_ts *ts = ads;

	if (!ts->read_cnt || (abs(ts->last_read - val) > ts->debounce_tol)) {
		/* Start over collecting consistent readings. */
		ts->read_rep = 0;
		/*
		 * Repeat it, if this was the first read or the read
		 * wasn't consistent enough.
		 */
		if (ts->read_cnt < ts->debounce_max) {
			ts->last_read = val;
			ts->read_cnt++;
			return SIRFSOC_TS_FILTER_REPEAT;
		} else {
			/*
			 * Maximum number of debouncing reached and still
			 * not enough number of consistent readings. Abort
			 * the whole sample, repeat it in the next sampling
			 * period.
			 */
			ts->read_cnt = 0;
			return SIRFSOC_TS_FILTER_IGNORE;
		}
	} else {
		if (++ts->read_rep > ts->debounce_rep) {
			/*
			 * Got a good reading for this coordinate,
			 * go for the next one.
			 */
			ts->read_cnt = 0;
			ts->read_rep = 0;
			return SIRFSOC_TS_FILTER_OK;
		} else {
			/* Read more values that are consistent. */
			ts->read_cnt++;
			return  SIRFSOC_TS_FILTER_REPEAT;
		}
	}
}

/*Get the touched x position form adc register*/
static int sirfsoc_ts_get_position_x(struct sirfsoc_ts *ts)
{
	int action, x;
	u32 reg_control1, coord;
	int cnt_x = 0;
	int sum_x = 0;

	reg_control1 = ADC_POLL | ADC_SEL(1) | ADC_DEL_SET(6)
			| ADC_FREQ_6K | ADC_TP_TIME(0) | ADC_SGAIN(0)
			| ADC_EXTCM(0) | ADC_RBAT_DISABLE
			| ADC_MORE_CTL1;

	while (true) {
		sirfsoc_adc_write_reg(reg_control1, ADC_CONTROL1);
		if (sirfsoc_adc_sync_reg() < 0) {
			ts->read_cnt = 0;
			ts->read_rep = 0;
			return -EBUSY;
		}

		coord = sirfsoc_adc_read_reg(ADC_COORD);
		x = coord & DATA_XMASK;

		cnt_x++;
		sum_x += x;
		ts->last_read = ts->last_x;
		action = sirfsoc_ts_debounce_filter(ts, x);
		ts->last_x = ts->last_read;

		switch (action) {
		case SIRFSOC_TS_FILTER_REPEAT:
			break;
		case SIRFSOC_TS_FILTER_IGNORE:
			return -EAGAIN;
			break;
		case SIRFSOC_TS_FILTER_OK:
			return sum_x / cnt_x;
			break;
		default:
			BUG();
		}
	}
}

/*Get the touched x position form adc register*/
static int sirfsoc_ts_get_position_y(struct sirfsoc_ts *ts)
{
	int action, y;
	u32 reg_control1, coord;
	int cnt_y = 0;
	int sum_y = 0;

	reg_control1 = ADC_POLL | ADC_SEL(2) | ADC_DEL_SET(6)
			| ADC_FREQ_6K | ADC_TP_TIME(0) | ADC_SGAIN(0)
			| ADC_EXTCM(0) | ADC_RBAT_DISABLE
			| ADC_MORE_CTL1;

	while (true) {
		sirfsoc_adc_write_reg(reg_control1, ADC_CONTROL1);
		if (sirfsoc_adc_sync_reg() < 0) {
			ts->read_cnt = 0;
			ts->read_rep = 0;
			return -EBUSY;
		}

		coord = sirfsoc_adc_read_reg(ADC_COORD);
		y = (coord & DATA_YMASK) >> DATA_SHIFT_BITS;

		cnt_y++;
		sum_y += y;
		ts->last_read = ts->last_y;
		action = sirfsoc_ts_debounce_filter(ts, y);
		ts->last_y = ts->last_read;

		switch (action) {
		case SIRFSOC_TS_FILTER_REPEAT:
			break;
		case SIRFSOC_TS_FILTER_IGNORE:
			return -EAGAIN;
			break;
		case SIRFSOC_TS_FILTER_OK:
			return sum_y / cnt_y;
			break;
		default:
			BUG();
		}
	}
}

static int sirfsoc_ts_read_state(struct sirfsoc_ts *ts)
{
	ts->x = sirfsoc_ts_get_position_x(ts);
	if (ts->x < 0)
		return ts->x;

	ts->y = sirfsoc_ts_get_position_y(ts);
	if (ts->y < 0)
		return ts->y;
	if (ts->press_hold_cnt == 0) {
		ts->reported_x = ts->x;
		ts->reported_y = ts->y;
	}

	return 0;
}

static void sirfsoc_ts_report_state(struct sirfsoc_ts *ts)
{
	int diff;
	input_report_abs(ts->input, ABS_PRESSURE, 1);
	input_report_key(ts->input, BTN_TOUCH, 1);

	diff = ts->debounce_tol;
	/*Make the position in the accuracy*/
	if ((ts->x < ts->reported_x + diff && ts->x > ts->reported_x - diff)
			&& (ts->y < ts->reported_y + diff
			&& ts->y > ts->reported_y - diff)) {
		ts->x = ts->reported_x;
		ts->y = ts->reported_y;
	} else {
		ts->reported_x = ts->x;
		ts->reported_y = ts->y;
	}

	ts_linear_scale(&ts->x, &ts->y);

	input_report_abs(ts->input, ABS_X, ts->x);
	input_report_abs(ts->input, ABS_Y, ts->y);

	input_mt_sync(ts->input);

	input_sync(ts->input);
}

static void sirfsoc_ts_report_work(struct work_struct *work)
{
	struct delayed_work *dw = container_of(work, struct delayed_work, work);
	struct sirfsoc_ts *ts = container_of(dw,
				struct sirfsoc_ts, report_work);
	struct input_dev *input = ts->input;

	if (sirfsoc_ts_get_pendown(ts)) {
		if (!sirfsoc_ts_read_state(ts))
			sirfsoc_ts_report_state(ts);

		ts->press_hold_cnt++;
		schedule_delayed_work(&ts->report_work, msecs_to_jiffies(10));
	} else {
		ts->press_hold_cnt = 0;
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
		dev_err(&pdev->dev, "sirfsoc ts: Cant allocate driver private data\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, ts);

	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&pdev->dev, "sirfsoc ts: Unable to allocate input device\n");
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
		dev_err(&pdev->dev, "sirfsoc ts: Unable to register input device\n");
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
	sirfsoc_adc_write_reg(sirfsoc_adc_read_reg(ADC_INTR) | PEN_INTR | DATA_INTR |
		PEN_INTR_EN | DATA_INTR_EN, ADC_INTR);


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
	/*touch is not pressed down*/
	ts->press_hold_cnt = 0;

	/*the value about touch accuracy*/
	ts->debounce_rep = 0x01;
	ts->debounce_max = 0x03;
	ts->debounce_tol = 0x30;

	input_set_abs_params(input_dev, ABS_X, 0, 0x3FFF, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, 0x3FFF, 0, 0);

	return 0;
out3:
	input_unregister_device(input_dev);
out2:
	input_free_device(input_dev);
out1:
	dev_err(&pdev->dev, "Start failed\n");
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

static const struct of_device_id tsc_sirfsoc_of_match[] = {
	{ .compatible = "sirf,prima2-tsc",},
	{ .compatible = "sirf,marco-tsc",},
	{}
};

static struct platform_driver tsc_sirfsoc_driver = {
	.driver	 = {
		.name   = DRIVER_NAME,
#ifdef CONFIG_PM
		.pm     = &sirfsoc_ts_pm_ops,
#endif
		.of_match_table = tsc_sirfsoc_of_match,
	},
	.probe	  = sirfsoc_ts_probe,
	.remove	 = sirfsoc_ts_remove,
	.shutdown       = sirfsoc_ts_shutdown,
};

module_platform_driver(tsc_sirfsoc_driver);

MODULE_AUTHOR("Sober Song <Zhiwu.Song@csr.com>");
MODULE_DESCRIPTION("SiRF SoC On-chip Touch screen driver");
MODULE_LICENSE("GPLv2");
