/*
 * SIRF Remote processor machine-specific module
 *
 * Copyright (c) 2014 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/kthread.h>
#include <linux/hwspinlock.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_device.h>

#include <linux/remoteproc.h>
#include <linux/remoteproc_dualos.h>

#include "remoteproc_internal.h"

/* Definition of IPC interrupts' Trigger Registers offset */
#define TR_S_NS_1	0x0000
#define TR_S_NS_2	0x0004
#define TR_S_M3_1	0x0008
#define TR_S_M3_2	0x000C
#define TR_S_KAS_1	0x0010
#define TR_S_KAS_2	0x0014
#define TR_NS_S_1	0x0100
#define TR_NS_S_2	0x0104
#define TR_NS_M3_1	0x0108
#define TR_NS_M3_2	0x010C
#define TR_NS_KAS_1	0x0110
#define TR_NS_KAS_2	0x0114
#define TR_M3_S_1	0x0200
#define TR_M3_S_2	0x0204
#define TR_M3_NS_1	0x0208
#define TR_M3_NS_2	0x020C
#define TR_M3_KAS_1	0x0210
#define TR_M3_KAS_2	0x0214
#define TR_KAS_S_1	0x0300
#define TR_KAS_S_2	0x0304
#define TR_KAS_NS_1	0x0308
#define TR_KAS_NS_2	0x030C
#define TR_KAS_M3_1	0x0310
#define TR_KAS_M3_2	0x0314

enum sirf_rproc_idx {
#ifdef CONFIG_CSRVISOR_DUALOS
	S2NS0,
	S2NS1,
	NS2S0,
	NS2S1,
	S2M30,
	S2M31,
	S2KAL0
	S2KAL1
#endif
	NS2M30,
	NS2M31,
	NS2KAL0,
	NS2KAL1,
};

enum sirf_rproc_hwspinlock_idx {
#ifdef CONFIG_CSRVISOR_DUALOS
	S2NS0_WL,
	S2NS0_RL,
	S2NS1_WL,
	S2NS1_RL,
	NS2S0_WL,
	NS2S0_RL,
	NS2S1_WL,
	NS2S1_RL,
	S2M30_WL,
	S2M30_RL,
	S2M31_WL,
	S2M31_RL,
	S2KAL0_WL,
	S2KAL0_RL,
	S2KAL1_WL,
	S2KAL1_RL,
#endif
	NS2M30_WL,
	NS2M30_RL,
	NS2M31_WL,
	NS2M31_RL,
	NS2KAL0_WL,
	NS2KAL0_RL,
	NS2KAL1_WL,
	NS2KAL1_RL,
};

#define DEF_FEATURES	(RPROC_F_DEVICE_MMIO | RPROC_F_DYNAMIC_VQ | \
			RPROC_F_DEVICE_UPDATE_NOTIFY)

#define S_FEATURES	(DEF_FEATURES | RPROC_F_BACKEND | \
			RPROC_F_BUS_WRITE | RPROC_F_PREDEFINED_VQ_NOTIFYID)

#define NS_FEATURES	(DEF_FEATURES | RPROC_F_FRONTEND | \
			RPROC_F_PREDEFINED_VQ_NOTIFYID)

#ifdef CONFIG_CSRVISOR_DUALOS

#include <linux/virtio_ids.h>
#include <linux/rpmsg.h>
#include <linux/virtio_i2c.h>
#include <linux/virtio_console.h>
#include <linux/virtio_clk.h>

/* This table defined the channels will be created when rpmsg backend
 * become ready. And the corresponded channels for the frontend will be
 * created by ns_service automatically.
 *
 * The channel that was created by ns_serice will receive msg from any
 * address, so dst address should be RPMSG_ADDR_ANY.
 *
 * If you want to restrict the address, you have to create the channel
 * manually in remote side, and change RPMSG_ADDR_ANY to customized address
 * and do the restriction in your client& service driver.
 */
static struct rpmsg_channel_descriptor s_rpmsg_channels[] = {
	{ "rpmsg-client-sample", 0x1234, RPMSG_ADDR_ANY, {0, 0} },
};

