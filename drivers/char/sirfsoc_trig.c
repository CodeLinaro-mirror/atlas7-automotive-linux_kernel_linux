/*
 * CSR TriG SDIO GPS driver
 *
 * Copyright (c) 2013-2014, 2016, The Linux Foundation. All rights reserved.
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

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/debugfs.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdhci.h>
#include <linux/platform_device.h>
#include <linux/semaphore.h>
#include <linux/kthread.h>
#include <linux/memblock.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/suspend.h>

#include "../mmc/core/sdio_ops.h"
#include "../mmc/host/sdhci.h"
#include "../mmc/host/sdhci-pltfm.h"

#include "sirfsoc_gpsdrv.h"

static struct trig_dev trigdev;
const unsigned int sg_config_p2_nco_value[14] = {
	P2_GLO_NCO_G10,
	P2_GLO_NCO_G03,
	P2_GLO_NCO_G04,
	P2_GLO_NCO_G02,
	P2_GLO_NCO_G18,
	P2_GLO_NCO_G09,
	P2_GLO_NCO_G12,
	P2_GLO_NCO_G11,
	P2_GLO_NCO_G01,
	P2_GLO_NCO_G20,
	P2_GLO_NCO_G19,
	P2_GLO_NCO_G17,
	P2_GLO_NCO_G07,
	P2_GLO_NCO_G08
};

DECLARE_COMPLETION(sg_evt_msg_ready);
DECLARE_COMPLETION(sg_evt_trig_exited);
DECLARE_COMPLETION(sg_evt_card_int);

static int
sirf_trig_probe(struct sdio_func *func, const struct sdio_device_id *id);
static void sirf_trig_remove(struct sdio_func *func);
static long
trig_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
static int trig_mmap(struct file *file, struct vm_area_struct *vma);
static int trig_open(struct inode *inode, struct file *filp);
static int trig_release(struct inode *inode, struct file *filp);

static const struct file_operations trig_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = trig_ioctl,
	.mmap = trig_mmap,
	.open = trig_open,
	.release = trig_release,

};

static phys_addr_t sirf_pbb_phy_base;
static phys_addr_t sirf_pbb_phy_size;

void __init sirfsoc_pbb_reserve_memblock(void)
{
	sirf_pbb_phy_size = SZ_1M;
	sirf_pbb_phy_base = memblock_alloc(sirf_pbb_phy_size, SZ_1M);
	memblock_remove(sirf_pbb_phy_base, sirf_pbb_phy_size);
}
EXPORT_SYMBOL(sirfsoc_pbb_reserve_memblock);

void __init sirfsoc_pbb_nosave_memblock(void)
{
	register_nosave_region_late(
		__phys_to_pfn(sirf_pbb_phy_base),
		__phys_to_pfn(sirf_pbb_phy_base +
			sirf_pbb_phy_size));
}
EXPORT_SYMBOL(sirfsoc_pbb_nosave_memblock);

static int trig_readl(struct sdio_func *func, int addr, int *err_ret)
{
	int i;
	unsigned int value = 0;

	for (i = 0; i < 4; i++)
		value |= (sdio_readb(func, addr + i, err_ret) << (8 * i));
	return value;
}

static void trig_writel(struct sdio_func *func, int addr, unsigned int value,
								int *err_ret)
{
	int i;
	for (i = 3; i >= 0; i--)
		sdio_writeb(func, (value >> (8 * i)) & 0xFF, addr + i, err_ret);
}

static int trig_reg_init(struct sdio_func *func, int addr, unsigned int value)
{
	unsigned int ret_value;
	int ret;

	trig_writel(func, addr, value, &ret);
	if (ret) {
		dev_info(&trigdev.ss_trig_sdio->dev, "Failed to write\r\n");
		return ret;
	}
	ret_value = trig_readl(func, addr, &ret);
	if (ret) {
		dev_info(&trigdev.ss_trig_sdio->dev, "Failed to read\r\n");
		return ret;
	}

	if (ret_value != value) {
		dev_info(&trigdev.ss_trig_sdio->dev,
			"Failed to write 0x%x 0x%x 0x%x\n",
			value, addr, ret_value);
		return -EIO;
	}
	return 0;
}

static int trig_ana_init(struct sdio_func *func, unsigned int trig_mode)
{
	int i;
	unsigned int ana_init_data_glo[] = {
		/*TRIG_CTRL, 0x0000000C,*/
		TRIG_INT_EN_MASK, 0x0000003C,
#if defined(TRIG_FIRST_VERSION)
		TRIG_GNSS_ANA_CTRL_BIAS, 0x0B5BDE50,
		TRIG_GNSS_ANA_CTRL_CLK_A, 0x234E040,
		/*TRIG_GNSS_ANA_CTRL_TESTA, 0x00100000,*/
		/*TRIG_GNSS_ANA_CTRL_TESTB, 0x0083C020,*/
		TRIG_GNSS_ANA_CTRL_MIXER, 0xD048ADE4,
		TRIG_GNSS_ANA_CTRL_GPS_AGC, 0xA0,
#endif
		/*TRIG_GNSS_ANA_CTRL_CSM, 0x00400410,*/
		TRIG_GNSS_ANA_CTRL_POWER_ENABLES, 0x7ffffff0,
		TRIG_GNSS_ANA_CTRL_CLK_A, 0x023ce040,
		TRIG_GNSS_ANA_CTRL_MIXER, 0xf0488de4,
		TRIG_GNSS_ANA_CTRL_ALL_DCOC, 0x00000030,
		TRIG_GNSS_ANA_CTRL_GLO_DCOC, 0x80800000,
		TRIG_GNSS_ANA_CTRL_GLO_DCOC, 0x8c800000,

#if defined(TRIG_FIRST_VERSION)
		TRIG_GNSS_ANA_CTRL_VCO, 0x00000040,
#endif

#if defined(TRIG_FIRST_VERSION)
		TRIG_GNSS_ANA_CTRL_RFCAL, 0x3D501FB5,
		TRIG_GNSS_ANA_CTRL_RFCAL, 0x7D501FB5
