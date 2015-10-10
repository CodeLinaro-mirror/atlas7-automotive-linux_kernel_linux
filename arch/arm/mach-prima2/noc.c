/*
 * Atlas7 NoC support
 *
 * Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
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

#define pr_fmt(fmt) "NoC: " fmt

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/sysfs.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/signal.h>
#include <linux/clk.h>
#include <asm/div64.h>

#define NOC_CPUM_ERRLOG   0x800
#define NOC_CPUM_FAULTEN  0x900

#define NOC_AUDMSCM_ERRLOG  0xC00
#define NOC_AUDMSCM_FAULTEN 0x400


#define NOC_DDRM_ERRLOG  0x180
#define NOC_DDRM_FAULTEN 0x800


#define NOC_RTCM_ERRLOG  0xA00
#define NOC_RTCM_FAULTEN 0x900



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

#define FLAGS_CPU	BIT(0)
#define FLAGS_STRICT	BIT(1)
#define FLAGS_NS	BIT(2)
#define FLAGS_S		BIT(3)
#define FLAGS_INITIATOR_NS  BIT(4)
#define FLAGS_INITIATOR_S   BIT(5)


#define CPUMASK_KAS   3
#define CPUMASK_CM3   2
#define CPUMASK_CSSI   1
#define CPUMASK_CA7   0

#define ACCESS_READ      BIT(0)
#define ACCESS_WRITE      BIT(1)
#define ACCESS_INITIATOR_READ      BIT(2)
#define ACCESS_INITIATOR_WRITE      BIT(3)


#define FW_RP_ENABLE_SET   0x3F04

struct sirfsoc_nocfw_dram_t {
	u32 rpbase;
	u32 startaddr;
	u32 endaddr;
	u32 initiator;
	u32 access;
	u32 initiator_access;
	u32 rpnum;
	u32 flags;
};

struct dramfw_regs_access_t {
	u32 initiator_r_set;
	u32 initiator_r_clr;
	u32 initiator_w_set;
	u32 initiator_w_clr;
};


/* dram fw */
struct dramfw_regs_t {
	u32 start;
	u32 end;
	u32 reserved_0[2];
	struct dramfw_regs_access_t access[4];
	u32 reserved_1[4];
	u32 fw_cpu_set;
	u32 fw_cpu_clr;
	u32 reserved_2[2];
	u32 prot_set;
	u32 prot_clr;
	u32 prot_val_set;
	u32 prot_val_clr;
	u32 target_set;
	u32 target_clr;
	u32 target_val_set;
	u32 target_val_clr;
};

/* reg fw */
struct regfw_regs_t {
	u32 ns_set;
	u32 ns_clr;
	u32 ns_sts;
	u32 a7_set;
	u32 a7_clr;
	u32 a7_sts;
	u32 cssi_set;
	u32 cssi_clr;
	u32 cssi_sts;
	u32 m3_set;
	u32 m3_clr;
	u32 m3_sts;
	u32 kas_set;
	u32 kas_clr;
	u32 kas_sts;
};

struct sirfsoc_nocfw_reg_t {
	u32 base;
	u32 off;
	u32 ns;
	u32 a7;

	u32 cssi;
	u32 m3;
	u32 kas;
};

#define ddrm_SecureState_ReadSet0    0x1050
#define ddrm_SecureState_ReadClr0    0x1054
#define ddrm_SecureState_ReadSts0    0x1058

#define ddrm_SecureState_ReadSet1    0x105C
#define ddrm_SecureState_ReadClr1    0x1060
#define ddrm_SecureState_ReadSts1    0x1064

#define ddrm_SecureState_ReadSet2    0x1068
#define ddrm_SecureState_ReadClr2    0x106C
#define ddrm_SecureState_ReadSts2    0x1070

#define ddrm_SecureState_ReadSet3    0x1074
#define ddrm_SecureState_ReadClr3    0x1078
#define ddrm_SecureState_ReadSts3    0x107C


#define ddrm_SecureState_WriteSet0    0x1080
#define ddrm_SecureState_WriteClr0    0x1084
#define ddrm_SecureState_WriteSts0    0x1088

#define ddrm_SecureState_WriteSet1    0x108C
#define ddrm_SecureState_WriteClr1    0x1090
#define ddrm_SecureState_WriteSts1    0x1094

#define ddrm_SecureState_WriteSet2    0x1098
#define ddrm_SecureState_WriteClr2    0x109C
#define ddrm_SecureState_WriteSts2    0x10A0

#define ddrm_SecureState_WriteSet3    0x10A4
#define ddrm_SecureState_WriteClr3    0x10A8
#define ddrm_SecureState_WriteSts3    0x10AC

struct dramfw_reg_secure_t {
	u32 readset;
	u32 readclr;
	u32 writeset;
	u32 writeclr;
};

static struct dramfw_reg_secure_t dramfw_reg_secure_list[] = {
	{ddrm_SecureState_ReadSet0, ddrm_SecureState_ReadClr0,
		ddrm_SecureState_WriteSet0, ddrm_SecureState_WriteClr0},
	{ddrm_SecureState_ReadSet1, ddrm_SecureState_ReadClr1,
		ddrm_SecureState_WriteSet1, ddrm_SecureState_WriteClr1},
	{ddrm_SecureState_ReadSet2, ddrm_SecureState_ReadClr2,
		ddrm_SecureState_WriteSet2, ddrm_SecureState_WriteClr2},
	{ddrm_SecureState_ReadSet3, ddrm_SecureState_ReadClr3,
		ddrm_SecureState_WriteSet3, ddrm_SecureState_WriteClr3}
};

struct noc_info_t {
	const char *desc;
};

