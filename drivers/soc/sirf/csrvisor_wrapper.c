/*
 * CSRVisor wrapper driver
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
#include <linux/clk.h>
#include <linux/hw_random.h>
#include <asm/cacheflush.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>

#define CSRVISOR_CPU	0

/* wait type for wait queue */
#define CSRVISOR_WAIT_REQ	0
#define CSRVISOR_WAIT_RES	1

/* command for csrvisor io */
#define IOCTL_CMD_CSRVISOR_IO	0x70000001

/* csrvisor call id */
#define CVID_FASTCALL_SERVICE		0x80000001
#define CVID_FASTCALL_READREG		0x80000002
#define CVID_FASTCALL_WRITEREG		0x80000003
#define CVID_FASTCALL_HWSPIN_LOCK	0x80000004
#define CVID_FASTCALL_HWSPIN_UNLOCK	0x80000005

/* secure register operations */
#define SECURE_REG_READ		0x0
#define SECURE_REG_WRITE	0x1

/* sub function commands */
#define CVIO_CMD_GET_RANDOM	0x70000003	/* get random value from HW */
#define CVIO_CMD_GET_CHIPUID	0x70000004	/* get chip uid for user */
#define CVIO_CMD_GET_SVMVALUE	0x70000008	/* get svm value */
#define CVIO_CMD_GET_SCMBUID	0x7000000a	/* get scambled uid */
#define CVIO_CMD_SET_SCMBTOKEN	0x7000000b	/* set scambled uid token */

/* Chip ID length fixed at 16 bytes */
#define DEVICE_CHIPUID_WORD_LENGTH	4
#define DEVICE_CHIPUID_BYTE_LENGTH	(DEVICE_CHIPUID_WORD_LENGTH * 4)

/* Scambled ID length fixed at 16 bytes */
#define DEVICE_SCMBUID_WORD_LENGTH	4
#define DEVICE_SCMBUID_BYTE_LENGTH	(DEVICE_SCMBUID_WORD_LENGTH * 4)

#define CMD_PARAM_MAGIC	0x6376696F
struct cmd_param {
	int magic;		/* in - magic number */
	int cmd;		/* in - command */
	int status;		/* out - command status */
	void *in_buf;		/* in - input buffer */
	int in_len;		/* in - input buffer length */
	void *out_buf;		/* out - output buffer */
	int out_len;		/* in/out - output buffer length*/
};

struct call_param_type {
	unsigned long service_id;
	void *service_param1;
	void *service_param2;
};

struct csrvisor_wrapper {
	struct task_struct *wrapper_thread;
	unsigned long csrvisor_ready;
	struct mutex call_mutex;
	struct mutex blob_mutex;
	wait_queue_head_t wqueue;
	int wq_wait_type;
	unsigned long return_val;
	atomic_t count;
	atomic_t inited;
	struct call_param_type *xmit_param;
	struct miscdevice wrapper_dev;
	struct clk *sec_clk;
	spinlock_t seq_lock;	/* secure register accessing sequence lock */
	spinlock_t pre_lock;	/* csrvisor_wrapper_prepare() call lock */
	unsigned long irq_flags;
};

static inline unsigned long __csrvisor_fastcall(unsigned long id,
						void *ptr, void *extra)
{
	register unsigned long r0 asm("r0") = id;
	register unsigned long r1 asm("r1") = (unsigned long)ptr;
	register unsigned long r2 asm("r2") = (unsigned long)extra;

	__asm__ __volatile__(".arch_extension sec\n\t"
		"dsb\n\t"
		"smc #0" : "=r"(r0)
		: "r"(r0), "r"(r1), "r"(r2)
		: "memory");

	return r0;
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
		cw_data->return_val =
			__csrvisor_fastcall(cw_data->xmit_param->service_id,
				    cw_data->xmit_param->service_param1,
				    cw_data->xmit_param->service_param2);

		/* wake up reader */
		cw_data->wq_wait_type = CSRVISOR_WAIT_RES;
		wake_up(&cw_data->wqueue);
	}

	return 0;
}
#endif