#endif
	};
	unsigned int ana_init_data_comp[] = {
		/*TRIG_CTRL, 0x0000000C,*/
		TRIG_INT_EN_MASK, 0x0000003C,
#if defined(TRIG_FIRST_VERSION)
		TRIG_GNSS_ANA_CTRL_BIAS, 0x0B5BDE50,
		TRIG_GNSS_ANA_CTRL_CLK_A, 0x234E040,
		/*TRIG_GNSS_ANA_CTRL_TESTA, 0x00100000,*/
		/*TRIG_GNSS_ANA_CTRL_TESTB, 0x0083C020,*/
		TRIG_GNSS_ANA_CTRL_MIXER, 0xD048ADE4,
		TRIG_GNSS_ANA_CTRL_GPS_AGC, 0xA0,
#endif
		TRIG_GNSS_ANA_CTRL_POWER_ENABLES, 0x7ffffff0,
		TRIG_GNSS_ANA_CTRL_CLK_A, 0x123ce040,
		TRIG_GNSS_ANA_CTRL_MIXER, 0xf0498de4,
		TRIG_GNSS_ANA_CTRL_ALL_DCOC, 0x00000020,
		TRIG_GNSS_ANA_CTRL_GLO_DCOC, 0x80800000,
		TRIG_GNSS_ANA_CTRL_GLO_DCOC, 0x8c800000,

#if defined(TRIG_FIRST_VERSION)
		TRIG_GNSS_ANA_CTRL_VCO, 0x00000040,
#endif

#if defined(TRIG_FIRST_VERSION)
		TRIG_GNSS_ANA_CTRL_RFCAL, 0x3D501FB5,
		TRIG_GNSS_ANA_CTRL_RFCAL, 0x7D501FB5
#endif
	};
	int item_num;
	unsigned int *p_ana_init_data;
	unsigned int val1;
	if (2 == trig_mode) {
		p_ana_init_data = &ana_init_data_comp[0];
		item_num = sizeof(ana_init_data_comp) /
			sizeof(ana_init_data_comp[0]) / 2;
	} else {
		p_ana_init_data = &ana_init_data_glo[0];
		item_num = sizeof(ana_init_data_glo) /
			sizeof(ana_init_data_glo[0]) / 2;
	}
	val1 = 0x00400410;
	if (trig_reg_init(func, TRIG_GNSS_ANA_CTRL_CSM, val1))
		dev_info(&trigdev.ss_trig_sdio->dev,
			"failed to write TRIG_GNSS_ANA_CTRL_CSM\r\n");
	else
		dev_info(&trigdev.ss_trig_sdio->dev,
			"Write reg success:TRIG_GNSS_ANA_CTRL_CSM.\r\n");
	/*wait for CAL_DONE or >48ms*/
	msleep(50);
	/*Configure the TriG ISP registers with intialization values*/
	for (i = 0; i < item_num; i++) {
		if (trig_reg_init(func, p_ana_init_data[i * 2],
			p_ana_init_data[i * 2 + 1]))
			dev_info(&trigdev.ss_trig_sdio->dev,
				"failed to initTriGReg\r\n");
	}
	return 1;
}

static int trig_isp_init(struct sdio_func *func, unsigned int trig_mode)
{
	int i;
	unsigned int isp_init_data_glo[] = {
#if 0
		ISP_GPS_LPF_CFG, 0x00000000,
		ISP_LO_AGILITY_CFG, 0x00000600,
		ISP_ADC_CFG, 0x00000080,
		ISP_ADC_CFG, 0x00000000,
		ISP_AGC_0_CFG_A, 0x038417fa,
		ISP_AGC_0_CFG_B, 0x00000007,
		ISP_AGC_0_GAIN_CFG, 0x0000001f,
		ISP_AGC_1_CFG_A, 0x000017fa,
		ISP_AGC_1_CFG_B, 0x00000007,
		ISP_AGC_1_GAIN_CFG, 0x0000001f,
		ISP_FE_STAGGER_CFG, 0x00000000,
		ISP_BE_STAGGER_CFG, 0x00000000,
#endif
#if defined ACTIVE_BLANKING
		ISP_BLANK_0_CFG, 0x00000002,
		ISP_ACTIVE_BLANK_0_CFG, 0x01f08600,
		ISP_BLANK_1_CFG, 0x00000002,
		ISP_ACTIVE_BLANK_1_CFG, 0x01f08600,
#endif
#if 0
		ISP_TC_0_LOOPS_CFG, 0x000006a0,
		ISP_TC_0_FREQ_CFG, 0x000318ca,
		ISP_TC_0_GAIN_CFG, 0x000f5c7f,
		ISP_TC_1_LOOPS_CFG, 0x00017a20,
		ISP_TC_1_FREQ_CFG, 0x00977638,
		ISP_TC_1_GAIN_CFG, 0x000f5c7f,
		ISP_TC_2_LOOPS_CFG, 0x00000000,
		ISP_TC_2_FREQ_CFG, 0x00000000,
		ISP_TC_2_GAIN_CFG, 0x00000000,
		ISP_TC_3_LOOPS_CFG, 0x00000000,
		ISP_TC_3_FREQ_CFG, 0x00000000,
		ISP_TC_3_GAIN_CFG, 0x00000000,
		ISP_TC_4_LOOPS_CFG, 0x00000000,
		ISP_TC_4_FREQ_CFG, 0x00000000,
		ISP_TC_4_GAIN_CFG, 0x00000000,
		ISP_TC_5_LOOPS_CFG, 0x00000000,
		ISP_TC_5_FREQ_CFG, 0x00000000,
		ISP_TC_5_GAIN_CFG, 0x00000000,
		ISP_GPS_QUANT_CFG_A, 0x115584cd,
		ISP_GPS_QUANT_CFG_B, 0x00000011,
		ISP_GLO_QUANT_CFG_A, 0x0f6184cd,
		ISP_GLO_QUANT_CFG_B, 0x00000011,
		ISP_SPEC_LDR_0_QUANT_CFG_A, 0x278d04cd,
		ISP_SPEC_LDR_0_QUANT_CFG_B, 0x00000011,
		ISP_SPEC_LDR_1_QUANT_CFG_A, 0x4364f9704cd,
		ISP_SPEC_LDR_1_QUANT_CFG_B, 0x00000011,
#endif
		ISP_FE_ON_OFF_CTL, 0x0000ffff,
		/*enable SCAN and MONITOR*/
		ISP_BE_ON_OFF_CTL, 0x00000030,
#if 0
		ISP_BE_ON_OFF_CTL, ISP_BE_ON_OFF_CTL_NONE_VALUE,
		ISP_BE_ON_OFF_CTL, 0x00000072,
		ISP_BE_ON_OFF_CTL, 0x02A0007A,
		ISP_BE_ON_OFF_CTL, 0x0AA0007A,
		ISP_P2_GPS_QUANT_CFG_A, 0x3dc484cd,
		ISP_P2_GPS_QUANT_CFG_B, 0x00000011,
		ISP_P2_GLO_QUANT_CFG_A, 0x246084cd,
		ISP_P2_GLO_QUANT_CFG_B, 0x00000011,
		ISP_P2_CONTROL,  ISP_P2_CONTROL_DEFAULT_VALUE_141_US,
		ISP_CWREM_CFG, 0x00000001
#endif
	};
	unsigned int isp_init_data_comp[] = {
		ISP_FE_ON_OFF_CTL, 0x0002ffff,
		ISP_P2_GLO_QUANT_CFG_A, 0x2B3684cd
	};
	int item_num;
	unsigned int *p_isp_init_data;
	if (2 == trig_mode) {
		item_num = sizeof(isp_init_data_comp) /
			sizeof(isp_init_data_comp[0]) / 2;
		p_isp_init_data = &isp_init_data_comp[0];
	} else {
		item_num = sizeof(isp_init_data_glo) /
			sizeof(isp_init_data_glo[0]) / 2;
		p_isp_init_data = &isp_init_data_glo[0];
	}
	for (i = 0; i < item_num; i++) {
		if (trig_reg_init(func, p_isp_init_data[i * 2],
			p_isp_init_data[i * 2 + 1]))
			dev_info(&trigdev.ss_trig_sdio->dev,
				"failed to initTriGReg\r\n");
	}
	return 1;
}

