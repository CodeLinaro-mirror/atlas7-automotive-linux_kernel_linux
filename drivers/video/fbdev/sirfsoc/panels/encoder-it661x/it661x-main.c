/*
 * CSR sirfsoc hdmi encoder driver
 *
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
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

#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/module.h>
#include <video/sirfsoc_vdss.h>
#include "hdmitx.h"

static LIST_HEAD(encoder_list);
static DEFINE_MUTEX(encoder_list_mutex);

struct encoder_drv_data {
	int video_resolution;
	int video_in_pixel_format;
	int video_out_pixel_format;

	struct i2c_client *client;

	struct list_head list;

	bool state;
};

static int it661x_register_encoder_to_panel(
			struct encoder_drv_data *phdmi)
{
	mutex_lock(&encoder_list_mutex);
	list_add_tail(&phdmi->list, &encoder_list);
	mutex_unlock(&encoder_list_mutex);
	return 0;
}

static void it661x_un_register_encoder_to_panel(
			struct encoder_drv_data *phdmi)
{
	mutex_lock(&encoder_list_mutex);
	list_del(&phdmi->list);
	mutex_unlock(&encoder_list_mutex);
}

struct encoder_drv_data *it661x_get_hdmi_encoder(void)
{
	struct list_head *l;
	struct encoder_drv_data *encoder_data = NULL;

	mutex_lock(&encoder_list_mutex);

	if (list_empty(&encoder_list))
		goto out;

	list_for_each(l, &encoder_list) {
		encoder_data = list_entry(l, struct encoder_drv_data, list);
	}

out:
	mutex_unlock(&encoder_list_mutex);
	if (!encoder_data)
		pr_err("No encoder device found\n");

	return encoder_data;
}
EXPORT_SYMBOL(it661x_get_hdmi_encoder);

unsigned char it661x_read_i2c_byte(unsigned char reg_addr)
{
	s32 val = 0;
	struct encoder_drv_data *encoder_data =
			it661x_get_hdmi_encoder();

	if (encoder_data)
		val = i2c_smbus_read_byte_data(encoder_data->client,
					reg_addr);

	if (val < 0)
		return 0;
	else
		return (unsigned char)val;
}

bool it661x_write_i2c_byte(unsigned char reg_addr, unsigned char data)
{
	s32 val = 0;
	struct encoder_drv_data *encoder_data =
			it661x_get_hdmi_encoder();

	if (encoder_data)
		val = i2c_smbus_write_byte_data(encoder_data->client,
					reg_addr,
					data);

	if (val < 0)
		return false;
	else
		return true;
}

bool it661x_read_i2c_byteN(unsigned char reg_addr,
			unsigned char *pdata, int n)
{
	int i;
	s32 val = 0;
	bool status = true;
	struct encoder_drv_data *encoder_data =
			it661x_get_hdmi_encoder();

	if (encoder_data)
		for (i = 0; i < n; i++) {
			val = i2c_smbus_read_byte_data(encoder_data->client,
					reg_addr + i);
			pdata[i] = (unsigned char)val;
			if (val < 0) {
				status = false;
				break;
			}
		}
	else
		status = false;

	return status;
}

bool it661x_write_i2c_byteN(unsigned char reg_addr,
			unsigned char *pdata, int n)
{
	int i;
	bool status = true;
	struct encoder_drv_data *encoder_data =
			it661x_get_hdmi_encoder();

	if (encoder_data)
		for (i = 0; i < n; i++) {
			if (!i2c_smbus_write_byte_data(encoder_data->client,
						reg_addr + i,
						pdata[i])) {
				status = false;
				break;
			}
		}
	else
		status = false;

	return status;
}

static void it661x_get_chipid(struct i2c_client *client,
			const unsigned char reg_addr)
{
	unsigned char chipid;

	chipid = i2c_smbus_read_byte_data(client, reg_addr);

	if ((chipid >> 5) & 0x01)
		pr_err("it6612 chip\n");
	else
		pr_err("it6611 chip\n");
}

static void it661x_set_display_mode(int video_resolution,
				int video_in_pixel_format,
				int video_out_pixel_format)
{
	enum hdmi_pixel_format srcfmt, dstfmt;

	srcfmt = (enum hdmi_pixel_format)video_in_pixel_format;
	dstfmt = (enum hdmi_pixel_format)video_out_pixel_format;

	switch (video_resolution) {
	case 1:
		it661x_change_display_option(HDMI_480p60, srcfmt, dstfmt);
		break;
	case 2:
		it661x_change_display_option(HDMI_720p60, srcfmt, dstfmt);
		break;
	case 3:
		it661x_change_display_option(HDMI_1080p60, srcfmt, dstfmt);
		break;
	default:
		/* by default, 720p input is enabled*/
		it661x_change_display_option(HDMI_720p60, srcfmt, dstfmt);
		break;
	}
}

