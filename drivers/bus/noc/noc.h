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
	struct noc_qos_t *qos_tbl;
	u32 qos_size;
	struct noc_probe_t *probe_tbl;
	u32 probe_size;
};

int noc_dump_errlog(struct noc_macro *nocm);
void noc_errlog_enable(struct noc_macro *nocm);
int noc_probe_init(struct noc_macro *nocm);
void noc_handle_probe(struct noc_macro *nocm);
int noc_qos_init(struct noc_macro *nocm);
int noc_get_cpu_by_name(const char *name);
int noc_get_noncpu_by_name(const char *name);
int noc_probe_suspend(struct noc_macro *nocm);
int noc_probe_resume(struct noc_macro *nocm);
int noc_get_id_by_orig(int orig);


#ifdef CONFIG_ATLAS7_NOC_FW
extern struct noc_macro *s_ddrm;
extern struct noc_macro *s_rtcm;
extern struct noc_macro *s_audiom;

int noc_spramfw_init(struct noc_macro *nocm);
int noc_regfw_init(struct noc_macro *nocm);
int noc_dramfw_init(struct noc_macro *nocm);
int ntfw_init(struct noc_macro *nocm);
int noc_get_rpbase_by_name(const char *name);
int noc_get_rpbase_by_bus(const char *name);

#endif

#endif