static struct noc_info_t noc_initator_id_list[] = {
	{"dmac2_ac97_aux_fifo"},
	{"kas_dram"},
	{"afe_cvd_vip0"},
	{"usp0_axi_i"},
	{"sgx"},
	{"sdr"},
	{"dmac2_usp1rx"},
	{"dmac2_usp1tx"},
	{"usb0"},
	{"usb1"},
	{"dmac2_usp0rx"},
	{"dmac2_usp0tx"},
	{"dmac2_usp2rx"},
	{"dmac2_usp2tx"},
	{"reserved"},
	{"reserved"},
	{"dmac3_iaccrx"},
	{"dmac3_i2s1rx"},
	{"dmac3_i2s1tx"},
	{"dmac3_iacctx2"},
	{"reserved"},
	{"reserved"},
	{"dmac3_ac97rx_fifo"},
	{"dmac3_iacctx0"},
	{"dmac3_iacctx1"},
	{"dmac3_iacctx3"},
	{"dmac3_ac97tx_fifo5"},
	{"dmac3_ac97tx_fifo6"},
	{"dmac3_ac97tx_fifo1"},
	{"dmac3_ac97tx_fifo2"},
	{"dmac3_ac97tx_fifo3"},
	{"dmac3_ac97tx_fifo4"},
	{"dmac4_usp3rx"},
	{"dmac4_usp3tx"},
	{"vpp0"},
	{"vpp1"},
	{"vip1"},
	{"dcu"},
	{"g2d"},
	{"nand"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"dmac4_uart6rx"},
	{"dmac4_uart6tx"},
	{"reserved"},
	{"reserved"},
	{"dmac0_uart4rx"},
	{"dmac0_uart4tx"},
	{"dmac0_uart0tx"},
	{"dmac0_uart0rx"},
	{"dmac0_uart3rx"},
	{"dmac0_uart3tx"},
	{"dmac0_uart2rx"},
	{"dmac0_uart2tx"},
	{"dmac0_uart5rx"},
	{"dmac0_uart5tx"},
	{"sec_secure"},
	{"sec_public"},
	{"dmac0_spi1rx"},
	{"dmac0_spi1tx"},
	{"reserved"},
	{"reserved"},
	{"sys2pci_vdifm"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"sys2pci_mediam"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"armm3_data"},
	{"qspi"},
	{"hash"},
	{"cssi_etr_axi"},
	{"eth_avb"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"lcd0_ly0_rd"},
	{"lcd0_ly1_rd"},
	{"lcd0_ly2_rd"},
	{"lcd0_ly3_rd"},
	{"lcd0_wb_rd"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"lcd1_ly1_rd"},
	{"lcd1_ly1_rd"},
	{"lcd1_ly2_rd"},
	{"lcd1_ly3_rd"},
	{"lcd1_wb_rd"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"vxd_mmu"},
	{"vxd_dmac"},
	{"vxd_vec"},
	{"vxd_dmc"},
	{"vxd_deb"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"jpeg_tar"},
	{"jpeg_code"},
	{"jpeg_thumb"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
};

enum NOC_MACRO_IDX {
	CPUM_IDX = 0,
	CGUM_IDX,
	BTM_IDX,
	GNSSM_IDX,
	GPUM_IDX,
	MEDIAM_IDX,
	VDIFM_IDX,
	AUDIOM_IDX,
	DDRM_IDX,
	RTCM_IDX,
	DRAMFW_IDX,
	SPRFW_IDX,
	UNDEF_IDX,
};

/*register firewall offset based on macro index*/
static u32 noc_regfw_offset_list[10] = {0x1050, 0x50, 0x1050, 0x1050,
		0x1050, 0x1050, 0x2050, 0x2050, 0x2050, 0x2050};

/*dram firewall sched port:six*/
#define FW_A7 0x0
#define FW_DDR_BE 0x4000
#define FW_DDR_RTLL 0x8000
#define FW_DDR_RT   0xC000
#define FW_DDR_SGX 0x10000
#define FW_DDR_VXD 0x14000

#define NOC_MACRO_NUM 12
#define NOC_MACRO_NAME_LEN 12

struct noc_macro {
	void __iomem *mbase;
	spinlock_t		lock;
	u32 idx;
	u32 irq;
	u32 errlogoff;
	u32 faultenoff;
	u32 log_enable:1;	/*if 1, MUST set errlogoff and faultenoff*/
	u32 qos_probe_enable:1; /*if 1, MUST set faultenoff*/
	u32 qos_enable:1;
	char name[NOC_MACRO_NAME_LEN];
	int (*init_macro)(struct platform_device *);
	struct qos_probe_t *qos_probe_tbl;
	u32 qos_probe_size;
	struct platform_device *pdev;
	struct clk *clk;
	struct noc_qos_t *qos_tbl;
	u32 qos_size;
};

/*qos*/
struct noc_qos_t {
	const char *desc;
	u32 reg_offset;
	u32 enabled;
	u32 bw;
	u32 saturation;
	u32 priority;
	u32 mode;
	const char *clock_name;
	u32 divider;	/*default 0 will mean no divider*/
	u32 clkfreqMhz;	/*this should be caculated dynamically*/
	struct clk *clk;
};

#define DEF_PRIO	0x00000404
#define RTLL_PRIO	0x00000707
#define RT_PRIO		0x00000505

static struct noc_qos_t noc_qos_cpum_list[] = {
	{"a7", 0x000, 1, 264, 0x40, DEF_PRIO, 0, "cpum_cpu", 2},/*400M?*/
	{"cssi_etr_axi", 0x080, 0, 264, 0x40, 0x00000504, 1,
		"coresight_cpudiv2"},/*400M?*/
};

static struct noc_qos_t noc_qos_audmscm_list[] = {
	{"dmac2", 0x500, 1, 100, 0x40, RTLL_PRIO, 0, "dmac2_kas"},
	{"dmac3", 0x580, 1, 100, 0x40, RTLL_PRIO, 0, "dmac3_kas"},
	{"audio_afe_cvd_vip0", 0x700, 1, 100, 0x40, RT_PRIO, 0,
		"cvd_io"},/*26M?*/
	{"kas_apb", 0x780, 1, 0, 0x40, RTLL_PRIO, 0, "kas_kas"},/*?*/
	{"kas_axi", 0xa00, 1, 100, 0x40, RTLL_PRIO, 0, "kas_kas"},
	{"usp0_axi", 0xa80, 1, 50, 0x40, RTLL_PRIO, 0, "usp0_kas"},
};

static struct noc_qos_t noc_qos_btm_list[] = {
	{"dmac4", 0x000, 1, 100, 0x40, RTLL_PRIO, 0, "dmac4_io"},
};

static struct noc_qos_t noc_qos_gpum_list[] = {
	{"sgx", 0x000, 1, 100, 0x40, DEF_PRIO, 0, "graphic_gpu"},
	{"sdr", 0x080, 1, 100, 0x40, RTLL_PRIO, 0, "vss_sdr"},
};

static struct noc_qos_t noc_qos_vdifm_list[] = {
	{"dcu", 0x500, 1, 60, 0x40, RT_PRIO, 0, "dcu_deint"},
	{"lcd0_r", 0x600, 1, 100, 0x40, RT_PRIO + 0x202, 0, "lcd0_disp0"},
	{"lcd1_r", 0x700, 1, 100, 0x40, RT_PRIO + 0x202, 0, "lcd1_disp1"},
	{"sys2pci", 0x800, 1, 100, 0x40, RTLL_PRIO, 0, "sys2pci_io"},
	{"vip1", 0xa00, 1, 100, 0x40, RT_PRIO, 0, "vip1_vip"},
	{"vpp0_r", 0xa80, 1, 100, 0x40, RT_PRIO + 0x101, 0, "vpp0_disp0"},
	{"vpp1_r", 0xb80, 1, 100, 0x40, RT_PRIO + 0x101, 0, "vpp1_disp1"},
	{"lcd0_w", 0xc80, 1, 100, 0x40, RT_PRIO + 0x202, 0, "lcd0_disp0"},
	{"lcd1_w", 0xd80, 1, 100, 0x40, RT_PRIO + 0x202, 0, "lcd1_disp1"},
	{"vpp0_w", 0xe80, 1, 100, 0x40, RT_PRIO + 0x101, 0, "vpp0_disp0"},
	{"vpp1_w", 0xf80, 1, 100, 0x40, RT_PRIO + 0x101, 0, "vpp1_disp1"},
};

static struct noc_qos_t noc_qos_mediam_list[] = {
	{"g2d_r", 0x000, 1, 100, 0x40, DEF_PRIO, 0, "g2d_g2d"},
	{"jpeg", 0x080, 1, 100, 0x40, DEF_PRIO, 0, "media_jpenc"},
	{"nand", 0x200, 1, 100, 0x40, DEF_PRIO, 0, "nand_io"},
	{"vxd", 0x280, 1, 100, 0x40, DEF_PRIO, 0, "media_vdec"},
	{"usb0", 0x300, 1, 100, 0x40, DEF_PRIO, 0, "usb0_usb"},
	{"usb1", 0x380, 1, 100, 0x40, DEF_PRIO, 0, "usb1_usb"},
	{"mediam_sys2pci", 0x400, 1, 100, 0x40, DEF_PRIO, 0, "sys2pci2_io"},
	{"g2d_w", 0xa00, 1, 100, 0x40, DEF_PRIO, 0, "g2d_g2d"},
};

static struct noc_qos_t noc_qos_gnssm_list[] = {
	{"dmac0", 0x000, 1, 100, 0x40, DEF_PRIO, 0, "dmac0_io"},
	{"eth_avb", 0x080, 1, 86, 0x40, DEF_PRIO, 0, "gmac_gmac"},/*?300M*/
	{"sec_public", 0x100, 1, 86, 0x40, DEF_PRIO, 0, "ccpub_sec"},
		/*300M?*/
	{"sec_secure", 0x180, 1, 86, 0x40, DEF_PRIO, 0, "ccsec_sec"},
		/*300M?*/
};

static struct noc_qos_t noc_qos_rtcm_list[] = {
	{"armm3_axi", 0x000, 0, 0, 0x40, 0x00000504, 1},
	{"hash", 0x080, 0, 0, 0x40, 0x00000504, 1},
	{"qspi", 0x100, 0, 0, 0x40, 0x00000504, 1},
	{"cssi_system_axi", 0x180, 0, 0, 0x40, 0x00000504, 1},
};

struct QosGenerator_register {
	u32	id_coreid;
	u32	id_revisionid;
	u32	priority;
	u32	mode;
	u32	bw;
	u32	saturation;
	u32	extcontrol;
};

#define QOS_PROBE_SINGLE_PORT 0x55

struct noc_macro_bw_t {
	u64 sum;
	u32 cnt;
	u32 bytes;
	u32 peak;
	u32 cur;
	u32 avg;
};

struct qos_probe_t {
	const char *name;
	u32 macro_offset;
	u32 port;
	u32 mclk;
	u32 period;
	u32 trigger_level;
	struct noc_macro_bw_t *bw;
	/*some probes use the same probe, we should just enable one of them*/
	u32 disabled;
	const char *clock_name;
	u32 divider;
	struct clk *clk;
};

static struct qos_probe_t qos_probe_vdifm_list[] = {
	{"syspci", 0x5000, QOS_PROBE_SINGLE_PORT, 150, 0x1d, 0xfff, NULL, 0,
		"sys2pci_io"},
	{"lcd0", 0x3000, 0, 300, 0x1e, 0xfff, NULL, 0},
	{"vpp0", 0x3000, 1, 300, 0x1e, 0xfff, NULL, 1},
	{"lcd1", 0x4000, 0, 300, 0x1e, 0xfff, NULL},
	{"vpp1", 0x4000, 1, 300, 0x1e, 0xfff, NULL, 1},
	{"vip1", 0x6000, QOS_PROBE_SINGLE_PORT, 300, 0x1e, 0xfff, NULL, 0,
		"vip1_vip"},
	{"dcu", 0, QOS_PROBE_SINGLE_PORT, 300, 0x1e, 0xfff, NULL, 0,
		"dcu_deint"},
};

static struct qos_probe_t qos_probe_ddrm_list[] = {
	{"ddrm", 0x400, QOS_PROBE_SINGLE_PORT, 400, 0x1e, 0xFFFF, NULL},
};

static struct qos_probe_t qos_probe_cpum_list[] = {
	{"a7", 0x400, QOS_PROBE_SINGLE_PORT, 400, 0x1e, 0xfff, NULL},
};

static struct qos_probe_t qos_probe_audiom_list[] = {
	{"vip0", 0, QOS_PROBE_SINGLE_PORT, 200, 0x1d, 0xfff, NULL},
	{"dmac2", 0x3000, 1, 200, 0x1d, 0xfff, NULL, 1},
	{"dmac3", 0x3000, 0, 200, 0x1d, 0xfff, NULL, 1},
	{"kas", 0x3000, 3, 200, 0x1d, 0xfff, NULL, 1},
	{"usp0", 0x3000, 2, 200, 0x1d, 0xfff, NULL},
};

static struct qos_probe_t qos_probe_btm_list[] = {
	{"dmac4", 0x400, QOS_PROBE_SINGLE_PORT, 150, 0x1d, 0xfff, NULL},
};

static struct qos_probe_t qos_probe_gnssm_list[] = {
	{"dmac0", 0x800, QOS_PROBE_SINGLE_PORT, 300, 0x1e, 0xfff, NULL, 0,
		"gmac_gmac"},
	{"eth", 0x2000, QOS_PROBE_SINGLE_PORT, 150, 0x1d, 0xfff, NULL},
};

static struct qos_probe_t qos_probe_gpum_list[] = {
	{"sgx", 0x400, QOS_PROBE_SINGLE_PORT, 300, 0x1e, 0xfff, NULL, 0,
		"graphic_gpu"},
	{"sdr", 0x2000, QOS_PROBE_SINGLE_PORT, 200, 0x1d, 0xfff, NULL, 0,
		"vss_sdr"},
};

static struct qos_probe_t qos_probe_mediam_list[] = {
	{"g2d", 0xc00, QOS_PROBE_SINGLE_PORT, 150, 0x1d, 0xfff, NULL, 0,
		"g2d_g2d"},
	{"vxd", 0x2000, QOS_PROBE_SINGLE_PORT, 150, 0x1d, 0xfff, NULL, 0,
		"media_vdec"},
};

/*reg fw*/
struct QosProbe_regs_t {
	u32 Id_CoreId;
	u32 Id_RevisionId;
	u32 MainCtl;
	u32 CfgCtl;
	u32 TracePortSel;
	u32 reserved[4];
	u32 StatPeriod;
	u32 StatGo;
	u32 StatAlarmMin;
	u32 StatAlarmMax;
	u32 StatAlarmStatus;
	u32 StatAlarmClr;
	u32 StatAlarmEn;
	u32 reserved1[61];
	u32 Counters_0_PortSel;
	u32 Counters_0_Src;
	u32 Counters_0_AlarmMode;
	u32 Counters_0_Val;
	u32 reserved2;
	u32 Counters_1_PortSel;
	u32 Counters_1_Src;
	u32 Counters_1_AlarmMode;
	u32 Counters_1_Val;
	u32 reserved3;
	u32 Counters_2_PortSel;
	u32 Counters_2_Src;
	u32 Counters_2_AlarmMode;
	u32 Counters_2_Val;
	u32 reserved4;
	u32 Counters_3_PortSel;
	u32 Counters_3_Src;
	u32 Counters_3_AlarmMode;
	u32 Counters_3_Val;

};

static int noc_macro_init(struct platform_device *);
static int noc_spram_firewall_init(struct platform_device *);
static int noc_dram_firewall_init(struct platform_device *);
static int noc_a7_init(struct platform_device *);
static void noc_handle_qos_macro_probe(struct noc_macro *nocm);

static struct noc_macro noc_macro_list[] = {
	{
		.name = "cpum",
		.idx = CPUM_IDX,
		.errlogoff = NOC_CPUM_ERRLOG,
		.faultenoff = NOC_CPUM_FAULTEN,
		.init_macro = noc_a7_init,
		.qos_probe_enable = 1,
		.qos_probe_tbl = &qos_probe_cpum_list[0],
		.qos_probe_size = ARRAY_SIZE(qos_probe_cpum_list),
		.qos_enable = 1,
		.qos_tbl = &noc_qos_cpum_list[0],
		.qos_size = ARRAY_SIZE(noc_qos_cpum_list),
	}, {
		.name = "cgum",
		.idx = CGUM_IDX,
	}, {
		.name = "btm",
		.idx = BTM_IDX,
		.faultenoff = 0x200,
		.init_macro = noc_macro_init,
		.qos_probe_enable = 1,
		.qos_probe_tbl = &qos_probe_btm_list[0],
		.qos_probe_size = ARRAY_SIZE(qos_probe_btm_list),
		.qos_enable = 1,
		.qos_tbl = &noc_qos_btm_list[0],
		.qos_size = ARRAY_SIZE(noc_qos_btm_list),

	}, {
		.name = "gnssm",
		.idx = GNSSM_IDX,
		.faultenoff = 0x600,
		.init_macro = noc_macro_init,
		.qos_probe_enable = 1,
		.qos_probe_tbl = &qos_probe_gnssm_list[0],
		.qos_probe_size = ARRAY_SIZE(qos_probe_gnssm_list),
		.qos_enable = 1,
		.qos_tbl = &noc_qos_gnssm_list[0],
		.qos_size = ARRAY_SIZE(noc_qos_gnssm_list),
	}, {
		.name = "gpum",
		.idx = GPUM_IDX,
		.errlogoff = 0x280,
		.faultenoff = 0x800,
		.init_macro = noc_macro_init,
		.qos_probe_enable = 1,
		.qos_probe_tbl = &qos_probe_gpum_list[0],
		.qos_probe_size = ARRAY_SIZE(qos_probe_gpum_list),
		.qos_enable = 1,
		.qos_tbl = &noc_qos_gpum_list[0],
		.qos_size = ARRAY_SIZE(noc_qos_gpum_list),
	}, {
		.name = "mediam",
		.idx = MEDIAM_IDX,
		.errlogoff = 0xb00,
		.faultenoff = 0x900,
		.init_macro = noc_macro_init,
		.qos_probe_enable = 1,
		.qos_probe_tbl = &qos_probe_mediam_list[0],
		.qos_probe_size = ARRAY_SIZE(qos_probe_mediam_list),
		.qos_enable = 1,
		.qos_tbl = &noc_qos_mediam_list[0],
		.qos_size = ARRAY_SIZE(noc_qos_mediam_list),
	}, {
		.name = "vdifm",
		.idx = VDIFM_IDX,
		.faultenoff = 0x400,
		.init_macro = noc_macro_init,
		.qos_probe_enable = 1,
		.qos_probe_tbl = &qos_probe_vdifm_list[0],
		.qos_probe_size = ARRAY_SIZE(qos_probe_vdifm_list),
		.qos_enable = 1,
		.qos_tbl = &noc_qos_vdifm_list[0],
		.qos_size = ARRAY_SIZE(noc_qos_vdifm_list),
	}, {
		.name = "audiom",
		.idx = AUDIOM_IDX,
		.errlogoff = NOC_AUDMSCM_ERRLOG,
		.faultenoff = NOC_AUDMSCM_FAULTEN,
		.init_macro = noc_macro_init,
		.log_enable = 1,
		.qos_probe_enable = 1,
		.qos_probe_tbl = &qos_probe_audiom_list[0],
		.qos_probe_size = ARRAY_SIZE(qos_probe_audiom_list),
		.qos_enable = 1,
		.qos_tbl = &noc_qos_audmscm_list[0],
		.qos_size = ARRAY_SIZE(noc_qos_audmscm_list),
	}, {
		.name = "ddrm",
		.idx = DDRM_IDX,
		.errlogoff = NOC_DDRM_ERRLOG,
		.faultenoff = NOC_DDRM_FAULTEN,
		.init_macro = noc_macro_init,
		.log_enable = 1,
		.qos_probe_enable = 1,
		.qos_probe_tbl = &qos_probe_ddrm_list[0],
		.qos_probe_size = ARRAY_SIZE(qos_probe_ddrm_list),
	}, {
		.name = "rtcm",
		.idx = RTCM_IDX,
		.errlogoff = NOC_RTCM_ERRLOG,
		.faultenoff = NOC_RTCM_FAULTEN,
		.init_macro = noc_macro_init,
		.log_enable = 1,
		.qos_enable = 1,
		.qos_tbl = &noc_qos_rtcm_list[0],
		.qos_size = ARRAY_SIZE(noc_qos_rtcm_list),
	}, {
		.name = "dramfw",
		.idx = DRAMFW_IDX,
		.init_macro = noc_dram_firewall_init,
	}, {
		.name = "spramfw",
		.idx = SPRFW_IDX,
		.init_macro = noc_spram_firewall_init,
	},
};



struct noc_dram_params_t {
	void __iomem *mbase;
	u32 startaddr;
	u32 endaddr;
	u32 initiator;
	u32 access;
	u32 initiator_access;
	u32 rpnum;
	u32 flags;
};

#define to_noc_fw_inf(d) container_of(d, struct noc_fw_inf, dev)

static struct noc_info_t noc_err_list[] = {
	{"target error detected by slave"},
	{"address decode error"},
	{"unsupported request"},
	{"power disconnect"},
	{"security violation"},
	{"hidden security violation"},
	{"timout"},
	{"reserved"},
};

static struct noc_info_t noc_cpu_list[] = {
	{"A7"},
	{"coresight"},
	{"M3"},
	{"KAS"},
};

static struct noc_info_t noc_opc_list[] = {
	{"read"},
	{"wrap read"},
	{"link read"},
	{"exclusive read"},
	{"write"},
	{"wrap write"},
	{"condition write"},
	{"reserved"},
	{"preable packet"},
	{"urgency packet"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
	{"reserved"},
};

struct probe_global_cfg_t {
	u32 period_en:1;
	u32 max_en:1;
	u32 probe_active:1;
	u32 nocm;
	u32 period;
	u32 max;
	u32 mode;
	u32 port_rotate;
	u32 probe_event;
};

static struct probe_global_cfg_t probe_cfg;

/*data abort handler can not get base list*/

static int noc_has_err(void __iomem *noc_errlog_mbase)
{
	u32 vld;

	vld = readl_relaxed(noc_errlog_mbase + ERRORLOGGER_0_ERRVLD);
	vld &= 0x1;
	/* 1 indicates an error has been logged (default: 0x0) */
	return vld;
}

/*
 * CAUTION: gpum, audiom don't have ERRORLOGGER_0_ERRLOG5 register!!!
 * when their error log is enabled, this function should be modified!!!
 */
static int noc_dump_errlog(struct noc_macro *nocm)
{
	u32 errCode0, errCode1, errCode3, errCode5, vld;
	void __iomem *noc_errlog_mbase;

	if (!nocm)
		goto err;

	noc_errlog_mbase = (void __iomem *)(nocm->mbase + nocm->errlogoff);

	/*return 1 for normal abort handler */
	vld = noc_has_err(noc_errlog_mbase);
	if (0 == vld)
		goto err;

	errCode0 = readl_relaxed(noc_errlog_mbase + ERRORLOGGER_0_ERRLOG0);
	errCode1 = readl_relaxed(noc_errlog_mbase + ERRORLOGGER_0_ERRLOG1);
	errCode3 = readl_relaxed(noc_errlog_mbase + ERRORLOGGER_0_ERRLOG3);
	errCode5 = readl_relaxed(noc_errlog_mbase + ERRORLOGGER_0_ERRLOG5);

	/*error type*/
	pr_info("err:\t%s\n", noc_err_list[(errCode0>>8) & 0x7].desc);

	/*initiator id*/
	if (nocm->idx == CPUM_IDX)
		pr_info("ID:\t%s\n", noc_cpu_list[(errCode5>>3) & 0x3].desc);
	else	if (0 == (errCode5 & 0x1))
		pr_info("ID:\t%s\n", noc_cpu_list[(errCode5>>2) & 0x3].desc);
	else
		pr_info("ID:\%s\n", noc_initator_id_list[(errCode5>>7 & 0x1F)
				| ((errCode5>>2 & 0x3)<<5)].desc);

	pr_info("Opc:\t%s\n", noc_opc_list[(errCode0>>1) & 0xF].desc);
	pr_info("Addr\t%08x\n", errCode3);
	pr_info("Len\t%08x\n", errCode0>>16 & 0x3F);

	/* clear the NoC errlog */
	writel_relaxed(0x1, noc_errlog_mbase + ERRORLOGGER_0_ERRCLR);

	return 0;
err:
	return 1;
}

static int noc_abort_handler(unsigned long addr, unsigned int fsr,
		struct pt_regs *regs)
{
	int ret;

