/*
 * CSR SiRF Atlas7DA Rearview driver
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

#define VIDEO_BRIGHTNESS_MAX 128
#define VIDEO_BRIGHTNESS_MIN (-128)

#define VIDEO_CONTRAST_MAX 256
#define VIDEO_CONTRAST_MIN 0

#define VIDEO_HUE_MAX 360
#define VIDEO_HUE_MIN 0

#define VIDEO_SATURATION_MAX 1026
#define VIDEO_SATURATION_MIN 0

#define REARVIEW_LAYER		SIRFSOC_VDSS_LAYER3
#define REARVIEW_AUXILIARY_LAYER SIRFSOC_VDSS_LAYER2

struct display_info {
	char		display[16];
	struct vdss_rect	src_rect;
	struct vdss_rect	sca_rect;
	struct vdss_rect	dst_rect;
	struct sirfsoc_vdss_panel *panel;
	struct sirfsoc_vdss_layer *l;		/* rearviw data layer */
	struct sirfsoc_vdss_screen *scn;

#ifdef CONFIG_REARVIEW_AUXILIARY
	struct sirfsoc_vdss_layer *aux_l;	/* rearviw auxiliary layer */
	/* auxiliary layer might take over from other layer, need restore */
	struct sirfsoc_vdss_layer_info saved_l_info;
	enum vdss_layer	saved_toplayer;
#endif
};

struct rv_dev {
	struct device	*dev;

	unsigned int	width;
	unsigned int	height;

	int		ipc_irq;

	bool		running;
	struct mutex	hw_lock;

	struct vdss_vpp_colorctrl color_ctrl;
	enum vdss_deinterlace_mode di_mode;

	bool		mirror_en;

	atomic_t	value;
	struct work_struct rv_work;
	struct workqueue_struct *rv_wq;

	void		*rv_vip;
	void		*rv_vpp;

	v4l2_std_id	source_std;
	struct display_info d_info;

	dma_addr_t	data_dma_addr, table_dma_addr;
	void		*data_virt_addr, *table_virt_addr;

#ifdef CONFIG_REARVIEW_AUXILIARY
	dma_addr_t	aux_dma_addr;
	void		*aux_virt_addr;
	unsigned int	aux_bytesperlength, aux_size;
#endif

	void __iomem	*ipc_int_addr, *ipc_msg_addr;
};


static ssize_t rv_enabled_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct rv_dev *rv = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", rv->running);
}

static ssize_t rv_enabled_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int r;
	bool e;
	struct rv_dev *rv = dev_get_drvdata(dev);

	r = strtobool(buf, &e);
	if (r)
		return r;

	atomic_set(&rv->value, e);
	queue_work(rv->rv_wq, &rv->rv_work);

	return size;
}

static void rv_setup_color(struct rv_dev *rv)
{
	struct vdss_vpp_op_params vpp_op_params = {0};

	vpp_op_params.type = VPP_OP_IBV;

	/*vpp color ctrl*/
	vpp_op_params.op.ibv.color_update_only = true;
	vpp_op_params.op.ibv.color_ctrl = rv->color_ctrl;

	/* start vpp */
	sirfsoc_vpp_present(rv->rv_vpp, &vpp_op_params);
}

static ssize_t rv_brightness_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct rv_dev *rv = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", rv->color_ctrl.brightness);
}

static ssize_t rv_brightness_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int r, val;
	struct rv_dev *rv = dev_get_drvdata(dev);

	r = kstrtoint(buf, 0, &val);
	if (r)
		return r;

	rv->color_ctrl.brightness = clamp(val, (s32)VIDEO_BRIGHTNESS_MIN,
			(s32)VIDEO_BRIGHTNESS_MAX);
	rv_setup_color(rv);

	return size;
}

static ssize_t rv_contrast_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct rv_dev *rv = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", rv->color_ctrl.contrast);
}

static ssize_t rv_contrast_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int r, val;
	struct rv_dev *rv = dev_get_drvdata(dev);

	r = kstrtoint(buf, 0, &val);
	if (r)
		return r;

	rv->color_ctrl.contrast = clamp(val, (s32)VIDEO_CONTRAST_MIN,
			(s32)VIDEO_CONTRAST_MAX);
	rv_setup_color(rv);

	return size;
}

