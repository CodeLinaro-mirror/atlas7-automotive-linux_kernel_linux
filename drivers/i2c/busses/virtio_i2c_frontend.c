/*
 * Virtio i2c frontend driver
 *
 * Copyright (c) 2013 Cambridge Silicon Radio Limited, a CSR plc group company.
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
#include <linux/remoteproc.h>
#include <linux/remoteproc_dualos.h>

#include <linux/virtio_i2c.h>

/**
 * struct virtio_i2c - virtio i2c device
 * @adapter: i2c adapter
 * @vdev: the virtio device
 * @vq: i2c virtqueue
 * @i2c_nr: i2c bus id that was allocated in backend OS
 * @status: i2c bus status
 * @lock: i2c bus lock
 * @vq_lock: protects vq,to allow concurrent senders.
 *
 */
struct virtio_i2c {
	struct i2c_adapter adapter;
	struct virtio_device *vdev;
	struct virtqueue *vq;
	int i2c_nr;
	int status;
	struct mutex lock;
	struct mutex vq_lock;
	wait_queue_head_t inq;
};

struct virtio_i2c_req {
	struct i2c_msg *msg;
	struct virti2c_req_outhdr outhdr;
	struct virti2c_req_inhdr  inhdr;
	struct scatterlist sg[];
};

static void __map_msg_to_sg(struct scatterlist *sglist, struct i2c_msg *msg)
{
	if (sglist) {
		sg_set_buf(sglist, msg->buf, msg->len);
		sg_mark_end(sglist);
	}
}

#if 0
static void __map_msg_to_sgs(struct scatterlist *sglist,
				struct i2c_msg *msg, int num_of_buf)
{
	struct scatterlist *sg;
	int index;

	sg = sglist;

	for (index = 0; index < num_of_buf; index++) {
		if (sg)	{
			sg_set_buf(sg, msg->buf[index], msg->len[index]);
			sg = sg_next(sg);
		} else
			break;
	}

	if (sg)
		sg_mark_end(sg);

	return;
}
#endif

static ssize_t virti2c_send_to_i2c(struct virtio_i2c *vi2c,
				struct virtio_i2c_req *i2c_req)
{
	struct virtqueue *vq = vi2c->vq;
	struct scatterlist outhdr, inhdr, *sgs[3];
	unsigned int num_out = 0, num_in = 0, err, len;
	struct virtio_i2c_req *req_handled = NULL;

	sg_init_one(&outhdr, &i2c_req->outhdr, sizeof(i2c_req->outhdr));
	sgs[num_out++] = &outhdr;

	if (i2c_req->msg->len) {
		__map_msg_to_sg(i2c_req->sg, i2c_req->msg);
		if (i2c_req->outhdr.type & VIRTIO_I2C_WRITE)
			sgs[num_out++] = i2c_req->sg;
		else
			sgs[num_out + num_in++] = i2c_req->sg;
	}

	sg_init_one(&inhdr, &i2c_req->inhdr, sizeof(i2c_req->inhdr));
	sgs[num_out + num_in++] = &inhdr;

	err = virtqueue_add_sgs(vq, sgs, num_out, num_in, i2c_req, GFP_KERNEL);
	/* Tell Host to go! */
	virtqueue_kick(vq);
	if (err)
		goto req_exit;

	wait_event(vi2c->inq,
		(req_handled = virtqueue_get_buf(vq, &len)));

	err = req_handled->inhdr.status;

req_exit:
	return err;
}

/* Virtual I2C handle requests from upper layer drivers */
static int virti2c_do_req(struct virtio_i2c *vi2c, struct i2c_msg *msg)
{
	struct virtio_i2c_req *i2c_req;
	int err;

	if (!vi2c->status) {
		dev_dbg(&vi2c->vdev->dev, "virtio I2C is not ready!\n");
		err = -EBUSY;
		goto exit;
	}

	/* alloc a req metadata to manage real request.
	 * this metadata will be free when request is done or err
	 */
	i2c_req = kzalloc(sizeof(*i2c_req) + sizeof(struct scatterlist) * 1,
			GFP_KERNEL);
	if (!i2c_req) {
		dev_dbg(&vi2c->vdev->dev, "out of memory!\n");
		err = -ENOMEM;
		goto exit;
	}

	sg_init_table(i2c_req->sg, 1);

	i2c_req->outhdr.addr = msg->addr;
	i2c_req->outhdr.flags = msg->flags;
	i2c_req->outhdr.len = msg->len;
	if (msg->flags & I2C_M_RD)
		i2c_req->outhdr.type = VIRTIO_I2C_READ;
	else
		i2c_req->outhdr.type = VIRTIO_I2C_WRITE;

	i2c_req->msg = msg;

	mutex_lock(&vi2c->vq_lock);
	err = virti2c_send_to_i2c(vi2c, i2c_req);
	mutex_unlock(&vi2c->vq_lock);

	kfree(i2c_req);
exit:
	return err;
}