/* This table defined the virtio i2c adapter descriptors */
static struct virtio_i2c_desc s_virtio_i2c_descs[] = {
	{ 0, "CSR Virtual I2C Adapter#0" },
	{ 1, "CSR Virtual I2C Adapter#1" },
};

/* This table defined the virtio console device descriptors */
static struct virtio_rproc_console_desc s_virtio_console_descs[] = {
	{ 0, "vport" },
};

/*
 * The compatible string of virtual device where all virtual
 * clock units connected.
 */
static const char s_virtio_clk_np_string[] = "sirf,virtio-clkc";

/* This table defined the virtio device will be create on remoteproc bus */
static struct rproc_vdev_desc s2ns0_rproc_vdev_desc[] = {
	/* virtio clock device descriptor */
	{ VIRTIO_ID_CLOCK, 1, 256,
		{	/* Prepare Features */
			VIRTIO_CLK_F_PREPARE,
			VIRTIO_CLK_F_UNPREPARE,
			VIRTIO_CLK_F_IS_PREPARED,
			/* Control Features */
			VIRTIO_CLK_F_ENABLE,
			VIRTIO_CLK_F_DISABLE,
			VIRTIO_CLK_F_IS_ENABLED,
		}, 6, RPROC_VDEV_MMIO_SIZE,
		(void *)s_virtio_clk_np_string,
		ARRAY_SIZE(s_virtio_clk_np_string)},
	/* virtio rpmsg bus device descriptor */
	{ VIRTIO_ID_RPMSG, 2, 256, { VIRTIO_RPMSG_F_NS, }, 1,
		RPROC_VDEV_MMIO_SIZE,
		s_rpmsg_channels, ARRAY_SIZE(s_rpmsg_channels) },
	/* virtio i2c device#0 descriptor */
	{ VIRTIO_ID_I2C, 1, 256, { VIRTIO_RING_F_INDIRECT_DESC, }, 1,
		RPROC_VDEV_MMIO_SIZE,
		&s_virtio_i2c_descs[0], sizeof(struct virtio_i2c_desc) },
	/* virtio i2c device#1 descriptor */
	{ VIRTIO_ID_I2C, 1, 256, { VIRTIO_RING_F_INDIRECT_DESC, }, 1,
		RPROC_VDEV_MMIO_SIZE,
		&s_virtio_i2c_descs[1], sizeof(struct virtio_i2c_desc) },
	/* virtio console device#0 descriptor */
	{ VIRTIO_ID_RPROC_SERIAL, 2, 256, {}, 0,
		RPROC_VDEV_MMIO_SIZE,
		&s_virtio_console_descs[0],
		sizeof(struct virtio_rproc_console_desc) },
};
#endif

struct fifo_buffer {
	struct hwspinlock *lock;
	unsigned char *buffer;
	u32 w_pos;
	u32 r_pos;
	u32 size;
	u32 *count; /* pointer to shared memory */
};

static int fifo_write(struct fifo_buffer *fifo,
		const void *data, u32 len)
{
	u32 overflow, count;
	int err;

	err = hwspin_lock_timeout(fifo->lock, 10);
	if (err) {
		pr_err("%s, Get hwspinlock failed!err= %d\n",
			__func__, err);
		WARN_ON(err);
		return err;
	}

	if (len > fifo->size) {
		err = -EFBIG;
		goto err_exit;
	}

	count = *fifo->count;
	overflow = len > (fifo->size - count);
	if (overflow) {
		/* previous data hasn't been read, FIFO busy */
		err = -EBUSY;
		goto err_exit;
	}

	/* copy data to fifo buffer */
	memcpy(fifo->buffer + fifo->w_pos, data, len);
	/* update fifo position */
	fifo->w_pos = (fifo->w_pos + len) % fifo->size;
	*fifo->count = count + len;

	/* memory barrier */
	smp_mb();

	err = 0;

err_exit:
	hwspin_unlock(fifo->lock);

	return err;
}

