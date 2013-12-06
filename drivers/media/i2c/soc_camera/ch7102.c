/*
 * CSR SiRFprima2 HDMI receiver driver
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
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

#include <media/soc_camera.h>
#include <media/ch7102.h>
#include <media/v4l2-subdev.h>
#include <linux/platform_device.h>
#include <linux/extcon/extcon-gpio.h>
#include <linux/of_gpio.h>
#include <linux/sysfs.h>

#define WIDTH  1280
#define HEIGHT	720

/* GPIO0,0 used as HPT interrupt */
#define GPIO_INTR 0
/* page selection register: register 00 */
#define PG_SEL	0x00
#define PAGE1	0x00
#define PAGE2	0x01
#define PAGE3	0x02
#define PAGE4	0x03
#define PAGE5	0x04
#define PAGE6	0x05
#define PAGE7	0x06
#define PAGE8	0x07
#define PAGE9	0x08
#define PAGE10	0x09
#define PAGE11	0x0A
#define PAGE12	0x0B

/* chip id :register 0xFE of page 12 */
#define CHIPID	0xFE
/* general status: register 0x05 of page 2 */
#define STATUS	0x05
/* chip control: register 0x19 of page 10 */
#define CONTROL	0x19
/* Audio status info: register 0x16 0f page 2*/
#define AUDIO_INFO	0x16


/*
 * structure
 */

struct ch7102_priv {
	struct v4l2_subdev		subdev;
	struct ch7102_video_info	*info;
	u32				preset;
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
	u8 value = 0;

	if (enable) {
		/*	select page 10 */
		i2c_smbus_write_byte_data(client, PG_SEL, PAGE10);
		value = i2c_smbus_read_byte_data(client, CONTROL);
		/*	enable output	*/
		value |= 0x80;
		i2c_smbus_write_byte_data(client, CONTROL, value);
	} else {
		i2c_smbus_write_byte_data(client, PG_SEL, PAGE10);
		value = i2c_smbus_read_byte_data(client, CONTROL);
		/*	set output to tri-state	*/
		value &= ~0x80;
		i2c_smbus_write_byte_data(client, CONTROL, value);
	}

	return 0;
}

static int ch7102_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);

	return soc_camera_set_power(&client->dev, ssdd, NULL, on);
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
	u8 value = 0;
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

	i2c_smbus_write_byte_data(client, PG_SEL, PAGE10);
	value = i2c_smbus_read_byte_data(client, CONTROL);
	/*	set output to tri-state	*/
	value &= ~0x80;
	i2c_smbus_write_byte_data(client, CONTROL, value);

	/* the preset is just set here and comment, will enable later*/
	/*
	 * priv->preset = V4L2_DV_720P60;
	 */

	return ret;
}

static struct v4l2_subdev_core_ops ch7102_subdev_core_ops = {
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
static struct platform_device *pextcon_dev;
struct gpio_extcon_platform_data hdmi_extcon_data = {
	.name = "hdmi-input",
	.gpio = 0,
	.debounce = 0,
	.irq_flags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
	.state_on = NULL,
	.state_off = NULL,
};

static struct platform_device *sirfsoc_hdmi_extcon_init(void)
{
	struct platform_device *pdev;
	struct gpio_extcon_platform_data *pextcon_data = &hdmi_extcon_data;
	int err = -ENOMEM;
	struct device_node *np = NULL;
	int gpio;

	np = of_find_compatible_node(NULL, NULL, "chrontel,ch7102");
	if (!np)
		goto err_out;

	gpio = of_get_named_gpio(np, "hp-hdmiinput-gpios", 0);
	if (!gpio_is_valid(gpio))
		goto err_out;
	else
		hdmi_extcon_data.gpio = gpio;

	pdev = platform_device_alloc("extcon-gpio", 0);
	if (!pdev)
		goto err_out;

	err = platform_device_add_data(pdev,
		pextcon_data, sizeof(*pextcon_data));
	if (err)
		goto err_out1;

	err = platform_device_add(pdev);
	if (err)
		goto err_out1;

	return pdev;

err_out1:
	platform_device_put(pdev);
err_out:
	return NULL;
}

static int samplerate_table[] = {
	44100, 44100, 48000, 32000, 22050, 44100, 24000, 44100,
	88200, 768000, 96000, 44100, 176400, 44100, 192000, 44100
};

static ssize_t ch7102_audio_rate_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	u8 value = 0;

	i2c_smbus_write_byte_data(client, PG_SEL, PAGE2);
	value = i2c_smbus_read_byte_data(client, AUDIO_INFO);
	value &= 0xf;
	return sprintf(buf, "%d", samplerate_table[value]);
}
static DEVICE_ATTR(ch7102_audio_rate, S_IRUGO,
		ch7102_audio_rate_show, NULL);

static int ch7102_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct ch7102_priv             *priv;
	struct ch7102_video_info       *info;
	struct i2c_adapter             *adapter =
		to_i2c_adapter(client->dev.parent);
	struct soc_camera_subdev_desc   *ssdd = soc_camera_i2c_to_desc(client);

	int ret;

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

	i2c_smbus_write_byte_data(client, PG_SEL, PAGE12);
	if (i2c_smbus_read_byte_data(client, CHIPID) < 0) {
		dev_err(&client->dev,
		"%s:read ch7102 chip id failed\n",
		__func__);
		return -EIO;
	}

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->info   = info;
	v4l2_i2c_subdev_init(&priv->subdev, client, &ch7102_subdev_ops);

	ch7102_client = client;
	pextcon_dev = sirfsoc_hdmi_extcon_init();

	ret = sysfs_create_file(&client->dev.kobj,
		&dev_attr_ch7102_audio_rate.attr);
	if (ret) {
		dev_err(&client->dev,
			"Failed to create audio sample rate sysfs file.\n");
		return ret;
	}

	return ch7102_video_probe(client);
}

static int ch7102_remove(struct i2c_client *client)
{
	sysfs_remove_file(&client->dev.kobj, &dev_attr_ch7102_audio_rate.attr);
	platform_device_unregister(pextcon_dev);

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
