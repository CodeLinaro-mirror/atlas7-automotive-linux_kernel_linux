/*
 * CSRVisor Remote processor machine-specific module
 *
 * Copyright (c) 2014 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>

#include <linux/remoteproc.h>
#include <linux/remoteproc_dualos.h>
#include <linux/csrvisor_fifo.h>
#include <linux/csrvisor_syscalls.h>

#ifdef CONFIG_CSRVISOR_REMOTEPROC_BACKEND
#include <linux/virtio_ids.h>
#include <linux/rpmsg.h>
#endif

#include "remoteproc_internal.h"

/* The id of CPU which the secure mode is enable*/
#define T_CPU	0

#if defined(CONFIG_SMP) && defined(CONFIG_CSRVISOR_REMOTEPROC_FRONTEND)
/* rproc init thread on TrustZone Enabled CPU */
static struct task_struct *s_init_thread_on_tcpu;
static wait_queue_head_t s_rproc_init_wq;
static bool s_rproc_init_status;
static int s_rproc_init_result;

/* kick thread on TrustZone Enabled CPU */
struct task_struct *s_kick_thread_on_tcpu;
/* list to save smc task of non-Secure OS */
static struct list_head s_smc_task_head;
static spinlock_t s_smc_task_lock;
static wait_queue_head_t s_smc_task_wq;
#endif

struct smc_task {
	struct list_head node;
	u32 id;
};

static struct csrvisor_rproc *s_csrvisor_rproc;
static struct device s_csrvisor_rproc_dev;

/**
 * struct csrvisor_rproc - csrvisor remote processor instance state
 * @rproc: rproc handle
 * @rsc_table_pa: the physical address of rproc resource table area.
 * @rsc_table_len: the length of rproc resource table.
 * @fifo_rx_lock: lock for fifo receive data.
 * @fifo_tx_lock: lock for fifo send data.
 * @tx_avail_wq: wait queue of send data when fifo is busy.
 * @fifo_avail: fifo status for send data.
 *				fifo can send data when fifo_avail is true.
 * @fifo_msg_rx: memory address for fifo arrived data.
 * @fifo_msg_tx: memory address for fifo send data.
 * @fifo_iomemmem: iomem address for fifo register.
 * @irq: the irq number of fifo allocated in backend OS.
 * @irq_gen_count: generate IRQ counter for statistic
 * @irq_get_count: arrive IRQ counter for statistic
 */
struct csrvisor_rproc {
	struct rproc *rproc;
	void *rsc_table_pa;
	u32 rsc_table_len;
	struct mutex fifo_rx_lock, fifo_tx_lock;
	wait_queue_head_t tx_avail_wq;
	bool fifo_avail;
	struct csrvisor_fifo_msg *fifo_msg_rx;
	struct csrvisor_fifo_msg *fifo_msg_tx;
	struct csrvisor_fifo_io_req *fifo_iomem;
	int irq;
#ifdef RPROC_STATISTIC
	unsigned int irq_gen_count;
	unsigned int irq_get_count;
#endif
};

static void fifo_register_write(struct csrvisor_rproc *srproc,
			unsigned int offset, unsigned int value)
{
	srproc->fifo_iomem->offset = offset;
	srproc->fifo_iomem->w_value = value;
	srproc->fifo_iomem->r_value = 0;

	dsb();
	csrvisor_fifo_reg_write(srproc->fifo_iomem);
	dsb();
}

/* This function has not been used, disable it to avoid compiler warning */
#if 0
static unsigned int fifo_register_read(struct csrvisor_rproc *srproc,
					unsigned int offset)
{
	srproc->fifo_iomem->offset = offset;
	srproc->fifo_iomem->w_value = 0;
	srproc->fifo_iomem->r_value = 0;

	dsb();
	csrvisor_fifo_reg_read(srproc->fifo_iomem);
	dsb();

	return srproc->fifo_iomem->r_value;
}
#endif

