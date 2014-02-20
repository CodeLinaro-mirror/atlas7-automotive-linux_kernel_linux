/*
 * Remote Processor Framework for Dual OS
 *
 * Copyright (c) 2013 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#ifndef _REMOTEPROC_DUALOS_H_
#define _REMOTEPROC_DUALOS_H_

#include <linux/remoteproc.h>

/* indicate this virtio device is attached on backend rproc bus */
#define VIRTIO_RPROC_F_BACK	19
/* indicate this virtio device is attached on frontend rproc bus */
#define VIRTIO_RPROC_F_FRONT	20

/*
*                         NOTIFYID 32 bits
* x x xx xxxx
* | |  |   |--------- bit(0~15)  vq id in VQ TYPE, VDEV ID in VDEV TYPE,
* | |  |              reserved in VBUS TYPE.
* | |  |------------- bit(16~23) declare the MMIO offset in VDEV TYPE
* | |---------------- bit(24~27) MMIO offset >= 256, access VDEV features
* |------------------ bit(28~31) NOTIFYID TYPE (0:VQ, 2:VBUS, 4:VDEV)
*/
#define RPROC_VDEV_MAX_VRING_NUM	32
#define RPROC_VRING_ALIGN_PAGE		4096
#define RPROC_VDEV_MMIO_SIZE		256

/* virtqueue had been configed in backend, included shared-memory for vring.
 * notify backend to config its virtqueue. */
#define MMIO_FRONT_VQ_READY	(RPROC_VDEV_MMIO_SIZE - 0x4)
/* frontend had been configed, notify backend that it's online */
#define MMIO_FRONT_ONLINE	(RPROC_VDEV_MMIO_SIZE - 0x8)
/* frontend is going to be offline, notify backend to cleanup
 * resource. */
#define MMIO_FRONT_OFFLINE	(RPROC_VDEV_MMIO_SIZE - 0xC)
/* backend had been configed, notify frontend that it's online */
#define MMIO_BACK_ONLINE	(RPROC_VDEV_MMIO_SIZE - 0x10)
/* backend is offline */
#define MMIO_BACK_OFFLINE	(RPROC_VDEV_MMIO_SIZE - 0x14)
/* the private data address of rproc vdev */
#define MMIO_PRIV_DATA		(RPROC_VDEV_MMIO_SIZE - 0x18)
/* the size of private data */
#define MMIO_PRIV_SIZE		(RPROC_VDEV_MMIO_SIZE - 0x1C)

/* Access virtio device features */
#define MMIO_DFEATURES		(RPROC_VDEV_MMIO_SIZE)
#define MMIO_GFEATURES		(RPROC_VDEV_MMIO_SIZE + 1)

#define GET_NOTIFY_ID(x)    ((x) & 0x0000FFFF)
#define GET_MMIO_OFFSET(x)  (((x) >> 16) & 0x000001FF)

#define IS_VQ_NOTIFY(x)     (!((x) & 0xF0000000))
#define IS_BUS_NOTIFY(x)    ((x) & 0x20000000)
#define IS_DEV_NOTIFY(x)    ((x) & 0x40000000)

#define MK_BUS_NOTIFYID(x)    (((x) & 0xFFFF) | 0x20000000)
#define MK_DEV_NOTIFYID(x, y) \
(((x) & 0xFFFF) | (((y) & 0x1FF) << 16) | 0x40000000)

#define ADD_DEVICE_ID(x)	(((x) & 0x7FFF) | 0x8000)
#define DEL_DEVICE_ID(x)	((x) & 0x7FFF)

#define IS_ADD_DEVICE_ID(x)	((x) & 0x8000)
#define IS_DEL_DEVICE_ID(x)	(!IS_ADD_DEVICE_ID(x))
#define GET_NOTIFY_DEVICE_ID(x)	((x) & 0x7FFF)

#define RPROC_VDEV_MIN_ID   0x20
#define RPROC_VRING_MIN_ID  0x80

/* Definition of BUS State MSG ID (0 ~ 0x1F)
 * MSG ID >= 0x20 is new device arrive msg, and
 * the id is the device's index.
 */
#define RPROC_BUS_STATE_OFFLINE		0
#define RPROC_BUS_STATE_SUSPENDED	1
#define RPROC_BUS_STATE_RUNNING		2
#define RPROC_BUS_STATE_CRASHED		3
#define RPROC_BUS_STATE_LAST		0x1F


/* This MACRO is indicates the resource entry is unused */
#define	RSC_NULL -1

/**
 * struct rproc_bus_task - remote processor bus task
 * @node: list node of bus each task.
 * @rproc: which remote proceesor this task belongs to.
 * @notifyid: this task's notify id.
 *
 */
struct rproc_bus_task {
	struct list_head node;
	struct rproc *rproc;
	u32 notifyid;
};

#endif /* _REMOTEPROC_DUALOS_H_ */
