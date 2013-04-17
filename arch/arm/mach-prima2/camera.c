/*
 *  Define Soc Camera device for CSR SiRFprimaII
 *
 *  Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group
 *  company.
 *
 *  Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <media/soc_camera.h>
#include <linux/i2c.h>
#include <media/tw9900.h>

static struct platform_device *sirf_camera_pdev;

static struct i2c_board_info tvdecoder_i2c_tw9900 = {
		I2C_BOARD_INFO("tw9900", (0x88 >> 1)),
};
static struct tw9900_video_info tw9900_info = {
	.buswidth       = SOCAM_DATAWIDTH_8,
	.mpout          = TW9900_MPO_FIELD,
};

struct soc_camera_link tw9900_link = {
	.bus_id		= 0,
	.board_info	= &tvdecoder_i2c_tw9900,
	.i2c_adapter_id = 0,
	.priv		= &tw9900_info,
};

struct soc_camera_desc camera_desc = {
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

static int __init sirfsoc_camera_init(void)
{
	struct platform_device *pdev;
	struct soc_camera_desc *pdata = &camera_desc;
	int err = -ENOMEM;

	pdev = platform_device_alloc("soc-camera-pdrv", -1);
	if (!pdev)
		goto err_out;

	err = platform_device_add_data(pdev, pdata, sizeof(*pdata));
	if (err)
		goto err_out;

	err = platform_device_add(pdev);
	if (err)
		goto err_out;

	sirf_camera_pdev = pdev;
	printk("sirfsoc_camera_init success\n");
	return 0;

err_out:
	printk("sirfsoc_camera_init failed\n");
	platform_device_put(pdev);
	return err;
}
module_init(sirfsoc_camera_init);

static void __exit sirfsoc_camera_exit(void)
{
	platform_device_unregister(sirf_camera_pdev);
}
module_exit(sirfsoc_camera_exit);

MODULE_AUTHOR("Renwei Wu");
MODULE_DESCRIPTION("SiRF camera platform device registration");
MODULE_LICENSE("GPL");
