/*
 * CSR SiRFSoc TS driver of USP and VIP controller
 *
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
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

#include <linux/types.h>
#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/interrupt.h>

#include "ts.h"

#define TS_DRV_NAME "usp_ts"

#define TS_FRAME_SIZE 188
#define TS_FRAME_BUF_NUM 1024

#define USP_FIFO_SIZE 512

#define MAX_SEQ_NUM   1024
#define MAX_INT_NUM   (MAX_SEQ_NUM * 2)


static int ts_pm_suspend(struct device *dev);
static int ts_pm_resume(struct device *dev);
static int ts_ioctl_stop(struct ts_dev *ts);

/*****************************TS HW*************************/

static inline void ts_write(struct ts_dev *ts,
			unsigned int regoffset, unsigned int value)
{
	writel(value, ts->regbase + (regoffset));

}

static inline unsigned int ts_read(struct ts_dev *ts,
			unsigned int regoffset)
{
	return readl(ts->regbase + (regoffset));
}

static long ts_usp_start(struct ts_dev *ts)
{
	unsigned int fifo_l = 16 / 4;
	unsigned int fifo_m = (USP_FIFO_SIZE / 2) / 4;
	unsigned int fifo_h = (USP_FIFO_SIZE - 16) / 4;

	unsigned int data_len	 = ts->frame_len*8;
	unsigned int frame_len	 = ts->frame_len*8;
	unsigned int shifter_len = 32;

	ts_write(ts, USP_MODE1, 0);

	/*although the usp work in TS module,
	we need to set the pins to be input IO mode manually,
	otherwise the loading of TS bus is too heavy
	for the signal source, and the data may be
	sampled incorrectly.*/
	ts_write(ts, USP_MODE1, USP_TXD_IO_MODE_INPUT |
		USP_TFS_IO_MODE_INPUT |
		USP_RXD_IO_MODE_INPUT |
		USP_SCLK_IO_MODE_INPUT |
		USP_SCLK_PIN_MODE_IO  |
		USP_RXD_PIN_MODE_IO   |
		USP_TXD_PIN_MODE_IO   |
		USP_TFS_PIN_MODE_IO   |
		USP_SYNC_MODE |
		USP_RFS_ACT_LEVEL_LOGIC1 |
		USP_TFS_ACT_LEVEL_LOGIC1 |
		USP_CLOCK_MODE_SLAVE | USP_EN);

	/*clear and disable all the interrupts */
	ts_write(ts, USP_INT_STATUS, 0x0003FFFF);
	ts_write(ts, USP_INT_ENABLE_CLR, 0x0000FFFF);

#if defined(CONFIG_ATLAS7_TS_DEBUG)
	/*enable TS interrupts	*/
	ts_write(ts, USP_INT_ENABLE_SET, (0x07 << 16));
#endif

	ts_write(ts, USP_RISC_DSP_MODE,   0);
	ts_write(ts, USP_RX_DMA_IO_LEN,   0);

	ts_write(ts, USP_MODE2, (1 << USP_RXD_DELAY_LEN_OFFSET) |
			USP_TFS_CLK_SLAVE_MODE | USP_RFS_CLK_SLAVE_MODE);


	ts_write(ts, USP_RX_DMA_IO_CTRL, 0x10);

	ts_write(ts, USP_RX_FRAME_CTRL, RX_DATA_LEN_L(data_len) |
		RX_FRAME_LEN_L(frame_len) |
		RX_SHIFT_LEN_L(shifter_len));

	ts_write(ts, USP_TRX_LEN_HI, RX_DATA_LEN_H(data_len) |
		RX_FRAME_LEN_H(frame_len));

	/* Configure RX FIFO Control register */
	ts_write(ts, USP_RX_FIFO_CTRL,
		((USP_FIFO_SIZE / 2) << USP_RX_FIFO_THD_OFFSET) |
		(2 << USP_RX_FIFO_WIDTH_OFFSET));

	/* Configure RX FIFO Level Check register */
	ts_write(ts, USP_RX_FIFO_LEVEL_CHK,
		RX_FIFO_SC(fifo_l) | RX_FIFO_LC(fifo_m) | RX_FIFO_HC(fifo_h));


	ts_write(ts, USP_RX_FIFO_OP,   USP_RX_FIFO_RESET);
	ts_write(ts, USP_RX_FIFO_OP,   USP_RX_FIFO_START);
	ts_write(ts, USP_TX_RX_ENABLE, USP_TSIF_VALID_MODE |
		USP_TSIF_SYNC_BYTE | USP_TSIF_EN | USP_RX_ENA);


	return 0;
}