/* Read 4 bytes integer from FIFO */
static int fifo_readl(struct rproc *rproc)
{
	struct csrvisor_rproc *srproc = rproc->priv;
	unsigned int rx_stat, tx_stat;
	int retval = 0;

	/* prepare buffer for fifo read */
	*((int *)srproc->fifo_msg_rx->buffer) = 0;
	srproc->fifo_msg_rx->bytes = 4;
	srproc->fifo_msg_rx->rw_bytes = 0;

	/* Interrupt is coming, we'll do fifo read operation.
	 * This operation will read tx status register and tx status register.
	 * If the rx status register includes IN_DATA status.
	 * There will be some incoming data that has been placed
	 * at RX DMA address.
	 */
	csrvisor_fifo_read(srproc->fifo_msg_rx);

	rx_stat = srproc->fifo_msg_rx->rx_stat;
	tx_stat = srproc->fifo_msg_rx->tx_stat;

	/*
	 * We don't need to ack the tx done interrupt,
	 * because we disable TX done intr in csrvisor.
	 * But TX err intr will be generated when tx failed.
	 */
#if 0
	if (irq_tx_stat & SW_FIFO_INTR_TX_DONE)
		dev_dbg(&rproc->dev, "SW_FIFO_INTR_TX_DONE\n");
#endif

	/* FIFO is busy, there is no enough space for new data */
	if (tx_stat & SW_FIFO_INTR_BUSY)
		srproc->fifo_avail = false;

	/*
	 * FIFO is available, there is enough space for new data.
	 * wake up the waiting queue
	 */
	if (tx_stat & SW_FIFO_INTR_AVAIL) {
		srproc->fifo_avail = true;
		wake_up(&srproc->tx_avail_wq);
	}

	/* The data is tring to be written into FIFO is too big.
	 * Its size is larger than FIFO's capacity
	 */
	if (tx_stat & SW_FIFO_INTR_BIG_SIZE) {
		dev_err(&rproc->dev, "SW_FIFO_INTR_BIG_SIZE\n");
		BUG_ON(1);
	}

	/* There is no data had been placed at RX dma memory */
	if (rx_stat & SW_FIFO_INTR_EMPTY)
		return 0;

	/* There is some data has been placed at RX dma memory */
	if (rx_stat & SW_FIFO_INTR_IN_DATA) {
		if (srproc->fifo_msg_rx->rw_bytes ==
			srproc->fifo_msg_rx->bytes)
			retval = *((int *)srproc->fifo_msg_rx->buffer);
	}

	return retval;
}

/* Write 4 bytes integer to FIFO */
static int fifo_writel(struct rproc *rproc, int value)
{
	struct csrvisor_rproc *srproc = rproc->priv;

	/* prepare buffer for channel write */
	*((int *)srproc->fifo_msg_tx->buffer) = value;
	srproc->fifo_msg_tx->bytes = sizeof(value);
	srproc->fifo_msg_tx->rw_bytes = 0;

	csrvisor_fifo_write(srproc->fifo_msg_tx);
	if (srproc->fifo_msg_tx->rw_bytes == srproc->fifo_msg_tx->bytes)
		return 0;

	if ((int)srproc->fifo_msg_tx->rw_bytes == -1UL) {
		dev_err(&rproc->dev,
			"Data size is too large to write into fifo\n");
		BUG_ON(1);
	}

	return -EBUSY;
}

/* FIFO interrupt handle thread, this thread must be pin to CPU0,
 * because fifo_readl will call SMC
 */
static irqreturn_t fifo_ist(int irq, void *data)
{
	struct rproc *rproc = (struct rproc *)data;
	int notifyid = 0;

	do {
		notifyid = fifo_readl(rproc);
		if (!notifyid)
			break;

		/* We will handle vq, vdev, vbus in different route */
		if (IS_VQ_NOTIFY(notifyid))
			rproc_vq_interrupt(rproc, GET_NOTIFY_ID(notifyid));
		else
			rproc_bus_interrupt(rproc, notifyid);
	} while (1);

	return IRQ_HANDLED;
}

