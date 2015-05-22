/*
 * CSR SiRF Atlas7DA Rearview driver
 *
 * Copyright (c) 2011 - 2015 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/kthread.h>
#include <linux/of_gpio.h>
#include <linux/of_address.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include <linux/dma-mapping.h>
#include <linux/input.h>
#include <linux/of_platform.h>
#include <linux/videodev2.h>
#include <linux/workqueue.h>

#include <video/sirfsoc_vdss.h>
#include "vip_capture.h"


#define RV_DRV_NAME		"sirf,rearview"


#define FRAME_WIDTH_DEFAULT	720
#define FRAME_HEIGHT_DEFAULT	480

#define FRAME_SIZE		(rv->width * rv->height * 2)
#define DATA_DMA_SIZE		(3 * FRAME_SIZE)
#define TABLE_DMA_SIZE		(1 * SZ_32)

#define DMA_FLAG_NORMAL		(1 << 25)
#define DMA_FLAG_PAUSE		(2 << 25)
#define DMA_FLAG_LOOP		(3 << 25)
#define DMA_FLAG_END		(4 << 25)
#define DMA_SET_LENGTH(x)	(((x) >> 2) - 1)

#define DMA_TABLE_1_LOW		(rv->table_virt_addr + 0)
#define DMA_TABLE_1_HIGH	(rv->table_virt_addr + 4)
#define DMA_TABLE_2_LOW		(rv->table_virt_addr + 8)
#define DMA_TABLE_2_HIGH	(rv->table_virt_addr + 12)
#define DMA_TABLE_3_LOW		(rv->table_virt_addr + 16)
#define DMA_TABLE_3_HIGH	(rv->table_virt_addr + 20)
#define DMA_TABLE_4_LOW		(rv->table_virt_addr + 24)

#define IPC_MSG_RV_MASK		BIT(31)


struct display_info {
	char		display[16];
	struct vdss_rect	src_rect;
	struct vdss_rect	sca_rect;
	struct vdss_rect	dst_rect;
	struct sirfsoc_vdss_panel *panel;
	struct sirfsoc_vdss_layer *l;
	struct sirfsoc_vdss_screen *scn;
};

struct rv_dev {
	struct device	*dev;

	unsigned int	width;
	unsigned int	height;

	int		ipc_irq;

	bool		running;
	struct mutex	hw_lock;

	bool		mirror_en;

	atomic_t	value;
	struct work_struct rv_work;

	void		*rv_vip;
	void		*rv_vpp;

	v4l2_std_id	source_std;
	struct display_info d_info;

	dma_addr_t	data_dma_addr, table_dma_addr;
	void		*data_virt_addr, *table_virt_addr;

	void __iomem	*ipc_int_addr, *ipc_msg_addr;
};


static bool rv_input_filter(struct input_handle *handle,
	unsigned int type, unsigned int code, int value)
{
	struct rv_dev *rv = handle->private;

	/*
	* We use KEY_CAMERA to switch rearview on or off
	* If rearview is running we suppress ABS and KEY(except POWER KEY)
	* events, and bypass all other input events,
	* so user will be forbidden to click touch panel or press non-power key
	*/
	switch (type) {
	case EV_ABS:
		return rv->running;

	case EV_KEY:
		switch (code) {
		case KEY_CAMERA:
			atomic_set(&rv->value, value);
			schedule_work(&rv->rv_work);
		case KEY_POWER:
			return false;

		default:
			return rv->running;
		}

	default:
		return false;
	}
}

static int rv_input_connect(struct input_handler *handler,
	struct input_dev *dev,
	const struct input_device_id *id)
{
	struct input_handle *handle;
	int err;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "rearview";
	handle->private = handler->private;

	err = input_register_handle(handle);
	if (err) {
		dev_err(&dev->dev, "Failed to register input handle: %d\n",
			err);
		goto err_free;
	}

	err = input_open_device(handle);
	if (err) {
		dev_err(&dev->dev, "Failed to open input device: %d\n", err);
		goto err_unregister;
	}

	return 0;

 err_unregister:
	input_unregister_handle(handle);
 err_free:
	kfree(handle);
	return err;
}

static void rv_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id rv_input_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			INPUT_DEVICE_ID_MATCH_KEYBIT,
		.evbit = { BIT_MASK(EV_KEY) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
	},
	{},
};

static struct input_handler rv_input_handler = {
	.filter		= rv_input_filter,
	.connect	= rv_input_connect,
	.disconnect	= rv_input_disconnect,
	.name		= "rearview",
	.id_table	= rv_input_ids,
};