static long ts_usp_stop(struct ts_dev *ts)
{

	ts_write(ts, USP_INT_ENABLE_CLR, 0x0000FFFF);
	ts_write(ts, USP_RX_FIFO_OP,   0);
	ts_write(ts, USP_TX_RX_ENABLE, 0);
	return 0;
}

static long ts_vip_start(struct ts_dev *ts)
{
	u32 val;

	ts_write(ts, CAM_INT_EN, 0);
	ts_write(ts, CAM_CTRL, 0);

	ts_write(ts, CAM_PIXEL_SHIFT, CAM_PIXEL_SHIFT_0TO7);
	ts_write(ts, CAM_PXCLK_CFG, 0);
	ts_write(ts, CAM_INPUT_BIT_SEL_0, 0);
	ts_write(ts, CAM_INPUT_BIT_SEL_1, 1);
	ts_write(ts, CAM_INPUT_BIT_SEL_2, 2);
	ts_write(ts, CAM_INPUT_BIT_SEL_3, 3);
	ts_write(ts, CAM_INPUT_BIT_SEL_4, 4);
	ts_write(ts, CAM_INPUT_BIT_SEL_5, 5);
	ts_write(ts, CAM_INPUT_BIT_SEL_6, 6);
	ts_write(ts, CAM_INPUT_BIT_SEL_7, 7);
	ts_write(ts, CAM_INPUT_BIT_SEL_8, 12);
	ts_write(ts, CAM_INPUT_BIT_SEL_9, 13);
	ts_write(ts, CAM_INPUT_BIT_SEL_10, 14);
	ts_write(ts, CAM_INPUT_BIT_SEL_11, 15);
	ts_write(ts, CAM_INPUT_BIT_SEL_12, 16);
	ts_write(ts, CAM_INPUT_BIT_SEL_13, 17);
	ts_write(ts, CAM_INPUT_BIT_SEL_14, 18);
	ts_write(ts, CAM_INPUT_BIT_SEL_15, 19);
	ts_write(ts, CAM_INPUT_BIT_SEL_HSYNC, 9);
	ts_write(ts, CAM_INPUT_BIT_SEL_VSYNC, 10);


	ts_write(ts, CAM_FIFO_CTRL_REG, 0x01);
	/* Disable FIFO */
	ts_write(ts, CAM_FIFO_OP_REG, CAM_FIFO_OP_FIFO_RESET);
	/* Set FIFO config data, high check, low check and stop check. */
	ts_write(ts, CAM_FIFO_LEVEL_CHECK, (0x10 << 20) | (0x08 << 10) | 0x01);

	/* clear all interrupts */
	ts_write(ts, CAM_INT_CTRL, CAM_INT_CTRL_MASK_A7);

	/* Enable overflow, underflow, sensor interrupt and bad field */
	ts_write(ts, CAM_INT_EN, CAM_INT_EN_FIFO_OFLOW |
				CAM_INT_EN_FIFO_UFLOW);


	ts_write(ts, CAM_TS_CTRL, CAM_TS_CTRL_INIT | CAM_TS_CTRL_VIP_TS);
	ts_write(ts, CAM_TS_CTRL, CAM_TS_CTRL_VIP_TS);

	/* Reset and active all the configuration before starting */
	val = ts_read(ts, CAM_CTRL);
	ts_write(ts, CAM_CTRL, val | CAM_CTRL_INIT);
	ts_write(ts, CAM_CTRL, val & ~CAM_CTRL_INIT);

	/* Start FIFO transfer to DMA */
	ts_write(ts, CAM_FIFO_OP_REG, CAM_FIFO_OP_FIFO_START);
	return 0;
}


