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

/* -- dirver configure -- */
#define CFG_SUPPORT_AUTO_UPG    0
#define CFG_SUPPORT_UPDATE_PROJECT_SETTING      0
/*touch key, HOME, SEARCH, RETURN etc*/
#define CFG_SUPPORT_TOUCH_KEY   0
#define CFG_MAX_TOUCH_POINTS    5
#define CFG_NUMOFKEYS   4
#define CFG_FTS_CTP_DRIVER_VERSION      "3.0"
#define CFG_POINT_READ_BUF      (3 + 6 * (CFG_MAX_TOUCH_POINTS))

#define FT5x0x_TX_NUM 28
#define FT5x0x_RX_NUM 16

#define FTS_NULL 0x0
#define FTS_TRUE 0x01
#define FTS_FALSE 0x0
#define FTS_PACKET_LENGTH 128
#define FTS_SETTING_BUF_LEN 128

#define KEY_PRESS       1
#define KEY_RELEASE     0

/*register address*/
#define FT5x06_REG_FW_VER 0xA6
#define I2C_CTPM_ADDRESS 0x70

enum ft5x0x_ts_regs {
	FT5X0X_REG_THGROUP		= 0x80,
	FT5X0X_REG_THPEAK		= 0x81,
	FT5X0X_REG_THCAL		= 0x82,
	FT5X0X_REG_THWATER		= 0x83,
	FT5X0X_REG_THTEMP		= 0x84,
	FT5X0X_REG_THDIFF		= 0x85,
	FT5X0X_REG_CTRL			= 0x86,
	FT5X0X_REG_TIMEENTERMONITOR	= 0x87,
	FT5X0X_REG_PERIODACTIVE		= 0x88, /* report rate */
	FT5X0X_REG_PERIODMONITOR	= 0x89,
	FT5X0X_REG_HEIGHT_B		= 0x8a,
	FT5X0X_REG_MAX_FRAME		= 0x8b,
	FT5X0X_REG_DIST_MOVE		= 0x8c,
	FT5X0X_REG_DIST_POINT		= 0x8d,
	FT5X0X_REG_FEG_FRAME		= 0x8e,
	FT5X0X_REG_SINGLE_CLICK_OFFSET	= 0x8f,
	FT5X0X_REG_DOUBLE_CLICK_TIME_MIN	= 0x90,
	FT5X0X_REG_SINGLE_CLICK_TIME	= 0x91,
	FT5X0X_REG_LEFT_RIGHT_OFFSET	= 0x92,
	FT5X0X_REG_UP_DOWN_OFFSET	= 0x93,
	FT5X0X_REG_DISTANCE_LEFT_RIGHT	= 0x94,
	FT5X0X_REG_DISTANCE_UP_DOWN	= 0x95,
	FT5X0X_REG_ZOOM_DIS_SQR		= 0x96,
	FT5X0X_REG_RADIAN_VALUE		= 0x97,
	FT5X0X_REG_MAX_X_HIGH		= 0x98,
	FT5X0X_REG_MAX_X_LOW		= 0x99,
	FT5X0X_REG_MAX_Y_HIGH		= 0x9a,
	FT5X0X_REG_MAX_Y_LOW		= 0x9b,
	FT5X0X_REG_K_X_HIGH		= 0x9c,
	FT5X0X_REG_K_X_LOW		= 0x9d,
	FT5X0X_REG_K_Y_HIGH		= 0x9e,
	FT5X0X_REG_K_Y_LOW		= 0x9f,
	FT5X0X_REG_AUTO_CLB_MODE	= 0xa0,
	FT5X0X_REG_LIB_VERSION_H	= 0xa1,
	FT5X0X_REG_LIB_VERSION_L	= 0xa2,
	FT5X0X_REG_CIPHER		= 0xa3,
	FT5X0X_REG_MODE			= 0xa4,
	FT5X0X_REG_PMODE		= 0xa5, /* Power Consume Mode*/
	FT5X0X_REG_FIRMID		= 0xa6, /* Firmware version */
	FT5X0X_REG_STATE		= 0xa7,
	FT5X0X_REG_FT5201ID		= 0xa8,
	FT5X0X_REG_ERR			= 0xa9,
	FT5X0X_REG_CLB			= 0xaa,
};

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
	struct work_struct	pen_event_work;
	struct workqueue_struct *ts_workqueue;
	/* Ensures that only one function can
	 *specify the Device Mode at a time. */
	struct mutex device_mode_mutex;
};

u16 g_rawdata[FT5x0x_TX_NUM][FT5x0x_RX_NUM];
static u8 ft5x0x_enter_factory(struct ft5x0x_ts_data *ft5x0x_ts);
static u8 ft5x0x_enter_work(struct ft5x0x_ts_data *ft5x0x_ts);

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

static int ft5x0x_i2c_txdata(char *txdata, int length)
{
	int ret;

	struct i2c_msg msg[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= length,
			.buf	= txdata,
		},
	};

	ret = i2c_transfer(this_client->adapter, msg, 1);
	if (ret < 0)
		dev_err(&this_client->dev,
			"%s i2c write error: %d\n", __func__, ret);

	return ret;
}

static int ft5x0x_write_reg(u8 addr, u8 para)
{
	u8 buf[3];
	int ret = -1;

	buf[0] = addr;
	buf[1] = para;
	ret = ft5x0x_i2c_txdata(buf, 2);
	if (ret < 0) {
		dev_err(&this_client->dev,
			"write reg failed! %#x ret: %d", buf[0], ret);
		return -1;
	}

	return 0;
}