int sirfsoc_vdss_panel_enable_encoder(void)
{
	struct encoder_drv_data *encoder_data =
				it661x_get_hdmi_encoder();

	if (encoder_data) {
		if (encoder_data->client)
			it661x_get_chipid(encoder_data->client, 0x5);

		it661x_set_display_mode(encoder_data->video_resolution,
					encoder_data->video_in_pixel_format,
					encoder_data->video_out_pixel_format);

		it661x_init();

		it661x_bring_up();

		pr_err("it661x enable routine is done\n");
	}

	return 0;
}
EXPORT_SYMBOL(sirfsoc_vdss_panel_enable_encoder);

int sirfsoc_vdss_panel_disable_encoder(void)
{
	return 0;
}
EXPORT_SYMBOL(sirfsoc_vdss_panel_disable_encoder);

bool sirfsoc_vdss_panel_find_encoder(void)
{
	struct encoder_drv_data *encoder_data =
				it661x_get_hdmi_encoder();

	if (encoder_data)
		return encoder_data->state;
	else
		return false;
}
EXPORT_SYMBOL(sirfsoc_vdss_panel_find_encoder);

static int it661x_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct encoder_drv_data *encoder_data;
	struct device_node *node = client->dev.of_node;

	encoder_data = devm_kzalloc(&client->dev, sizeof(*encoder_data),
					GFP_KERNEL);
	if (!encoder_data)
		return -ENOMEM;

	encoder_data->client = client;

	i2c_set_clientdata(client, encoder_data);

	of_property_read_u32(node, "video-resolution",
					&encoder_data->video_resolution);
	of_property_read_u32(node, "video-in-pixel-format",
					&encoder_data->video_in_pixel_format);
	of_property_read_u32(node, "video-out-pixel-format",
					&encoder_data->video_out_pixel_format);

	encoder_data->state = true;

	it661x_register_encoder_to_panel(encoder_data);

	dev_err(&client->dev, "it661x probed");

	return 0;
}

static int it661x_remove(struct i2c_client *client)
{
	struct encoder_drv_data *encoder_data =
				it661x_get_hdmi_encoder();

	it661x_un_register_encoder_to_panel(encoder_data);

	dev_err(&client->dev, "it661x removed");
	return 0;
}

static const struct i2c_device_id it661x_id[] = {
	{ "it661x", 0 },
	{}
};

MODULE_DEVICE_TABLE(i2c, it661x_id);

static struct i2c_driver it661x_driver = {
	.driver = {
		.name	= "it661x-encoder",
		.owner	= THIS_MODULE,
	},
	.probe	= it661x_probe,
	.remove	= it661x_remove,
	.id_table = it661x_id,
};

static int __init it661x_encoder_init(void)
{
	return i2c_add_driver(&it661x_driver);
}

static void __exit it661x_encoder_exit(void)
{
	i2c_del_driver(&it661x_driver);
}

subsys_initcall(it661x_encoder_init);
module_exit(it661x_encoder_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HDMI Encoder Driver");
MODULE_AUTHOR("Guchun Chen");
