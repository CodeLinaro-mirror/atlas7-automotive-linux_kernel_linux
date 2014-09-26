/*
 * tw9900 Video Driver
 *
 * Copyright (C) 2005-2006 Micronas USA Inc.
 *
 * Based on tw9906 driver,
 * Currently this driver supports NTSC(default) and PAL input format,
 * ITU-R-656 8-bit YCrCb 4:2:2 output format,
 * 720x480 or 720x576 resolution output.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <linux/delay.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <linux/ioctl.h>
#include <linux/slab.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

struct tw9900 {
	v4l2_std_id norm;
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
};

static inline struct tw9900 *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct tw9900, sd);
}

static const u8 initial_registers[] = {
	0x02, 0x40,	/* Input0, CVBS */
	0x03, 0xA7,	/* ITU-R-656 8-bit YCrCb 4:2:2, output tri-stated */
	0x05, 0x81,	/* VSYNC/HSYNC pin setting for NTSC, or 0x01 for PAL */
	0x06, 0x80,	/* Resets the device to its default state */
	0x07, 0x02,	/* The two MSBs of windows */
	0x08, 0x13,	/* Window: vertical delay */
	0x09, 0xF0,	/* Window: vertical 480 */
	0x0A, 0x10,	/* Window: horizontal delay */
	0x0B, 0xD0,	/* Window: horizontal 720 */
	0x10, 0x00,	/* Brightness */
	0x11, 0x60,	/* Contrast */
	0x12, 0x11,	/* Sharpness */
	0x13, 0x7E,	/* U gain */
	0x14, 0x7E,	/* V gain */
	0x15, 0x00,	/* HUE */
	0x19, 0x57,	/* VBI disable, pixel 1,2,3..on the VD[7:0] data bus */
	0x1A, 0x0F,	/* Anti-alias filter control, power saving enabled */
	0x1C, 0x0F,	/* Disable shadow registers, auto detection standard*/
	0x1D, 0x03,	/* Recognition only 'NTSC(M)' and 'PAL (B,D,G,H,I)' */
	0x28, 0x04,	/* Decrease 30~40ms in NTSC detection, 10~20ms PAL */
	0x29, 0x03,	/* Vertical sync delay in half line length increment */
	0x2D, 0x07,	/* Hsync output disables when video loss is detected */
	0x2E, 0x25,	/* Set Horizontal PLL acquisition time to 'Normal' */
	0x55, 0x00,	/* Odd Even field video output line number is same */
	0x6B, 0x09,	/* HSYNC start position */
	0x6C, 0x19,	/* HSYNC end position */
	0x6D, 0x0A,	/* VSYNC start position */
	0x00, 0x00,	/* Terminator (reg 0x00 is read-only) */
};

static int write_reg(struct v4l2_subdev *sd, u8 reg, u8 value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return i2c_smbus_write_byte_data(client, reg, value);
}

static int write_regs(struct v4l2_subdev *sd, const u8 *regs)
{
	int i;

	for (i = 0; regs[i] != 0x00; i += 2)
		if (write_reg(sd, regs[i], regs[i + 1]) < 0)
			return -1;
	return 0;
}

static int tw9900_g_std(struct v4l2_subdev *sd, v4l2_std_id *norm)
{
	struct tw9900 *dec = to_state(sd);

	*norm = dec->norm;

	return 0;
}