/*read register of ft5x0x*/
static int ft5x0x_read_reg(u8 addr, u8 *pdata)
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

/*read TP firmware version*/
static unsigned char ft5x0x_read_fw_ver(void)
{
	unsigned char ver;
	ft5x0x_read_reg(FT5X0X_REG_FIRMID, &ver);
	return ver;
}

/*upgrade related*/
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

void delay_qt_ms(unsigned long  w_ms)
{
	udelay(w_ms * 1000);
}

/*
read data from ctpm by i2c interface,implemented by special user;
*/
unsigned char i2c_read_interface(unsigned char bt_ctpm_addr,
			unsigned char *pbt_buf, unsigned int dw_lenth)
{
	int ret;

	ret = i2c_master_recv(this_client, pbt_buf, dw_lenth);

	if (ret <= 0) {
		dev_err(&this_client->dev, "[FTS]i2c_read_interface error\n");
		return FTS_FALSE;
	}

	return FTS_TRUE;
}

/*
    write data to ctpm by i2c interface,implemented by special user;
*/
unsigned char i2c_write_interface(unsigned char bt_ctpm_addr,
			unsigned char *pbt_buf, unsigned int dw_lenth)
{
	int ret;
	ret = i2c_master_send(this_client, pbt_buf, dw_lenth);
	if (ret <= 0) {
		dev_err(&this_client->dev,
			"[FTS]i2c_write_interface error line = %d, ret = %d\n",
			__LINE__, ret);
		return FTS_FALSE;
	}

	return FTS_TRUE;
}

unsigned char cmd_write(unsigned char btcmd, unsigned char btPara1,
			unsigned char btPara2, unsigned char btPara3,
			unsigned char num)
{
	unsigned char write_cmd[4] = {0};

	write_cmd[0] = btcmd;
	write_cmd[1] = btPara1;
	write_cmd[2] = btPara2;
	write_cmd[3] = btPara3;
	return i2c_write_interface(I2C_CTPM_ADDRESS, write_cmd, num);
}

unsigned char byte_write(unsigned char *pbt_buf, unsigned int dw_len)
{
	return i2c_write_interface(I2C_CTPM_ADDRESS, pbt_buf, dw_len);
}

unsigned char byte_read(unsigned char *pbt_buf, unsigned char bt_len)
{
	return i2c_read_interface(I2C_CTPM_ADDRESS, pbt_buf, bt_len);
}

static unsigned char CTPM_FW[] = { };

