/*
 * Virtio CANBUS driver
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

#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>

#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/remoteproc.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>

/* CSR Private VIRTIO DEVICE IDs */
#define VIRTIO_ID_CSR_BASE	0x1000
#define VIRTIO_ID_CAN		(VIRTIO_ID_CSR_BASE | 0x09)

#define FRAME_TYPE_NORMAL	0x1000
#define FRAME_TYPE_FD		0x1001

/* VIRTIO CAN MMIO */
#define CAN_MMIO_CLOCK_RATE	0
#define CAN_MMIO_CTRL_MODE	4
#define CAN_MMIO_ECHO_SKB_MAX	8
#define CAN_MMIO_MAX_FRAME_SIZE	12
#define CAN_MMIO_BIT_RATE	16

struct virtio_can {
	struct can_priv can;
	struct net_device *dev;
	struct napi_struct napi;
	struct virtio_device *vdev;
	struct virtqueue *rvq, *svq;
	/* protect xmit virtqueue */
	spinlock_t svq_lock;
	wait_queue_head_t inq, sendq;
	u32 max_frame_size;
	void *inbuf;
	dma_addr_t inbuf_dma;
	u32 inbuf_sz, queue_sz;
	int status;
};

struct vcan_frame_header {
	u32 type;
	u32 len;
	u32 channel;
	u32 res;
};

struct vcan_frame {
	struct vcan_frame_header header;
	union {
		u8 raw[8];
		struct can_frame cf;
		struct canfd_frame cfd;
	};
};

static int virtio_can_add_buffer(struct virtqueue *vq, void *addr,
				 u32 len, bool out)
{
	struct scatterlist sg;
	int err;

	sg_init_one(&sg, addr, len);
	if (out)
		err = virtqueue_add_outbuf(vq, &sg, 1, addr, GFP_KERNEL);
	else
		err = virtqueue_add_inbuf(vq, &sg, 1, addr, GFP_KERNEL);
	WARN_ON(err); /* sanity check; this can't really happen */

	return err;
}

static int virtio_can_fill_in_queue(struct virtio_device *vdev)
{
	struct virtio_can *vcan = vdev->priv;
	struct virtqueue *vq = vcan->rvq;
	u32 bufsz, idx;

	vcan->queue_sz = virtqueue_get_vring_size(vq);
	bufsz = ALIGN(vcan->max_frame_size, 8);
	vcan->inbuf_sz = ALIGN(bufsz * vcan->queue_sz, PAGE_SIZE);
	vcan->inbuf = dma_alloc_coherent(vdev->dev.parent->parent,
			vcan->inbuf_sz, &vcan->inbuf_dma, GFP_KERNEL);
	if (!vcan->inbuf)
		return -ENOMEM;

	dev_dbg(&vdev->dev,
		"IN QUEUE slot:%d persz:%d(%d) total:%d\n",
		vcan->queue_sz, bufsz,
		sizeof(struct vcan_frame),
		vcan->inbuf_sz);

	/* set up the receive buffers */
	for (idx = 0; idx < vcan->queue_sz; idx++) {
		virtio_can_add_buffer(vq, vcan->inbuf + bufsz * idx,
				      bufsz, false);
	}

	return 0;
}

static void virtio_can_netdev_start(struct virtio_device *vdev)
{
	struct virtio_can *vcan = vdev->priv;

	/* tell the remote processor it can start sending messages */
	virtqueue_kick(vcan->rvq);
}

static void virtio_can_read_frame(struct net_device *dev,
				  void *raw, u32 type)
{
	struct net_device_stats *stats = &dev->stats;
	struct can_frame *cf;
	struct canfd_frame *cfd;
	struct sk_buff *skb;
	void *pdata;
	u32 len;
	u8 *dlc;

	if (type == FRAME_TYPE_NORMAL)
		skb = alloc_can_skb(dev, &cf);
	else
		skb = alloc_canfd_skb(dev, &cfd);

	if (unlikely(!skb)) {
		stats->rx_dropped++;
		return;
	}

	if (type == FRAME_TYPE_NORMAL) {
		pdata = cf;
		len = sizeof(*cf);
		dlc = &cf->can_dlc;
	} else {
		pdata = cfd;
		len = sizeof(*cfd);
		dlc = &cfd->len;
	}

	memcpy(pdata, raw, len);
	netif_receive_skb(skb);

	stats->rx_packets++;
	stats->rx_bytes += *dlc;
}

static int virtio_can_xmit(struct virtio_can *vcan, void *data, u32 len)
{
	int err;
	unsigned long flags;
	dma_addr_t dma_handle;
	void *buf;

	/* Make sure data memory can be accessed by M3, we have to
	 * allocate buffer from DMA. Because we have already guaranteed
	 * M3 can access A7 DMA area.
	 */
	buf = dma_alloc_coherent(vcan->vdev->dev.parent->parent,
				 len, &dma_handle, GFP_ATOMIC);
	if (!buf)
		return -ENOMEM;

	memcpy(buf, data, len);

	spin_lock_irqsave(&vcan->svq_lock, flags);

	err = virtio_can_add_buffer(vcan->svq, buf, len, true);
	virtqueue_kick(vcan->svq);
	if (err)
		goto failed;

failed:
	spin_unlock_irqrestore(&vcan->svq_lock, flags);

	return err ? err : len;
}