static ssize_t rv_hue_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct rv_dev *rv = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", rv->color_ctrl.hue);
}

static ssize_t rv_hue_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int r, val;
	struct rv_dev *rv = dev_get_drvdata(dev);

	r = kstrtoint(buf, 0, &val);
	if (r)
		return r;

	rv->color_ctrl.hue = clamp(val, (s32)VIDEO_HUE_MIN,
			(s32)VIDEO_HUE_MAX);
	rv_setup_color(rv);

	return size;
}

static ssize_t rv_saturation_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct rv_dev *rv = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", rv->color_ctrl.saturation);
}

static ssize_t rv_saturation_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int r, val;
	struct rv_dev *rv = dev_get_drvdata(dev);

	r = kstrtoint(buf, 0, &val);
	if (r)
		return r;

	rv->color_ctrl.saturation = clamp(val, (s32)VIDEO_SATURATION_MIN,
			(s32)VIDEO_SATURATION_MAX);
	rv_setup_color(rv);

	return size;
}

static ssize_t rv_di_mode_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct rv_dev *rv = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", rv->di_mode);
}

static ssize_t rv_di_mode_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int r, val;
	struct rv_dev *rv = dev_get_drvdata(dev);

	r = kstrtoint(buf, 0, &val);
	if (r)
		return r;

	if (val < VDSS_VPP_DI_RESERVED || val > VDSS_VPP_DI_VMRI)
		return -EINVAL;

	rv->di_mode = (enum vdss_deinterlace_mode)val;

	return size;
}

static DEVICE_ATTR(enabled, S_IRUGO|S_IWUSR,
				rv_enabled_show, rv_enabled_store);
static DEVICE_ATTR(brightness, S_IRUGO|S_IWUSR,
				rv_brightness_show, rv_brightness_store);
static DEVICE_ATTR(contrast, S_IRUGO|S_IWUSR,
				rv_contrast_show, rv_contrast_store);
static DEVICE_ATTR(hue, S_IRUGO|S_IWUSR,
				rv_hue_show, rv_hue_store);
static DEVICE_ATTR(saturation, S_IRUGO|S_IWUSR,
				rv_saturation_show, rv_saturation_store);
static DEVICE_ATTR(di_mode, S_IRUGO|S_IWUSR,
				rv_di_mode_show, rv_di_mode_store);


static const struct attribute *rv_sysfs_attrs[] = {
	&dev_attr_enabled.attr,
	&dev_attr_brightness.attr,
	&dev_attr_contrast.attr,
	&dev_attr_hue.attr,
	&dev_attr_saturation.attr,
	&dev_attr_di_mode.attr,
	NULL
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
			queue_work(rv->rv_wq, &rv->rv_work);
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
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
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
	int ret = 0, xres, yres;

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

#ifdef CONFIG_REARVIEW_AUXILIARY
	/* alloc panel resolution@ARGB8888 format buffer */
	xres = rv->d_info.panel->timings.xres;
	yres = rv->d_info.panel->timings.yres;
	rv->aux_bytesperlength = xres * 4;
	rv->aux_size = yres * rv->aux_bytesperlength;

	rv->aux_virt_addr = dma_alloc_coherent(rv->dev, rv->aux_size,
						&rv->aux_dma_addr, GFP_KERNEL);
	if (rv->aux_virt_addr == NULL) {
		dev_err(rv->dev, "can't alloc aux memory\n");
		return -ENOMEM;
	}
#endif

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
				REARVIEW_LAYER, true);

	if (!rv->d_info.l) {
		dev_err(rv->dev, "no layer for rearview");
		return -EBUSY;
	}

	/* full source capture full screen display */
	rv->d_info.src_rect.left	= 0;
	rv->d_info.src_rect.top		= 0;
	rv->d_info.src_rect.right	= rv->width - 1;
	rv->d_info.src_rect.bottom	= rv->height - 1;

	rv->d_info.sca_rect.left	= 0;
	rv->d_info.sca_rect.top		= 0;
	rv->d_info.sca_rect.right	= rv->d_info.panel->timings.xres - 1;
	rv->d_info.sca_rect.bottom	= rv->d_info.panel->timings.yres - 1;

	rv->d_info.dst_rect.left	= 0;
	rv->d_info.dst_rect.top		= 0;
	rv->d_info.dst_rect.right	= rv->d_info.panel->timings.xres - 1;
	rv->d_info.dst_rect.bottom	= rv->d_info.panel->timings.yres - 1;

	return	0;
}

