/*
 * Remote Processor Framework for Backend OS
 *
 * Copyright (c) 2013 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/dma-mapping.h>
#include <linux/string.h>
#include <linux/remoteproc.h>
#include <linux/remoteproc_dualos.h>
#include <linux/idr.h>

#include <linux/virtio_ids.h>
#include <linux/virtio_ring.h>

#include "remoteproc_internal.h"

static inline struct fw_rsc_hdr *rsc_to_hdr(struct fw_rsc_vdev *rsc)
{
	u32 offset;
	struct fw_rsc_hdr *hdr = (struct fw_rsc_hdr *)0;

	offset = (u32)hdr->data - (u32)hdr;

	hdr = (struct fw_rsc_hdr *)((u32)rsc - offset);
	return hdr;
}

void __rproc_dump_device_entry(struct rproc *rproc,
			struct fw_rsc_vdev *rsc)
{
	struct fw_rsc_hdr *rsc_hdr;
	rsc_hdr = rsc_to_hdr(rsc);
	dev_info(&rproc->dev,
		"\rVDEV#1 TYPE :%08x\n"
		"\rVIRTIO ID   : %d\n"
		"\rVDEV ID     : %d\n"
		"\rFEATURES    : %08x\n"
		"\rCONFIG LEN  : %d\n"
		"\rVRINGS      : %d\n"
		"\rSTATUS      : %08x\n",
		rsc_hdr->type,
		rsc->id,
		rsc->notifyid,
		rsc->dfeatures,
		rsc->config_len,
		rsc->num_of_vrings,
		rsc->status);
}

/**
 * rproc_handle_bus_task() - handle the bus task
 * @rproc: the remote processor handle to register
 * @notifyid: bus task id.
 */
static int rproc_handle_bus_task(struct rproc *rproc, int notifyid)
{
	int bus_state_id = GET_NOTIFY_ID(notifyid);

	switch (bus_state_id) {
	case RPROC_BUS_STATE_RUNNING:
		rproc->state = RPROC_RUNNING;
		wake_up(&rproc->async_kick_wq);
		dev_info(&rproc->dev, "remote bus is running now.\n");
		break;

	case RPROC_BUS_STATE_SUSPENDED:
		rproc->state = RPROC_SUSPENDED;
		break;

	case RPROC_BUS_STATE_OFFLINE:
		rproc->state = RPROC_OFFLINE;
		break;

	case RPROC_BUS_STATE_CRASHED:
		rproc->state = RPROC_CRASHED;
		break;

	case RPROC_BUS_STATE_LAST:
	default:
		rproc->state = RPROC_LAST;
		dev_info(&rproc->dev,
			"Unknown Bus State notifyid %08x\n", bus_state_id);
		return -EINVAL;
	}

	return 0;
}

/**
 * rproc_bus_task_thread() - a thread to handle the rproc bus task.
 * @data: the remote processor handle to register
 *
 * some bus task will take much cpu resource, or cause cpu schedule,
 * just like create device, these tasks will reduce the realtime of
 * all virtio device driver. So we can't do these tasks in IRQ context.
 * In ISR, we just add the bus task id into a list, and left them to
 * be handled in this thread.
 *
 */
static int rproc_bus_task_thread(void *data)
{
	struct rproc *rproc = (struct rproc *)data;
	struct rproc_bus_task *task;
	unsigned long flags;
	u32 notifyid;

	do {
		wait_event_interruptible(rproc->bus_task_wq,
			!list_empty(&rproc->bus_task_head) ||
			kthread_should_stop());

		if (kthread_should_stop())
			break;

		/* Grab a pending task from the task list */
		task = list_entry(rproc->bus_task_head.next,
				struct rproc_bus_task, node);
		if (task) {
			/* Prevent list_add_tail in ISR */
			spin_lock_irqsave(&rproc->bus_task_lock, flags);
			list_del(rproc->bus_task_head.next);
			spin_unlock_irqrestore(&rproc->bus_task_lock, flags);

			notifyid = task->notifyid;
			kfree(task);

			if (IS_DEV_NOTIFY(notifyid))
				rproc_handle_virtio_task(rproc, notifyid);
			else if (IS_BUS_NOTIFY(notifyid))
				rproc_handle_bus_task(rproc, notifyid);
			else {
				dev_dbg(&rproc->dev,
				"Unknown RPROC bus task notifyid:%08x\n",
				notifyid);
			}
		}

	} while (1);

	return 0;
}

/**
 * rproc_async_kick_thread() - a thread to handle the async rproc kick.
 * @data: the remote processor handle to register
 *
 * In dual OS environment, if any side of rproc is not ready, the sync
 * call of rproc kick will be missed. if we block the rproc kick until
 * both side rproc are ready, then the local OS boot sequence should be
 * blocked by other side OS.
 *
 */
