/*
 * Tunex Radio Driver for Linux
 * Copyright (c) 2014, 2015, 2016 The Linux Foundation. All rights reserved.
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

#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/dma-direction.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/miscdevice.h>
#include <linux/mmc/core.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdhci.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "../../../mmc/core/sdio_ops.h"
#include "../../../mmc/host/sdhci.h"
#include "../../../mmc/host/sdhci-pltfm.h"

#include "tunex.h"
#include "tx_regtrans.h"

#define DRV_NAME "tunex"

#define WAIT_DATA_TIMEOUT 1000
#define REG_TRANS_TIMEOUT 1000
#define LOOPDMA_BUF_SIZE (2048 * (1 << LOOPDMA_BUF_SIZE_SHIFT))

static void tunex_config_dma_on(struct csr_radio *radio);

/*
 * tunex_writel write 4 registers of function 1-7 by CMD60 each time
 * the register address increased by the last CMD52 address
 */
static int tunex_writel(struct sdio_func *func, u32 val)
{
	struct mmc_command cmd = {0};
	int err;

	BUG_ON(!func);

	cmd.opcode = SD_IO_W_VENDOR;
	cmd.arg = val;
	cmd.flags = MMC_RSP_SPI_R5 | MMC_RSP_R5 | MMC_CMD_AC;

	err = mmc_wait_for_cmd(func->card->host, &cmd, 0);
	if (err) {
		pr_err("%s: mmc_wait_for_cmd ret=%d\n", __func__, err);
		return err;
	}
	if (!mmc_host_is_spi(func->card->host)) {
		if (cmd.resp[0] & R5_ERROR) {
			pr_err("%s: resp has R5_ERROR\n", __func__);
			return -EIO;
		}
		if (cmd.resp[0] & R5_FUNCTION_NUMBER) {
			pr_err("%s: resp has R5_FUNCTION_NUMBER\n", __func__);
			return -EINVAL;
		}
		if (cmd.resp[0] & R5_OUT_OF_RANGE) {
			pr_err("%s: resp has R5_OUT_OF_RANGE\n", __func__);
			return -ERANGE;
		}
	}

	return 0;
}

/*
 * the tunex only support read/write registers by CMD52, can not use sdio_readw
 */
static u16 tunex_sdio_readw(struct sdio_func *func, unsigned int addr, int *err)
{
	int ret = 0;
	u16 reg_val;
	u8 val;

	val = sdio_readb(func, addr, &ret);
	if (ret) {
		if (err)
			*err = ret;
		return val;
	}
	reg_val = val;
	addr += 1;
	val = sdio_readb(func, addr, &ret);
	reg_val |= ((u16)val << 8);
	if (ret && err)
		*err = ret;

	return reg_val;
}

/*
 * the tunex only support read/write registers by CMD52, can not use sdio_writew
 */
static int tunex_sdio_writew(struct sdio_func *func, unsigned int addr, u16 val)
{
	int ret;
	u8 reg_val;

	reg_val = ((val >> 8) & 0xFF);
	sdio_writeb(func, reg_val, addr+1, &ret);
	if (ret)
		return ret;

	reg_val = val & 0xFF;
	sdio_writeb(func, reg_val, addr, &ret);
	if (ret)
		return ret;
	return 0;
}

static int
tunex_fn0_read(struct csr_radio *radio, unsigned int num,
		u16 *buf, int addr)
{
	int i;
	int ret;
	struct sdio_func *func = radio->radio_sdio.func;

	for (i = 0; i < num; i++) {
		buf[i] = (u16)sdio_f0_readb(func, addr + i, &ret);
		if (ret)
			return ret;
	}
	return 0;
}

static int
tunex_fn1_read(struct csr_radio *radio, unsigned int num,
		u16 *buf, int addr)
{
	int i, offset;
	int ret = 0;
	struct sdio_func *func = radio->radio_sdio.func;
	/* function 1 read order is LSB then MSB */
	for (i = 0; i < num; i++) {
		offset = i << 1;
		/* read even address first */
		buf[i] = tunex_sdio_readw(func, addr + offset, &ret);
		if (ret)
			return ret;
	}
	return 0;
}

