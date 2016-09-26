#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/sysfs.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>

#include "noc.h"

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

/*non-cpu secure state*/
struct initiator_config_t {
	u32 set;
	u32 clear;
	u32 status;
};

/* cpu secure state & access for range*/
struct cpu_firewall_t {
	struct initiator_config_t ns;
	struct initiator_config_t cpu_access[4];
};

/* noncpu secure state for range*/
struct noncpu_firewall_t {
	struct initiator_config_t read[4];
	struct initiator_config_t write[4];
};
#define MODE_BLOCK 0
#define MODE_ALLOW 1
/*mode is secure feature of rp:range process of ram*/
#define MODE_S 0
#define MODE_NS 1
/*state is secure feature of initiator itself*/
#define STATE_S 0
#define STATE_NS 1

#define CPU_M3 2
#define CPU_KAS 3

static void noc_write_reg(int val, void __iomem *addr)
{
	int page;

	writel(val, addr);

	/* here for debugging purpose */
	page = page_to_phys(vmalloc_to_page(addr)) & PAGE_MASK;

	pr_err("mw 0x%08x 0x%08x\n", page |
				((u32)addr & ~PAGE_MASK), val);
}

static void ramfw_config_range(struct dramfw_regs_t *dfwregs,
				int start, int size)
{
	noc_write_reg(start, &dfwregs->start);
	noc_write_reg(start + size, &dfwregs->end);
}

static void ramfw_config_cpu(struct dramfw_regs_t *dfwregs, int cpu,
				int permit_access, int mode)
{
#define DFW_CPU_WRITE 1
#define DFW_CPU_READ 5
#define DFW_CPU_VAL (BIT(DFW_CPU_WRITE) | BIT(DFW_CPU_READ))

	if (permit_access == MODE_BLOCK)
		noc_write_reg(1<<cpu | 1<<(cpu+4), &dfwregs->fw_cpu_clr);

	/*rw enable*/
	noc_write_reg(DFW_CPU_VAL, &dfwregs->prot_set);

	if (mode == MODE_NS)
		noc_write_reg(DFW_CPU_VAL, &dfwregs->prot_val_set);
	else
		noc_write_reg(DFW_CPU_VAL, &dfwregs->prot_val_clr);
}

#define ddrm_SecureState_WriteSet0   0x1050
#define ddrm_SecureState_WriteClr0   0x1054

#define ddrm_SecureState_WriteSet1   0x105C
#define ddrm_SecureState_WriteClr1   0x1060

#define ddrm_SecureState_WriteSet2   0x1068
#define ddrm_SecureState_WriteClr2   0x106C

#define ddrm_SecureState_WriteSet3   0x1074
#define ddrm_SecureState_WriteClr3   0x1078

#define ddrm_SecureState_ReadSet0    0x1080
#define ddrm_SecureState_ReadClr0    0x1084

#define ddrm_SecureState_ReadSet1    0x108C
#define ddrm_SecureState_ReadClr1    0x1090

#define ddrm_SecureState_ReadSet2    0x1098
#define ddrm_SecureState_ReadClr2    0x109C

#define ddrm_SecureState_ReadSet3    0x10A4
#define ddrm_SecureState_ReadClr3    0x10A8


struct ramfw_noncpu_state_t {
	u32 writeset;
	u32 writeclr;
	u32 readset;
	u32 readclr;
};

static struct ramfw_noncpu_state_t ramfw_noncpu_state_list[] = {
	{ddrm_SecureState_WriteSet0, ddrm_SecureState_WriteClr0,
		ddrm_SecureState_ReadSet0, ddrm_SecureState_ReadClr0},
	{ddrm_SecureState_WriteSet1, ddrm_SecureState_WriteClr1,
		ddrm_SecureState_ReadSet1, ddrm_SecureState_ReadClr1},
	{ddrm_SecureState_WriteSet2, ddrm_SecureState_WriteClr2,
		ddrm_SecureState_ReadSet2, ddrm_SecureState_ReadClr2},
	{ddrm_SecureState_WriteSet3, ddrm_SecureState_WriteClr3,
		ddrm_SecureState_ReadSet3, ddrm_SecureState_ReadClr3}
};

