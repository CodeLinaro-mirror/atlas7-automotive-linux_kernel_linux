/*
 * CSR Atlas7 HDMI receiver driver
 *
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/v4l2-mediabus.h>
#include <linux/videodev2.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <media/v4l2-subdev.h>
#include <linux/platform_device.h>
#include <linux/extcon/extcon-gpio.h>
#include <linux/of_gpio.h>
#include <media/v4l2-of.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

#define PIXEL_SHIFT_16BIT  0
#define SDR  0
#define DDR  1

#define BITS_8  0
#define BITS_16 1

struct it68013_video_info {
	bool hsyncpos;
	bool vsyncpos;
	bool interlaced;
	u32 hactive;
	u32 hbp;
	u32 hfp;
	u32 hsyncw;
	u32 vactive;
	u32 vbp;
	u32 vfp;
	u32 vsyncw;
	u32 tmdsclk;
	u32 pclk;
};

struct it68013_priv {
	struct v4l2_subdev		subdev;
	struct it68013_video_info	info;
	struct v4l2_of_endpoint		endpoint;
	unsigned char		data_bus_mode;
	unsigned char		data_bus_width;
	bool				is_start;
	int					hotplug_gpio;
	bool                is_connected;
};
struct it68013_reg_ini {
	unsigned char ucaddr;
	unsigned char andmask;
	unsigned char ucvalue;
};

/* 1080p prefer timing: 1920x1080p -> 1280x720p -> 720x480p -> 640x480p */
static const unsigned char it68013_edid_table[128] = {
	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
	0x26, 0x85, 0x02, 0x68, 0x01, 0x00, 0x00, 0x00,
	0x0C, 0x14, 0x01, 0x03, 0x80, 0x1C, 0x15, 0x78,
	0x0A, 0x1E, 0xAC, 0x98, 0x59, 0x56, 0x85, 0x28,
	0x29, 0x52, 0x57, 0x20, 0x00, 0x00, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x3A,
	0x80, 0x18, 0x71, 0x38, 0x2D, 0x40, 0x58, 0x2C,
	0x45, 0x00, 0x10, 0x09, 0x00, 0x00, 0x00, 0x1E,
	0x01, 0x1D, 0x00, 0x72, 0x51, 0xD0, 0x1E, 0x20,
	0x6E, 0x28, 0x55, 0x00, 0xA0, 0x5A, 0x00, 0x00,
	0x00, 0x1E, 0x8C, 0x0A, 0xD0, 0x8A, 0x20, 0xE0,
	0x2D, 0x10, 0x10, 0x3E, 0x96, 0x00, 0x04, 0x03,
	0x00, 0x00, 0x00, 0x18, 0xD6, 0x09, 0x80, 0xA0,
	0x20, 0xE0, 0x2D, 0x10, 0x10, 0x60, 0xA2, 0x00,
	0x04, 0x03, 0x00, 0x00, 0x00, 0x1E, 0x00, 0x59
};