int fts_ctpm_fw_upgrade(unsigned char *pbt_buf, unsigned int dw_lenth)
{
	unsigned char reg_val[2] = {0};
	unsigned int i = 0;

	unsigned int packet_number;
	unsigned int j;
	unsigned int temp;
	unsigned int lenght;
	unsigned char packet_buf[FTS_PACKET_LENGTH + 6];
	unsigned char auc_i2c_write_buf[10];
	unsigned char bt_ecc;
	int i_ret;

	/*********Step 1:Reset  CTPM *****/
	/*write 0xaa to register 0xfc*/
	ft5x0x_write_reg(0xfc, 0xaa);
	delay_qt_ms(50);
	/*write 0x55 to register 0xfc*/
	ft5x0x_write_reg(0xfc, 0x55);
	dev_info(&this_client->dev, "[FTS] Step 1: Reset CTPM test\n");

	delay_qt_ms(30);


	/*********Step 2:Enter upgrade mode *****/
	auc_i2c_write_buf[0] = 0x55;
	auc_i2c_write_buf[1] = 0xaa;
	do {
		i++;
		i_ret = ft5x0x_i2c_txdata(auc_i2c_write_buf, 2);
		delay_qt_ms(5);
	} while (i_ret <= 0 && i < 5);

	/*********Step 3:check READ-ID***********************/
	cmd_write(0x90, 0x00, 0x00, 0x00, 4);
	byte_read(reg_val, 2);
	if (reg_val[0] == 0x79 && reg_val[1] == 0x3)
		dev_dbg(&this_client->dev,
			"[FTS] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",
			reg_val[0], reg_val[1]);
	else
		return ERR_READID;

	cmd_write(0xcd, 0x0, 0x00, 0x00, 1);
	byte_read(reg_val, 1);
	dev_dbg(&this_client->dev,
		"[FTS] bootloader version = 0x%x\n", reg_val[0]);

	/*****Step 4:erase app and panel paramenter area******/
	cmd_write(0x61, 0x00, 0x00, 0x00, 1); /*erase app area*/
	delay_qt_ms(1500);
	cmd_write(0x63, 0x00, 0x00, 0x00, 1);  /*erase panel parameter area*/
	delay_qt_ms(100);
	dev_dbg(&this_client->dev, "[FTS] Step 4: erase.\n");

	/*********Step 5:write firmware(FW) to ctpm flash*********/
	bt_ecc = 0;
	dev_dbg(&this_client->dev, "[FTS] Step 5: start upgrade.\n");
	dw_lenth = dw_lenth - 8;
	packet_number = (dw_lenth) / FTS_PACKET_LENGTH;
	packet_buf[0] = 0xbf;
	packet_buf[1] = 0x00;
	for (j = 0; j < packet_number; j++) {
		temp = j * FTS_PACKET_LENGTH;
		packet_buf[2] = (unsigned char)(temp >> 8);
		packet_buf[3] = (unsigned char)temp;
		lenght = FTS_PACKET_LENGTH;
		packet_buf[4] = (unsigned char)(lenght >> 8);
		packet_buf[5] = (unsigned char)lenght;

		for (i = 0; i < FTS_PACKET_LENGTH; i++) {
			packet_buf[6 + i] = pbt_buf[j * FTS_PACKET_LENGTH + i];
			bt_ecc ^= packet_buf[6 + i];
		}

		byte_write(&packet_buf[0], FTS_PACKET_LENGTH + 6);
		delay_qt_ms(FTS_PACKET_LENGTH / 6 + 1);
		if ((j * FTS_PACKET_LENGTH % 1024) == 0)
			dev_info(&this_client->dev,
				"[FTS] upgrade the 0x%x th byte.\n",
				((unsigned int)j) * FTS_PACKET_LENGTH);
	}

	if ((dw_lenth) % FTS_PACKET_LENGTH > 0) {
		temp = packet_number * FTS_PACKET_LENGTH;
		packet_buf[2] = (unsigned char)(temp >> 8);
		packet_buf[3] = (unsigned char)temp;

		temp = (dw_lenth) % FTS_PACKET_LENGTH;
		packet_buf[4] = (unsigned char)(temp >> 8);
		packet_buf[5] = (unsigned char)temp;

		for (i = 0; i < temp; i++) {
			packet_buf[6 + i] = pbt_buf[packet_number *
						FTS_PACKET_LENGTH + i];
			bt_ecc ^= packet_buf[6 + i];
		}

		byte_write(&packet_buf[0], temp + 6);
		delay_qt_ms(20);
	}

	/*send the last six byte*/
	for (i = 0; i < 6; i++) {
		temp = 0x6ffa + i;
		packet_buf[2] = (unsigned char)(temp >> 8);
		packet_buf[3] = (unsigned char)temp;
		temp = 1;
		packet_buf[4] = (unsigned char)(temp >> 8);
		packet_buf[5] = (unsigned char)temp;
		packet_buf[6] = pbt_buf[dw_lenth + i];
		bt_ecc ^= packet_buf[6];

		byte_write(&packet_buf[0], 7);
		delay_qt_ms(20);
	}

	/*send the opration head*/
	cmd_write(0xcc, 0x00, 0x00, 0x00, 1);
	byte_read(reg_val, 1);
	dev_dbg(&this_client->dev,
		"[FTS] Step 6: ecc read 0x%x, new firmware 0x%x.\n",
		reg_val[0], bt_ecc);
	if (reg_val[0] != bt_ecc)
		return ERR_ECC;

	/*********Step 7: reset the new FW***********************/
	cmd_write(0x07, 0x00, 0x00, 0x00, 1);

	/*make sure CTP startup normally*/
	msleep(300);

	return ERR_OK;
}

static int fts_get_rawdata(void)
{
	int retval  = 0;
	int i       = 0;
	u8  devmode = 0x00;
	u8  rownum  = 0x00;

	u8 read_buffer[FT5x0x_RX_NUM * 2];
	struct ft5x0x_ts_data *ft5x0x_ts = i2c_get_clientdata(this_client);
	struct i2c_msg msgs[] = {
		{
			.addr	= this_client->addr,
			.flags	= I2C_M_RD,
			.len	= FT5x0x_RX_NUM * 2,
			.buf	= read_buffer,
		},
	};
	if (ft5x0x_enter_factory(ft5x0x_ts) < 0) {
		dev_err(&this_client->dev,
			"%s ERROR: could not enter factory mode", __func__);
		retval = -1;
		goto error_return;
	}
	/*scan*/
	if (ft5x0x_read_reg(0x00, &devmode) < 0) {
		dev_err(&this_client->dev,
			"%s %d ERROR: could not read register 0x00",
			__func__, __LINE__);
		retval = -1;
		goto error_return;
	}
	devmode |= 0x80;
	if (ft5x0x_write_reg(0x00, devmode) < 0) {
		dev_err(&this_client->dev,
			"%s %d ERROR: could not read register 0x00",
			__func__, __LINE__);
		retval = -1;
		goto error_return;
	}
	msleep(20);
	if (ft5x0x_read_reg(0x00, &devmode) < 0) {
		dev_err(&this_client->dev,
			"%s %d ERROR: could not read register 0x00",
			__func__, __LINE__);
		retval = -1;
		goto error_return;
	}
	if (0x00 != (devmode&0x80)) {
		dev_err(&this_client->dev,
			"%s %d ERROR: could not scan",
			__func__, __LINE__);
		retval = -1;
		goto error_return;
	}
	for (rownum = 0; rownum < FT5x0x_TX_NUM; rownum++) {
		memset(read_buffer, 0x00, (FT5x0x_RX_NUM * 2));
		if (ft5x0x_write_reg(0x00, &rownum) < 0) {
			dev_err(&this_client->dev,
				"%s ERROR: could not write rownum",
				__func__);
			retval = -1;
			goto error_return;
		}
		delay_qt_ms(1);

		retval = i2c_transfer(this_client->adapter, msgs, 1);
		if (retval < 0) {
			dev_err(&this_client->dev,
				"%s ERROR: Could not read row %u raw data",
				__func__, rownum);
			retval = -1;
			goto error_return;
		}
		for (i = 0; i < FT5x0x_RX_NUM; i++) {
			g_rawdata[rownum][i] = (read_buffer[i << 1] << 8)
						+ read_buffer[(i << 1) + 1];
		}
	}
error_return:
	if (ft5x0x_enter_work(ft5x0x_ts) < 0) {
		dev_err(&this_client->dev,
			"%s ERROR: could not enter work mode ",
			__func__);
		retval = -1;
	}
	return retval;
}