static inline void rv_input_register(struct rv_dev *rv)
{
	int err;

	rv_input_handler.private = rv;

	err = input_register_handler(&rv_input_handler);
	if (err)
		dev_err(rv->dev, "Register input handler failed %d", err);
}

static inline void rv_input_unregister(void)
{
	input_unregister_handler(&rv_input_handler);
}


static inline void rv_init_dma_table(struct rv_dev *rv)
{
	writel(DMA_FLAG_PAUSE | DMA_SET_LENGTH(FRAME_SIZE), DMA_TABLE_1_LOW);
	writel(rv->data_dma_addr, DMA_TABLE_1_HIGH);

	writel(DMA_FLAG_PAUSE | DMA_SET_LENGTH(FRAME_SIZE), DMA_TABLE_2_LOW);
	writel(rv->data_dma_addr + FRAME_SIZE, DMA_TABLE_2_HIGH);

	writel(DMA_FLAG_PAUSE | DMA_SET_LENGTH(FRAME_SIZE), DMA_TABLE_3_LOW);
	writel(rv->data_dma_addr + 2*FRAME_SIZE, DMA_TABLE_3_HIGH);

	writel(DMA_FLAG_PAUSE, DMA_TABLE_4_LOW);
}

static inline void rv_set_dma_table_run(struct rv_dev *rv)
{
	writel(DMA_FLAG_NORMAL | DMA_SET_LENGTH(FRAME_SIZE), DMA_TABLE_1_LOW);
	writel(DMA_FLAG_NORMAL | DMA_SET_LENGTH(FRAME_SIZE), DMA_TABLE_2_LOW);
	writel(DMA_FLAG_NORMAL | DMA_SET_LENGTH(FRAME_SIZE), DMA_TABLE_3_LOW);
	writel(DMA_FLAG_LOOP, DMA_TABLE_4_LOW);
}

static inline void rv_set_dma_table_stop(struct rv_dev *rv)
{
	writel(DMA_FLAG_END, DMA_TABLE_1_LOW);
	writel(DMA_FLAG_END, DMA_TABLE_2_LOW);
	writel(DMA_FLAG_END, DMA_TABLE_3_LOW);
	writel(DMA_FLAG_END, DMA_TABLE_4_LOW);
}

static int rv_setup_dma(struct rv_dev *rv)
{
	int ret = 0;

	ret = dma_set_coherent_mask(rv->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(rv->dev, "set dma coherent mask error\n");
		return ret;
	}

	if (!rv->dev->dma_mask)
		rv->dev->dma_mask = &rv->dev->coherent_dma_mask;
	else
		dma_set_mask(rv->dev, DMA_BIT_MASK(32));

	rv->data_virt_addr = dma_alloc_coherent(rv->dev,
				DATA_DMA_SIZE + TABLE_DMA_SIZE,
				&rv->data_dma_addr, GFP_KERNEL);
	if (rv->data_virt_addr == NULL) {
		dev_err(rv->dev, "can't alloc dma memory\n");
		return -ENOMEM;
	}

	rv->table_dma_addr = rv->data_dma_addr + DATA_DMA_SIZE;
	rv->table_virt_addr = rv->data_virt_addr + DATA_DMA_SIZE;

	rv_init_dma_table(rv);

	return ret;
}

static int rv_panel_match(struct sirfsoc_vdss_panel *panel, void *data)
{
	if (strcmp(panel->alias, data) == 0)
		return 1;
	else
		return 0;
}

static int rv_get_display_info(struct rv_dev *rv)
{
	if (!sirfsoc_vdss_is_initialized())
		return -ENXIO;

	rv->d_info.panel = sirfsoc_vdss_find_panel(rv->d_info.display,
							rv_panel_match);
	if (!rv->d_info.panel)
		rv->d_info.panel = sirfsoc_vdss_get_primary_device();

	if (!rv->d_info.panel) {
		dev_err(rv->dev, "find panel failed\n");
		return	-ENODEV;
	}

	rv->d_info.scn = sirfsoc_vdss_find_screen_from_panel(rv->d_info.panel);

	if (!rv->d_info.scn) {
		dev_err(rv->dev, "no screen for the panel\n");
		return -ENODEV;
	}

	rv->d_info.l = sirfsoc_vdss_get_layer_from_screen(rv->d_info.scn,
									true);

	if (!rv->d_info.l) {
		dev_err(rv->dev, "no layer for rearview");
		return -EBUSY;
	}

	/* full source capture full screen display */
	rv->d_info.src_rect.left	= 0;
	rv->d_info.src_rect.top		= 0;
	rv->d_info.src_rect.right	= rv->width;
	rv->d_info.src_rect.bottom	= rv->height;

	rv->d_info.sca_rect.left	= 0;
	rv->d_info.sca_rect.top		= 0;
	rv->d_info.sca_rect.right	= rv->d_info.panel->timings.xres;
	rv->d_info.sca_rect.bottom	= rv->d_info.panel->timings.yres;

	rv->d_info.dst_rect.left	= 0;
	rv->d_info.dst_rect.top		= 0;
	rv->d_info.dst_rect.right	= rv->d_info.panel->timings.xres;
	rv->d_info.dst_rect.bottom	= rv->d_info.panel->timings.yres;

	return	0;
}