static int tw9900_s_std(struct v4l2_subdev *sd, v4l2_std_id norm)
{
	struct tw9900 *dec = to_state(sd);
	bool is_60hz = norm & V4L2_STD_525_60;
	static const u8 config_60hz[] = {
		0x05, 0x81,
		0x07, 0x02,
		0x08, 0x13,
		0x09, 0xF0,
		0,    0,
	};
	static const u8 config_50hz[] = {
		0x05, 0x01,
		0x07, 0x12,
		0x08, 0x18,
		0x09, 0x20,
		0,    0,
	};

	if (!(norm & (V4L2_STD_NTSC | V4L2_STD_PAL)))
		return -EINVAL;

	write_regs(sd, is_60hz ? config_60hz : config_50hz);
	dec->norm = norm;

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int tw9900_g_register(struct v4l2_subdev *sd,
			     struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	if (reg->reg > 0xff)
		return -EINVAL;

	ret = i2c_smbus_read_byte_data(client, reg->reg);
	if (ret < 0)
		return ret;

	/*
	 * ret	= int
	 * reg->val = __u64
	 */
	reg->val = (__u64)ret;

	return 0;
}

static int tw9900_s_register(struct v4l2_subdev *sd,
			const struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->reg > 0xff ||
	    reg->val > 0xff)
		return -EINVAL;

	return i2c_smbus_write_byte_data(client, reg->reg, reg->val);
}
#endif

static int tw9900_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int i = 0;
	u8 value;

	if (!enable) {
		value = i2c_smbus_read_byte_data(client, 0x03);
		value |= 0x07;	/* set all output to tri-state */
		i2c_smbus_write_byte_data(client, 0x03, value);

		return 0;
	}

	i2c_smbus_write_byte_data(client, 0x06, 0x80);	/* set default state */

	value = i2c_smbus_read_byte_data(client, 0x1D);
	value |= 0x80;	/* manually initiate auto format detection process */
	i2c_smbus_write_byte_data(client, 0x1D, value);

	while (i < 20) {
		value = i2c_smbus_read_byte_data(client, 0x1C);

		if (!(value & 0x80)) {	/* 0: Idle, 1: detection in progress */

			msleep(20);

			value = i2c_smbus_read_byte_data(client, 0x1C);
			if (!(value & 0x80))
				break;

			msleep(20);
		} else
			msleep(20);
		i++;
	}

	if (i >= 20) {
		v4l_err(client, "No signal detected\n");
		return -EINVAL;
	}

	if (!((value >> 4) & 0x7))
		v4l_info(client, "NTSC(M) signal\n");
	else {
		v4l_info(client, "PAL(B,D,G,H,I) signal\n");
		tw9900_s_std(sd, V4L2_STD_PAL);
	}

	value = i2c_smbus_read_byte_data(client, 0x03);
	value &= 0xF8;	/* enable all output */
	i2c_smbus_write_byte_data(client, 0x03, value);

	return 0;
}

static int tw9900_g_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	struct tw9900 *dec = to_state(sd);

	if (dec->norm & V4L2_STD_NTSC) {
		mf->width	= 720;
		mf->height	= 480;
	} else {
		mf->width	= 720;
		mf->height	= 576;
	}

	mf->code	= V4L2_MBUS_FMT_UYVY8_2X8;
	mf->colorspace	= V4L2_COLORSPACE_JPEG;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

static int tw9900_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	struct tw9900 *dec = to_state(sd);

	a->bounds.left			= 0;
	a->bounds.top			= 0;
	if (dec->norm & V4L2_STD_NTSC) {
		a->bounds.width         = 720;
		a->bounds.height        = 480;
	} else {
		a->bounds.width         = 720;
		a->bounds.height        = 576;
	}
	a->defrect                      = a->bounds;
	a->type				= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	return 0;
}

static int tw9900_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	struct tw9900 *dec = to_state(sd);

	a->c.left	= 0;
	a->c.top	= 0;
	if (dec->norm & V4L2_STD_NTSC) {
		a->c.width	= 720;
		a->c.height	= 480;
	} else {
		a->c.width	= 720;
		a->c.height	= 576;
	}
	a->type	= V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return 0;
}

static int tw9900_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			   enum v4l2_mbus_pixelcode *code)
{
	if (index)
		return -EINVAL;

	*code = V4L2_MBUS_FMT_UYVY8_2X8;
	return 0;
}