static int
tunex_fn0_write(struct csr_radio *radio, unsigned int num,
		const u16 *buf, int addr)
{
	int i;
	int ret;
	u8 reg_byte;
	struct sdio_func *func = radio->radio_sdio.func;

	for (i = 0; i < num; i++) {
		reg_byte = (u8)buf[i];
		sdio_f0_writeb(func, reg_byte, addr+i, &ret);
		if (ret)
			return ret;
	}
	return 0;
}

static int
tunex_fn1_write(struct csr_radio *radio, unsigned int num,
		const u16 *buf, int addr)
{
	int i, offset;
	int ret;
	u32 reg_write32;
	struct sdio_func *func;
	struct device *dev;

	dev = radio->device;
	func = radio->radio_sdio.func;

	for (i = 0; i < num; i++) {
		offset = i << 1;
		/*
		 *  the CMD60 increase the pre MCD52 address to write
		 * we need write by CMD52 to set the write addressfirstly
		 * before CMD60
		 */
		if (i == 0)
			ret = tunex_sdio_writew(func, addr + offset, buf[i]);
		else {
			/* write 2 registers bt CMD60 */
			if ((num - 1) / 2 > 0 && i < (num - 1)) {
				reg_write32 = buf[i + 1];
				reg_write32 <<= 16;
				reg_write32 |= (u32)buf[i];
				i++;
				ret = tunex_writel(func, reg_write32);
			} else
				ret = tunex_sdio_writew(func,
						addr + offset,
						buf[i]);
		}
		if (ret)
			return ret;
	}
	return 0;
}

static int
tunex_config_data_write(struct csr_radio *radio, unsigned int num,
		int addr, const u16 *buf)
{
	int ret = 0;
	int i;
	u32 reg_write32;
	struct sdio_func *func = radio->radio_sdio.func;
	/*
	 * expect reg_count is even (valid only for writing),
	 * the CMD60 write use the last CMD52 address, write 2 16bits
	 * register firstly to set the register address
	 */
	ret = tunex_sdio_writew(func, addr, buf[0]);
	if (ret)
		return ret;
	ret = tunex_sdio_writew(func, addr, buf[1]);
	if (ret)
		return ret;
	for (i = 2; i < num; i += 2) {
		reg_write32 = buf[i + 1];
		reg_write32 <<= 16;
		reg_write32 |= (u32)buf[i];
		ret = tunex_writel(func, reg_write32);
		if (ret)
			return ret;
	}
	return ret;
}

static int
tunex_config_data_read(struct csr_radio *radio, unsigned int num,
		int addr, u16 *buf)
{
	int ret = 0;
	int i;
	struct sdio_func *func = radio->radio_sdio.func;

	for (i = 0; i < num; i++)
		buf[i] = tunex_sdio_readw(func, addr, &ret);

	return ret;
}

static int tunex_sdio_reinit(struct csr_radio *radio)
{
	struct mmc_host *host;
	struct sdio_func *func;
	int ret;

	host = radio->radio_sdio.host->mmc;
	func = radio->radio_sdio.func;

	sdio_reset_comm(host->card);
	sdio_claim_host(func);

	sdio_enable_func(func);
	ret = sdio_set_block_size(func, 512);
	sdio_release_host(func);

	return ret;
}

/*
 * there are 3 interface for the functin
 * 1. configure the start register address for next read/write
 * 2. enable the function
 * 3. reinit the tunex sdio
 */
static int tunex_get_params(struct csr_radio *radio,
		int id, struct tx_message_element *element,
		int *fn, int *addr)
{
	int ret = 0;
	struct sdio_func *func = radio->radio_sdio.func;

	switch (TX_MPID(id)) {
	case TX_MPID_REGT_ADDRESS:
		if (element->val.u16val[0] & (1 << 15)) {
			*fn = 0;
			*addr = element->val.u16val[1];
		} else {
			*fn = 1;
			*addr = ((u32)(element->val.u16val[0]&0x7) << 14) |
				(element->val.u16val[1] & 0x3FFE);
		}
		break;
	case TX_MPID_REGT_ENABLEFUNC:
		sdio_claim_host(func);
		ret = sdio_enable_func(func);
		sdio_release_host(func);
		if (id & TX_MFLAG_READ) {
			/* if read flag is set than return
			   status of sdio_enable_func back */
			element->val.i32val = (int)ret;
		}
		break;
	case TX_MPID_REGT_REINIT:
		ret = tunex_sdio_reinit(radio);
		if (id & TX_MFLAG_READ) {
			/* if read flag is set than return
			   status of radio_sdio_reinit back */
			element->val.i32val = (int)ret;
		}
		break;
	}
	return ret;
}

