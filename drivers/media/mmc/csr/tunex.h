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

#ifndef __RADIO_CSR_SDIO_H__
#define __RADIO_CSR_SDIO_H__

/* driver definitions */
#define DRIVER_NAME "radio-csr"

/* kernel includes */
#include "tx_message.h"
#include "tx_itf_data.h"

#define SD_IO_W_VENDOR				60

#define DATA_BUF_SIZE	(4*1024*1024)
#define MAX_BUF_COUNT	8
#define MAX_BUF_SIZE	0x80000

#define SOFT_RST_CMD (1 << 25)
#define SOFT_RST_DAT (1 << 26)

#define BUF0_READY	0x1
#define BUF1_READY	0x2
#define BUF0_ERR	0x1
#define BUF1_ERR	0x2
/*IOCTL numbers*/
#define RADIO_IO_MAGIC			'R'
#define IOCTL_RADIO_INIT		_IOR(RADIO_IO_MAGIC, 1, int)
#define IOCTL_RADIO_RW_CONFIG		_IOR(RADIO_IO_MAGIC, 2, int)
#define IOCTL_DATA_CONTROL		_IOR(RADIO_IO_MAGIC, 3, int)
#define IOCTL_GET_BUFFER_POINTER	_IOR(RADIO_IO_MAGIC, 4, int)
#define IOCTL_RELEASE_BUFFER		_IOR(RADIO_IO_MAGIC, 5, int)
#define IOCTL_GET_IN_POINTER		_IOR(RADIO_IO_MAGIC, 6, int)

enum dma_status_t {
	STOP = 0,
	START = 1,
};
struct csr_radio_sdio {
	struct sdio_func *func;
	struct sdhci_host *host;
	unsigned char *data_va_buf[2];
	void *loopdma_va_buf[2];
	u32 data_buf_size;
	u32 data_buf_count;
	struct device dev;
};

struct dma_config {
	enum dma_status_t dma_status;
	u32 dma_frames;
	u32 dma_length;
	u32 clock_rate;
	u32 dma_timeout;
	u32 cmd53addr;
	u32 timer_interval;
};

struct dma_buf_info {
	u32 buf_addr;
	u32 size;
};

struct buf_list {
	struct dma_buf_info buf_info;
	struct list_head list;
};

struct csr_radio {
	struct sdhci_sirf_priv *ss_sirfsoc;

	struct device *device;
	struct miscdevice misc_radio;
	spinlock_t lock;
	wait_queue_head_t	data_avail;
	struct tx_message *cfg_msg;
	struct tx_message *saved_msg;
	struct dma_config data_control;
	struct csr_radio_sdio radio_sdio;
	struct hrtimer	hrt;
	int tunex_num;
	dma_addr_t dma_addr;
	int dma_buf_size;
	unsigned int in;
	unsigned int out; /* save real out pointer when release_buf */
	/* save the last out pointer before release_buf,
	   the app may call get pointer twice and then
	   release buf */
	unsigned int pre_out;
	unsigned int buf_full;
	unsigned int buffer_ready;
};

extern int sdio_reset_comm(struct mmc_card *card);

#endif
