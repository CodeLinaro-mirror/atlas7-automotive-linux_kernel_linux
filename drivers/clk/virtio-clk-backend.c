/*
 * Virtio clock backend driver
 *
 * Copyright (c) 2014 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/limits.h>
#include <linux/of.h>

#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/vringh.h>
#include <linux/remoteproc.h>
#include <linux/remoteproc_dualos.h>

#include <linux/virtio_clk.h>

/**
 * struct virtio_clock - virtio clock device
 * @vdev: the virtio device
 * @vrh: virtio clock virtqueue
 * @np: a dummy device node, where all clk units are connected.
 *
 */
struct virtio_clk {
	struct virtio_device *vdev;
	struct vringh *vrh;
	struct device_node *np;
	int maxclk;
	struct task_struct *vq_task;
	wait_queue_head_t wq;
};

struct virtio_clk_req_buffer {
	struct virtio_clk *vclk;
	struct virtio_clk_req *req;
	struct vringh_kiov io;
	unsigned short head;
};

static size_t virtio_clk_get_request(struct vringh *vrh,
			struct virtio_clk_req_buffer *buf)
{
	int ret;
	size_t len;

	vringh_kiov_init(&buf->io, NULL, 0);
	ret = vringh_pop_kern(vrh, NULL, &buf->io, &buf->head, GFP_KERNEL);
	if (ret) {
		if (ret != -ENOSPC) {
			dev_err(&buf->vclk->vdev->dev,
				"pop data from VRING failed!err=%d\n", ret);
			BUG_ON(1);
		}
		goto clean_iov;
	}

	if ((buf->io.used > 1)) {
		dev_err(&buf->vclk->vdev->dev,
			"virtio clock data only can have ONE io_vector\n");
		BUG_ON(buf->io.used > 1);
	}

	len =  buf->io.iov[0].iov_len;
	if (!len)
		goto clean_iov;

	buf->req = (struct virtio_clk_req *)buf->io.iov[0].iov_base;

	return len;

clean_iov:
	/* Clean up the kiov */
	vringh_kiov_cleanup(&buf->io);
	return 0;

}

static int virtio_clk_handle_request(struct vringh *vrh,
			struct virtio_clk_req_buffer *buf)
{
	struct virtio_clk_req *req = buf->req;
	struct virtio_clk *vclk = buf->vclk;
	int err = 0;
	struct clk *clk;

	print_hex_dump(KERN_DEBUG, "vclk:", DUMP_PREFIX_NONE,
		16, 1, buf->req, sizeof(*req), true);

	clk = of_clk_get(vclk->np, req->clk_index);
	if (IS_ERR(clk)) {
		err = PTR_ERR(clk);
		dev_err(&vclk->vdev->dev,
			"Clock get failed ... id[%d]\n", req->clk_index);
		goto handle_out;
	}

	switch (req->clk_op_code) {
	case VIRT_CLK_PREPARE:
		err = clk_prepare(clk);
		if (err) {
			dev_err(&vclk->vdev->dev,
				"Clock prepare failed, err=%d\n", err);
			goto handle_out;
		}
		break;

	case VIRT_CLK_UNPREPARE:
		clk_unprepare(clk);
		break;

	case VIRT_CLK_IS_PREPARED:
		req->data.prepared = __clk_is_prepared(clk);
		break;

	case VIRT_CLK_ENABLE:
		err = clk_enable(clk);
		if (err) {
			dev_err(&vclk->vdev->dev,
				"Clock enable failed, err=%d\n", err);
			goto handle_out;
		}
		break;

	case VIRT_CLK_DISABLE:
		clk_disable(clk);
		break;

	case VIRT_CLK_IS_ENABLED:
		req->data.enabled = __clk_is_enabled(clk);
		break;

	case VIRT_CLK_SET_RATE:
		err = clk_set_rate(clk, req->data.rate);
		if (err) {
			dev_err(&vclk->vdev->dev,
			"Clock set rate failed, err=%d\n", err);
			goto handle_out;
		}
		break;

	case VIRT_CLK_ROUND_RATE:
		req->data.rate = clk_round_rate(clk, req->data.rate);
		break;

	case VIRT_CLK_RECALC_RATE:
		req->data.rate = clk_get_rate(clk);
		break;

	case VIRT_CLK_RECALC_ACCURACY:
	case VIRT_CLK_DETERMINE_RATE:
	case VIRT_CLK_SET_PARENT:
	case VIRT_CLK_GET_PARENT:
	case VIRT_CLK_SET_RATE_AND_PARENT:
	case VIRT_CLK_UNPREPARE_UNUSED:
	case VIRT_CLK_DISABLE_UNUSED:
	case VIRT_CLK_INIT:
		break;

	default:
		dev_err(&vclk->vdev->dev,
			"Unkonwn virtual clock opcode:%08x\n",
			req->clk_op_code);
		err = -EPERM;
		break;
	}

handle_out:

	req->status = err;
	if (!IS_ERR(clk))
		clk_put(clk);

	/* Push in-data (read/write & status data) to vq */
	vringh_push_kern(vrh, NULL, &buf->io, buf->head, 0);
	vringh_notify(vrh);

	return 0;
}

static int virtio_clk_vrh_ist(void *vclk_ptr)
{
	struct virtio_clk *vclk = (struct virtio_clk *)vclk_ptr;
	struct virtio_clk_req_buffer req_buf;
	size_t buf_len;

	req_buf.vclk = vclk;

	do {
		wait_event_interruptible(vclk->wq,
		(buf_len = virtio_clk_get_request(vclk->vrh, &req_buf)));
		vringh_notify_disable_kern(vclk->vrh);

		do {
			virtio_clk_handle_request(vclk->vrh, &req_buf);
			buf_len = virtio_clk_get_request(vclk->vrh, &req_buf);
		} while (buf_len);

		vringh_notify_enable_kern(vclk->vrh);
	} while (1);

	return 0;
}


/* vringh has data arrive intterrupt IRQ */
static void virtio_clk_vrh_isr(struct virtio_device *vdev,
				struct vringh *vrh)
{
	struct virtio_clk *vclk = vdev->priv;
	dev_dbg(&vdev->dev, "%s\n", __func__);
	/* wake up hanlde thread */
	wake_up(&vclk->wq);
}


static int virtio_clk_turn_online(struct virtio_device *vdev)
{
	struct virtio_clk *vclk = vdev->priv;
	vrh_callback_t *vrh_cbs[] = {virtio_clk_vrh_isr};
	struct vringh *vrhs[1];
	int ret;

	if (vclk->vrh)	{
		dev_dbg(&vdev->dev,
			"virtio clock vringh had been configed already!\n");
		return 0;
	}
	/* Frontend has prepared the VQ's desc table already.
	 * Now, we can setup the VQs in backend
	 */
	/* We use a bidirectional virtqueue, so we expect one virtqueues */
	ret = vdev->vringh_config->find_vrhs(vdev, 1, vrhs, vrh_cbs);
	if (ret)
		return ret;

	vclk->vrh = vrhs[0];

	/* Start the thread to handle output data in virtqueue */
	vclk->vq_task = kthread_run(virtio_clk_vrh_ist,
				(void *)vclk, "virtio_clk_vrh_ist");

	/* tell the remote processor backend is online */
	virtio_cwrite32(vdev, MMIO_BACK_ONLINE, vdev->index);

	dev_info(&vdev->dev, "online\n");
	return 0;
}