	ret = noc_dump_errlog(&noc_macro_list[CPUM_IDX]);
	if (0 != ret)
		return 1;
	/*
	* If it was not an imprecise abort (Bit10==0),
	* then we need to correct the
	* return address to be _after_ the instruction.
	*/
	if (!(fsr & (1 << 10)))
		regs->ARM_pc += 4;

	return 0;
}

static void noc_qos_probe_stop(struct noc_macro *nocm)
{
	struct QosProbe_regs_t	 *probe_reg;
	struct qos_probe_t *entry;
	u32 i;

	for (i = 0; i < nocm->qos_probe_size; i++) {
		entry = nocm->qos_probe_tbl + i;
		probe_reg = (struct QosProbe_regs_t	*)
				(nocm->mbase + entry->macro_offset);

		/*clear field GlobalEn enable the counting of bytes.*/
		writel_relaxed(0, &probe_reg->CfgCtl);

		/*clear staten, alarmen, */
		writel_relaxed(readl_relaxed(&probe_reg->MainCtl) & ~0x18,
				&probe_reg->MainCtl);
		writel_relaxed(0, &probe_reg->StatAlarmEn);
	}
}

/*
 * get appropriate period for different nocms according to their clock
 * compared to ddrm clock, to make them have approximately the same alram time
 */
static u32 noc_probe_get_period(struct noc_macro *nocm,
	struct qos_probe_t *entry)
{
	u32 j;
	u32 period = entry->period;