static int rproc_async_kick_thread(void *data)
{
	struct rproc *rproc = (struct rproc *)data;
	struct rproc_bus_task *kick;
	u32 notifyid;

	do {
		wait_event_interruptible(rproc->async_kick_wq,
			(rproc->state == RPROC_RUNNING) &&
			!list_empty(&rproc->async_kick_head));

		/* Grab a pending kick from the kick list */
		kick = list_entry(rproc->async_kick_head.next,
			struct rproc_bus_task, node);

		if (kick) {
			list_del(rproc->async_kick_head.next);
			notifyid = kick->notifyid;
			kfree(kick);
			/* kick the remote processor,
			 * and let it know bus is update
			 */
			rproc->ops->kick(rproc, notifyid);
		}

	} while (1);

	return 0;
}


/**
 * rproc_kick_bus_async() - kick other side async.
 * @rproc: the remote processor handle to register
 * @notifyid: message id of kick
 *
 * In dual OS environment, if any side of rproc is not ready, the sync
 * call of rproc kick will be missed. if we block the rproc kick until
 * both side rproc are ready, then the local OS boot sequence should be
 * blocked by other side OS.
 *
 * Note: the virtio driver never call this funtion directly, this function
 * is rproc internal used.
 */
void rproc_kick_bus_async(struct rproc *rproc, int notifyid)
{
	struct rproc_bus_task *task;

	dev_dbg(&rproc->dev,
		"rproc_kick_bus_async id=%08x\n", notifyid);

	task = kmalloc(sizeof(*task), GFP_KERNEL);
	if (task == NULL)
		return;

	task->notifyid = notifyid;
	task->rproc = rproc;

	mutex_lock(&rproc->lock);
	list_add_tail(&task->node, &rproc->async_kick_head);
	mutex_unlock(&rproc->lock);

	wake_up(&rproc->async_kick_wq);
}

/**
 * rproc_task_thread_setup() - create the task threads of register rproc.
 * @rproc: the remote processor handle to register
 *
 * Note: this function can be called in backend/frontend OS.
 */
int rproc_task_thread_setup(struct rproc *rproc)
{
	rproc->bus_task = kthread_run(rproc_bus_task_thread,
				(void *)rproc, "rproc_bus_task_thread");
	if (IS_ERR(rproc->bus_task)) {
		dev_err(&rproc->dev,
			"Creating RPROC bus task thread failed.\n");
		return PTR_ERR(rproc->bus_task);
	}

	rproc->async_kick_task = kthread_run(rproc_async_kick_thread,
			(void *)rproc, "rproc_async_kick_thread");
	if (IS_ERR(rproc->async_kick_task)) {
		dev_err(&rproc->dev,
			"Creating RPROC async kick thread failed.\n");
		return PTR_ERR(rproc->async_kick_task);
	}

	return 0;
}

/**
 * rproc_task_thread_stop() - stop the task threads of register rproc.
 * @rproc: the remote processor handle to register
 *
 * Note: this function can be called in backend/frontend OS.
 */
void rproc_task_thread_stop(struct rproc *rproc)
{
	if (rproc->async_kick_task) {
		kthread_stop(rproc->async_kick_task);
		rproc->async_kick_task = NULL;
	}

	if (rproc->bus_task) {
		kthread_stop(rproc->bus_task);
		rproc->bus_task = NULL;
	}
}

/**
 * rproc_set_mmio_handler() - set mmio handler of virtio rproc device.
 * @vdev: the virtio device handler.
 * @mmio: rproc device mmio handle function.
 *
 * Note: this function only can be called in backend/frontend OS.
 * Returns 0 on success, or an appropriate error code otherwise
 */
int rproc_set_mmio_handler(struct virtio_device *vdev, rproc_dev_mmio *mmio)
{
	struct rproc_vdev *rvdev = vdev_to_rvdev(vdev);

	rvdev->mmio = mmio;
	return 0;
}
EXPORT_SYMBOL(rproc_set_mmio_handler);

/**
 * rproc_setup_device() - setup virtio rproc device.
 * @vdev: the virtio device handler.
 *
 * this function will be called when virtio driver probe a virtio
 * device on virtio bus.
 *
 * Note: this function only can be called in backend OS.
 * Returns 0 on success, or an appropriate error code otherwise
 */
static int rproc_setup_device_resource(struct rproc *rproc,
				struct fw_rsc_vdev *rsc,
				struct rproc_vdev_desc *vdev_desc)
{
	int index;
	void *cfg;

	rsc->id = vdev_desc->virtio_id;
	rsc->notifyid = rproc->vdev_notifyid_index++;
	rsc->dfeatures = 0;
	rsc->gfeatures = 0;
	rsc->num_of_vrings = vdev_desc->vq_number;
	rsc->reserved[0] = 0;
	rsc->reserved[1] = 0;
	rsc->config_len = vdev_desc->config_len;

	/* setup virtio device features */
	for (index = 0; index < vdev_desc->feature_sz; index++) {
		unsigned int f = vdev_desc->features[index];
		BUG_ON(f >= 32);
		rsc->dfeatures |= (1 << f);
	}

	/* Setup vring resource */
	for (index = 0; index < vdev_desc->vq_number; index++) {
		rsc->vring[index].notifyid = rproc->vq_notifyid_index++;
		rsc->vring[index].align = RPROC_VRING_ALIGN_PAGE;
		rsc->vring[index].num = vdev_desc->vq_length;
		rsc->vring[index].reserved = 0;
		rsc->vring[index].da = 0;
	}