/*
 * The SDR software read/write registers by this function.
 * There are 2 types ID: value and ptr
 * it configure the register's address for next read/write if ID is vale
 * and there are 2 types register write:
 * 1. mix read/write for 8/16/32 bits (TX_MOID(id) is TX_MOID_REGT_DATA)
 * 2. write a large number of registers by CMD60 (TX_MOID_REGT_COEF)
 */
static void tunex_control_regs_rw(struct csr_radio *radio)
{
	int i;
	int count;
	struct tx_message *msg;
	struct tx_message_element *pelmt;
	int fn;
	u16 *buf;
	u32 id;
	unsigned int num;
	struct sdio_func *func;
	int addr;

	func = radio->radio_sdio.func;
	msg = radio->cfg_msg;
	count = msg->count;
	fn = 0;
	addr = 0;
	for (i = 0; i < count; i++) {
		id = msg->elements[i].id;
		pelmt = &msg->elements[i];
		if (!(id & TX_MFLAG_BYREF)) {
			tunex_get_params(radio, id, pelmt, &fn, &addr);
			continue;
		}
		sdio_claim_host(func);

		switch (TX_MOID(id)) {
		case TX_MOID_REGT_DATA:
			num = TX_MOSIZE(id) / 2;
			buf = (u16 *)(pelmt->val.ptr);
			if (id & TX_MFLAG_WRITE && fn)
				msg->error = tunex_fn1_write(radio, num,
						buf, addr);
			else if (id & TX_MFLAG_READ && fn)
				msg->error = tunex_fn1_read(radio, num,
						buf, addr);
			else if (id & TX_MFLAG_WRITE && fn == 0)
				msg->error = tunex_fn0_write(radio, num,
						buf, addr);
			else if (id & TX_MFLAG_READ && fn == 0)
				msg->error = tunex_fn0_read(radio, num,
						buf, addr);
			break;
		case TX_MOID_REGT_COEF:
			buf = (u16 *)(pelmt->val.ptr);
			if (id & TX_MFLAG_READ) {
				num = TX_MOSIZE(id) / 2;
				msg->error = tunex_config_data_read(radio,
						num, addr, buf);
			} else {
				num = (TX_MOSIZE(id) / 2) & (~0x1);
				msg->error = tunex_config_data_write(radio,
						num, addr, buf);
			}
			break;
		default:
			dev_err(radio->device, "register trans id err\n");
			break;
		}

		sdio_release_host(func);
		/* FIXME: sometimes there is no response when access multi tunex
		 * and the host will be reset
		 * we need restart the loop dma when it happened
		 */
		if (msg->error == -ETIMEDOUT &&
				radio->data_control.dma_status == START) {
			tunex_config_dma_on(radio);
			break;
		}
	}
}

