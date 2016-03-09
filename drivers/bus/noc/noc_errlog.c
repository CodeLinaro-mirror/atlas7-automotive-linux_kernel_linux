/*
 * Atlas7 NoC support
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

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/sysfs.h>

#include "noc.h"

#define ERRORLOGGER_0_ID_COREID   0x0
#define ERRORLOGGER_0_ID_REVISIONID 0x4
#define ERRORLOGGER_0_FAULTEN   0x8
#define ERRORLOGGER_0_ERRVLD       0xc
#define ERRORLOGGER_0_ERRCLR  0x10
#define ERRORLOGGER_0_ERRLOG0 0x14
#define ERRORLOGGER_0_ERRLOG1 0x18
#define ERRORLOGGER_0_ERRLOG3 0x20
#define ERRORLOGGER_0_ERRLOG5 0x28

#define NOC_SB_FAULTEN 0x08
#define NOC_SB_FLAGINEN0 0x10
#define NOC_SB_FLAGINSTATUS 0x14
#define NOC_SB_FAULT_STATUS 0x0C

static const char * const noc_err_list[] = {
	"target error detected by slave",
	"address decode error",
	"unsupported request",
	"power disconnect",
	"security violation",
	"hidden security violation",
	"timout",
	"reserved",
};

static const char * const noc_opc_list[] = {
	"read",
	"wrap read",
	"link read",
	"exclusive read",
	"write",
	"wrap write",
	"condition write",
	"reserved",
	"preable packet",
	"urgency packet",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
};

static const char * const noc_cpu_list[] = {
	"a7",
	"cssi",
	"m3",
	"kas",
};

static const char * const noc_initator_id_list[] = {
	"dmac2_ac97_aux_fifo",
	"kas_dram",
	"afe_cvd_vip0",
	"usp0_axi_i",
	"sgx",
	"sdr",
	"dmac2_usp1rx",
	"dmac2_usp1tx",
	"usb0",
	"usb1",
	"dmac2_usp0rx",
	"dmac2_usp0tx",
	"dmac2_usp2rx",
	"dmac2_usp2tx",
	"reserved",
	"reserved",
	"dmac3_iaccrx",
	"dmac3_i2s1rx",
	"dmac3_i2s1tx",
	"dmac3_iacctx2",
	"reserved",
	"reserved",
	"dmac3_ac97rx_fifo",
	"dmac3_iacctx0",
	"dmac3_iacctx1",
	"dmac3_iacctx3",
	"dmac3_ac97tx_fifo5",
	"dmac3_ac97tx_fifo6",
	"dmac3_ac97tx_fifo1",
	"dmac3_ac97tx_fifo2",
	"dmac3_ac97tx_fifo3",
	"dmac3_ac97tx_fifo4",
	"dmac4_usp3rx",
	"dmac4_usp3tx",
	"vpp0",
	"vpp1",
	"vip1",
	"dcu",
	"g2d",
	"nand",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"dmac4_uart6rx",
	"dmac4_uart6tx",
	"reserved",
	"reserved",
	"dmac0_uart4rx",
	"dmac0_uart4tx",
	"dmac0_uart0tx",
	"dmac0_uart0rx",
	"dmac0_uart3rx",
	"dmac0_uart3tx",
	"dmac0_uart2rx",
	"dmac0_uart2tx",
	"dmac0_uart5rx",
	"dmac0_uart5tx",
	"sec_secure",
	"sec_public",
	"dmac0_spi1rx",
	"dmac0_spi1tx",
	"reserved",
	"reserved",
	"sys2pci_vdifm",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"sys2pci_mediam",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"armm3_data",
	"qspi",
	"hash",
	"cssi_etr_axi",
	"eth_avb",
	"reserved",
	"reserved",
	"reserved",
	"lcd0_ly0_rd",
	"lcd0_ly1_rd",
	"lcd0_ly2_rd",
	"lcd0_ly3_rd",
	"lcd0_wb_rd",
	"reserved",
	"reserved",
	"reserved",
	"lcd1_ly1_rd",
	"lcd1_ly1_rd",
	"lcd1_ly2_rd",
	"lcd1_ly3_rd",
	"lcd1_wb_rd",
	"reserved",
	"reserved",
	"reserved",
	"vxd_mmu",
	"vxd_dmac",
	"vxd_vec",
	"vxd_dmc",
	"vxd_deb",
	"reserved",
	"reserved",
	"reserved",
	"jpeg_tar",
	"jpeg_code",
	"jpeg_thumb",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
};

int noc_get_cpu_by_name(const char *name)
{
	int i = 0;
	int size = ARRAY_SIZE(noc_cpu_list);

	while (i < size) {
		if (!strcmp(noc_cpu_list[i], name))
			return i;
		i++;
	}
	return -1;
}

/*data abort handler can not get base list*/
static bool noc_has_err(void __iomem *noc_errlog_mbase)
{
	u32 vld;

	vld = readl_relaxed(noc_errlog_mbase + ERRORLOGGER_0_ERRVLD);
	vld &= 0x1;
	/* 1 indicates an error has been logged (default: 0x0) */
	return !!vld;
}