static void ramfw_config_noncpu_access(struct dramfw_regs_t *
					base, u32 initiator)
{
	u32 i, val;

	i = initiator / 32;
	val = 1<<(initiator - 32 * i);

	/* dram access read/write */
	noc_write_reg(val, &base->access[i].initiator_r_clr);
	noc_write_reg(val, &base->access[i].initiator_w_clr);
}

static void ramfw_config_noncpu_state(struct dramfw_regs_t *base,
			u32 initiator, int state)
{
	u32 i, val;

	i = initiator / 32;
	val = 1<<(initiator - 32 * i);

	/* initiator access read/write */
	if (state == MODE_NS) {
		noc_write_reg(val, s_ddrm->mbase +
				ramfw_noncpu_state_list[i].readset);
		noc_write_reg(val, s_ddrm->mbase +
				ramfw_noncpu_state_list[i].writeset);
	} else {
		noc_write_reg(val, s_ddrm->mbase +
				ramfw_noncpu_state_list[i].readclr);
		noc_write_reg(val, s_ddrm->mbase +
				ramfw_noncpu_state_list[i].writeclr);

	}
}

static void ramfw_config_noncpu_mode(struct dramfw_regs_t *base,
			u32 initiator, int mode)
{
	u32 i, val;

	i = initiator / 32;
	val = 1<<(initiator - 32 * i);

	noc_write_reg(0x00000022, &base->prot_set);
	noc_write_reg(0x00000022, &base->prot_val_set);
	if (mode == MODE_S)
		noc_write_reg(0x00000022, &base->prot_val_clr);
}

#define RP_ENABLE_OFF   0x3F04
static ssize_t spramfw_noncpu_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct noc_macro *nocm = dev_get_drvdata(dev);
	struct dramfw_regs_t *dfwregs;
	int access, state, mode, noncpu, rpnum = 0;
	char name[32];
	unsigned long flags;

	memset(name, 0, sizeof(name));
	if (sscanf(buf, "%s %d %d %d\n",
			name, &access, &state, &mode) != 4)
		return -EINVAL;

	/*
	 * if there is any bus access during configuring FW,
	 * it might fail so we need a unplug for CPU1 and
	 * disable IRQ to quiet both CPU1 and IRQ
	 */
	local_irq_save(flags);
	dfwregs = (struct dramfw_regs_t *)((void __iomem *)nocm->mbase +
		0x100 * rpnum);

	noncpu = noc_get_noncpu_by_name(name);
	if (noncpu < 0)
		goto out;

	/*spram noncpu id need adjust for chip bug*/
	noncpu = noc_get_id_by_orig(noncpu);
	/*1: block access*/
	if (!access)
		ramfw_config_noncpu_access(dfwregs, noncpu);

	ramfw_config_noncpu_state(dfwregs, noncpu, state);
	/*1: set range as secure for rp*/
	ramfw_config_range(dfwregs, 0x04000000, 0x30000);
	ramfw_config_noncpu_mode(dfwregs, noncpu, mode);

	/* last step enable rp */
	noc_write_reg(1<<rpnum, (void __iomem *)nocm->mbase + RP_ENABLE_OFF);
out:
	local_irq_restore(flags);
	return len;
}

static DEVICE_ATTR_WO(spramfw_noncpu);