/*
 * I2C adapter master xfer does not support callback, so we have to call
 * this function in sync method, and free the msg after this call done
 */
static int virtio_i2c_master_xfer(struct i2c_adapter *adap,
				struct i2c_msg *msgs, int num)
{
	int i, retval;
	struct virtio_i2c *vi2c = adap->algo_data;

	for (i = 0; i < num; i++) {
		if (msgs[i].flags & I2C_M_RD)
			/* read */
			retval = virti2c_do_req(vi2c, &msgs[i]);
		else if (i + 1 < num && (msgs[i + 1].flags & I2C_M_RD) &&
			msgs[i].addr == msgs[i + 1].addr) {
			/* write then read from same address */
			retval = virti2c_do_req(vi2c, &msgs[i]);
			if (retval)
				goto err;
			i++;
			retval = virti2c_do_req(vi2c, &msgs[i]);
		} else
			/* write */
			retval = virti2c_do_req(vi2c, &msgs[i]);

		if (retval)
			goto err;
	}
	return 0;

err:
	return retval;
}
static u32 virtio_i2c_functionality(struct i2c_adapter *adapter)
{
	return  I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static struct i2c_algorithm virtio_i2c_algorithm = {
	.master_xfer = virtio_i2c_master_xfer,
	.functionality = virtio_i2c_functionality,
};

/* virtqueue incoming data intterrupt IRQ */
static void virti2c_vq_isr(struct virtqueue *vq)
{
	struct virtio_i2c *vi2c = vq->vdev->priv;

	dev_dbg(&vq->vdev->dev, "%s\n", __func__);
	/* Wake up the blocked read or write context */
	wake_up(&vi2c->inq);
}

static int __virti2c_find_match_node(struct virtio_device *vdev,
					struct virtio_i2c *vi2c)
{
	u32 bus_id;
	int err = 0;
	struct device_node *np;

	for_each_compatible_node(np, "virtio", "csr,virtio-i2c-frontend") {
		err = of_property_read_u32_index(np, "bus_id", 0, &bus_id);
		if (err)
			continue;

		if (bus_id == vi2c->adapter.nr) {
			vi2c->adapter.dev.of_node = np;
			break;
		}
	}

	return err;
}

/* virtual i2c backend online handler */
static int virti2c_turn_online(struct virtio_device *vdev)
{
	int err;

	struct virtio_i2c *vi2c = vdev->priv;

	mutex_lock(&vi2c->lock);
	/* Set local device status to ready */
	vi2c->status = 1;
	mutex_unlock(&vi2c->lock);

	/* try to add this i2c adapter will remote i2c nr */
	err = i2c_add_numbered_adapter(&vi2c->adapter);
	if (!err)
		return 0;

	/* try to add i2c adapter with dynamical bus id */
	return i2c_add_adapter(&vi2c->adapter);
}

static int virti2c_mmio(struct virtio_device *vdev, u32 offset)
{
	u32 value_u = 0;

	switch (offset) {
	/************* RPROC Defined MMIO Handlers *****************/
	case MMIO_BACK_ONLINE:
		value_u = virti2c_turn_online(vdev);
		break;

	case MMIO_BACK_OFFLINE:
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
	char real_name[I2C_NAME_LENGTH];

	vi2c->adapter.algo_data = vi2c;
	vi2c->adapter.dev.parent = &vdev->dev;
	vi2c->adapter.dev.of_node = NULL;

	/* Assign i2c adapter algorithm ops */
	vi2c->adapter.algo = &virtio_i2c_algorithm;
	vi2c->adapter.owner = THIS_MODULE;

	/* read virtio i2c config info from mmio */
	vi2c->adapter.class = virtio_cread32(vdev, I2C_MMIO_CLASS);
	vi2c->adapter.nr = virtio_cread32(vdev, I2C_MMIO_ADAPTER_NR);
	vi2c->adapter.timeout = virtio_cread32(vdev, I2C_MMIO_TIME_OUT);
	vi2c->adapter.retries = virtio_cread32(vdev, I2C_MMIO_RETRIES);

	/* read name info from MMIO */
	virtio_cread_bytes(vdev, I2C_MMIO_NAME,
				real_name, I2C_NAME_LENGTH);
	virtio_cread_bytes(vdev, I2C_MMIO_VIRT_NAME,
				vi2c->adapter.name, I2C_NAME_LENGTH);
	dev_dbg(&vdev->dev,
		"\rname:%s\n"
		"\rreal name:%s\n"
		"\rclass %d nr:%d timeout:%d retries:%d\n",
		vi2c->adapter.name, real_name,
		vi2c->adapter.class, vi2c->adapter.nr,
		vi2c->adapter.timeout, vi2c->adapter.retries);

	/* initialize locks */
	rt_mutex_init(&vi2c->adapter.bus_lock);
	mutex_init(&vi2c->adapter.userspace_clients_lock);
	INIT_LIST_HEAD(&vi2c->adapter.userspace_clients);

	/* start register virtio i2c adapter to i2c system */
	/* save i2c nr that was allcocated by backend OS */
	vi2c->i2c_nr = vi2c->adapter.nr;

	return __virti2c_find_match_node(vdev, vi2c);
}

static int virti2c_probe(struct virtio_device *vdev)
{
	vq_callback_t *vq_cbs[] = {virti2c_vq_isr};
	const char *names[] = {"virti2c_vq_isr" };
	struct virtqueue *vqs[1];
	struct virtio_i2c *vi2c;
	int err = 0;

	vi2c = kzalloc(sizeof(*vi2c), GFP_KERNEL);
	if (!vi2c)
		return -ENOMEM;

	vi2c->vdev = vdev;
	vi2c->status = 0;

	mutex_init(&vi2c->lock);
	mutex_init(&vi2c->vq_lock);
	init_waitqueue_head(&vi2c->inq);

	/* We use a bidirectional virtqueue, so we expect one virtqueues */
	err = vdev->config->find_vqs(vdev, 1, vqs, vq_cbs, names);
	if (err)
		goto free_vi2c;

	vi2c->vq = vqs[0];

	/* suppress "tx-complete" interrupts */
	virtqueue_disable_cb(vi2c->vq);

	vdev->priv = vi2c;

	rproc_set_mmio_handler(vdev, virti2c_mmio);

	err = virti2c_hw_init(vdev, vi2c);
	if (err)
		goto del_vqs;

	/* Tell the remote processor, front is online. */
	virtio_cwrite32(vdev, MMIO_FRONT_ONLINE, vdev->index);

	dev_info(&vdev->dev, "online\n");
	return 0;

del_vqs:
	vdev->config->del_vqs(vi2c->vdev);

free_vi2c:
	kfree(vi2c);

	return err;
}

static void virti2c_remove(struct virtio_device *vdev)
{
	struct virtio_i2c *vi2c = vdev->priv;

	i2c_del_adapter(&vi2c->adapter);

	vdev->config->reset(vdev);

	vdev->config->del_vqs(vi2c->vdev);

	kfree(vi2c);
}


/* Setting the VIRTIO TYPE ID for this virtual device driver */
static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_I2C, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

/* Setting the VIRTIO Feature Bits for this virtual device driver.
 * The bits are: 0~15. 16~31 are reserved.
 */
static unsigned int features[] = {};

static struct virtio_driver virtio_i2c_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name = KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.id_table = id_table,
	.probe = virti2c_probe,
	.remove = virti2c_remove,
};

static int __init virti2c_init(void)
{
	return register_virtio_driver(&virtio_i2c_driver);
}
module_init(virti2c_init);

static void __exit virti2c_fini(void)
{
	unregister_virtio_driver(&virtio_i2c_driver);
}
module_exit(virti2c_fini);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio i2c frontend driver");
MODULE_LICENSE("GPL v2");