static long
tunex_ioctl_rw_config(struct csr_radio *radio, unsigned long reg_msg)
{
	unsigned int size;
	unsigned int count;
	unsigned int id;
	int i;
	long ret;
	struct tx_message *msg;
	struct tx_message_element *pelmt;
	struct device *dev = radio->device;

	msg = (struct tx_message *)reg_msg;
	if (copy_from_user(&count, (void __user *)reg_msg, sizeof(count)))
		return -EINVAL;

	size = TX_MSGSIZE_MEM(count);
	radio->cfg_msg = kzalloc(size, GFP_KERNEL);
	if (!radio->cfg_msg)
		return -ENOMEM;

	radio->saved_msg = kzalloc(size, GFP_KERNEL);
	if (!radio->saved_msg) {
		kfree(radio->cfg_msg);
		return -ENOMEM;
	}

	if (copy_from_user(radio->cfg_msg, (void __user *)reg_msg, size)) {
		kfree(radio->cfg_msg);
		kfree(radio->saved_msg);
		return -EINVAL;
	}
	memcpy(radio->saved_msg, radio->cfg_msg, size);

	for (i = 0; i < count; i++) {
		id = radio->cfg_msg->elements[i].id;
		if (!(id & TX_MFLAG_BYREF))
			continue;

		pelmt = &radio->cfg_msg->elements[i];
		switch (TX_MOID(id)) {
		case TX_MOID_REGT_COEF:
		case TX_MOID_REGT_DATA:
			size = TX_MOSIZE(id);
			pelmt->val.ptr = kzalloc(size, GFP_KERNEL);
			if (copy_from_user(pelmt->val.ptr,
					(void __user *)
					radio->saved_msg->elements[i].val.ptr,
						size)) {
				ret = -EINVAL;
				goto free_memory;
			}
			break;
		default:
			dev_err(dev, "ignore id 0x%X\n", id);
			break;
		}
	}
	tunex_control_regs_rw(radio);
	radio->saved_msg->error = radio->cfg_msg->error;

	/* copy error status back to userspace */
	if (copy_to_user((void __user *)reg_msg, radio->saved_msg,
				sizeof(*(radio->saved_msg)))) {
		dev_err(dev, "[ERROR] copy_to_user failed (cp error status)\n");
		ret = -EINVAL;
		goto free_memory;
	}

	/* copy BYREF data to userspace */
	for (i = 0; i < count; i++) {
		id = radio->cfg_msg->elements[i].id;
		pelmt = &radio->saved_msg->elements[i];
		if (!(id & TX_MFLAG_BYREF))
			continue;
		if (id & TX_MFLAG_READ) {
			size = TX_MOSIZE(id);
			switch (TX_MOID(id)) {
			case TX_MOID_REGT_COEF:
			case TX_MOID_REGT_DATA:
				if (copy_to_user(
					(void __user *)pelmt->val.ptr,
					radio->cfg_msg->elements[i].val.ptr,
					size)) {
						ret = -EINVAL;
						goto free_memory;
					}
				break;
			default:
				break;
			}
		}
		kfree(radio->cfg_msg->elements[i].val.ptr);
	}
	kfree(radio->cfg_msg);
	kfree(radio->saved_msg);

	return 0;

free_memory:

	if (radio->cfg_msg) {
		/* free BYREF memory */
		for (i = 0; i < count; i++) {
			id = radio->cfg_msg->elements[i].id;
			pelmt = &radio->cfg_msg->elements[i];
			if (id & TX_MFLAG_BYREF) {
				switch (TX_MOID(id)) {
				case TX_MOID_REGT_COEF:
				case TX_MOID_REGT_DATA:
					kfree(pelmt->val.ptr);
				break;
				}
			}
		}
		kfree(radio->cfg_msg);
	}
	kfree(radio->saved_msg);
	return ret;
}

static enum hrtimer_restart tunex_hrtimer_callback(struct hrtimer *hrt)
{
	int size;
	unsigned int sys_addr;
	struct csr_radio *radio =
		container_of(hrt, struct csr_radio, hrt);
	dma_addr_t loopdma_buf = radio->ss_sirfsoc->loopdma_buf[0];
	struct sdhci_host *host = radio->radio_sdio.host;
	unsigned int intmask;
	unsigned int buffer_err;