static long ts_vip_stop(struct ts_dev *ts)
{
	ts_write(ts, CAM_FIFO_OP_REG, CAM_FIFO_OP_FIFO_STOP);
	ts_write(ts, CAM_CTRL, CAM_CTRL_INIT);
	return 0;
}

static long ts_hw_start(struct ts_dev *ts)
{
	long ret;

	ret = ts->ops->hw_start(ts);
	return ret;
}

static long ts_hw_stop(struct ts_dev *ts)
{
	long ret;

	ret = ts->ops->hw_stop(ts);
	return ret;
}

static void ts_usp_irq(struct ts_dev *ts, int irq)
{
	unsigned int status;

	status = ts_read(ts, USP_INT_STATUS);
	ts_write(ts, USP_INT_STATUS, status);

#if defined(CONFIG_ATLAS7_TS_DEBUG)
	if (status & USP_RX_TSIF_SYNC_BYTE_ERR)
		pr_info("TS INT: USP_RX_TSIF_SYNC_BYTE_ERR\n");

	if (status & USP_RX_TSIF_PROTOCOL_ERR)
		pr_info("TS INT: USP_RX_TSIF_PROTOCOL_ERR\n");

	if (status & USP_RX_TSIF_ERR)
			pr_info("TS INT: USP_RX_TSIF_ERR\n");
#endif /* CONFIG_ATLAS7_TS_DEBUG */

}

static void ts_vip_irq(struct ts_dev *ts, int irq)
{
	unsigned int status;

	status = ts_read(ts, CAM_INT_CTRL);
	ts_write(ts, CAM_INT_CTRL, status);
}

static irqreturn_t ts_irq(int irq, void *data)
{
	struct ts_dev *ts = data;

	ts->ops->hw_irq(ts, irq);
	return IRQ_HANDLED;
}


static void ts_usp_dump_registers(struct ts_dev *ts)
{
	pr_info("USP_MODE1          =0x%x\n", ts_read(ts, USP_MODE1));
	pr_info("USP_MODE2          =0x%x\n", ts_read(ts, USP_MODE2));
	pr_info("USP_RX_FRAME_CTRL  =0x%x\n", ts_read(ts, USP_RX_FRAME_CTRL));
	pr_info("USP_TRX_LEN_HI     =0x%x\n", ts_read(ts, USP_TRX_LEN_HI));
	pr_info("USP_TX_RX_ENABLE   =0x%x\n", ts_read(ts, USP_TX_RX_ENABLE));
	pr_info("USP_INT_ENABLE     =0x%x\n", ts_read(ts, USP_INT_ENABLE_SET));
	pr_info("USP_INT_STATUS     =0x%x\n", ts_read(ts, USP_INT_STATUS));
	pr_info("USP_RX_DMA_IO_CTRL =0x%x\n", ts_read(ts, USP_RX_DMA_IO_CTRL));
	pr_info("USP_RX_DMA_IO_LEN  =0x%x\n", ts_read(ts, USP_RX_DMA_IO_LEN));
	pr_info("USP_RX_FIFO_CTRL   =0x%x\n", ts_read(ts, USP_RX_FIFO_CTRL));
	pr_info("USP_RX_FIFO_LEVEL  =0x%x\n",
		ts_read(ts, USP_RX_FIFO_LEVEL_CHK));
	pr_info("USP_RX_FIFO_OP     =0x%x\n", ts_read(ts, USP_RX_FIFO_OP));
	pr_info("USP_RX_FIFO_STATUS =0x%x\n", ts_read(ts, USP_RX_FIFO_STATUS));
}

