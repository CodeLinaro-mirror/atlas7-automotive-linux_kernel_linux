#ifndef __NOC_H__
#define __NOC_H__

struct kobj_ext_attribute {
	struct kobj_attribute attr;
	void *var;
};
#define to_ext_attr(x) container_of(x, struct kobj_ext_attribute, attr)

struct noc_macro;
struct noc_probe_t;

struct noc_macro {
	struct platform_device *pdev;
	const char *name;
	struct clk *clk;
	void __iomem *mbase;
	spinlock_t lock;
	u32 irq;
	u32 errlogoff;
	u32 faultenoff;
	u32 regfwoff;
	u32 schedoff;
	u32 probe_enable;	/*if 1, MUST set faultenoff*/
	struct noc_qos_t *qos_tbl;
	u32 qos_size;
	struct noc_probe_t *probe_tbl;
	u32 probe_size;
	u32 probe_en;
	u32 probe_manual_mode;	/*manual or alarm mode*/
	u32 probe_port_rotate;
	/* for sysfs*/
	struct kobject *qos_kobj;
	struct kobj_ext_attribute all_qos_attr;
	struct kobject *probe_kobj;
	struct kobj_ext_attribute probe_en_attr;
	struct kobj_ext_attribute probe_out_attr;
	struct kobj_ext_attribute probe_manual_mode_attr;
	struct kobj_ext_attribute probe_port_rotate_attr;
};

int noc_dump_errlog(struct noc_macro *nocm);
void noc_errlog_enable(struct noc_macro *nocm);
int noc_probe_init(struct noc_macro *nocm);
void noc_handle_probe(struct noc_macro *nocm);
int noc_qos_init(struct noc_macro *nocm);
int noc_get_cpu_by_name(const char *name);

#ifdef CONFIG_ATLAS7_NOC_FW
int noc_spramfw_init(struct noc_macro *nocm);
int noc_regfw_init(struct noc_macro *nocm);
#endif

#endif
