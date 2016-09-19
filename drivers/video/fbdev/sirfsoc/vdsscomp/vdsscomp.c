/*
 * CSR sirfsoc vdss composition driver
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

#include <linux/module.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

#include <video/sirfsoc_vdss.h>
#include <video/vdsscomp.h>
#include "vdsscomp.h"

#define MODULE_NAME	"vdsscomp"

static struct vdsscomp_dev *gdev;

static void vdsscomp_sync_cb(struct work_struct *work)
{
	struct vdsscomp_sync *sync =
		container_of(work, struct vdsscomp_sync, work);

	if (sync->cb_fn)
		sync->cb_fn(sync->cb_arg, 1);

	devm_kfree(gdev->pdev, sync);
}

static bool vdsscomp_layer_enable(
		struct vdsscomp_layer_data *l,
		struct vdsscomp_layer_info *info,
		u32 phys_addr)
{
	struct sirfsoc_vdss_layer *layer = l->layer;
	struct sirfsoc_vdss_layer_info layer_info;
	struct vdss_rect src_rect, dst_rect;
	struct vdss_surface src_surf;
	int src_skip, dst_skip;

	src_rect.left = info->src_rect.left;
	src_rect.top = info->src_rect.top;
	src_rect.right = info->src_rect.right;
	src_rect.bottom = info->src_rect.bottom;

	dst_rect.left = info->dst_rect.left;
	dst_rect.top = info->dst_rect.top;
	dst_rect.right = info->dst_rect.right;
	dst_rect.bottom = info->dst_rect.bottom;

	memset(&src_surf, 0, sizeof(src_surf));
	src_surf.fmt = info->fmt;
	src_surf.width = info->width;
	src_surf.height = info->height;
	src_surf.base = phys_addr;
	if (info->interlace.interlaced) {
		if (info->interlace.field_offset)
			src_surf.field = VDSS_FIELD_SEQ_TB;
		else
			src_surf.field = VDSS_FIELD_INTERLACED_TB;
	} else
		src_surf.field = VDSS_FIELD_NONE;

	if (sirfsoc_vpp_is_passthrough_support(info->fmt))
		l->disp_mode = VDSS_DISP_PASS_THROUGH;
	else
		l->disp_mode = VDSS_DISP_NORMAL;

	if (!sirfsoc_vdss_check_size(l->disp_mode,
	    &src_surf, &src_rect, &src_skip,
	    layer, &dst_rect, &dst_skip))
		return false;

	if (l->disp_mode == VDSS_DISP_PASS_THROUGH) {
		struct vdss_vpp_op_params params;

		/* Create VPP device */
		if (l->vpp == NULL) {
			struct vdss_vpp_create_device_params dev_params;

			memset(&dev_params, 0, sizeof(dev_params));
			l->vpp = sirfsoc_vpp_create_device(
					layer->lcdc_id, &dev_params);
			if (l->vpp == NULL)
				return false;
		}

		/* Set VPP info */
		memset(&params, 0, sizeof(params));
		params.type = VPP_OP_PASS_THROUGH;

		params.op.passthrough.interlace.di_mode = info->interlace.mode;
		params.op.passthrough.src_surf = src_surf;
		params.op.passthrough.src_rect = src_rect;
		params.op.passthrough.dst_rect = dst_rect;

		/*vpp color ctrl*/
		params.op.passthrough.color_ctrl.brightness = 0;
		params.op.passthrough.color_ctrl.contrast = 128;
		params.op.passthrough.color_ctrl.hue = 0;
		params.op.passthrough.color_ctrl.saturation = 128;

		sirfsoc_vpp_present(l->vpp, &params);
	}

	memset(&layer_info, 0, sizeof(layer_info));
	layer->get_info(layer, &layer_info);

	if (l->disp_mode == VDSS_DISP_NORMAL) {
		layer_info.src_surf = src_surf;
		layer_info.src_rect = src_rect;
		layer_info.line_skip = src_skip;
	} else {
		layer_info.src_surf.base = 0;
		layer_info.src_surf.fmt = info->fmt;
		layer_info.src_surf.width = dst_rect.right - dst_rect.left + 1;
		layer_info.src_surf.height = dst_rect.bottom - dst_rect.top + 1;
		layer_info.src_rect = dst_rect;
		layer_info.line_skip = dst_skip;
	}
	layer_info.dst_rect = dst_rect;

	layer_info.pre_mult_alpha = info->pre_mult_alpha;
	layer_info.disp_mode = l->disp_mode;

	if (layer_info.src_surf.fmt == VDSS_PIXELFORMAT_8888)
		layer_info.source_alpha = 1;

	print_vdss_layer_info(&layer_info);

	layer->set_info(layer, &layer_info);
	layer->screen->apply(layer->screen);
	layer->enable(layer);
	return true;
}