static int fifo_read(struct fifo_buffer *fifo,
		void *data, u32 len)
{
	int err;
	u32 count;

	err = hwspin_lock_timeout(fifo->lock, 10);
	if (err) {
		pr_err("%s, Get hwspinlock failed!err= %d\n",
			__func__, err);
			WARN_ON(err);
		return err;
	}

	count = *fifo->count;
	if (!count) {
		err = -ENOSPC;
		goto err_exit;
	}

	if (len > count)
		len = count;

	/* copy data from fifo buffer */
	memcpy(data, fifo->buffer + fifo->r_pos, len);
	/* update fifo position */
	fifo->r_pos = (fifo->r_pos + len) % fifo->size;
	*fifo->count = count - len;

	err = 0;

err_exit:
	hwspin_unlock(fifo->lock);

	return err;
}

static int fifo_init(struct fifo_buffer *fifo, void *buffer,
			int size, int hwlock_id)
{

	fifo->lock = hwspin_lock_request_specific(hwlock_id);
	if (!fifo->lock) {
		pr_info("%s:Could not request specific hwspin lock!\n",
			__func__);
		return -ENODEV;
	}

	fifo->count = (u32 *)buffer;
	fifo->buffer = (unsigned char *)(buffer + sizeof(u32));
	fifo->w_pos = 0;
	fifo->r_pos = 0;
	fifo->size = size - 4;
	*fifo->count = 0;

	return 0;
}

/* Hardware info for remoteproc */
struct hw_info {
	const char name[32];
	u32 setreg;
	u32 clrreg;
	u32 w_fifo_chn;
	u32 r_fifo_chn;
	u32 w_fifo_lock;
	u32 r_fifo_lock;
	u32 fifo_sz;
	u32 features;
	u32 vdev_num;
	struct rproc_vdev_desc *vdev_desc;
};

/* FIFO shared memory has been divide into 2 logical channels */
#define FIFO_LOGIC_CHN_0	0
#define FIFO_LOGIC_CHN_1	1

/**
 * struct sirf_rproc - SIRF remote processor instance state
 * @rproc: rproc handle
 * @rsc_table_pa: the physical address of rproc resource table area.
 * @rsc_table_len: the length of rproc resource table.
 * @fifo_rx_lock: lock for fifo receive data.
 * @fifo_tx_lock: lock for fifo send data.
 * @tx_avail_wq: wait queue of send data when fifo is busy.
 * @fifo_avail: fifo status for send data.
 *		fifo can send data when fifo_avail is true.
 * @fifo_msg_rx: memory address for fifo arrived data.
 * @fifo_msg_tx: memory address for fifo send data.
 * @fifo_iomemmem: iomem address for fifo register.
 * @irq: the irq number of fifo allocated in backend OS.
 * @irq_gen_count: generate IRQ counter for statistic
 * @irq_get_count: arrive IRQ counter for statistic
 */
struct sirf_rproc {
	struct rproc *rproc;
	const struct hw_info *hwinfo;
	void *rsc_table_pa;
	u32 rsc_table_len;
	void *vdev_list;
	u32 vdev_num;
	void __iomem *io_base;
	void __iomem *set_reg;
	void __iomem *clr_reg;
	struct fifo_buffer w_fifo;
	struct fifo_buffer r_fifo;
	u32 fifo_sz;
	spinlock_t w_fifo_lock;
	int irq;
};

/* Interrupt handler for IRQs from remote processor */
static irqreturn_t sirf_rproc_ipc_isr(int irq, void *data)
{
	struct rproc *rproc = (struct rproc *)data;
	struct sirf_rproc *srproc = (struct sirf_rproc *)rproc->priv;
	u32 notifyid;
	int err;

	/* clear interrupt */
	readl(srproc->clr_reg);
	do {
		err = fifo_read(&srproc->r_fifo, &notifyid,
				sizeof(notifyid));
		if (err)
			break;
		/* We will handle vq, vdev, vbus in different route */
		if (IS_VQ_NOTIFY(notifyid))
			rproc_vq_interrupt(rproc, GET_NOTIFY_ID(notifyid));
		else
			rproc_bus_interrupt(rproc, notifyid);
	} while (1);

	return IRQ_HANDLED;
}