static void ts_vip_dump_registers(struct ts_dev *ts)
{
	pr_info("CAM_CTRL            =0x%x\n", ts_read(ts, CAM_CTRL));
	pr_info("CAM_TS_CTRL         =0x%x\n", ts_read(ts, CAM_TS_CTRL));
	pr_info("CAM_INT_EN          =0x%x\n", ts_read(ts, CAM_INT_EN));
	pr_info("CAM_INT_CTRL        =0x%x\n", ts_read(ts, CAM_INT_CTRL));
	pr_info("CAM_DMA_CTRL        =0x%x\n", ts_read(ts, CAM_DMA_CTRL));
	pr_info("CAM_FIFO_CTRL_REG   =0x%x\n", ts_read(ts, CAM_FIFO_CTRL_REG));
	pr_info("CAM_FIFO_OP_REG     =0x%x\n", ts_read(ts, CAM_FIFO_OP_REG));
	pr_info("CAM_FIFO_STATUS_REG =0x%x\n",
		ts_read(ts, CAM_FIFO_STATUS_REG));
	pr_info("CAM_PXCLK_CFG       =0x%x\n", ts_read(ts, CAM_PXCLK_CFG));
}


/******************************DMA*************************/

static inline void ts_dma_update_info(struct ts_dev *ts,
	unsigned int pos, unsigned int update_flags)
{

	if (update_flags & UPDATE_FLAGS_INTR)
		if (++ts->dma_intr_num >= MAX_INT_NUM)
			ts->dma_intr_num = 0;

	if (update_flags & UPDATE_FLAGS_POS)
		ts->dma_running_pos = pos;
}

static unsigned int ts_usp_dma_get_pos(struct ts_dev *ts)
{
	struct dma_tx_state tx_state;
	unsigned int pos_running;

	dmaengine_tx_status(ts->rx_chan, ts->dma_cookie, &tx_state);
	pos_running = ts->buffer_size - tx_state.residue;

	return pos_running;
}

static unsigned int ts_vip_dma_get_pos(struct ts_dev *ts)
{
	unsigned int pos_running;

	pos_running = ts_read(ts, DMAN_CUR_DATA_ADDR)
				- ts_read(ts, DMAN_ADDR);
	return pos_running;
}



static unsigned int ts_dma_get_current_status(struct ts_dev *ts,
	dma_addr_t *data_addr, unsigned int *data_size)
{
	dma_addr_t start_addr;
	unsigned int pos_running;
	unsigned int pos;
	unsigned int size;

	pos_running = ts->ops->dma_get_pos(ts);

	pos = ts->dma_running_pos;

	start_addr = ts->buffer_addr_dma + pos;
	if (pos_running > pos) {
		size = pos_running - pos;
		pos = pos_running;
	} else {
		/*only report the end part of the buffer,
		to avoid the system	performance drop
		due to call dma_sync_single_for_cpu() frequently.
		*/
		size = ts->buffer_size - pos;
		pos = 0;
	}
	if (data_addr)
		*data_addr = start_addr;
	if (data_size)
		*data_size = size;

	return pos;

}


static void ts_usp_dma_irq(struct ts_dev *ts)
{

}


static void ts_vip_dma_irq(struct ts_dev *ts)
{
	ts_write(ts, DMAN_INT, ts_read(ts, DMAN_INT));
	ts_write(ts, DMAN_LOOP_CTRL, (1<<0)|(1<<16));
}


static irqreturn_t ts_dma_irq(int irq, void *data)
{
	struct ts_dev *ts = data;
	dma_addr_t start_addr;
	unsigned int pos;
	unsigned int size;
	unsigned int update_flags;
	unsigned long flags;

	ts->ops->dma_irq(ts);

	update_flags = UPDATE_FLAGS_INTR | UPDATE_FLAGS_POS;

	spin_lock_irqsave(&ts->buffer_lock, flags);
	pos = ts_dma_get_current_status(ts, &start_addr, &size);
	ts_dma_update_info(ts, pos, update_flags);
	spin_unlock_irqrestore(&ts->buffer_lock, flags);

	dma_sync_single_for_cpu(ts->dev, start_addr,
						size, DMA_FROM_DEVICE);

	ts->data_is_ready = true;
	wake_up(&ts->wait_read);

	return IRQ_HANDLED;
}

