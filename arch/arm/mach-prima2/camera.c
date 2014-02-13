/*
 * Define Soc Camera device for CSR SiRFprimaII
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <media/soc_camera.h>
#include <linux/i2c.h>
#include <media/tw9900.h>
#include <media/ch7102.h>

static struct i2c_board_info tvdecoder_i2c_tw9900 = {
	I2C_BOARD_INFO("tw9900", (0x88 >> 1)),
};

static struct tw9900_video_info tw9900_info = {
	.buswidth       = SOCAM_DATAWIDTH_8,
	.mpout          = TW9900_MPO_FIELD,
};

static struct soc_camera_desc tw9900_desc = {
	.host_desc = {
		.bus_id = 0,
		.i2c_adapter_id = 0,
		.board_info = &tvdecoder_i2c_tw9900,
	},

	.subdev_desc = {
		.flags = 0,
		.drv_priv = &tw9900_info,

	},
};

static struct i2c_board_info hdmireceiver_i2c_ch7102 = {
	I2C_BOARD_INFO("ch7102", (0xF0 >> 1)),
};

static struct ch7102_video_info ch7102_info = {
	.buswidth	= SOCAM_DATAWIDTH_8,
};

static struct soc_camera_desc ch7102_desc = {
	.host_desc = {
		.bus_id = 0,
		.i2c_adapter_id = 1,
		.board_info = &hdmireceiver_i2c_ch7102,
	},

	.subdev_desc = {
		.flags = 0,
		.drv_priv = &ch7102_info,

	},
};

static struct platform_device sirfsoc_camera[] = {
	{
		.name	= "soc-camera-pdrv",
		.id	= 0,
		.dev	= {
			.platform_data = &tw9900_desc,
		},
	}, {
		.name	= "soc-camera-pdrv",
		.id	= 1,
		.dev	= {
			.platform_data = &ch7102_desc,
		},
	},
};

static struct platform_device *sirfsoc_camera_pdev[] __initdata = {
	&sirfsoc_camera[0],
	&sirfsoc_camera[1],
};

int __init sirfsoc_add_camera_pdev(void)
{
	return platform_add_devices(sirfsoc_camera_pdev,
		ARRAY_SIZE(sirfsoc_camera_pdev));
}