static int sirf_rproc_start(struct rproc *rproc)
{
	return 0;
}

static int sirf_rproc_stop(struct rproc *rproc)
{
	return 0;
}

static void sirf_rproc_resource(struct rproc *rproc)
{
	struct sirf_rproc *srproc = (struct sirf_rproc *)rproc->priv;

	rproc->table_ptr = srproc->rsc_table_pa;
	rproc->table_len = srproc->rsc_table_len;
}

static void sirf_rproc_release(struct rproc *rproc)
{
	struct sirf_rproc *srproc = rproc->priv;

	if (srproc->hwinfo->features & RPROC_F_BACKEND)
		iounmap(srproc->rsc_table_pa);

	iounmap(srproc->io_base);

	kfree(rproc->vdev_desc_tbl);

	rproc->table_ptr = 0;
	rproc->table_len = 0;
}

static void sirf_rproc_kick(struct rproc *rproc, int notify_id)
{
	struct sirf_rproc *srproc = rproc->priv;
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&srproc->w_fifo_lock, flags);

	ret = fifo_write(&srproc->w_fifo, &notify_id, sizeof(notify_id));
	if (ret) {
		dev_err(&rproc->dev,
			"%s could not completed, err=%d\n",
			__func__, ret);
		WARN_ON(1);
		goto failed;
	}

	/* Trigger interrupt to remote side */
	writel(0x01, srproc->set_reg);
failed:
	spin_unlock_irqrestore(&srproc->w_fifo_lock, flags);
}

