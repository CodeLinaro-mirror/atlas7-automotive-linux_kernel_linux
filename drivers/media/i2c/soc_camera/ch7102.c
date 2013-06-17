/*
 * ch7102 Video Driver
 *
 * Copyright (C) 2008 Renesas Solutions Corp.
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * Based on ov772x driver,
 *
 * Copyright (C) 2008 Kuninori Morimoto <morimoto.kuninori@renesas.com>
 * Copyright 2006-7 Jonathan Corbet <corbet@lwn.net>
 * Copyright (C) 2008 Magnus Damm
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/v4l2-mediabus.h>
#include <linux/videodev2.h>

#include <media/soc_camera.h>
#include <media/ch7102.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-subdev.h>

#define WIDTH  1280
#define HEIGHT	720

/*
 * structure
 */

struct ch7102_priv {
	struct v4l2_subdev		subdev;
	struct ch7102_video_info	*info;
	u32				preset;
	u32				revision;
};

/*
 * general function
 */
static struct ch7102_priv *to_ch7102(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct ch7102_priv,
			    subdev);
}


/*
 * subdevice operations
 */
static int ch7102_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return 0;
}

static int ch7102_g_chip_ident(struct v4l2_subdev *sd,
			       struct v4l2_dbg_chip_ident *id)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ch7102_priv *priv = to_ch7102(client);

	id->ident = 10;
	id->revision = priv->revision;

	return 0;
}

static int ch7102_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);

	return soc_camera_set_power(&client->dev, ssdd, on);
}

static int ch7102_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ch7102_priv *priv = to_ch7102(client);

	a->c.left	= 0;
	a->c.top	= 0;
	a->c.width	= WIDTH;
	a->c.height	= HEIGHT;
	a->type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return 0;
}

static int ch7102_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ch7102_priv *priv = to_ch7102(client);

	a->bounds.left			= 0;
	a->bounds.top			= 0;
	a->bounds.width			= WIDTH;
	a->bounds.height		= HEIGHT;
	a->defrect                      = a->bounds;
	a->type				= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	return 0;
}

static int ch7102_g_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ch7102_priv *priv = to_ch7102(client);

	mf->width	= WIDTH;
	mf->height	= HEIGHT;
	mf->code	= V4L2_MBUS_FMT_UYVY8_2X8;
	mf->colorspace	= V4L2_COLORSPACE_JPEG;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

static int ch7102_s_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	mf->width	= WIDTH;
	mf->height	= HEIGHT;
	return 0;
}

static int ch7102_try_fmt(struct v4l2_subdev *sd,
			  struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ch7102_priv *priv = to_ch7102(client);

	mf->code = V4L2_MBUS_FMT_UYVY8_2X8;
	mf->colorspace = V4L2_COLORSPACE_JPEG;


	mf->width	= WIDTH;
	mf->height	= HEIGHT;

	return 0;
}

static int ch7102_video_probe(struct i2c_client *client)
{
	struct ch7102_priv *priv = to_ch7102(client);
	int ret;

	/*
	 * ch7102 only use 8 bits bus width
	 */
	if (SOCAM_DATAWIDTH_8  != priv->info->buswidth) {
		dev_err(&client->dev, "bus width error\n");
		return -ENODEV;
	}

	ret = ch7102_s_power(&priv->subdev, 1);
	if (ret < 0)
		return ret;

	/* the preset is just set here and comment, will enable later*/
	/*
	 * priv->preset = V4L2_DV_720P60;
	 */

	return ret;
}

static struct v4l2_subdev_core_ops ch7102_subdev_core_ops = {
	.g_chip_ident	= ch7102_g_chip_ident,
	.s_power	= ch7102_s_power,
};

static int ch7102_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			   enum v4l2_mbus_pixelcode *code)
{
	if (index)
		return -EINVAL;

	*code = V4L2_MBUS_FMT_UYVY8_2X8;
	return 0;
}

static int ch7102_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *cfg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);

	cfg->flags = V4L2_MBUS_MASTER;
	cfg->type = V4L2_MBUS_BT656;
	cfg->flags = soc_camera_apply_board_flags(ssdd, cfg);

	return 0;
}

static int ch7102_s_mbus_config(struct v4l2_subdev *sd,
				const struct v4l2_mbus_config *cfg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);
	unsigned long flags = soc_camera_apply_board_flags(ssdd, cfg);

	return 0;
}

static struct v4l2_subdev_video_ops ch7102_subdev_video_ops = {
	.s_stream	= ch7102_s_stream,
	.g_mbus_fmt	= ch7102_g_fmt,
	.s_mbus_fmt	= ch7102_s_fmt,
	.try_mbus_fmt	= ch7102_try_fmt,
	.cropcap	= ch7102_cropcap,
	.g_crop		= ch7102_g_crop,
	.enum_mbus_fmt	= ch7102_enum_fmt,
	.g_mbus_config  = ch7102_g_mbus_config,
	.s_mbus_config  = ch7102_s_mbus_config,
};

static struct v4l2_subdev_ops ch7102_subdev_ops = {
	.core	= &ch7102_subdev_core_ops,
	.video	= &ch7102_subdev_video_ops,
};

static struct i2c_client *ch7102_client;

static int ch7102_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct ch7102_priv             *priv;
	struct ch7102_video_info       *info;
	struct i2c_adapter             *adapter =
		to_i2c_adapter(client->dev.parent);
	struct soc_camera_subdev_desc   *ssdd = soc_camera_i2c_to_desc(client);

	if (!ssdd || !ssdd->drv_priv) {
		dev_err(&client->dev, "ch7102: missing platform data!\n");
		return -EINVAL;
	}

	info = ssdd->drv_priv;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev,
			"I2C-Adapter doesn't support "
			"I2C_FUNC_SMBUS_BYTE_DATA\n");
		return -EIO;
	}

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->info   = info;

	v4l2_i2c_subdev_init(&priv->subdev, client, &ch7102_subdev_ops);

	ch7102_client = client;

	return ch7102_video_probe(client);
}

static int ch7102_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id ch7102_id[] = {
	{ "ch7102", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ch7102_id);

static struct i2c_driver ch7102_i2c_driver = {
	.driver = {
		.name = "ch7102",
	},
	.probe    = ch7102_probe,
	.remove   = ch7102_remove,
	.id_table = ch7102_id,
};

module_i2c_driver(ch7102_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for ch7102");
MODULE_AUTHOR("Renwei Wu");
MODULE_LICENSE("GPL v2");