static void release_dma_buffer(unsigned int buf_id)
{
	unsigned int intmask;
	/*pr_info("Enter release_dma_buffer(%d)...\r\n", buf_id);*/
	if (0 == buf_id) {
		writel(LOOPDMA_BUFF0_RDY_FLAG,
			trigdev.ss_trig_sdio->host->ioaddr +
			LOOPDMA_INT_STATUS);

		intmask = readl(trigdev.ss_trig_sdio->host->ioaddr +
				LOOPDMA_INT_STATUS);

		if (intmask & LOOPDMA_BUFF0_ERR_FLAG) {
			dev_info(&trigdev.ss_trig_sdio->dev,
				"release_dma_buffer when buffer_err[0]\n");
			writel(LOOPDMA_BUFF0_ERR_FLAG,
				trigdev.ss_trig_sdio->host->ioaddr +
				LOOPDMA_INT_STATUS);
			sdhci_writel(trigdev.ss_trig_sdio->host,
					trigdev.ss_sirfsoc->loopdma_buf[0],
					SDHCI_DMA_ADDRESS);
		}
	} else {
		writel(LOOPDMA_BUFF1_RDY_FLAG,
			trigdev.ss_trig_sdio->host->ioaddr +
			LOOPDMA_INT_STATUS);

		intmask = readl(trigdev.ss_trig_sdio->host->ioaddr +
				LOOPDMA_INT_STATUS);

		if (intmask & LOOPDMA_BUFF1_ERR_FLAG) {
			dev_info(&trigdev.ss_trig_sdio->dev,
				"release_dma_buffer when buffer_err[1]\n");
			writel(LOOPDMA_BUFF1_ERR_FLAG,
				trigdev.ss_trig_sdio->host->ioaddr +
				LOOPDMA_INT_STATUS);
			sdhci_writel(trigdev.ss_trig_sdio->host,
					trigdev.ss_sirfsoc->loopdma_buf[1],
					SDHCI_DMA_ADDRESS);
		}
	}
	return;
}

static int trig_int_thread(void *data)
{
	unsigned int cur_buf_id;
	static int counter;
	unsigned int intmask;

	struct sched_param param = {
		.sched_priority = 14
	};
	sched_setscheduler(current, SCHED_FIFO, &param);
	counter = 0;
	trigdev.dma_to_user_counter = 0;
	trigdev.cur_gps_msg = &trigdev.msg_buf[0];
	dev_info(&trigdev.ss_trig_sdio->dev,
		"enter trig_int_thread already!!!!!!!!\n");
	while (1) {
		sdio_dma_int_handler();
		/*pr_info("trig get sdio dma int!!!!!!!!\n");*/
		if (1 == trigdev.thread_exit) {
			dev_info(&trigdev.ss_trig_sdio->dev,
				"trigdev.trigintthread exit now\r\n");
			complete(&sg_evt_trig_exited);
			break;
		}

		trigdev.cur_gps_msg->rtc_tick =
			sirfsoc_rtc_iobrg_readl(trigdev.gps_rtc_base);

		if (trigdev.ss_sirfsoc->buffer_crc_err) {
			dev_info(&trigdev.ss_trig_sdio->dev,
				"trig get crc error!!!!!!!!\r\n");
			trigdev.ss_sirfsoc->buffer_crc_err = 0;
			/*continue;*/
		}
		if (!trigdev.ss_sirfsoc->buffer_dma_int) {
			dev_info(&trigdev.ss_trig_sdio->dev,
				"not trig loopdma int!\r\n");
			continue;
		}
		trigdev.ss_sirfsoc->buffer_dma_int = 0;

		intmask = readl(trigdev.ss_trig_sdio->host->ioaddr +
			LOOPDMA_INT_STATUS);
		if (intmask & LOOPDMA_BUFF0_RDY_FLAG)
			trigdev.ss_sirfsoc->buffer_ready[0] = 1;
		else
			trigdev.ss_sirfsoc->buffer_ready[0] = 0;
		if (intmask & LOOPDMA_BUFF1_RDY_FLAG)
			trigdev.ss_sirfsoc->buffer_ready[1] = 1;
		else
			trigdev.ss_sirfsoc->buffer_ready[1] = 0;
		if (intmask & LOOPDMA_BUFF0_ERR_FLAG)
			trigdev.ss_sirfsoc->buffer_err[0] = 1;
		else
			trigdev.ss_sirfsoc->buffer_err[0] = 0;
		if (intmask & LOOPDMA_BUFF1_ERR_FLAG)
			trigdev.ss_sirfsoc->buffer_err[1] = 1;
		else
			trigdev.ss_sirfsoc->buffer_err[1] = 0;
#if 0
		/* print out the counter for debugging */
		int tmp_counter;
		tmp_counter++;
		if (tmp_counter == 1) {
			tmp_counter = 0;
			pr_debug("trig interrupt coming!");
			pr_debug("ready[0]:%d,ready[1]%d,\n"
				trigdev.ss_sirfsoc->buffer_ready[0],
				trigdev.ss_sirfsoc->buffer_ready[1]);
			pr_debug("error[0]:%d,error[1]:%d\n",
				trigdev.ss_sirfsoc->buffer_err[0],
				trigdev.ss_sirfsoc->buffer_err[1]);
		}
#endif
		/* get loop dma buffer
		* cur_buf_id set to 2 to indicate an unused value */
		cur_buf_id = 2;

		if (!trigdev.ss_sirfsoc->buffer_ready[0] &&
			!trigdev.ss_sirfsoc->buffer_ready[1])
			dev_info(&trigdev.ss_trig_sdio->dev,
				"TriG: both buffer not ready\n");

		if (trigdev.ss_sirfsoc->buffer_ready[0] &&
			!trigdev.ss_sirfsoc->buffer_ready[1]) {
			dma_sync_single_for_cpu(
				mmc_dev(trigdev.ss_trig_sdio->host->mmc),
					trigdev.ss_sirfsoc->loopdma_buf[0],
					512 * (1 << LOOPDMA_BUF_SIZE_SHIFT),
					DMA_FROM_DEVICE);
			trigdev.cur_gps_msg->buf_adrs =
				(unsigned int)
				trigdev.ss_trig_sdio->loopdma_va_buf[0];
			cur_buf_id = 0;

		}

		if (!trigdev.ss_sirfsoc->buffer_ready[0] &&
			trigdev.ss_sirfsoc->buffer_ready[1]) {
			dma_sync_single_for_cpu(
				mmc_dev(trigdev.ss_trig_sdio->host->mmc),
					trigdev.ss_sirfsoc->loopdma_buf[1],
					512 * (1 << LOOPDMA_BUF_SIZE_SHIFT),
					DMA_FROM_DEVICE);
			trigdev.cur_gps_msg->buf_adrs =
				(unsigned int)
				trigdev.ss_trig_sdio->loopdma_va_buf[1];
			cur_buf_id = 1;
		}

		if (trigdev.ss_sirfsoc->buffer_ready[0] &&
			trigdev.ss_sirfsoc->buffer_ready[1]) {
			if (trigdev.ss_sirfsoc->buffer_err[0] &&
					!trigdev.ss_sirfsoc->buffer_err[1]) {
				dev_info(&trigdev.ss_trig_sdio->dev,
					"TriG: buf0_err marked!\n");
				if (trigdev.dma_to_user_counter == 0) {
					writel(LOOPDMA_BUFF0_ERR_FLAG,
					trigdev.ss_trig_sdio->host->ioaddr +
					LOOPDMA_INT_STATUS);
					trigdev.ss_sirfsoc->buffer_err[0] = 0;

					writel(LOOPDMA_BUFF0_RDY_FLAG,
					trigdev.ss_trig_sdio->host->ioaddr +
					LOOPDMA_INT_STATUS);

					trigdev.ss_sirfsoc->buffer_ready[0]
					= 0;

					sdhci_writel(trigdev.ss_trig_sdio->host
					, trigdev.ss_sirfsoc->loopdma_buf[0],
					SDHCI_DMA_ADDRESS);
				}
				writel(LOOPDMA_BUFF1_RDY_FLAG,
					trigdev.ss_trig_sdio->host->ioaddr +
					LOOPDMA_INT_STATUS);
				trigdev.ss_sirfsoc->buffer_ready[1] = 0;
			} else if (!trigdev.ss_sirfsoc->buffer_err[0] &&
					trigdev.ss_sirfsoc->buffer_err[1]) {
				dev_info(&trigdev.ss_trig_sdio->dev,
					"TriG: buf1_err marked!\n");
				if (trigdev.dma_to_user_counter == 0) {
					writel(LOOPDMA_BUFF1_ERR_FLAG,
					trigdev.ss_trig_sdio->host->ioaddr +
					LOOPDMA_INT_STATUS);
					trigdev.ss_sirfsoc->buffer_err[1] = 0;

					writel(LOOPDMA_BUFF1_RDY_FLAG,
					trigdev.ss_trig_sdio->host->ioaddr +
					LOOPDMA_INT_STATUS);

					trigdev.ss_sirfsoc->buffer_ready[1]
					= 0;

					sdhci_writel(trigdev.ss_trig_sdio->host
					, trigdev.ss_sirfsoc->loopdma_buf[1],
					SDHCI_DMA_ADDRESS);
				}
				writel(LOOPDMA_BUFF0_RDY_FLAG,
					trigdev.ss_trig_sdio->host->ioaddr +
						LOOPDMA_INT_STATUS);
				trigdev.ss_sirfsoc->buffer_ready[0] = 0;
			} else {
				dev_info(&trigdev.ss_trig_sdio->dev,
					"TriG: unexpected SDIO interrupt!\n");
			}
		}

#if 0
		pr_debug("buffer status: ready[0]:%d,ready[1]%d,\n"
			trigdev.ss_sirfsoc->buffer_ready[0],
			trigdev.ss_sirfsoc->buffer_ready[1]);
		pr_debug("buffer status: error[0]:%d,error[1]:%d,\n"
			trigdev.ss_sirfsoc->buffer_err[0],
			trigdev.ss_sirfsoc->buffer_err[1]);
		pr_debug("trigdev.dma_to_user_counter:%d\n",
			trigdev.dma_to_user_counter);
#endif

		trigdev.cur_gps_msg->buf_len = TRIG_LOOPDMA_BLK_SIZE;

		if (counter < SKIP_PACKETS_NUM) {
			if (2 != cur_buf_id)
				release_dma_buffer(cur_buf_id);
			counter++;
		} else {
			if (2 != cur_buf_id && 1 == trigdev.process_packet) {
				trigdev.tmp_gps_msg = trigdev.cur_gps_msg;
				complete(&sg_evt_msg_ready);
				/* switch msg buffer */
				trigdev.cur_gps_msg =
				(trigdev.cur_gps_msg != trigdev.msg_buf) ?
				trigdev.msg_buf : (trigdev.msg_buf + 1);
			}
			if (2 != cur_buf_id && 1 != trigdev.process_packet)
				release_dma_buffer(cur_buf_id);
		}
	}

	return 0;
}

