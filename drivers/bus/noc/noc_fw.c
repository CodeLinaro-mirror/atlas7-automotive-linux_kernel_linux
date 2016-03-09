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

static void noc_write_reg(int val, void __iomem *addr)
{
	int page;

	writel(val, addr);

	/* here for debugging purpose */
	page = page_to_phys(vmalloc_to_page(addr)) & PAGE_MASK;

	pr_debug("0x%08x,0x%08x\n", page |
				((u32)addr & ~PAGE_MASK), val);
}

static void ramfw_cpu(struct dramfw_regs_t *dfwregs,
			int start, int size, int cpu)
{
#define DFW_CPU_WRITE 1
#define DFW_CPU_READ 5
#define DFW_CPU_VAL (BIT(DFW_CPU_WRITE) | BIT(DFW_CPU_READ))
	noc_write_reg(start, &dfwregs->start);
	noc_write_reg(start + size, &dfwregs->end);

	noc_write_reg(0xff, &dfwregs->fw_cpu_clr);
	noc_write_reg(1<<cpu | 1<<(cpu+4), &dfwregs->fw_cpu_set);

	/*ignore cpu ids settings has been enabled by default*/
	noc_write_reg(DFW_CPU_VAL, &dfwregs->prot_clr);
	noc_write_reg(DFW_CPU_VAL, &dfwregs->prot_val_clr);
	/*rw enable*/
	noc_write_reg(DFW_CPU_VAL, &dfwregs->prot_set);
	/*default prot_val_set is secure */

}

#define RP_ENABLE_OFF   0x3F04
static ssize_t spramfw_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct noc_macro *nocm = dev_get_drvdata(dev);
	struct dramfw_regs_t *dfwregs;
	int start, size, cpu, rpnum;
	char name[16];
	unsigned long flags;

	memset(name, 0, sizeof(name));
	if (sscanf(buf, "%s %x %x %d\n",
			name, &start, &size, &rpnum) != 4)
		return -EINVAL;
	local_irq_save(flags);
	dfwregs = (struct dramfw_regs_t *)((void __iomem *)nocm->mbase +
		0x100 * rpnum);

	cpu = noc_get_cpu_by_name(name);
	if (cpu < 0)
		goto out;
	ramfw_cpu(dfwregs, start, size, cpu);
	/* last step enable rp */
	noc_write_reg(1<<rpnum, (void __iomem *)nocm->mbase + RP_ENABLE_OFF);
out:
	local_irq_restore(flags);
	return len;
}

static DEVICE_ATTR_WO(spramfw);

#define RFW_CLR_OFF 4
static void regfw_bit_clear(void __iomem *addr, u32 bit)
{
	noc_write_reg(1<<bit, addr + RFW_CLR_OFF);
}

static ssize_t regfw_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct noc_macro *nocm = dev_get_drvdata(dev);
	struct cpu_firewall_t *rfwregs = nocm->mbase + (int)nocm->regfwoff;
	unsigned long flags;

	u32 bit;

	if (sscanf(buf, "%d\n", &bit) != 1)
		return -EINVAL;

	local_irq_save(flags);
	if (bit > 0 && bit < 31)
		regfw_bit_clear(&rfwregs->ns, bit);

	local_irq_restore(flags);
	return len;
}

static DEVICE_ATTR_WO(regfw);

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
	ret = device_create_file(&pdev->dev, &dev_attr_spramfw);
	if (ret)
		dev_err(&pdev->dev,
			"failed to create dram firewall attribute, %d\n",
			ret);

	return 0;
}