/* gpio and ipc(CAN) won't cross control rearview */
static irqreturn_t rv_ipc_irq_handler(int irq, void *data)
{
	struct rv_dev *rv = data;

	/* clear interrupt flag */
	readl(rv->ipc_int_addr);

	atomic_set(&rv->value, readl(rv->ipc_msg_addr) & IPC_MSG_RV_MASK);
	queue_work(rv->rv_wq, &rv->rv_work);

	return IRQ_HANDLED;
}

#ifdef CONFIG_REARVIEW_AUXILIARY

static void rv_aux_fillrect(struct rv_dev *rv, struct vdss_rect *rect,
							void *color, int len)
{
	int i, j;

	for (j = rect->top; j < rect->bottom; j++)
		for (i = rect->left; i < rect->right; i++)
			memcpy(rv->aux_virt_addr + j * rv->aux_bytesperlength
						+ i * len, color, len);
}

/*
* Draw distance alarm lines on another overlay.
* We haven't plan to maintain it in future, so hard code here, sorry.
*/
static void rv_aux_drawline(struct rv_dev *rv)
{
	struct vdss_rect rect;
	int xres, yres, i;

	unsigned int red32 = 0x00ff0000;
	unsigned int yellow32 = 0x00ffff00;
	unsigned int green32 = 0x0000ff00;
	unsigned int transparent_ratio = CONFIG_AUXILIARY_TRANSPARENT_VALUE;

	/* integrating alpha value to the colors */
	red32		|= ((100 - transparent_ratio) * 0xFF / 100) << 24;
	yellow32	|= ((100 - transparent_ratio) * 0xFF / 100) << 24;
	green32		|= ((100 - transparent_ratio) * 0xFF / 100) << 24;

	xres = rv->d_info.panel->timings.xres;
	yres = rv->d_info.panel->timings.yres;

	/* green lines drawing */
	rect.left	= xres * 5 / 16;
	rect.right	= xres * 7 / 16 - 20;
	rect.top	= yres * 5 / 8;
	rect.bottom	= yres * 5 / 8 + 7;
	rv_aux_fillrect(rv, &rect, &green32, 4);
	rect.left	= xres * 9 / 16 + 20;
	rect.right	= xres * 11 / 16;
	rect.top	= yres * 5 / 8;
	rect.bottom	= yres * 5 / 8 + 7;
	rv_aux_fillrect(rv, &rect, &green32, 4);
	for (i = 0; i < 17; i++) {
		rect.left	= xres * 5 / 16 - i;
		rect.right	= xres * 5 / 16 + 9 - i;
		rect.top	= yres * 5 / 8 + i;
		rect.bottom	= yres * 5 / 8 + i + 1;
		rv_aux_fillrect(rv, &rect, &green32, 4);

		rect.left	= xres * 11 / 16 - 9 + i;
		rect.right	= xres * 11 / 16 + i;
		rect.top	= yres * 5 / 8 + i;
		rect.bottom	= yres * 5 / 8 + i + 1;
		rv_aux_fillrect(rv, &rect, &green32, 4);
	}
	for (i = 27; i < 37; i++) {
		rect.left	= xres * 5 / 16 - i;
		rect.right	= xres * 5 / 16 + 9 - i;
		rect.top	= yres * 5 / 8 + i;
		rect.bottom	= yres * 5 / 8 + i + 1;
		rv_aux_fillrect(rv, &rect, &green32, 4);

		rect.left	= xres * 11 / 16 - 9 + i;
		rect.right	= xres * 11 / 16 + i;
		rect.top	= yres * 5 / 8 + i;
		rect.bottom	= yres * 5 / 8 + i + 1;
		rv_aux_fillrect(rv, &rect, &green32, 4);
	}

	/* yellow lines drawing */
	i = 47;
	rect.left	= xres * 5 / 16 - i;
	rect.right	= xres * 7 / 16 - i;
	rect.top	= yres * 5 / 8 + i;
	rect.bottom	= yres * 5 / 8 + i + 7;
	rv_aux_fillrect(rv, &rect, &yellow32, 4);
	rect.left	= xres * 9 / 16 + i;
	rect.right	= xres * 11 / 16 + i;
	rect.top	= yres * 5 / 8 + i;
	rect.bottom	= yres * 5 / 8 + i + 7;
	rv_aux_fillrect(rv, &rect, &yellow32, 4);
	for (i = 47; i < 72; i++) {
		rect.left	= xres * 5 / 16 - i;
		rect.right	= xres * 5 / 16 + 9 - i;
		rect.top	= yres * 5 / 8 + i;
		rect.bottom	= yres * 5 / 8 + i + 1;
		rv_aux_fillrect(rv, &rect, &yellow32, 4);

		rect.left	= xres * 11 / 16 - 9 + i;
		rect.right	= xres * 11 / 16 + i;
		rect.top	= yres * 5 / 8 + i;
		rect.bottom	= yres * 5 / 8 + i + 1;
		rv_aux_fillrect(rv, &rect, &yellow32, 4);
	}
	for (i = 82; i < 107; i++) {
		rect.left	= xres * 5 / 16 - i;
		rect.right	= xres * 5 / 16 + 9 - i;
		rect.top	= yres * 5 / 8 + i;
		rect.bottom	= yres * 5 / 8 + i + 1;
		rv_aux_fillrect(rv, &rect, &yellow32, 4);

		rect.left	= xres * 11 / 16 - 9 + i;
		rect.right	= xres * 11 / 16 + i;
		rect.top	= yres * 5 / 8 + i;
		rect.bottom	= yres * 5 / 8 + i + 1;
		rv_aux_fillrect(rv, &rect, &yellow32, 4);
	}

	/* red lines drawing */
	i = 117;
	rect.left	= xres * 5 / 16 - i;
	rect.right	= xres * 7 / 16 - i + 40;
	rect.top	= yres * 5 / 8 + i;
	rect.bottom	= yres * 5 / 8 + i + 7;
	rv_aux_fillrect(rv, &rect, &red32, 4);
	rect.left	= xres * 9 / 16 + i - 40;
	rect.right	= xres * 11 / 16 + i;
	rect.top	= yres * 5 / 8 + i;
	rect.bottom	= yres * 5 / 8 + i + 7;
	rv_aux_fillrect(rv, &rect, &red32, 4);
	for (i = 117; i < 157; i++) {
		rect.left	= xres * 5 / 16 - i;
		rect.right	= xres * 5 / 16 + 9 - i;
		rect.top	= yres * 5 / 8 + i;
		rect.bottom	= yres * 5 / 8 + i + 1;
		rv_aux_fillrect(rv, &rect, &red32, 4);

		rect.left	= xres * 11 / 16 - 9 + i;
		rect.right	= xres * 11 / 16 + i;
		rect.top	= yres * 5 / 8 + i;
		rect.bottom	= yres * 5 / 8 + i + 1;
		rv_aux_fillrect(rv, &rect, &red32, 4);
	}
	for (i = 167; i < 207; i++) {
		rect.left	= xres * 5 / 16 - i;
		rect.right	= xres * 5 / 16 + 9 - i;
		rect.top	= yres * 5 / 8 + i;
		rect.bottom	= yres * 5 / 8 + i + 1;
		rv_aux_fillrect(rv, &rect, &red32, 4);

		rect.left	= xres * 11 / 16 - 9 + i;
		rect.right	= xres * 11 / 16 + i;
		rect.top	= yres * 5 / 8 + i;
		rect.bottom	= yres * 5 / 8 + i + 1;
		rv_aux_fillrect(rv, &rect, &red32, 4);
	}
}