/* Software FIFO init */
static void fifo_hw_init(struct csrvisor_rproc *srproc)
{
#ifdef CONFIG_CSRVISOR_REMOTEPROC_BACKEND
	/* Assign IRQ number */
	srproc->irq = FIFO_SOFT_IRQ_S;

	/* Assing IOMEM address */
	srproc->fifo_iomem =
		(struct csrvisor_fifo_io_req *)SW_FIFO_S_IOMEM_BASE;

	/* Secure OS Write Channel#0, Read Channel#1 */
	fifo_register_write(srproc, SW_FIFO_W_CHANNEL_REG, 0);
	fifo_register_write(srproc, SW_FIFO_R_CHANNEL_REG, 1);

	srproc->fifo_msg_tx = (struct csrvisor_fifo_msg *)SW_FIFO_CHN0_TX_BASE;
	srproc->fifo_msg_rx = (struct csrvisor_fifo_msg *)SW_FIFO_CHN1_RX_BASE;
#else
	/* Assign IRQ number */
	srproc->irq = FIFO_SOFT_IRQ_NS;

	/* Assing IOMEM address */
	srproc->fifo_iomem =
		(struct csrvisor_fifo_io_req *)SW_FIFO_NS_IOMEM_BASE;

	/* Non-Secure OS Write Channel#1, Read Channel#0 */
	fifo_register_write(srproc, SW_FIFO_W_CHANNEL_REG, 1);
	fifo_register_write(srproc, SW_FIFO_R_CHANNEL_REG, 0);

	srproc->fifo_msg_tx = (struct csrvisor_fifo_msg *)SW_FIFO_CHN1_TX_BASE;
	srproc->fifo_msg_rx = (struct csrvisor_fifo_msg *)SW_FIFO_CHN0_RX_BASE;
#endif
	/* Setup IRQ numbers which are allocated for SW FIFO */
	fifo_register_write(srproc, SW_FIFO_IRQ_NUM_REG, srproc->irq);
}

/* map resource table for remoteproc */
static int csrvisor_rproc_map_resource(struct csrvisor_rproc *srproc)
{
	srproc->rsc_table_pa = (void *)SIRF_RPROC_RESOURCE_TABLE_BASE;
	srproc->rsc_table_len = SIRF_RPROC_RESOURCE_TABLE_LEN;

	return 0;
}

static void csrvisor_rproc_unmap_resource(struct csrvisor_rproc *srproc)
{
	if (srproc->rsc_table_pa) {
		srproc->fifo_msg_rx = NULL;
		srproc->fifo_msg_tx = NULL;
		srproc->rsc_table_pa = 0;
		srproc->rsc_table_len = 0;
	}
	return;
}

static int csrvisor_rproc_start(struct rproc *rproc)
{
	return 0;
}

static int csrvisor_rproc_stop(struct rproc *rproc)
{
	return 0;
}


#ifdef CONFIG_CSRVISOR_REMOTEPROC_BACKEND
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

/* This table defined the virtio device will be create on remoteproc bus */
static struct rproc_vdev_desc s_rproc_vdev_desc[] = {
	/* virtio rpmsg bus device descriptor */
	{ VIRTIO_ID_RPMSG, 2, 256, { VIRTIO_RPMSG_F_NS, }, 1,
		RPROC_VDEV_MMIO_SIZE,
		s_rpmsg_channels, ARRAY_SIZE(s_rpmsg_channels) },
};
#endif

static void csrvisor_rproc_resource(struct rproc *rproc)
{
	struct csrvisor_rproc *srproc = (struct csrvisor_rproc *)rproc->priv;

	fifo_hw_init(srproc);
	csrvisor_rproc_map_resource(srproc);

	rproc->table_ptr = srproc->rsc_table_pa;
	rproc->table_len = srproc->rsc_table_len;

#ifdef CONFIG_CSRVISOR_REMOTEPROC_BACKEND
	rproc->vdev_desc_tbl = s_rproc_vdev_desc;
	rproc->vdev_desc_tbl_len = ARRAY_SIZE(s_rproc_vdev_desc);
#else
	rproc->vdev_desc_tbl = NULL;
	rproc->vdev_desc_tbl_len = 0;
#endif
}

static void csrvisor_rproc_release(struct rproc *rproc)
{
	struct csrvisor_rproc *srproc = (struct csrvisor_rproc *)rproc->priv;

	rproc->table_ptr = 0;
	rproc->table_len = 0;

	csrvisor_rproc_unmap_resource(srproc);

	return;
}


static void __csrvisor_rproc_kick(struct rproc *rproc, int notify_id)
{
	struct csrvisor_rproc *srproc = rproc->priv;
	int ret;

	do {
		ret = fifo_writel(rproc, notify_id);
		if (!ret)
			break;
		wait_event_interruptible(srproc->tx_avail_wq,
			srproc->fifo_avail);
	} while (1);
}