static const struct hw_info sirf_rproc_hwinfo[] = {
#ifdef CONFIG_CSRVISOR_DUALOS
	{
	  .name = "s2ns0-rproc",
	  .setreg = TR_S_NS_1, .clrreg = TR_NS_S_1,
	  .w_fifo_chn = FIFO_LOGIC_CHN_0,
	  .r_fifo_chn = FIFO_LOGIC_CHN_1,
	  .w_fifo_lock = S2NS0_WL, .r_fifo_lock = S2NS0_RL,
	  .fifo_sz = 0x10000,
	  .features = S_FEATURES,
	  .vdev_num = ARRAY_SIZE(s2ns0_rproc_vdev_desc),
	  .vdev_desc = s2ns0_rproc_vdev_desc,
	}, {
	  .name = "s2ns1-rproc",
	  .setreg = TR_S_NS_2, .clrreg = TR_NS_S_2,
	  .w_fifo_chn = FIFO_LOGIC_CHN_0,
	  .r_fifo_chn = FIFO_LOGIC_CHN_1,
	  .w_fifo_lock = S2NS1_WL, .r_fifo_lock = S2NS1_RL,
	  .fifo_sz = 0x10000,
	  .features = S_FEATURES,
	}, {
	  .name = "ns2s0-rproc",
	  .setreg = TR_NS_S_1, .clrreg = TR_S_NS_1,
	  .w_fifo_chn = FIFO_LOGIC_CHN_1,
	  .r_fifo_chn = FIFO_LOGIC_CHN_0,
	  .w_fifo_lock = NS2S0_WL, .r_fifo_lock = NS2S0_RL,
	  .fifo_sz = 0x10000,
	  .features = NS_FEATURES,
	}, {
	  .name = "ns2s1-rproc",
	  .setreg = TR_NS_S_2, .clrreg = TR_S_NS_2,
	  .w_fifo_chn = FIFO_LOGIC_CHN_1,
	  .r_fifo_chn = FIFO_LOGIC_CHN_0,
	  .w_fifo_lock = NS2S1_WL, .r_fifo_lock = NS2S1_RL,
	  .fifo_sz = 0x10000,
	  .features = NS_FEATURES,
	}, {
	  .name = "s2m30-rproc",
	  .setreg = TR_S_M3_1, .clrreg = TR_M3_S_1,
	  .w_fifo_chn = FIFO_LOGIC_CHN_0,
	  .r_fifo_chn = FIFO_LOGIC_CHN_1,
	  .w_fifo_lock = S2M30_WL, .r_fifo_lock = S2M30_RL,
	  .fifo_sz = 0x1000,
	  .features = S_FEATURES,
	}, {
	  .name = "s2m31-rproc",
	  .setreg = TR_S_M3_2, .clrreg = TR_M3_S_2,
	  .w_fifo_chn = FIFO_LOGIC_CHN_0,
	  .r_fifo_chn = FIFO_LOGIC_CHN_1,
	  .w_fifo_lock = S2M31_WL, .r_fifo_lock = S2M31_RL,
	  .fifo_sz = 0x1000,
	  .features = S_FEATURES,
	}, {
	  .name = "s2kal0-rproc",
	  .setreg = TR_S_KAS_1, .clrreg = TR_KAS_S_1,
	  .w_fifo_chn = FIFO_LOGIC_CHN_0,
	  .r_fifo_chn = FIFO_LOGIC_CHN_1,
	  .w_fifo_lock = S2KAL0_WL, .r_fifo_lock = S2KAL0_RL,
	  .fifo_sz = 0x1000,
	  .features = S_FEATURES,
	}, {
	  .name = "s2kal1-rproc",
	  .setreg = TR_S_KAS_2, .clrreg = TR_KAS_S_2,
	  .w_fifo_chn = FIFO_LOGIC_CHN_0,
	  .r_fifo_chn = FIFO_LOGIC_CHN_1,
	  .w_fifo_lock = S2KAL1_WL, .r_fifo_lock = S2KAL1_RL,
	  .fifo_sz = 0x1000,
	  .features = S_FEATURES,
	},
#endif
	{
	  .name = "ns2m30-rproc",
	  .setreg = TR_NS_M3_1, .clrreg = TR_M3_NS_1,
	  .w_fifo_chn = FIFO_LOGIC_CHN_0,
	  .r_fifo_chn = FIFO_LOGIC_CHN_1,
	  .w_fifo_lock = NS2M30_WL, .r_fifo_lock = NS2M30_RL,
	  .fifo_sz = 0x1000,
	  .features = NS_FEATURES,
	}, {
	  .name = "ns2m31-rproc",
	  .setreg = TR_NS_M3_2, .clrreg = TR_M3_NS_2,
	  .w_fifo_chn = FIFO_LOGIC_CHN_0,
	  .r_fifo_chn = FIFO_LOGIC_CHN_1,
	  .w_fifo_lock = NS2M31_WL, .r_fifo_lock = NS2M31_RL,
	  .fifo_sz = 0x1000,
	  .features = NS_FEATURES,
	}, {
	  .name = "ns2kal0-rproc",
	  .setreg = TR_NS_KAS_1, .clrreg = TR_KAS_NS_1,
	  .w_fifo_chn = FIFO_LOGIC_CHN_0,
	  .r_fifo_chn = FIFO_LOGIC_CHN_1,
	  .w_fifo_lock = NS2KAL0_WL, .r_fifo_lock = NS2KAL0_RL,
	  .fifo_sz = 0x1000,
	  .features = NS_FEATURES,
	}, {
	  .name = "ns2kal1-rproc",
	  .setreg = TR_NS_KAS_2, .clrreg = TR_KAS_NS_2,
	  .w_fifo_chn = FIFO_LOGIC_CHN_0,
	  .r_fifo_chn = FIFO_LOGIC_CHN_1,
	  .w_fifo_lock = NS2KAL1_WL, .r_fifo_lock = NS2KAL1_RL,
	  .fifo_sz = 0x1000,
	  .features = NS_FEATURES,
	}
};