	if (probe_cfg.period_en) {
		if (nocm->idx == DDRM_IDX)
			period = probe_cfg.period;
		else {
			period = noc_macro_list[DDRM_IDX].qos_probe_tbl[0].mclk
				/ entry->mclk;
			for (j = 1; j < 32; j++)
				if (period>>j == 0)
					break;
			period = probe_cfg.period - j + 1;
		}
	}

	return period;
}

static void noc_qos_probe_init(struct noc_macro *nocm)
{
	struct QosProbe_regs_t	 *probe_reg;
	struct qos_probe_t *entry;
	u32 i;
	u32 period;
	int ret;

	for (i = 0; i < nocm->qos_probe_size; i++) {
		entry = nocm->qos_probe_tbl + i;
		probe_reg = (struct QosProbe_regs_t	*)
				(nocm->mbase + entry->macro_offset);

		if (entry->disabled)
			continue;

		if (entry->clock_name) {
			if (entry->clk == NULL) {
				entry->clk = devm_clk_get(&nocm->pdev->dev,
					entry->clock_name);
				if (IS_ERR(entry->clk)) {
					pr_err("%s: failed get clk of %s!\n",
						__func__, entry->clock_name);
					entry->clk = NULL;
					continue;
				}
			}

			ret = clk_prepare_enable(entry->clk); /* fixme: check the ret */
		}

		if (entry->bw == NULL) {
			entry->bw = devm_kzalloc(&nocm->pdev->dev,
					sizeof(struct noc_macro_bw_t),
					GFP_KERNEL);
			if (entry->bw == NULL)
				return;
		}
		/*re-statistics*/
		memset(entry->bw, 0, sizeof(*entry->bw));

		writel_relaxed(readl_relaxed(&probe_reg->MainCtl) | BIT(3),
			&probe_reg->MainCtl);

		/*
		* Only if The table above contain port number:
		* Set register Counters_0_PortSel to the value
		* corresponding to the probe point of interest.
		* no need , A& probe doesnt have more than one port
		*/
		if (entry->port != QOS_PROBE_SINGLE_PORT)
			writel_relaxed(entry->port,
				&probe_reg->Counters_0_PortSel);

		/* Set register Counters_0_Src to 0x8 (BYTES) to count bytes.*/
		writel_relaxed(probe_cfg.probe_event,
			&probe_reg->Counters_0_Src);

		/*
		* Set register Counters_1_Src to 0x10 (CHAIN)
		* to increment when counter 0 wraps.
		*/
		writel_relaxed(0x10, &probe_reg->Counters_1_Src);

		/*
		* Setting register StatPeriod to 2^period cycles.
		* also can config to 0x00 ( manual mode )
		*/
		period = noc_probe_get_period(nocm, entry);
		writel_relaxed(period, &probe_reg->StatPeriod);
		pr_info("%s(%dMHz): period=0x%x\n", entry->name, entry->mclk,
			period);

		/*alarm mode, chained*/
		writel_relaxed(probe_cfg.mode,
			&probe_reg->Counters_0_AlarmMode);
		writel_relaxed(0, &probe_reg->Counters_1_AlarmMode);

		/*set alarmMax and Min*/
		if (nocm->idx == DDRM_IDX)
			writel_relaxed(0xFFFFF, &probe_reg->StatAlarmMax);
		else
			writel_relaxed(0xFFF, &probe_reg->StatAlarmMax);
		writel_relaxed(0, &probe_reg->StatAlarmMin);

		if (probe_cfg.max_en) {
			if (probe_cfg.mode == 1)
				writel_relaxed(probe_cfg.max,
					&probe_reg->StatAlarmMin);
			else if (probe_cfg.mode == 2)
				writel_relaxed(probe_cfg.max,
					&probe_reg->StatAlarmMax);
			pr_info("%s: max=0x%x, mode=%d\n", entry->name,
				probe_cfg.max, probe_cfg.mode);
		}

		/*trigger alarm any time*/
		if (probe_cfg.mode == 3) {
			writel_relaxed(0xffffffff, &probe_reg->StatAlarmMin);
			writel_relaxed(0x0, &probe_reg->StatAlarmMax);
		}
		pr_info("%s: mode=%d, event=0x%x\n", entry->name,
			probe_cfg.mode, probe_cfg.probe_event);

		/*enable alm*/
		writel_relaxed(readl_relaxed(&probe_reg->MainCtl) | 0x10,
				&probe_reg->MainCtl);
		writel_relaxed(1, &probe_reg->StatAlarmEn);

		/*Set field GlobalEn enable the counting of bytes.*/
		writel(1, &probe_reg->CfgCtl);
	}
}

/*
 * some masters use different ports of the same probe,
 * so need rotate ports to get all masters' values
 */
static void noc_probe_port_rotate(struct noc_macro *nocm, u32 index)
{
	struct qos_probe_t *entry = NULL;
	u32 offset = 0;
	u32 i = 0;
	struct QosProbe_regs_t	 *probe_reg = NULL;

	if (index >= nocm->qos_probe_size)
		return;

	entry = nocm->qos_probe_tbl + index;
	if (entry->port == QOS_PROBE_SINGLE_PORT)
		return;

	entry->disabled = 1;
	offset = entry->macro_offset;
	probe_reg = (struct QosProbe_regs_t	*)
			(nocm->mbase + entry->macro_offset);

	for (i = index + 1; i < nocm->qos_probe_size; i++) {

		entry = nocm->qos_probe_tbl + i;
		if (entry->port == QOS_PROBE_SINGLE_PORT)
			continue;

		if (entry->macro_offset == offset) {
			entry->disabled = 0;
			writel_relaxed(entry->port,
				&probe_reg->Counters_0_PortSel);
			return;
		}
	}

	for (i = 0; i < index; i++) {

		entry = nocm->qos_probe_tbl + i;
		if (entry->port == QOS_PROBE_SINGLE_PORT)
			continue;

		if (entry->macro_offset == offset) {
			entry->disabled = 0;
			writel_relaxed(entry->port,
				&probe_reg->Counters_0_PortSel);
			return;
		}
	}
}

static void noc_handle_qos_macro_probe(struct noc_macro *nocm)
{
	struct QosProbe_regs_t	 *probe_reg;
	struct qos_probe_t *entry;
	struct noc_macro_bw_t *bw;
	u32 i;
	u64 val;

	for (i = 0; i < nocm->qos_probe_size; i++) {

		entry = nocm->qos_probe_tbl + i;
		if (entry->disabled)
			continue;

		probe_reg = (struct QosProbe_regs_t	*)
			(nocm->mbase + entry->macro_offset);

		 /*for manual mode:writel_relaxed(1, &probe_reg->StatGo);*/
		if (!readl(&probe_reg->StatAlarmStatus))
			continue;

		val = (readl_relaxed(&probe_reg->Counters_1_Val) << 16) |
				readl_relaxed(&probe_reg->Counters_0_Val);
		if (probe_cfg.port_rotate && val == 0)
			goto next;

		if (!entry->bw) {
			entry->bw = devm_kzalloc(&nocm->pdev->dev,
					sizeof(struct noc_macro_bw_t),
					GFP_KERNEL | GFP_ATOMIC);
			if (!entry->bw)
				goto next;
		}

		bw = entry->bw;
		bw->bytes = val;
		val *= entry->mclk;
		bw->cur = do_div(val,
			(1 << readl_relaxed(&probe_reg->StatPeriod)));
		bw->cur = val;
		bw->peak = max(bw->cur, bw->peak);
		/*overflow?*/
		if (bw->cnt + 1 < bw->cnt || bw->sum + bw->cur < bw->sum) {
			bw->cnt = 0;
			bw->sum = 0;
		}
		bw->cnt++;
		bw->sum += bw->cur;
		val = bw->sum;
		bw->avg = do_div(val,
			bw->cnt);
		bw->avg = val;

		pr_info("%s-%s:%x,%d/%d/%d\n",
			nocm->name,
			entry->name,
			bw->bytes,
			bw->cur,
			bw->peak,
			bw->avg);

next:
		if (probe_cfg.port_rotate
			&& entry->port != QOS_PROBE_SINGLE_PORT)
			noc_probe_port_rotate(nocm, i);

		/*clr the alm*/
		writel(1, &probe_reg->StatAlarmClr);
	}
}

/*handler noc audio macro interrupt*/
static irqreturn_t noc_irq_handle(int irq, void *data)
{
	struct noc_macro *nocm = (struct noc_macro *)data;
	u32 val, val2;
	/*sb_flaginstatus*/
	val = readl_relaxed(nocm->mbase + nocm->faultenoff + 0x14);
	/*sb_faultstatus*/
	val2 = readl_relaxed(nocm->mbase + nocm->faultenoff + 0x0C);
	/*pr_info("nocm1:%s, 0x%x, 0x%x\n", nocm->name, val, val2);*/
	if (nocm->log_enable)
		noc_dump_errlog(nocm);

	if (nocm->qos_probe_enable)
		noc_handle_qos_macro_probe(nocm);

	return IRQ_HANDLED;
}

static void noc_fault_enable(struct noc_macro *nocm)
{
	writel_relaxed(0x1, nocm->mbase +
		nocm->faultenoff + NOC_SB_FAULTEN);
	/*
	 * rtcm_sb_main_SidebandManager_FlagInEn0
	 * 0  StatAlarm  rtcm_probe  Statistics alarm
	 * 1  Fault  rtcm_observer  Error logging event
	 */
	writel_relaxed(0xffff, nocm->mbase +
		nocm->faultenoff + NOC_SB_FLAGINEN0);
	if (nocm->log_enable)
		writel_relaxed(0x1, nocm->mbase +
			nocm->errlogoff + ERRORLOGGER_0_FAULTEN);
}

static void noc_dramfw_cpu_set(void __iomem *fw_cpu_clr,
			void __iomem *fw_cpu_set, u32 initiator, u32 access)
{
	/*
	 * clear all except r_CA7 and w_CA7,set r_CA7 and w_CA7
	 * r_KAS r_CM3 r_CSSI r_CA7 w_KAS w_CM3 w_CSSI w_CA7
	 */
	writel_relaxed(0xFFFFFFFF, fw_cpu_clr);
	if (access & ACCESS_READ)
		writel_relaxed(1<<(initiator+4), fw_cpu_set);

	if (access & ACCESS_WRITE)
		writel_relaxed(1<<initiator, fw_cpu_set);
}

static void noc_dramfw_noncpu_acess_set(struct dramfw_regs_t *base,
		void __iomem *ddrm_addr, u32 initiator, u32 access,
		u32 initiator_access, u32 flags)
{
	u32 i = 0;
	u32 val;

	/* non-cpu initiator */
	for (i = 0; i < 4; i++) {
		writel_relaxed(0xFFFFFFFF,
				&base->access[i].initiator_r_clr);
		writel_relaxed(0xFFFFFFFF,
				&base->access[i].initiator_w_clr);
	}

	i = initiator / 32;
	val = 1<<(initiator - 32 * i);
	/* dram access read/write */
	if (access & ACCESS_READ)
		writel_relaxed(val, &base->access[i].initiator_r_set);
	if (access & ACCESS_WRITE)
		writel_relaxed(val, &base->access[i].initiator_w_set);

	/* initiator access read/write */
	if (initiator_access & ACCESS_INITIATOR_READ) {
		if (FLAGS_INITIATOR_S & flags)
			writel_relaxed(val,
				ddrm_addr +
				dramfw_reg_secure_list[i].readclr);
		else if (FLAGS_INITIATOR_NS & flags)
			writel_relaxed(val,
				ddrm_addr +
			dramfw_reg_secure_list[i].readset);
	}
	if (initiator_access & ACCESS_INITIATOR_WRITE) {
		if (FLAGS_INITIATOR_S & flags)
			writel_relaxed(val,
				ddrm_addr +
			dramfw_reg_secure_list[i].writeclr);
		else if (FLAGS_INITIATOR_NS & flags)
			writel_relaxed(val,
				ddrm_addr +
			dramfw_reg_secure_list[i].writeset);
	}
}
static void noc_dramfw_noncpu_secure_set(struct dramfw_regs_t *base,
		u32 access, u32 flags)
{
	/*
	 *clear AxPROT[0] and AxPROT[2] enable,set AxPROT[1]
	 * arprot_2 arprot_1 arprot_0 r.awprot_2 awprot_1 awprot_0
	 */
	writel_relaxed(0x00000077, &base->prot_clr);
	writel_relaxed(0x000000FF, &base->prot_val_clr);

	/*
	 * r_strict	arprot_2 arprot_1 arprot_0 w_strict
	 *awprot_2 awprot_1 awprot_0
	 */

	if (access & ACCESS_READ) {
		writel_relaxed(0x00000020, &base->prot_set);

		if (FLAGS_STRICT & flags)
			writel_relaxed(0x00000080, &base->prot_val_set);

		if (FLAGS_NS & flags)
			writel_relaxed(0x00000020, &base->prot_val_set);
	}

	if (access & ACCESS_WRITE) {
		writel_relaxed(0x00000002, &base->prot_set);

		if (FLAGS_STRICT & flags)
			writel_relaxed(0x00000008, &base->prot_val_set);

		if (FLAGS_NS & flags)
			writel_relaxed(0x00000002, &base->prot_val_set);

	}
}

static void noc_dramfw_set(struct noc_dram_params_t *params)
{
	struct dramfw_regs_t *base;
	struct noc_macro *nocm;
	void __iomem *mbase;
	u32 startaddr;
	u32 endaddr;
	u32 initiator;
	u32 access;
	u32 initiator_access;
	u32 rpnum;
	u32 flags;

	mbase = params->mbase;
	startaddr = params->startaddr;
	endaddr = params->endaddr;
	initiator = params->initiator;
	access = params->access;
	initiator_access = params->initiator_access;
	rpnum = params->rpnum;
	flags = params->flags;

	base = (struct dramfw_regs_t *)(mbase +
		0x100 * rpnum);

	initiator &= 0xFF;

	writel_relaxed(startaddr, &base->start);
	writel_relaxed(endaddr, &base->end);

	if (flags & FLAGS_CPU)
		noc_dramfw_cpu_set(&base->fw_cpu_clr,
			&base->fw_cpu_set, initiator, access);
	else {
		nocm = &noc_macro_list[DDRM_IDX];
		noc_dramfw_noncpu_acess_set(base,
				nocm->mbase,
				initiator, access, initiator_access, flags);
	}
	noc_dramfw_noncpu_secure_set(base, access, flags);

	/* last step enable rp */
	writel_relaxed(1<<rpnum, mbase + FW_RP_ENABLE_SET);
}

static void noc_regfw_setval(void __iomem *addr_clr,
			void __iomem *addr_set, u32 val)
{
	writel_relaxed(0xFFFFFFFF, addr_clr);
	writel_relaxed(val, addr_set);
}

static void noc_regfw_set(void __iomem *mbase, u32 off, u32 ns,
				u32 a7, u32 cssi, u32 m3, u32 kas)
{
	struct regfw_regs_t *base;

	base = (struct regfw_regs_t *)(mbase + off);
	noc_regfw_setval(&base->ns_clr, &base->ns_set, ns);
	noc_regfw_setval(&base->a7_clr, &base->a7_set, a7);
	noc_regfw_setval(&base->m3_clr, &base->m3_set, m3);
	noc_regfw_setval(&base->cssi_clr, &base->cssi_set, cssi);
	noc_regfw_setval(&base->kas_clr, &base->kas_set, kas);
}

static ssize_t dramfw_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct noc_dram_params_t params;
	struct noc_macro *nocm;
	u32 schedport_off;