static int _csrvisor_fastcall(struct csrvisor_wrapper *cw_data,
			      struct call_param_type *p_call_param)
{
#ifdef CONFIG_SMP
	/*
	* Schedule to CPU0 working thread for saving CPU1 loading if
	* caller on CPU1 and preempt is enabled (checking preempt
	* count); otherwise just fallthrough and do fastcall directly.
	*/
	if ((smp_processor_id() != CSRVISOR_CPU) &&
	    (preempt_count() == 0) &&
	    atomic_read(&cw_data->inited) == 1) {
		int ret;
		/* protect and copy data into queue (1 item depth) */
		mutex_lock(&cw_data->blob_mutex);
		cw_data->xmit_param = p_call_param;
		cw_data->wq_wait_type = CSRVISOR_WAIT_REQ;
		wake_up(&cw_data->wqueue);
		ret = wait_event_interruptible(cw_data->wqueue,
			cw_data->wq_wait_type == CSRVISOR_WAIT_RES);
		mutex_unlock(&cw_data->blob_mutex);
		return ret;
	}
#endif
	/* called directly; usning local p_call_param */
	cw_data->return_val =
		__csrvisor_fastcall(p_call_param->service_id,
				    p_call_param->service_param1,
				    p_call_param->service_param2);

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

/* allocate command parameter in kernel for csrvisor call
 * from_user indicates src_param is a user mode param or a kernel one */
static int csrvisor_fastcall(struct cmd_param *src_param, int from_user,
				struct csrvisor_wrapper *cw_data)
{
	int ret;
	size_t param_size, offset;
	struct cmd_param *xfer_param;
	struct device *dev;
	struct call_param_type call_param;
	dma_addr_t dma_addr;

	dev = cw_data->wrapper_dev.this_device;

	mutex_lock(&cw_data->call_mutex);

	if (src_param->magic != CMD_PARAM_MAGIC) {
		ret = -EINVAL;
		goto __unlock_and_exit;
	}

	/*
	* create transport parameter block
	* csrvisor accesses only physical address
	* alloc VA in local and store mapped PA in transport block
	* uses dma coherent since csrvisor works as hardware
	*/
	param_size = sizeof(*xfer_param)
				+ src_param->in_len
				+ src_param->out_len;
	xfer_param = dma_zalloc_coherent(
				dev,
				param_size,
				&dma_addr,
				GFP_KERNEL);
	if (!xfer_param) {
		ret = -ENOMEM;
		goto __unlock_and_exit;
	}

	xfer_param->magic = src_param->magic;
	xfer_param->cmd = src_param->cmd;
	xfer_param->status = src_param->status;

	/* map input buffer if there are */
	if (src_param->in_buf && src_param->in_len) {
		offset = sizeof(*xfer_param);
		if (param_size < offset ||
		    param_size - offset < src_param->in_len) {
			ret = -EINVAL;
			goto __free_and_exit;
		}

		if (from_user)
			ret = copy_from_user((char *)xfer_param + offset,
				src_param->in_buf, src_param->in_len);
		else
			if (memcpy((char *)xfer_param + offset,
					src_param->in_buf, src_param->in_len))
				ret = 0;
			else
				ret = -EFAULT;
		if (ret)
			goto __free_and_exit;

		xfer_param->in_buf = (void *)(dma_addr + offset);
		xfer_param->in_len = src_param->in_len;
	}

	/* map output buffer if there are */
	if (src_param->out_buf && src_param->out_len) {
		offset = sizeof(*xfer_param) + src_param->in_len;
		xfer_param->out_buf = (void *)(dma_addr + offset);
		xfer_param->out_len = src_param->out_len;
	}

	/* csrvisor handles DMA addr */
	call_param.service_id = CVID_FASTCALL_SERVICE;
	call_param.service_param1 = (void *)dma_addr;
	call_param.service_param2 = NULL;

	/* push to csrviosr */
	ret = _csrvisor_fastcall(cw_data, &call_param);

	/* update returned status */
	src_param->status = xfer_param->status;
	src_param->out_len = xfer_param->out_len;

