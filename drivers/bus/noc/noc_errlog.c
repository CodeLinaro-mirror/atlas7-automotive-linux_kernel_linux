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

#define FW_A7 0x0
#define FW_DDR_BE 0x4000
#define FW_DDR_RTLL 0x8000
#define FW_DDR_RT   0xC000
#define FW_DDR_SGX 0x10000
#define FW_DDR_VXD 0x14000

struct id_rp_maps_t {
	char *name;
	int rpbase;
};

static struct id_rp_maps_t noc_cpu_id_list[] = {
	{"kas_apb", FW_DDR_RTLL},
	{"kas_axi", FW_DDR_RTLL},
	{"armm3_apb", FW_DDR_BE},
	{"armm3_axi", FW_DDR_RT},
};

static struct id_rp_maps_t noc_initator_id_list[] = {
	{"dmac2_ac97_aux_fifo", FW_DDR_RTLL},
	{"kas_dram", FW_DDR_RTLL},
	{"afe_cvd_vip0_spdif-tx", FW_DDR_RTLL},
	{"usp0_axi_i", FW_DDR_RTLL},
	{"sgx", FW_DDR_SGX},
	{"sdr", FW_DDR_RTLL},
	{"dmac2_usp1rx", FW_DDR_RTLL},
	{"dmac2_usp1tx", FW_DDR_RTLL},
	{"usb0", FW_DDR_BE},
	{"usb1", FW_DDR_BE},
	{"dmac2_usp0rx", FW_DDR_RTLL},
	{"dmac2_usp0tx", FW_DDR_RTLL},
	{"dmac2_usp2rx", FW_DDR_RTLL},
	{"dmac2_usp2tx", FW_DDR_RTLL},
	{"dmac2_iaccrx1",},
	{"dmac2_iaccrx2",},
	{"dmac3_iaccrx0", FW_DDR_RTLL},
	{"dmac3_i2s1rx", FW_DDR_RTLL},
	{"dmac3_i2s1tx", FW_DDR_RTLL},
	{"dmac3_iacctx2", FW_DDR_RTLL},
	{"reserved",},
	{"reserved",},
	{"dmac3_ac97rx_fifo", FW_DDR_RTLL},
	{"dmac3_iacctx0", FW_DDR_RTLL},
	{"dmac3_iacctx1", FW_DDR_RTLL},
	{"dmac3_iacctx3", FW_DDR_RTLL},
	{"dmac3_ac97tx_fifo5", FW_DDR_RTLL},
	{"dmac3_ac97tx_fifo6", FW_DDR_RTLL},
	{"dmac3_ac97tx_fifo1", FW_DDR_RTLL},
	{"dmac3_ac97tx_fifo2", FW_DDR_RTLL},
	{"dmac3_ac97tx_fifo3", FW_DDR_RTLL},
	{"dmac3_ac97tx_fifo4", FW_DDR_RTLL},
	{"dmac4_usp3rx", FW_DDR_RTLL},
	{"dmac4_usp3tx", FW_DDR_RTLL},
	{"vpp0", FW_DDR_RT},
	{"vpp1", FW_DDR_RT},
	{"vip1", FW_DDR_RT},
	{"dcu", FW_DDR_RT},
	{"g2d", FW_DDR_BE},
	{"nand", FW_DDR_BE},
	{"DDR_BIST",},
	{"reserved",},
	{"reserved",},
	{"reserved",},
	{"dmac4_uart6rx", FW_DDR_RTLL},
	{"dmac4_uart6tx", FW_DDR_RTLL},
	{"reserved",},
	{"reserved",},
	{"dmac0_uart4rx", FW_DDR_BE},
	{"dmac0_uart4tx", FW_DDR_BE},
	{"dmac0_uart0tx", FW_DDR_BE},
	{"dmac0_uart0rx", FW_DDR_BE},
	{"dmac0_uart3rx", FW_DDR_BE},
	{"dmac0_uart3tx", FW_DDR_BE},
	{"dmac0_uart2rx", FW_DDR_BE},
	{"dmac0_uart2tx", FW_DDR_BE},
	{"dmac0_uart5rx", FW_DDR_BE},
	{"dmac0_uart5tx", FW_DDR_BE},
	{"sec_secure", FW_DDR_BE},
	{"sec_public", FW_DDR_BE},
	{"dmac0_spi1rx", FW_DDR_BE},
	{"dmac0_spi1tx", FW_DDR_BE},
	{"reserved",},
	{"reserved",},
	{"sys2pci_vdifm", FW_DDR_RT},
	{"reserved",},
	{"reserved",},
	{"reserved",},
	{"reserved",},
	{"reserved",},
	{"reserved",},
	{"reserved",},
	{"reserved",},
	{"reserved",},
	{"reserved",},
	{"reserved",},
	{"reserved",},
	{"reserved",},
	{"reserved",},
	{"reserved",},
	{"sys2pci_mediam", FW_DDR_BE},
	{"reserved",},
	{"reserved",},
	{"reserved",},
	{"DMAC1_HS_I2S0", FW_DDR_RT},
	{"DMAC1_HS_I2S1", FW_DDR_RT},
	{"reserved",},
	{"reserved",},
	{"armm3_data", FW_DDR_RT},
	{"qspi", FW_DDR_BE},
	{"hash", FW_DDR_BE},
	{"cssi_etr_axi", FW_DDR_BE},
	{"eth_avb", FW_DDR_BE},
	{"reserved",},
	{"reserved",},
	{"reserved",},
	{"lcd0_ly0_rd", FW_DDR_RT},
	{"lcd0_ly1_rd", FW_DDR_RT},
	{"lcd0_ly2_rd", FW_DDR_RT},
	{"lcd0_ly3_rd", FW_DDR_RT},
	{"lcd0_wb_rd", FW_DDR_RT},
	{"reserved",},
	{"reserved",},
	{"reserved",},
	{"lcd1_ly0_rd_lcd1_wb_wr", FW_DDR_RT},
	{"lcd1_ly1_rd", FW_DDR_RT},
	{"lcd1_ly2_rd", FW_DDR_RT},
	{"lcd1_ly3_rd", FW_DDR_RT},
	{"lcd1_wb_rd", FW_DDR_RT},
	{"reserved",},
	{"reserved",},
	{"reserved",},
	{"vxd_mmu", FW_DDR_VXD},
	{"vxd_dmac", FW_DDR_VXD},
	{"vxd_vec", FW_DDR_VXD},
	{"vxd_dmc", FW_DDR_VXD},
	{"vxd_deb", FW_DDR_VXD},
	{"reserved",},
	{"reserved",},
	{"reserved",},
	{"jpeg_tar", FW_DDR_BE},
	{"jpeg_code", FW_DDR_BE},
	{"jpeg_thumb", FW_DDR_BE},
	{"reserved",},
	{"reserved",},
	{"reserved",},
	{"reserved",},
	{"reserved",},
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

int noc_get_noncpu_by_name(const char *name)
{
	int i = 0;
	int size = ARRAY_SIZE(noc_initator_id_list);

	while (i < size) {
		if (!strcmp(noc_initator_id_list[i].name, name))
			return i;
		i++;
	}
	return -1;
}

int noc_get_rpbase_by_name(const char *name)
{
	int i = 0;
	int size = ARRAY_SIZE(noc_initator_id_list);

	while (i < size) {
		if (!strcmp(noc_initator_id_list[i].name, name))
			return noc_initator_id_list[i].rpbase;
		i++;
	}
	return -1;
}

int noc_get_rpbase_by_bus(const char *name)
{
	int i = 0;
	int size = ARRAY_SIZE(noc_cpu_id_list);

	while (i < size) {
		if (!strcmp(noc_cpu_id_list[i].name, name))
			return noc_cpu_id_list[i].rpbase;
		i++;
	}
	return -1;
}

struct id_err_maps_t {
	int orig;
	int new;
};

static struct id_err_maps_t err_id_maps[] = {
	{8, 24},
	{9, 25},
	{10, 26},
	{11, 27},
	{12, 28},
	{13, 29},
	{16, 0},
	{17, 1},
	{18, 2},
	{19, 3},
	{22, 6},
	{23, 7},
	{44, 60},
	{45, 61},
	{80, 64},
};

int noc_get_id_by_orig(int orig)
{
	int i = 0;
	int size = ARRAY_SIZE(err_id_maps);

	if (of_machine_is_compatible("sirf,atlas7-b3"))
		return orig;

	while (i < size) {
		if (err_id_maps[i].orig == orig)
			return err_id_maps[i].new;
		i++;
	}
	return orig;
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

	pr_debug("err[%s]\n", nocm->name);
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
		pr_info("ID:\%s\n", noc_initator_id_list
				[(errCode5>>5) & 0x7F].name);

	pr_info("Opc:\t%s\n", noc_opc_list[(errCode0>>1) & 0xF]);
	pr_info("Addr\t%08x\n", errCode3);
	pr_info("Len\t%08x\n", errCode0>>16 & 0xFF);

	/* clear the NoC errlog */
	writel_relaxed(0x1, noc_errlog_mbase + ERRORLOGGER_0_ERRCLR);
	spin_unlock(&nocm->lock);

	return 0;
err:
	spin_unlock(&nocm->lock);
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
