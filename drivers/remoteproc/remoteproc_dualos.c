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

/**
 * rproc_alloc_resource_entry() - alloc a resource entry for device.
 * @rproc: the remote processor handle to register
 * @pp_rsc_entry: the pointer of device resource entry pointer.
 *
 * This function only can be called in backend OS, when virtio rproc
 * driver create a new device in backend.
 *
 * Returns 0 on success, or an appropriate error code otherwise
 */
static int rproc_alloc_resource_entry(struct rproc *rproc,
			struct rproc_vdev *rvdev)
{
	u32 max, index;
	struct fw_rsc_vdev *rsc_vdev;
	struct fw_rsc_hdr *rsc_hdr;

	max = rproc->table_ptr->num;

	for (index = 0; index < max; index++) {
		rsc_hdr = (struct fw_rsc_hdr *)
				((u32)rproc->table_ptr +
				rproc->table_ptr->offset[index]);

		rsc_vdev = (struct fw_rsc_vdev *)rsc_hdr->data;
		if (RSC_NULL == rsc_hdr->type) {
			/* Occupy this entry immediately. */
			rsc_hdr->type = RSC_VDEV;
			rvdev->rsc_offset = rproc->table_ptr->offset[index] +
						sizeof(*rsc_hdr);
			return 0;
		}
	}

	return -ENOSPC;
}

/**
 * rproc_release_resource_entry() - release a resource entry of delete device.
 * @rproc: the remote processor handle to register
 * @rsc_entry: the pointer of device resource entry.
 */
void rproc_release_resource_entry(struct rproc *rproc,
			struct rproc_vdev *rvdev)
{
	struct fw_rsc_hdr *rsc_hdr;
	struct fw_rsc_vdev *rsc_vdev;

	rsc_vdev = (struct fw_rsc_vdev *)
			((u32)rproc->table_ptr + rvdev->rsc_offset);
	rsc_hdr = rsc_to_hdr(rsc_vdev);

	/* Free this entry immediately. */
	rsc_hdr->type = RSC_NULL;
	memset(rsc_vdev, 0, sizeof(struct fw_rsc_vdev) +
		sizeof(struct fw_rsc_vdev_vring) * RPROC_VDEV_MAX_VRING_NUM +
		RPROC_VDEV_MMIO_SIZE);
}

/**
 * rproc_setup_vring() - setup vring of virtio rproc device.
 * @rvdev: the rproc device handler.
 * @idx: the index of this vring in the resource entry of this device.
 *
 * this function will allocate a unique notify id for vring, and fill
 * metadata into vring's resource memory.
 *
 * Returns 0 on success, or an appropriate error code otherwise
 */