	if (xfer_param->out_buf && src_param->out_buf) {
		offset = sizeof(*xfer_param) + src_param->in_len;
		if (param_size < offset ||
		    param_size - offset < src_param->out_len) {
			ret = -EINVAL;
			goto __free_and_exit;
		}

		if (from_user)
			ret = copy_to_user(src_param->out_buf,
				(char *)xfer_param + offset,
				src_param->out_len);
		else
			if (memcpy(src_param->out_buf,
				(char *)xfer_param + offset,
					src_param->out_len))
				ret = 0;
			else
				ret = -EFAULT;
	}

__free_and_exit:
	dma_free_coherent(dev, param_size, xfer_param, dma_addr);
__unlock_and_exit:
	mutex_unlock(&cw_data->call_mutex);
	return ret;
}


static long csrvisor_wrapper_ioctl(struct file *file,
			unsigned int cmd, unsigned long arg)
{
	int ret;
	struct cmd_param *user_param;
	struct csrvisor_wrapper *cw_data = container_of(file->private_data,
			struct csrvisor_wrapper, wrapper_dev);

	if (cmd != IOCTL_CMD_CSRVISOR_IO)
		return -EINVAL;

	if (!access_ok(VERIFY_READ, arg, sizeof(*user_param)))
		return -EFAULT;

	user_param = (struct cmd_param *)arg;

	/* map user parameter and get result */
	ret = csrvisor_fastcall(user_param, 1, cw_data);

	return ret;
}

static const struct file_operations csrviosr_wrapper_fops = {
	.owner		=	THIS_MODULE,
	.open		=	csrvisor_wrapper_open,
	.release	=	csrvisor_wrapper_close,
	.unlocked_ioctl	=	csrvisor_wrapper_ioctl,
};

static struct csrvisor_wrapper cw_private_glob = {
	.csrvisor_ready		= 0,
	.wrapper_dev.minor	= 128,
	.wrapper_dev.name	= "cvwrapper",
	.wrapper_dev.fops	= &csrviosr_wrapper_fops,
	.seq_lock	=	__SPIN_LOCK_UNLOCKED(cw_private_glob.seq_lock),
	.pre_lock	=	__SPIN_LOCK_UNLOCKED(cw_private_glob.pre_lock),
};

#define __CSRVISOR_KPARAM_INITIALIZER(					\
		name, xcmd, in_ptr, in_size, out_ptr, out_size) {	\
	.magic		=	CMD_PARAM_MAGIC,			\
	.cmd		=	xcmd,					\
	.status		=	0,					\
	.in_buf		=	in_ptr,					\
	.in_len		=	in_size,				\
	.out_buf	=	out_ptr,				\
	.out_len	=	out_size }				\

#define DECLARE_CSRVISOR_SERVICE_PARAM(					\
			name, xcmd, in_ptr, in_size, out_ptr, out_size)	\
	struct cmd_param name =						\
		__CSRVISOR_KPARAM_INITIALIZER(				\
			name, xcmd, in_ptr, in_size, out_ptr, out_size)	\

/* provided an interface to get chip id for user via sysfs */
static ssize_t chip_uid_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned int chip_uid[DEVICE_CHIPUID_WORD_LENGTH];
	struct csrvisor_wrapper *cw_data = &cw_private_glob;
	DECLARE_CSRVISOR_SERVICE_PARAM(param, CVIO_CMD_GET_CHIPUID, NULL, 0,
			chip_uid, sizeof(chip_uid));

	if (!csrvisor_fastcall(&param, 0, cw_data))
		return sprintf(buf, "%08x%08x%08x%08x\n",
			chip_uid[0], chip_uid[1], chip_uid[2], chip_uid[3]);
	else
		return 0;
}
static DEVICE_ATTR_RO(chip_uid);

static ssize_t scmb_uid_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	unsigned int scmb_uid[DEVICE_SCMBUID_WORD_LENGTH];
	struct csrvisor_wrapper *cw_data = &cw_private_glob;

	DECLARE_CSRVISOR_SERVICE_PARAM(param, CVIO_CMD_GET_SCMBUID, NULL, 0,
			scmb_uid, sizeof(scmb_uid));

	if (!csrvisor_fastcall(&param, 0, cw_data))
		return sprintf(buf, "%08x%08x%08x%08x\n",
			scmb_uid[0], scmb_uid[1], scmb_uid[2], scmb_uid[3]);
	else
		return 0;
}