	if (!vdev_desc->priv_data)
		return 0;

	/* Setup device private data */
	if (RPROC_HAS_FEATURE(rproc, RPROC_F_DYNAMIC_VQ))
		cfg = &rsc->vring[RPROC_VDEV_MAX_VRING_NUM];
	else
		cfg = &rsc->vring[rsc->num_of_vrings];

	memcpy(cfg + MMIO_PRIV_DATA, &vdev_desc->priv_data, sizeof(void *));
	memcpy(cfg + MMIO_PRIV_SIZE, &vdev_desc->priv_size, sizeof(u32));
	return 0;
}

/**
 * rproc_alloc_resource_table() - alloc a resource table for a remote processor
 * @rproc: the remote processor handle to register
 *
 * the memory space of resouce table is allocated by platform rproc.
 * this function will calculate the max entry number of this resource table, and
 * reset each entry to unused.
 *
 * Note: this function can be called in backend/frontend OS.
 */
void rproc_alloc_resource_table(struct rproc *rproc)
{
	size_t max_vdev_entry_size = 0;
	u32 max_of_vdev_entry = 0;
	u32 index = 0, offset = 0;
	struct fw_rsc_hdr *rsc_hdr = NULL;

	if (!rproc || !rproc->ops || !rproc->ops->resource)
		return;

	rproc->ops->resource(rproc);

	/* If this rproc has the RPROC_F_BUS_WRITE feature, then this rproc
	 * will do bus initialization.
	 */
	if (!RPROC_HAS_FEATURE(rproc, RPROC_F_BUS_WRITE))
		return;

	memset((u8 *)rproc->table_ptr, 0, rproc->table_len);
	rproc->table_ptr->ver = RPROC_RSC_TABLE_VER_DUAL_OS;
	rproc->table_ptr->num = 0;
	rproc->vdev_notifyid_index = RPROC_VDEV_MIN_ID;
	rproc->vq_notifyid_index = RPROC_VRING_MIN_ID;

	max_vdev_entry_size = sizeof(struct fw_rsc_hdr)
				+ sizeof(struct fw_rsc_vdev);

	/* If this rproc supports variable VQ number,
	 * from 1 to RPROC_VDEV_MAX_VRING_NUM
	 */
	if (RPROC_HAS_FEATURE(rproc, RPROC_F_DYNAMIC_VQ))
		max_vdev_entry_size +=
			sizeof(struct fw_rsc_vdev_vring) *
				RPROC_VDEV_MAX_VRING_NUM;

	/* If this rproc supports device mmio */
	if (RPROC_HAS_FEATURE(rproc, RPROC_F_DEVICE_MMIO))
		max_vdev_entry_size += RPROC_VDEV_MMIO_SIZE;

	/* Each Entry has one u32 offset, one rsc header, and
	 * onr rsc vdev.
	 */
	max_of_vdev_entry =
			(rproc->table_len - sizeof(struct resource_table)) /
			(sizeof(u32) + max_vdev_entry_size);

	/* initialize the vdev resource entry */
	/* first vdev resource entry offset */
	offset = (u32)&(rproc->table_ptr->offset[max_of_vdev_entry]);
	for (index = 0; index < max_of_vdev_entry; index++) {
		rsc_hdr = (struct fw_rsc_hdr *)(offset +
					max_vdev_entry_size * index);
		/* Set each entry to UNUSED. */
		rsc_hdr->type = RSC_NULL;
		rproc->table_ptr->offset[index] =
				(u32)rsc_hdr - (u32)rproc->table_ptr;
	}

	/* Set the last entry to RSC_LAST to indicate end. */
	rsc_hdr->type = RSC_VDEV;

	/* steup the vdev resource table by vdev descriptor table */
	for (index = 0; index < rproc->vdev_desc_tbl_len; index++) {
		offset = rproc->table_ptr->offset[index];
		rsc_hdr = (struct fw_rsc_hdr *)((u32)rproc->table_ptr + offset);
		if (rsc_hdr->type == RSC_VDEV)
			break;

		rproc_setup_device_resource(rproc,
				(struct fw_rsc_vdev *)rsc_hdr->data,
				&rproc->vdev_desc_tbl[index]);
		rsc_hdr->type = RSC_VDEV;
	}

	rproc->table_ptr->num = index;

	dev_info(&rproc->dev,
		"\nremoteproc resource table        : %08x -- %08x\n"
		"\rremoteproc resource entry numbber: %d\n",
		(u32)rproc->table_ptr,
		(u32)rproc->table_ptr + rproc->table_len,
		rproc->table_ptr->num);
}

/**
 * rproc_release_resource_table() - release the resource table of remote processor
 * @rproc: the remote processor handle to register
 *
 * the memory space of resouce table is allocated by platform rproc.
 * this function will calculate the max entry number of this resource table, and
 * reset each entry to unused.
 *
 * Note: this function can be called in backend/frontend OS.
 */
void rproc_release_resource_table(struct rproc *rproc)
{
	if (!rproc || !rproc->ops || !rproc->ops->release)
		return;

	rproc->ops->release(rproc);
}
