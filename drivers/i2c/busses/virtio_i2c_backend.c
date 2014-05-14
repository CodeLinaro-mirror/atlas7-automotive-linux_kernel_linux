/*
 * Virtio-based remote processor i2c
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

#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/vringh.h>

#include <linux/remoteproc.h>
#include <linux/remoteproc_dualos.h>

#include <linux/virtio_i2c.h>

/* struct virtio_i2c_iov - virtio i2c context info
 * @riov: IOV holding data read from the vring for read request.
 *	  Note that riov may still hold data when call funtion returns.
 * @wiov: IOV holding data read from the vring for write request.
 *	  Note that riov may still hold data when call funtion returns.
 * @head: Last descriptor ID we received from vringh_getdesc_kern.
 *	  We use this to put descriptor back on the used ring. USHRT_MAX is
 *	  used to indicate invalid head-id.
 */
struct virtio_i2c_iov {
	struct vringh_kiov out;
	struct vringh_kiov in;
	unsigned short head;
};

/**
 * struct virtio_i2c - virtual remote processor state
 * @vdev: the virtio device
 * @adapter: the real i2c adapter pointer
 * @vrh: host side vring
 * @bus_id: the real i2c adapter bus is
 * @status: the virtio i2c status
 * @vq_lock:protects vq, sleepers, to allow concurrent senders.
 *	sending a message might require waking up a dozing remote
 *	processor, which involves sleeping, hence the mutex.
 */
struct virtio_i2c {
	struct virtio_device *vdev;
	struct i2c_adapter *adapter;
	struct vringh *vrh;
	int bus_id;
	struct mutex vq_lock;
	struct task_struct *vq_task;
	wait_queue_head_t outq;
};

struct virtio_i2c_req {
	struct virtio_i2c *vi2c;
	struct i2c_msg msg;
	struct virtio_i2c_iov iov;
	struct virti2c_req_outhdr *outhdr;
	struct virti2c_req_inhdr  *inhdr;
};

#define DUMP_I2C_REQ
#ifdef DUMP_I2C_REQ
static void __dump_i2c_req(struct virtio_i2c_req *req)
{
	int len;
	pr_info("\nVIRTIO I2C REQ, TYPE: %s\n"
		   "\rSTATUS:%08x\n"
		   "\rLEN   :%08x\n"
		   "\rADDR  :%08x\n"
		   "\rFLAGS :%08x\n"
		   "\rHEAD  :%d\n"
		   "\rRIOV_U:%d WIOV_U:%d\n",
		   (req->outhdr->type & VIRTIO_I2C_WRITE)?"WRITE":"READ",
			req->inhdr->status, req->outhdr->len,
			req->outhdr->addr, req->outhdr->flags,
			req->iov.head,
			req->iov.out.used, req->iov.in.used);

	len = (req->msg.len > 256) ? 256 : req->msg.len;
	print_hex_dump(KERN_INFO, "virtio_i2c: ",
			DUMP_PREFIX_NONE, 16, 1,
			req->msg.buf, len, false);
}
#endif

static int __i2c_handle_request(struct virtio_i2c_req *req)
{
	struct i2c_adapter *adap = req->vi2c->adapter;

	if (!adap)
		return -EINVAL;
#ifdef DUMP_I2C_REQ
	__dump_i2c_req(req);
#endif
	return i2c_transfer(adap, &req->msg, 1);
}

static void virti2c_free_request(struct virtio_i2c_req *req)
{
	vringh_kiov_cleanup(&req->iov.out);
	vringh_kiov_cleanup(&req->iov.in);

	kfree(req);
}

static void virti2c_req_done(void *opaque, int status)
{
	struct virtio_i2c_req *req = (struct virtio_i2c_req *)opaque;
	struct virtio_i2c *vi2c = req->vi2c;

	req->inhdr->status = status;

	/* Push in-data (read/write & status data) to vq */
	vringh_push_kern(vi2c->vrh, &req->iov.out,
			&req->iov.in, req->iov.head, 0);
	vringh_notify(vi2c->vrh);

	virti2c_free_request(req);
}

static
struct virtio_i2c_req *virti2c_alloc_request(struct virtio_i2c *vi2c)
{
	struct virtio_i2c_req *req = kzalloc(sizeof(*req), GFP_KERNEL);

	if (!req) {
		pr_err("Out of memory! %s\n", __func__);
		return NULL;
	}

	vringh_kiov_init(&req->iov.out, NULL, 0);
	vringh_kiov_init(&req->iov.in, NULL, 0);
	req->vi2c = vi2c;

	return req;
}

static
struct virtio_i2c_req *virti2c_get_request(struct virtio_i2c *vi2c)
{
	int err;
	struct virtio_i2c_req *req;

	req = virti2c_alloc_request(vi2c);
	if (!req)
		return NULL;