/*hdmi port init register table  */
static const struct it68013_reg_ini it68013_hdmi_init_table[] = {
	{0x0F, 0x03, 0x00}, {0x10, 0xFF, 0x08}, {0x10, 0xFF, 0x17},
	{0x11, 0xFF, 0x1F}, {0x18, 0xFF, 0x1F}, {0x12, 0xFF, 0xF8},
	{0x10, 0xFF, 0x10}, {0x11, 0xFF, 0xA0}, {0x18, 0xFF, 0xA0},
	{0x12, 0xFF, 0x00}, {0x0F, 0x03, 0x01}, {0xB0, 0x03, 0x01},
	{0xC0, 0x80, 0x00}, {0x0F, 0x03, 0x00}, {0x17, 0xc0, 0x80},
	{0x1E, 0xc0, 0x00}, {0x16, 0x08, 0x08}, {0x1D, 0x08, 0x08},
	{0x2B, 0xFF, 0x07}, {0x31, 0xFF, 0x2C}, {0x34, 0xFF, 0xE1},
	{0x35, 0x1E, 0x14}, {0x4B, 0x1E, 0x14}, {0x54, 0xFF, 0x11},
	{0x6A, 0xFF, 0x81}, {0x74, 0xFF, 0xA0}, {0x50, 0x1F, 0x12},
	{0x7A, 0x80, 0x80}, {0x85, 0x02, 0x02}, {0xC0, 0x43, 0x40},
	{0x71, 0x08, 0x00}, {0x37, 0xFF, 0x88}, {0x4D, 0xFF, 0x88},
	{0x67, 0x80, 0x00}, {0x7A, 0x70, 0x70}, {0x77, 0x80, 0x00},
	{0x0F, 0x03, 0x01}, {0xC0, 0x80, 0x00}, {0x0F, 0x03, 0x00},
	{0x7E, 0x40, 0x40}, {0x52, 0x20, 0x20}, {0x53, 0xC0, 0x40},
	{0x58, 0xFF, 0x33}, {0x25, 0xFF, 0x1F}, {0x3D, 0xFF, 0x1F},
	{0x27, 0xFF, 0x1F}, {0x28, 0xFF, 0x1F}, {0x29, 0xFF, 0x1F},
	{0x3F, 0xFF, 0x1F}, {0x40, 0xFF, 0x1F}, {0x41, 0xFF, 0x1F},
	{0x0F, 0x03, 0x01}, {0xBC, 0xFF, 0x06}, {0xB5, 0x03, 0x03},
	{0xB6, 0x07, 0x00}, {0x0F, 0x03, 0x00}, {0x22, 0xFF, 0x00},
	{0x3A, 0xFF, 0x00}, {0x26, 0xFF, 0x00}, {0x3E, 0xFF, 0x00},
	{0x63, 0xFF, 0x3F}, {0x73, 0x08, 0x00}, {0x64, 0x08, 0x08},
	{0x05, 0xFF, 0xFF},
	/* disable all interrupts temporarily  */
	{0x5D, 0xFF, 0}, {0x5E, 0xFF, 0}, {0x5F, 0xFF, 0},
	{0x60, 0xFF, 0}, {0x61, 0xFF, 0}, {0x62, 0xFF, 0},
	{0xFF, 0xFF, 0xFF}
};

static void it68013_hotplug_notify(struct v4l2_subdev *sd, bool is_connected);


/*
 * general function
 */
static struct it68013_priv *to_it68013(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct it68013_priv,
			    subdev);
}


static ssize_t hotplug_status_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct it68013_priv *priv;

	priv = to_it68013(container_of(dev, struct i2c_client, dev));

	if (!priv)
		return -ENODEV;

	return sprintf(buf, "%d\n", priv->is_connected);
}

static DEVICE_ATTR_RO(hotplug_status);


/*
 * it68013 function

 * get the phyaddress from the edid, please reference the edid defined.
 */
static unsigned char it68013_find_phyaddress(const unsigned char *pedid)
{
	unsigned char vsdb_addr;
	unsigned char i, tag, end, count;

	if ((*pedid != 0x02) || (*(pedid + 1) != 0x03))
		return 0;

	end = *(pedid + 2);
	for (i = 0x04; i < end;) {
		tag = (*(pedid + i)) >> 5;
		count = (*(pedid + i)) & 0x1f;
		i++;

		if (tag == 0x03 && *(pedid + i) == 0x03 &&
			*(pedid + i + 1) == 0x0C &&
			*(pedid + i + 2) == 0x00) {
			vsdb_addr = 128 + i + 3;
			return vsdb_addr;
		}
		i += count;
	}
	return 0;
}

