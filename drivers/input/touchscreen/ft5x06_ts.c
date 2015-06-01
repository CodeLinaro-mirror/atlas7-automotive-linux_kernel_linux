/*
 * drivers/input/touchscreen/ft5x0x_ts.c
 *
 * FocalTech ft5x0x TouchScreen driver.
 *
 * Copyright (c) 2010  Focal tech Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * VERSION	DATE		AUTHOR	Note
 *	1.0	2010-01-05	WenFS	only support mulititouch
 *	2.0	2011-09-05	Duxx	Add touch key
 *	3.0	2011-09-09	Luowj
 *
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>

#include <linux/syscalls.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/of_gpio.h>

#define FT5X0X_NAME     "ft5x06"

#define SCREEN_MAX_X    1024
#define SCREEN_MAX_Y    600
#define PRESS_MAX       50

#define FT5X0X_REG_FIRMID	0xa6
/* Only the special version firmware not need reverse xy */
#define FT5X0X_SPECIAL_FW_VER	0x15

/*touch key, HOME, SEARCH, RETURN etc*/
#define CFG_SUPPORT_TOUCH_KEY   0
#define CFG_MAX_TOUCH_POINTS    5
#define CFG_NUMOFKEYS   4
#define CFG_POINT_READ_BUF      (3 + 6 * (CFG_MAX_TOUCH_POINTS))

#define KEY_PRESS       1
#define KEY_RELEASE     0

static struct i2c_client *this_client;

struct ts_event {
	u16 au16_x[CFG_MAX_TOUCH_POINTS];
	u16 au16_y[CFG_MAX_TOUCH_POINTS];
	/*touch event:  0 -- down; 1-- contact; 2 -- contact*/
	u8 au8_touch_event[CFG_MAX_TOUCH_POINTS];
	u8 au8_finger_id[CFG_MAX_TOUCH_POINTS];
	u16 pressure;
	u8  touch_point;
};


struct ft5x0x_ts_data {
	struct input_dev	*input_dev;
	struct ts_event		event;
	unsigned int touch_pin;
	bool is_reverse_xy;
};

#if CFG_SUPPORT_TOUCH_KEY
int tsp_keycodes[CFG_NUMOFKEYS] = {

	KEY_MENU,
	KEY_HOME,
	KEY_BACK,
	KEY_SEARCH
};

char *tsp_keyname[CFG_NUMOFKEYS] = {
	"Menu",
	"Home",
	"Back",
	"Search"
};

static bool tsp_keystatus[CFG_NUMOFKEYS];
#endif

enum {
	ERR_OK,
	ERR_MODE,
	ERR_READID,
	ERR_ERASE,
	ERR_STATUS,
	ERR_ECC,
	ERR_DL_ERASE_FAIL,
	ERR_DL_PROGRAM_FAIL,
	ERR_DL_VERIFY_FAIL
};

/* read register of ft5x0x */
static int ft5x0x_ts_read_reg(u8 addr, u8 *pdata)
{
	int ret;
	u8 buf[2];
	struct i2c_msg msgs[2];

	/*register address*/
	buf[0] = addr;

	msgs[0].addr = this_client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = buf;
	msgs[1].addr = this_client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].buf = buf;

	ret = i2c_transfer(this_client->adapter, msgs, 2);
	if (ret < 0)
		dev_err(&this_client->dev,
			"msg %s i2c read error: %d\n", __func__, ret);

	*pdata = buf[0];
	return ret;
}

/* read touchscreen controler firmware version */
static u8 ft5x0x_ts_read_fw_ver(void)
{
	u8 ver;
	ft5x0x_ts_read_reg(FT5X0X_REG_FIRMID, &ver);
	return ver;
}

static void ft5x0x_ts_release(void)
{
	struct ft5x0x_ts_data *data = i2c_get_clientdata(this_client);
	input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 0);
	input_report_key(data->input_dev, BTN_TOUCH, 0);
	input_mt_sync(data->input_dev);
	input_sync(data->input_dev);
}

static int ft5x0x_i2c_rxdata(char *rxdata, int length)
{
	int ret;

	struct i2c_msg msgs[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= rxdata,
		},
		{
			.addr	= this_client->addr,
			.flags	= I2C_M_RD,
			.len	= length,
			.buf	= rxdata,
		},
	};

	ret = i2c_transfer(this_client->adapter, msgs, 2);
	if (ret < 0)
		dev_err(&this_client->dev,
			"msg %s i2c read error: %d\n", __func__, ret);

	return ret;
}