	if (sscanf(buf, "%x %x %x %x %x %x %x %x\n",
				&schedport_off, &params.startaddr,
				&params.endaddr, &params.initiator,
				&params.access, &params.initiator_access,
				&params.rpnum, &params.flags) != 8)
		return -EINVAL;

	nocm = &noc_macro_list[DRAMFW_IDX];
	params.mbase = nocm->mbase + schedport_off;

	noc_dramfw_set(&params);
	return len;
}
static DEVICE_ATTR_WO(dramfw);

static ssize_t spramfw_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct noc_macro *nocm;
	struct noc_dram_params_t params;

	if (sscanf(buf, "%x %x %x %x %x %x %x\n",
				&params.startaddr, &params.endaddr,
				&params.initiator, &params.access,
				&params.initiator_access,
				&params.rpnum, &params.flags) != 7)

		return -EINVAL;
	nocm = &noc_macro_list[SPRFW_IDX];
	params.mbase = nocm->mbase;
	noc_dramfw_set(&params);
	return len;
}
static DEVICE_ATTR_WO(spramfw);

static ssize_t regfw_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct noc_macro *nocm;

	u32 idx, ns, a7, cssi, m3, kas;

	if (sscanf(buf, "%x %x %x %x %x %x\n", &idx, &ns, &a7,
				&cssi, &m3, &kas) != 6 || idx > 9)
		return -EINVAL;

	nocm = &noc_macro_list[idx];
	noc_regfw_set(nocm->mbase,
			noc_regfw_offset_list[idx], ns, a7, cssi, m3, kas);
	return len;
}