static int gpio_control(unsigned int on_off)
{
	if (of_machine_is_compatible("sirf,prima2")) {
		/*TRIG_SHUTDOWN_B*/
		gpio_set_value(trigdev.sg_trig_gpios.shutdown, 0);
		msleep(100);
		gpio_set_value(trigdev.sg_trig_gpios.shutdown, on_off);
		/*LNA_EN*/
		gpio_set_value(trigdev.sg_trig_gpios.lan_en, on_off);
		/*TCXO_ONLY_B*/
		gpio_set_value(trigdev.sg_trig_gpios.clk_out, on_off);
	}
	return 1;
}

static int trig_sdio_deinit(void)
{
	if (0 == trigdev.config_msg->running_mode) {
		if (NULL != trigdev.trigintthread) {
			trigdev.thread_exit = 1;

			dev_info(&trigdev.ss_trig_sdio->dev,
				"before sdio_dma_int_complete\n");
			sdio_dma_int_complete();

			dev_info(&trigdev.ss_trig_sdio->dev,
				"wait for triGIsEnd:\n");
			wait_for_completion(&sg_evt_trig_exited);
			dev_info(&trigdev.ss_trig_sdio->dev,
				"wait for triGIsEnd sucessfully\n");
			trigdev.thread_exit = 0;
		}
	}
	return 1;
}

static void trig_deinit(void)
{
	int sdhc_rst_value;
	int intmask;
	if (0 == trig_sdio_deinit())
		dev_info(&trigdev.ss_trig_sdio->dev,
			"trig_sdio_deinit failed! \r\n");
#if 0
	trig_writel(trigdev.ss_trig_sdio->func,
				TRIG_CTRL,
				deinitval,
				&ret1);
	if (ret1)
		pr_info("failed to Rewrite TRIG_CTRL register in deinit \r\n");
	else
		pr_info("OK!\r\n");
#endif
	intmask = readl(trigdev.ss_trig_sdio->host->ioaddr +
			LOOPDMA_INT_STATUS);
	if (intmask & LOOPDMA_BUFF0_ERR_FLAG ||
		intmask & LOOPDMA_BUFF1_ERR_FLAG) {
		writel(LOOPDMA_BUFF1_RDY_FLAG,
			trigdev.ss_trig_sdio->host->ioaddr +
			LOOPDMA_INT_STATUS);
		writel(LOOPDMA_BUFF0_ERR_FLAG,
			trigdev.ss_trig_sdio->host->ioaddr +
			LOOPDMA_INT_STATUS);
		writel(LOOPDMA_BUFF1_ERR_FLAG,
			trigdev.ss_trig_sdio->host->ioaddr +
			LOOPDMA_INT_STATUS);
		sdhci_writel(trigdev.ss_trig_sdio->host,
				trigdev.ss_sirfsoc->loopdma_buf[1],
				SDHCI_DMA_ADDRESS);
	}
	/*Abort the TRIG DMA transmission*/
	mmc_io_rw_direct(trigdev.ss_trig_sdio->func->card,
			1, 0, 0x06, trigdev.ss_trig_sdio->func->num, NULL);
	msleep(50);
	sdhc_rst_value = readl(trigdev.ss_trig_sdio->host->ioaddr + 0x2c);
	/*SD_RST_DAT & SD_RST_CMD*/
	sdhc_rst_value |= 0x06000000;
	writel(sdhc_rst_value, trigdev.ss_trig_sdio->host->ioaddr + 0x2c);
	dev_info(&trigdev.ss_trig_sdio->dev,
		"stop trig dma transmission, fun:%d\n",
		trigdev.ss_trig_sdio->func->num);

	sdio_release_irq(trigdev.ss_trig_sdio->func);

	sdio_release_host(trigdev.ss_trig_sdio->func);

	gpio_control(0);
#if 0
	mmc_detect_change(trigdev.ss_sirfsoc->host->mmc, 0);
	msleep(200);
#endif
}