static int rv_auxiliary_start(struct rv_dev *rv)
{
	struct sirfsoc_vdss_layer *l;
	struct sirfsoc_vdss_layer *active_layers[2]; /* rearview & auxiliary */
	struct sirfsoc_vdss_layer_info info;
	struct sirfsoc_vdss_screen_info sinfo;

	rv->d_info.aux_l = sirfsoc_vdss_get_layer_from_screen(rv->d_info.scn,
				REARVIEW_AUXILIARY_LAYER, false);
	if (!rv->d_info.aux_l) {
		dev_err(rv->dev, "no layer for rearview auxiliary");
		return -EBUSY;
	}

	/* set all the layer data to 0, default transparent 100% */
	memset(rv->aux_virt_addr, 0, rv->aux_size);

	/* unlock rearview & auxiliary layers, disable and lock other layers */
	sirfsoc_vdss_set_exclusive_layers(&rv->d_info.l, 1, false);
	active_layers[0] = rv->d_info.l;
	active_layers[1] = rv->d_info.aux_l;
	sirfsoc_vdss_set_exclusive_layers(&active_layers[0], 2, true);

	/* get the layer which used for auxiliary original info and backup */
	l = rv->d_info.aux_l;
	l->get_info(l, &info);
	rv->d_info.saved_l_info = info;

	/* apply rearview auxiliary layer setting */
	info.src_surf.base = rv->aux_dma_addr;
	info.disp_mode = VDSS_DISP_NORMAL;

	info.src_rect.left = 0;
	info.src_rect.top = 0;
	info.src_rect.right = rv->d_info.panel->timings.xres - 1;
	info.src_rect.bottom = rv->d_info.panel->timings.yres - 1;

	info.dst_rect.left = 0;
	info.dst_rect.top = 0;
	info.dst_rect.right =  rv->d_info.panel->timings.xres - 1;
	info.dst_rect.bottom = rv->d_info.panel->timings.yres - 1;
	info.src_surf.fmt = VDSS_PIXELFORMAT_8888;

	info.src_surf.width = rv->d_info.panel->timings.xres;
	info.src_surf.height = rv->d_info.panel->timings.yres;

	info.global_alpha	= false;
	info.ckey_on		= false;
	info.dst_ckey_on	= false;
	info.pre_mult_alpha	= false;
	info.source_alpha	= true;

	l->set_info(l, &info);
	l->screen->apply(l->screen);
	l->enable(l);

	/* get the original toplayer info and backup */
	l->screen->get_info(l->screen, &sinfo);
	rv->d_info.saved_toplayer = sinfo.top_layer;

	/* set the auxiliary layer to the new toplayer */
	sinfo.top_layer = l->id;
	l->screen->set_info(l->screen, &sinfo);
	l->screen->apply(l->screen);

	/* start to draw distance alarm lines */
	rv_aux_drawline(rv);

	return 0;
}