static ssize_t dramfw_noncpu_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct noc_macro *nocm = dev_get_drvdata(dev);
	struct dramfw_regs_t *dfwregs;
	int access, state, mode, noncpu, rpnum = 0, rpbase;
	char name[32];
	unsigned long flags;

	memset(name, 0, sizeof(name));
	if (sscanf(buf, "%s %d %d %d\n",
			name, &access, &state, &mode) != 4)
		return -EINVAL;
	local_irq_save(flags);

	rpbase = noc_get_rpbase_by_name(name);
	dfwregs = (struct dramfw_regs_t *)((void __iomem *)nocm->mbase +
			rpbase + 0x100 * rpnum);

	noncpu = noc_get_noncpu_by_name(name);
	if (noncpu < 0)
		goto out;

	/*1: block access*/
	if (!access)
		ramfw_config_noncpu_access(dfwregs, noncpu);

	ramfw_config_noncpu_state(dfwregs, noncpu, state);

	/*apply secure rp attribute to whole 512MB dram*/
	ramfw_config_range(dfwregs, 0x40000000, 0x20000000);
	/*1: set range mode for rp*/
	ramfw_config_noncpu_mode(dfwregs, noncpu, mode);

	/* last step enable rp */
	noc_write_reg(1<<rpnum, (void __iomem *)nocm->mbase +
		rpbase + RP_ENABLE_OFF);
out:
	local_irq_restore(flags);
	return len;
}

static DEVICE_ATTR_WO(dramfw_noncpu);

static void noncpu_config_secure(void)
{
	/*set all initiator state to be secure*/
	noc_write_reg(0xffffffff, s_ddrm->mbase +
			ramfw_noncpu_state_list[0].readclr);
	noc_write_reg(0xffffffff, s_ddrm->mbase +
			ramfw_noncpu_state_list[0].writeclr);

	noc_write_reg(0xffffffff, s_ddrm->mbase +
			ramfw_noncpu_state_list[1].readclr);
	noc_write_reg(0xffffffff, s_ddrm->mbase +
			ramfw_noncpu_state_list[1].writeclr);

	noc_write_reg(0xffffffff, s_ddrm->mbase +
			ramfw_noncpu_state_list[2].readclr);
	noc_write_reg(0xffffffff, s_ddrm->mbase +
			ramfw_noncpu_state_list[2].writeclr);

	noc_write_reg(0xffffffff, s_ddrm->mbase +
			ramfw_noncpu_state_list[3].readclr);
	noc_write_reg(0xffffffff, s_ddrm->mbase +
			ramfw_noncpu_state_list[3].writeclr);
}

static void noncpu_config_nonsecure(void)
{
	/*set all initiator state to be nonsecure*/
	noc_write_reg(0xffffffff, s_ddrm->mbase +
			ramfw_noncpu_state_list[0].readset);
	noc_write_reg(0xffffffff, s_ddrm->mbase +
			ramfw_noncpu_state_list[0].writeset);

	noc_write_reg(0xffffffff, s_ddrm->mbase +
			ramfw_noncpu_state_list[1].readset);
	noc_write_reg(0xffffffff, s_ddrm->mbase +
			ramfw_noncpu_state_list[1].writeset);

	noc_write_reg(0xffffffff, s_ddrm->mbase +
			ramfw_noncpu_state_list[2].readset);
	noc_write_reg(0xffffffff, s_ddrm->mbase +
			ramfw_noncpu_state_list[2].writeset);

	noc_write_reg(0xffffffff, s_ddrm->mbase +
			ramfw_noncpu_state_list[3].readset);
	noc_write_reg(0xffffffff, s_ddrm->mbase +
			ramfw_noncpu_state_list[3].writeset);
}

static ssize_t noncpu_secure_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	int state;

	if (sscanf(buf, "%d\n", &state) != 1)
		return -EINVAL;
	if (state == STATE_NS)
		noncpu_config_nonsecure();
	else
		noncpu_config_secure();

	return len;
}

static DEVICE_ATTR_WO(noncpu_secure);

/*configure M3 secure state*/
static void noc_config_m3_state(u32 state)
{
	/*m3*/
	if (state == STATE_NS)
		noc_write_reg(1, (void __iomem *)s_rtcm->mbase + 0x1050);
	else
		noc_write_reg(1, (void __iomem *)s_rtcm->mbase + 0x1054);
}