static const struct of_device_id sirf_rproc_dt_ids[] = {
#ifdef CONFIG_CSRVISOR_DUALOS
	{
		.compatible = "sirf,s2ns0-rproc",
		.data = &sirf_rproc_hwinfo[S2NS0],
	}, {
		.compatible = "sirf,s2ns1-rproc",
		.data = &sirf_rproc_hwinfo[S2NS1],
	}, {
		.compatible = "sirf,ns2s0-rproc",
		.data = &sirf_rproc_hwinfo[NS2S0],
	}, {
		.compatible = "sirf,ns2s1-rproc",
		.data = &sirf_rproc_hwinfo[NS2S1],
	}, {
		.compatible = "sirf,s2m30-rproc",
		.data = &sirf_rproc_hwinfo[S2M30],
	}, {
		.compatible = "sirf,s2m31-rproc",
		.data = &sirf_rproc_hwinfo[S2M31],
	}, {
		.compatible = "sirf,s2kal0-rproc",
		.data = &sirf_rproc_hwinfo[S2KAL0],
	}, {
		.compatible = "sirf,s2kal1-rproc",
		.data = &sirf_rproc_hwinfo[S2KAL1],
	},
#endif
	{
		.compatible = "sirf,ns2m30-rproc",
		.data = &sirf_rproc_hwinfo[NS2M30],
	}, {
		.compatible = "sirf,ns2m31-rproc",
		.data = &sirf_rproc_hwinfo[NS2M31],
	}, {
		.compatible = "sirf,ns2kal0-rproc",
		.data = &sirf_rproc_hwinfo[NS2KAL0],
	}, {
		.compatible = "sirf,ns2kal1-rproc",
		.data = &sirf_rproc_hwinfo[NS2KAL1],
	},
};

static u32 sirf_rproc_features(struct device *dev)
{
	const struct of_device_id *match;

	match = of_match_device(sirf_rproc_dt_ids, dev);
	if (!match) {
		dev_err(dev, "Using default features!\n");
		return DEF_FEATURES;
	}

	return ((struct hw_info *)match->data)->features;
}

static struct rproc_ops sirf_rproc_ops = {
	.start = sirf_rproc_start,
	.stop = sirf_rproc_stop,
	.kick = sirf_rproc_kick,
	.resource = sirf_rproc_resource,
	.release = sirf_rproc_release,
	.features = sirf_rproc_features,
};

static int __sirf_rproc_parse_memory(struct platform_device *pdev,
				struct sirf_rproc *srproc)
{
	struct device_node *m_node;
	struct resource res;
	int ret;

	m_node = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (!m_node)
		return -ENODEV;

	ret = of_address_to_resource(m_node, 0, &res);
	if (ret) {
		dev_err(&pdev->dev,
			"Convert address to resource failed! ret=%d\n",
			ret);
		return ret;
	}

	srproc->rsc_table_pa = (void *)__phys_to_virt(res.start);
	srproc->rsc_table_len = res.end - res.start + 1;

	return 0;
}

static int __sirf_rproc_parse_args(struct platform_device *pdev,
				struct sirf_rproc *srproc)
{
	void *tx_buffer, *rx_buffer;
	struct resource_table *rsc_table;
	int ret;

	ret = of_irq_get(pdev->dev.of_node, 0);
	if (ret == -EPROBE_DEFER) {
		dev_err(&pdev->dev,
			"Unable to find IRQ number. ret=%d\n", ret);
		goto failed;
	}
	srproc->irq = ret;

	/* retrieve io base */
	srproc->io_base = of_iomap(pdev->dev.of_node, 0);
	if (!srproc->io_base) {
		dev_err(&pdev->dev, "Unable to map rproc registers!\n");
		ret = -ENOMEM;
		goto failed;
	}

	/* Parse share memory information */
	ret = __sirf_rproc_parse_memory(pdev, srproc);
	if (ret) {
		dev_err(&pdev->dev,
			"Unable to setup ipc share memory info. ret=%d\n",
			ret);
		goto free_io;
	}

	srproc->fifo_sz = srproc->hwinfo->fifo_sz;
	if (srproc->fifo_sz * 2 >= srproc->rsc_table_len) {
		dev_err(&pdev->dev,
			"There is no memory left for resource table!\n");
		ret = -EINVAL;
		goto free_io;
	}

	if (srproc->hwinfo->features & RPROC_F_BACKEND)
		goto setup_rsc;