int fts_ctpm_auto_clb(void)
{
	unsigned char uc_temp;
	unsigned char i;

	dev_dbg(&this_client->dev, "[FTS] start auto CLB.\n");
	msleep(200);
	ft5x0x_write_reg(0, 0x40);
	/*make sure already enter factory mode*/
	delay_qt_ms(100);
	/*write command to start calibration*/
	ft5x0x_write_reg(2, 0x4);
	delay_qt_ms(300);
	for (i = 0; i < 100; i++) {
		ft5x0x_read_reg(0, &uc_temp);
		if (((uc_temp & 0x70) >> 4) == 0x0)
			break;
		delay_qt_ms(200);
		dev_dbg(&this_client->dev, "[FTS] waiting calibration %d\n", i);
	}
	dev_dbg(&this_client->dev, "[FTS] calibration OK.\n");

	msleep(300);
	/*goto factory mode*/
	ft5x0x_write_reg(0, 0x40);
	/*make sure already enter factory mode*/
	delay_qt_ms(100);
	/*store CLB result*/
	ft5x0x_write_reg(2, 0x5);
	delay_qt_ms(300);
	/*return to normal mode*/
	ft5x0x_write_reg(0, 0x0);
	msleep(300);
	dev_dbg(&this_client->dev, "[FTS] store CLB result OK.\n");
	return 0;
}

int fts_ctpm_fw_upgrade_with_i_file(void)
{
	unsigned char *pbt_buf = FTS_NULL;
	int i_ret;

	pbt_buf = CTPM_FW;
	/*call the upgrade function*/
	i_ret = fts_ctpm_fw_upgrade(pbt_buf, sizeof(CTPM_FW));
	if (i_ret != 0)
		dev_err(&this_client->dev,
			"[FTS] upgrade failed i_ret = %d.\n", i_ret);
	else {
		dev_dbg(&this_client->dev, "[FTS] upgrade successfully.\n");
		fts_ctpm_auto_clb();
	}

	return i_ret;
}

unsigned char fts_ctpm_get_i_file_ver(void)
{
	unsigned int ui_sz;
	ui_sz = sizeof(CTPM_FW);
	if (ui_sz > 2)
		return CTPM_FW[ui_sz - 2];
	else
		/*TBD, error handling*/
		return 0xff;
}

/*update project setting*/
int fts_ctpm_update_project_setting(void)
{
	unsigned char uc_i2c_addr;	/*I2C slave address (8 bit address)*/
	unsigned char uc_io_voltage;	/*IO Voltage 0---3.3v;	1----1.8v*/
	unsigned char uc_panel_factory_id;	/*TP panel factory ID*/

	unsigned char buf[FTS_SETTING_BUF_LEN];
	unsigned char reg_val[2] = {0};
	unsigned char auc_i2c_write_buf[10];
	unsigned char packet_buf[FTS_SETTING_BUF_LEN + 6];
	unsigned int i = 0;
	int i_ret;

	uc_i2c_addr = 0x70;
	uc_io_voltage = 0x0;
	uc_panel_factory_id = 0x5a;

	/*********Step 1:Reset  CTPM *****/
	/*write 0xaa to register 0xfc*/
	ft5x0x_write_reg(0xfc, 0xaa);
	delay_qt_ms(50);
	/*write 0x55 to register 0xfc*/
	ft5x0x_write_reg(0xfc, 0x55);
	dev_dbg(&this_client->dev, "[FTS] Step 1: Reset CTPM test\n");

	delay_qt_ms(30);

	/*********Step 2:Enter upgrade mode *****/
	auc_i2c_write_buf[0] = 0x55;
	auc_i2c_write_buf[1] = 0xaa;
	do {
		i++;
		i_ret = ft5x0x_i2c_txdata(auc_i2c_write_buf, 2);
		delay_qt_ms(5);
	} while (i_ret <= 0 && i < 5);

	/*********Step 3:check READ-ID***********************/
	cmd_write(0x90, 0x00, 0x00, 0x00, 4);
	byte_read(reg_val, 2);
	if (reg_val[0] == 0x79 && reg_val[1] == 0x3)
		dev_dbg(&this_client->dev,
			"[FTS] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",
			reg_val[0], reg_val[1]);
	else
		return ERR_READID;

	cmd_write(0xcd, 0x0, 0x00, 0x00, 1);
	byte_read(reg_val, 1);
	dev_dbg(&this_client->dev, "bootloader version = 0x%x\n", reg_val[0]);


	/* --------- read current project setting  ---------- */
	/*set read start address*/
	buf[0] = 0x3;
	buf[1] = 0x0;
	buf[2] = 0x78;
	buf[3] = 0x0;
	byte_write(buf, 4);
	byte_read(buf, FTS_SETTING_BUF_LEN);

	for (i = 0; i < FTS_SETTING_BUF_LEN; i++) {
		if (i % 16 == 0)
			dev_dbg(&this_client->dev, "\n");
		dev_dbg(&this_client->dev, "0x%x, ", buf[i]);
	}
	dev_dbg(&this_client->dev, "\n");

	/*--------- Step 4:erase project setting --------------*/
	cmd_write(0x62, 0x00, 0x00, 0x00, 1);
	delay_qt_ms(100);

	/*----------  Set new settings ---------------*/
	buf[0] = uc_i2c_addr;
	buf[1] = ~uc_i2c_addr;
	buf[2] = uc_io_voltage;
	buf[3] = ~uc_io_voltage;
	buf[4] = uc_panel_factory_id;
	buf[5] = ~uc_panel_factory_id;
	packet_buf[0] = 0xbf;
	packet_buf[1] = 0x00;
	packet_buf[2] = 0x78;
	packet_buf[3] = 0x0;
	packet_buf[4] = 0;
	packet_buf[5] = FTS_SETTING_BUF_LEN;
	for (i = 0; i < FTS_SETTING_BUF_LEN; i++) {
		packet_buf[6 + i] = buf[i];
		if (i % 16 == 0)
			dev_dbg(&this_client->dev, "\n");
		dev_dbg(&this_client->dev, "0x%x, ", buf[i]);
	}
	dev_dbg(&this_client->dev, "\n");
	byte_write(&packet_buf[0], FTS_SETTING_BUF_LEN + 6);
	delay_qt_ms(100);

	/********* reset the new FW***********************/
	cmd_write(0x07, 0x00, 0x00, 0x00, 1);

	msleep(200);

	return 0;
}

