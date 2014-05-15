/*
 * Virtio remoteproc serial backend driver
 *
 * Copyright (c) 2014 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/freezer.h>

#include <linux/virtio.h>
#include <linux/virtio_console.h>
#include <linux/vringh.h>

/*
 * This is a global struct for storing common data for all the devices
 * this driver handles.
 *
 */
struct ports_driver_data {
	/* Used for registering chardevs */
	struct class *class;

	/* List of all the devices we're handling */
	struct list_head portdevs;
};

static struct ports_driver_data pdrvdata;

struct port_buffer {
	char *data;
	size_t size;
	size_t offset;
	size_t left;
	struct vringh_kiov io;
	unsigned short head;
};

/*
 * virtio rproc serial port device instance
 */
struct ports_device {
	/* The virtio device we're associated with */
	struct virtio_device *vdev;
	/* The 'id' to identify the port with the Host */
	u32 id;
	/* The IO vqs for this port */
	struct vringh *in_vrh, *out_vrh;
	/* Major number for this device.  Ports will be created as minors. */
	int chr_major;
	/* The current buffer from which data has to be fed to readers */
	struct port_buffer readbuf, writebuf;

	/* To protect the vq operations for the control channel */
	spinlock_t inbuf_lock;
	spinlock_t outvq_lock;

	/* Each port associates with a separate char device */
	struct cdev cdev;
	struct device *dev;
	/* A waitqueue for poll() or blocking read operations */
	wait_queue_head_t waitqueue;
	/* The 'name' of the port that we expose via sysfs properties */
	char *name;
	/* We can notify apps of host connect / disconnect events via SIGIO */
	struct fasync_struct *async_queue;

	bool connected;
	bool is_open;
	bool discard;
};

static int discard_data(struct ports_device *portdev)
{
	int err;
	struct vringh_kiov out;
	unsigned short head;

	if (!portdev->discard)
		return -EACCES;

	vringh_kiov_init(&out, NULL, 0);

	err = vringh_pop_kern(portdev->out_vrh, &out,
			NULL, &head, GFP_KERNEL);
	if (err)
		goto clean;

	/* Push in-data (read/write & status data) to vq */
	vringh_push_kern(portdev->out_vrh, &out, NULL,
			head, 0);
	vringh_notify(portdev->out_vrh);

clean:
	vringh_kiov_cleanup(&out);

	return err;
}

static void clear_buffer(struct ports_device *portdev,
			struct vringh *vrh)
{
	struct port_buffer *buf;

	if (vrh == portdev->out_vrh) {
		buf = &portdev->readbuf;
		vringh_push_kern(vrh, &buf->io, NULL, buf->head, 0);
	} else {
		buf = &portdev->writebuf;
		vringh_push_kern(vrh, NULL, &buf->io,
					buf->head, buf->offset);
	}

	buf->data = NULL;
	buf->size = 0;
	buf->left = 0;
	buf->offset = 0;

	/* Clean up the kiov */
	vringh_kiov_cleanup(&buf->io);
	vringh_notify(vrh);
}

static size_t get_buffer(struct ports_device *portdev,
				struct vringh *vrh)
{
	int ret;
	struct port_buffer *buf;

	if (vrh == portdev->out_vrh) {
		buf = &portdev->readbuf;
		if (buf->left)
			return buf->left;

		vringh_kiov_init(&buf->io, NULL, 0);
		ret = vringh_pop_kern(vrh, &buf->io, NULL,
				&buf->head, GFP_KERNEL);

	} else {
		buf = &portdev->writebuf;
		if (buf->size)
			return buf->size;

		vringh_kiov_init(&buf->io, NULL, 0);
		ret = vringh_pop_kern(vrh, NULL, &buf->io,
			&buf->head, GFP_KERNEL);
	}

	if (ret) {
		if (ret != -ENOSPC) {
			dev_err(&portdev->vdev->dev,
				"pop data from VRING failed!err=%d\n", ret);
			BUG_ON(1);
		}

		/* Clean up the kiov */
		vringh_kiov_cleanup(&buf->io);
		return 0;
	}

	if ((buf->io.used > 1)) {
		dev_err(&portdev->vdev->dev,
			"virtio console data only can have ONE io_vector\n");
		BUG_ON(buf->io.used > 1);
	}

