/*
 * CSRVisor wrapper driver
 *
 * Copyright (c) 2014 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/dma-mapping.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/of.h>
#include <asm/cacheflush.h>

#define CSRVISOR_CPU	0

/* wait type for wait queue */
#define CSRVISOR_WAIT_REQ	0
#define CSRVISOR_WAIT_RES	1

/* command for csrvisor io */
#define IOCTL_CMD_CSRVISOR_IO	0x70000001

/* function commands -- get chip uid for user */
#define CVIO_CMD_GET_CHIPUID	0x70000004
/* Chip ID length fixed at 16 bytes */
#define DEVICE_CHIPUID_LENGTH	16

#define CMD_PACKET_MAGIC	0x6376696F
struct cmd_packet {
	int magic;		/* in - magic number */
	int cmd;		/* in - command */
	int status;		/* out - command status */
	void *in_buf;		/* in - input buffer */
	int in_len;		/* in - input buffer length */
	void *out_buf;		/* out - output buffer */
	int out_len;		/* in/out - output buffer length*/
};

struct csrvisor_wrapper {
	struct task_struct *wrapper_thread;
	struct mutex call_mutex;
	wait_queue_head_t wqueue;
	int wq_wait_type;
	atomic_t count;
	struct cmd_packet *xfer_pkt;
	struct miscdevice wrapper_dev;
};

static inline void csrvisor_fastcall(void *ptr)
{
	register unsigned long r0 asm("r0") = 0x80000001;
	register unsigned long r1 asm("r1") = (unsigned long)ptr;

	__asm__ __volatile__(".arch_extension sec\n\t"
		"dsb\n\t"
		"smc #0" :		/* no output */
		: "r"(r0), "r"(r1)
		: "memory");
}

#ifdef CONFIG_SMP
static int csrvisor_wrapper_thread(void *data)
{
	struct csrvisor_wrapper *cw_data = data;

	while (1) {
		wait_event_interruptible(cw_data->wqueue,
			cw_data->wq_wait_type == CSRVISOR_WAIT_REQ ||
			kthread_should_stop());

		if (kthread_should_stop())
			break;

		/* do fastcall */
		csrvisor_fastcall(cw_data->xfer_pkt);

		/* wake up reader */
		cw_data->wq_wait_type = CSRVISOR_WAIT_RES;
		wake_up(&cw_data->wqueue);
	}

	return 0;
}
#endif

static int do_csrvisor_fastcall(struct csrvisor_wrapper *cw_data)
{
#ifdef CONFIG_SMP
	if (smp_processor_id() != CSRVISOR_CPU) {
		cw_data->wq_wait_type = CSRVISOR_WAIT_REQ;
		wake_up(&cw_data->wqueue);
		return wait_event_interruptible(cw_data->wqueue,
			cw_data->wq_wait_type == CSRVISOR_WAIT_RES);
	}
#endif

	csrvisor_fastcall(cw_data->xfer_pkt);
	return 0;
}

static int csrvisor_wrapper_open(struct inode *inode, struct file *file)
{
	struct csrvisor_wrapper *cw_data = container_of(file->private_data,
			struct csrvisor_wrapper, wrapper_dev);

	if (!atomic_add_unless(&cw_data->count, 1, 1))
		return -EBUSY;

	return 0;
}
static int csrvisor_wrapper_close(struct inode *inode, struct file *file)
{
	struct csrvisor_wrapper *cw_data = container_of(file->private_data,
			struct csrvisor_wrapper, wrapper_dev);

	atomic_dec(&cw_data->count);

	return 0;
}

static int map_packet_and_call(struct cmd_packet *user_pkt,
				struct csrvisor_wrapper *cw_data)
{
	int ret;
	size_t pkt_size, offset;
	struct cmd_packet *xfer_pkt;
	struct device *dev;
	dma_addr_t dma_addr;

	dev = cw_data->wrapper_dev.this_device;

	if (user_pkt->magic != CMD_PACKET_MAGIC)
		return -EINVAL;

	/*
	* create transport packet -- csrvisor uses physical address
	* alloc VA in local and store mapped PA in transport packet
	* uses dma coherent since csrvisor works as hardware
	*/
	pkt_size = sizeof(struct cmd_packet)
				+ user_pkt->in_len
				+ user_pkt->out_len;
	xfer_pkt = dma_zalloc_coherent(
				dev,
				pkt_size,
				&dma_addr,
				GFP_KERNEL);
	if (!xfer_pkt)
		return -ENOMEM;

	xfer_pkt->magic = user_pkt->magic;
	xfer_pkt->cmd = user_pkt->cmd;
	xfer_pkt->status = user_pkt->status;

	/* map input buffer if there are */
	if (user_pkt->in_buf && user_pkt->in_len) {
		offset = sizeof(struct cmd_packet);
		if (copy_from_user((char *)xfer_pkt + offset,
				user_pkt->in_buf,
				user_pkt->in_len)) {
			ret = -EFAULT;
			goto __pkt_exit;
		}
		xfer_pkt->in_buf = (void *)(dma_addr + offset);
		xfer_pkt->in_len = user_pkt->in_len;
	}

	/* map output buffer if there are */
	if (user_pkt->out_buf && user_pkt->out_len) {
		offset = sizeof(struct cmd_packet) + user_pkt->in_len;
		xfer_pkt->out_buf = (void *)(dma_addr + offset);
		xfer_pkt->out_len = user_pkt->out_len;
	}

	/* csrvisor handles DMA addr */
	cw_data->xfer_pkt = (struct cmd_packet *)dma_addr;

	/* push to csrviosr */
	ret = do_csrvisor_fastcall(cw_data);

	/* update returned status */
	user_pkt->status = xfer_pkt->status;
	user_pkt->out_len = xfer_pkt->out_len;