	err = vringh_pop_kern(vi2c->vrh, &req->iov.out, &req->iov.in,
				&req->iov.head, GFP_KERNEL);
	if (err)
		goto failed;

	return req;

failed:
	virti2c_free_request(req);
	return NULL;
}

/* handle the request from remote processor */
static void virti2c_handle_request(struct virtio_i2c_req *req)
{
	uint32_t type;
	int ret;
	struct virtio_i2c_iov *iov = &req->iov;

	if (iov->out.used < 1 || iov->in.used < 1) {
		pr_err("virtio-i2c missing headers\n");
		BUG_ON(1);
	}

	if (iov->out.iov[0].iov_len < sizeof(*req->outhdr) ||
		iov->in.iov[iov->in.used - 1].iov_len <
		sizeof(*req->inhdr)) {
		pr_err("virtio-i2c header not in correct element\n");
		BUG_ON(1);
	}

	req->outhdr = (void *)iov->out.iov[0].iov_base;
	req->inhdr = (void *)iov->in.iov[iov->in.used - 1].iov_base;

	req->msg.addr = req->outhdr->addr;
	req->msg.flags = req->outhdr->flags;
	req->msg.len = req->outhdr->len;

	type = req->outhdr->type;

	pr_debug("IOV: in:%d out:%d\n", iov->out.used, iov->in.used);

	if (type & VIRTIO_I2C_WRITE) {
		req->msg.buf = (u8 *)iov->out.iov[1].iov_base;
		ret = __i2c_handle_request(req);
	} else if (type & VIRTIO_I2C_READ) {
		req->msg.buf = (u8 *)iov->in.iov[0].iov_base;
		ret = __i2c_handle_request(req);
	} else
		ret = -EINVAL;

	virti2c_req_done(req, ret);
}

/* virti2c_vrh_ist - vringh data handle thread
 * @vi2c_ptr: virtio i2c instance
 *
 * We put request to be handled in ist, because this phase
 * will do I2C IO operation.
 *
 */
static int virti2c_vrh_ist(void *vi2c_ptr)
{
	struct virtio_i2c *vi2c = (struct virtio_i2c *)vi2c_ptr;
	struct virtio_i2c_req *req;

	do {
		wait_event_interruptible(vi2c->outq,
			(req = virti2c_get_request(vi2c)));

		vringh_notify_disable_kern(vi2c->vrh);

		do {
			virti2c_handle_request(req);
			req = virti2c_get_request(vi2c);
		} while (req);

		vringh_notify_enable_kern(vi2c->vrh);
	} while (1);

	return 0;
}


/* vringh has data arrive intterrupt IRQ */
static void virti2c_vrh_isr(struct virtio_device *vdev,
				struct vringh *vrh)
{
	struct virtio_i2c *vi2c = vdev->priv;
	dev_dbg(&vdev->dev, "%s\n", __func__);
	/* wake up hanlde thread */
	wake_up(&vi2c->outq);
}


static int virti2c_turn_online(struct virtio_device *vdev)
{
	struct virtio_i2c *vi2c = vdev->priv;
	vrh_callback_t *vrh_cbs[] = {virti2c_vrh_isr};
	struct vringh *vrhs[1];
	int ret;

	if (vi2c->vrh)	{
		dev_dbg(&vdev->dev,
			"virtio i2c vringh had been configed already!\n");
		return 0;
	}
	/* Frontend has prepared the VQ's desc table already.
	 * Now, we can setup the VQs in backend
	 */
	/* We use a bidirectional virtqueue, so we expect one virtqueues */
	ret = vdev->vringh_config->find_vrhs(vdev, 1, vrhs, vrh_cbs);
	if (ret)
		return ret;

	vi2c->vrh = vrhs[0];

	/* Start the thread to handle output data in virtqueue */
	vi2c->vq_task = kthread_run(virti2c_vrh_ist,
				(void *)vi2c, "virti2c_vrh_ist");

	/* tell the remote processor backend is online */
	virtio_cwrite32(vdev, MMIO_BACK_ONLINE, vdev->index);

	dev_info(&vdev->dev, "online\n");
	return 0;
}

#ifdef CONFIG_PM_SLEEP
/* pm callbacks */
static int virti2c_suspend(struct device *dev)
{
	struct virtio_device *vdev = dev_to_virtio(dev);
	struct virtio_i2c *vi2c = vdev->priv;

	dev_dbg(dev, "do suspend!");

	/* release the real i2c adapter */
	i2c_put_adapter(vi2c->adapter);
	return 0;
}

static int virti2c_resume(struct device *dev)
{
	struct virtio_device *vdev = dev_to_virtio(dev);
	struct virtio_i2c *vi2c = vdev->priv;

	dev_dbg(dev, "do resume!");
	/* get real i2c adapter with saved bus id */
	vi2c->adapter = i2c_get_adapter(vi2c->bus_id);
	if (!vi2c->adapter) {
		dev_err(dev,
			"virtio i2c could not open real i2c adapter!\n");
		return -ENODEV;
	}

	return 0;
}
#endif