/* set the edid table into it68013.
* note: the i2c address of edid ram is set in the register 0x87
*/
static void it68013_edid_init(struct i2c_client *client)
{
	int i;
	unsigned char sum = 0;
	unsigned char block0_checksum, block1_checksum;
	unsigned char vsdb_addr;
	struct device_node *edid_np;
	struct i2c_client *edid_client;

	edid_np = of_find_compatible_node(NULL, NULL, "itetech,edid");
	if (!edid_np) {
		dev_err(&client->dev, "Fail to find edid i2c node\n");
		return;
	}

	edid_client = of_find_i2c_device_by_node(edid_np);
	if (!edid_client) {
		dev_err(&client->dev, "Fail to get edid i2c node\n");
		goto node_put_exit;
	}
	/*set the edid address firstly.*/
	i2c_smbus_write_byte_data(client, 0x87, (edid_client->addr<<1)|0x01);

	for (i = 0; i < 127; i++) {
		if (i2c_smbus_write_byte_data(edid_client,
						i, it68013_edid_table[i])) {
			dev_err(&edid_client->dev, "EDID 1st 128 write err\n");
			goto device_put_exit;
		}
		sum += it68013_edid_table[i];
	}
	/*set the checksum */
	block0_checksum = 0x00 - sum;
	i2c_smbus_write_byte_data(client, 0xC4, block0_checksum);

	if (sizeof(it68013_edid_table) < 256)
		goto device_put_exit;

	/*support the extend edid */
	sum = 0;
	for (i = 128; i < 255; i++) {
		if (i2c_smbus_write_byte_data(edid_client,
						i, it68013_edid_table[i])) {
			dev_err(&edid_client->dev, "EDID 2nd 128 write err\n");
			goto device_put_exit;
		}
		sum += it68013_edid_table[i];
	}
	block1_checksum = 0x00 - sum;

	i2c_smbus_write_byte_data(client, 0xC5, block1_checksum);

	vsdb_addr = it68013_find_phyaddress(it68013_edid_table + 128);
	if (vsdb_addr != 0) {
		i2c_smbus_write_byte_data(client, 0xC1, vsdb_addr);
		i2c_smbus_write_byte_data(client, 0xC2, 0x11);
		i2c_smbus_write_byte_data(client, 0xC3, 0x00);
		i2c_smbus_write_byte_data(client, 0xC5, 0x6A);

		i2c_smbus_write_byte_data(client, 0xC6, 0x12);
		i2c_smbus_write_byte_data(client, 0xC7, 0x00);
		i2c_smbus_write_byte_data(client, 0xC9, 0x69);
	}

device_put_exit:
	put_device(&edid_client->dev);
node_put_exit:
	of_node_put(edid_np);
}

/* set and clear the register of hdmi port*/
static void it68013_hdmi_set(struct i2c_client *client, unsigned char offset,
		unsigned char mask, unsigned char ucdata)
{
	unsigned char temp;

	temp = i2c_smbus_read_byte_data(client, offset);
	temp = (temp & (~mask & 0xFF)) + (mask & ucdata);
	i2c_smbus_write_byte_data(client, offset, temp);
}

/* write the register of hdmi port*/
static void it68013_hdmi_write(struct i2c_client *client, unsigned char start,
		unsigned int len, const unsigned char *data)
{
	unsigned char i;

	for (i = 0; i < len; i++)
		i2c_smbus_write_byte_data(client, start+i, data[i]);
}


/* init the hdmi port registers.  */
static void it68013_hdmi_init(struct i2c_client *client,
		const struct it68013_reg_ini *table)
{
	u32 cnt = 0;

	while (table[cnt].ucaddr != 0xFF) {
		it68013_hdmi_set(client, table[cnt].ucaddr,
				table[cnt].andmask,
				table[cnt].ucvalue);
		cnt++;
	}
}