/*read touch point information*/
static int ft5x0x_read_data(void)
{
	struct ft5x0x_ts_data *data = i2c_get_clientdata(this_client);
	struct ts_event *event = &data->event;
	u8 buf[CFG_POINT_READ_BUF] = {0};
	int ret = -1;
	int i;

	ret = ft5x0x_i2c_rxdata(buf, CFG_POINT_READ_BUF);
	if (ret < 0) {
		dev_err(&this_client->dev,
			"%s read_data i2c_rxdata failed: %d\n",
			__func__, ret);
		return ret;
	}
	memset(event, 0, sizeof(struct ts_event));
	event->touch_point = buf[2] & 0x07;

	if (event->touch_point > CFG_MAX_TOUCH_POINTS)
		event->touch_point = CFG_MAX_TOUCH_POINTS;

	for (i = 0; i < event->touch_point; i++) {
		if (data->is_reverse_xy) {
			event->au16_y[i] = (s16)(buf[3 + 6 * i] & 0x0F) << 8 |
							(s16)buf[4 + 6 * i];
			event->au16_x[i] = (s16)(buf[5 + 6 * i] & 0x0F) << 8 |
							(s16)buf[6 + 6 * i];
		} else {
			event->au16_x[i] = (s16)(buf[3 + 6 * i] & 0x0F) << 8 |
							(s16)buf[4 + 6 * i];
			event->au16_y[i] = (s16)(buf[5 + 6 * i] & 0x0F) << 8 |
							(s16)buf[6 + 6 * i];
		}
		event->au8_touch_event[i] = buf[0x3 + 6*i] >> 6;
		event->au8_finger_id[i] = (buf[5 + 6 * i]) >> 4;
	}

	event->pressure = PRESS_MAX;

	return 0;
}

#if CFG_SUPPORT_TOUCH_KEY
int ft5x0x_touch_key_process(struct input_dev *dev,
				int x, int y, int touch_event)
{
	int i;
	int key_id;

	if  (y < 517 && y > 497)
		key_id = 1;
	else if (y < 367 && y > 347)
		key_id = 0;
	else if (y < 217 && y > 197)
		key_id = 2;
	else if (y < 67 && y > 47)
		key_id = 3;
	else
		key_id = 0xf;

	for (i = 0; i < CFG_NUMOFKEYS; i++) {
		if (tsp_keystatus[i]) {
			input_report_key(dev, tsp_keycodes[i], 0);
			dev_dbg(&this_client->dev,
				"[FTS] %s key is release. Keycode : %d\n",
				tsp_keyname[i], tsp_keycodes[i]);
			tsp_keystatus[i] = KEY_RELEASE;
		} else if (key_id == i) {
			if (touch_event == 0) {
				input_report_key(dev, tsp_keycodes[i], 1);
				dev_dbg(&this_client->dev,
					"[FTS] %s key is pressed. Keycode : %d\n",
					tsp_keyname[i], tsp_keycodes[i]);
				tsp_keystatus[i] = KEY_PRESS;
			}
		}
	}
	return 0;
}
#endif

static void ft5x0x_report_value(void)
{
	struct ft5x0x_ts_data *data = i2c_get_clientdata(this_client);
	struct ts_event *event = &data->event;
	int i;

	for (i = 0; i < event->touch_point; i++) {
		if (event->au16_x[i] < SCREEN_MAX_X &&
				event->au16_y[i] < SCREEN_MAX_Y) {
			/*LCD view area*/
			input_report_abs(data->input_dev,
					ABS_MT_POSITION_X, event->au16_x[i]);
			input_report_abs(data->input_dev,
					ABS_MT_POSITION_Y, event->au16_y[i]);
			input_report_abs(data->input_dev,
					ABS_MT_WIDTH_MAJOR, 30);
			input_report_abs(data->input_dev,
				ABS_MT_TRACKING_ID, event->au8_finger_id[i]);
			if (event->au8_touch_event[i] == 0 ||
				event->au8_touch_event[i] == 2) {
				input_report_abs(data->input_dev,
					ABS_MT_TOUCH_MAJOR, event->pressure);
				input_report_key(data->input_dev, BTN_TOUCH, 1);
			} else {
				input_report_abs(data->input_dev,
					ABS_MT_TOUCH_MAJOR, 0);
				input_report_key(data->input_dev, BTN_TOUCH, 0);
			}
		} else {
		/*maybe the touch key area*/
#if CFG_SUPPORT_TOUCH_KEY
			if (event->au16_x[i] >= SCREEN_MAX_X) {
				ft5x0x_touch_key_process(data->input_dev,
					event->au16_x[i], event->au16_y[i],
					event->au8_touch_event[i]);
			}
#endif
		}
		input_mt_sync(data->input_dev);
	}
	input_sync(data->input_dev);

	if (event->touch_point == 0) {
		ft5x0x_ts_release();
		return;
	}
} /*end ft5x0x_report_value*/

static irqreturn_t ft5x0x_ts_interrupt(int irq, void *dev_id)
{
	int ret = -1;
	ret = ft5x0x_read_data();
	if (ret == 0)
		ft5x0x_report_value();

	return IRQ_HANDLED;
}

#ifdef CONFIG_PM_SLEEP
static int ft5x0x_ts_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	if (device_may_wakeup(&client->dev))
		enable_irq_wake(client->irq);

	return 0;
}