static void ts_dma_usp_callback(void *data)
{
	struct ts_dev *ts = data;

	ts_dma_irq(0, ts);
}


static unsigned int ts_dma_usp_start(struct ts_dev *ts)
{
	struct dma_slave_config rx_slv_cfg = {
		.src_maxburst = 2,
	};

	if (test_and_set_bit(TS_FLAG_DMA, &ts->device_flags))
			return 0;

	dmaengine_slave_config(ts->rx_chan, &rx_slv_cfg);

	ts->desc = dmaengine_prep_dma_cyclic(ts->rx_chan, ts->buffer_addr_dma,
			ts->buffer_size, ts->buffer_size / 2,
			DMA_DEV_TO_MEM, DMA_PREP_INTERRUPT);
	if (IS_ERR_OR_NULL(ts->desc)) {
		dev_err(ts->dev, "hs-ts: prep rx dma failed\n");
		return -ENODEV;
	}

	ts->dma_intr_num = 0;
	ts->dma_running_pos = 0;

	ts->desc->callback = ts_dma_usp_callback;
	ts->desc->callback_param = ts;

	ts->dma_cookie = dmaengine_submit(ts->desc);
	dma_async_issue_pending(ts->rx_chan);

	return 0;
}

static bool ts_dam_cal_xy(struct ts_dev *ts,
	unsigned int len, unsigned int *x, unsigned int *y)
{
	unsigned int xlen, ylen;
	unsigned int item_len;
#define DMA_XY_MAX 0x10000

	if ((len % sizeof(unsigned int)))
		return false;

	item_len = ts->frame_len;
	if (!(len % item_len)) {
		for (xlen = item_len; xlen < DMA_XY_MAX; xlen += item_len) {
			ylen = len/xlen;
			if (ylen < DMA_XY_MAX) {
				*x = xlen;
				*y = ylen;
				return true;
			}
		}
	} else {
		item_len = sizeof(unsigned int);
		for (xlen = item_len; xlen < DMA_XY_MAX; xlen += item_len) {
			ylen = len/xlen;
			if (ylen < DMA_XY_MAX) {
				*x = xlen;
				*y = ylen;
				return true;
			}
		}
	}

	return false;
}

static unsigned int ts_dma_vip_start(struct ts_dev *ts)
{
	unsigned int x;
	unsigned int y;

	ts_write(ts, CAM_DMA_LEN, 0);
	ts_write(ts, CAM_DMA_CTRL, 0);

	ts_write(ts, DMAN_XLEN, 0);

	ts_dam_cal_xy(ts, ts->buffer_size, &x, &y);

	ts_write(ts, DMAN_YLEN, y - 1);
	ts_write(ts, DMAN_WIDTH, x/4);

	ts_write(ts, DMAN_MUL, ts->buffer_size/4/2);

	ts_write(ts, DMAN_CTRL, 0x3);

	ts_write(ts, DMAN_INT_EN, (1<<3) | (1<<0));

	ts_write(ts, DMAN_LOOP_CTRL, (1<<0) | (1<<16));

	ts_write(ts, DMAN_ADDR, ts->buffer_addr_dma);

	return 0;
}


static unsigned int ts_dma_start(struct ts_dev *ts)
{
	long ret;

	ret = ts->ops->dma_start(ts);
	return ret;
}

static void ts_dma_usp_stop(struct ts_dev *ts)
{
	dmaengine_terminate_all(ts->rx_chan);
}

static void ts_dma_vip_stop(struct ts_dev *ts)
{
	unsigned int val;
	uint32_t times = 200000;
	unsigned int state_mask;

	/*wait command and data fifo is empty firstly  */
	state_mask = (1<<5) | (1<<7);
	while (((ts_read(ts, DMAN_STATE0) & state_mask)
		!= state_mask) && times--)
		cpu_relax();

	/*stop if dma is idle */
	if (times) {
		val = ts_read(ts, DMAN_CTRL);
		val |= (1<<6);
		ts_write(ts, DMAN_CTRL, val);
	}
}