static int trig_config_glo_nco(int *glo_channel, int mask)
{
	int i;
	int index;
	unsigned int config_nco[MAX_GLONASS_CHNUM];
	unsigned int init_trig_data[] = {
		ISP_P2_GLO1_NCO,
		ISP_P2_GLO2_NCO,
		ISP_P2_GLO3_NCO,
		ISP_P2_GLO4_NCO,
		ISP_P2_GLO5_NCO,
		ISP_P2_GLO6_NCO,
		ISP_P2_GLO7_NCO
	};

	for (i = 0; i < trigdev.trig_param->m_fifo_num; i++) {
		if (1 == (mask & 0x01)) {
			trigdev.config_msg->trig_svid[i] =
				*(glo_channel+i) - 70;
			if (trigdev.config_msg->trig_svid[i] > 13 ||
				trigdev.config_msg->trig_svid[i] < 0) {
				dev_info(&trigdev.ss_trig_sdio->dev,
					"Invalid GLONASS SVID:%d",
					trigdev.config_msg->trig_svid[i]);
			} else {
				index = trigdev.config_msg->trig_svid[i];
				config_nco[i] = sg_config_p2_nco_value[index];
				if (trig_reg_init(trigdev.ss_trig_sdio->func,
					init_trig_data[i], config_nco[i])) {
					dev_info(&trigdev.ss_trig_sdio->dev,
						"failed Rewrite NCO regs!\n");
					return 1;
				}
			}
		}
		mask = mask >> 1;
	}
	return 0;
}

static void trig_card_int_proc(struct sdio_func *func)
{
	dev_info(&trigdev.ss_trig_sdio->dev,
		"Received a TriG card interrupt in card proc!\n");
	complete(&sg_evt_card_int);
}