static void rv_auxiliary_stop(struct rv_dev *rv)
{
	struct sirfsoc_vdss_layer *l = rv->d_info.aux_l;
	struct sirfsoc_vdss_screen_info sinfo;

	if (!l)
		return;

	/* remove the distance alarm lines */
	memset(rv->aux_virt_addr, 0, rv->aux_size);

	/* disable auxiliary layer and restore its original setting */
	l->disable(l);
	l->set_info(l, &rv->d_info.saved_l_info);
	l->screen->apply(l->screen);

	/* restore original toplayer setting */
	l->screen->get_info(l->screen, &sinfo);
	sinfo.top_layer = rv->d_info.saved_toplayer;
	l->screen->set_info(l->screen, &sinfo);
	l->screen->apply(l->screen);
}

#endif

static void rv_callback_from_vpp(void *arg,
					enum vdss_vpp id,
					enum vdss_vpp_op_type type)
{
	struct rv_dev *rv = (struct rv_dev *)arg;

	/*
	* If rearview preempts vpp passthrough case successfully,
	* vpp will disable video layer firstly, but the background appears,
	* so it will cause screen flash, to avoid this issue,
	* we disable all the active layer at the beginning.
	*/
	if (type != VPP_OP_IBV)
		sirfsoc_vdss_set_exclusive_layers(&rv->d_info.l, 1, true);

}