static DEVICE_ATTR_WO(regfw);

static void QosGenerator_Get(struct noc_qos_t *entry,
		struct noc_macro *nocm)
{
	u32 bw, extcontrol;
	struct QosGenerator_register *qos_reg =
		(struct QosGenerator_register *)(nocm->mbase +
		entry->reg_offset);
	int ret;

	if (entry->clock_name) {
		if (entry->clk == NULL) {
			entry->clk = devm_clk_get(&nocm->pdev->dev,
				entry->clock_name);
			if (IS_ERR(entry->clk)) {
				pr_err("%s: failed to get clock of %s!\n",
					__func__, entry->clock_name);
				entry->clk = NULL;
				return;
			}
		}

		ret = clk_prepare_enable(entry->clk);
		if (ret) {
			pr_err("%s: failed clk_prepare_enable %s!\n",
				__func__, entry->clock_name);
			return;
		}
		entry->clkfreqMhz = clk_get_rate(entry->clk) / 1000000;
	}

	bw = readl_relaxed(&qos_reg->bw);
	entry->bw = bw * entry->clkfreqMhz / 256;
	entry->mode = readl_relaxed(&qos_reg->mode);
	entry->saturation = readl_relaxed(&qos_reg->saturation);
	entry->priority = readl_relaxed(&qos_reg->priority);
	extcontrol = readl_relaxed(&qos_reg->extcontrol);
	pr_info("get: %s qos values:  %d(reg=0x%x, freq=%dM), 0x%x, 0x%x, \
		0x%x, 0x%x\n", entry->desc, entry->bw, bw, entry->clkfreqMhz,
		entry->priority, entry->mode, entry->saturation, extcontrol);

	if (!IS_ERR(entry->clk))
		clk_disable_unprepare(entry->clk);

}

static void QosGenerator_Set(struct noc_qos_t *entry,
		struct noc_macro *nocm)
{
	u32 bw;
	struct QosGenerator_register *qos_reg =
		(struct QosGenerator_register *)(nocm->mbase +
		entry->reg_offset);
	int ret;

	if (entry->clock_name) {
		if (entry->clk == NULL) {
			entry->clk = devm_clk_get(&nocm->pdev->dev,
				entry->clock_name);
			if (IS_ERR(entry->clk)) {
				pr_err("%s: failed to get clock of %s!\n",
					__func__, entry->clock_name);
				entry->clk = NULL;
				return;
			}
		}

		ret = clk_prepare_enable(entry->clk);
		if (ret) {
			pr_err("%s: failed to clk_prepare_enable %s!\n",
				__func__, entry->clock_name);
			return;
		}
		entry->clkfreqMhz = clk_get_rate(entry->clk) / 1000000;
	}

	bw = entry->bw * 256 / entry->clkfreqMhz;
	writel_relaxed(bw, &qos_reg->bw);
	writel_relaxed(entry->mode, &qos_reg->mode);
	writel_relaxed(entry->saturation, &qos_reg->saturation);
	writel_relaxed(entry->priority, &qos_reg->priority);
	writel_relaxed(0, &qos_reg->extcontrol);
	pr_info("set: %s qos values read:  0x%x, 0x%x, 0x%x, 0x%x\n",
		entry->desc, readl_relaxed(&qos_reg->bw),
		readl_relaxed(&qos_reg->priority),
		readl_relaxed(&qos_reg->mode),
		readl_relaxed(&qos_reg->saturation));
	QosGenerator_Get(entry, nocm);

	if (!IS_ERR(entry->clk))
		clk_disable_unprepare(entry->clk);

}

static void QosGenerator_init(struct noc_macro *nocm)
{
	struct noc_qos_t *entry;
	int j;

	do {
		if (!(nocm->qos_tbl) || !nocm->qos_enable)
			break;

		for (j = 0; j < nocm->qos_size; j++) {
			entry = nocm->qos_tbl + j;
			if (entry->enabled)
				QosGenerator_Set(entry, nocm);
		}
	} while (0);

	do {
		if (!(nocm->qos_tbl))
			break;

		for (j = 0; j < nocm->qos_size; j++) {
			entry = nocm->qos_tbl + j;
			if (!entry->enabled)
				QosGenerator_Get(entry, nocm);
		}
	} while (0);
}