static int ft5x0x_ts_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	if (device_may_wakeup(&client->dev))
		disable_irq_wake(client->irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(ft5x0x_dev_pm_ops,
			ft5x0x_ts_suspend, ft5x0x_ts_resume);
#endif

static int
ft5x0x_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct ft5x0x_ts_data *ft5x0x_ts;
	struct input_dev *input_dev;
	u8 fw_ver = 0;
	int err = 0;
	u8 tmp = 0;
#if CFG_SUPPORT_TOUCH_KEY
	int i;
#endif

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}

	err = i2c_master_send(client, &tmp, 1);
	if (err != 1)
		return -ENODEV;

	ft5x0x_ts = kzalloc(sizeof(struct ft5x0x_ts_data), GFP_KERNEL);
	if (!ft5x0x_ts)	{
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}

	this_client = client;
	i2c_set_clientdata(client, ft5x0x_ts);

	err = devm_request_threaded_irq(&this_client->dev,
		this_client->irq,
		NULL, ft5x0x_ts_interrupt,
		IRQF_ONESHOT,
		"ft5x0x_ts", ft5x0x_ts);
	if (err) {
		dev_err(&client->dev, "\nFailed to register interrupt\n");
		goto exit_irq_request_failed;
	}

	/* use firmware version to check if need reverse xy */
	ft5x0x_ts->is_reverse_xy = true;
	fw_ver = ft5x0x_ts_read_fw_ver();
	if (fw_ver == FT5X0X_SPECIAL_FW_VER)
		ft5x0x_ts->is_reverse_xy = false;

	input_dev = input_allocate_device();
	if (!input_dev) {
		err = -ENOMEM;
		dev_err(&client->dev, "failed to allocate input device\n");
		goto exit_input_dev_alloc_failed;
	}

	ft5x0x_ts->input_dev = input_dev;

	set_bit(ABS_MT_POSITION_X, input_dev->absbit);
	set_bit(ABS_MT_POSITION_Y, input_dev->absbit);
	set_bit(ABS_MT_TOUCH_MAJOR, input_dev->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, input_dev->absbit);
	set_bit(BTN_TOUCH, input_dev->keybit);
	set_bit(INPUT_PROP_DIRECT, input_dev->propbit);

	input_set_abs_params(input_dev,
			ABS_MT_POSITION_X, 0, SCREEN_MAX_X, 5, 0);
	input_set_abs_params(input_dev,
			ABS_MT_POSITION_Y, 0, SCREEN_MAX_Y, 5, 0);
	input_set_abs_params(input_dev,
			ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);
	input_set_abs_params(input_dev,
			ABS_MT_WIDTH_MAJOR, 0, 30, 0, 0);
	input_set_abs_params(input_dev,
			ABS_MT_TRACKING_ID, 0, 5, 0, 0);

	set_bit(EV_KEY, input_dev->evbit);
	set_bit(EV_ABS, input_dev->evbit);

#if CFG_SUPPORT_TOUCH_KEY
	/*setup key code area*/
	set_bit(EV_SYN, input_dev->evbit);
	input_dev->keycode = tsp_keycodes;
	for (i = 0; i < CFG_NUMOFKEYS; i++) {
		input_set_capability(input_dev, EV_KEY,
				((int *)input_dev->keycode)[i]);
		tsp_keystatus[i] = KEY_RELEASE;
	}
#endif

	input_dev->name	= FT5X0X_NAME;
	err = input_register_device(input_dev);
	if (err) {
		dev_err(&client->dev,
		"ft5x0x_ts_probe: failed to register input device: %s\n",
		dev_name(&client->dev));
		goto exit_input_register_device_failed;
	}

	dev_dbg(&this_client->dev, "[FTS] ==probe over =\n");
	return 0;

exit_input_register_device_failed:
	input_free_device(input_dev);
exit_input_dev_alloc_failed:
exit_irq_request_failed:
	dev_err(&this_client->dev, "==singlethread error =\n");
	i2c_set_clientdata(client, NULL);
	kfree(ft5x0x_ts);
exit_alloc_data_failed:
exit_check_functionality_failed:
	return err;
}

static int ft5x0x_ts_remove(struct i2c_client *client)
{
	struct ft5x0x_ts_data *ft5x0x_ts;
	dev_dbg(&this_client->dev, "==ft5x0x_ts_remove=\n");
	ft5x0x_ts = i2c_get_clientdata(client);
	input_unregister_device(ft5x0x_ts->input_dev);
	kfree(ft5x0x_ts);
	i2c_set_clientdata(client, NULL);
	return 0;
}

static const struct i2c_device_id ft5x0x_ts_id[] = {
	{FT5X0X_NAME, 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, ft5x0x_ts_id);

static struct i2c_driver ft5x0x_ts_driver = {
	.probe		= ft5x0x_ts_probe,
	.remove		= ft5x0x_ts_remove,
	.id_table	= ft5x0x_ts_id,
	.driver	= {
		.name	= FT5X0X_NAME,
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM_SLEEP
		.pm	= &ft5x0x_dev_pm_ops,
#endif
	},
};

module_i2c_driver(ft5x0x_ts_driver);

MODULE_AUTHOR("<wenfs@Focaltech-systems.com>");
MODULE_DESCRIPTION("FocalTech ft5x0x TouchScreen driver");
MODULE_LICENSE("GPL");