#if CFG_SUPPORT_AUTO_UPG
int fts_ctpm_auto_upg(void)
{
	unsigned char uc_host_fm_ver;
	unsigned char uc_tp_fm_ver;
	int	   i_ret;

	uc_tp_fm_ver = ft5x0x_read_fw_ver();
	uc_host_fm_ver = fts_ctpm_get_i_file_ver();
	if (uc_tp_fm_ver == 0xa6 || uc_tp_fm_ver < uc_host_fm_ver) {
		msleep(100);
		dev_dbg(&this_client->dev,
			"[FTS] uc_tp_fm_ver = 0x%x, uc_host_fm_ver = 0x%x\n",
			uc_tp_fm_ver, uc_host_fm_ver);
		i_ret = fts_ctpm_fw_upgrade_with_i_file();
		if (i_ret == 0) {
			msleep(300);
			uc_host_fm_ver = fts_ctpm_get_i_file_ver();
			dev_dbg(&this_client->dev,
				"[FTS] upgrade to new version 0x%x\n",
				uc_host_fm_ver);
		} else
			dev_err(&this_client->dev,
				"[FTS] upgrade failed ret=%d.\n", i_ret);
	}
	return 0;
}
#endif

static void ft5x0x_ts_release(void)
{
	struct ft5x0x_ts_data *data = i2c_get_clientdata(this_client);
	input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 0);
	input_report_key(data->input_dev, BTN_TOUCH, 0);
	input_mt_sync(data->input_dev);
	input_sync(data->input_dev);
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
		event->au16_x[i] = (s16)(buf[3 + 6 * i] & 0x0F)<<8 |
							(s16)buf[4 + 6 * i];
		event->au16_y[i] = (s16)(buf[5 + 6 * i] & 0x0F) << 8 |
							(s16)buf[6 + 6 * i];
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
		if (event->au16_y[i] < SCREEN_MAX_X &&
				event->au16_x[i] < SCREEN_MAX_Y) {
			/*LCD view area*/
			input_report_abs(data->input_dev,
					ABS_MT_POSITION_X, event->au16_y[i]);
			input_report_abs(data->input_dev,
					ABS_MT_POSITION_Y, event->au16_x[i]);
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

static void ft5x0x_ts_pen_irq_work(struct work_struct *work)
{
	int ret = -1;

	ret = ft5x0x_read_data();
	if (ret == 0)
		ft5x0x_report_value();

	enable_irq(this_client->irq);
}

static irqreturn_t ft5x0x_ts_interrupt(int irq, void *dev_id)
{
	struct ft5x0x_ts_data *ft5x0x_ts = dev_id;

	disable_irq_nosync(this_client->irq);

	if (!work_pending(&ft5x0x_ts->pen_event_work))
		queue_work(ft5x0x_ts->ts_workqueue, &ft5x0x_ts->pen_event_work);

	return IRQ_HANDLED;
}
#ifdef CONFIG_PM
/*static void ft5x0x_ts_suspend(struct early_suspend *handler)
{
	struct ft5x0x_ts_data *ts;
	ts =  container_of(handler, struct ft5x0x_ts_data, early_suspend);

	disable_irq(this_client->irq);
	disable_irq(IRQ_EINT(6));
	cancel_work_sync(&ts->pen_event_work);
	flush_workqueue(ts->ts_workqueue);
	/==set mode ==/
	ft5x0x_set_reg(FT5X0X_REG_PMODE, PMODE_HIBERNATE);
}*/
/*
static void ft5x0x_ts_resume(struct early_suspend *handler)
{
	/wake the mode/
	__gpio_as_output(GPIO_FT5X0X_WAKE);
	__gpio_clear_pin(GPIO_FT5X0X_WAKE);
	msleep(100);
	__gpio_set_pin(GPIO_FT5X0X_WAKE);
	msleep(100);
	enable_irq(this_client->irq);
	enable_irq(IRQ_EINT(6));
}*/
#endif

/* sysfs */
static u8 ft5x0x_enter_factory(struct ft5x0x_ts_data *ft5x0x_ts)
{
	u8 regval;
	flush_workqueue(ft5x0x_ts->ts_workqueue);
	disable_irq_nosync(this_client->irq);
	ft5x0x_write_reg(0, 0x40);
	delay_qt_ms(100);

	if (ft5x0x_read_reg(0x00, &regval) < 0)
		dev_err(&this_client->dev,
			"%s ERROR: could not read register\n",
			__func__);
	else {
		if ((regval & 0x70) != 0x40)
			return -1;
	}

	return 0;
}
static u8 ft5x0x_enter_work(struct ft5x0x_ts_data *ft5x0x_ts)
{
	u8 regval;
	ft5x0x_write_reg(0x00, 0x00);
	msleep(100);

	if (ft5x0x_read_reg(0x00, &regval) < 0)
		dev_err(&this_client->dev,
			"%s ERROR: could not read register\n",
			__func__);
	else {
		if ((regval & 0x70) != 0x00)
			return -1;
	}
	enable_irq(this_client->irq);
	return 0;
}

static int ft5x0x_GetFirmwareSize(char *firmware_name)
{
	struct file *pfile = NULL;
	struct inode *inode;
	unsigned long magic;
	off_t fsize = 0;
	char filepath[128];

	memset(filepath, 0, sizeof(filepath));

	sprintf(filepath, "/sdcard/%s", firmware_name);
	dev_info(&this_client->dev, "filepath=%s\n", filepath);
	if (NULL == pfile)
		pfile = filp_open(filepath, O_RDONLY, 0);

	if (IS_ERR(pfile)) {
		dev_err(&this_client->dev,
			"error occured while opening file %s.\n", filepath);
		return -1;
	}
	inode = pfile->f_dentry->d_inode;
	magic = inode->i_sb->s_magic;
	fsize = inode->i_size;
	filp_close(pfile, NULL);
	return fsize;
}
static int ft5x0x_ReadFirmware(char *firmware_name,
			unsigned char *firmware_buf)
{
	struct file *pfile = NULL;
	struct inode *inode;
	unsigned long magic;
	off_t fsize;
	char filepath[128];
	loff_t pos;
	mm_segment_t old_fs;

	memset(filepath, 0, sizeof(filepath));
	sprintf(filepath, "/sdcard/%s", firmware_name);
	dev_info(&this_client->dev, "filepath=%s\n", filepath);
	if (NULL == pfile)
		pfile = filp_open(filepath, O_RDONLY, 0);

	if (IS_ERR(pfile)) {
		dev_err(&this_client->dev,
			"error occured while opening file %s.\n", filepath);
		return -1;
	}

	inode = pfile->f_dentry->d_inode;
	magic = inode->i_sb->s_magic;
	fsize = inode->i_size;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	pos = 0;

	vfs_read(pfile, firmware_buf, fsize, &pos);

	filp_close(pfile, NULL);
	set_fs(old_fs);
	return 0;
}

int fts_ctpm_fw_upgrade_with_app_file(char *firmware_name)
{
	unsigned char *pbt_buf = FTS_NULL;
	int i_ret;
	u8 fwver;
	int fwsize = ft5x0x_GetFirmwareSize(firmware_name);
	if (fwsize <= 0) {
		dev_err(&this_client->dev,
			"%s ERROR:Get firmware size failed\n",
			__func__);
		return -1;
	}

	pbt_buf = kmalloc(fwsize + 1, GFP_ATOMIC);
	if (ft5x0x_ReadFirmware(firmware_name, pbt_buf)) {
		dev_err(&this_client->dev,
			"%s() - ERROR: request_firmware failed\n", __func__);
			kfree(pbt_buf);
			return -1;
	}

	/*call the upgrade function*/
	i_ret =  fts_ctpm_fw_upgrade(pbt_buf, fwsize);
	if (i_ret != 0)
		dev_err(&this_client->dev,
			"%s() - ERROR: [FTS] upgrade failed i_ret = %d.\n",
			__func__,  i_ret);
	else {
		dev_info(&this_client->dev, "[FTS] upgrade successfully.\n");
			if (ft5x0x_read_reg(FT5x06_REG_FW_VER, &fwver) >= 0)
				dev_info(&this_client->dev,
					"the new fw ver is 0x%02x\n", fwver);
			fts_ctpm_auto_clb();
	}
	kfree(pbt_buf);
	return i_ret;
}

static ssize_t ft5x0x_tpfwver_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct ft5x0x_ts_data *data = NULL;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	ssize_t num_read_chars = 0;
	u8 fwver = 0;

	data = (struct ft5x0x_ts_data *) i2c_get_clientdata(client);
	mutex_lock(&data->device_mode_mutex);
	if (ft5x0x_read_reg(FT5x06_REG_FW_VER, &fwver) < 0)
		num_read_chars = snprintf(buf, PAGE_SIZE,
				"get tp fw version fail!\n");
	else
		num_read_chars = snprintf(buf, PAGE_SIZE, "%02X\n", fwver);

	mutex_unlock(&data->device_mode_mutex);
	return num_read_chars;
}