/*configure Kas secure state*/
static void noc_config_kas_state(u32 state)
{
	/*kas*/
	if (state == STATE_NS)
		noc_write_reg(1, (void __iomem *)s_audiom->mbase + 0x1050);
	else
		noc_write_reg(1, (void __iomem *)s_audiom->mbase + 0x1054);
}

static ssize_t dramfw_cpu_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct noc_macro *nocm = dev_get_drvdata(dev);
	struct dramfw_regs_t *dfwregs;
	int access, mode, cpu, rpnum = 0, state, rpbase;
	char name[16];
	char bus[16];
	unsigned long flags;

	memset(name, 0, sizeof(name));
	if (sscanf(buf, "%s %s %d %d %d\n",
			name, bus, &access, &state, &mode) != 5)
		return -EINVAL;

	local_irq_save(flags);
	rpbase = noc_get_rpbase_by_bus(bus);
	dfwregs = (struct dramfw_regs_t *)((void __iomem *)nocm->mbase +
			rpbase + 0x100 * rpnum);

	cpu = noc_get_cpu_by_name(name);
	if (cpu < 0)
		goto out;

	/*apply secure rp attribute to whole 512MB dram*/
	ramfw_config_range(dfwregs, 0x40000000, 0x20000000);

	switch (cpu) {
	case CPU_M3:
		noc_config_m3_state(state);
		break;
	case CPU_KAS:
		noc_config_kas_state(state);
		break;
	default:
		break;
	}

	ramfw_config_cpu(dfwregs, cpu, access, mode);
	/* last step enable rp */
	noc_write_reg(1<<rpnum,
			(void __iomem *)nocm->mbase + rpbase + RP_ENABLE_OFF);
out:
	local_irq_restore(flags);
	return len;
}
static DEVICE_ATTR_WO(dramfw_cpu);

static ssize_t spramfw_cpu_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct noc_macro *nocm = dev_get_drvdata(dev);
	struct dramfw_regs_t *dfwregs;
	int access, mode, cpu, rpnum = 0, state;
	char name[16];
	unsigned long flags;

	memset(name, 0, sizeof(name));
	if (sscanf(buf, "%s %d %d %d\n",
			name, &access, &state, &mode) != 4)
		return -EINVAL;


	local_irq_save(flags);
	dfwregs = (struct dramfw_regs_t *)((void __iomem *)nocm->mbase +
		0x100 * rpnum);

	cpu = noc_get_cpu_by_name(name);
	if (cpu < 0)
		goto out;

	ramfw_config_range(dfwregs, 0x04000000, 0x30000);
	ramfw_config_cpu(dfwregs, cpu, access, mode);
	switch (cpu) {
	case CPU_M3:
		noc_config_m3_state(state);
		break;
	case CPU_KAS:
		noc_config_kas_state(state);
		break;
	default:
		break;
	}
	/* last step enable rp */
	noc_write_reg(1<<rpnum, (void __iomem *)nocm->mbase + RP_ENABLE_OFF);
out:
	local_irq_restore(flags);
	return len;
}


static DEVICE_ATTR_WO(spramfw_cpu);

static ssize_t regfw_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct noc_macro *nocm = dev_get_drvdata(dev);
	struct cpu_firewall_t *rfwregs = nocm->mbase + (int)nocm->regfwoff;
	unsigned long flags;
	char name[16];
	u32 state, mode, access, bit;
	int cpu;

	if (sscanf(buf, "%s %d %d %d %d\n",
			name, &access, &state, &mode, &bit) != 5)
		return -EINVAL;

	cpu = noc_get_cpu_by_name(name);
	if (cpu < 0)
		goto out;

	local_irq_save(flags);

	/*restore default nonsecure*/
	noc_write_reg(0xffffffff, &rfwregs->ns.set);
	if (!mode)
		noc_write_reg(1<<bit, &rfwregs->ns.clear);

	/*restore default access*/
	noc_write_reg(0xffffffff, &rfwregs->cpu_access[cpu].set);
	if (!access)
		noc_write_reg(1<<bit, &rfwregs->cpu_access[cpu].clear);

	switch (cpu) {
	case CPU_M3:
		noc_config_m3_state(state);
		break;
	case CPU_KAS:
		noc_config_kas_state(state);
		break;
	default:
		break;
	}

	local_irq_restore(flags);