	/* read current DMA address*/
	sys_addr = readl(host->ioaddr + 0);
	/* radio->in: last read offset pointer
	 * loopdma_buf: the start address of the DMA
	 * size: current avaible data size from last check
	 */
	size = sys_addr - loopdma_buf - radio->in;
	if (size < 0) {
		/*
		 * size < 0 means the current dma address
		 * over the end address of the DMA
		 */
		size = LOOPDMA_BUF_SIZE - radio->in;
		dma_sync_single_for_cpu(mmc_dev(host->mmc),
				loopdma_buf + radio->in,
				size,
				DMA_FROM_DEVICE);
		radio->in = 0;
	}
	size = sys_addr - loopdma_buf - radio->in;
	/* process the data by 512 Bytes block */
	if (size & ~0x1FF) {
		spin_lock(&radio->lock);
		size = size & ~0x1FF;
		dma_sync_single_for_cpu(mmc_dev(host->mmc),
				loopdma_buf + radio->in,
				size,
				DMA_FROM_DEVICE);
		radio->in += size;
		if (radio->in == LOOPDMA_BUF_SIZE)
			radio->in = 0;
		if (radio->in == radio->out)
			radio->buf_full = 1;
		spin_unlock(&radio->lock);
		wake_up(&(radio->data_avail));
	}
	intmask = readl(host->ioaddr + LOOPDMA_INT_STATUS);
	if (intmask & LOOPDMA_BUFF0_RDY_FLAG) {
		writel(LOOPDMA_BUFF0_RDY_FLAG,
				host->ioaddr + LOOPDMA_INT_STATUS);
	}
	if (intmask & LOOPDMA_BUFF1_RDY_FLAG) {
		writel(LOOPDMA_BUFF1_RDY_FLAG,
				host->ioaddr + LOOPDMA_INT_STATUS);
	}
	if (radio->buf_full)
		radio->buf_full = 0;

	buffer_err = 0;
	if (intmask & LOOPDMA_BUFF0_ERR_FLAG)
		buffer_err = BUF0_ERR;
	if (intmask & LOOPDMA_BUFF1_ERR_FLAG)
		buffer_err |= BUF1_ERR;
	if (buffer_err) {
		writel(intmask & (LOOPDMA_BUFF0_RDY_FLAG |
				LOOPDMA_BUFF1_RDY_FLAG |
				LOOPDMA_BUFF0_ERR_FLAG |
				LOOPDMA_BUFF1_ERR_FLAG),
				host->ioaddr + LOOPDMA_INT_STATUS);

		sdhci_writel(host, radio->ss_sirfsoc->loopdma_buf[0],
				SDHCI_DMA_ADDRESS);
		radio->buffer_ready = 0;
	}

	hrtimer_forward_now(hrt,
			ns_to_ktime(radio->data_control.timer_interval));

	return HRTIMER_RESTART;
}

static void tunex_dma_framecnt_wt(struct csr_radio *radio,
		struct tx_message_element *elmt)
{
	struct device *dev;

	dev = radio->device;
	if (elmt->val.u32val * 512 > MAX_BUF_SIZE) {
		elmt->id |= TX_MFLAG_ERROR;
		dev_err(dev, "config frames: dmaframecnt %d too big (max %d)\n",
				elmt->val.u32val, MAX_BUF_SIZE/512);
	} else {
		radio->data_control.dma_frames = elmt->val.u32val;
		radio->data_control.dma_length =
			radio->data_control.dma_frames * 512;
		dev_info(dev, "dma len: %d\n",
				radio->data_control.dma_length);
	}

}


static void tunex_config_dma_on(struct csr_radio *radio)
{
	struct sdio_func *func = radio->radio_sdio.func;

	sdio_claim_host(func);
	radio->in = 0;
	radio->out = 0;
	radio->pre_out = 0;
	radio->buf_full = 0;

	hrtimer_start(&radio->hrt,
			ns_to_ktime(radio->data_control.timer_interval),
			HRTIMER_MODE_REL);
	sdhci_writel(radio->radio_sdio.host,
			radio->ss_sirfsoc->loopdma_buf[0],
			SDHCI_DMA_ADDRESS);
	mmc_io_rw_extended(func->card,
			0,
			func->num,
			radio->data_control.cmd53addr,
			1,
			NULL,
			1024,
			512);
	radio->data_control.dma_status = START;
	sdio_release_host(func);
}

static void tunex_config_dma_off(struct csr_radio *radio)
{
	struct sdio_func *func = radio->radio_sdio.func;
	int rst_val;
	u8 abort;
	u8 ret;

	sdio_claim_host(func);
	hrtimer_cancel(&radio->hrt);
	ret = mmc_io_rw_direct(func->card,
			0,
			0,
			SDIO_CCCR_ABORT,
			0,
			&abort);
	if (ret)
		abort = func->num & 0x7;
	else
		abort = (abort & 0xF0) | (func->num & 0x7);
	ret = mmc_io_rw_direct(func->card,
			1,
			0,
			SDIO_CCCR_ABORT,
			abort,
			NULL);

	rst_val = readl(radio->radio_sdio.host->ioaddr + SDHCI_CLOCK_CONTROL);
	rst_val |= (SOFT_RST_CMD | SOFT_RST_DAT);
	writel(rst_val, radio->radio_sdio.host->ioaddr + SDHCI_CLOCK_CONTROL);
	radio->data_control.dma_status = STOP;
	sdio_release_host(func);
}

