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
* x xxx xxxx
* |  |   |--------- bit(0~15)  vq id in VQ TYPE, VDEV ID in VDEV TYPE,
* |  |------------- bit(16~27) MMIO offset 0 ~ 0xFFF
* |---------------- bit(28~31) NOTIFYID TYPE (0:VQ, 2:VBUS, 4:VDEV)
*/
#define RPROC_VDEV_MAX_VRING_NUM	32
#define RPROC_VRING_ALIGN_PAGE		4096
#define RPROC_VDEV_MMIO_SIZE		4096

/* the version of this mmio space */
#define MMIO_VERSION		0x00
/* the features of vdev */
#define MMIO_FEATURES		0x04
/* virtqueue had been configed in backend, included shared-memory for vring.
 * notify backend to config its virtqueue. */
#define MMIO_FRONT_VQ_READY	0x08
/* frontend had been configed, notify backend that it's online */
#define MMIO_FRONT_ONLINE	0x0C
/* frontend is going to be offline, notify backend to cleanup
 * resource. */
#define MMIO_FRONT_OFFLINE	0x10
/* backend had been configed, notify frontend that it's online */
#define MMIO_BACK_ONLINE	0x14
/* backend is offline */
#define MMIO_BACK_OFFLINE	0x18
/* the private data address of rproc vdev */
#define MMIO_PRIV_DATA		0x1C
/* the size of private data */
#define MMIO_PRIV_SIZE		0x20
/* the last RPROC common MMIO offset,
 * the vdev customized MMIO could use this offset as start base
 */
#define MMIO_CONFIG_BASE	0x24

/* return vq or vdev notify id */
#define GET_NOTIFY_ID(x)    ((x) & 0x0000FFFF)

/* return MMIO offset */
#define GET_MMIO_OFFSET(x)  (((x) >> 16) & 0x00000FFF)

/* is a vq notify */
#define IS_VQ_NOTIFY(x)     (!((x) & 0xF0000000))
/* is a bus notify */
#define IS_BUS_NOTIFY(x)    ((x) & 0x20000000)
/* is a vdev notify */
#define IS_DEV_NOTIFY(x)    ((x) & 0x40000000)

/* combine a bus notify id */
#define MK_BUS_NOTIFYID(x)    (((x) & 0xFFFF) | 0x20000000)
/* combine a vdev notify id */
#define MK_DEV_NOTIFYID(x, y) \
(((x) & 0xFFFF) | (((y) & 0xFFF) << 16) | 0x40000000)

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

static  inline void rproc_virtio_enable_notify(struct virtio_device *vdev)
{
	struct rproc_vdev *rvdev = vdev_to_rvdev(vdev);

	rvdev->kick_disable = false;
};

static inline void rproc_virtio_disable_notify(struct virtio_device *vdev)
{
	struct rproc_vdev *rvdev = vdev_to_rvdev(vdev);

	rvdev->kick_disable = true;
};

#endif /* _REMOTEPROC_DUALOS_H_ */