out:
	return len;
}

static DEVICE_ATTR_WO(regfw);

static ssize_t ntfw_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct noc_macro *nocm = dev_get_drvdata(dev);
	void __iomem *addr;
	unsigned long flags;
	char name[16];
	u32 state, mode, access, offset, val;
	int cpu;

#define NTFW_SECURE_BIT	BIT(4)

	if (sscanf(buf, "%s %d %d %d %d\n", name,
			&access, &state, &mode, &offset) != 5)
		return -EINVAL;

	cpu = noc_get_cpu_by_name(name);
	if (cpu < 0)
		goto out;

	local_irq_save(flags);
	addr = nocm->mbase + (int)offset;

	/*restore default*/
	val = ~0;

	if (!mode)
		val &= ~NTFW_SECURE_BIT;

	if (!access)
		val &= ~(1<<cpu);

	switch (cpu) {
	case CPU_M3:
		noc_config_m3_state(state);
		break;
	case CPU_KAS:
		noc_config_kas_state(state);
		break;
	default:
		break;
	}

	noc_write_reg(val, addr);

	local_irq_restore(flags);
out:
	return len;
}

static DEVICE_ATTR_WO(ntfw);


int noc_regfw_init(struct noc_macro *nocm)
{
	struct platform_device *pdev = nocm->pdev;
	int ret;

	/*
	 * fireware has been set earlier in secure mode, here
	 * it is only for debug purpose
	 */

	ret = device_create_file(&pdev->dev, &dev_attr_regfw);
	if (ret)
		dev_err(&pdev->dev,
			"failed to create spram firewall attribute, %d\n",
			ret);

	return 0;
}

int noc_spramfw_init(struct noc_macro *nocm)
{
	struct platform_device *pdev = nocm->pdev;
	int ret;

	/*
	 * fireware has been set earlier in secure mode, here
	 * it is only for debug purpose
	 */
	ret = device_create_file(&pdev->dev, &dev_attr_spramfw_cpu);
	if (ret)
		dev_err(&pdev->dev,
			"failed to create dram firewall attribute, %d\n",
			ret);

	ret = device_create_file(&pdev->dev, &dev_attr_spramfw_noncpu);
	if (ret)
		dev_err(&pdev->dev,
			"failed to create dram firewall attribute, %d\n",
			ret);

	return 0;
}

int noc_dramfw_init(struct noc_macro *nocm)
{
	struct platform_device *pdev = nocm->pdev;
	int ret;

	/*
	 * fireware has been set earlier in secure mode, here
	 * it is only for debug purpose
	 */
	ret = device_create_file(&pdev->dev, &dev_attr_dramfw_noncpu);
	if (ret)
		dev_err(&pdev->dev,
			"failed to create dram firewall attribute, %d\n",
			ret);
	ret = device_create_file(&pdev->dev, &dev_attr_dramfw_cpu);
	if (ret)
		dev_err(&pdev->dev,
			"failed to create dram firewall attribute, %d\n",
			ret);

	ret = device_create_file(&pdev->dev, &dev_attr_noncpu_secure);
	if (ret)
		dev_err(&pdev->dev,
			"failed to create noncpu_secure attribute, %d\n",
			ret);
	return 0;
}

int ntfw_init(struct noc_macro *nocm)
{
	struct platform_device *pdev = nocm->pdev;
	int ret;

	ret = device_create_file(&pdev->dev, &dev_attr_ntfw);
	if (ret)
		dev_err(&pdev->dev,
			"failed to create dram firewall attribute, %d\n",
			ret);

	return 0;
}