static int tw9900_s_routing(struct v4l2_subdev *sd, u32 input,
				      u32 output, u32 config)
{
	switch (input) {
	case 0:		/* INPUT_CVBS_AIN1 */
		write_reg(sd, 0x02, 0x40);
		break;
	case 1:		/* INPUT_CVBS_AIN2 */
		write_reg(sd, 0x02, 0x44);
		break;
	case 2:		/* INPUT_YC */
		write_reg(sd, 0x02, 0x54);
		break;
	default:
		return 0;
	}

	return 0;
}

static int tw9900_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct tw9900 *dec = container_of(ctrl->handler, struct tw9900, hdl);
	struct v4l2_subdev *sd = &dec->sd;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		write_reg(sd, 0x10, ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		write_reg(sd, 0x11, ctrl->val);
		break;
	case V4L2_CID_HUE:
		write_reg(sd, 0x15, ctrl->val);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct v4l2_ctrl_ops tw9900_ctrl_ops = {
	.s_ctrl = tw9900_s_ctrl,
};

static struct v4l2_subdev_core_ops tw9900_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= tw9900_g_register,
	.s_register	= tw9900_s_register,
#endif
};

static struct v4l2_subdev_video_ops tw9900_video_ops = {
	.s_std		= tw9900_s_std,
	.g_std		= tw9900_g_std,
	.s_stream	= tw9900_s_stream,
	.g_mbus_fmt	= tw9900_g_fmt,
	.cropcap	= tw9900_cropcap,
	.g_crop		= tw9900_g_crop,
	.enum_mbus_fmt	= tw9900_enum_fmt,
	.s_routing	= tw9900_s_routing,
};

static struct v4l2_subdev_ops tw9900_ops = {
	.core	= &tw9900_core_ops,
	.video	= &tw9900_video_ops,
};

static int tw9900_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct tw9900 *dec;
	struct v4l2_subdev *sd;
	struct v4l2_ctrl_handler *hdl;
	struct i2c_adapter *adapter = client->adapter;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		v4l2_err(client, "I2C-Adapter doesn't support BYTE DATA\n");
		return -EIO;
	}

	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	dec = kzalloc(sizeof(*dec), GFP_KERNEL);
	if (dec == NULL)
		return -ENOMEM;

	sd = &dec->sd;
	v4l2_i2c_subdev_init(sd, client, &tw9900_ops);

	hdl = &dec->hdl;
	v4l2_ctrl_handler_init(hdl, 4);
	v4l2_ctrl_new_std(hdl, &tw9900_ctrl_ops,
				V4L2_CID_BRIGHTNESS, -128, 127, 1, 0);
	v4l2_ctrl_new_std(hdl, &tw9900_ctrl_ops,
				V4L2_CID_CONTRAST, 0, 255, 1, 0x60);
	v4l2_ctrl_new_std(hdl, &tw9900_ctrl_ops,
				V4L2_CID_HUE, -128, 127, 1, 0);
	sd->ctrl_handler = hdl;
	if (hdl->error) {
		v4l2_ctrl_handler_free(hdl);
		kfree(dec);
		return hdl->error;
	}

	/* Initialize tw9900 */
	dec->norm = V4L2_STD_NTSC;

	if (write_regs(sd, initial_registers) < 0) {
		v4l2_err(client, "error initializing TW9900\n");
		kfree(dec);
		return -EINVAL;
	}

	return 0;
}

static int tw9900_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&to_state(sd)->hdl);
	kfree(to_state(sd));
	return 0;
}

static const struct i2c_device_id tw9900_id[] = {
	{ "tw9900", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tw9900_id);

static struct i2c_driver tw9900_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name = "tw9900",
	},
	.probe    = tw9900_probe,
	.remove   = tw9900_remove,
	.id_table = tw9900_id,
};

module_i2c_driver(tw9900_driver);

MODULE_DESCRIPTION("TW9900 I2C subdev driver");
MODULE_AUTHOR("Andy Sun<Andy.Sun@csr.com>");
MODULE_LICENSE("GPL v2");