static void vdsscomp_layer_disable(struct vdsscomp_layer_data *l)
{
	l->layer->disable(l->layer);
	if (l->vpp) {
		sirfsoc_vpp_destroy_device(l->vpp);
		l->vpp = NULL;
		l->disp_mode = VDSS_DISP_NORMAL;
	}
}

static void vdsscomp_layer_flip(struct vdsscomp_layer_data *l, u32 base)
{
	struct sirfsoc_vdss_layer *layer = l->layer;

	if (l->disp_mode == VDSS_DISP_PASS_THROUGH) {
		struct vdss_vpp_op_params vpp_op = {0};

		vpp_op.type = VPP_OP_PASS_THROUGH;
		vpp_op.op.passthrough.src_surf.base = base;
		vpp_op.op.passthrough.flip = true;

		sirfsoc_vpp_present(l->vpp, &vpp_op);
	}

	/*
	 * In passthrough mode, we found that if only
	 * VPP registers are changed, we should also
	 * set LX_CTRL_CONFIRM, otherwise VPP shadow
	 * registers won't take effect in next vsync
	 * */
	layer->flip(layer, base);
}

int vdsscomp_gralloc_queue(struct vdsscomp_setup_data *d,
	void (*cb_fn)(void *, int), void *cb_arg)
{
	struct vdsscomp_sync *sync;
	int i;
	int r = 0;
	unsigned long flags;

	/*
	 * when compositor(eg. surfaceflinger, weston) exit,
	 * no new valid vdsscomp_setup_data queued any more,
	 * but sync object of the last frame should be released,
	 * so in this case, call vdsscomp_gralloc_queue with
	 * fake vdsscomp_setup_data can flush sync
	 */
	if (d == NULL)
		goto flush_sync;

	if ((d->num_disps > gdev->num_displays) ||
		(d->disps[0].num_layers > gdev->displays[0].num_layers) ||
		(d->num_disps > 1 &&
		(d->disps[1].num_layers > gdev->displays[1].num_layers))) {
		dev_err(DEV(gdev), "invalid display num or layer num\n");
		r = -EINVAL;
		goto skip_comp;
	}

	for (i = 0; i < d->num_disps; i++) {
		int layer;
		struct vdsscomp_setup_disp_data *disp;
		struct sirfsoc_vdss_panel *panel;
		struct sirfsoc_vdss_screen *scn;
		struct vdsscomp_layer_data *l;
		struct sirfsoc_vdss_screen_info screen_info;
		struct sirfsoc_vdss_layer_info layer_info;

		disp = &d->disps[i];
		panel = gdev->displays[i].panel;
		scn = sirfsoc_vdss_find_screen_from_panel(panel);
		if (!scn) {
			dev_err(DEV(gdev), "no screen for the panel\n");
			r = -EINVAL;
			goto skip_comp;
		}

		scn->get_info(scn, &screen_info);
		if (screen_info.top_layer != disp->scn.top_layer ||
			screen_info.back_color != disp->scn.back_color) {
			screen_info.top_layer = disp->scn.top_layer;
			screen_info.back_color = disp->scn.back_color;
			scn->set_info(scn, &screen_info);
			scn->apply(scn);
		}

		for (layer = 0; layer < gdev->displays[i].num_layers; layer++) {
			l = &gdev->displays[i].layers[layer];
			if (!(disp->dirty_mask & (1 << layer)))
				continue;

			/* If the fmt is changed, disable the layer */
			if (disp->layers[layer].enabled) {
				l->layer->get_info(l->layer, &layer_info);
				if ((u32)(layer_info.src_surf.fmt) !=
				    (u32)(disp->layers[layer].fmt))
					vdsscomp_layer_disable(l);
			} else
				vdsscomp_layer_disable(l);
		}

		for (layer = 0; layer < gdev->displays[i].num_layers; layer++) {
			l = &gdev->displays[i].layers[layer];
			if (!(disp->dirty_mask & (1 << layer))) {
				if (disp->phys_addr[layer] != 0)
					vdsscomp_layer_flip(
						l,
						disp->phys_addr[layer]
						);
				continue;
			}

			if (disp->layers[layer].enabled)
				vdsscomp_layer_enable(
					l,
					&disp->layers[layer],
					disp->phys_addr[layer]
					);
		}
	}
flush_sync:
	sync = devm_kzalloc(gdev->pdev, sizeof(*sync), GFP_KERNEL);
	if (!sync)
		return -ENOMEM;

	sync->cb_arg = cb_arg;
	sync->cb_fn = cb_fn;
	INIT_WORK(&sync->work, vdsscomp_sync_cb);

	spin_lock_irqsave(&gdev->flip_lock, flags);
	list_add_tail(&sync->list, &gdev->flip_list);
	spin_unlock_irqrestore(&gdev->flip_lock, flags);

skip_comp:
	return r;
}
EXPORT_SYMBOL(vdsscomp_gralloc_queue);

