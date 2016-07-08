/*
 * kalimba license interface
 *
 * kalimba DSP may send license request occasionally;
 * collect the request and inform user caller to dispatch,
 * and then forward response to DSP
 *
 * Copyright (c) 2015 The Linux Foundation. All rights reserved.
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
#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "audio-protocol.h"

#define DSP_REQ_DATA_BYTES	22
#define	DSP_RESP_DATA_BYTES	24
#define DSP_RESP_DATA_WORDS	(DSP_RESP_DATA_BYTES >> 1)

struct kalimba_license_data {
	struct miscdevice	miscdev;
	wait_queue_head_t	wait_qh;
	void			*ipc_action;
	atomic_t		open_count;
	u32			req_buf[DSP_REQ_DATA_BYTES];
	u32			req_len;
	struct mutex		req_mutex;
};

static const struct file_operations kalimba_license_fops;

static struct kalimba_license_data kaslic = {
	.miscdev.minor	=	MISC_DYNAMIC_MINOR,
	.miscdev.name	=	"kaslic",
	.miscdev.fops	=	&kalimba_license_fops,
};


void kalimba_license_req(u32 data_len, void *data)
{
	/* exit if no listener is on kaslic */
	if (atomic_read(&kaslic.open_count) == 0)
		return;

	mutex_lock(&kaslic.req_mutex);

	if (data_len > DSP_REQ_DATA_BYTES)
		data_len = DSP_REQ_DATA_BYTES;

	kaslic.req_len = data_len;
	memcpy(kaslic.req_buf, data, kaslic.req_len);

	mutex_unlock(&kaslic.req_mutex);

	wake_up_interruptible(&kaslic.wait_qh);
}

static ssize_t license_read(struct file *file, char __user *data,
			size_t size, loff_t *offset)
{
	unsigned long res;

	if (kaslic.req_len == 0 && file->f_flags & O_NONBLOCK)
		return -EAGAIN;

	wait_event_interruptible(kaslic.wait_qh, kaslic.req_len != 0);
	if (signal_pending(current)) {
		pr_err("kaslic: interrupted while getting data\n");
		return -EINTR;
	}

	if (size > DSP_REQ_DATA_BYTES)
		size = DSP_REQ_DATA_BYTES;

	mutex_lock(&kaslic.req_mutex);
	/* move data in req_buf to user & clear */
	res = copy_to_user(data, kaslic.req_buf, size);
	kaslic.req_len = 0;
	mutex_unlock(&kaslic.req_mutex);

	if (res)
		return -EFAULT;

	return (ssize_t)size;
}

static ssize_t license_write(struct file *file, const char __user *data,
			size_t size, loff_t *offset)
{
	u16 resp[DSP_RESP_DATA_WORDS];

	if (size > DSP_RESP_DATA_BYTES)
		size = DSP_REQ_DATA_BYTES;

	if (copy_from_user(resp, data, size))
		return -EFAULT;

	kas_send_license_ctrl_resp(size, resp);

	return size;
}

static int license_open(struct inode *node, struct file *file)
{
	/* single open */
	if (!atomic_add_unless(&kaslic.open_count, 1, 1))
		return -EBUSY;

	return 0;
}

static int license_release(struct inode *inode, struct file *file)
{
	atomic_dec(&kaslic.open_count);

	/* existed data in buffer is not fresh for next reader, discard it */
	kaslic.req_len = 0;

	return 0;
}

static unsigned int license_poll(struct file *file,
		struct poll_table_struct *p)
{
	poll_wait(file, &kaslic.wait_qh, p);

	return kaslic.req_len ? (POLLIN | POLLRDNORM) : 0;
}

static const struct file_operations kalimba_license_fops = {
	.owner		=	THIS_MODULE,
	.open		=	license_open,
	.read		=	license_read,
	.write		=	license_write,
	.poll		=	license_poll,
	.release	=	license_release,
};

int license_init(void)
{
	int rc;

	/* register a device for user data exchange */
	rc = misc_register(&kaslic.miscdev);
	if (rc) {
		pr_err("can not register miscdevice, err:%d\n", rc);
		return rc;
	}

	init_waitqueue_head(&kaslic.wait_qh);
	mutex_init(&kaslic.req_mutex);

	return 0;
}

void license_deinit(void)
{
	misc_deregister(&kaslic.miscdev);
}