/* get the chip id, verder id and device id*/
static int it68013_get_chipid(struct i2c_client *client,
		u16 *vendor, u16 *device)
{

	s8 value;

	value = i2c_smbus_read_byte_data(client, 0x01);
	if (value < 0)
		return -EIO;

	*vendor = value;
	*vendor <<= 8;

	value = i2c_smbus_read_byte_data(client, 0x00);
	if (value < 0)
		return -EIO;

	*vendor |= (u16)value;

	value = i2c_smbus_read_byte_data(client, 0x03);
	if (value < 0)
		return -EIO;

	*device = value;
	*device <<= 8;

	value = i2c_smbus_read_byte_data(client, 0x02);
	if (value < 0)
		return -EIO;

	*device |= (u16)value;

	return 0;
}

/* get the actual video timing info*/
static void it68013_get_vid_info(const struct i2c_client *client,
					struct it68013_video_info *info)
{
	int hsyncpol, vsyncpol, interlaced;
	int htotal, hactive, hfp, hsyncw;
	int vtotal, vactive, vfp, vsyncw;

	unsigned int uctmdsclk = 0;
	unsigned char rddata, ucclk;
	int pclk;

	rddata = i2c_smbus_read_byte_data(client, 0x9A);
	pclk = (124 * 255 / rddata) / 10;
	rddata = i2c_smbus_read_byte_data(client, 0x90);

	ucclk = i2c_smbus_read_byte_data(client, 0x91);
	if (ucclk != 0) {
		if (rddata & 0x01)
			uctmdsclk = 2 * 12 * 256 / ucclk;
		else if (rddata & 0x02)
			uctmdsclk = 4 * 12 * 256 / ucclk;
		else
			uctmdsclk = 12 * 256 / ucclk;
	}

	interlaced = (i2c_smbus_read_byte_data(client, 0x99)&0x02) >> 1;
	htotal = ((i2c_smbus_read_byte_data(client, 0x9D)&0x3F) << 8)
				+ i2c_smbus_read_byte_data(client, 0x9C);
	hactive = ((i2c_smbus_read_byte_data(client, 0x9F)&0x3F) << 8)
				+ i2c_smbus_read_byte_data(client, 0x9E);
	hfp = ((i2c_smbus_read_byte_data(client, 0xA1)&0xF0) << 4)
				+ i2c_smbus_read_byte_data(client, 0xA2);
	hsyncw = ((i2c_smbus_read_byte_data(client, 0xA1)&0x01) << 8)
				+ i2c_smbus_read_byte_data(client, 0xA0);
	hsyncpol = (i2c_smbus_read_byte_data(client, 0xA8)&0x04) >> 2;

	vtotal = ((i2c_smbus_read_byte_data(client, 0xA4)&0x0F) << 8)
				+ i2c_smbus_read_byte_data(client, 0xA3);
	vactive = ((i2c_smbus_read_byte_data(client, 0xA4)&0xF0) << 4)
				+ i2c_smbus_read_byte_data(client, 0xA5);
	vfp = i2c_smbus_read_byte_data(client, 0xA7) & 0x3F;
	vsyncw = i2c_smbus_read_byte_data(client, 0xA6) & 0x1F;
	vsyncpol = (i2c_smbus_read_byte_data(client, 0xA8)&0x08) >> 3;

	info->hactive = hactive;
	info->hfp = hfp;
	info->hsyncw = hsyncw;
	info->hbp = htotal - hactive - hfp - hsyncw;
	info->vactive = vactive;
	info->vfp = vfp;
	info->vsyncw = vsyncw;
	info->vbp = vtotal - vactive - vfp - vsyncw;

	if (interlaced & 0x01)
		info->interlaced = true;
	else
		info->interlaced = false;
	if (vsyncpol & 0x01)
		info->vsyncpos = true;
	else
		info->vsyncpos = false;
	if (hsyncpol & 0x01)
		info->hsyncpos = true;
	else
		info->hsyncpos = false;

	info->tmdsclk = uctmdsclk;
	info->pclk = pclk;

}

/* wait the it68013 status of register 0x0A.*/
static int it68013_wait_status(struct i2c_client *client, u8 status)
{
	u32 timeout;
	u8 reg_status;

	/*wait about 3 seconds here*/
	timeout = 150;
	do {
		reg_status = i2c_smbus_read_byte_data(client, 0x0A);
		if (reg_status & status)
			break;
		msleep(20);
	} while (--timeout);

	if (!timeout)
		return -EIO;

	return 0;
}