	rsc_table = srproc->rsc_table_pa;
	if (rsc_table->ver != 1 &&
		rsc_table->ver != RPROC_RSC_TABLE_VER_DUAL_OS) {
		dev_err(&pdev->dev,
			"unsupported fw ver: %d\n",
			rsc_table->ver);
		ret = -EINVAL;
		goto free_io;
	}

setup_rsc:
	srproc->rsc_table_len = srproc->rsc_table_len - srproc->fifo_sz * 2;

	tx_buffer = srproc->rsc_table_pa + srproc->rsc_table_len +
		srproc->fifo_sz * srproc->hwinfo->w_fifo_chn;
	rx_buffer = srproc->rsc_table_pa + srproc->rsc_table_len +
		srproc->fifo_sz * srproc->hwinfo->r_fifo_chn;

	ret = fifo_init(&srproc->w_fifo, tx_buffer,
			srproc->fifo_sz, srproc->hwinfo->w_fifo_lock);
	if (ret)
		goto free_io;

	ret = fifo_init(&srproc->r_fifo, rx_buffer,
			srproc->fifo_sz, srproc->hwinfo->r_fifo_lock);
	if (ret)
		goto free_io;

	srproc->set_reg = srproc->io_base + srproc->hwinfo->setreg;
	srproc->clr_reg = srproc->io_base + srproc->hwinfo->clrreg;

	return 0;

free_io:
	iounmap(srproc->io_base);
	srproc->io_base = NULL;
	srproc->rsc_table_pa = NULL;

failed:
	return ret;
}

static int sirf_rproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);

	rproc_del(rproc);
	rproc_put(rproc);

	return 0;
}

static int sirf_rproc_probe(struct platform_device *pdev)
{
	struct sirf_rproc *srproc;
	struct rproc *rproc;
	const struct of_device_id *match;
	const struct hw_info *hwinfo;
	int ret;

	match = of_match_device(sirf_rproc_dt_ids, &pdev->dev);
	if (!match) {
		dev_err(&pdev->dev, "Unknown device model\n");
		return -EINVAL;
	}
	/* retrieve hwinfo */
	hwinfo = match->data;

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "dma_set_coherent_mask: %d\n", ret);
		return ret;
	}

	rproc = rproc_alloc(&pdev->dev, hwinfo->name,
			&sirf_rproc_ops, NULL, sizeof(*srproc));
	if (!rproc)
		return -ENOMEM;

	rproc->vdev_desc_tbl = hwinfo->vdev_desc;
	rproc->vdev_desc_tbl_len = hwinfo->vdev_num;
	srproc = rproc->priv;
	srproc->rproc = rproc;
	srproc->hwinfo = hwinfo;

	spin_lock_init(&srproc->w_fifo_lock);

	ret = __sirf_rproc_parse_args(pdev, srproc);
	if (ret)
		goto free_rproc;

	ret = devm_request_irq(&rproc->dev, srproc->irq, sirf_rproc_ipc_isr,
				0, hwinfo->name, rproc);
	if (ret) {
		dev_err(&rproc->dev,
			"request_threaded_irq %d error: %d\n",
			srproc->irq, ret);
		goto free_rproc;
	}

	dev_info(&rproc->dev,
		"Register SIRF remote processor device to IRQ:%d\n",
		srproc->irq);

	ret = rproc_add(rproc);
	if (ret) {
		dev_err(&rproc->dev, "rproc_add failed: %d\n", ret);
		goto free_rproc;
	}

	/* sync resource table share memory */
	smp_mb();

	platform_set_drvdata(pdev, rproc);

	return 0;

free_rproc:
	rproc_put(rproc);

	return ret;
}

static struct platform_driver sirf_rproc_driver = {
	.probe = sirf_rproc_probe,
	.remove = sirf_rproc_remove,
	.driver = {
		.name = "sirfsoc_remoteproc",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(sirf_rproc_dt_ids),
	},
};
module_platform_driver(sirf_rproc_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("SIRF Remote Processor driver");