static long trig_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	unsigned int *pbb_pagemem;
	int ret = 0;
	unsigned int p2CtrVal;
	unsigned int addr, value;
	/*param[0]: addr, param[1]: value*/
	unsigned int param[2];
	/*unsigned int deinitval = 0x0000000f;*/
	unsigned int config_trig_reset;

	switch (cmd) {
	case IOCTL_TRIG_INIT:
		if (copy_from_user(trigdev.config_msg_user,
					(void __user *) arg,
					sizeof(struct TRIG_CONFIG_PARAM))) {
			ret = -EINVAL;
		}
		/*running mode 0-TriG 1-File*/
		trigdev.config_msg->running_mode =
			trigdev.config_msg_user->running_mode;
		/*TriG mode 0-93us 1-141us*/
		trigdev.config_msg->trig_mode =
			trigdev.config_msg_user->trig_mode;
		trigdev.config_msg->trig_valid_ch_num =
			trigdev.config_msg_user->trig_valid_ch_num;

		trigdev.valid_chan_num =
			trigdev.config_msg->trig_valid_ch_num;

		/*Write the parameters related to TriG mode*/
		if (3 == trigdev.config_msg->trig_mode) {
			trigdev.trig_param->m_glo_ch_num = 1;
			trigdev.trig_param->m_fifo_num = 1;
			trigdev.trig_param->m_glo_offset_in_dword = 0;
			trigdev.trig_param->m_each_glo_packet_byte = 512;
			trigdev.trig_param->m_packet_time_stamp = 0x200;
			p2CtrVal = ISP_P2_CONTROL_COMPASS_VALUE_141_US;
		} else if (2 == trigdev.config_msg->trig_mode) {
			/*141us COMPASS mode*/
			trigdev.trig_param->m_glo_ch_num =
				trigdev.config_msg->trig_valid_ch_num;
			trigdev.trig_param->m_fifo_num = 1;
			trigdev.trig_param->m_glo_offset_in_dword = 50;
			trigdev.trig_param->m_each_glo_packet_byte = 288;
			trigdev.trig_param->m_packet_time_stamp = 0x300;
			p2CtrVal = ISP_P2_CONTROL_COMPASS_VALUE_141_US;
		} else if (1 == trigdev.config_msg->trig_mode) {
			/*141us mode*/
			trigdev.trig_param->m_glo_ch_num = 4;
			trigdev.trig_param->m_fifo_num = 4;
			trigdev.trig_param->m_glo_offset_in_dword = 50;
			trigdev.trig_param->m_each_glo_packet_byte = 72;
			trigdev.trig_param->m_packet_time_stamp = 0x300;
			p2CtrVal = ISP_P2_CONTROL_DEFAULT_VALUE_141_US;
		} else {
			/*93us mode*/
			trigdev.trig_param->m_glo_ch_num = 7;
			trigdev.trig_param->m_fifo_num = 7;
			trigdev.trig_param->m_glo_offset_in_dword = 34;
			trigdev.trig_param->m_each_glo_packet_byte = 48;
			trigdev.trig_param->m_packet_time_stamp = 0x200;
			p2CtrVal = ISP_P2_CONTROL_DEFAULT_VALUE_93_US;
		}

		trigdev.trig_param->m_each_wraddr_increae_in_sample =
			trigdev.trig_param->m_each_glo_packet_byte * 1024;

		gpio_control(1);
		msleep(50);
#if 0
		mmc_detect_change(trigdev.ss_sirfsoc->host->mmc, 0);
		msleep(200);
		pr_info("TRIG_INIT claim %08x\r\n",
			(unsigned int)trigdev.ss_trig_sdio->func);
#endif
		sdio_claim_host(trigdev.ss_trig_sdio->func);
		ret = sdio_enable_func(trigdev.ss_trig_sdio->func);
		if (ret) {
			dev_info(&trigdev.ss_trig_sdio->dev,
				"error to enable the SDIO device function\n");
			sdio_release_host(trigdev.ss_trig_sdio->func);
			return ret;
		}

		sdio_claim_irq(trigdev.ss_trig_sdio->func, trig_card_int_proc);

		ret = sdio_set_block_size(trigdev.ss_trig_sdio->func,
					  TRIG_SDIO_BLK_SIZE);
		if (ret) {
			dev_info(&trigdev.ss_trig_sdio->dev,
				"error to set block size\n");
			sdio_release_host(trigdev.ss_trig_sdio->func);
			return ret;
		}
		sdio_f0_writeb(trigdev.ss_trig_sdio->func, 0x02, 2, &ret);

		/*Hold ISP reset*/
		config_trig_reset = 0x0000000C;
		trig_writel(trigdev.ss_trig_sdio->func,
					TRIG_CTRL,
					config_trig_reset,
					&ret);
		if (ret) {
			sdio_release_host(trigdev.ss_trig_sdio->func);
			return -EIO;
		}

		if (0 == trig_ana_init(trigdev.ss_trig_sdio->func,
					   trigdev.config_msg->trig_mode)) {
			sdio_release_host(trigdev.ss_trig_sdio->func);
			return -EIO;
		}

		ret = trig_isp_init(trigdev.ss_trig_sdio->func,
					trigdev.config_msg->trig_mode);
		if (0 == ret) {
			dev_info(&trigdev.ss_trig_sdio->dev,
				"error to init trig_isp_init\n");

			sdio_release_host(trigdev.ss_trig_sdio->func);
			return ret;
		}
		/*Rewrite ISP_P2_CONTROL register*/
		if (trig_reg_init(trigdev.ss_trig_sdio->func,
					  ISP_P2_CONTROL,
					  p2CtrVal)) {
			dev_info(&trigdev.ss_trig_sdio->dev,
				"failed to Rewrite ISP_P2_CONTROL register\n");
			return -EIO;
		}
		/*Rewrite NCO registers*/
		if (2 == trigdev.config_msg->trig_mode) {
			if (trig_reg_init(trigdev.ss_trig_sdio->func,
					  ISP_P2_GLO1_NCO,
					  0x1653AA76)) {
				dev_info(&trigdev.ss_trig_sdio->dev,
					"failed to Rewrite ISP_P2_GLO1_NCO register\n");
				return -EIO;
			}
		} else
			trig_config_glo_nco(
				&trigdev.config_msg_user->trig_svid[0],
				trigdev.config_msg_user->trig_valid_ch_num);

		config_trig_reset = 0x00000000;
		trig_writel(trigdev.ss_trig_sdio->func,
					TRIG_CTRL,
					config_trig_reset,
					&ret);
		if (ret) {
			dev_info(&trigdev.ss_trig_sdio->dev,
				"failed to Rewrite TRIG_CTRL register\n");
			sdio_release_host(trigdev.ss_trig_sdio->func);
			return -EIO;
		}
		trigdev.trigintthread = kthread_create(
				trig_int_thread, NULL, "trigdev.trigintthread");
		if (IS_ERR(trigdev.trigintthread)) {
			dev_info(&trigdev.ss_trig_sdio->dev,
				"Unable to start kernerl thread\n");
			ret = PTR_ERR(trigdev.trigintthread);
			trigdev.trigintthread = NULL;
			return ret;
		}
		wake_up_process(trigdev.trigintthread);
		ret = mmc_io_rw_extended(trigdev.ss_trig_sdio->func->card,
						0,
						trigdev.ss_trig_sdio->func->num,
						TRIG_SDIO_FIFO_DATA,
						1,
						NULL,
						512,
						512);
		if (ret) {
			dev_info(&trigdev.ss_trig_sdio->dev,
				"error to mmc_io_rw_extended\n");
			sdio_release_host(trigdev.ss_trig_sdio->func);
			return ret;
		} else {
			dev_info(&trigdev.ss_trig_sdio->dev,
				"command mmc_io_rw_extended done\n");
		}
		if (trigdev.filp_init == NULL)
			trigdev.filp_init = filp;
		break;

	case IOCTL_TRIG_CONFIG:
		if (copy_from_user(trigdev.config_msg_user,
					(void __user *) arg,
					sizeof(struct TRIG_CONFIG_PARAM))) {
			ret = -EINVAL;
		}
		writel(LOOPDMA_BUFF0_RDY_FLAG,
			trigdev.ss_trig_sdio->host->ioaddr +
			LOOPDMA_INT_STATUS);
		writel(LOOPDMA_BUFF1_RDY_FLAG,
			trigdev.ss_trig_sdio->host->ioaddr +
			LOOPDMA_INT_STATUS);
		/*NCO configuration*/
		trig_config_glo_nco(&trigdev.config_msg_user->trig_svid[0],
			trigdev.config_msg_user->trig_valid_ch_num);

		break;

	case IOCTL_TRIG_SDIO_INT_ENABLE:
		break;

	case IOCTL_TRIG_SDIO_INT_DISABLE:
		break;

	case IOCTL_TRIG_GET_PACKET_PARAM:
		if (copy_to_user((void __user *) arg,
				 trigdev.trig_param,
				 sizeof(struct TRIG_PARAMETER))) {
			ret = -EINVAL;
		}
		break;

	case IOCTL_TRIG_ISPREG_READ:
		if (copy_from_user(&addr,
					(void __user *) arg,
					sizeof(addr))) {
			return -EINVAL;
		}
		value = trig_readl(trigdev.ss_trig_sdio->func, addr, &ret);
		if (copy_to_user((void __user *) arg,
					&value,
					sizeof(value))) {
			ret = -EINVAL;
		}
		break;

	case IOCTL_TRIG_ISPREG_WRITE:
		if (copy_from_user(&param[0],
					(void __user *) arg,
					sizeof(param))) {
			return -EINVAL;
		}
		trig_writel(trigdev.ss_trig_sdio->func,
				param[0], param[1], &ret);
		break;

	case IOCTL_TRIG_RESET_PACKET_PROC:
		trigdev.process_packet = 1;
		break;

	case IOCTL_TRIG_STOP_PACKET_PROC:
		trigdev.process_packet = 0;
		break;

	case IOCTL_TRIG_DEINIT:
		if (NULL == trigdev.filp_init) {
			dev_info(&trigdev.ss_trig_sdio->dev,
				"No need to deinit trig\n");
			return 0;
		}
		trigdev.filp_init = NULL;

		trig_deinit();

		dev_info(&trigdev.ss_trig_sdio->dev,
			"IOCTL_TRIG_DEINIT release %08x\n",
			(unsigned int)trigdev.ss_trig_sdio->func);
		break;

	case IOCTL_TRIG_SET_GLOBB_THREADID:
		break;

	case IOCTL_TRIG_WAIT_FOR_CARD_INT:
#if 0
		int value, ret;
		msleep(1000);
		value = trig_readl(trigdev.ss_trig_sdio->func, 0x35c, &ret);
		pr_info("TRIG_CW0_NOISE_STAT:%08x\r\n", value);
		value = trig_readl(trigdev.ss_trig_sdio->func, 0x484, &ret);
		pr_info("TRIG_CW1_NOISE_STAT:%08x\r\n", value);
		value = trig_readl(trigdev.ss_trig_sdio->func, 0x488, &ret);
		pr_info("TRIG_CW1_PEAK_ID0_STAT:%08x\r\n", value);
#endif
		if (!wait_for_completion_interruptible_timeout(
				&sg_evt_card_int, msecs_to_jiffies(1000)))
			ret = -EINVAL;
		else
		dev_info(&trigdev.ss_trig_sdio->dev,
			"Received a TriG card interrupt!\n");
		break;

	case IOCTL_TRIG_GET_PBB_MEM:
		if (copy_from_user(trigdev.trig_param_buf,
					(void __user *) arg,
					sizeof(struct TRIG_PARA_BUF))) {
			ret = -EINVAL;
		}
		pbb_pagemem = (unsigned int *)trigdev.pbb_base_addr;
		trigdev.trig_param_buf->pbb_buf_phy_addr =
					trigdev.pbb_phys_addr;
		if (!pbb_pagemem) {
			dev_info(&trigdev.ss_trig_sdio->dev,
				"mmap pbb is failed!\n");
			ret = -EINVAL;
		}
		trigdev.trig_param_buf->pbb_buf_vir_addr =
					(unsigned int)pbb_pagemem;

		if (copy_to_user((void __user *) arg,
					trigdev.trig_param_buf,
					sizeof(struct TRIG_PARA_BUF))) {
			ret = -EINVAL;
		}
		dev_info(&trigdev.ss_trig_sdio->dev,
			"pbb mem_size:%d,virt_addr 0x%x,phys_addr 0x%x\n",
			trigdev.trig_param_buf->mem_size,
			(unsigned int)pbb_pagemem,
			(unsigned int)(
			trigdev.trig_param_buf->pbb_buf_phy_addr));
		break;

	case IOCTL_TRIG_RELEASE_PBB_MEM:
		if (copy_from_user(trigdev.trig_param_buf,
					(void __user *) arg,
					sizeof(struct TRIG_PARA_BUF))) {
			ret = -EINVAL;
		}
#if 0
		dma_free_coherent(NULL,
				trigdev.trig_param_buf->mem_size,
				(int)trigdev.trig_param_buf->pbb_buf_vir_addr,
				trigdev.trig_param_buf->pbb_buf_phy_addr);
#endif
		break;

	case IOCTL_TRIG_GET_LOOPDMA_VIR_ADDR:
		trigdev.trig_param_buf->dma0_buf_kernerl_vir_addr =
				(unsigned int)
				trigdev.ss_trig_sdio->loopdma_va_buf[0];
		trigdev.trig_param_buf->dma1_buf_kernerl_vir_addr =
				(unsigned int)
				trigdev.ss_trig_sdio->loopdma_va_buf[1];
		trigdev.trig_param_buf->dma0_buf_kernerl_phy_addr =
				trigdev.ss_sirfsoc->loopdma_buf[0];
		trigdev.trig_param_buf->dma1_buf_kernerl_phy_addr =
				trigdev.ss_sirfsoc->loopdma_buf[1];
		if (copy_to_user((void __user *) arg,
					 trigdev.trig_param_buf,
					 sizeof(struct TRIG_PARA_BUF))) {
			ret = -EINVAL;
		}
		break;

	case IOCTL_TRIG_GET_CUR_LOOPDMA_ADDR:
		if (!wait_for_completion_interruptible_timeout(
				&sg_evt_msg_ready, msecs_to_jiffies(500))) {
			ret = -EINVAL;
		} else {
			if (copy_to_user((void __user *) arg,
					/*trigdev.cur_gps_msg,*/
					trigdev.tmp_gps_msg,
					sizeof(struct SDIO_GPS_MSG))) {
				ret = -EINVAL;
			}
			trigdev.dma_to_user_counter++;
		}
		break;

	case IOCTL_TRIG_RELEASE_CUR_LOOPDMA_ADDR:
		/*pr_info("Enter RELEASE_CUR_LOOPDMA_ADDR.\r\n");*/
		if (copy_from_user(trigdev.trig_param_buf,
					   (void __user *) arg,
					   sizeof(struct TRIG_PARA_BUF))) {
			ret = -EINVAL;
		}

		if (trigdev.trig_param_buf->need_release_dma_buf ==
			trigdev.trig_param_buf->dma0_buf_kernerl_vir_addr) {
			release_dma_buffer(0);
		} else if (trigdev.trig_param_buf->need_release_dma_buf ==
			trigdev.trig_param_buf->dma1_buf_kernerl_vir_addr) {
			release_dma_buffer(1);
		} else {
			dev_info(&trigdev.ss_trig_sdio->dev,
				"GET ERROR RELEASE CUR LOOPDMA ADDR!\n");
		}
		trigdev.dma_to_user_counter--;
		break;

	}
	return ret;
}