static ssize_t scmb_uid_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long long scmb_token;
	struct csrvisor_wrapper *cw_data = &cw_private_glob;

	DECLARE_CSRVISOR_SERVICE_PARAM(param, CVIO_CMD_SET_SCMBTOKEN,
			NULL, 0, NULL, 0);

	if (count < sizeof(scmb_token))
		return -EINVAL;

	ret = kstrtoull(buf, 16, &scmb_token);

	param.in_buf = &scmb_token;
	param.in_len = sizeof(scmb_token);

	if (!ret && !csrvisor_fastcall(&param, 0, cw_data))
		return count;
	else
		return ret ? ret : -EFAULT;
}
static DEVICE_ATTR_RW(scmb_uid);

#ifdef CONFIG_HW_RANDOM
int cvrng_read(struct hwrng *rng, void *data, size_t max_bytes, bool wait)
{
	struct csrvisor_wrapper *cw_data =
			(struct csrvisor_wrapper *)rng->priv;
	DECLARE_CSRVISOR_SERVICE_PARAM(param, CVIO_CMD_GET_RANDOM, NULL, 0,
					data, max_bytes);

	if (!csrvisor_fastcall(&param, 0, cw_data))
		return max_bytes;
	else
		return 0;
}

/* csrvisor HW RNG. Simple 'read' is enough for implement */
static struct hwrng csrvisor_hwrng = {
	.name		=	"cvrng",
	.read		=	cvrng_read,
	.priv		=	(unsigned long)&cw_private_glob,
	.quality	=	1000,
};
#endif

static int csrvisor_wrapper_prepare(struct csrvisor_wrapper *cw_data)
{
	struct device_node *dn;
	int ret, flag;

	/* every core may enter here; use spinlock to protect it */
	spin_lock_irqsave(&cw_data->pre_lock, flag);

	if (cw_data->csrvisor_ready) {
		ret = 0;
		goto __exit_unlock;
	}

	if (!of_machine_is_compatible("sirf,atlas7")) {
		ret = -EINVAL;
		goto __exit_unlock;
	}

	/* open discretix secuity clock since csrvisor uses it */
	dn = of_find_compatible_node(NULL, NULL, "dx,cc44s");
	cw_data->sec_clk = of_clk_get_by_name(dn, NULL);
	if (IS_ERR(cw_data->sec_clk)) {
		pr_err("can not find ccsec clock\n");
		ret = PTR_ERR(cw_data->sec_clk);
		goto __exit_unlock;
	}

	ret = clk_prepare_enable(cw_data->sec_clk);
	if (ret) {
		pr_err("enable ccsec clock failed\n");
		goto __err_exit_put_clk;
	}

	cw_data->csrvisor_ready = 1;

	spin_unlock_irqrestore(&cw_data->pre_lock, flag);

	return 0;

__err_exit_put_clk:
	clk_put(cw_data->sec_clk);

__exit_unlock:
	spin_unlock_irqrestore(&cw_data->pre_lock, flag);

	return ret;
}

#define DECLARE_CSRVISOR_CALLPARAM(					\
			name, serv_id, serv_param1, serv_param2)	\
	struct call_param_type name = {					\
		.service_id = serv_id,					\
		.service_param1 = (void *)serv_param1,			\
		.service_param2 = (void *)serv_param2 }			\

unsigned long restricted_reg_read(unsigned long addr)
{
	struct csrvisor_wrapper *cw_data = &cw_private_glob;
	DECLARE_CSRVISOR_CALLPARAM(local_call_param,
		CVID_FASTCALL_READREG, addr, NULL);

	BUG_ON(csrvisor_wrapper_prepare(cw_data));
	BUG_ON(_csrvisor_fastcall(cw_data, &local_call_param));

	return cw_data->return_val;
}
EXPORT_SYMBOL(restricted_reg_read);

unsigned long restricted_reg_write(unsigned long addr, unsigned long data)
{
	struct csrvisor_wrapper *cw_data = &cw_private_glob;
	DECLARE_CSRVISOR_CALLPARAM(local_call_param,
		CVID_FASTCALL_WRITEREG, addr, data);

	BUG_ON(csrvisor_wrapper_prepare(cw_data));
	BUG_ON(_csrvisor_fastcall(cw_data, &local_call_param));

	return cw_data->return_val;
}
EXPORT_SYMBOL(restricted_reg_write);