	if (xfer_pkt->out_buf) {
		offset = sizeof(struct cmd_packet) + user_pkt->in_len;
		if (copy_to_user(user_pkt->out_buf,
				(char *)xfer_pkt + offset,
				user_pkt->out_len))
			ret = -EFAULT;
	}

__pkt_exit:
	dma_free_coherent(dev, pkt_size, xfer_pkt, dma_addr);
	cw_data->xfer_pkt = NULL;
	return ret;
}

static long csrvisor_wrapper_ioctl(struct file *file,
			unsigned int cmd, unsigned long arg)
{
	int ret;
	struct cmd_packet *user_pkt;
	struct csrvisor_wrapper *cw_data = container_of(file->private_data,
			struct csrvisor_wrapper, wrapper_dev);

	if (cmd != IOCTL_CMD_CSRVISOR_IO)
		return -EINVAL;

	if (!access_ok(VERIFY_READ, arg, sizeof(struct cmd_packet)))
		return -EFAULT;

	user_pkt = (struct cmd_packet *)arg;

	/*
	* handle one packet only during once call,
	* lock packet transport process
	*/
	mutex_lock(&cw_data->call_mutex);
	ret = map_packet_and_call(user_pkt, cw_data);
	mutex_unlock(&cw_data->call_mutex);

	return ret;
}

static const struct file_operations csrviosr_wrapper_fops = {
	.owner		=	THIS_MODULE,
	.open		=	csrvisor_wrapper_open,
	.release	=	csrvisor_wrapper_close,
	.unlocked_ioctl	=	csrvisor_wrapper_ioctl,
};

static struct csrvisor_wrapper cw_private_glob = {
	.wrapper_dev.minor	=	128,
	.wrapper_dev.name	=	"cvwrapper",
	.wrapper_dev.fops	=	&csrviosr_wrapper_fops,
};

/* provided an interface to get chip id for user via sysfs */
static ssize_t chip_uid_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cmd_packet *local_pkt;
	unsigned int *chip_uid;
	size_t pkt_size, str_size;
	dma_addr_t dma_addr;
	struct csrvisor_wrapper *cw_data = &cw_private_glob;

	/* allocate local packet directly */
	pkt_size = sizeof(struct cmd_packet)
				+ DEVICE_CHIPUID_LENGTH;

	local_pkt = dma_zalloc_coherent(dev, pkt_size, &dma_addr, GFP_KERNEL);
	if (!local_pkt)
		return 0;

	/* build internal packet */
	local_pkt->magic = CMD_PACKET_MAGIC;
	local_pkt->cmd = CVIO_CMD_GET_CHIPUID;
	local_pkt->status = 0;
	local_pkt->in_buf = NULL; /* input unnecessary */
	local_pkt->in_len = 0;
	local_pkt->out_buf = (void *)(dma_addr + sizeof(struct cmd_packet));
	local_pkt->out_len = DEVICE_CHIPUID_LENGTH;

	/* lock & go */
	mutex_lock(&cw_data->call_mutex);
	cw_data->xfer_pkt = (struct cmd_packet *)dma_addr;
	do_csrvisor_fastcall(cw_data);
	cw_data->xfer_pkt = NULL;
	mutex_unlock(&cw_data->call_mutex);

	/* return */
	chip_uid = (unsigned int *)(local_pkt + 1);
	str_size = sprintf(buf, "%08x%08x%08x%08x\n",
			chip_uid[0], chip_uid[1], chip_uid[2], chip_uid[3]);
	dma_free_coherent(dev, pkt_size, local_pkt, dma_addr);

	return str_size;
}

static DEVICE_ATTR_RO(chip_uid);

static __init int csrvisor_wrapper_init(void)
{
	struct csrvisor_wrapper *cw_data = &cw_private_glob;
	int ret;

	if (!of_machine_is_compatible("sirf,atlas7"))
		return -EINVAL;

	/* register device */
	ret = misc_register(&cw_data->wrapper_dev);
	if (ret) {
		pr_err("failed to register misc device\n");
		return ret;
	}

	ret = dma_set_coherent_mask(cw_data->wrapper_dev.this_device,
				DMA_BIT_MASK(32));
	if (ret) {
		pr_err("failed to set dma coherent mask:%d\n", ret);
		goto __fail;
	}

#ifdef CONFIG_SMP
	cw_data->wq_wait_type = CSRVISOR_WAIT_RES;
	init_waitqueue_head(&cw_data->wqueue);
	mutex_init(&cw_data->call_mutex);
	atomic_set(&cw_data->count, 0);

	/* working thread */
	cw_data->wrapper_thread = kthread_create(
					csrvisor_wrapper_thread,
					cw_data,
					"csrvisor_wrapper_thread");
	if (IS_ERR(cw_data->wrapper_thread)) {
		pr_err("failed to create csrvisor_wrapper_thread\n");
		ret = PTR_ERR(cw_data->wrapper_thread);
		goto __fail;
	}

	/* bind to cpu 0 */
	kthread_bind(cw_data->wrapper_thread, CSRVISOR_CPU);
	wake_up_process(cw_data->wrapper_thread);
#endif
	device_create_file(cw_data->wrapper_dev.this_device,
		&dev_attr_chip_uid);

	return 0;

__fail:
	misc_deregister(&cw_data->wrapper_dev);
	return ret;
}
module_init(csrvisor_wrapper_init);

static void __exit csrvisor_wrapper_exit(void)
{
	struct csrvisor_wrapper *cw_data = &cw_private_glob;
#ifdef CONFIG_SMP
	kthread_stop(cw_data->wrapper_thread);
#endif
	misc_deregister(&cw_data->wrapper_dev);
}
module_exit(csrvisor_wrapper_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("CSRVisor wrapper driver");