static int it68013_video_start(struct i2c_client *client)
{
	struct it68013_priv *priv = to_it68013(client);
	u8 value = 0;
	int ret;
	const u8 color_reg_table[] = {
		0x10, 0x80, 0x10, 0x09,
		0x04, 0x0e, 0x02, 0xc9,
		0x00, 0x0f, 0x3d, 0x84,
		0x03, 0x6d, 0x3f, 0xab,
		0x3d, 0xd1, 0x3e, 0x84,
		0x03};

	/*no need to start again   */
	if (priv->is_start)
		return 0;

	/*wait port 0 power 5v detect status  */
	ret = it68013_wait_status(client, 0x01);
	if (ret)
		return ret;

	/* set HPD to high */
	i2c_smbus_write_byte_data(client, 0x0F, 0x01);
	value = i2c_smbus_read_byte_data(client, 0xB0);
	i2c_smbus_write_byte_data(client, 0xB0, value | 0x3);
	i2c_smbus_write_byte_data(client, 0x0F, 0x00);

	/*wait port 0 video stable status  */
	ret = it68013_wait_status(client, 0x80);
	if (ret)
		return ret;

	/* config color space matrix regs */
	i2c_smbus_write_byte_data(client, 0x0F, 0x01);
	it68013_hdmi_write(client, 0x70,
			ARRAY_SIZE(color_reg_table),
			color_reg_table);
	i2c_smbus_write_byte_data(client, 0x0F, 0x00);

	switch (priv->endpoint.bus_type) {
	case V4L2_MBUS_BT656:
		if ((priv->data_bus_width == BITS_8) &&
				(priv->data_bus_mode == SDR)) {
			/* D_8BIT_656_SDR */
			i2c_smbus_write_byte_data(client, 0x51, 0x04);
			i2c_smbus_write_byte_data(client, 0x65, 0x52);
		} else if ((priv->data_bus_width == BITS_16) &&
						(priv->data_bus_mode == SDR)) {
			/* D_16BIT_656_SDR (BTA1004) */
			i2c_smbus_write_byte_data(client, 0x51, 0x00);
			i2c_smbus_write_byte_data(client, 0x65, 0xd2);
		} else if ((priv->data_bus_width == BITS_8) &&
						(priv->data_bus_mode == DDR)) {
			/* D_8BIT_656_DDR */
			i2c_smbus_write_byte_data(client, 0x50, 0xb2);
			i2c_smbus_write_byte_data(client, 0x51, 0x44);
			i2c_smbus_write_byte_data(client, 0x65, 0x52);
		}
		break;
	case V4L2_MBUS_PARALLEL:
		if ((priv->data_bus_width == BITS_8) &&
				(priv->data_bus_mode == SDR)) {
			/* D_8BIT_601_SDR */
			i2c_smbus_write_byte_data(client, 0x51, 0x04);
			i2c_smbus_write_byte_data(client, 0x65, 0x12);
		} else if ((priv->data_bus_width == BITS_16) &&
						(priv->data_bus_mode == SDR)) {
			/* D_16BIT_601_SDR (BT656 + DE) */
			i2c_smbus_write_byte_data(client, 0x51, 0x00);
			i2c_smbus_write_byte_data(client, 0x65, 0x12);
		} else if ((priv->data_bus_width == BITS_8) &&
						(priv->data_bus_mode == DDR)) {
			/* D_8BIT_601_DDR */
			i2c_smbus_write_byte_data(client, 0x50, 0xb2);
			i2c_smbus_write_byte_data(client, 0x51, 0x44);
			i2c_smbus_write_byte_data(client, 0x65, 0x12);
		}
		break;
	default:
		break;
	}

	/*wait port 0 video stable status  */
	ret = it68013_wait_status(client, 0x80);
	if (ret)
		return ret;

	priv->is_start = true;

	/*
	* get the actual video timing here
	* workaround:
	* have to wait 2s, otherwise interlaced indicator might be wrong.
	*/
	ssleep(2);
	it68013_get_vid_info(client, &priv->info);

	return 0;
}