/* gpio and ipc(CAN) won't cross control rearview */
static irqreturn_t rv_ipc_irq_handler(int irq, void *data)
{
	struct rv_dev *rv = data;

	/* clear interrupt flag */
	readl(rv->ipc_int_addr);

	atomic_set(&rv->value, readl(rv->ipc_msg_addr) & IPC_MSG_RV_MASK);
	schedule_work(&rv->rv_work);

	return IRQ_HANDLED;
}

static void rv_start(struct rv_dev *rv)
{
	struct vip_rv_info rv_info = {0};
	struct vdss_vpp_op_params vpp_op_params = {0};
	struct vdss_vpp_create_device_params vpp_dev_params = {0};
	struct sirfsoc_vdss_layer_info info;

	/* vip setting */
	rv_info.std		= rv->source_std;
	rv_info.rv_vip		= rv->rv_vip;
	rv_info.mirror_en	= rv->mirror_en;
	rv_info.match_addrs[0]	= rv->data_dma_addr + FRAME_SIZE - 128;
	rv_info.match_addrs[1]	= rv->data_dma_addr + 2*FRAME_SIZE - 128;
	rv_info.match_addrs[2]	= rv->data_dma_addr + 3*FRAME_SIZE - 128;
	rv_info.dma_table_addr	= rv->table_dma_addr;
	vip_rv_config(&rv_info);

	/* start vip dma */
	rv_set_dma_table_run(rv);

	/* start vip */
	vip_rv_start(rv->rv_vip);

	/* lcd layer setting */
	rv->d_info.l->get_info(rv->d_info.l, &info);

	info.base = 0;
	info.passthrough = true;

	info.src_rect.left = rv->d_info.sca_rect.left;
	info.src_rect.top = rv->d_info.sca_rect.top;
	info.src_rect.right = rv->d_info.sca_rect.right - 1;
	info.src_rect.bottom = rv->d_info.sca_rect.bottom - 1;

	info.dst_rect.left = rv->d_info.dst_rect.left;
	info.dst_rect.top = rv->d_info.dst_rect.top;
	info.dst_rect.right = rv->d_info.dst_rect.right - 1;
	info.dst_rect.bottom = rv->d_info.dst_rect.bottom - 1;

	info.fmt = VPP_TO_LCD_PIXELFORMAT;
	info.surf_width = info.src_rect.right - info.src_rect.left;
	info.surf_height = info.src_rect.bottom - info.src_rect.top;

	rv->d_info.l->set_info(rv->d_info.l, &info);
	rv->d_info.l->screen->apply(rv->d_info.l->screen);

	/* disable all other layers */
	sirfsoc_vdss_set_exclusive_layers(&rv->d_info.l, 1, true);

	/* vpp setting */
	vpp_dev_params.func = NULL;
	vpp_dev_params.arg = NULL;
	rv->rv_vpp = sirfsoc_vpp_create_device(SIRFSOC_VDSS_VPP0,
							&vpp_dev_params);

	vpp_op_params.type = VPP_OP_IBV;
	vpp_op_params.op.ibv.src_id =
		((struct vip_dev *)rv->rv_vip)->is_atlas7_vip0 ?
				SIRFSOC_VDSS_VIP0_EXT : SIRFSOC_VDSS_VIP1_EXT;
	vpp_op_params.op.ibv.src_size	= 3;

	vpp_op_params.op.ibv.interlace.interlaced = true;
	vpp_op_params.op.ibv.interlace.field_offset = FRAME_SIZE/2;
	vpp_op_params.op.ibv.interlace.di_top = false;
	vpp_op_params.op.ibv.interlace.out_mode = VDSS_P_SINGLE;
	#ifdef CONFIG_VERTICAL_MEDIAN
	vpp_op_params.op.ibv.interlace.di_mode = VDSS_VPP_DI_VMRI;
	#elif defined(CONFIG_MEAVE)
	vpp_op_params.op.ibv.interlace.di_mode = VDSS_VPP_DI_WEAVE;
	#elif defined(CONFIG_CONFIG_3TAP_MEDIAN)
	vpp_op_params.op.ibv.interlace.di_mode = VDSS_VPP_3MEDIAN;
	#else
	vpp_op_params.op.ibv.interlace.di_mode = VDSS_VPP_DI_VMRI;
	#endif
	vpp_op_params.op.ibv.interlace.input_top_first = true;
	vpp_op_params.op.ibv.interlace.output_top_first = false;

	vpp_op_params.op.ibv.src_rect.left = rv->d_info.src_rect.left;
	vpp_op_params.op.ibv.src_rect.top = rv->d_info.src_rect.top;
	vpp_op_params.op.ibv.src_rect.right = rv->d_info.src_rect.right - 1;
	vpp_op_params.op.ibv.src_rect.bottom = rv->d_info.src_rect.bottom - 1;

	vpp_op_params.op.ibv.dst_rect.left = rv->d_info.sca_rect.left;
	vpp_op_params.op.ibv.dst_rect.top = rv->d_info.sca_rect.top;
	vpp_op_params.op.ibv.dst_rect.right = rv->d_info.sca_rect.right - 1;
	vpp_op_params.op.ibv.dst_rect.bottom = rv->d_info.sca_rect.bottom - 1;

	vpp_op_params.op.ibv.src_surf[0].fmt = VDSS_PIXELFORMAT_YVYU;
	vpp_op_params.op.ibv.src_surf[0].width = rv->width;
	vpp_op_params.op.ibv.src_surf[0].height = rv->height;
	vpp_op_params.op.ibv.src_surf[0].base = rv->data_dma_addr;
	vpp_op_params.op.ibv.src_surf[1].base = rv->data_dma_addr
								+ 1*FRAME_SIZE;
	vpp_op_params.op.ibv.src_surf[2].base = rv->data_dma_addr
								+ 2*FRAME_SIZE;

	/* start vpp */
	sirfsoc_vpp_present(rv->rv_vpp, &vpp_op_params);

	/* start lcd layer */
	if (!rv->d_info.l->is_enabled(rv->d_info.l))
		rv->d_info.l->enable(rv->d_info.l);
}