static unsigned int ts_dma_stop(struct ts_dev *ts)
{
	if (test_and_clear_bit(TS_FLAG_DMA, &ts->device_flags))
		ts->ops->dma_stop(ts);

	return 0;
}

static int ts_dma_req_buffer(struct ts_dev *ts, unsigned int len)
{
	if (!len)
		return -EINVAL;

	if (ts->buffer_addr_cpu) {
		dma_free_coherent(ts->dev, ts->buffer_size,
			ts->buffer_addr_cpu, ts->buffer_addr_dma);
	}

	ts->buffer_addr_cpu = dma_alloc_coherent(ts->dev, len,
			&ts->buffer_addr_dma, GFP_KERNEL);
	if (!ts->buffer_addr_cpu) {
		dev_err(ts->dev, "%s: request dma memory failed\n",
			TS_DRV_NAME);
		return -ENOMEM;
	}

	ts->buffer_size = len;

	return 0;
}

static void ts_dma_get_info(struct ts_dev *ts,
		struct ts_buffer_info *info)
{
	unsigned long flags;

	spin_lock_irqsave(&ts->buffer_lock, flags);
	info->buffer_size = ts->buffer_size;
	info->running_pos = ts->dma_running_pos;
	info->seq_num = ts->dma_intr_num/2;
	spin_unlock_irqrestore(&ts->buffer_lock, flags);
}

static int ts_usp_dma_setup(struct ts_dev *ts, struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;

	ts->rx_chan = dma_request_slave_channel(dev, "rx");
	if (!ts->rx_chan) {
		dev_err(dev, "request rx dma failed\n");
		ret = -ENODEV;
	}
	return ret;
}

static int ts_vip_dma_setup(struct ts_dev *ts, struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;

	ts->dma_irq = platform_get_irq(pdev, 1);
	if (!ts->dma_irq) {
		dev_err(dev, "%s: fail to get dma irq\n", __func__);
		return -ENODEV;
	}
	ret = devm_request_irq(dev, ts->dma_irq, ts_dma_irq,
		0, TS_DRV_NAME, ts);
	if (ret) {
		dev_err(dev, "%s: fail to request dma irq\n", __func__);
		return -ENODEV;
	}

	return ret;
}



static const struct ts_ops ts_usp_ops = {
	.hw_start = ts_usp_start,
	.hw_stop  = ts_usp_stop,
	.hw_irq   = ts_usp_irq,
	.hw_dump_registers = ts_usp_dump_registers,
	.dma_get_pos = ts_usp_dma_get_pos,
	.dma_irq = ts_usp_dma_irq,
	.dma_setup = ts_usp_dma_setup,
	.dma_start = ts_dma_usp_start,
	.dma_stop  = ts_dma_usp_stop,
};

static const struct ts_ops ts_vip_ops = {
	.hw_start = ts_vip_start,
	.hw_stop  = ts_vip_stop,
	.hw_irq   = ts_vip_irq,
	.hw_dump_registers = ts_vip_dump_registers,
	.dma_get_pos = ts_vip_dma_get_pos,
	.dma_irq = ts_vip_dma_irq,
	.dma_setup = ts_vip_dma_setup,
	.dma_start = ts_dma_vip_start,
	.dma_stop  = ts_dma_vip_stop,
};

static int ts_open(struct inode *inode, struct file *filp)
{
	struct ts_dev *ts =
		container_of(filp->private_data, struct ts_dev, misc_dev);

	if (test_and_set_bit(TS_FLAG_OPEN, &ts->device_flags))
		return -EBUSY;

	ts->data_is_ready = false;
	return 0;
}

static int ts_release(struct inode *inode, struct file *filp)
{
	struct ts_dev *ts =
		container_of(filp->private_data, struct ts_dev, misc_dev);

	if (test_bit(TS_FLAG_START, &ts->device_flags))
		ts_ioctl_stop(ts);

	clear_bit(TS_FLAG_OPEN,  &ts->device_flags);

	return 0;
}