static int trig_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long off;
	dev_info(&trigdev.ss_trig_sdio->dev,
		"enter trig-mmap\n");
	off = vma->vm_pgoff << PAGE_SHIFT;
	off += trigdev.ss_sirfsoc->loopdma_buf[0];
	if ((off == trigdev.ss_sirfsoc->loopdma_buf[0]) ||
		(off == trigdev.ss_sirfsoc->loopdma_buf[1])) {
		vma->vm_pgoff = off >> PAGE_SHIFT;
		dev_info(&trigdev.ss_trig_sdio->dev,
			"trig mmap loopdma buffer address\n");
	} else {
		vma->vm_pgoff = off >> PAGE_SHIFT;
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
		dev_info(&trigdev.ss_trig_sdio->dev,
			"trig mmap PBB buffer address\n");
	}
	if (remap_pfn_range(vma, vma->vm_start, off >> PAGE_SHIFT,
					vma->vm_end - vma->vm_start,
					vma->vm_page_prot)) {
		return -EAGAIN;
	}
	return 0;
}
static int trig_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int trig_release(struct inode *inode, struct file *filp)
{
	if (NULL == trigdev.filp_init) {
		dev_info(&trigdev.ss_trig_sdio->dev,
			"Trig already release\n");
		return 0;
	}
	if (trigdev.filp_init != filp) {
		dev_info(&trigdev.ss_trig_sdio->dev,
			"No need for this filp to release\n");
		return 0;
	}

	trig_deinit();

	dev_info(&trigdev.ss_trig_sdio->dev,
		"trig_release release %08x\n",
		(unsigned int)trigdev.ss_trig_sdio->func);
	trigdev.filp_init = NULL;
	return 0;
}

static const struct sdio_device_id trig_sdio_ids[] = {
	{ SDIO_DEVICE(SDIO_VENDOR_ID_CSR, SDIO_DEVICE_ID_CSR_TRIG) },
	{ },
};
MODULE_DEVICE_TABLE(sdio, trig_sdio_ids);

static struct sdio_driver trig_sdio_driver = {
	.name = "trig_sdio",
	.id_table = trig_sdio_ids,
	.probe = sirf_trig_probe,
	.remove = sirf_trig_remove,
	.drv = {
	   .owner = THIS_MODULE,
	}
};