static ssize_t QosGenerator_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct noc_macro *nocm;
	struct noc_qos_t *entry;
	int i, j, pos = 0;

	pos += scnprintf(buf + pos,
		PAGE_SIZE - pos,
		"Niu:\tbw\tpriority\tmode\tsaturation\tclkfreqMhz\n");

	for (i = 0; i < ARRAY_SIZE(noc_macro_list); i++) {
		nocm = &noc_macro_list[i];
		if (!(nocm->qos_tbl))
			continue;

		pos += scnprintf(buf + pos,
			PAGE_SIZE - pos,
			"%s->:\n",
			nocm->name);

		for (j = 0; j < nocm->qos_size; j++) {
			entry = nocm->qos_tbl + j;
			pos += scnprintf(buf + pos,
				PAGE_SIZE - pos,
				"%s\t%dMBps\t0x%x\t%d\t0x%x\t%dM\n",
				entry->desc,
				entry->bw,
				entry->priority,
				entry->mode,
				entry->saturation,
				entry->clkfreqMhz);

		}
	}

	return pos;
}

static void QosGenerator_store_usage(void)
{
	u32 i, j, bit = 0, pos = 0;
	struct noc_macro *nocm;
	struct noc_qos_t *entry;
	char *table = NULL;

	pr_info("QosGenerator_store_usage:\n");
	pr_info("\techo 2 nocm qosbox bw priority mode saturation: set a \
		qosbox's parameters\n");
	pr_info("\t\tnocm: bitwise, see nocm-qosbox table below.\n");
	pr_info("\t\tqosbox: bitwise, see nocm-qosbox table below.\n");
	pr_info("\t\tbw: MBps.\n");
	pr_info("\t\tpriority: P1[15:8]|P0[7:0]\n");
	pr_info("\t\tmode : 0=fixed, 1=limiter, 2=bypass, 3=regulator.\n");
	pr_info("\t\tsaturation : bursty window bytes, 16*saturation.\n");

	pr_info("\techo ? : prompt this usage\n");
	pr_info("\tAppendix: nocm-qosbox(bitwise, freqency) table:\n");
	pr_info("\t\tNOTE: table values may change due to driver update!!!\n");

	table = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (table == NULL)
		return;

	for (i = 0; i < ARRAY_SIZE(noc_macro_list); i++) {
		nocm = &noc_macro_list[i];
		if (!(nocm->qos_tbl))
			continue;

		pos += scnprintf(table + pos,
			PAGE_SIZE - pos,
			"\t\t%s(0x%x) - ",
			nocm->name,
			1<<bit);

		for (j = 0; j < nocm->qos_size; j++) {
			entry = nocm->qos_tbl + j;

			pos += scnprintf(table + pos,
				PAGE_SIZE - pos,
				"%s(0x%x, %dM), ",
				entry->desc,
				1<<j,
				entry->clkfreqMhz);
		}

		pos += scnprintf(table + pos,
			PAGE_SIZE - pos,
			"\n");

		bit++;
		table[pos] = 0;
		pr_info("%s", table);
		pos = 0;
	}

	kfree(table);
}

static ssize_t QosGenerator_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct noc_macro *nocm;
	u32 i, j;
	int cnt = 0;
	char opc = 0;
	u32 macro = 0xffff, qosbox = 0;
	struct noc_qos_t *entry = NULL;

	if (sscanf(buf, "%c", &opc) != 1)
		return -EINVAL;

	if (opc == '2')	{
		if (sscanf(buf, "%c %x %x ", &opc, &macro, &qosbox) != 3)
			return -EINVAL;

		for (i = 0; i < ARRAY_SIZE(noc_macro_list); i++) {
			nocm = &noc_macro_list[i];
			if (!nocm->qos_tbl)
				continue;

			if (macro & 0x1) {
				for (j = 0; j < nocm->qos_size; j++) {
					if (qosbox & 0x1) {
						entry = nocm->qos_tbl + j;
						break;
					}

					qosbox >>= 1;
				}
				break;
			}
			macro >>= 1;
		}

		if (entry == NULL)
			return -EINVAL;

		cnt = sscanf(buf, "%c %x %x %d %x %d %x\n", &opc, &macro,
			&qosbox, &entry->bw, &entry->priority, &entry->mode,
			&entry->saturation);
		pr_info("input param cnt=%d: %c %x %x %d %x %d %x\n", cnt,
			opc, macro, qosbox, entry->bw, entry->priority,
			entry->mode, entry->saturation);

		QosGenerator_Set(entry, nocm);
	} else {
		QosGenerator_store_usage();
	}

	return len;
}

static DEVICE_ATTR_RW(QosGenerator);

static ssize_t QosProbe_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct noc_macro *nocm;
	struct qos_probe_t *entry;
	struct noc_macro_bw_t *bw;
	int i, j, pos = 0;

	pos += scnprintf(buf + pos,
		PAGE_SIZE - pos,
		"Niu:\tBytes\tcur\tpeak\tavgMBps\n");

	for (i = 0; i < ARRAY_SIZE(noc_macro_list); i++) {
		nocm = &noc_macro_list[i];
		if (!(nocm->qos_probe_enable))
			continue;

		for (j = 0; j < nocm->qos_probe_size; j++) {
			entry = nocm->qos_probe_tbl + j;
			bw = entry->bw;
			if (!bw || bw->peak == 0)
				continue;
			pos += scnprintf(buf + pos,
				PAGE_SIZE - pos,
				"%s\t%x\t%d\t%d\t%d\n",
				entry->name,
				bw->bytes,
				bw->cur,
				bw->peak,
				bw->avg);

		}
	}

	return pos;
}

static void QosProbe_store_usage(void)
{
	u32 i, j, bit = 0, pos = 0;
	struct noc_macro *nocm;
	struct qos_probe_t *entry;
	char *table;

	pr_info("QosProbe_store_usage:\n");
	pr_info("\techo 0 : stop probe\n");
	pr_info("\techo 1 [nocm [period [max [mode [port_rotate]]]]]: start \
		probe of [nocm(s)], parameters are only effective during this\
		probe\n");
	pr_info("\t\tnocm: bitwise, see nocm-probe table below.\n");
	pr_info("\t\tperiod: probe window of ddrm, 2^period cycles of probe \
		clock. eg:1e=2.71s(400MHz)\n");
	pr_info("\t\tmax: alarm max if mode is 2(max), min if mode is 1(min)\
		\n");
	pr_info("\t\tmode: 0:off; 1:min; 2:max(default); 3:min_max\n");
	pr_info("\t\tport_rotate: 1:port rotate within the same probe, will \
		overwrite mode=3\n");

	pr_info("\techo 2 nocm probe : disable probe(s) of a nocm. bits of \
		value 1 are disabled, 0 enabled\n");
	pr_info("\t\tnocm: bitwise, see nocm-probe table below.\n");
	pr_info("\t\tprobe: bitwise, see nocm-probe table below.\n");

	pr_info("\techo ? : prompt this usage\n");
	pr_info("\tAppendix: nocm-probe(bitwise, freqency, disabled) table:\
		\n");
	pr_info("\t\tNOTE: table values may change due to driver update!!!\n");
	pr_info("\t\tNOTE: vdifm - lcd0/vpp0, lcd1/vpp1; audiom - dmac2/dmac3\
		/kas/usp0. / pairs use the same probe\n");

	table = kzalloc(PAGE_SIZE,
				GFP_KERNEL);
	if (table == NULL)
		return;

	for (i = 0; i < ARRAY_SIZE(noc_macro_list); i++) {
		nocm = &noc_macro_list[i];
		if (!(nocm->qos_probe_enable))
			continue;

		pos += scnprintf(table + pos,
			PAGE_SIZE - pos,
			"\t\t%s(0x%x) - ",
			nocm->name,
			1<<bit);

		for (j = 0; j < nocm->qos_probe_size; j++) {
			entry = nocm->qos_probe_tbl + j;

			pos += scnprintf(table + pos,
				PAGE_SIZE - pos,
				"%s(0x%x, %dM, %d), ",
				entry->name,
				1<<j,
				entry->mclk,
				entry->disabled);
		}

		pos += scnprintf(table + pos,
			PAGE_SIZE - pos,
			"\n");

		bit++;
		table[pos] = 0;
		pr_info("%s", table);
		pos = 0;
	}

	kfree(table);
}

