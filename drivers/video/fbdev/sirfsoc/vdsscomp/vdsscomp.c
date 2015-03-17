/*
 * CSR sirfsoc vdss composition driver
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
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

	kfree(sync);
}

int vdsscomp_gralloc_queue(struct vdsscomp_setup_data *d,
	void (*cb_fn)(void *, int), void *cb_arg)
{
	struct vdsscomp_sync *sync;
	int i;
	int r = 0;
	unsigned long flags;

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
		struct sirfsoc_vdss_layer *l;
		struct sirfsoc_vdss_layer_info layer_info;
		struct sirfsoc_vdss_screen_info screen_info;

		disp = &d->disps[i];
		panel = gdev->displays[i].panel;
		scn = sirfsoc_vdss_find_screen_from_panel(panel);

		if ((disp->dirty_mask & SCREEN_DIRTY_MASK)) {
			scn->get_info(scn, &screen_info);
			screen_info.top_layer = disp->scn.top_layer;
			screen_info.back_color = disp->scn.back_color;
			/*TODO: set screen info */
		}

		for (layer = 0; layer < gdev->displays[i].num_layers; layer++) {
			l = gdev->displays[i].layers[layer];
			if (!(disp->dirty_mask & (1 << layer))) {
				if (disp->phys_addr[layer] != 0)
					l->flip(l, disp->phys_addr[layer]);

				continue;
			}
			memset(&layer_info, 0, sizeof(layer_info));

			if (disp->layers[layer].enabled) {
				l->get_info(l, &layer_info);
				layer_info.base = disp->phys_addr[layer];
				layer_info.fmt = disp->layers[layer].fmt;
				layer_info.surf_width =
					disp->layers[layer].width;
				layer_info.surf_height =
					disp->layers[layer].height;
				layer_info.src_rect.left =
					disp->layers[layer].src_rect.left;
				layer_info.src_rect.top =
					disp->layers[layer].src_rect.top;
				layer_info.src_rect.right =
					disp->layers[layer].src_rect.right;
				layer_info.src_rect.bottom =
					disp->layers[layer].src_rect.bottom;
				layer_info.dst_rect.left =
					disp->layers[layer].dst_rect.left;
				layer_info.dst_rect.top =
					disp->layers[layer].dst_rect.top;
				layer_info.dst_rect.right =
					disp->layers[layer].dst_rect.right;
				layer_info.dst_rect.bottom =
					disp->layers[layer].dst_rect.bottom;
				layer_info.pre_mult_alpha =
					disp->layers[layer].pre_mult_alpha;

				if (layer_info.fmt == VDSS_PIXELFORMAT_8888)
					layer_info.source_alpha = 1;

				print_vdss_layer_info(&layer_info);

				l->set_info(l, &layer_info);
				l->screen->apply(l->screen);
				l->enable(l);
			} else {
				l->disable(l);
			}
		}
	}

	sync = kzalloc(sizeof(*sync), GFP_KERNEL);
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
		if (d->layers[i]->screen == scn)
			dis->layers_owned |= 1 << i;
		else if (!d->layers[i]->is_enabled(d->layers[i]))
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

static void vdsscomp_flip_isr(void *pdata, unsigned int irqstatus)
{
	struct vdsscomp_dev *cdev = pdata;
	struct vdsscomp_sync *sync;

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
		kfree(sync);
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
	for (i = 0; i < d->num_layers; i++)
		d->layers[i] = sirfsoc_vdss_get_layer(d->lcdc_index, i);

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
		for (i = 0; i < d->num_layers; i++)
			d->layers[i] = sirfsoc_vdss_get_layer(d->lcdc_index, i);
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

	cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);

	cdev->dev.minor = MISC_DYNAMIC_MINOR;
	cdev->dev.name = "vdsscomp";
	cdev->dev.mode = 0666;
	cdev->dev.fops = &comp_fops;

	ret = misc_register(&cdev->dev);
	if (ret) {
		pr_err("vdsscomp: failed to register misc device.\n");
		kfree(cdev);
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
	kfree(cdev);
	return ret;
}

static int vdsscomp_remove(struct platform_device *pdev)
{
	struct vdsscomp_dev *cdev = platform_get_drvdata(pdev);

	misc_deregister(&cdev->dev);
	vdsscomp_deinit_flip(cdev);
	kfree(cdev);
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