void sirfsoc_iobg_lock(void)
{
#ifdef CONFIG_NOC_LOCK_RTCM
	struct csrvisor_wrapper *cw_data = &cw_private_glob;
	DECLARE_CSRVISOR_CALLPARAM(local_call_param,
		CVID_FASTCALL_HWSPIN_LOCK, NULL, NULL);

	BUG_ON(csrvisor_wrapper_prepare(cw_data));
	spin_lock_irqsave(&cw_data->seq_lock, cw_data->irq_flags);
	BUG_ON(_csrvisor_fastcall(cw_data, &local_call_param));
#endif
}
EXPORT_SYMBOL(sirfsoc_iobg_lock);

void sirfsoc_iobg_unlock(void)
{
#ifdef CONFIG_NOC_LOCK_RTCM
	struct csrvisor_wrapper *cw_data = &cw_private_glob;
	DECLARE_CSRVISOR_CALLPARAM(local_call_param,
		CVID_FASTCALL_HWSPIN_UNLOCK, NULL, NULL);

	BUG_ON(csrvisor_wrapper_prepare(cw_data));
	BUG_ON(_csrvisor_fastcall(cw_data, &local_call_param));

	spin_unlock_irqrestore(&cw_data->seq_lock, cw_data->irq_flags);
#endif
}
EXPORT_SYMBOL(sirfsoc_iobg_unlock);

static __init int csrvisor_wrapper_init(void)
{
	struct csrvisor_wrapper *cw_data = &cw_private_glob;
	int ret;
#ifndef CONFIG_NOC_LOCK_RTCM
	return 0;
#endif
	/* register device */
	ret = misc_register(&cw_data->wrapper_dev);
	if (ret) {
		pr_err("failed to register misc device\n");
		return ret;
	}

	ret = csrvisor_wrapper_prepare(cw_data);
	if (ret) {
		pr_err("prepare wrapper failed.\n");
		goto __err_exit_deregister;
	}

	ret = dma_set_coherent_mask(cw_data->wrapper_dev.this_device,
				DMA_BIT_MASK(32));
	if (ret) {
		pr_err("failed to set dma coherent mask:%d\n", ret);
		goto __err_exit_disable_clk;
	}

#ifdef CONFIG_SMP
	cw_data->wq_wait_type = CSRVISOR_WAIT_RES;
	init_waitqueue_head(&cw_data->wqueue);
	mutex_init(&cw_data->call_mutex);
	mutex_init(&cw_data->blob_mutex);
	atomic_set(&cw_data->count, 0);

	/* working thread */
	cw_data->wrapper_thread = kthread_create(
					csrvisor_wrapper_thread,
					cw_data,
					"csrvisor_wrapper_thread");
	if (IS_ERR(cw_data->wrapper_thread)) {
		pr_err("failed to create csrvisor_wrapper_thread\n");
		ret = PTR_ERR(cw_data->wrapper_thread);
		goto __err_exit_disable_clk;
	}

	/* bind to cpu 0 */
	kthread_bind(cw_data->wrapper_thread, CSRVISOR_CPU);
	wake_up_process(cw_data->wrapper_thread);
#endif
	device_create_file(cw_data->wrapper_dev.this_device,
		&dev_attr_chip_uid);

	device_create_file(cw_data->wrapper_dev.this_device,
		&dev_attr_scmb_uid);

#ifdef CONFIG_HW_RANDOM
	/* register hardware random generator */
	hwrng_register(&csrvisor_hwrng);
#endif
	atomic_set(&cw_data->inited, 1);

	return 0;

__err_exit_disable_clk:
	clk_disable_unprepare(cw_data->sec_clk);
	clk_put(cw_data->sec_clk);
__err_exit_deregister:
	misc_deregister(&cw_data->wrapper_dev);
	return ret;
}
module_init(csrvisor_wrapper_init);

static void __exit csrvisor_wrapper_exit(void)
{
	struct csrvisor_wrapper *cw_data = &cw_private_glob;
#ifndef CONFIG_NOC_LOCK_RTCM
	return;
#endif
#ifdef CONFIG_SMP
	kthread_stop(cw_data->wrapper_thread);
#endif
	clk_disable_unprepare(cw_data->sec_clk);
	clk_put(cw_data->sec_clk);
	misc_deregister(&cw_data->wrapper_dev);

	atomic_set(&cw_data->inited, 0);
}
module_exit(csrvisor_wrapper_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("CSRVisor wrapper driver");