static ssize_t QosProbe_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct noc_macro *nocm;
	u32 i, j;
	int cnt = 0;
	char opc = 0;
	u32 macro = 0xffff, probe = 0;

	if (sscanf(buf, "%c", &opc) != 1)
		return -EINVAL;

	if (opc == '1')	{
		if (probe_cfg.probe_active)
			return -EINVAL;

		memset(&probe_cfg, 0, sizeof(probe_cfg));
		probe_cfg.nocm = 0xffff;
		probe_cfg.mode = 2;
		probe_cfg.port_rotate = 0;
		probe_cfg.probe_event = 0x08;

		cnt = sscanf(buf, "%c %x %x %x %d %d %x\n", &opc,
			&probe_cfg.nocm, &probe_cfg.period,
			&probe_cfg.max, &probe_cfg.mode,
			&probe_cfg.port_rotate, &probe_cfg.probe_event);

		if (cnt >= 4)
			probe_cfg.max_en = 1;
		if (cnt >= 3)
			probe_cfg.period_en = 1;
		if (probe_cfg.mode != 1 && probe_cfg.mode != 2)
			probe_cfg.max_en = 0;
		if (probe_cfg.port_rotate)
			probe_cfg.mode = 3;

		pr_info("input param cnt=%d: %c %x %x %x %d %d %x\n", cnt, opc,
			probe_cfg.nocm, probe_cfg.period, probe_cfg.max,
			probe_cfg.mode, probe_cfg.port_rotate,
			probe_cfg.probe_event);

		macro = probe_cfg.nocm;
		for (i = 0; i < ARRAY_SIZE(noc_macro_list); i++) {
			nocm = &noc_macro_list[i];
			if (nocm->qos_probe_enable) {
				if (macro & 0x1)
					noc_qos_probe_init(nocm);
				macro >>= 1;
			}
		}

		probe_cfg.probe_active = 1;
		pr_info("Nocm-probe:Bytes,cur/peak/avg(MBps)\n");
	} else if (opc == '2') {
		if (probe_cfg.probe_active)
			return -EINVAL;
		if (sscanf(buf, "%c %x %x\n", &opc, &macro, &probe) != 3)
			return -EINVAL;

		pr_info("input param : %c %x %x\n", opc, macro, probe);
		for (i = 0; i < ARRAY_SIZE(noc_macro_list); i++) {
			nocm = &noc_macro_list[i];
			if (nocm->qos_probe_enable) {
				if (macro & 0x1)
					for (j = 0; j < nocm->qos_probe_size;
						j++) {
						nocm->qos_probe_tbl[j].disabled
							= probe & 0x1;
						probe >>= 1;
					}
				macro >>= 1;
			}
		}
	} else if (opc == '0') {
		if (!probe_cfg.probe_active)
			return -EINVAL;

		macro = probe_cfg.nocm;
		for (i = 0; i < ARRAY_SIZE(noc_macro_list); i++) {
			nocm = &noc_macro_list[i];
			if (nocm->qos_probe_enable) {
				if (macro & 0x1)
					noc_qos_probe_stop(nocm);
				macro >>= 1;
			}
		}

		probe_cfg.probe_active = 0;
	} else {
		QosProbe_store_usage();
	}

	return len;
}

static DEVICE_ATTR_RW(QosProbe);

static const struct of_device_id sirfsoc_nocfw_ids[] = {
	{ .compatible = "sirf,nocfw-cpum", .data = &noc_macro_list[0] },
	{ .compatible = "sirf,nocfw-cgum", .data = &noc_macro_list[1] },
	{ .compatible = "sirf,nocfw-btm", .data = &noc_macro_list[2] },
	{ .compatible = "sirf,nocfw-gnssm", .data = &noc_macro_list[3] },
	{ .compatible = "sirf,nocfw-gpum", .data = &noc_macro_list[4] },
	{ .compatible = "sirf,nocfw-mediam", .data = &noc_macro_list[5] },
	{ .compatible = "sirf,nocfw-vdifm", .data = &noc_macro_list[6] },
	{ .compatible = "sirf,nocfw-audiom", .data = &noc_macro_list[7] },
	{ .compatible = "sirf,nocfw-ddrm", .data = &noc_macro_list[8] },
	{ .compatible = "sirf,nocfw-rtcm", .data = &noc_macro_list[9] },
	{ .compatible = "sirf,nocfw-dramfw", .data = &noc_macro_list[10] },
	{ .compatible = "sirf,nocfw-spramfw", .data = &noc_macro_list[11] },

};

static int noc_dram_firewall_init(struct platform_device *pdev)
{
	int ret;
	/*
	 * fireware has been set earlier in secure mode, here
	 * it is only for debug purpose
	 */
	ret = device_create_file(&pdev->dev, &dev_attr_dramfw);
	if (ret)
		dev_err(&pdev->dev,
			"failed to create dram firewall attribute, %d\n",
			ret);

	ret = device_create_file(&pdev->dev, &dev_attr_regfw);
	if (ret)
		dev_err(&pdev->dev,
			"failed to create spram firewall attribute, %d\n",
			ret);

	ret = device_create_file(&pdev->dev, &dev_attr_QosGenerator);
	if (ret)
		dev_err(&pdev->dev,
			"failed to create noc qos attribute, %d\n",
			ret);

	ret = device_create_file(&pdev->dev, &dev_attr_QosProbe);
	if (ret)
		dev_err(&pdev->dev,
			"failed to create noc qos attribute, %d\n",
			ret);

	return 0;
}

static int noc_spram_firewall_init(struct platform_device *pdev)
{
	int ret;
	/*
	 * fireware has been set earlier in secure mode, here
	 * it is only for debug purpose
	 */
	ret = device_create_file(&pdev->dev, &dev_attr_spramfw);
	if (ret)
		dev_err(&pdev->dev,
			"failed to create spram firewall attribute, %d\n",
			ret);

	return 0;
}

static int noc_a7_init(struct platform_device *pdev)
{
	struct noc_macro *nocm;

	nocm = platform_get_drvdata(pdev);
	/*enable errlog trigger, A7 use abort*/
	hook_fault_code(8, noc_abort_handler, SIGBUS, 0,
		"external abort on non-linefetch");

	hook_fault_code(22, noc_abort_handler, SIGBUS, 0,
		"imprecise external abort");

	/*init alarm irq just as other macros*/
	noc_macro_init(pdev);

	return 0;
}

static int noc_macro_init(struct platform_device *pdev)
{
	int ret;
	struct noc_macro *nocm;

	nocm = platform_get_drvdata(pdev);
	/* ignore qos on pxp for lack some modules*/
#if 0
	if (!of_machine_is_compatible("sirf,atlas7-pxp"))
		QosGenerator_init(nocm);
#endif
	if (!(nocm->log_enable || nocm->qos_probe_enable))
		return 0;

	/*enable errlog trigger, thus irq/abort could come*/
	nocm->clk = devm_clk_get(&pdev->dev, "nocm");
	if (!IS_ERR(nocm->clk)) {
		pr_info("%s: succeed to get clock of %s!\n", __func__,
			nocm->name);
		ret = clk_prepare_enable(nocm->clk);
		pr_info("%s: clk_prepare_enable %d!\n", __func__, ret);
	}

	ret = of_irq_get(pdev->dev.of_node, 0);
	if (ret <= 0) {
		dev_info(&pdev->dev,
			"Unable to find IRQ number. ret=%d\n", ret);
		goto err;
	}
	nocm->irq = ret;

	noc_fault_enable(nocm);
	ret = devm_request_irq(&pdev->dev,
			nocm->irq,
			noc_irq_handle,
			0,
			nocm->name, nocm);
	if (ret) {
		pr_err("err: devm_request_irq %s: ret=%d\n", nocm->name, ret);
		goto err;
	}

	return 0;
err:
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int noc_pm_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct noc_macro *nocm = platform_get_drvdata(pdev);

	if (!IS_ERR(nocm->clk))
		clk_disable_unprepare(nocm->clk);

	return 0;
}

static int noc_pm_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct noc_macro *nocm = platform_get_drvdata(pdev);

	if (!IS_ERR(nocm->clk))
		clk_prepare_enable(nocm->clk);

	return 0;
}

static const struct dev_pm_ops noc_pm_ops = {
	.suspend_late = noc_pm_suspend,
	.resume_early = noc_pm_resume,
};

#endif

static int sirfsoc_noc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	struct noc_macro *nocm;

	match = of_match_device(sirfsoc_nocfw_ids, &pdev->dev);
	nocm = (struct noc_macro *)match->data;
	nocm->mbase = of_iomap(np, 0);
	if (!nocm->mbase) {
		pr_err("err: %s: of_iomap error\n", nocm->name);
		return -ENOMEM;
	}

	spin_lock_init(&nocm->lock);
	platform_set_drvdata(pdev, nocm);
	nocm->pdev = pdev;
	nocm->clk = NULL;

	if (nocm->init_macro)
		nocm->init_macro(pdev);

	return 0;
}

static struct platform_driver sirfsoc_noc_driver = {
	.driver = {
		   .name = "sirf-noc",
		   .of_match_table = sirfsoc_nocfw_ids,
#ifdef CONFIG_PM_SLEEP
		   .pm = &noc_pm_ops,
#endif
		   },
	.probe = sirfsoc_noc_probe,
};


module_platform_driver(sirfsoc_noc_driver);