static int ts_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct ts_dev *ts =
		container_of(filp->private_data, struct ts_dev, misc_dev);
	int ret;

	vma->vm_pgoff = ts->buffer_addr_dma >> PAGE_SHIFT;
	ret = remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
				vma->vm_end - vma->vm_start,
				vma->vm_page_prot);
	return ret;
}

static unsigned int ts_poll(struct file *filp, poll_table *pt)
{
	struct ts_dev *ts =
		container_of(filp->private_data, struct ts_dev, misc_dev);
	unsigned int mask = 0;

	poll_wait(filp, &ts->wait_read, pt);

	if (ts->data_is_ready) {
		mask |= POLLIN;
		ts->data_is_ready = false;
	}

	return mask;
}

static int ts_ioctl_start(struct ts_dev *ts)
{

	if (test_bit(TS_FLAG_START, &ts->device_flags))
		return -EBUSY;

	ts_dma_start(ts);
	ts_hw_start(ts);

	set_bit(TS_FLAG_START, &ts->device_flags);
	return 0;
}

static int ts_ioctl_stop(struct ts_dev *ts)
{
	ts_hw_stop(ts);
	ts_dma_stop(ts);
	clear_bit(TS_FLAG_START, &ts->device_flags);

	return 0;
}

static int ts_ioctl_get_buffer_info(struct ts_dev *ts,
	struct ts_buffer_info *user_info)
{
	struct ts_buffer_info info;
	int ret;

	ts_dma_get_info(ts, &info);
	ret = copy_to_user(user_info, &info, sizeof(info));

	return ret;
}

static int ts_ioctl_req_buffer(struct ts_dev *ts, unsigned long len)
{
	int ret;

	ret = ts_dma_req_buffer(ts, len);

	return ret;
}

static int ts_ioctl_get_para(struct ts_dev *ts, struct ts_para *user_para)
{
	struct ts_para para;
	int ret;

	para.frame_length = ts->frame_len;

	ret = copy_to_user(user_para, &para, sizeof(*user_para));

	return ret;
}


static int ts_ioctl_set_para(struct ts_dev *ts, struct ts_para *user_para)
{
	struct ts_para para;

	if (copy_from_user(&para, user_para , sizeof(para)))
		return -EFAULT;

	if (para.frame_length != 188 && para.frame_length != 204)
		return -EINVAL;

	ts->frame_len = para.frame_length;

	return 0;
}