	buf->data = (char *)buf->io.iov[0].iov_base;
	buf->size = buf->io.iov[0].iov_len;
	buf->left = buf->size;
	buf->offset = 0;

	print_hex_dump(KERN_DEBUG, "virtcons_read:", DUMP_PREFIX_NONE,
		16, 1, buf->data, buf->size, true);

	return buf->size;
}

static ssize_t port_fops_read(struct file *filp, char __user *ubuf,
			      size_t count, loff_t *offp)
{
	struct ports_device *portdev;
	struct port_buffer *buf;
	ssize_t ret, buf_len, copy_len;

	portdev = filp->private_data;
	buf = &portdev->readbuf;

	/* Port is closed. */
	if (!portdev->is_open)
		return -ENODEV;

	buf_len = get_buffer(portdev, portdev->out_vrh);
	if (!buf_len) {
		if (!portdev->connected)
			return 0;

		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		ret = wait_event_freezable(portdev->waitqueue,
			(buf_len = get_buffer(portdev, portdev->out_vrh))
			&& (buf_len > 0));
		if (ret < 0)
			return ret;
	}

	copy_len = min(count, buf->left);

	ret = copy_to_user(ubuf, buf->data + buf->offset, copy_len);
	BUG_ON(ret);

	buf->offset += copy_len;
	buf->left = buf->size - copy_len;

	if (!buf->left)
		clear_buffer(portdev, portdev->out_vrh);

	return copy_len;
}

static ssize_t port_fops_write(struct file *filp, const char __user *ubuf,
			       size_t count, loff_t *offp)
{
	struct ports_device *portdev;
	struct port_buffer *buf;
	ssize_t ret, buf_len, copy_len;

	portdev = filp->private_data;
	buf = &portdev->writebuf;

	/* Port is opened. */
	if (!portdev->is_open)
		return -ENODEV;

	buf_len = get_buffer(portdev, portdev->in_vrh);
	if (!buf_len) {
		if (!portdev->connected)
			return 0;

		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		ret = wait_event_freezable(portdev->waitqueue,
			(buf_len = get_buffer(portdev, portdev->in_vrh))
			&& (buf_len > 0));
		if (ret < 0)
			return ret;
	}

	copy_len = min(count, buf->size);

	ret = copy_from_user(buf->data, ubuf, copy_len);
	BUG_ON(ret);

	buf->offset += copy_len;
	buf->left = buf->size - copy_len;
	clear_buffer(portdev, portdev->in_vrh);

	return copy_len;
}

static unsigned int port_fops_poll(struct file *filp, poll_table *wait)
{
	struct ports_device *portdev;
	unsigned int ret = 0;

	portdev = filp->private_data;
	poll_wait(filp, &portdev->waitqueue, wait);

	if (!portdev->is_open) {
		/* Port got closed */
		return POLLHUP;
	}

	if (get_buffer(portdev, portdev->out_vrh))
		ret |= POLLIN | POLLRDNORM;
	if (get_buffer(portdev, portdev->in_vrh))
		ret |= POLLOUT;
	if (!portdev->connected)
		ret |= POLLHUP;

	return ret;
}

static int port_fops_release(struct inode *inode, struct file *filp)
{
	struct ports_device *portdev;
	int err;

	portdev = filp->private_data;

	spin_lock_irq(&portdev->inbuf_lock);
	portdev->is_open = false;

	spin_unlock_irq(&portdev->inbuf_lock);

	/* discard data remainded in vring */
	do {
		err = discard_data(portdev);
	} while (!err);

	return 0;
}

static int port_fops_open(struct inode *inode, struct file *filp)
{
	struct cdev *p_cdev = inode->i_cdev;
	struct ports_device *portdev;

	/* We get the portdev with a cdev here */
	portdev = container_of(p_cdev, struct ports_device, cdev);

	filp->private_data = portdev;

	/* Allow only one process to open a particular port at a time */
	spin_lock_irq(&portdev->inbuf_lock);
	if (portdev->is_open) {
		spin_unlock_irq(&portdev->inbuf_lock);
		return -EBUSY;
	}

	portdev->is_open = true;
	spin_unlock_irq(&portdev->inbuf_lock);

	nonseekable_open(inode, filp);

	return 0;
}