static long
tunex_ioctl_data_control(struct csr_radio *radio,
	       unsigned long data_msg)
{
	unsigned int size;
	unsigned int count;
	unsigned int id;
	int i;
	int ret = 0;
	struct tx_message *msg;
	struct device *dev;

	dev = radio->device;
	msg = (struct tx_message *)data_msg;
	if (copy_from_user(&count, (void __user *)data_msg,
				sizeof(unsigned int)))
		return -EINVAL;

	size = TX_MSGSIZE_MEM(count);

	msg = kmalloc(size, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	if (copy_from_user(msg, (void __user *)data_msg, size)) {
		ret = -EINVAL;
		goto err_exit;
	}

	for (i = 0; i < count; i++) {
		id = msg->elements[i].id;
		if (id & TX_MFLAG_BYREF) {
			msg->elements[i].id |= TX_MFLAG_ERROR;
			continue;
		}
		if (radio->data_control.dma_status != STOP &&
				TX_MPID(id) != TX_MPID_DATA_DMAACTIVE &&
				id & TX_MFLAG_WRITE) {
			msg->elements[i].id |= TX_MFLAG_ERROR;
			break;
		}
		switch (TX_MPID(id)) {
		case TX_MPID_DATA_DMAACTIVE:
			if ((id & TX_MFLAG_WRITE) &&
					msg->elements[i].val.i32val)
				tunex_config_dma_on(radio);
			else if ((id & TX_MFLAG_WRITE) &&
					!msg->elements[i].val.i32val)
				tunex_config_dma_off(radio);

			if (id & TX_MFLAG_READ) {
				msg->elements[i].val.i32val =
					radio->data_control.dma_status;
			}
			break;
		case TX_MPID_DATA_DMAFRAMECNT:
			if (id & TX_MFLAG_WRITE) {
				tunex_dma_framecnt_wt(radio,
						&msg->elements[i]);
			}
			if (id & TX_MFLAG_READ) {
				msg->elements[i].val.u32val =
					radio->data_control.dma_frames;
			}
			break;
		case TX_MPID_DATA_DMATIMEOUT:
			if (id & TX_MFLAG_WRITE) {
				radio->data_control.dma_timeout =
					msg->elements[i].val.u32val;
			}
			if (id & TX_MFLAG_READ) {
				msg->elements[i].val.u32val =
					radio->data_control.dma_timeout;
			}
			break;
		case TX_MPID_DATA_CLOCKRATE:
			if (id & TX_MFLAG_WRITE) {
				radio->data_control.clock_rate =
					msg->elements[i].val.u32val;
			}
			if (id & TX_MFLAG_READ) {
				msg->elements[i].val.u32val =
					radio->data_control.clock_rate;
			}
			break;
		case TX_MPID_DATA_CMD53_ADDR:
			if (id & TX_MFLAG_WRITE) {
				radio->data_control.cmd53addr =
					msg->elements[i].val.u32val;
			}
			if (id & TX_MFLAG_READ) {
				msg->elements[i].val.u32val =
					radio->data_control.cmd53addr;
			}
			break;
		case TX_MPID_DATA_TIMER_INTVL:
			if (id & TX_MFLAG_WRITE) {
				radio->data_control.timer_interval =
					msg->elements[i].val.u32val;
			}
			if (id & TX_MFLAG_READ) {
				msg->elements[i].val.u32val =
					radio->data_control.timer_interval;
			}
			break;
		default:
			msg->elements[i].id |= TX_MFLAG_ERROR;
			break;
		}
	}

	if (copy_to_user((void __user *)data_msg, msg, size))
		ret = -EINVAL;

err_exit:
	kfree(msg);
	return ret;
}

static long
tunex_ioctl_get_buf_pointer(struct csr_radio *radio, unsigned long arg)
{
	long wait;
	struct dma_buf_info buf_info;
	struct device *dev;
	int size;

	dev = radio->device;

	wait = wait_event_interruptible_timeout(
			radio->data_avail,
			radio->in != radio->pre_out || radio->buf_full,
			WAIT_DATA_TIMEOUT);
	if (likely(wait > 0)) {
		buf_info.buf_addr =  radio->pre_out;
		if (radio->in > radio->pre_out) {
			size = radio->in - radio->pre_out;
			radio->pre_out = radio->pre_out + size;
		} else {
			/* ring-buffer loops to the start */
			size = LOOPDMA_BUF_SIZE - radio->pre_out;
			radio->pre_out = 0;
		}
		buf_info.size = size;
		if (copy_to_user((void __user *)arg,
					&buf_info,
					sizeof(struct dma_buf_info)))
			return  -EINVAL;
	} else if (!wait) {
		dev_err(dev, "ioctl_get_buf_addr timeout\n");
		return -ETIMEDOUT;
	} else
		return wait;
	return 0;
}

static long
tunex_ioctl_release_buf(struct csr_radio *radio,
		unsigned long size)
{
#if 0
	unsigned long flags;
	struct sdhci_host *host = radio->radio_sdio.host;
	spin_lock_irqsave(&radio->lock, flags);
	radio->out += size;
	spin_unlock_irqrestore(&radio->lock, flags);
#endif
	return 0;
}

static long tunex_ioctl_get_in_buf(struct csr_radio *radio)
{
	if (radio->data_control.dma_status != START)
		return -EINVAL;

	return radio->in;
}

static int tunex_fops_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int tunex_fops_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct csr_radio *radio = container_of(filp->private_data,
			struct csr_radio, misc_radio);

	vma->vm_end = vma->vm_start + radio->dma_buf_size;
	if (remap_pfn_range(vma, vma->vm_start, radio->dma_addr >> PAGE_SHIFT,
				vma->vm_end - vma->vm_start,
				vma->vm_page_prot)) {
		return -EAGAIN;
	}
	return 0;
}