static ssize_t ft5x0x_tpfwver_store(struct device *dev,
					struct device_attribute *attr,
						const char *buf, size_t count)
{
	/* place holder for future use */
	return -EPERM;
}

static ssize_t ft5x0x_tprwreg_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	/* place holder for future use */
	return -EPERM;
}

static ssize_t ft5x0x_tprwreg_store(struct device *dev,
					struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct ft5x0x_ts_data *data = NULL;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	ssize_t num_read_chars = 0;
	int retval;
	u16 wmreg = 0;
	u8 regaddr = 0xff, regvalue = 0xff;
	u8 valbuf[5];

	memset(valbuf, 0, sizeof(valbuf));

	data = (struct ft5x0x_ts_data *) i2c_get_clientdata(client);
	mutex_lock(&data->device_mode_mutex);
	num_read_chars = count - 1;

	if (num_read_chars != 2) {
		if (num_read_chars != 4) {
			dev_info(&this_client->dev,
				"please input 2 or 4 character\n");
			goto error_return;
		}
	}

	memcpy(valbuf, buf, num_read_chars);
	retval = kstrtoul(valbuf, 16, &wmreg);
	if (0 != retval)
		goto error_return;

	if (2 == num_read_chars) {
		/*read register*/
		regaddr = wmreg;
		if (ft5x0x_read_reg(regaddr, &regvalue) < 0)
			dev_err(&this_client->dev,
				"Could not read the register(0x%02x)\n",
				regaddr);
		else
			dev_info(&this_client->dev,
				"the register(0x%02x) is 0x%02x\n",
				regaddr, regvalue);
	} else {
		regaddr = wmreg>>8;
		regvalue = wmreg;
		if (ft5x0x_write_reg(regaddr, regvalue) < 0)
			dev_err(&this_client->dev,
				"Could not write the register(0x%02x)\n",
				regaddr);
		else
			dev_err(&this_client->dev,
				"Write 0x%02x into register(0x%02x) successful\n",
				regvalue, regaddr);
	}
error_return:
	mutex_unlock(&data->device_mode_mutex);

	return count;
}