static int port_fops_fasync(int fd, struct file *filp, int mode)
{
	struct ports_device *portdev;

	portdev = filp->private_data;

	return fasync_helper(fd, filp, mode, &portdev->async_queue);
}

/*
 * The file operations that we support: programs in the guest can open
 * a console device, read from it, write to it, poll for data and
 * close it.  The devices are at
 *   /dev/vport<device number>p<port number>
 */
static const struct file_operations port_fops = {
	.owner = THIS_MODULE,
	.open  = port_fops_open,
	.read  = port_fops_read,
	.write = port_fops_write,
	.poll  = port_fops_poll,
	.release = port_fops_release,
	.fasync = port_fops_fasync,
	.llseek = no_llseek,
};

static void virtcons_out_isr(struct virtio_device *vdev,
				struct vringh *vrh)
{
	struct ports_device *portdev;

	portdev = vdev->priv;

	if (portdev->is_open) {
		/* wake up read thread */
		wake_up_interruptible(&portdev->waitqueue);
		return;
	}

	/* The console had not been opend, so discard the data */
	discard_data(portdev);
}

static void virtcons_in_isr(struct virtio_device *vdev,
				struct vringh *vrh)
{
	struct ports_device *portdev;

	portdev = vdev->priv;

	if (portdev->is_open) {
		/* wake up write wait thread */
		wake_up_interruptible(&portdev->waitqueue);
		return;
	}
}

static int init_vqs(struct ports_device *portdev)
{
	vrh_callback_t *vrh_cbs[] = {virtcons_in_isr, virtcons_out_isr};
	struct vringh *vrhs[2];
	int err;

	/* Find the queues. */
	err = portdev->vdev->vringh_config->find_vrhs(portdev->vdev,
					2, vrhs, vrh_cbs);
	if (err)
		return err;

	portdev->in_vrh = vrhs[0];
	portdev->out_vrh = vrhs[1];

	return 0;
}

static int init_port(struct ports_device *portdev)
{
	struct virtio_rproc_console_desc *virtcons_desc;
	dev_t devt;
	int err, len;

	/* read virtio console config data from private data */
	virtcons_desc = (struct virtio_rproc_console_desc *)
			virtio_cread32(portdev->vdev, MMIO_PRIV_DATA);
	if (!virtcons_desc) {
		dev_err(&portdev->vdev->dev,
			"virtio console doesn't have private data!\n");
		return -EINVAL;
	}

	portdev->id = virtcons_desc->id;
	portdev->name = virtcons_desc->name;

	/* disable notify remote side when we initialize the config space */
	rproc_virtio_disable_notify(portdev->vdev);

	/* config virtio console mmio space */
	virtio_cwrite32(portdev->vdev,
			CONSOLE_MMIO_PORT_ID, virtcons_desc->id);

	/* write virtio console customized name to mmio */
	len = strlen(virtcons_desc->name) + 1;
	len = (len > CONSOLE_MMIO_NAME_LEN) ? CONSOLE_MMIO_NAME_LEN : len;
	portdev->vdev->config->set(portdev->vdev,
			CONSOLE_MMIO_NAME, virtcons_desc->name, len);

	/* enable notify remote side */
	rproc_virtio_enable_notify(portdev->vdev);

	portdev->async_queue = NULL;

	portdev->connected = false;
	portdev->is_open = false;
	portdev->discard = false;

	cdev_init(&portdev->cdev, &port_fops);

	devt = MKDEV(portdev->chr_major, 0);
	err = cdev_add(&portdev->cdev, devt, 1);
	if (err < 0) {
		dev_err(&portdev->vdev->dev,
			"Error %d adding cdev for port %u\n",
			err, portdev->id);
		return err;
	}

	portdev->dev = device_create(pdrvdata.class, &portdev->vdev->dev,
				  devt, portdev, "%s%u",
				  portdev->name, portdev->id);
	if (IS_ERR(portdev->dev)) {
		err = PTR_ERR(portdev->dev);
		dev_err(&portdev->vdev->dev,
			"Error %d creating device for port %u\n",
			err, portdev->id);
		goto del_cdev;
	}

	spin_lock_init(&portdev->inbuf_lock);
	spin_lock_init(&portdev->outvq_lock);
	init_waitqueue_head(&portdev->waitqueue);

	return 0;

del_cdev:
	cdev_del(&portdev->cdev);
	return err;
}