static int virtio_clk_mmio(struct virtio_device *vdev, u32 offset)
{
	u32 value_u = 0;

	switch (offset) {
	/************* RPROC Defined MMIO Handlers *****************/
	case MMIO_FRONT_ONLINE:
		value_u = virtio_clk_turn_online(vdev);
		break;

	case MMIO_FRONT_VQ_READY:
		dev_dbg(&vdev->dev, "Frontend VQ had been ready!\n");
		break;

	case MMIO_FEATURES:
	case MMIO_FRONT_OFFLINE:
		break;

	/*************** Customized MMIO Handlers *******************/
	default:
		dev_err(&vdev->dev, "Bad MMIO offset:%d\n", offset);
		break;
	}
	return 0;
}

static int virtio_clk_hw_init(struct virtio_device *vdev,
				struct virtio_clk *vclk)
{
	int len;
	const char *compat;

	vclk->np = (void *)virtio_cread32(vdev, MMIO_PRIV_DATA);
	if (!vclk->np)
		return -ENODEV;

	vclk->np = of_parse_phandle(vclk->np, "vclk-controller", 0);
	if (!vclk->np)
		return -ENODEV;

	if (of_property_read_u32(vclk->np, "clock_num", &vclk->maxclk)) {
		dev_err(&vdev->dev,
			"Unable to find clock units number in device node.\n");
		return -ENODEV;
	}

	if (of_property_read_string(vclk->np, "compatible", &compat)) {
		dev_err(&vdev->dev,
			"Unable to find compatible string.\n");
		return -ENODEV;
	}

	/* disable notify remote side when we initialize the config space */
	rproc_virtio_disable_notify(vdev);

	len = strlen(compat) + sizeof(char);

	virtio_cwrite32(vdev, VCLK_MMIO_UNIT_NUM, vclk->maxclk);
	virtio_cwrite32(vdev, VCLK_MMIO_UNIT_LEN, len);
	vdev->config->set(vdev, VCLK_MMIO_UNIT_DATA, compat, len);

	/* enable notify remote side */
	rproc_virtio_enable_notify(vdev);

	return 0;
}

static int virtio_clk_probe(struct virtio_device *vdev)
{
	struct virtio_clk *vclk;
	int err;

	vclk = kzalloc(sizeof(*vclk), GFP_KERNEL);
	if (!vclk)
		return -ENOMEM;

	vclk->vdev = vdev;
	err = virtio_clk_hw_init(vdev, vclk);
	if (err)
		return err;

	init_waitqueue_head(&vclk->wq);

	vdev->priv = vclk;

	rproc_set_mmio_handler(vdev, virtio_clk_mmio);

	return 0;
}


static void virtio_clk_remove(struct virtio_device *vdev)
{
	struct virtio_clk *vclk = vdev->priv;

	vdev->config->reset(vdev);

	if (vclk->vq_task)
		kthread_stop(vclk->vq_task);

	vdev->vringh_config->del_vrhs(vclk->vdev);

	kfree(vclk);
}

/* Setting the VIRTIO TYPE ID for this virtual device */
static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_CLOCK, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

/* virtual clock virtio features */
static unsigned int features[] = {
	/* Prepare Features */
	VIRTIO_CLK_F_PREPARE,
	VIRTIO_CLK_F_UNPREPARE,
	VIRTIO_CLK_F_IS_PREPARED,
	/* Control Features */
	VIRTIO_CLK_F_ENABLE,
	VIRTIO_CLK_F_DISABLE,
	VIRTIO_CLK_F_IS_ENABLED,
};

static struct virtio_driver virtio_clk_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name = KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.id_table = id_table,
	.probe = virtio_clk_probe,
	.remove = virtio_clk_remove,
};

static int __init virtio_clk_init(void)
{
	/* we regisger the virtio driver to drive virtio device */
	return register_virtio_driver(&virtio_clk_driver);
}
module_init(virtio_clk_init);

static void __exit virtio_clk_fini(void)
{
	unregister_virtio_driver(&virtio_clk_driver);
}
module_exit(virtio_clk_fini);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio clock backend driver");
MODULE_LICENSE("GPL v2");
