/*
 * CSR SiRFprima2 Rearview implementation
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/fb.h>
#include <linux/console.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/dmaengine.h>
#include <linux/module.h>
#include <linux/input.h>

#include <asm/dma.h>

#include "rearview.h"

#define BLT_DI_METHOD BLT_DI_NONE

#define ACTIVE_NULL	0
#define ACTIVE_INT	1
#define ACTIVE_HIBER	2
#define ACTIVE_SLEEP	3

DECLARE_WAIT_QUEUE_HEAD(rearview_event);

struct rearview_state rv = {
	.active = ACTIVE_NULL,
	.lock = __SPIN_LOCK_UNLOCKED(lock),
};

struct rearview_setting rearview_env;
struct sirfsocfb_bltparms blt_params;

static int prev_field = -1;	/* 0: odd field, 1: even field */
static int cur_field = -1;	/* 0: odd field, 1: even field */
static int cur_frame;	/* indicate index of the frame in the dma buffer */

static atomic_t rv_started = ATOMIC_INIT(0);


#ifdef CONFIG_INPUT

struct rearview_input_priv {
	struct input_handle handle;
};

static bool rearview_input_filter(struct input_handle *handle,
	unsigned int type, unsigned int code, int value)
{
	bool suppress;

	if ((atomic_read(&rv_started) == 0))
		return false;

	switch (type) {
	case EV_ABS:
		suppress = true;
		break;
	case EV_KEY:
		if (code != KEY_POWER)
			suppress = true;
		else
			suppress = false;
		break;
	default:
		suppress = false;
		break;
	}

	return suppress;

}