static void init_display_timing(struct vdsscomp_display_info *dis,
	struct sirfsoc_vdss_panel *panel)
{
	if (dis == NULL || panel == NULL)
		return;

	dis->timings.xres = panel->timings.xres;
	dis->timings.yres = panel->timings.yres;
	dis->timings.pixel_clock = panel->timings.pixel_clock;
	dis->timings.hsw = panel->timings.hsw;
	dis->timings.hfp = panel->timings.hfp;
	dis->timings.hbp = panel->timings.hbp;
	dis->timings.vsw = panel->timings.vsw;
	dis->timings.vfp = panel->timings.vfp;
	dis->timings.vbp = panel->timings.vbp;
}

static long vdsscomp_query_display(struct vdsscomp_dev *cdev,
	struct vdsscomp_display_info *dis)
{
	struct sirfsoc_vdss_panel *panel;
	struct vdsscomp_display_data *d;
	struct sirfsoc_vdss_screen *scn;
	int i;

	/* get display */
	if (dis->ix >= cdev->num_displays)
		return -EINVAL;

	d = &cdev->displays[dis->ix];
	panel = d->panel;

	scn = panel->src->screen;
	if (!scn)
		return -EINVAL;

	/* fill out display information */
	dis->type = panel->type;
	dis->enabled = (panel->state == SIRFSOC_VDSS_PANEL_ENABLED);
	dis->layers_avail = 0;
	dis->layers_owned = 0;

	/* find all overlays available for/owned by this display */
	for (i = 0; i < d->num_layers && dis->enabled; i++) {
		if (d->layers[i].layer->screen == scn)
			dis->layers_owned |= 1 << i;
		else if (!d->layers[i].layer->is_enabled(d->layers[i].layer))
			dis->layers_avail |= 1 << i;
	}

	dis->layers_avail |= dis->layers_owned;

	init_display_timing(dis, panel);

	return 0;
}

static long vdsscomp_ioctl(struct file *filp, unsigned int cmd,
	unsigned long arg)
{
	struct miscdevice *dev = filp->private_data;
	struct vdsscomp_dev *cdev = container_of(dev, struct vdsscomp_dev, dev);
	void __user *ptr = (void __user *)arg;

	union {
		struct vdsscomp_display_info dis;
	} u;

	switch (cmd) {
	case VDSSCIOC_QUERY_DISPLAY:
		if (copy_from_user(&u.dis, ptr, sizeof(u.dis)))
			return -EFAULT;
		vdsscomp_query_display(cdev, &u.dis);
		if (copy_to_user(ptr, &u.dis, sizeof(u.dis)))
			return -EFAULT;
		break;
	default:
		return  -EINVAL;
	}

	return 0;
}

static int vdsscomp_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static const struct file_operations comp_fops = {
	.owner		= THIS_MODULE,
	.open		= vdsscomp_open,
	.unlocked_ioctl = vdsscomp_ioctl,
};