static netdev_tx_t virtio_can_start_xmit(struct sk_buff *skb,
					 struct net_device *dev)
{
	struct virtio_can *vcan = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct can_frame *cf = (struct can_frame *)skb->data;
	int cbxmit;

	if (can_dropped_invalid_skb(dev, skb))
		return NETDEV_TX_OK;

	cbxmit = virtio_can_xmit(vcan, cf, skb->len);
	if (cbxmit <= 0) {
		netif_stop_queue(dev);
		netdev_err(dev, "BUG! TX buffer full when queue awake!\n");
		return NETDEV_TX_BUSY;
	}

	can_put_echo_skb(skb, dev, 0);
	stats->tx_packets++;
	stats->tx_bytes += cf->can_dlc;

	return NETDEV_TX_OK;
}

/* called when an rx buffer is used, and it's time to digest a message */
static int virtio_can_poll_rx(struct virtqueue *vq, int quota)
{
	struct virtio_can *vcan = vq->vdev->priv;
	struct vcan_frame *vf;
	unsigned int len, msgs_received = 0;

	while (quota > 0) {
		vf = virtqueue_get_buf(vq, &len);
		if (!vf)
			break;

		virtio_can_read_frame(vcan->dev, vf->raw, vf->header.type);
		/* Push buffer back to in queue */
		virtio_can_add_buffer(vq, vf, len, false);

		msgs_received++;
		quota--;
	}

	dev_dbg(&vcan->vdev->dev, "Received %u messages\n", msgs_received);

	/* tell the remote processor we added another available rx buffer */
	if (msgs_received)
		virtqueue_kick(vcan->rvq);

	return msgs_received;
}

static int virtio_can_poll(struct napi_struct *napi, int quota)
{
	struct net_device *dev = napi->dev;
	struct virtio_can *vcan = netdev_priv(dev);
	int work_done = 0;

	work_done += virtio_can_poll_rx(vcan->rvq, quota);
	if (work_done < quota)
		napi_complete(napi);

	virtqueue_enable_cb(vcan->rvq);
	return work_done;
}

static int virtio_can_open(struct net_device *dev)
{
	int err;
	struct virtio_can *vcan = netdev_priv(dev);

	/* check or determine and set bittime */
	err = open_candev(dev);
	if (err)
		return err;

	napi_enable(&vcan->napi);
	netif_start_queue(dev);

	return 0;
}

static int virtio_can_close(struct net_device *dev)
{
	struct virtio_can *vcan = netdev_priv(dev);

	netif_stop_queue(dev);
	napi_disable(&vcan->napi);

	close_candev(dev);

	return 0;
}