static int virti2c_mmio(struct virtio_device *vdev, u32 offset)
{
	u32 value_u = 0;

	switch (offset) {
	/************* RPROC Defined MMIO Handlers *****************/
	case MMIO_FRONT_ONLINE:
		value_u = virti2c_turn_online(vdev);
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

static int virti2c_hw_init(struct virtio_device *vdev,
				struct virtio_i2c *vi2c)
{
	struct virtio_i2c_desc *vi2c_desc;
	struct i2c_adapter *adapter;
	int len;

	/* read virtio i2c config data from private data */
	vi2c_desc = (struct virtio_i2c_desc *)
			virtio_cread32(vdev, MMIO_PRIV_DATA);
	if (!vi2c_desc) {
		dev_err(&vdev->dev,
			"virtio i2c doesn't specify real i2c adapter!\n");
		return -EINVAL;
	}

	/* get real i2c adapter with config data */
	adapter = i2c_get_adapter(vi2c_desc->i2c_adapter_id);
	if (!adapter) {
		dev_err(&vdev->dev,
			"virtio i2c could not open real i2c adapter!\n");
		return -ENODEV;
	}

	vi2c->bus_id = vi2c_desc->i2c_adapter_id;

	/* disable notify remote side when we initialize the config space */
	rproc_virtio_disable_notify(vdev);

	/* config virtio i2c mmio space with real adapter's info */
	virtio_cwrite32(vdev, I2C_MMIO_CLASS, adapter->class);
	virtio_cwrite32(vdev, I2C_MMIO_ADAPTER_NR, adapter->nr);
	virtio_cwrite32(vdev, I2C_MMIO_TIME_OUT, adapter->timeout);
	virtio_cwrite32(vdev, I2C_MMIO_RETRIES,	adapter->retries);

	/* write virtio i2c adapter customized name to mmio */
	len = strlen(vi2c_desc->name) + 1;
	len = (len > I2C_NAME_LENGTH) ? I2C_NAME_LENGTH : len;
	vdev->config->set(vdev, I2C_MMIO_VIRT_NAME,
				vi2c_desc->name, len);

	/* write real i2c adapter name to mmio */
	len = strlen(adapter->name) + 1;
	len = (len > I2C_NAME_LENGTH) ? I2C_NAME_LENGTH : len;
	vdev->config->set(vdev, I2C_MMIO_NAME, adapter->name, len);

	/* enable notify remote side */
	rproc_virtio_enable_notify(vdev);
	/* assign real i2c adapter to virtio i2c */
	vi2c->adapter = adapter;

	return 0;
}

static int virti2c_probe(struct virtio_device *vdev)
{
	struct virtio_i2c *vi2c;
	int err;

	vi2c = kzalloc(sizeof(*vi2c), GFP_KERNEL);
	if (!vi2c)
		return -ENOMEM;

	vi2c->vdev = vdev;
	err = virti2c_hw_init(vdev, vi2c);
	if (err)
		return err;

	mutex_init(&vi2c->vq_lock);

	init_waitqueue_head(&vi2c->outq);

	vdev->priv = vi2c;

	rproc_set_mmio_handler(vdev, virti2c_mmio);

	return 0;
}


static void virti2c_remove(struct virtio_device *vdev)
{
	struct virtio_i2c *vi2c = vdev->priv;

	vdev->config->reset(vdev);

	if (vi2c->vq_task)
		kthread_stop(vi2c->vq_task);

	vdev->vringh_config->del_vrhs(vi2c->vdev);

	i2c_put_adapter(vi2c->adapter);

	kfree(vi2c);
}

/* Setting the VIRTIO TYPE ID for this virtual device */
static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_I2C, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

/* You can define customized feaures bits, 0~16 is allowed */

/* virtual i2c virtio features */
static unsigned int features[] = {
	VIRTIO_RING_F_INDIRECT_DESC,
};

static SIMPLE_DEV_PM_OPS(virtio_i2c_pm, virti2c_suspend, virti2c_resume);
static struct virtio_driver virtio_i2c_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name = KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.id_table = id_table,
	.probe = virti2c_probe,
	.remove = virti2c_remove,
	.driver.pm = &virtio_i2c_pm,
};

static int __init virti2c_init(void)
{
	/* we regisger the virtio driver to drive virtio device */
	return register_virtio_driver(&virtio_i2c_driver);
}
module_init(virti2c_init);

static void __exit virti2c_fini(void)
{
	unregister_virtio_driver(&virtio_i2c_driver);
}
module_exit(virti2c_fini);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio i2c backend driver");
MODULE_LICENSE("GPL v2");