/* Generate a interrupt in other side. (kick a virtqueue).
 * kick will be called by Secure OS on CPU0, but will be called
 * by non-Secure OS on any cpu core.
 * So we have to put non-Secure OS kick task into a CPU0 kick thread.
 */
static void csrvisor_rproc_kick(struct rproc *rproc, int notify_id)
{
	struct csrvisor_rproc *srproc = rproc->priv;

#if defined(CONFIG_SMP) && defined(CONFIG_CSRVISOR_REMOTEPROC_FRONTEND)
	struct smc_task *task;

	task = kmalloc(sizeof(*task), GFP_KERNEL);
	if (task == NULL)
		return;

	task->id = notify_id;

	mutex_lock(&srproc->fifo_tx_lock);
	list_add_tail(&task->node, &s_smc_task_head);
	mutex_unlock(&srproc->fifo_tx_lock);
	wake_up(&s_smc_task_wq);
#else
	mutex_lock(&srproc->fifo_tx_lock);
	__csrvisor_rproc_kick(rproc, notify_id);
	mutex_unlock(&srproc->fifo_tx_lock);
#endif
}

static u32 csrvisor_rproc_features(void)
{
	u32 features = RPROC_F_DEVICE_MMIO | RPROC_F_DYNAMIC_VQ |
			RPROC_F_DEVICE_UPDATE_NOTIFY;

#ifdef CONFIG_CSRVISOR_REMOTEPROC_BACKEND
	features |= (RPROC_F_BACKEND | RPROC_F_BUS_WRITE |
			RPROC_F_PREDEFINED_VQ_NOTIFYID);
#else
	features |= (RPROC_F_FRONTEND | RPROC_F_PREDEFINED_VQ_NOTIFYID);
#endif
	return features;
}

static struct rproc_ops csrvisor_rproc_ops = {
	.start = csrvisor_rproc_start,
	.stop = csrvisor_rproc_stop,
	.kick = csrvisor_rproc_kick,
	.resource = csrvisor_rproc_resource,
	.release = csrvisor_rproc_release,
	.features = csrvisor_rproc_features,
};

#if defined(CONFIG_SMP) && defined(CONFIG_CSRVISOR_REMOTEPROC_FRONTEND)
static int csrvisor_rproc_kick_thread_on_tcpu(void *data)
{
	struct rproc *rproc = (struct rproc *)data;
	struct smc_task *task;
	unsigned long flags;
	u32 task_id;

	do {
		wait_event_interruptible(s_smc_task_wq,
			!list_empty(&s_smc_task_head) ||
			kthread_should_stop());

		if (kthread_should_stop())
			break;

		/* Grab a pending task from the task list */
		task = list_entry(s_smc_task_head.next,
				struct smc_task, node);
		if (task) {
			/* Prevent list_add_tail in ISR */
			spin_lock_irqsave(&s_smc_task_lock, flags);
			list_del(s_smc_task_head.next);
			spin_unlock_irqrestore(&s_smc_task_lock, flags);

			task_id = task->id;
			kfree(task);

			__csrvisor_rproc_kick(rproc, task_id);
		}
	} while (1);

	return 0;
}
#endif