static ssize_t ft5x0x_fwupdate_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	/* place holder for future use */
	return -EPERM;
}

/*upgrade from *.i*/
static ssize_t ft5x0x_fwupdate_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct ft5x0x_ts_data *data = NULL;

	u8 uc_host_fm_ver;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	int i_ret = fts_ctpm_fw_upgrade_with_i_file();
	data = (struct ft5x0x_ts_data *) i2c_get_clientdata(client);
	mutex_lock(&data->device_mode_mutex);

	disable_irq(this_client->irq);
	if (i_ret == 0) {
		msleep(300);
		uc_host_fm_ver = fts_ctpm_get_i_file_ver();
		dev_info(&this_client->dev,
			"%s [FTS] upgrade to new version 0x%x\n",
			 __func__, uc_host_fm_ver);
	} else {
		dev_err(&this_client->dev,
			"%s ERROR:[FTS] upgrade failed ret=%d.\n",
			__func__, i_ret);
	}
	enable_irq(this_client->irq);

	mutex_unlock(&data->device_mode_mutex);

	return count;
}

static ssize_t ft5x0x_fwupgradeapp_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	/* place holder for future use */
	return -EPERM;
}
/*upgrade from app.bin*/
static ssize_t ft5x0x_fwupgradeapp_store(struct device *dev,
					struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct ft5x0x_ts_data *data = NULL;

	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	char fwname[128];
	memset(fwname, 0, sizeof(fwname));
	data = (struct ft5x0x_ts_data *) i2c_get_clientdata(client);
	sprintf(fwname, "%s", buf);
	fwname[count-1] = '\0';

	mutex_lock(&data->device_mode_mutex);
	disable_irq(this_client->irq);

	fts_ctpm_fw_upgrade_with_app_file(fwname);

	enable_irq(this_client->irq);

	mutex_unlock(&data->device_mode_mutex);

	return count;
}

static ssize_t ft5x0x_rawdata_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct ft5x0x_ts_data *data = NULL;

	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	ssize_t num_read_chars = 0;
	int i = 0, j = 0;
	data = (struct ft5x0x_ts_data *) i2c_get_clientdata(client);
	mutex_lock(&data->device_mode_mutex);
	if (fts_get_rawdata() < 0)
		sprintf(buf, "%s", "could not get rawdata\n");
	else {
		for (i = 0; i < FT5x0x_TX_NUM; i++) {
			for (j = 0; j < FT5x0x_RX_NUM; j++) {
				num_read_chars += sprintf(
					&(buf[num_read_chars]), "%u ",
					g_rawdata[i][j]);
			}
			buf[num_read_chars-1] = '\n';
		}
	}
	mutex_unlock(&data->device_mode_mutex);
	return num_read_chars;
}

/*upgrade from app.bin*/
static ssize_t ft5x0x_rawdata_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count) {
	return -EPERM;
}


/* sysfs */
static DEVICE_ATTR(ftstpfwver, S_IRUGO|S_IWUSR,
		ft5x0x_tpfwver_show, ft5x0x_tpfwver_store);
static DEVICE_ATTR(ftsfwupdate, S_IRUGO|S_IWUSR,
		ft5x0x_fwupdate_show, ft5x0x_fwupdate_store);
static DEVICE_ATTR(ftstprwreg, S_IRUGO|S_IWUSR,
		ft5x0x_tprwreg_show, ft5x0x_tprwreg_store);
static DEVICE_ATTR(ftsfwupgradeapp, S_IRUGO|S_IWUSR,
		ft5x0x_fwupgradeapp_show, ft5x0x_fwupgradeapp_store);

