/*
* Atlas7 NoC support
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

#include <asm/signal.h>

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


/*dram fw*/
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

/*reg fw*/
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
	char name[NOC_MACRO_NAME_LEN];
	int (*init_macro)(struct platform_device *);
};
static int noc_macro_init(struct platform_device *);
static int noc_spram_firewall_init(struct platform_device *);
static int noc_dram_firewall_init(struct platform_device *);
static int noc_a7_init(struct platform_device *);

static struct noc_macro noc_macro_list[] = {
	{
		.name = "cpum",
		.idx = CPUM_IDX,
		.errlogoff = NOC_CPUM_ERRLOG,
		.faultenoff = NOC_CPUM_FAULTEN,
		.init_macro = noc_a7_init,
	},
	{
		.name = "cgum",
		.idx = CGUM_IDX,
	},
	{
		.name = "btm",
		.idx = BTM_IDX,
	},
	{
		.name = "gnssm",
		.idx = GNSSM_IDX,
	},
	{
		.name = "gpum",
		.idx = GPUM_IDX,
	},
	{
		.name = "mediam",
		.idx = MEDIAM_IDX,
	},
	{
		.name = "vdifm",
		.idx = VDIFM_IDX,
	},
	{
		.name = "audiom",
		.idx = AUDIOM_IDX,
		.errlogoff = NOC_AUDMSCM_ERRLOG,
		.faultenoff = NOC_AUDMSCM_FAULTEN,
		.init_macro = noc_macro_init,
	},
	{
		.name = "ddrm",
		.idx = DDRM_IDX,
		.errlogoff = NOC_DDRM_ERRLOG,
		.faultenoff = NOC_DDRM_FAULTEN,
		.init_macro = noc_macro_init,
	},
	{
		.name = "rtcm",
		.idx = RTCM_IDX,
		.errlogoff = NOC_RTCM_ERRLOG,
		.faultenoff = NOC_RTCM_FAULTEN,
		.init_macro = noc_macro_init,
	},
	{
		.name = "dramfw",
		.idx = DRAMFW_IDX,
		.init_macro = noc_dram_firewall_init,
	},
	{
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
/*data abort handler can not get base list*/

static int noc_has_err(void __iomem *noc_errlog_mbase)
{
	u32 vld;

	vld = readl_relaxed(noc_errlog_mbase + ERRORLOGGER_0_ERRVLD);
	vld &= 0x1;
	/*1 indicates an error has been logged (default: 0x0)*/
	return vld;
}

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
	else

		pr_info("ID:\%s\n", noc_initator_id_list[(errCode0>>7 & 0x1F)
				| ((errCode0>>2 & 0x3)<<5)].desc);

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

/*handler noc audio macro interrupt*/
static irqreturn_t noc_irq_handle(int irq, void *data)
{
	struct noc_macro *nocm = (struct noc_macro *)data;

	noc_dump_errlog(nocm);
	return IRQ_HANDLED;
}

static void  noc_fault_enable(struct noc_macro *nocm)
{
	writel_relaxed(0x1, nocm->mbase +
		nocm->faultenoff + NOC_SB_FAULTEN);
	writel_relaxed(0x1, nocm->mbase +
		nocm->faultenoff + NOC_SB_FLAGINEN0);
	writel_relaxed(0x1, nocm->mbase +
		nocm->errlogoff + ERRORLOGGER_0_FAULTEN);
}

static void noc_dramfw_cpu_set(void __iomem *fw_cpu_clr,
			void __iomem *fw_cpu_set, u32 initiator, u32 access)
{
	/* clear all except r_CA7 and w_CA7,set r_CA7 and w_CA7
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

	/*non-cpu initiator*/
	for (i = 0; i < 4; i++) {
		writel_relaxed(0xFFFFFFFF,
				&base->access[i].initiator_r_clr);
		writel_relaxed(0xFFFFFFFF,
				&base->access[i].initiator_w_clr);
	}

	i = initiator / 32;
	val = 1<<(initiator - 32 * i);
	/*dram access read/write*/
	if (access & ACCESS_READ)
		writel_relaxed(val, &base->access[i].initiator_r_set);
	if (access & ACCESS_WRITE)
		writel_relaxed(val, &base->access[i].initiator_w_set);

	/*initiator access read/write*/
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

	/*r_strict	arprot_2 arprot_1 arprot_0 w_strict
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

	if (!params)
		return;

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

	/*last step enable rp*/
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

	if (!mbase)
		return;

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
	return 0;
}

static int noc_spram_firewall_init(struct platform_device *pdev)
{
	int ret;

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
	noc_fault_enable(nocm);

	return 0;
}

static int noc_macro_init(struct platform_device *pdev)
{
	int ret;
	struct noc_macro *nocm;

	nocm = platform_get_drvdata(pdev);

	ret = of_irq_get(pdev->dev.of_node, 0);
	if (ret <= 0) {
		dev_info(&pdev->dev,
			"Unable to find IRQ number. ret=%d\n", ret);
		goto err;
	}
	nocm->irq = ret;
	/*enable errlog trigger, thus irq/abort could come*/
	noc_fault_enable(nocm);
	ret = devm_request_irq(&pdev->dev,
			nocm->irq,
			noc_irq_handle,
			0,
			nocm->name, nocm);
	if (ret)
		goto err;

	return 0;
err:
	return ret;
}


static int noc_macro_probe(struct platform_device *pdev)
{
	struct noc_macro *nocm;
	const struct of_device_id *match;
	int ret;

	match = of_match_node(sirfsoc_nocfw_ids, pdev->dev.of_node);
	if (!match) {
		ret = -ENODEV;
		goto err;
	}

	nocm = (struct noc_macro *)match->data;
	nocm->mbase = of_iomap(pdev->dev.of_node, 0);
	if (!nocm->mbase) {
		ret = -ENOMEM;
		goto err;
	}

	spin_lock_init(&nocm->lock);
	platform_set_drvdata(pdev, nocm);

	if (nocm->init_macro)
		nocm->init_macro(pdev);

	return 0;

err:
	return ret;
}

static struct platform_driver sirf_nocfw_driver = {
	.probe = noc_macro_probe,
	.driver = {
		.name = "sirfsoc_nocfw",
		.owner = THIS_MODULE,
		.of_match_table = sirfsoc_nocfw_ids,
	},
};

static __init int sirfsoc_noc_init(void)
{
	if (of_machine_is_compatible("sirf,atlas7")) {
		hook_fault_code(8, noc_abort_handler, SIGBUS, 0,
			"external abort on non-linefetch");

		hook_fault_code(22, noc_abort_handler, SIGBUS, 0,
			"imprecise external abort");
	}

	return platform_driver_register(&sirf_nocfw_driver);
}

arch_initcall(sirfsoc_noc_init);