static void it68013_video_out(struct i2c_client *client)
{
	u8 value;

	/*	enable output	*/
	value = i2c_smbus_read_byte_data(client, 0x53);
	value &= ~0x0F;
	i2c_smbus_write_byte_data(client, 0x53, value);
	i2c_smbus_write_byte_data(client, 0x84, 0x8F);
	i2c_smbus_write_byte_data(client, 0x6A, 0x81);
}

static int it68013_video_stop(struct i2c_client *client)
{
	struct it68013_priv *priv = to_it68013(client);
	u8 value;

	/* set HPD to low */
	i2c_smbus_write_byte_data(client, 0x0F, 0x01);
	value = i2c_smbus_read_byte_data(client, 0xB0);
	i2c_smbus_write_byte_data(client, 0xB0, value & ~0x2);
	i2c_smbus_write_byte_data(client, 0x0F, 0x00);

	value = i2c_smbus_read_byte_data(client, 0x53);
	/*	set output to tri-state */
	value |= 0x0F;
	i2c_smbus_write_byte_data(client, 0x53, value);

	priv->is_start = false;

	return 0;
}

/*
 * subdevice operations
 */
static int it68013_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	if (enable) {
		ret = it68013_video_start(client);
		if (ret)
			return ret;
		it68013_video_out(client);
	} else {
		it68013_video_stop(client);
	}

	return 0;
}

static int it68013_try_fmt(struct v4l2_subdev *sd,
			  struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct it68013_priv *priv = to_it68013(client);
	int ret;

	mf->code = V4L2_MBUS_FMT_UYVY8_2X8;
	mf->colorspace = V4L2_COLORSPACE_JPEG;

	ret = it68013_video_start(client);
	if (ret)
		return ret;

	mf->width = priv->info.hactive;

	if (priv->info.interlaced) {
		if ((mf->field != V4L2_FIELD_SEQ_TB) &&
					(mf->field != V4L2_FIELD_SEQ_BT))
			mf->field = V4L2_FIELD_SEQ_TB;

		mf->height = priv->info.vactive * 2;
	} else {
		mf->field = V4L2_FIELD_NONE;
		mf->height = priv->info.vactive;
	}

	return 0;
}

static int it68013_video_probe(struct i2c_client *client)
{
	u8 value;
	/*must be initialized firtly */
	it68013_hdmi_init(client, it68013_hdmi_init_table);
	/*set the edid             */
	it68013_edid_init(client);

	/* set HPD to high */
	i2c_smbus_write_byte_data(client, 0x0F, 0x01);
	value = i2c_smbus_read_byte_data(client, 0xB0);
	i2c_smbus_write_byte_data(client, 0xB0, value | 0x3);
	i2c_smbus_write_byte_data(client, 0x0F, 0x00);

	/* enable the 5V detect interrupt     */
	i2c_smbus_write_byte_data(client, 0x5D, 0x01);

	return 0;
}

static int it68013_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			   enum v4l2_mbus_pixelcode *code)
{
	if (index)
		return -EINVAL;

	*code = V4L2_MBUS_FMT_YVYU8_2X8;
	return 0;
}