static int sirf_trig_probe(struct sdio_func *func,
		const struct sdio_device_id *id)
{
	struct mmc_card *card = func->card;
	struct mmc_host *host = card->host;
	struct device *dev = host->parent;
	struct platform_device *pdev = to_platform_device(dev);
	struct sdhci_host *shost = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(shost);
	struct sdhci_sirf_priv *priv = pltfm_host->priv;
	struct device_node *pdn;
	int ret;

	dev_info(dev, "TriG driver probe start now\n");
	trigdev.ss_sirfsoc = priv;
	trigdev.ss_trig_sdio->dev = *dev;

	trigdev.ss_trig_sdio->loopdma_va_buf[0] = priv->mem_buf[0];
	trigdev.ss_trig_sdio->loopdma_va_buf[1] = priv->mem_buf[1];

	trigdev.pbb_phys_addr = sirf_pbb_phy_base;
	trigdev.pbb_base_addr = ioremap(sirf_pbb_phy_base,
					sirf_pbb_phy_size);
	if (!trigdev.pbb_base_addr) {
		dev_err(&pdev->dev, "GPS: ioremap failed for gps-pbb\n");
		ret = -EINVAL;
		return -ENOMEM;
	}
	trigdev.thread_exit = 0;

	/*move following lines from init_module to
	 *here to fix the hibernation bug in P2EVB*/
	pdn = of_find_node_by_path(GPS_NODEPATH_DTS);
	if (!pdn) {
		dev_err(&pdev->dev, "can't find prima2-gps node\n");
		return -EINVAL;
	}
	trigdev.sg_trig_gpios.shutdown =
		of_get_named_gpio(pdn, "shutdown-gpios", 0);
	trigdev.sg_trig_gpios.lan_en =
		of_get_named_gpio(pdn, "lan-en-gpios", 0);
	trigdev.sg_trig_gpios.clk_out =
		of_get_named_gpio(pdn, "clk-out-gpios", 0);

	if (gpio_is_valid(trigdev.sg_trig_gpios.shutdown)) {
		ret = gpio_request(
			trigdev.sg_trig_gpios.shutdown, "shutdown-gpios");
		if (ret) {
			dev_info(&trigdev.ss_trig_sdio->dev,
				"TriG TRIG_SHUTDOWN_B request failed!\n");
			return ret;
		}
		gpio_direction_output(trigdev.sg_trig_gpios.shutdown, 1);
	}

	if (gpio_is_valid(trigdev.sg_trig_gpios.lan_en)) {
		ret = gpio_request(
			trigdev.sg_trig_gpios.lan_en, "lan-en-gpios");
		if (ret) {
			dev_info(&trigdev.ss_trig_sdio->dev,
				"TriG LAN_EN request failed!\n");
			return ret;
		}
		gpio_direction_output(trigdev.sg_trig_gpios.lan_en, 1);
	}

	if (gpio_is_valid(trigdev.sg_trig_gpios.clk_out)) {
		ret = gpio_request(
			trigdev.sg_trig_gpios.clk_out, "clk-out-gpios");
		if (ret) {
			dev_info(&trigdev.ss_trig_sdio->dev,
				"TriG CLK_OUT request failed!\n");
			return ret;
		}
		gpio_direction_output(trigdev.sg_trig_gpios.clk_out, 1);
	}

	/* get gps_rtc_base */
	pdn = of_find_node_by_path(GPSRTC_NODEPATH_DTS);
	if (!pdn) {
		dev_err(&pdev->dev, "TRIG: can't find node name gpsrtc\n");
		return -EINVAL;
	}
	ret = of_property_read_u32(pdn, "reg", &trigdev.gps_rtc_base);
	if (ret) {
		dev_err(&pdev->dev, "can't find gpsrtc base in dtb\n");
		return -EINVAL;
	}

	ret = register_chrdev_region(MKDEV(TRIG_MAJOR, 0), 1, "trig_sdio");
	if (ret < 0)
		dev_err(&pdev->dev, "TRIG: can't register device!\n");

	cdev_init(&(trigdev.ss_trig_sdio->cdev), &trig_fops);
	trigdev.ss_trig_sdio->cdev.owner = THIS_MODULE;
	trigdev.ss_trig_sdio->cdev.ops = &trig_fops;
	ret = cdev_add(&(trigdev.ss_trig_sdio->cdev), MKDEV(TRIG_MAJOR, 0), 1);
	if (ret)
		dev_err(&pdev->dev, "TRIG: Error adding TRIG!\n");

	dev_info(&trigdev.ss_trig_sdio->dev,
		"TriG probed, func 0x%x, trigdev.ss_trig_sdio 0x%x\n",
		(unsigned int)func,
		(unsigned int)trigdev.ss_trig_sdio->func);
	trigdev.ss_trig_sdio->func = func;
	trigdev.ss_trig_sdio->host = shost;
	return 0;
}

static void sirf_trig_remove(struct sdio_func *func)
{
	dev_info(&trigdev.ss_trig_sdio->dev,
		"TriG driver remove start now!\n");

	/*move following lines from exit_module to here*/
	if (gpio_is_valid(trigdev.sg_trig_gpios.shutdown))
		gpio_free(trigdev.sg_trig_gpios.shutdown);
	if (gpio_is_valid(trigdev.sg_trig_gpios.lan_en))
		gpio_free(trigdev.sg_trig_gpios.lan_en);
	if (gpio_is_valid(trigdev.sg_trig_gpios.clk_out))
		gpio_free(trigdev.sg_trig_gpios.clk_out);
	dev_info(&trigdev.ss_trig_sdio->dev,
		"end of gpio free!\n");
	/*end of line moving*/
	iounmap((void *)trigdev.pbb_base_addr);
	sdio_claim_host(func);
	sdio_disable_func(func);
	sdio_release_host(func);

	cdev_del(&(trigdev.ss_trig_sdio->cdev));
	unregister_chrdev_region(MKDEV(TRIG_MAJOR, 0), 1);

	dev_info(&trigdev.ss_trig_sdio->dev,
		"TriG driver remove end!");
}

static int __init trig_sdio_init_module(void)
{
	trigdev.process_packet = 0;
	trigdev.ss_trig_sdio = kzalloc(
		sizeof(struct trig_sdio), GFP_KERNEL);
	trigdev.config_msg = kzalloc(
		sizeof(struct TRIG_CONFIG_PARAM), GFP_KERNEL);
	trigdev.trig_param = kzalloc(
		sizeof(struct TRIG_PARAMETER), GFP_KERNEL);
	trigdev.trig_param_buf = kzalloc(
		sizeof(struct TRIG_PARA_BUF), GFP_KERNEL);
	trigdev.config_msg_user = kzalloc(
		sizeof(struct TRIG_CONFIG_PARAM), GFP_KERNEL);
	return sdio_register_driver(&trig_sdio_driver);
}

static void __exit trig_sdio_exit_module(void)
{
	kfree(trigdev.config_msg_user);
	kfree(trigdev.trig_param_buf);
	kfree(trigdev.trig_param);
	kfree(trigdev.config_msg);
	kfree(trigdev.ss_trig_sdio);
	sdio_unregister_driver(&trig_sdio_driver);
}

module_init(trig_sdio_init_module);
module_exit(trig_sdio_exit_module);

MODULE_DESCRIPTION("SiRF SoC TriG SDIO GPS driver");
MODULE_LICENSE("GPL");