static int rproc_setup_vring(struct rproc_vdev *rvdev, int idx)
{
	struct rproc *rproc = rvdev->rproc;
	struct device *dev = &rproc->dev;
	struct rproc_vring *rvring = &rvdev->vring[idx];
	struct fw_rsc_vdev *rsc;

	int notifyid;

	/*
	 * Assign an rproc-wide unique index for this vring
	 * TODO: assign a notifyid for rvdev updates as well
	 * TODO: support predefined notifyids (via resource table)
	 */
	notifyid = idr_alloc(&rproc->notifyids,
				rvring, RPROC_VRING_MIN_ID, 0, GFP_KERNEL);
	if (notifyid < 0) {
		dev_err(dev, "idr_alloc for vring failed: %d\n", notifyid);
		return notifyid;
	}

	dev_dbg(dev, "vring: idr %d\n", notifyid);

	/* left rvring.VA&DMA after frontend config vdev to config */
	rvring->notifyid = notifyid;

	/*
	 * Let the rproc know the notifyid and da of this vring.
	 * Not all platforms use dma_alloc_coherent to automatically
	 * set up the iommu. In this case the device address (da) will
	 * hold the physical address and not the device address.
	 */
	rsc = (struct fw_rsc_vdev *)
			((u32)rproc->table_ptr + rvdev->rsc_offset);
	rsc->vring[idx].notifyid = notifyid;
	rsc->vring[idx].align = rvring->align;
	rsc->vring[idx].num = rvring->len;
	rsc->vring[idx].reserved = 0;

	return 0;
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
 * rproc_find_new_device() - rproc find a new device arrive on bus.
 * @rproc: the remote processor handle to register.
 * @notifyid: the new arrived device notify id.
 *
 * if the frontend rproc found a new resource entry had been appeared
 * in resource table, then the rporc will create a new device as this
 * entry described.
 *
 *
 * Note: this function only can be called in frontend rproc internal.
 *
 * Returns 0 on success, or an appropriate error code otherwise
 */
static int rproc_find_new_device(struct rproc *rproc, u32 notifyid)
{
	struct fw_rsc_hdr *rsc_hdr;
	struct fw_rsc_vdev *rsc_vdev;
	struct device *dev = &rproc->dev;
	int index, ret, offset = 0, entry_sz;

	rsc_hdr = NULL;
	/* get the new arrive device entry that the bus had notified me */
	for (index = 0; index < rproc->table_ptr->num; index++) {
		rsc_hdr = (struct fw_rsc_hdr *)((u32)rproc->table_ptr +
					rproc->table_ptr->offset[index]);
		rsc_vdev = (struct fw_rsc_vdev *)rsc_hdr->data;
		if (notifyid == rsc_vdev->notifyid) {
			if (idr_find(&rproc->rvdev_ids, notifyid)) {
				dev_info(dev,
					"vdev#%d had been created already!\n",
					notifyid);
				return -EEXIST;
			}
			offset = rproc->table_ptr->offset[index]
					+ sizeof(*rsc_hdr);
			break;
		}
	}

	if (!rsc_hdr)
		return -EINVAL;
#if 0
	__rproc_dump_device_entry(rproc, rsc_vdev);
#endif
	entry_sz = sizeof(struct fw_rsc_hdr) + sizeof(struct fw_rsc_vdev);
	if (RPROC_HAS_FEATURE(rproc, RPROC_F_DYNAMIC_VQ))
		entry_sz += sizeof(struct fw_rsc_vdev_vring) *
				RPROC_VDEV_MAX_VRING_NUM;

	/* If this rproc supports device mmio */
	if (RPROC_HAS_FEATURE(rproc, RPROC_F_DEVICE_MMIO))
		entry_sz += RPROC_VDEV_MMIO_SIZE;


	ret = rproc_handle_vdev(rproc, rsc_vdev, offset, entry_sz);
	return ret;
}


/**
 * rproc_find_del_device() - rproc find a device was deleted from bus
 *
 * @rproc: the remote processor handle to register.
 * @notifyid: the device notify id that the deleted device had.
 *
 * Note: this function only can be called in frontend rproc internal.
 *
 * Returns 0 on success, or an appropriate error code otherwise
 */
static int rproc_find_del_device(struct rproc *rproc, u32 notifyid)
{
	struct rproc_vdev *rvdev;

	rvdev = idr_find(&rproc->rvdev_ids, notifyid);
	if (!rvdev) {
		dev_info(&rproc->dev,
			"could not find the rproc virtio device#%d\n",
			notifyid);
		return -ENODEV;
	}

	rproc_remove_virtio_dev(rvdev);
	return 0;
}


/**
 * rproc_handle_bus_task() - handle the bus task
 * @rproc: the remote processor handle to register
 * @notifyid: bus task id.
 */
static int rproc_handle_bus_task(struct rproc *rproc, int notifyid)
{
	int bus_state_id = GET_NOTIFY_ID(notifyid);

	if (bus_state_id > RPROC_BUS_STATE_LAST) {
		if (IS_ADD_DEVICE_ID(bus_state_id)) {
			u32 notifyid = GET_NOTIFY_DEVICE_ID(bus_state_id);
			return rproc_find_new_device(rproc, notifyid);
		} else if (IS_DEL_DEVICE_ID(bus_state_id)) {
			u32 notifyid = GET_NOTIFY_DEVICE_ID(bus_state_id);
			return rproc_find_del_device(rproc, notifyid);
		}
		return -EINVAL;
	}

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
		return -1;
	}

	rproc->async_kick_task = kthread_run(rproc_async_kick_thread,
			(void *)rproc, "rproc_async_kick_thread");
	if (IS_ERR(rproc->async_kick_task)) {
		dev_err(&rproc->dev,
			"Creating RPROC async kick thread failed.\n");
		return -1;
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
int rproc_setup_device(struct virtio_device *vdev)
{
	int index;
	struct rproc_vdev *rvdev = vdev_to_rvdev(vdev);
	struct rproc *rproc = vdev_to_rproc(vdev);
	struct fw_rsc_vdev *rsc;

	if (!rvdev->rsc_offset)
		return -EINVAL;

	rsc = (struct fw_rsc_vdev *)((u32)rproc->table_ptr +
				rvdev->rsc_offset);
	rsc->id = rvdev->vdev.id.device;
	rsc->notifyid = rvdev->notifyid;
	rsc->dfeatures = rvdev->features;
	rsc->gfeatures = 0;
	rsc->num_of_vrings = rvdev->num_of_vring;
	rsc->reserved[0] = 0;
	rsc->reserved[1] = 0;
	rsc->config_len = RPROC_VDEV_MMIO_SIZE;

	for (index = 0; index < rvdev->num_of_vring; index++)
		rproc_setup_vring(rvdev, index);

	/* kick the remote processor, and let it know bus is update */
	rproc_kick_bus_async(rproc,
		MK_BUS_NOTIFYID(ADD_DEVICE_ID(rvdev->notifyid)));
	return 0;
}
EXPORT_SYMBOL(rproc_setup_device);

/**
 * rproc_create_device() - create a virtio-rproc device in virtual bus
 * @pp_vdev: the pointer of virtio device pointer,
 *           return virtio device pointer to caller.
 * @virtio_device_id: indentify which type of virtio device created.
 * @vq_num: how many vqs this virtio device had.
 * @vq_len: how many descriptors each vq holds.
 * @features: feature array of this virtio device.
 * @feature_sz: array size of the features.
 * @rproc_name: which rproc bus this virtio device will be created.
 *
 * This function will occupy a resource entry in resouce table for
 * new created virtio device, and register new virtio device to virtio bus.
 * In this phase, this resource entry will be initialized with litte metadata.
 * the further config will be done when virtio bus found a matched virtio
 * driver.
 *
 * Note: this function only can be called in backend OS.
 * Returns 0 on success, or an appropriate error code otherwise
 */
int rproc_create_device(struct virtio_device **pp_vdev,
			u32 virtio_device_id, u32 vq_num, u32 vq_len,
			u32 features[], u32 feature_sz,
			rproc_dev_mmio *mmio, char *rproc_name)
{
	int ret = -1, index = 0, alloc_vq = 0;
	struct rproc *rproc;
	struct rproc_vdev *rvdev;
	struct rproc_vring *rvring;

	rproc = rproc_get_instance_by_name(rproc_name);
	if (!rproc) {
		dev_err(&rproc->dev,
			"rproc[%s] doesn't exist!\n", rproc_name);
		return -ENODEV;
	}

	if (vq_num > RVDEV_NUM_VRINGS)
		alloc_vq = vq_num - RVDEV_NUM_VRINGS;

	rvdev = kzalloc(sizeof(struct rproc_vdev) +
			sizeof(struct rproc_vring) * alloc_vq, GFP_KERNEL);
	if (!rvdev) {
		dev_err(&rproc->dev, "kzalloc for rvdev failed!\n");
		return -ENOMEM;
	}

	rvdev->rproc = rproc;
	rvdev->num_of_vring = vq_num;

	/*
	 * Setup rproc_vring data here,
	 * rsc_vring will be setup when driver probe device.
	 */
	for (index = 0; index < vq_num; index++) {
		rvring = &rvdev->vring[index];
		rvring->len = vq_len;
		rvring->align = RPROC_VRING_ALIGN_PAGE;
		rvring->rvdev = rvdev;
	}

	mutex_lock(&rproc->lock);
	ret = rproc_alloc_resource_entry(rproc, rvdev);
	mutex_unlock(&rproc->lock);
	if (ret)
		goto free_rvdev;

	mutex_lock(&rproc->lock);
	ret = rproc_alloc_vdev_notifyid(rproc, rvdev);
	mutex_unlock(&rproc->lock);
	if (ret)
		goto free_entry;

	rvdev->mmio = mmio;

	/* Set VIRTIO Device Features */
	rvdev->features = 0;
	for (index = 0; index < feature_sz; index++) {
		unsigned int f = features[index];
		BUG_ON(f >= 32);
		rvdev->features |= (1 << f);
	}

	mutex_lock(&rproc->lock);
	list_add_tail(&rvdev->node, &rproc->rvdevs);
	mutex_unlock(&rproc->lock);

	/* it is now safe to add the virtio device */
	ret = rproc_add_virtio_dev(rvdev, virtio_device_id);
	if (ret)
		goto remove_rvdev;

	*pp_vdev = &rvdev->vdev;

	return 0;

remove_rvdev:
	mutex_lock(&rproc->lock);
	list_del(&rvdev->node);
	idr_remove(&rproc->rvdev_ids, index);
	mutex_unlock(&rproc->lock);

free_entry:
	mutex_lock(&rproc->lock);
	rproc_release_resource_entry(rproc, rvdev);
	mutex_unlock(&rproc->lock);

free_rvdev:
	kfree(rvdev);

	return ret;
}
EXPORT_SYMBOL(rproc_create_device);

/**
 * rproc_remove_device() - alloc a resource table for a remote processor
 * @rproc: the remote processor handle to register
 *
 * the memory space of resouce table is allocated by platform rproc.
 * this function will calculate the max entry number of this resource table, and
 * reset each entry to unused.
 *
 * Note: this function only can be called in backend OS.
 */
void rproc_remove_device(struct virtio_device *vdev)
{
	struct rproc_vdev *rvdev = vdev_to_rvdev(vdev);

	rproc_remove_virtio_dev(rvdev);
}
EXPORT_SYMBOL(rproc_remove_device);


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

	rproc->table_ptr->num = max_of_vdev_entry;

	/* first vdev resource entry */
	offset = (u32)&(rproc->table_ptr->offset[max_of_vdev_entry]);
	for (index = 0; index < max_of_vdev_entry; index++) {
		struct fw_rsc_hdr *rsc_hdr = (struct fw_rsc_hdr *)(offset +
					max_vdev_entry_size * index);
		/* Set each entry to UNUSED. */
		rsc_hdr->type = RSC_NULL;
		rproc->table_ptr->offset[index] =
				(u32)rsc_hdr - (u32)rproc->table_ptr;
	}

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