static void remove_vqs(struct ports_device *portdev)
{
	portdev->vdev->vringh_config->del_vrhs(portdev->vdev);
}

static int virtcons_turn_online(struct virtio_device *vdev)
{
	struct ports_device *portdev;
	int err;

	portdev = (struct ports_device *)vdev->priv;
	err = init_vqs(portdev);
	if (err < 0) {
		dev_err(&vdev->dev, "Error %d initializing vqs\n", err);
		return err;
	}

	portdev->connected = true;
	/* Tell the remote processor, front is online. */
	virtio_cwrite32(vdev, MMIO_BACK_ONLINE, vdev->index);

	dev_info(&vdev->dev, "online\n");
	return 0;
}

static int virtcons_mmio(struct virtio_device *vdev, u32 offset)
{
	int val = 0;

	switch (offset) {
	/************* RPROC Defined MMIO Handlers *****************/
	case MMIO_FRONT_ONLINE:
		val = virtcons_turn_online(vdev);
		break;

	case MMIO_FRONT_VQ_READY:
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

static const struct file_operations portdev_fops = {
	.owner = THIS_MODULE,
};

static int virtcons_probe(struct virtio_device *vdev)
{
	struct ports_device *portdev;
	int err;

	portdev = kzalloc(sizeof(*portdev), GFP_KERNEL);
	if (!portdev) {
		dev_err(&vdev->dev, "Out of memory!\n");
		return -ENOMEM;
	}

	/* Attach this portdev to this virtio_device, and vice-versa. */
	portdev->vdev = vdev;
	vdev->priv = portdev;

	portdev->chr_major = register_chrdev(0, "virtio-portsdev",
					     &portdev_fops);
	if (portdev->chr_major < 0) {
		dev_err(&vdev->dev,
			"Error %d registering chrdev for device %u\n",
			portdev->chr_major, vdev->index);
		err = portdev->chr_major;
		goto free;
	}

	/*
	 * Initialize the console port
	 */
	err = init_port(portdev);
	if (err)
		goto free_chrdev;

	rproc_set_mmio_handler(vdev, virtcons_mmio);

	return 0;

free_chrdev:
	unregister_chrdev(portdev->chr_major, "virtio-portsdev");
free:
	kfree(portdev);
	return err;
}

static void virtcons_remove(struct virtio_device *vdev)
{
	struct ports_device *portdev;

	portdev = vdev->priv;

	/* Disable interrupts for vqs */
	vdev->config->reset(vdev);

	remove_vqs(portdev);
	kfree(portdev);
}

static struct virtio_device_id rproc_serial_id_table[] = {
	{ VIRTIO_ID_RPROC_SERIAL, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int rproc_serial_features[] = {
};

static struct virtio_driver virtio_rproc_serial = {
	.feature_table = rproc_serial_features,
	.feature_table_size = ARRAY_SIZE(rproc_serial_features),
	.driver.name =	"virtio_rproc_serial",
	.driver.owner =	THIS_MODULE,
	.id_table =	rproc_serial_id_table,
	.probe =	virtcons_probe,
	.remove =	virtcons_remove,
};

static int __init init(void)
{
	int err;

	pdrvdata.class = class_create(THIS_MODULE, "virtio-ports");
	if (IS_ERR(pdrvdata.class)) {
		err = PTR_ERR(pdrvdata.class);
		pr_err("Error %d creating virtio-ports class\n", err);
		return err;
	}

	INIT_LIST_HEAD(&pdrvdata.portdevs);

	err = register_virtio_driver(&virtio_rproc_serial);
	if (err < 0) {
		pr_err("Error %d registering virtio rproc serial driver\n",
		       err);
		goto free;
	}
	return 0;

free:
	class_destroy(pdrvdata.class);
	return err;
}

static void __exit fini(void)
{
	unregister_virtio_driver(&virtio_rproc_serial);

	class_destroy(pdrvdata.class);
}
module_init(init);
module_exit(fini);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio remoteproc serial driver");
MODULE_LICENSE("GPL");