static int virtio_can_set_mode(struct net_device *dev, enum can_mode mode)
{
	switch (mode) {
	case CAN_MODE_START:
		netif_wake_queue(dev);
		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

/* virtqueue incoming data intterrupt IRQ */
static void virtio_can_recv_isr(struct virtqueue *vq)
{
	struct virtio_can *vcan = vq->vdev->priv;

	dev_dbg(&vq->vdev->dev, "%s\n", __func__);

	/* Wake up the blocked read or write context */
	virtqueue_disable_cb(vq);

	/* Sometimes, the virtio's virtqueue callback is executed in
	 * a thread context. If we call napi_schedule() here and system
	 * becomes IDLE and finds an existing softirq. it will have the
	 * "NOHZ: local_softirq_pending 08" issue.
	 * So we use local_bh_disable() to walkaround this issue.
	 */
	local_bh_disable();
	napi_schedule(&vcan->napi);
	local_bh_enable();
}

static void virtio_can_xmit_isr(struct virtqueue *vq)
{
	struct virtio_can *vcan = vq->vdev->priv;
	void *buf;
	dma_addr_t dma_handle;
	int len;

	while (1) {
		buf = virtqueue_get_buf(vcan->svq, &len);
		if (!buf)
			break;

		dma_handle =
			virt_to_dma(vcan->vdev->dev.parent->parent,
				    buf);
		dma_free_coherent(vcan->vdev->dev.parent->parent,
				  len, buf, dma_handle);
	}
	/* Wake up the blocked read or write context */
	netif_wake_queue(vcan->dev);
	dev_dbg(&vq->vdev->dev, "%s\n", __func__);
}

static const struct net_device_ops virtio_can_netdev_ops = {
	.ndo_open	= virtio_can_open,
	.ndo_stop	= virtio_can_close,
	.ndo_start_xmit	= virtio_can_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static
struct net_device *virtio_can_netdev_init(struct virtio_device *vdev)
{
	struct net_device *dev;
	struct virtio_can *vcan;
	struct can_priv *can;
	vq_callback_t *vq_cbs[] = { virtio_can_recv_isr,
					virtio_can_xmit_isr };
	const char * const names[] = { "virtio_can_rx", "virtio_can_tx" };
	struct virtqueue *vqs[2];
	int err;
	u32 echo_skb_max;

	echo_skb_max = virtio_cread32(vdev, CAN_MMIO_ECHO_SKB_MAX);
	dev = alloc_candev(sizeof(*vcan), echo_skb_max);
	if (!dev)
		return NULL;

	/* retrieve virtio CAN instance */
	vcan = netdev_priv(dev);
	vcan->dev = dev;

	/* retrieve CAN configurations */
	can = &vcan->can;
	vcan->max_frame_size = virtio_cread32(vdev, CAN_MMIO_MAX_FRAME_SIZE);
	can->clock.freq = virtio_cread32(vdev, CAN_MMIO_CLOCK_RATE);
	can->ctrlmode_supported = virtio_cread32(vdev, CAN_MMIO_CTRL_MODE);
	can->bittiming.bitrate = virtio_cread32(vdev, CAN_MMIO_BIT_RATE);
	can->data_bittiming.bitrate = vcan->can.bittiming.bitrate;
	can->echo_skb_max = echo_skb_max;
	can->do_set_mode = virtio_can_set_mode;

	/* Init virtio CAN data members */
	vcan->vdev = vdev;
	vcan->status = 0;
	spin_lock_init(&vcan->svq_lock);
	init_waitqueue_head(&vcan->inq);
	init_waitqueue_head(&vcan->sendq);

	/* We expect two virtqueues, rx and tx (and in this order) */
	err = vdev->config->find_vqs(vdev, 2, vqs, vq_cbs,
				     (const char **)names);
	if (err) {
		dev_err(&vdev->dev, "find vqs failed! err=%d\n", err);
		goto free_can;
	}

	vcan->rvq = vqs[0];
	vcan->svq = vqs[1];
	vdev->priv = vcan;

	err = virtio_can_fill_in_queue(vdev);
	if (err) {
		dev_err(&vdev->dev,
			"fill incoming queue failed! err=%d\n",
			err);
		goto del_vqs;
	}

	dev_info(&vdev->dev, "Initialized!\n");

	return dev;

del_vqs:
	vdev->config->del_vqs(vdev);

free_can:
	free_candev(dev);

	return NULL;
}

static void virtio_can_netdev_free(struct virtio_device *vdev)
{
	struct virtio_can *vcan = vdev->priv;
	struct net_device *dev = vcan->dev;

	vdev->config->del_vqs(vcan->vdev);

	dma_free_coherent(vdev->dev.parent->parent, vcan->inbuf_sz,
			  vcan->inbuf, vcan->inbuf_dma);

	free_candev(dev);
}

static int virtio_can_probe(struct virtio_device *vdev)
{
	struct net_device *dev;
	struct virtio_can *vcan;
	int err = 0;

	dev = virtio_can_netdev_init(vdev);
	if (!dev) {
		dev_err(&vdev->dev, "Initialize CAN net failed\n");
		return -ENODEV;
	}

	dev->netdev_ops	= &virtio_can_netdev_ops;
	dev->flags |= IFF_ECHO;

	vcan = netdev_priv(dev);
	netif_napi_add(dev, &vcan->napi, virtio_can_poll, NAPI_POLL_WEIGHT);
	SET_NETDEV_DEV(dev, &vdev->dev);

	err = register_candev(dev);
	if (err) {
		dev_err(&vdev->dev, "registering netdev failed\n");
		goto exit_free;
	}

	virtio_can_netdev_start(vdev);
	dev_info(&vdev->dev, "virtio CAN device registered\n");

	return 0;

 exit_free:
	virtio_can_netdev_free(vdev);

	return err;
}

static void virtio_can_remove(struct virtio_device *vdev)
{
	struct virtio_can *vcan = vdev->priv;
	struct net_device *dev = vcan->dev;

	unregister_candev(dev);

	vdev->config->reset(vdev);

	virtio_can_netdev_free(vdev);
}

/* Setting the VIRTIO TYPE ID for this virtual device driver */
static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_CAN, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

/* Setting the VIRTIO Feature Bits for this virtual device driver.
 * The bits are: 0~15. 16~31 are reserved.
 */
static unsigned int features[] = {};

static struct virtio_driver virtio_can_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name = KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.id_table = id_table,
	.probe = virtio_can_probe,
	.remove = virtio_can_remove,
};

static int __init virtio_can_init(void)
{
	return register_virtio_driver(&virtio_can_driver);
}
module_init(virtio_can_init);

static void __exit virtio_can_fini(void)
{
	unregister_virtio_driver(&virtio_can_driver);
}
module_exit(virtio_can_fini);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio CANBUS driver");
MODULE_LICENSE("GPL v2");