static void rv_start(struct rv_dev *rv)
{
	struct vip_rv_info rv_info = {0};
	struct vdss_vpp_op_params vpp_op_params = {0};
	struct vdss_vpp_create_device_params vpp_dev_params = {0};
	struct sirfsoc_vdss_layer_info info;
	struct vdss_surface src_surf;
	int src_skip, dst_skip;

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

	/* if mirror enabled, line buffer will disorder the pixel data */
	if (rv->mirror_en)
		src_surf.fmt = VDSS_PIXELFORMAT_YVYU;
	else
		src_surf.fmt = VDSS_PIXELFORMAT_YUYV;

	src_surf.width = rv->width;
	src_surf.height = rv->height;
	src_surf.base = 0;

	if (!sirfsoc_vdss_check_size(VDSS_DISP_IBV, &src_surf,
	    &rv->d_info.src_rect, &src_skip, rv->d_info.l,
	    &rv->d_info.sca_rect, &dst_skip)) {
		dev_err(rv->dev, "vdss check size failed");
		return;
	}

	/* lcd layer setting */
	rv->d_info.l->get_info(rv->d_info.l, &info);

	info.src_surf.base = 0;
	info.disp_mode = VDSS_DISP_IBV;

	info.src_rect = rv->d_info.sca_rect;
	info.dst_rect = rv->d_info.dst_rect;
	info.line_skip = dst_skip;

	info.src_surf.fmt = VPP_TO_LCD_PIXELFORMAT;
	info.src_surf.width = rv->d_info.sca_rect.right -
		rv->d_info.sca_rect.left + 1;
	info.src_surf.height = rv->d_info.sca_rect.bottom -
		rv->d_info.sca_rect.top + 1;

	rv->d_info.l->set_info(rv->d_info.l, &info);
	rv->d_info.l->screen->apply(rv->d_info.l->screen);

	/* vpp setting */
	vpp_dev_params.func = rv_callback_from_vpp;
	vpp_dev_params.arg = rv;
	/* passthrough mode: VPP0->LCDC0, VPP1->LCDC1, default use VPP0 */
	if (!strcmp(rv->d_info.display, "display1"))
		rv->rv_vpp = sirfsoc_vpp_create_device(SIRFSOC_VDSS_VPP1,
							&vpp_dev_params);
	else
		rv->rv_vpp = sirfsoc_vpp_create_device(SIRFSOC_VDSS_VPP0,
							&vpp_dev_params);

	vpp_op_params.type = VPP_OP_IBV;
	vpp_op_params.op.ibv.src_id =
		is_cvd_vip((struct vip_dev *)rv->rv_vip) ?
				SIRFSOC_VDSS_VIP0_EXT : SIRFSOC_VDSS_VIP1_EXT;
	vpp_op_params.op.ibv.src_size	= 3;

	vpp_op_params.op.ibv.interlace.interlaced = true;
	vpp_op_params.op.ibv.interlace.field_offset = FRAME_SIZE/2;
	vpp_op_params.op.ibv.interlace.di_top = false;
	vpp_op_params.op.ibv.interlace.out_mode = VDSS_P_SINGLE;
	vpp_op_params.op.ibv.interlace.di_mode = rv->di_mode;
	vpp_op_params.op.ibv.interlace.input_top_first = true;
	vpp_op_params.op.ibv.interlace.output_top_first = false;

	vpp_op_params.op.ibv.src_rect = rv->d_info.src_rect;
	vpp_op_params.op.ibv.dst_rect = rv->d_info.sca_rect;

	vpp_op_params.op.ibv.src_surf[0] = src_surf;
	vpp_op_params.op.ibv.src_surf[0].base = rv->data_dma_addr;
	vpp_op_params.op.ibv.src_surf[1] = src_surf;
	vpp_op_params.op.ibv.src_surf[1].base = rv->data_dma_addr
								+ 1*FRAME_SIZE;
	vpp_op_params.op.ibv.src_surf[2] = src_surf;
	vpp_op_params.op.ibv.src_surf[2].base = rv->data_dma_addr
								+ 2*FRAME_SIZE;

	/*vpp color ctrl*/
	vpp_op_params.op.ibv.color_update_only = false;
	vpp_op_params.op.ibv.color_ctrl = rv->color_ctrl;

	/* start vpp */
	sirfsoc_vpp_present(rv->rv_vpp, &vpp_op_params);

	/* start vip */
	vip_rv_start(rv->rv_vip);

	/* enable lcd rearview layer */
	if (!rv->d_info.l->is_enabled(rv->d_info.l))
		rv->d_info.l->enable(rv->d_info.l);

	/* disable lcd other layers */
	sirfsoc_vdss_set_exclusive_layers(&rv->d_info.l, 1, true);

#ifdef CONFIG_REARVIEW_AUXILIARY
	rv_auxiliary_start(rv);
#endif
}