static long
tunex_fops_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long ret;
	struct csr_radio *radio;

	radio = container_of(filp->private_data, struct csr_radio,
			misc_radio);

	if (!radio->radio_sdio.func)
		return -ENODEV;

	switch (cmd) {
	case IOCTL_RADIO_INIT:
		ret = tunex_sdio_reinit(radio);
		break;
	case IOCTL_RADIO_RW_CONFIG:
		ret = tunex_ioctl_rw_config(radio, arg);
		break;
	case IOCTL_DATA_CONTROL:
		ret = tunex_ioctl_data_control(radio, arg);
		break;
	case IOCTL_GET_BUFFER_POINTER:
		ret = tunex_ioctl_get_buf_pointer(radio, arg);
		break;
	case IOCTL_RELEASE_BUFFER:
		ret = tunex_ioctl_release_buf(radio, arg);
		break;
	case IOCTL_GET_IN_POINTER:
		ret =  tunex_ioctl_get_in_buf(radio);
		break;
	default:
		dev_err(radio->device, "Unsupport ioctl %x\n", cmd);
		return -EINVAL;
	}
	return ret;
}

static const struct file_operations tunex_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = tunex_fops_ioctl,
	.mmap = tunex_fops_mmap,
	.open = tunex_fops_open,
};

static const struct sdio_device_id tunex_sdio_devices[] = {
	{ SDIO_DEVICE(0x032A, 0x0020) },
	{ },
};

MODULE_DEVICE_TABLE(sdio, tunex_sdio_devices);