static long ts_ioctl(struct file *filp, unsigned int cmd,
					unsigned long arg)
{
	struct ts_dev *ts =
		container_of(filp->private_data, struct ts_dev, misc_dev);
	int ret = 0;

	switch (cmd) {
	case IOCTL_START:
		ret = ts_ioctl_start(ts);
		break;
	case IOCTL_STOP:
		ret = ts_ioctl_stop(ts);
		break;
	case IOCTL_GET_BUFFER_INFO:
		ret = ts_ioctl_get_buffer_info(ts,
			(struct ts_buffer_info *)arg);
		break;
	case IOCTL_REQ_BUFFER:
		ret = ts_ioctl_req_buffer(ts, arg);
		break;
	case IOCTL_GET_PARAM:
		ret = ts_ioctl_get_para(ts, (struct ts_para *)arg);
		break;
	case IOCTL_SET_PARAM:
		ret = ts_ioctl_set_para(ts, (struct ts_para *)arg);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct file_operations ts_fops = {
	.unlocked_ioctl = ts_ioctl,
	.open = ts_open,
	.release = ts_release,
	.mmap = ts_mmap,
	.poll = ts_poll,
};



static int ts_probe(struct platform_device *pdev)
{
	int ret;
	struct ts_dev *ts;
	struct device *dev = &pdev->dev;
	struct resource *res;
	const struct ts_portdata *port_data;
	const struct of_device_id *match;
	u32 index;

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	spin_lock_init(&ts->buffer_lock);
	init_waitqueue_head(&ts->wait_read);

	ts->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(ts->clk)) {
		dev_err(dev, "get clk failed\n");
		return PTR_ERR(ts->clk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ts->regbase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ts->regbase)) {
		dev_err(dev, "request ioremap failed\n");
		ret = -ENODEV;
		goto out;
	}

	match = of_match_device(dev->driver->of_match_table, dev);
	if (WARN_ON(!match)) {
		ret = -ENODEV;
		goto out;
	}
	port_data = (const struct ts_portdata *)(match->data);
	ts->ops = port_data->ops;

	if (of_property_read_u32(dev->of_node, "cell-index", &index)) {
		dev_err(dev, "Fail to get dev index\n");
		ret = -ENODEV;
		goto out;
	}

	if (!port_data->port_base)
		ts->is_usp_port = true;
	ts->dev_num = port_data->port_base + index;

	/*it is ok if the interrupt setting is invaild. */
	ts->irq = platform_get_irq(pdev, 0);
	if (!ts->irq)
		dev_err(dev, "%s: fail to get irq\n", __func__);

	ret = devm_request_irq(dev, ts->irq, ts_irq, 0, TS_DRV_NAME, ts);
	if (ret)
		dev_err(dev, "%s: fail to request irq\n", __func__);


	ret = ts->ops->dma_setup(ts, pdev);
	if (ret)
		goto out;

	ts->frame_len = TS_FRAME_SIZE;
	ts->buffer_size = ts->frame_len*TS_FRAME_BUF_NUM;
	if (ts_dma_req_buffer(ts, ts->buffer_size)) {
		ret = -ENOMEM;
		goto out;
	}

	ts->dev = dev;
	dev_set_drvdata(dev, ts);

	ts->misc_dev.name = devm_kasprintf(dev, GFP_KERNEL, "usp_ts%d",
		ts->dev_num);
	ts->misc_dev.fops = &ts_fops;
	ts->misc_dev.minor = MISC_DYNAMIC_MINOR;

	ret = misc_register(&ts->misc_dev);
	if (ret) {
		dev_err(dev, "misc register fail\n");
		goto out;
	}

	ts_pm_resume(dev);

	return 0;
out:
	dev_err(dev, "ts_probe fail\n");
	return ret;
}

static int ts_remove(struct platform_device *pdev)
{
	struct ts_dev *ts = dev_get_drvdata(&pdev->dev);

	if (ts->is_usp_port)
		dmaengine_terminate_all(ts->rx_chan);

	misc_deregister(&ts->misc_dev);

	return 0;
}

static int ts_pm_suspend(struct device *dev)
{
	struct ts_dev *ts = dev_get_drvdata(dev);

	clk_disable_unprepare(ts->clk);

	return 0;
}

static int ts_pm_resume(struct device *dev)
{
	struct ts_dev *ts = dev_get_drvdata(dev);

	return clk_prepare_enable(ts->clk);
}

static const struct ts_portdata of_match_data[2] = {
	{.port_base = 0, .ops = &ts_usp_ops},
	{.port_base = 3, .ops = &ts_vip_ops},
};

static const struct of_device_id ts_of_match[] = {
	{ .compatible = "sirf,atlas7-usp_ts", .data = &of_match_data[0]},
	{ .compatible = "sirf,atlas7-vip_ts", .data = &of_match_data[1]},
	{}
};

static SIMPLE_DEV_PM_OPS(ts_pm_ops, ts_pm_suspend, ts_pm_resume);

static struct platform_driver ts_driver = {
	.driver		= {
		.name	= TS_DRV_NAME,
		.of_match_table = ts_of_match,
		.pm = &ts_pm_ops,
	},
	.probe		= ts_probe,
	.remove		= ts_remove,
};

module_platform_driver(ts_driver);

MODULE_DESCRIPTION("TS driver for SiRFAtlas7DA");
MODULE_LICENSE("GPL v2");
