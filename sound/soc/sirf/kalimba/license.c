/*
 * kalimba license interface
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

#include "dsp.h"
#include "ipc.h"

#define	DSP_REQ_DATA_BYTES	22
#define	DSP_RESP_DATA_BYTES	24
#define DSP_RESP_TOTAL_BYTES	28
#define DSP_RESP_DATA_WORDS	(DSP_RESP_DATA_BYTES >> 1)
#define DSP_RESP_TOTAL_WORDS	(DSP_RESP_TOTAL_BYTES >> 1)

struct kalimba_license_data {
	struct miscdevice	miscdev;
	wait_queue_head_t	wait_qh;
	void			*ipc_action;
	atomic_t		open_count;
	u32			req_buf[DSP_REQ_DATA_BYTES];
	u32			req_len;
	struct mutex		req_mutex;
};

static int ipc_action_handler(u16 msg, void *priv, u16 *data)
{
	struct kalimba_license_data *p_kaslic =
			(struct kalimba_license_data *)priv;

	/* exit if no listener is on kaslic */
	if (atomic_read(&p_kaslic->open_count) == 0)
		return ACTION_HANDLED;

	mutex_lock(&p_kaslic->req_mutex);

	p_kaslic->req_len = DSP_REQ_DATA_BYTES;
	memcpy(p_kaslic->req_buf, data, p_kaslic->req_len);

	mutex_unlock(&p_kaslic->req_mutex);

	wake_up_interruptible(&p_kaslic->wait_qh);
	return ACTION_HANDLED;
}

static ssize_t license_read(struct file *file, char __user *data,
			size_t size, loff_t *offset)
{
	struct kalimba_license_data *p_kaslic =
			(struct kalimba_license_data *)file->private_data;
	unsigned long res;

	if (p_kaslic->req_len == 0 && file->f_flags & O_NONBLOCK)
		return -EAGAIN;

	wait_event_interruptible(p_kaslic->wait_qh, p_kaslic->req_len != 0);
	if (signal_pending(current)) {
		pr_err("kaslic: interrupted while getting data\n");
		return -EINTR;
	}

	if (size > DSP_REQ_DATA_BYTES)
		size = DSP_REQ_DATA_BYTES;

	mutex_lock(&p_kaslic->req_mutex);
	/* move data in req_buf to user & clear */
	res = copy_to_user(data, p_kaslic->req_buf, size);
	p_kaslic->req_len = 0;
	mutex_unlock(&p_kaslic->req_mutex);

	if (res)
		return -EFAULT;

	return (ssize_t)size;
}

static ssize_t license_write(struct file *file, const char __user *data,
			size_t size, loff_t *offset)
{
	u16 resp[DSP_RESP_TOTAL_WORDS];

	if (size > DSP_RESP_DATA_BYTES)
		size = DSP_REQ_DATA_BYTES;

	/* assemble and send license verdict resp to kamlimba
	1st word : command
	2nd word : data size in words
	rest words : data content */
	resp[0] = KASCMD_SIGNAL_ID_LICENCE_CHECK_RSP;
	resp[1] = DSP_RESP_DATA_WORDS;
	if (copy_from_user(&resp[2], data, size))
		return -EFAULT;

	/* send resp -- need ACK, no RESP required */
	ipc_send_msg(resp, DSP_RESP_TOTAL_WORDS, MSG_NEED_ACK, NULL);

	return size;
}

static int license_open(struct inode *node, struct file *file)
{
	struct kalimba_license_data *p_kaslic =
			container_of(file->private_data,
				struct kalimba_license_data,
				miscdev);
	/* single open */
	if (!atomic_add_unless(&p_kaslic->open_count, 1, 1))
		return -EBUSY;

	file->private_data = p_kaslic;

	return 0;
}

static int license_release(struct inode *inode, struct file *file)
{
	struct kalimba_license_data *p_kaslic =
			(struct kalimba_license_data *)file->private_data;

	atomic_dec(&p_kaslic->open_count);

	/* existed data in buffer is not fresh for next reader, discard it */
	p_kaslic->req_len = 0;

	return 0;
}

static unsigned int license_poll(struct file *file,
		struct poll_table_struct *p)
{
	struct kalimba_license_data *p_kaslic =
			(struct kalimba_license_data *)file->private_data;

	poll_wait(file, &p_kaslic->wait_qh, p);

	return p_kaslic->req_len ? (POLLIN | POLLRDNORM) : 0;
}

static const struct file_operations kalimba_license_fops = {
	.owner		=	THIS_MODULE,
	.open		=	license_open,
	.read		=	license_read,
	.write		=	license_write,
	.poll		=	license_poll,
	.release	=	license_release,
};

static struct kalimba_license_data kaslic = {
	.miscdev.minor	=	MISC_DYNAMIC_MINOR,
	.miscdev.name	=	"kaslic",
	.miscdev.fops	=	&kalimba_license_fops,
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

	/* register handler for incoming license request */
	kaslic.ipc_action = register_kalimba_msg_action(
			KASCMD_SIGNAL_ID_LICENCE_CHECK_REQ,
			ipc_action_handler,
			&kaslic);
	if (!kaslic.ipc_action) {
		pr_err("failed to register ipc message\n");
		rc = -EFAULT;
		goto __deregister_and_fallout;
	}

	return 0;

__deregister_and_fallout:
	misc_deregister(&kaslic.miscdev);
	return rc;
}

void license_deinit(void)
{
	unregister_kalimba_msg_action(kaslic.ipc_action);
	misc_deregister(&kaslic.miscdev);
}