/*
 * CAUTION: gpum, audiom don't have ERRORLOGGER_0_ERRLOG5 register!!!
 * when their error log is enabled, this function should be modified!!!
 */
#define NOC_INITIATOR_TYPE	BIT(0)
#define NOC_INITIATOR_TYPE_CPU	0
int noc_dump_errlog(struct noc_macro *nocm)
{
	u32 errCode0, errCode1, errCode3, errCode5;
	bool vld;
	void __iomem *noc_errlog_mbase;

	pr_info("err[%s]\n", nocm->name);

	noc_errlog_mbase = (void __iomem *)(nocm->mbase + nocm->errlogoff);
	/* race of async abort and irq*/
	spin_lock(&nocm->lock);
	/*return 1 for normal abort handler */
	vld = noc_has_err(noc_errlog_mbase);
	if (!vld)
		goto err;

	errCode0 = readl_relaxed(noc_errlog_mbase + ERRORLOGGER_0_ERRLOG0);
	errCode1 = readl_relaxed(noc_errlog_mbase + ERRORLOGGER_0_ERRLOG1);
	errCode3 = readl_relaxed(noc_errlog_mbase + ERRORLOGGER_0_ERRLOG3);
	errCode5 = readl_relaxed(noc_errlog_mbase + ERRORLOGGER_0_ERRLOG5);

	/*error type*/
	pr_info("err[%s]:\t%s\n", nocm->name,
		noc_err_list[(errCode0>>8) & 0x7]);

	/*initiator id*/
	if (NOC_INITIATOR_TYPE_CPU == (errCode5 & NOC_INITIATOR_TYPE))
		pr_info("ID:\t%s\n", noc_cpu_list[(errCode5>>10) & 0x3]);
	else
		pr_info("ID:\%s\n", noc_initator_id_list[(errCode5>>5)
			& 0x7F]);

	pr_info("Opc:\t%s\n", noc_opc_list[(errCode0>>1) & 0xF]);
	pr_info("Addr\t%08x\n", errCode3);
	pr_info("Len\t%08x\n", errCode0>>16 & 0xFF);

	/* clear the NoC errlog */
	writel_relaxed(0x1, noc_errlog_mbase + ERRORLOGGER_0_ERRCLR);
	spin_unlock(&nocm->lock);

	return 0;
err:
	return 1;
}

/*noc fault contains probe and errlog interrupts*/
void noc_errlog_enable(struct noc_macro *nocm)
{
	writel_relaxed(0x1, nocm->mbase +
		nocm->faultenoff + NOC_SB_FAULTEN);
	/*
	 * enable errlog and all alarm interrupts
	 */
	writel_relaxed(0xffff, nocm->mbase +
		nocm->faultenoff + NOC_SB_FLAGINEN0);

	if (nocm->errlogoff)
		writel_relaxed(0x1, nocm->mbase +
			nocm->errlogoff + ERRORLOGGER_0_FAULTEN);
}