static int rearview_input_connect(struct input_handler *handler,
	struct input_dev *dev,
	const struct input_device_id *id)
{
	struct rearview_input_priv *priv;
	int err;

	pr_debug("connect to input device %s\n", dev->name);
	priv = kzalloc(sizeof(struct rearview_input_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->handle.dev = dev;
	priv->handle.handler = handler;
	priv->handle.name = "rearview";
	priv->handle.private = priv;

	err = input_register_handle(&priv->handle);
	if (err) {
		pr_err("Failed to register input rearview handler, error %d\n",
			err);
		goto err_free;
	}

	err = input_open_device(&priv->handle);
	if (err) {
		pr_err("Failed to open input device, error %d\n", err);
		goto err_unregister;
	}

	return 0;

 err_unregister:
	input_unregister_handle(&priv->handle);
 err_free:
	kfree(priv);
	return err;
}

static void rearview_input_disconnect(struct input_handle *handle)
{
	struct rearview_input_priv *priv = handle->private;

	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(priv);
}

static const struct input_device_id rearview_input_ids[] = {
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

static struct input_handler rearview_input_handler = {
	.filter		= rearview_input_filter,
	.connect	= rearview_input_connect,
	.disconnect	= rearview_input_disconnect,
	.name		= "rearview",
	.id_table	= rearview_input_ids,
};

static bool rearview_input_registered;

static inline void rearview_input_register(void)
{
	int err;

	err = input_register_handler(&rearview_input_handler);
	if (err)
		pr_err("Failed to register input handler, error %d", err);
	else
		rearview_input_registered = true;
}

static inline void rearview_input_unregister(void)
{
	if (rearview_input_registered) {
		input_unregister_handler(&rearview_input_handler);
		rearview_input_registered = false;
	}
}

#else

static inline void rearview_input_register(void)
{
}

static inline void rearview_input_unregister(void)
{
}

#endif

static bool rearview_enabled(void)
{
	return (atomic_read(&rv_started) != 0);
}

static irqreturn_t rearview_switch_irq_handler(int irq, void *data)
{
	pr_debug("%s\n", __func__);
	spin_lock(&rv.lock);
	rv.active = ACTIVE_INT;
	spin_unlock(&rv.lock);

	wake_up_interruptible(&rearview_event);

	return IRQ_HANDLED;
}

static void rearview_dma_callback(void *data)
{
	struct fb_info *info = rearview_env.fbi;

	blt_params.src.base = rearview_env.dma_addr +
		cur_frame * (rearview_env.dma_buf_span * 2);

	blt_params.dst.base = rearview_env.fb_addr;

	if (cur_field == 1)
		blt_params.flag &= ~BLT_BOT_FIELD_FIRST;
	else
		blt_params.flag |= BLT_BOT_FIELD_FIRST;

	sirfsocfb_km_blt_yuv2rgb(info, &blt_params);

	cur_frame++;
	cur_frame %= 2;
}

static irqreturn_t rearview_vip_irq_handler(int irq, void *data)
{
	u32 status;

	status = rearview_env.vip_funcs->pfnGetInterrupts();
	rearview_env.vip_funcs->pfnClearInterrupts(status);

	if (status & VIP_INTMASK_SENSOR) {
		prev_field = cur_field;
		cur_field = rearview_env.vip_funcs->pfnGetFID();
		if (prev_field != -1 && cur_field == prev_field)
			pr_err("vip bad field sequence\n");
	}

	if (status & VIP_INTMASK_FIFO_OFLOW)
		pr_err("vip FIFO overflow happens\n");

	if (status & VIP_INTMASK_FIFO_UFLOW)
		pr_err("vip FIFO underflow happens\n");

	return IRQ_HANDLED;
}

static void __init_fbdev(void)
{
	struct fb_info *info = rearview_env.fbi;
	struct module *owner;

	pr_debug("%s\n", __func__);

	if (rearview_env.fb_opened)
		return;

	owner = info->fbops->owner;
	if (!try_module_get(owner)) {
		pr_err("%s: cannot get framebuffer module\n", __func__);
		goto error_get;
	}

	if (info->fbops->fb_open != NULL) {
		int res;
		res = info->fbops->fb_open(info, 0);
		if (res != 0) {
			pr_err("%s: cannot open framebuffer\n", __func__);
			goto error_open;
		}
		sirfsocfb_km_enable_feature_layer(info, REARVIEW_FEATURE_LAYER);
		rearview_env.fb_opened = true;
	}

	return;

error_open:
	module_put(owner);
error_get:
	return;
}

static void __deinit_fbdev(void)
{
	struct fb_info *info = rearview_env.fbi;
	struct module *owner;

	pr_debug("%s\n", __func__);

	if (!rearview_env.fb_opened)
		return;

	owner = info->fbops->owner;

	sirfsocfb_km_disable_feature_layer(info);
	if (info->fbops->fb_release != NULL)
		(void)info->fbops->fb_release(info, 0);
	rearview_env.fb_opened = false;

	module_put(owner);
}


#ifdef REARVIEW_AUXILIARY

static void rv_aux_drawline(void)
{
	struct fb_info *info = rearview_env.aux_fbi;
	void *fb_addr = rearview_env.aux_fb_addr;
	int i, j;

	unsigned short red16 = 0xf800;
	unsigned short yellow16 = 0xffe0;
	unsigned short green16 = 0x07e0;

	unsigned int red32 = 0xffff0000;
	unsigned int yellow32 = 0xffffff00;
	unsigned int green32 = 0xff00ff00;

	switch (info->var.bits_per_pixel) {
	case 16:
		for (j = info->var.yres*5/8; j < info->var.yres*5/8 + 5; j++)
			for (i = info->var.xres/4; i < info->var.xres*3/4; i++)
				memcpy(fb_addr + j*info->fix.line_length + i*2, &green16, 2);
		for (j = info->var.yres*6/8; j < info->var.yres*6/8 + 5; j++)
			for (i = info->var.xres/4; i < info->var.xres*3/4; i++)
				memcpy(fb_addr + j*info->fix.line_length + i*2, &yellow16, 2);
		for (j = info->var.yres*7/8; j < info->var.yres*7/8 + 5; j++)
			for (i = info->var.xres/4; i < info->var.xres*3/4; i++)
				memcpy(fb_addr + j*info->fix.line_length + i*2, &red16, 2);
		break;
	case 32:
		for (j = info->var.yres*5/8; j < info->var.yres*5/8 + 5; j++)
			for (i = info->var.xres/4; i < info->var.xres*3/4; i++)
				memcpy(fb_addr + j*info->fix.line_length + i*4, &green32, 4);
		for (j = info->var.yres*6/8; j < info->var.yres*6/8 + 5; j++)
			for (i = info->var.xres/4; i < info->var.xres*3/4; i++)
				memcpy(fb_addr + j*info->fix.line_length + i*4, &yellow32, 4);
		for (j = info->var.yres*7/8; j < info->var.yres*7/8 + 5; j++)
			for (i = info->var.xres/4; i < info->var.xres*3/4; i++)
				memcpy(fb_addr + j*info->fix.line_length + i*4, &red32, 4);
		break;
	default:
		pr_err("%s: bpp %d not supported\n", __func__,
			info->var.bits_per_pixel);
		return;
	}
}

static void rearview_auxiliary_start(void)
{
	struct fb_info *info = rearview_env.aux_fbi;
	void *fb_addr = rearview_env.aux_fb_addr;

	struct module *owner;
	struct fb_var_screeninfo var;
	struct sirfsocfb_colorkeys ckey;
	int i, colorkey = 0x1002;

	pr_debug("%s\n", __func__);

	owner = info->fbops->owner;
	if (!try_module_get(owner)) {
		pr_err("%s: cannot get framebuffer module\n", __func__);
		goto error_get;
	}

	if (info->fbops->fb_open != NULL) {
		int ret;
		ret = info->fbops->fb_open(info, 0);
		if (ret != 0) {
			pr_err("%s: cannot open framebuffer\n", __func__);
			goto error_open;
		}
	}

	/* if bpp equals 16, use colorkey mode; equals 32, take alpha
	 * blending */
	switch (info->var.bits_per_pixel) {
	case 16:
		ckey.enable = 1;
		ckey.color_key_big = colorkey;
		ckey.color_key_small = colorkey;
		info->fbops->fb_ioctl(info, SIRFSOCFB_SET_COLORKEYS, &ckey);
		for (i = 0; i < info->var.yres * info->fix.line_length; i += 2)
			memcpy(fb_addr + i, &colorkey, 2);
		break;
	case 32:
		memset(fb_addr, 0, info->var.yres * info->fix.line_length);
		break;
	default:
		pr_err("%s: bpp %d not supported\n", __func__,
			info->var.bits_per_pixel);
		goto error_open;
	}

	var = info->var;
	var.yoffset = 0;
	var.activate = FB_ACTIVATE_NOW;
	fb_set_var(info, &var);

	info->fbops->fb_ioctl(info, SIRFSOCFB_ENABLE_LAYER, 0);
	info->fbops->fb_ioctl(info, SIRFSOCFB_SET_TOPLAYER, 0);

	rv_aux_drawline();
	return;

error_open:
	module_put(owner);
error_get:
	return;
}

static void rearview_auxiliary_stop(void)
{
	struct fb_info *info = rearview_env.aux_fbi;
	void *fb_addr = rearview_env.aux_fb_addr;
	struct module *owner;

	pr_debug("%s\n", __func__);

	owner = info->fbops->owner;

	memset(fb_addr, 0, info->var.yres * info->fix.line_length);
	info->fbops->fb_ioctl(info, SIRFSOCFB_DISABLE_LAYER, 0);
	if (info->fbops->fb_release != NULL)
		info->fbops->fb_release(info, 0);

	module_put(owner);
}

#endif

static void rearview_start(int lightweight)
{
	VIP_FUNCTIONTABLE *funcs = rearview_env.vip_funcs;
	struct dma_async_tx_descriptor *rx_desc;
	int ret;

	pr_info("%s, lightweight = %d\n", __func__, lightweight);

	prev_field = -1;
	cur_field = -1;
	cur_frame = 0;

	/* to save original VIP context. */
	rearview_env.save_vip_context(rearview_env.data);

	/* set up vip & dma interrupts. */
	ret = request_irq(rearview_env.vip_irq, rearview_vip_irq_handler,
		0, "rearview_vip", &rearview_env);

	if (ret) {
		pr_err("%s: request vip irq error.\n", __func__);
		return;
	}

	clk_prepare_enable(rearview_env.vip_clk);

	funcs->pfnInitialize(rearview_env.base, NULL);

	if (!lightweight) {
		funcs->pfnSetParams(&rearview_env.vip_params);

		if (rearview_env.pdata && rearview_env.pdata->init) {
			ret = rearview_env.pdata->init(rearview_env.vip_dev);
			if (ret) {
				pr_err("%s: vip init error.\n", __func__);
				return;
			}
		}

		if (rearview_env.pdata && rearview_env.pdata->power) {
			ret = rearview_env.pdata->power(rearview_env.vip_dev,
							1);
			if (ret) {
				pr_err("%s: vip power on error.\n", __func__);
				return;
			}
		}

		if (rearview_env.pdata && rearview_env.pdata->reset) {
			ret = rearview_env.pdata->reset(rearview_env.vip_dev);
			if (ret) {
				pr_err("%s: vip reset error.\n", __func__);
				return;
			}
		}

		/* start tvdecoder device */
		rearview_env.decoder_ops->start(INPUT_CVBS_AIN1);
	}

	/* set vip dma to work in loop mode */
	rx_desc = dmaengine_prep_dma_cyclic(rearview_env.dma_chan,
					rearview_env.dma_addr,
					4 * SRC_WIDTH * SRC_HEIGHT,
					2 * SRC_WIDTH * SRC_HEIGHT,
					DMA_DEV_TO_MEM,
					0);
	rx_desc->callback = rearview_dma_callback;

	dmaengine_submit(rx_desc);
	dma_async_issue_pending(rearview_env.dma_chan);

	funcs->pfnStart(0);
#if 0
	funcs->pfnPrintRegister();
#endif
	__init_fbdev();

#ifdef REARVIEW_AUXILIARY
	rearview_auxiliary_start();
#endif
}

static void rearview_stop(void)
{
	int ret;

	pr_info("%s\n", __func__);

	rearview_env.decoder_ops->stop();

	free_irq(rearview_env.vip_irq, &rearview_env);
	rearview_env.vip_funcs->pfnStop();
	dmaengine_terminate_all(rearview_env.dma_chan);
	rearview_env.vip_funcs->pfnTerminate();

	if (rearview_env.pdata && rearview_env.pdata->power) {
		ret = rearview_env.pdata->power(rearview_env.vip_dev, 0);
		if (ret)
			pr_err("%s: vip power off  error.\n", __func__);
	}

	if (rearview_env.pdata && rearview_env.pdata->release) {
		ret = rearview_env.pdata->release(rearview_env.vip_dev);
		if (ret)
			pr_err("%s: vip release error.\n", __func__);
	}

	clk_disable_unprepare(rearview_env.vip_clk);

	/* to restore original VIP context. */
	rearview_env.restore_vip_context(rearview_env.data);

#ifdef REARVIEW_AUXILIARY
	rearview_auxiliary_stop();
#endif

	__deinit_fbdev();

	pr_debug("%s: leaving\n", __func__);
}

static int rearview_freeze(void)
{
	pr_debug("%s: rv_started %d\n", __func__, atomic_read(&rv_started));

	disable_irq(rearview_env.irq);

	if (atomic_read(&rv_started) > 0) {
		rearview_stop();
		atomic_set(&rv_started, 0);
	}

	return 0;
}

static int rearview_restore(void)
{
	unsigned long flags;

	pr_debug("%s: rv_started %d\n", __func__, atomic_read(&rv_started));

	enable_irq(rearview_env.irq);

	spin_lock_irqsave(&rv.lock, flags);
	rv.active = ACTIVE_HIBER;
	spin_unlock_irqrestore(&rv.lock, flags);
	wake_up_interruptible(&rearview_event);

	return 0;
}

static int rearview_suspend(void)
{
	pr_debug("%s\n", __func__);

	if (atomic_read(&rv_started) > 0) {
		free_irq(rearview_env.vip_irq, &rearview_env);
		rearview_env.vip_funcs->pfnStop();
		dmaengine_terminate_all(rearview_env.dma_chan);
		rearview_env.vip_funcs->pfnTerminate();
		clk_disable_unprepare(rearview_env.vip_clk);
		rearview_env.restore_vip_context(rearview_env.data);
		memset(rearview_env.fbi->screen_base, 0,
			rearview_env.fbi->fix.smem_len);
	}

	return 0;
}

static int rearview_resume(void)
{
	unsigned long flags;

	pr_debug("%s\n", __func__);

	spin_lock_irqsave(&rv.lock, flags);
	rv.active = ACTIVE_SLEEP;
	spin_unlock_irqrestore(&rv.lock, flags);
	wake_up_interruptible(&rearview_event);

	return 0;
}

static void rearview_init(void)
{
	int ret;
	int i;
	VIP_PARAMS *params;
	struct fb_info *info = NULL;

	pr_debug("%s\n", __func__);

	ret = gpio_request(rearview_env.gpio, "rearview_switch");
	if (ret) {
		pr_err("%s: gpio_request error for rearview switch\n",
			__func__);
		return;

	}

	/* set up rearview switch interrupt */
	ret = request_irq(rearview_env.irq, rearview_switch_irq_handler,
		IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING, "rearview_switch",
		&rearview_env);

	if (ret) {
		pr_err("%s: cannot request irq for rearview switch\n",
			__func__);
		return;
	}

	pr_info("rearview switch value %d\n",
		gpio_get_value(rearview_env.gpio));

	rearview_env.dma_buf_span = (SRC_HEIGHT / 2) * SRC_WIDTH * 2;

	/* init vip pmrmas */
	params = &rearview_env.vip_params;

	params->SrcRect.left = 0;
	params->SrcRect.top = 0;
	params->SrcRect.right = params->SrcRect.left +
		SRC_WIDTH * 2 - 1;
	params->SrcRect.bottom = params->SrcRect.top +
		SRC_HEIGHT / 2 - 1;

	params->eSrcFormat = SRC_PXLFORMAT;
	params->eDstFormat = LCD_PIXELFORMAT_UNKNOWN;

	params->uiFlag |= VIP_CTRL_CCIR656_EN;

	/* Vip multiplex USB0 for atlas6 */
#ifdef CONFIG_ARCH_ATLAS6
	params->uiFlag |= VIP_CTRL_PAD_MUX_UPLI;
#endif

	params->PixelBitSelect = rearview_env.pdata->sirfsoc_camera_pixel_shift;

	/* init tvdecoder device */
	rearview_env.decoder_ops->init();

	/* init vpp & lcd through fb interface */
	for (i = 0; i < FB_MAX; i++)
		if (registered_fb[i] != NULL)
			info = registered_fb[i];
		else
			break;

	if (info == NULL) {
		pr_err("%s: cannot find fb for rearview function\n", __func__);
		return;
	}

	pr_info("%s: fb %d is used for rearview function\n", __func__, i - 1);

	rearview_env.fbi = info;
	rearview_env.fb_addr = info->fix.smem_start;

#ifdef REARVIEW_AUXILIARY
	if (info == registered_fb[2] || registered_fb[2] == NULL) {
		pr_err("%s: cannot find fb for rearview auxiliary function\n", __func__);
		return;
	}

	rearview_env.aux_fbi = registered_fb[2];
	rearview_env.aux_fb_addr = registered_fb[2]->screen_base;
#endif
	/*
	 * init vpp blt params, dma_buffer -> overlay_buffer,
	 * deinterlaced
	 */

	blt_params.src.width = SRC_WIDTH;
	blt_params.src.height = SRC_HEIGHT;
	blt_params.src.fmt = FORMAT_CrYCbY_422_I;
	blt_params.src.rect.left = 0;
	blt_params.src.rect.top = 0;
	blt_params.src.rect.right = SRC_WIDTH;
	blt_params.src.rect.bottom = SRC_HEIGHT;
	if (BLT_DI_METHOD == BLT_DI_NONE)
		blt_params.src.rect.bottom = SRC_HEIGHT / 2;

	blt_params.dst.width = info->var.xres;
	blt_params.dst.height = info->var.yres;
	if (info->var.bits_per_pixel == 16)
		blt_params.dst.fmt = FORMAT_RGB_565;
	else if (info->var.bits_per_pixel == 32)
		blt_params.dst.fmt = FORMAT_BGRA_8888;
	blt_params.dst.rect.left = 0;
	blt_params.dst.rect.top = 0;
	blt_params.dst.rect.right = info->var.xres;
	blt_params.dst.rect.bottom = info->var.yres;

	blt_params.flag = (blt_params.flag & ~BLT_DI_MODE_MASK) |
		BLT_DI_METHOD;

	blt_params.flag |= BLT_NOT_WAIT_COMPLETE;

	rearview_input_register();
}

static void rearview_deinit(void)
{
	pr_debug("%s\n", __func__);

	rearview_input_unregister();

	if (atomic_read(&rv_started) > 0) {
		rearview_stop();
		atomic_set(&rv_started, 0);
	}

	rearview_env.decoder_ops->deinit();

	free_irq(rearview_env.irq, &rearview_env);

	gpio_free(rearview_env.gpio);
}

int rearview_thread(void *data)
{
	unsigned long flags;
	int need_break;
	struct sirfsoc_camera_dev *pcdev = data;
	int state = ACTIVE_INT;

	pr_info("%s\n", __func__);

	rearview_env.vip_funcs = &pcdev->vip_funcs;
	rearview_env.base = pcdev->base;
	rearview_env.vip_irq = pcdev->irq;
	rearview_env.vip_clk = pcdev->clk;
	rearview_env.vip_dev = pcdev->dev;

	rearview_env.dma_chan = pcdev->dma_chan;
	rearview_env.dma_addr = pcdev->rearview_dma_addr;

	rearview_env.gpio = pcdev->rearview_gpio;
	rearview_env.irq = gpio_to_irq(rearview_env.gpio);

	rearview_env.decoder_ops = pcdev->rearview_decoder_ops;
	rearview_env.pdata = pcdev->pdata;

	rearview_env.save_vip_context = pcdev->save_vip_context;
	rearview_env.restore_vip_context = pcdev->restore_vip_context;
	rearview_env.data = pcdev;

	pcdev->rearview_freeze = rearview_freeze;
	pcdev->rearview_restore = rearview_restore;
	pcdev->rearview_suspend = rearview_suspend;
	pcdev->rearview_resume = rearview_resume;
	pcdev->rearview_enabled = rearview_enabled;


	rearview_init();

	while (1) {
		DEFINE_WAIT(wait);
		pr_debug("%s\n", "thread loop 1");

		if (state == ACTIVE_INT || state == ACTIVE_HIBER) {
			int st = gpio_get_value(rearview_env.gpio);

			if (st && atomic_read(&rv_started) == 0) {
				rearview_start(false);
				atomic_set(&rv_started, 1);
			}

			if (!st && atomic_read(&rv_started) > 0) {
				rearview_stop();
				atomic_set(&rv_started, 0);
			}
		} else if (state == ACTIVE_SLEEP) {
			int st = gpio_get_value(rearview_env.gpio);

			if (st) {
				rearview_start(false);
				atomic_set(&rv_started, 1);
			} else {
				if (atomic_read(&rv_started) > 0) {
					__deinit_fbdev();
					atomic_set(&rv_started, 0);
				}
			}
		}

		while (1) {
			pr_debug("%s\n", "thread loop 2");
			spin_lock_irqsave(&rv.lock, flags);
			state = rv.active;
			rv.active = 0;
			prepare_to_wait(&rearview_event, &wait,
				TASK_INTERRUPTIBLE);
			need_break = kthread_should_stop() || state;
			spin_unlock_irqrestore(&rv.lock, flags);

			if (need_break)
				break;

			schedule();
		}
		pr_debug("%s\n", "thread loop 3");
		finish_wait(&rearview_event, &wait);
		if (kthread_should_stop())
			break;
	}

	rearview_deinit();

	pr_info("%s leaving\n", __func__);

	return 0;
}