static int it68013_g_timings(struct v4l2_subdev *sd,
				struct v4l2_dv_timings *timings)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct it68013_priv *priv = to_it68013(client);

	timings->bt.width = priv->info.hactive;
	timings->bt.height = priv->info.vactive;
	timings->bt.interlaced = priv->info.interlaced ?
				V4L2_DV_INTERLACED : V4L2_DV_PROGRESSIVE;
	timings->bt.polarities =
			(priv->info.hsyncpos ?	V4L2_DV_HSYNC_POS_POL : 0)
			| (priv->info.vsyncpos ? V4L2_DV_VSYNC_POS_POL : 0);
	timings->bt.pixelclock = priv->info.pclk;
	timings->bt.hfrontporch = priv->info.hfp;
	timings->bt.hsync = priv->info.hsyncw;
	timings->bt.hbackporch = priv->info.hbp;
	timings->bt.vfrontporch = priv->info.vfp;
	timings->bt.vsync = priv->info.vsyncw;
	timings->bt.vbackporch = priv->info.vbp;

	return 0;
}

static int it68013_enum_framesizes(struct v4l2_subdev *sd,
					struct v4l2_frmsizeenum *fsize)
{
	if (fsize->index != 0)
			return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise.min_width = 2;
	fsize->stepwise.min_height = 1;
	fsize->stepwise.max_width = 1920;
	fsize->stepwise.max_height = 1080;
	fsize->stepwise.step_width = 2;
	fsize->stepwise.step_height = 1;

	return 0;
}

static struct v4l2_subdev_core_ops it68013_subdev_core_ops = {

};

static struct v4l2_subdev_video_ops it68013_subdev_video_ops = {
	.s_stream	= it68013_s_stream,
	.try_mbus_fmt	= it68013_try_fmt,
	.enum_mbus_fmt	= it68013_enum_fmt,
	.g_dv_timings = it68013_g_timings,
	.enum_framesizes = it68013_enum_framesizes,
};

static struct v4l2_subdev_ops it68013_subdev_ops = {
	.core	= &it68013_subdev_core_ops,
	.video	= &it68013_subdev_video_ops,
};

static void it68013_hotplug_notify(struct v4l2_subdev *sd, bool is_connected)
{
	unsigned int event;

	if (is_connected)
		event = KOBJ_ONLINE;
	else
		event = KOBJ_OFFLINE;

	v4l2_subdev_notify(sd, event, NULL);
}


static irqreturn_t it68013_intr_handler(int irq, void *data)
{
	struct i2c_client *client = data;
	struct it68013_priv *priv;
	u8	reg05;
	u8	reg06;
	u8	reg07;
	u8	reg08;
	u8	reg09;
	u8  reg0a;
	u8	regd0;

	priv = to_it68013(client);

	reg05 = i2c_smbus_read_byte_data(client, 0x05);
	reg06 = i2c_smbus_read_byte_data(client, 0x06);
	reg07 = i2c_smbus_read_byte_data(client, 0x07);
	reg08 = i2c_smbus_read_byte_data(client, 0x08);
	reg09 = i2c_smbus_read_byte_data(client, 0x09);
	reg0a = i2c_smbus_read_byte_data(client, 0x0a);
	regd0 = i2c_smbus_read_byte_data(client, 0xd0);

	/*clear all the interrupt flag   */
	i2c_smbus_write_byte_data(client, 0x05, reg05);
	i2c_smbus_write_byte_data(client, 0x06, reg06);
	i2c_smbus_write_byte_data(client, 0x07, reg07);
	i2c_smbus_write_byte_data(client, 0x08, reg08);
	i2c_smbus_write_byte_data(client, 0x09, reg09);
	i2c_smbus_write_byte_data(client, 0x0a, reg0a);
	i2c_smbus_write_byte_data(client, 0xd0, regd0);

	/* 5V detect interrupt */
	priv->is_connected = !!(reg0a & 0x01);

	it68013_hotplug_notify(&priv->subdev, priv->is_connected);

	return IRQ_HANDLED;
}