static struct attribute *ft5x0x_attributes[] = {
	&dev_attr_ftstpfwver.attr,
	&dev_attr_ftsfwupdate.attr,
	&dev_attr_ftstprwreg.attr,
	&dev_attr_ftsfwupgradeapp.attr,
	NULL
};

static struct attribute_group ft5x0x_attribute_group = {
	.attrs = ft5x0x_attributes
};

static int
ft5x0x_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct ft5x0x_ts_data *ft5x0x_ts;
	struct input_dev *input_dev;
	struct device_node *np = client->dev.of_node;
	int err = 0;
	unsigned char uc_reg_value;
#if CFG_SUPPORT_TOUCH_KEY
	int i;
#endif

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}

	ft5x0x_ts = kzalloc(sizeof(struct ft5x0x_ts_data), GFP_KERNEL);
	if (!ft5x0x_ts)	{
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}

	this_client = client;
	i2c_set_clientdata(client, ft5x0x_ts);

	mutex_init(&ft5x0x_ts->device_mode_mutex);
	INIT_WORK(&ft5x0x_ts->pen_event_work, ft5x0x_ts_pen_irq_work);

	ft5x0x_ts->ts_workqueue = create_singlethread_workqueue(
					dev_name(&client->dev));
	if (!ft5x0x_ts->ts_workqueue) {
		err = -ESRCH;
		goto exit_create_singlethread;
	}

	ft5x0x_ts->touch_pin = of_get_named_gpio(np, "touch-gpio", 0);
	if (!gpio_is_valid(ft5x0x_ts->touch_pin)) {
		dev_err(&client->dev, "invalid touch_pin supplied\n");
		return -EINVAL;
	}
	if (devm_gpio_request(&client->dev, ft5x0x_ts->touch_pin, "touch-gpio")) {
		dev_err(&client->dev, "request touch gpio failed\n");
		return -EINVAL;
	}
	gpio_direction_input(ft5x0x_ts->touch_pin);
	this_client->irq = gpio_to_irq(ft5x0x_ts->touch_pin);

	if (this_client->irq) {
		err = request_irq(this_client->irq,
				ft5x0x_ts_interrupt, IRQF_TRIGGER_FALLING,
				"ft5x0x_ts", ft5x0x_ts);
		if (err < 0) {
			dev_err(&client->dev,
				"ft5x0x_probe: request irq failed\n");
			goto exit_irq_request_failed;
		}
	} else {
		dev_err(&this_client->dev, "no irq found\n");
		err = -ENODEV;
		goto exit_irq_request_failed;
	}

	disable_irq(this_client->irq);

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
			ABS_MT_POSITION_X, 0, SCREEN_MAX_X, 0, 0);
	input_set_abs_params(input_dev,
			ABS_MT_POSITION_Y, 0, SCREEN_MAX_Y, 0, 0);
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

	msleep(150);  /*make sure CTP already finish startup process*/

	/*get some register information*/
	uc_reg_value = ft5x0x_read_fw_ver();
	dev_dbg(&this_client->dev, "[FTS] Firmware version = 0x%x\n",
					uc_reg_value);
	ft5x0x_read_reg(FT5X0X_REG_PERIODACTIVE, &uc_reg_value);
	dev_dbg(&this_client->dev, "[FTS] report rate is %dHz.\n",
					uc_reg_value * 10);
	ft5x0x_read_reg(FT5X0X_REG_THGROUP, &uc_reg_value);
	dev_dbg(&this_client->dev, "[FTS] touch threshold is %d.\n",
					uc_reg_value * 4);

#if CFG_SUPPORT_AUTO_UPG
	fts_ctpm_auto_upg();
#endif

#if CFG_SUPPORT_UPDATE_PROJECT_SETTING
	fts_ctpm_update_project_setting();
#endif

	enable_irq(this_client->irq);
	/*create sysfs*/
	err = sysfs_create_group(&client->dev.kobj, &ft5x0x_attribute_group);
	if (0 != err) {
		dev_err(&client->dev,
			"%s() - ERROR: sysfs_create_group() failed: %d\n",
			__func__, err);
		sysfs_remove_group(&client->dev.kobj, &ft5x0x_attribute_group);
	} else
		dev_dbg(&this_client->dev,
			"ft5x0x:%s() - sysfs_create_group() succeeded.\n",
			__func__);

	dev_dbg(&this_client->dev, "[FTS] ==probe over =\n");
	return 0;

exit_input_register_device_failed:
	input_free_device(input_dev);
exit_input_dev_alloc_failed:
	free_irq(this_client->irq, ft5x0x_ts);
exit_irq_request_failed:
	cancel_work_sync(&ft5x0x_ts->pen_event_work);
	destroy_workqueue(ft5x0x_ts->ts_workqueue);
exit_create_singlethread:
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
	mutex_destroy(&ft5x0x_ts->device_mode_mutex);
	free_irq(this_client->irq, ft5x0x_ts);
	input_unregister_device(ft5x0x_ts->input_dev);
	kfree(ft5x0x_ts);
	cancel_work_sync(&ft5x0x_ts->pen_event_work);
	destroy_workqueue(ft5x0x_ts->ts_workqueue);
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
	},
};

module_i2c_driver(ft5x0x_ts_driver);

MODULE_AUTHOR("<wenfs@Focaltech-systems.com>");
MODULE_DESCRIPTION("FocalTech ft5x0x TouchScreen driver");
MODULE_LICENSE("GPL");