static int csrvisor_rproc_probe(void)
{
	struct csrvisor_rproc *srproc;
	struct rproc *rproc;
	int ret;

	/*
	 * In the future, we will use the platform device
	 * to replace s_csrvisor_rproc_dev
	 */
	device_initialize(&s_csrvisor_rproc_dev);
	rproc = rproc_alloc(&s_csrvisor_rproc_dev, "csrvisor_rproc#0",
			&csrvisor_rproc_ops, NULL, sizeof(*srproc));
	if (!rproc)
		return -ENOMEM;

	srproc = rproc->priv;
	srproc->rproc = rproc;

	mutex_init(&srproc->fifo_rx_lock);
	mutex_init(&srproc->fifo_tx_lock);
	init_waitqueue_head(&srproc->tx_avail_wq);

	srproc->fifo_avail = true;

	ret = request_threaded_irq(srproc->irq, NULL, fifo_ist, IRQF_ONESHOT,
					"csrvisor_sw_fifo", rproc);
	if (ret) {
		dev_err(&rproc->dev,
			"request_threaded_irq %d error: %d\n",
			srproc->irq, ret);
		goto free_rproc;
	}
	dev_info(&rproc->dev,
		"Register csrvisor remote processor device to IRQ:%d\n",
		srproc->irq);

#if defined(CONFIG_SMP) && defined(CONFIG_CSRVISOR_REMOTEPROC_FRONTEND)
	/* We have to pin this irq to CPU0,
	 * because SMC will be call in this irq handle thread
	 */
	pr_info("Place SMC task to TrustZone Enabled CPU.\n");
	irq_set_affinity(srproc->irq, cpumask_of(T_CPU));

	spin_lock_init(&s_smc_task_lock);
	init_waitqueue_head(&s_smc_task_wq);
	INIT_LIST_HEAD(&s_smc_task_head);

	/* Create a kick thread and pin to Secure CPU */
	s_kick_thread_on_tcpu = kthread_create(
					csrvisor_rproc_kick_thread_on_tcpu,
					(void *)rproc,
					"rproc_smc_task/%d", T_CPU);
	if (IS_ERR(s_kick_thread_on_tcpu)) {
		pr_err("%s:Create kick thread on CPU#0 failed!\n", __func__);
		return PTR_ERR(s_kick_thread_on_tcpu);
	}
	kthread_bind(s_kick_thread_on_tcpu, T_CPU);
	wake_up_process(s_kick_thread_on_tcpu);
#endif
	/* Enable SW FIFO IRQ */
	fifo_register_write(srproc, SW_FIFO_IRQ_CTRL_REG, 1);

	s_csrvisor_rproc = srproc;

	ret = rproc_add(rproc);
	if (ret) {
		dev_err(&rproc->dev, "rproc_add failed: %d\n", ret);
		goto free_alloc_irq;
	}

	return 0;


free_alloc_irq:
	free_irq(srproc->irq, rproc);

free_rproc:
	rproc_put(rproc);

	return ret;
}

#if defined(CONFIG_SMP) && defined(CONFIG_CSRVISOR_REMOTEPROC_FRONTEND)
static int csrvisor_rproc_init_thread_on_tcpu(void *data)
{
	s_rproc_init_result = csrvisor_rproc_probe();
	s_rproc_init_status = true;
	wake_up(&s_rproc_init_wq);
	return 0;
}
#endif

static int csrvisor_rproc_remove(void)
{
	struct rproc *rproc = s_csrvisor_rproc->rproc;

	rproc_del(rproc);
	rproc_put(rproc);

#if defined(CONFIG_SMP) && defined(CONFIG_CSRVISOR_REMOTEPROC_FRONTEND)
	if (s_kick_thread_on_tcpu)
		kthread_stop(s_kick_thread_on_tcpu);
#endif
	s_csrvisor_rproc = NULL;
	return 0;
}

static int csrvisor_rproc_init(void)
{
	pr_info("Init csrvisor remoteproc.\n");
#if defined(CONFIG_SMP) && defined(CONFIG_CSRVISOR_REMOTEPROC_FRONTEND)
	if (smp_processor_id() != T_CPU) {
		pr_info("Place initialization to TrustZone enabled CPU.\n");
		/* put task to work thread on CPU0 */
		s_rproc_init_status = false;
		s_rproc_init_result = 0;
		init_waitqueue_head(&s_rproc_init_wq);

		s_init_thread_on_tcpu = kthread_create(
					csrvisor_rproc_init_thread_on_tcpu,
					NULL, "rproc_smc_task/%d", T_CPU);
		if (IS_ERR(s_init_thread_on_tcpu)) {
			pr_err("%s:Put task to CPU#0 failed!\n", __func__);
			return PTR_ERR(s_init_thread_on_tcpu);
		}

		/* pin this thread to TrustZone CPU */
		kthread_bind(s_init_thread_on_tcpu, T_CPU);
		/* Start this thread */
		wake_up_process(s_init_thread_on_tcpu);
		/* Wait initializtion to be completed */
		wait_event_interruptible(s_rproc_init_wq, s_rproc_init_status);

		return s_rproc_init_result;
	}
#endif
	return csrvisor_rproc_probe();
}

static void __exit csrvisor_rproc_exit(void)
{
	csrvisor_rproc_remove();
}
module_init(csrvisor_rproc_init);
module_exit(csrvisor_rproc_exit);


MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("CSRVisor Remote Processor driver");