static int it68013_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct it68013_priv *priv;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct device_node *it68013_np = client->dev.of_node;
	int	data_shift;
	u16 vendor_id, device_id;
	int ret;
	const char *mode;
	struct device_node *port_np, *endpoint_np = NULL;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev,
			"I2C-Adap doesn't support I2C_FUNC_SMBUS_BYTE_DATA\n");
		return -EIO;
	}

	if (it68013_get_chipid(client, &vendor_id, &device_id)) {
		dev_err(&client->dev,
		"%s:read it68013 chip id failed\n",
		__func__);
		return -ENODEV;
	}

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&priv->subdev, client, &it68013_subdev_ops);

	/*get setting from the dtb*/
	port_np = of_get_child_by_name(it68013_np, "port");
	if (port_np) {
		endpoint_np = of_get_next_child(port_np, NULL);
		if (!endpoint_np) {
			dev_err(&client->dev, "## [IT68013] endpoint node\n");
			return -EINVAL;
		}
	} else {
		dev_err(&client->dev, "[IT68013] Can't find port node\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(endpoint_np, "data-shift", &data_shift);
	if (ret)
		dev_err(&client->dev, "[IT68013] data shift value\n");

	/*default mode is 8 bit mode  */
	priv->data_bus_width = BITS_8;
	if (data_shift == PIXEL_SHIFT_16BIT)
		priv->data_bus_width = BITS_16;

	ret = of_property_read_string(endpoint_np, "data-mode", &mode);
	if (ret)
		dev_err(&client->dev, "[IT68013] Can't get data mode value\n");

	if (!strcmp(mode, "SDR"))
		priv->data_bus_mode = SDR;
	if (!strcmp(mode, "DDR"))
		priv->data_bus_mode = DDR;


	priv->hotplug_gpio = of_get_named_gpio(it68013_np,
		"hp-hdmiinput-gpios", 0);

	if (gpio_is_valid(priv->hotplug_gpio)) {

		ret = devm_gpio_request_one(&client->dev, priv->hotplug_gpio,
			GPIOF_IN, client->name);
		if (ret)
			dev_err(&client->dev, "request gpio failed!\n");

		ret = gpio_direction_input(priv->hotplug_gpio);
		if (ret)
			dev_err(&client->dev, "set gpio input failed\n");

		ret = devm_request_threaded_irq(&client->dev,
					gpio_to_irq(priv->hotplug_gpio),
					NULL, it68013_intr_handler,
					IRQF_TRIGGER_FALLING,
					client->name, client);

		if (ret)
			dev_err(&client->dev,
			"irq requested failed, %d\n", ret);
	}


	v4l2_of_parse_endpoint(endpoint_np, &priv->endpoint);

	of_node_put(endpoint_np);
	of_node_put(port_np);

	dev_info(&client->dev,
		"it68013 Vendor ID:0x%0x,Device ID:0x%x\n",
		vendor_id, device_id);

	ret = it68013_video_probe(client);
	if (ret)
		dev_err(&client->dev, "it68013_video_probe failed, %d\n", ret);


	/*check the connect status, send notification when plug-in  */
	priv->is_connected = !!(i2c_smbus_read_byte_data(client, 0x0a) & 0x01);

	ret = sysfs_create_file(&client->dev.kobj,
			&dev_attr_hotplug_status.attr);

	if (ret)
		dev_err(&client->dev,
	"it68013_video_probe sysfs_create_file failed, %d\n", ret);


	return 0;
}

static int it68013_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	it68013_s_stream(sd, 0);

	sysfs_remove_file(&client->dev.kobj, &dev_attr_hotplug_status.attr);

	v4l2_device_unregister_subdev(sd);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int it68013_pm_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	/* do the hardware initialization */
	it68013_video_probe(client);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(it68013_pm_ops, NULL, it68013_pm_resume);


static const struct i2c_device_id it68013_id[] = {
	{ "it68013", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, it68013_id);

static struct i2c_driver it68013_i2c_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name = "it68013",
		.pm = &it68013_pm_ops,
	},
	.probe    = it68013_probe,
	.remove   = it68013_remove,
	.id_table = it68013_id,
};

module_i2c_driver(it68013_i2c_driver);

MODULE_DESCRIPTION("IT68013 I2C subdev driver");
MODULE_LICENSE("GPL v2");