#ifdef CONFIG_ANDROID
static void vdsscomp_flip_send_timestamp(struct work_struct *data)
{
	struct device *dev;
	struct vdsscomp_dev *cdev;
	char buf[64];
	char *envp[2];

	cdev = container_of(data, struct vdsscomp_dev, vsync_work);
	dev = cdev->pdev;

	snprintf(buf, sizeof(buf), "TIMESTAMP=%llu",
				ktime_to_ns(cdev->vsync_timestamp));
	envp[0] = buf;
	envp[1] = NULL;
	kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, envp);
}
#endif

static void vdsscomp_flip_isr(void *pdata, unsigned int irqstatus)
{
	struct vdsscomp_dev *cdev = pdata;
	struct vdsscomp_sync *sync;

#ifdef CONFIG_ANDROID
	cdev->vsync_timestamp = ktime_get();
	schedule_work(&cdev->vsync_work);
#endif

	spin_lock(&cdev->flip_lock);
	if (list_empty(&cdev->flip_list) ||
		list_is_singular(&cdev->flip_list)) {
		spin_unlock(&cdev->flip_lock);
		return;
	}

	sync = list_entry(cdev->flip_list.next, struct vdsscomp_sync, list);
	list_del_init(&sync->list);
	spin_unlock(&cdev->flip_lock);

	queue_work(cdev->sync_wkq, &sync->work);

}

static int vdsscomp_init_flip(struct vdsscomp_dev *cdev)
{
	struct sirfsoc_vdss_panel *panel;

	INIT_LIST_HEAD(&cdev->flip_list);
	spin_lock_init(&cdev->flip_lock);

	cdev->sync_wkq = create_singlethread_workqueue("vdsscomp_sync");
	if (!cdev->sync_wkq)
		return -ENOMEM;

#ifdef CONFIG_ANDROID
	INIT_WORK(&cdev->vsync_work, vdsscomp_flip_send_timestamp);
#endif
	/* the panel for primary display */
	panel = cdev->displays[0].panel;
	sirfsoc_lcdc_register_isr(panel->src->lcdc_id, vdsscomp_flip_isr,
		cdev, LCDC_INT_VSYNC);

	return 0;
}

static int vdsscomp_deinit_flip(struct vdsscomp_dev *cdev)
{
	struct sirfsoc_vdss_panel *panel;
	unsigned long flags;
	struct vdsscomp_sync *sync, *tmp;

	/* the panel for primary display */
	panel = cdev->displays[0].panel;
	sirfsoc_lcdc_unregister_isr(panel->src->lcdc_id, vdsscomp_flip_isr,
		cdev, LCDC_INT_VSYNC);

	spin_lock_irqsave(&gdev->flip_lock, flags);
	list_for_each_entry_safe(sync, tmp, &cdev->flip_list, list) {
		list_del(&sync->list);
		if (sync->cb_fn)
			sync->cb_fn(sync->cb_arg, 1);
		devm_kfree(gdev->pdev, sync);
	}
	spin_unlock_irqrestore(&gdev->flip_lock, flags);

	destroy_workqueue(cdev->sync_wkq);

	return 0;
}