static void rv_stop(struct rv_dev *rv)
{
#ifdef CONFIG_REARVIEW_AUXILIARY
	rv_auxiliary_stop(rv);
#endif

	/* stop lcd layer */
	if (rv->d_info.l->is_enabled(rv->d_info.l))
		rv->d_info.l->disable(rv->d_info.l);

	/* stop vpp */
	sirfsoc_vpp_destroy_device(rv->rv_vpp);
	rv->rv_vpp = NULL;
	rv->d_info.l->screen->wait_for_vsync(rv->d_info.l->screen);

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
	unsigned int mirror = 0;
	struct resource	*res;
	v4l2_std_id std;
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

	of_property_read_u32(node, "mirror", &mirror);
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

	/* set NTSC format as default */
	rv->source_std = V4L2_STD_NTSC;

	if (strcmp(std_name, "NTSC") == 0)
		rv->source_std = V4L2_STD_NTSC;
	if (strcmp(std_name, "PAL") == 0)
		rv->source_std = V4L2_STD_PAL;
	if (strcmp(std_name, "AUTO") == 0) {
		std = vip_rv_querystd(rv->rv_vip);
		if (std & (V4L2_STD_NTSC | V4L2_STD_NTSC_443))
			rv->source_std = V4L2_STD_NTSC;
		if (std & (V4L2_STD_PAL | V4L2_STD_PAL_Nc))
			rv->source_std = V4L2_STD_PAL;
	}

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

	ret = rv_get_display_info(rv);
	if (ret) {
		dev_err(dev, "get display info error\n");
		goto exit;
	}

	ret = rv_setup_dma(rv);
	if (ret) {
		dev_err(dev, "set memory error\n");
		goto exit;
	}

	rv->rv_wq = create_workqueue("rearview_workqueue");
	if (!rv->rv_wq) {
		dev_err(dev, "create workqueue failed\n");
		goto exit;
	}

	INIT_WORK(&rv->rv_work, rv_worker);

	ret = devm_request_irq(dev, rv->ipc_irq, rv_ipc_irq_handler,
				IRQF_TRIGGER_NONE, "rearview_ipc_switch", rv);
	if (ret) {
		dev_err(dev, "cannot request ipc irq for rearview switch\n");
		ret = -EINVAL;
		goto wq_exit;
	}

	platform_set_drvdata(pdev, rv);

	/* set default colors */
	rv->color_ctrl.brightness = 0;
	rv->color_ctrl.contrast = 128;
	rv->color_ctrl.hue = 0;
	rv->color_ctrl.saturation = 128;

#ifdef CONFIG_VERTICAL_MEDIAN
	rv->di_mode = VDSS_VPP_DI_VMRI;
#elif defined(CONFIG_MEAVE)
	rv->di_mode = VDSS_VPP_DI_WEAVE;
#elif defined(CONFIG_CONFIG_3TAP_MEDIAN)
	rv->di_mode = VDSS_VPP_3MEDIAN;
#else
	rv->di_mode = VDSS_VPP_DI_VMRI;
#endif

	/* rearview might start here, must be put after default colors set */
	rv_input_register(rv);

	ret = sysfs_create_files(&dev->kobj, rv_sysfs_attrs);
	if (ret) {
		dev_err(dev, "failed to create sysfs files\n");
		goto wq_exit;
	}

	pr_info("rearview start on %s\n", display_name);

	return 0;

wq_exit:
	destroy_workqueue(rv->rv_wq);
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

#ifdef CONFIG_REARVIEW_AUXILIARY
	dma_free_coherent(rv->dev, rv->aux_size,
				rv->aux_virt_addr, rv->aux_dma_addr);
#endif

	sysfs_remove_files(&rv->dev->kobj, rv_sysfs_attrs);

	destroy_workqueue(rv->rv_wq);

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
	return platform_driver_register(&rv_driver);
}

static void __exit sirfsoc_rv_exit(void)
{
	platform_driver_unregister(&rv_driver);
}

subsys_initcall_sync(sirfsoc_rv_init);
module_exit(sirfsoc_rv_exit);


MODULE_DESCRIPTION("SIRFSoC Atlas7 Rearview driver");
MODULE_LICENSE("GPL v2");