static void rv_stop(struct rv_dev *rv)
{
	/* stop lcd layer */
	if (rv->d_info.l->is_enabled(rv->d_info.l))
		rv->d_info.l->disable(rv->d_info.l);

	/* stop vpp */
	sirfsoc_vpp_destroy_device(rv->rv_vpp);

	/* enable all other layers */
	sirfsoc_vdss_set_exclusive_layers(&rv->d_info.l, 1, false);

	/* stop vip dma */
	rv_set_dma_table_stop(rv);

	/* stop vip */
	vip_rv_stop(rv->rv_vip);
}

static void rv_worker(struct work_struct *work)
{
	struct rv_dev *rv = container_of(work, struct rv_dev, rv_work);

	mutex_lock(&rv->hw_lock);
	if (atomic_read(&rv->value) && !rv->running) {
		rv_start(rv);
		rv->running = true;
	}
	if (!atomic_read(&rv->value) && rv->running) {
		rv_stop(rv);
		rv->running = false;
	}
	mutex_unlock(&rv->hw_lock);
}

static int rv_probe(struct platform_device *pdev)
{
	struct device_node *rv_vip_np, *node = pdev->dev.of_node;
	struct platform_device *rv_vip_pdev;
	struct device *dev = &pdev->dev;
	struct rv_dev *rv = NULL;
	const char *std_name, *display_name;
	unsigned char mirror = 0;
	struct resource	*res;
	int ret = 0;

	rv = devm_kzalloc(dev, sizeof(*rv), GFP_KERNEL);
	if (!rv) {
		ret = -ENOMEM;
		goto exit;
	}

	/*
	* The CAN stack is running on M3, M3 will filter the CAN FRAMES
	* and detect CAN messages of "Rearview start/stop".
	* So the Rearview driver needn't  VIRTIO_CAN and parse CAN frame.
	* M3 exports 2 addresses to Rearview driver, one for read-to-clear
	* dedicated interrupt flag and one for decoded message value.
	*/
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rv->ipc_int_addr = devm_ioremap_resource(dev, res);
	if (!rv->ipc_int_addr) {
		dev_err(dev, "fail to ioremap ipc int regs\n");
		ret = -ENOMEM;
		goto exit;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	rv->ipc_msg_addr = devm_ioremap_resource(dev, res);
	if (!rv->ipc_msg_addr) {
		dev_err(dev, "fail to ioremap msg int regs\n");
		ret = -ENOMEM;
		goto exit;
	}

	/*
	* Because Rearview CAN can be triggered in uboot and Linux scenes,
	* and for time delay sensitive reason, they don't use the RPMSG to send
	* trigger message, adopt dedicated irq instead.
	*/
	rv->ipc_irq = platform_get_irq(pdev, 0);
	if (!rv->ipc_irq) {
		dev_err(dev, "fail to get ipc irq\n");
		ret = -EINVAL;
		goto exit;
	}

	of_property_read_u8(node, "mirror", &mirror);
	of_property_read_string(node, "source-std", &std_name);
	of_property_read_string(node, "display-panel", &display_name);

	rv_vip_np = of_find_compatible_node(NULL, NULL, "sirf,rv-vip");
	if (!rv_vip_np) {
		dev_err(dev, "can't find rearview vip\n");
		ret = -ENODEV;
		goto exit;
	}

	rv_vip_pdev = of_find_device_by_node(rv_vip_np);
	if (!rv_vip_pdev) {
		dev_err(dev, "can't find rearview vip pdev\n");
		ret = -ENODEV;
		of_node_put(rv_vip_np);
		goto exit;
	}

	device_lock(&rv_vip_pdev->dev);
	rv->rv_vip = platform_get_drvdata(rv_vip_pdev);
	device_unlock(&rv_vip_pdev->dev);

	of_node_put(rv_vip_np);

	rv->dev		= dev;
	rv->mirror_en	= mirror ? true : false;
	rv->running	= false;
	mutex_init(&rv->hw_lock);
	strncpy(rv->d_info.display, display_name, sizeof(rv->d_info.display));

	if (strcmp(std_name, "NTSC") == 0)
		rv->source_std = V4L2_STD_NTSC;
	if (strcmp(std_name, "PAL") == 0)
		rv->source_std = V4L2_STD_PAL;

	if (rv->source_std == V4L2_STD_NTSC) {
		rv->width	= 720;
		rv->height	= 480;
	} else if (rv->source_std == V4L2_STD_PAL) {
		rv->width	= 720;
		rv->height	= 576;
	} else {
		rv->width	= FRAME_WIDTH_DEFAULT;
		rv->height	= FRAME_HEIGHT_DEFAULT;
	}

	ret = rv_setup_dma(rv);
	if (ret) {
		dev_err(dev, "set memory error\n");
		goto exit;
	}

	ret = rv_get_display_info(rv);
	if (ret) {
		dev_err(dev, "get display info error\n");
		goto exit;
	}

	INIT_WORK(&rv->rv_work, rv_worker);

	ret = devm_request_irq(dev, rv->ipc_irq, rv_ipc_irq_handler,
				IRQF_TRIGGER_NONE, "rearview_ipc_switch", rv);
	if (ret) {
		dev_err(dev, "cannot request ipc irq for rearview switch\n");
		ret = -EINVAL;
		goto exit;
	}

	platform_set_drvdata(pdev, rv);

	rv_input_register(rv);

	pr_info("rearview start on %s\n", display_name);

	return 0;

exit:
	return ret;
}

static int rv_remove(struct platform_device *pdev)
{
	struct rv_dev *rv = platform_get_drvdata(pdev);

	rv_input_unregister();

	mutex_lock(&rv->hw_lock);
	if (rv->running) {
		rv_stop(rv);
		rv->running = false;
	}
	mutex_unlock(&rv->hw_lock);

	dma_free_coherent(rv->dev, DATA_DMA_SIZE + TABLE_DMA_SIZE,
				rv->data_virt_addr, rv->data_dma_addr);

	pr_info("rv_remove done\n");

	return 0;
}


static const struct of_device_id rv_match_tbl[] = {
	{ .compatible = "sirf,rearview", },
	{},
};

static struct platform_driver rv_driver = {
	.driver	= {
		.name = RV_DRV_NAME,
		.of_match_table = rv_match_tbl,
	},
	.probe = rv_probe,
	.remove = rv_remove,
};

static int __init sirfsoc_rv_init(void)
{
	platform_driver_register(&rv_driver);
}

static void __exit sirfsoc_rv_exit(void)
{
	platform_driver_unregister(&rv_driver);
}

subsys_initcall_sync(sirfsoc_rv_init);
module_exit(sirfsoc_rv_exit);


MODULE_DESCRIPTION("SIRFSoC Atlas7 Rearview driver");
MODULE_AUTHOR("Bin SUN <Andy.Sun@csr.com>");
MODULE_LICENSE("GPL v2");