static int vdsscomp_init_displays(struct vdsscomp_dev *cdev)
{
	struct sirfsoc_vdss_panel *panel;
	struct vdsscomp_display_data *d;
	int i;

	cdev->num_displays = 0;
	panel = NULL;

	panel = sirfsoc_vdss_get_primary_device();

	if (panel == NULL) {
		dev_err(cdev->pdev, "no primary display available\n");
		goto err;
	}

	sirfsoc_vdss_get_panel(panel);
	if (panel->driver == NULL) {
		dev_warn(cdev->pdev, "no driver for primary display: %s\n",
			panel->name);
		sirfsoc_vdss_put_panel(panel);
		goto err;
	}

	d = &cdev->displays[cdev->num_displays++];
	d->panel = panel;
	d->lcdc_index = SIRFSOC_VDSS_LCDC0;

	d->num_layers = sirfsoc_vdss_get_num_layers(d->lcdc_index);
	for (i = 0; i < d->num_layers; i++) {
		d->layers[i].layer = sirfsoc_vdss_get_layer(d->lcdc_index, i);
		d->layers[i].vpp = NULL;
	}

	d->num_screens = sirfsoc_vdss_get_num_screens(d->lcdc_index);
	for (i = 0; i < d->num_screens; i++)
		d->screens[i] = sirfsoc_vdss_get_screen(d->lcdc_index, i);

	if (sirfsoc_vdss_get_num_lcdc() > 1) {
		panel = sirfsoc_vdss_get_secondary_device();
		if (panel == NULL)
			goto out;
		sirfsoc_vdss_get_panel(panel);
		if (panel->driver == NULL) {
			dev_warn(cdev->pdev, "no driver for secondary display: %s\n",
				panel->name);
			sirfsoc_vdss_put_panel(panel);
			goto out;
		}
		d = &cdev->displays[cdev->num_displays++];
		d->lcdc_index = SIRFSOC_VDSS_LCDC1;
		d->num_layers = sirfsoc_vdss_get_num_layers(d->lcdc_index);
		for (i = 0; i < d->num_layers; i++) {
			d->layers[i].layer = sirfsoc_vdss_get_layer(
						d->lcdc_index,
						i);
			d->layers[i].vpp = NULL;
		}
		d->num_screens = sirfsoc_vdss_get_num_screens(d->lcdc_index);
		for (i = 0; i < d->num_screens; i++)
			d->screens[i] =
				sirfsoc_vdss_get_screen(d->lcdc_index, i);

		d->panel = panel;
	}

out:
	return 0;
err:
	return -EINVAL;

}

static int vdsscomp_probe(struct platform_device *pdev)
{
	int ret;
	struct vdsscomp_dev *cdev;

	if (sirfsoc_vdss_is_initialized() == false)
		return -EPROBE_DEFER;

	cdev = devm_kzalloc(&pdev->dev,
		sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return -ENOMEM;

	cdev->dev.minor = MISC_DYNAMIC_MINOR;
	cdev->dev.name = "vdsscomp";
	cdev->dev.mode = 0666;
	cdev->dev.fops = &comp_fops;

	ret = misc_register(&cdev->dev);
	if (ret) {
		pr_err("vdsscomp: failed to register misc device.\n");
		devm_kfree(&pdev->dev, cdev);
		return ret;
	}

	cdev->pdev = &pdev->dev;
	platform_set_drvdata(pdev, cdev);

	ret = vdsscomp_init_displays(cdev);
	if (ret) {
		dev_err(cdev->pdev, "failed to init display data\n");
		goto cleanup;
	}

	vdsscomp_init_flip(cdev);

	gdev = cdev;

	return 0;
cleanup:
	misc_deregister(&cdev->dev);
	devm_kfree(&pdev->dev,  cdev);
	return ret;
}

static int vdsscomp_remove(struct platform_device *pdev)
{
	struct vdsscomp_dev *cdev = platform_get_drvdata(pdev);

	misc_deregister(&cdev->dev);
	vdsscomp_deinit_flip(cdev);
	devm_kfree(&pdev->dev, cdev);
	gdev = NULL;

	return 0;
}

static struct platform_driver vdsscomp_driver = {
	.probe = vdsscomp_probe,
	.remove = vdsscomp_remove,
	.driver = { .name = MODULE_NAME, .owner = THIS_MODULE }
};

static struct platform_device vdsscomp_device = {
	.name = MODULE_NAME,
	.id = -1,
};

static int __init vdsscomp_init(void)
{
	int err;

	err = platform_driver_register(&vdsscomp_driver);
	if (err) {
		pr_err("vdsscomp_init: unable to register platform driver.\n");
		goto out;
	}

	err = platform_device_register(&vdsscomp_device);
	if (err) {
		pr_err("vdsscomp_init: unable to register platform device.\n");
		platform_driver_unregister(&vdsscomp_driver);
		goto out;
	}

	return 0;
out:
	return err;
}

static void __exit vdsscomp_exit(void)
{
	platform_device_unregister(&vdsscomp_device);
	platform_driver_unregister(&vdsscomp_driver);
}

MODULE_LICENSE("GPL v2");
module_init(vdsscomp_init);
module_exit(vdsscomp_exit);