static int tunex_sdio_probe(struct sdio_func *func,
		const struct sdio_device_id *id)
{
	int ret;
	struct csr_radio *radio;
	struct mmc_card *card;
	struct mmc_host *host;
	struct device *dev;
	struct platform_device *pdev;
	struct sdhci_host *shost;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_sirf_priv *priv;
	struct mmc_context_info *context_info;
	char *dev_name;

	card = func->card;
	host = card->host;
	dev = host->parent;
	pdev = to_platform_device(dev);
	shost = platform_get_drvdata(pdev);
	context_info = &host->context_info;

	radio = devm_kzalloc(&pdev->dev, sizeof(*radio), GFP_KERNEL);
	if (!radio)
		return -ENOMEM;
	dev_name = devm_kzalloc(&pdev->dev, 16, GFP_KERNEL);
	if (!dev_name)
		return -ENOMEM;
	/* check the tunex device number. The Atlas7 only support up to 2
	 * devices, and set the device name to tunex1 for first device
	 */

	radio->tunex_num = of_alias_get_id(pdev->dev.of_node, "tunex");
	radio->device = &pdev->dev;
	pltfm_host = sdhci_priv(shost);
	priv = pltfm_host->priv;
	radio->ss_sirfsoc = priv;

	radio->radio_sdio.dev = *dev;

	spin_lock_init(&radio->lock);

	init_waitqueue_head(&radio->data_avail);

	radio->radio_sdio.func = func;
	radio->radio_sdio.host = shost;

	sdio_claim_host(func);
	sdio_enable_func(func);
	ret = sdio_set_block_size(func, 512);
	sdio_release_host(func);

	radio->data_control.cmd53addr = 0;
	sdio_set_drvdata(func, radio);

	radio->data_control.dma_length = 0x1000;
	radio->data_control.dma_frames = 4;

	if (priv->loopdma) {
		radio->dma_addr = priv->loopdma_buf[0];
		radio->dma_buf_size = LOOPDMA_BUF_SIZE;
		radio->data_control.dma_status = STOP;
		priv->lpdma_buf_sft = 10;
		radio->data_control.timer_interval = 20000000;

		hrtimer_init(&radio->hrt,
				CLOCK_MONOTONIC,
				HRTIMER_MODE_REL);
		radio->hrt.function = tunex_hrtimer_callback;
	} else {
		dev_err(dev, "csrradio: host do not support loop dma!\n");
		return -ENODEV;
	}

	snprintf(dev_name, 16, "%s%d", DRV_NAME, radio->tunex_num);
	radio->misc_radio.name = dev_name;
	radio->misc_radio.fops = &tunex_fops;
	radio->misc_radio.minor = MISC_DYNAMIC_MINOR;
	radio->misc_radio.parent = &pdev->dev;
	ret = misc_register(&radio->misc_radio);
	if (unlikely(ret)) {
		dev_err(&pdev->dev, "misc register fail\n");
		return ret;
	}

	return 0;
}

static void tunex_sdio_remove(struct sdio_func *func)
{
	struct csr_radio *radio;
	struct device *dev;
	struct mmc_host *host;
	struct mmc_context_info *context_info;
	int ret;

	host = func->card->host;
	context_info = &host->context_info;

	radio = sdio_get_drvdata(func);
	dev = radio->device;
	ret = misc_deregister(&radio->misc_radio);
	if (ret)
		dev_err(dev, "misc_deregiser fail %d\n", ret);
	radio->data_control.dma_status = STOP;

	sdio_claim_host(func);
	sdio_disable_func(func);
	sdio_release_host(func);
}

#ifdef CONFIG_PM_SLEEP
/* the card will be removed by mmc core if suspend is NULL */

static int tunex_sdio_suspend(struct device *dev)
{
	return 0;
}

static int tunex_sdio_resume(struct device *dev)
{
	/* app will re-set the modes after resuming,
	 * so here driver does nothing
	 */
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(tunex_sdio_pm_ops, tunex_sdio_suspend,
		tunex_sdio_resume);

static struct sdio_driver tunex_sdio_driver = {
	.name = "tunex_sdio",
	.id_table = tunex_sdio_devices,
	.probe = tunex_sdio_probe,
	.remove = tunex_sdio_remove,
	.drv = {
		.owner = THIS_MODULE,
		.pm = &tunex_sdio_pm_ops,
	}
};

static int __init tunex_sdio_init(void)
{
	return sdio_register_driver(&tunex_sdio_driver);
}
module_init(tunex_sdio_init);

static void __exit tunex_sdio_exit(void)
{
	sdio_unregister_driver(&tunex_sdio_driver);
}

module_exit(tunex_sdio_exit);

MODULE_DESCRIPTION("Driver support for CSR SDIO Radio");
MODULE_LICENSE("GPLv2");
