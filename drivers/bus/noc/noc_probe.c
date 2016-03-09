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
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/sysfs.h>
#include <linux/clk.h>
#include <asm/div64.h>
#include "noc.h"
#include "trace.h"

#define SHOW_BW_VALUE_0		0	/*show the bw even it is 0*/
#define PROBE_SINGLE_PORT	((u32)-1)

#define ALARM_MODE_OFF		0
#define ALARM_MODE_MIN		1
#define ALARM_MODE_MAX		2
#define ALARM_MODE_MIN_MAX	3

enum e_probe_event {
	PE_OFF = 0,
	PE_CYCLE,
	PE_IDLE,
	PE_XFER,
	PE_BUSY,
	PE_WAIT,
	PE_PKT,
	PE_LUT,
	PE_BYTE,	/*0X08*/
	PE_PRESS0,
	PE_PRESS1,
	PE_PRESS2,
	PE_FILT0,
	PE_FILT1,
	PE_FILT2,
	PE_FILT3,
	PE_CHIAN,	/*0X10*/
	PE_LUT_BYTE_EN,
	PE_LUT_BYTE,
	PE_FILT_BYTE_EN,
	PE_FILT_BYTE,
};


struct noc_probe_common_cfg_t {
	/* for each probe's period calibration*/
	u32 period;
	u32 freq_ref;
	/* trigger level*/
	u32 max;
};

struct noc_probe_common_cfg_t cmn_cfg;

/* probe reg*/
struct probe_counter_regs_t {
	u32 reserved;
	u32 counters_m_portsel;
	u32 counters_m_src;
	u32 counters_m_alarm_mode;
	u32 counters_m_val;
};

struct probe_regs_t {
	u32 id_core_id;
	u32 id_revision_Id;
	u32 main_ctl;
	u32 cfg_ctl;
	u32 trace_port_sel;
	u32 filter_lut;
	u32 reserved[3];
	u32 stat_period;
	u32 stat_go;
	u32 stat_alarm_min;
	u32 stat_alarm_max;
	u32 stat_alarm_status;
	u32 stat_alarm_clr;
	u32 stat_alarm_en;
	u32 reserved1[61];
	u32 counters_0_portsel;
	u32 counters_0_src;
	u32 counters_0_alarm_mode;
	u32 counters_0_val;
	u32 reserved2;
	u32 counters_1_portsel;
	u32 counters_1_src;
	u32 counters_1_alarm_mode;
	u32 counters_1_val;
	struct probe_counter_regs_t probe_counters[1];
};

/*packet filters*/
struct packet_filter_regs_t {
	u32 filters_n_route_id_base;
	u32 filters_n_route_id_mask;
	u32 filters_n_addr_base_low;
	u32 reserved;
	u32 filters_n_window_size;
	u32 reserved1[2];
	u32 filters_n_opcode;
	u32 filters_n_status;
	u32 filters_n_length;
	u32 filters_n_urgency;
	u32 filters_n_user_base;
	u32 filters_n_user_mask;
	u32 reserved2[2];
};

/*probe*/
struct noc_macro_bw_t {
	u64 sum;
	u32 cnt;
	u32 bytes;
	u32 peak;
	u32 cur;
	u32 avg;
};

/*
 * one probe can probe multiple events using different
 * counters, one unit would use 2 counters
 */
struct noc_probe_unit_t {
	struct noc_probe_t *probe;
	const char *name;
	u32 enabled;
	u32 probe_event;
	struct noc_macro_bw_t bandwidth;

	/* for sysfs*/
	struct kobj_ext_attribute attr_enabled;
	struct kobj_ext_attribute attr_probe_event;
	struct attribute *probe_attrs[3];	/*NULL terminated*/
	struct attribute_group probe_attr_group;
};
#define MAX_PROBE_UNITS	7

/* packet filter*/
struct noc_pfilter_t {
	struct noc_probe_t *probe;
	const char *name;
	u32 macro_offset;
	u32 user_base;
	u32 user_mask;
	u32 route_id_base;
	u32 route_id_mask;
	u32 addr_base_low;
	u32 window_size;
	u32 opcode;
	u32 status;
	u32 length;
	u32 urgency;

	/* for sysfs*/
	struct kobj_ext_attribute attr_user_base;
	struct kobj_ext_attribute attr_user_mask;
	struct kobj_ext_attribute attr_route_id_base;
	struct kobj_ext_attribute attr_route_id_mask;
	struct kobj_ext_attribute attr_addr_base_low;
	struct kobj_ext_attribute attr_window_size;
	struct kobj_ext_attribute attr_opcode;
	struct kobj_ext_attribute attr_status;
	struct kobj_ext_attribute attr_length;
	struct kobj_ext_attribute attr_urgency;
	struct attribute *pfilter_attrs[11];	/*NULL terminated*/
	struct attribute_group pfilter_attr_group;
};
#define MAX_PACKET_FILTERS	4

struct noc_probe_t {
	const char *name;
	u32 macro_offset;
	u32 port;
	u32 mclk;
	u32 period;
	u32 max;
	u32 min;
	u32 mode;
	/*some masters use the same probe, we should just enable one of them*/
	u32 disabled;
	u32 probe_event;
	const char *clock_name;
	u32 divider;
	struct clk *clk;
	struct noc_macro *nocm;
	struct noc_macro_bw_t bandwidth;
	struct noc_probe_unit_t extra_probe[MAX_PROBE_UNITS];
	u32 extra_size;
	struct noc_pfilter_t pfilter[MAX_PACKET_FILTERS];
	u32 pfilter_size;
	u32 pfilter_lut;

	/* for sysfs*/
	struct kobject *probe_kobj;
	struct kobj_ext_attribute attr_period;
	struct kobj_ext_attribute attr_max;
	struct kobj_ext_attribute attr_min;
	struct kobj_ext_attribute attr_mode;
	struct kobj_ext_attribute attr_disabled;
	struct kobj_ext_attribute attr_probe_event;
	struct attribute *probe_attrs[7];	/*NULL terminated*/
	struct attribute_group probe_attr_group;
	/* this one may be optional*/
	struct kobj_ext_attribute attr_pfilter_lut;
};

static inline int probe_enable_clk(struct noc_probe_t *entry)
{
	int ret = 0;

	if (entry->clk) {
		ret = clk_prepare_enable(entry->clk);
		if (ret) {
			pr_err("%s: failed clk_prepare_enable %s!\n",
				__func__, entry->clock_name);
			return ret;
		}
	}

	return ret;
}

static inline void probe_disable_clk(struct noc_probe_t *entry)
{
	if (entry->clk)
		clk_disable_unprepare(entry->clk);
}

/*
 * get appropriate period for different nocms according to their clock
 * compared to ddrm clock, to make them have approximately the same alram time
 */
static u32 noc_probe_get_period(u32 mclk)
{
	u32 j;
	u32 period;

	period = cmn_cfg.freq_ref / mclk;
	for (j = 1; j < 32; j++)
		if (period>>j == 0)
			break;
	period = cmn_cfg.period - j + 1;

	return period;
}

/*
 * some masters use different ports of the same probe,
 * so need rotate ports to get all masters' values
 */
static void noc_probe_port_rotate(struct noc_macro *nocm, u32 index)
{
	struct noc_probe_t *entry = NULL;
	u32 offset = 0;
	u32 i = 0;
	struct probe_regs_t	 *probe_reg = NULL;

	if (index >= nocm->probe_size)
		return;

	entry = nocm->probe_tbl + index;
	if (entry->port == PROBE_SINGLE_PORT)
		return;

	entry->disabled = 1;
	offset = entry->macro_offset;
	probe_reg = (struct probe_regs_t	*)
			(nocm->mbase + entry->macro_offset);

	for (i = index + 1; i < nocm->probe_size; i++) {

		entry = nocm->probe_tbl + i;
		if (entry->port == PROBE_SINGLE_PORT)
			continue;

		if (entry->macro_offset == offset) {
			entry->disabled = 0;
			writel_relaxed(entry->port,
				&probe_reg->counters_0_portsel);
			return;
		}
	}

	for (i = 0; i < index; i++) {

		entry = nocm->probe_tbl + i;
		if (entry->port == PROBE_SINGLE_PORT)
			continue;

		if (entry->macro_offset == offset) {
			entry->disabled = 0;
			writel_relaxed(entry->port,
				&probe_reg->counters_0_portsel);
			return;
		}
	}
}

#define get_counter(i) \
	(readl_relaxed(&probe_reg->probe_counters[i].counters_m_val) |\
	readl_relaxed(&probe_reg->probe_counters[i + 1].counters_m_val) << 16)

static inline void noc_handle_extra_probe(struct noc_probe_t *entry)
{
	struct probe_regs_t	 *probe_reg;
	struct noc_macro *nocm = entry->nocm;
	struct noc_macro_bw_t *bw;
	u32 i;
	u32 val;

	for (i = 0; i < entry->extra_size; i++) {
		struct noc_probe_unit_t *pu;

		pu = entry->extra_probe + i;
		if (!pu->enabled)
			continue;

		probe_reg = (struct probe_regs_t	*)
			(nocm->mbase + entry->macro_offset);

		val = get_counter(i * 2);
		trace_noc_bw_data(pu->name, val);

		bw = &pu->bandwidth;
		if (!SHOW_BW_VALUE_0 && bw->cnt == 0 && val == 0)
			continue;

		bw->bytes = val;
		bw->peak = max(bw->bytes, bw->peak);
		/*overflow?*/
		if (bw->cnt + 1 < bw->cnt || bw->sum + bw->bytes < bw->sum) {
			bw->cnt = 0;
			bw->sum = 0;
		}
		bw->cnt++;
		bw->sum += bw->bytes;
	}
}

void noc_handle_probe(struct noc_macro *nocm)
{
	struct probe_regs_t	 *probe_reg;
	struct noc_probe_t *entry;
	struct noc_macro_bw_t *bw;
	u32 i;
	u32 val;

	for (i = 0; i < nocm->probe_size; i++) {

		entry = nocm->probe_tbl + i;
		if (entry->disabled)
			continue;

		probe_reg = (struct probe_regs_t	*)
			(nocm->mbase + entry->macro_offset);

		if (!readl(&probe_reg->stat_alarm_status))
			continue;

		val = (readl_relaxed(&probe_reg->counters_1_val) << 16) |
				readl_relaxed(&probe_reg->counters_0_val);
		trace_noc_bw_data(entry->name, val);
		bw = &entry->bandwidth;
		if (!SHOW_BW_VALUE_0 && bw->cnt == 0 && val == 0)
			goto next;

		bw->bytes = val;
		bw->peak = max(bw->bytes, bw->peak);
		/*overflow?*/
		if (bw->cnt + 1 < bw->cnt || bw->sum + bw->bytes < bw->sum) {
			bw->cnt = 0;
			bw->sum = 0;
		}
		bw->cnt++;
		bw->sum += bw->bytes;

next:
		noc_handle_extra_probe(entry);

		if (nocm->probe_port_rotate
			&& entry->port != PROBE_SINGLE_PORT)
			noc_probe_port_rotate(nocm, i);

		/*clr the alm*/
		writel(1, &probe_reg->stat_alarm_clr);
	}
}

static void noc_probe_stop(struct noc_macro *nocm)
{
	struct probe_regs_t	 *probe_reg;
	struct noc_probe_t *entry;
	u32 i;

	for (i = 0; i < nocm->probe_size; i++) {
		entry = nocm->probe_tbl + i;
		probe_reg = (struct probe_regs_t	*)
				(nocm->mbase + entry->macro_offset);

		if (entry->disabled)
			continue;

		/*clear field GlobalEn enable the counting of bytes.*/
		writel_relaxed(0, &probe_reg->cfg_ctl);

		/*clear staten, alarmen*/
		writel_relaxed(readl_relaxed(&probe_reg->main_ctl) & ~0x18,
				&probe_reg->main_ctl);
		writel_relaxed(0, &probe_reg->stat_alarm_en);

		probe_disable_clk(entry);
	}
}

static void noc_probe_extra_start(struct noc_probe_t *entry)
{
	struct probe_regs_t *probe_reg;
	struct noc_macro *nocm = entry->nocm;
	int i = 0;

	if (entry->extra_size == 0)
		return;

	probe_reg = (struct probe_regs_t *)
			(nocm->mbase + entry->macro_offset);

	for (i = 0; i < entry->extra_size * 2; i++)
		writel_relaxed(0,
			&probe_reg->probe_counters[i].counters_m_alarm_mode);

	for (i = 0; i < entry->extra_size; i++) {
		struct noc_probe_unit_t *pu;

		pu = entry->extra_probe + i;
		if (!pu->enabled)
			continue;

		/*re-statistics*/
		memset(&pu->bandwidth, 0, sizeof(pu->bandwidth));

		writel_relaxed(pu->probe_event,
			&probe_reg->probe_counters[i*2].counters_m_src);
		writel_relaxed(0x10,
			&probe_reg->probe_counters[i*2 + 1].counters_m_src);

		/*
		* event 0x14(FILT_BYTE): should set  FiltByteAlwaysChainableEn
		* in mainctl, and use trace_port_sel instead of
		* counter_m_port_sel if need.
		* event 0x12(LUT_BYTE): should set LUT,
		* 0xaaaa means just filter0
		*/
		if (pu->probe_event == 0x14)
			writel_relaxed(
				readl_relaxed(&probe_reg->main_ctl) | BIT(7),
				&probe_reg->main_ctl);

		pr_debug("extra probe[%d] of %s, evt:0x%x started!\n",
			i, entry->name, pu->probe_event);
	}
}


static int noc_probe_start(struct noc_macro *nocm)
{
	struct probe_regs_t	 *probe_reg;
	struct noc_probe_t *entry;
	u32 i;
	int ret = 0;

	for (i = 0; i < nocm->probe_size; i++) {
		entry = nocm->probe_tbl + i;
		probe_reg = (struct probe_regs_t	*)
				(nocm->mbase + entry->macro_offset);

		if (entry->disabled)
			continue;

		ret = probe_enable_clk(entry);
		if (ret)
			return ret;

		/*re-statistics*/
		memset(&entry->bandwidth, 0, sizeof(entry->bandwidth));

		/*StatEn */
		writel_relaxed(readl_relaxed(&probe_reg->main_ctl) | BIT(3),
			&probe_reg->main_ctl);

		/*
		* Only if The table above contain port number:
		* Set register counters_0_portsel to the value
		* corresponding to the probe point of interest.
		* no need , A& probe doesnt have more than one port
		*/
		if (entry->port != PROBE_SINGLE_PORT)
			writel_relaxed(entry->port,
				&probe_reg->counters_0_portsel);

		/* Set register counters_0_src to 0x8 (BYTES) to count bytes.*/
		writel_relaxed(entry->probe_event,
			&probe_reg->counters_0_src);

		/*
		* Set register counters_1_src to 0x10 (CHAIN)
		* to increment when counter 0 wraps.
		*/
		writel_relaxed(0x10, &probe_reg->counters_1_src);

		/*
		* Setting register stat_period to 2^period cycles.
		* also can config to 0x00 ( manual mode )
		*/
		writel_relaxed(entry->period, &probe_reg->stat_period);

		/*alarm mode, chained*/
		writel_relaxed(entry->mode,
			&probe_reg->counters_0_alarm_mode);
		writel_relaxed(0, &probe_reg->counters_1_alarm_mode);

		/*set alarmMax and Min*/
		writel_relaxed(entry->max, &probe_reg->stat_alarm_max);
		writel_relaxed(entry->min, &probe_reg->stat_alarm_min);

		/*trigger alarm any time*/
		if (entry->mode == ALARM_MODE_MIN_MAX) {
			writel_relaxed(0xffffffff, &probe_reg->stat_alarm_min);
			writel_relaxed(0x0, &probe_reg->stat_alarm_max);
		}

		pr_debug("%s(%dMHz): period=%x, max/min=0x%x/0x%x, mode=%d\n",
			entry->name, entry->mclk, entry->period,
			entry->max, entry->min, entry->mode);

		noc_probe_extra_start(entry);

		/*enable alm*/
		writel_relaxed(readl_relaxed(&probe_reg->main_ctl) | 0x10,
				&probe_reg->main_ctl);
		writel_relaxed(1, &probe_reg->stat_alarm_en);


		/*Set field GlobalEn enable the counting of bytes.*/
		writel(1, &probe_reg->cfg_ctl);
	}

	return 0;
}

int noc_probe_common_init(void)
{
	static struct device_node *bnp;
	struct device_node *cnp = NULL;
	int ret = 0;


	/* find common marco parameters first*/
	if (!bnp) {
		cnp = of_find_node_by_name(NULL, "noc-macro-common");
		if (!cnp) {
			pr_err("noc-macro-common not found\n");
			return -ENODEV;
		}

		bnp = of_get_child_by_name(cnp, "bw_probe");
		if (!bnp) {
			pr_err("bw_probe not found\n");
			return -ENODEV;
		}

		ret = of_property_read_u32(bnp, "period", &cmn_cfg.period);
		if (ret) {
			pr_err("of_property_read_u32 period failed\n");
			return ret;
		}

		ret = of_property_read_u32(bnp, "freq_ref", &cmn_cfg.freq_ref);
		if (ret) {
			pr_err("of_property_read_u32 freq_ref failed\n");
			return ret;
		}

		ret = of_property_read_u32(bnp, "max", &cmn_cfg.max);
		if (ret) {
			pr_err("of_property_read_u32 max failed\n");
			return ret;
		}

		pr_debug("noc-macro-common:0x%x, %d, 0x%x\n",
			cmn_cfg.period, cmn_cfg.freq_ref, cmn_cfg.max);
	}

	return ret;
}

#define init_probe_attr_rw(_name) {					\
	entry->attr_##_name.attr.attr.name = #_name;			\
	entry->attr_##_name.attr.attr.mode = (S_IWUSR | S_IRUGO);	\
	entry->attr_##_name.attr.show = probe_show_##_name;		\
	entry->attr_##_name.attr.store = probe_store_##_name;		\
	entry->attr_##_name.var = (void *)entry;			\
	entry->probe_attrs[j++] = &entry->attr_##_name.attr.attr; }	\


#define define_probe_attr_show_func(name)				\
									\
static ssize_t probe_show_##name(struct kobject *kobj,			\
					struct kobj_attribute *attr,	\
					char *buf)			\
{									\
	struct kobj_ext_attribute *ea = to_ext_attr(attr);		\
	struct noc_probe_t *entry = (struct noc_probe_t *)ea->var;	\
	int pos = 0;							\
									\
	pos += scnprintf(buf + pos,					\
		PAGE_SIZE - pos,					\
		"0x%x\n",						\
		entry->name);						\
									\
	return pos;							\
}


#define define_probe_attr_store_func(name)				\
									\
static ssize_t probe_store_##name(struct kobject *kobj,			\
					struct kobj_attribute *attr,	\
					const char *buf, size_t len)	\
{									\
	struct kobj_ext_attribute *ea = to_ext_attr(attr);		\
	struct noc_probe_t *entry = (struct noc_probe_t *)ea->var;	\
	int ret;							\
									\
	if (entry->nocm->probe_en)					\
		return -EINVAL;						\
									\
	ret = kstrtou32(buf, 0, &entry->name);				\
	if (ret)							\
		return ret;						\
									\
	return len;							\
}


define_probe_attr_show_func(period);
define_probe_attr_store_func(period);
define_probe_attr_show_func(max);
define_probe_attr_store_func(max);
define_probe_attr_show_func(min);
define_probe_attr_store_func(min);
define_probe_attr_show_func(mode);
define_probe_attr_store_func(mode);
define_probe_attr_show_func(disabled);
define_probe_attr_store_func(disabled);
define_probe_attr_show_func(probe_event);
define_probe_attr_store_func(probe_event);


static ssize_t probe_pfilter_lut_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	struct kobj_ext_attribute *ea = to_ext_attr(attr);
	struct noc_probe_t *entry = (struct noc_probe_t *)ea->var;
	int pos = 0;

	pos += scnprintf(buf + pos,
		PAGE_SIZE - pos,
		"0x%x\n",
		entry->pfilter_lut);

	return pos;
}

static ssize_t probe_pfilter_lut_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t len)
{
	struct kobj_ext_attribute *ea = to_ext_attr(attr);
	struct noc_probe_t *entry = (struct noc_probe_t *)ea->var;
	struct probe_regs_t *probe_reg;
	int ret;

	if (entry->nocm->probe_en)
		return -EINVAL;

	ret = probe_enable_clk(entry);
	if (ret)
		return ret;

	ret = kstrtou32(buf, 0, &entry->pfilter_lut);
	if (ret)
		return ret;

	probe_reg = (struct probe_regs_t *)
		(entry->nocm->mbase + entry->macro_offset);
	writel_relaxed(entry->pfilter_lut, &probe_reg->filter_lut);

	probe_disable_clk(entry);

	return len;
}

const char *extra_probe_name[MAX_PROBE_UNITS] = {
	"extra_probe1",
	"extra_probe2",
	"extra_probe3",
	"extra_probe4",
	"extra_probe5",
	"extra_probe6",
	"extra_probe7",
};

#define init_extra_probe_attr_rw(_name) {				\
	entry->extra_probe[j].probe = entry;				\
	entry->extra_probe[j].name = extra_probe_name[j];		\
	entry->extra_probe[j].attr_##_name.attr.attr.name = #_name;	\
	entry->extra_probe[j].attr_##_name.attr.attr.mode =		\
		(S_IWUSR | S_IRUGO);					\
	entry->extra_probe[j].attr_##_name.attr.show =			\
		extra_probe_show_##_name;				\
	entry->extra_probe[j].attr_##_name.attr.store =			\
		extra_probe_store_##_name;				\
	entry->extra_probe[j].attr_##_name.var =			\
		&entry->extra_probe[j];					\
	entry->extra_probe[j].probe_attrs[k++] =			\
		&entry->extra_probe[j].attr_##_name.attr.attr;	}	\


#define define_extra_probe_attr_show_func(name)				\
									\
static ssize_t extra_probe_show_##name(struct kobject *kobj,		\
					struct kobj_attribute *attr,	\
					char *buf)			\
{									\
	struct kobj_ext_attribute *ea = to_ext_attr(attr);		\
	struct noc_probe_unit_t *entry = (struct noc_probe_unit_t *)	\
		ea->var;						\
	int pos = 0;							\
									\
	pos += scnprintf(buf + pos,					\
		PAGE_SIZE - pos,					\
		"0x%x\n",						\
		entry->name);						\
									\
	return pos;							\
}


#define define_extra_probe_attr_store_func(name)			\
									\
static ssize_t extra_probe_store_##name(struct kobject *kobj,		\
					struct kobj_attribute *attr,	\
					const char *buf, size_t len)	\
{									\
	struct kobj_ext_attribute *ea = to_ext_attr(attr);		\
	struct noc_probe_unit_t *entry = (struct noc_probe_unit_t *)	\
		ea->var;						\
	int ret;							\
									\
	if (entry->probe->nocm->probe_en)				\
		return -EINVAL;						\
									\
	ret = kstrtou32(buf, 0, &entry->name);				\
	if (ret)							\
		return ret;						\
									\
	return len;							\
}


define_extra_probe_attr_show_func(enabled);
define_extra_probe_attr_store_func(enabled);
define_extra_probe_attr_show_func(probe_event);
define_extra_probe_attr_store_func(probe_event);

static ssize_t probe_out_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	struct kobj_ext_attribute *ea = to_ext_attr(attr);
	struct noc_macro *nocm = (struct noc_macro *)ea->var;
	struct noc_probe_t *entry;
	struct noc_macro_bw_t *bw;
	int i, j, pos = 0;

	for (j = 0; j < nocm->probe_size; j++) {
		entry = nocm->probe_tbl + j;
		bw = &entry->bandwidth;
		if (!SHOW_BW_VALUE_0 && bw->peak == 0)
			continue;

		if (pos == 0)
			pos += scnprintf(buf + pos,
				PAGE_SIZE - pos,
				"%s\nNiu(evt):\tBytes\tcur\tpeak\tavgMBps\n",
				nocm->name);

		pos += scnprintf(buf + pos,
			PAGE_SIZE - pos,
			"%s(0x%x)\t%x\t%d\t%d\t%d\n",
			entry->name,
			entry->probe_event,
			bw->bytes,
			bw->cur,
			bw->peak,
			bw->avg);

		for (i = 0; i < entry->extra_size; i++) {
			struct noc_probe_unit_t *pu;

			pu = entry->extra_probe + i;
			if (!pu->enabled)
				continue;

			bw = &pu->bandwidth;
			if (!SHOW_BW_VALUE_0 && bw->peak == 0)
				continue;

			pos += scnprintf(buf + pos,
				PAGE_SIZE - pos,
				"%s(0x%x)\t%x\t%d\t%d\t%d\n",
				pu->name,
				pu->probe_event,
				bw->bytes,
				bw->cur,
				bw->peak,
				bw->avg);
		}
	}

	return pos;
}

static ssize_t probe_en_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	struct kobj_ext_attribute *ea = to_ext_attr(attr);
	struct noc_macro *nocm = (struct noc_macro *)ea->var;
	int pos = 0;

	pos += scnprintf(buf + pos,
		PAGE_SIZE - pos,
		"%d\n",
		nocm->probe_en);

	return pos;
}

static ssize_t probe_en_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t len)
{
	struct kobj_ext_attribute *ea = to_ext_attr(attr);
	struct noc_macro *nocm = (struct noc_macro *)ea->var;
	struct noc_probe_t *entry;
	struct probe_regs_t *probe_reg;
	struct noc_macro_bw_t *bw;
	int en, i, j;
	u64 val;
	int ret;

	ret = kstrtou32(buf, 0, &en);
	if (ret)
		return ret;

	if (!(en ^ nocm->probe_en))
		return -EINVAL;

	if (en) {
		noc_probe_start(nocm);
		nocm->probe_en = en;
		return len;
	}

	/* here stop probe*/
	noc_probe_stop(nocm);
	pr_debug("Nocm-probe(evt)[cnt]:Bytes,cur/peak/avg(MBps)\n");
	for (j = 0; j < nocm->probe_size; j++) {
		entry = nocm->probe_tbl + j;
		probe_reg = (struct probe_regs_t	*)
			(nocm->mbase + entry->macro_offset);
		bw = &entry->bandwidth;
		if ((!SHOW_BW_VALUE_0 && bw->peak == 0) || bw->cnt == 0)
			continue;

		val = bw->sum;
		bw->avg = do_div(val,
			bw->cnt);
		bw->avg = val;
		val *= entry->mclk;
		bw->avg = do_div(val, 1<<entry->period);
		bw->avg = val;

		val = bw->peak;
		val *= entry->mclk;
		bw->peak = do_div(val, 1<<entry->period);
		bw->peak = val;

		val = bw->bytes;
		val *= entry->mclk;
		bw->cur = do_div(val, 1<<entry->period);
		bw->cur = val;

		pr_debug("%s-%s(0x%x)[%d]:%x,%d/%d/%d\n",
			nocm->name,
			entry->name,
			entry->probe_event,
			bw->cnt,
			bw->bytes,
			bw->cur,
			bw->peak,
			bw->avg);

		for (i = 0; i < entry->extra_size; i++) {
			struct noc_probe_unit_t *pu;

			pu = entry->extra_probe + i;
			if (!pu->enabled)
				continue;

			bw = &pu->bandwidth;
			if ((!SHOW_BW_VALUE_0 && bw->peak == 0)
				|| bw->cnt == 0)
				continue;

			val = bw->sum;
			bw->avg = do_div(val,
				bw->cnt);
			bw->avg = val;
			val *= entry->mclk;
			bw->avg = do_div(val, 1<<entry->period);
			bw->avg = val;

			val = bw->peak;
			val *= entry->mclk;
			bw->peak = do_div(val, 1<<entry->period);
			bw->peak = val;

			val = bw->bytes;
			val *= entry->mclk;
			bw->cur = do_div(val, 1<<entry->period);
			bw->cur = val;

			pr_debug("%s-%s(0x%x)[%d]:%x,%d/%d/%d\n",
				entry->name,
				pu->name,
				pu->probe_event,
				bw->cnt,
				bw->bytes,
				bw->cur,
				bw->peak,
				bw->avg);
		}
	}

	nocm->probe_en = en;

	return len;
}

static ssize_t probe_manual_mode_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	struct kobj_ext_attribute *ea = to_ext_attr(attr);
	struct noc_macro *nocm = (struct noc_macro *)ea->var;
	int pos = 0;

	pos += scnprintf(buf + pos,
		PAGE_SIZE - pos,
		"%d\n",
		nocm->probe_manual_mode);

	return pos;
}

static ssize_t probe_manual_mode_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t len)
{
	struct kobj_ext_attribute *ea = to_ext_attr(attr);
	struct noc_macro *nocm = (struct noc_macro *)ea->var;
	int ret;

	ret = kstrtou32(buf, 0, &nocm->probe_manual_mode);
	if (ret)
		return ret;

	return len;
}

static ssize_t probe_port_rotate_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	struct kobj_ext_attribute *ea = to_ext_attr(attr);
	struct noc_macro *nocm = (struct noc_macro *)ea->var;
	int pos = 0;

	pos += scnprintf(buf + pos,
		PAGE_SIZE - pos,
		"%d\n",
		nocm->probe_port_rotate);

	return pos;
}

static ssize_t probe_port_rotate_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t len)
{
	struct kobj_ext_attribute *ea = to_ext_attr(attr);
	struct noc_macro *nocm = (struct noc_macro *)ea->var;
	int ret;

	ret = kstrtou32(buf, 0, &nocm->probe_port_rotate);
	if (ret)
		return ret;

	return len;
}

const char *pfilter_name[MAX_PACKET_FILTERS] = {
	"pfilter0",
	"pfilter1",
	"pfilter2",
	"pfilter3",
};

#define init_pfilter_attr_rw(_name) {					\
	entry->pfilter[j].probe = entry;				\
	entry->pfilter[j].name = pfilter_name[j];			\
	entry->pfilter[j].attr_##_name.attr.attr.name = #_name;		\
	entry->pfilter[j].attr_##_name.attr.attr.mode =			\
		(S_IWUSR | S_IRUGO);					\
	entry->pfilter[j].attr_##_name.attr.show =			\
		pfilter_show_##_name;					\
	entry->pfilter[j].attr_##_name.attr.store =			\
		pfilter_store_##_name;					\
	entry->pfilter[j].attr_##_name.var =				\
		&entry->pfilter[j];					\
	entry->pfilter[j].pfilter_attrs[k++] =				\
		&entry->pfilter[j].attr_##_name.attr.attr;	}	\


#define define_pfilter_attr_show_func(name)				\
									\
static ssize_t pfilter_show_##name(struct kobject *kobj,		\
					struct kobj_attribute *attr,	\
					char *buf)			\
{									\
	struct kobj_ext_attribute *ea = to_ext_attr(attr);		\
	struct noc_pfilter_t *entry = (struct noc_pfilter_t *)		\
		ea->var;						\
	struct packet_filter_regs_t *pfilter_reg =			\
		(struct packet_filter_regs_t *)(entry->probe->nocm->mbase +\
		entry->macro_offset);					\
	int pos = 0;							\
	int ret;							\
									\
	ret = probe_enable_clk(entry->probe);				\
	if (ret)							\
		return ret;						\
									\
	entry->name = readl_relaxed(&pfilter_reg->filters_n_##name);	\
									\
	pos += scnprintf(buf + pos,					\
		PAGE_SIZE - pos,					\
		"0x%x\n",						\
		entry->name);						\
									\
	probe_disable_clk(entry->probe);				\
									\
	return pos;							\
}


#define define_pfilter_attr_store_func(name)				\
									\
static ssize_t pfilter_store_##name(struct kobject *kobj,		\
					struct kobj_attribute *attr,	\
					const char *buf, size_t len)	\
{									\
	struct kobj_ext_attribute *ea = to_ext_attr(attr);		\
	struct noc_pfilter_t *entry = (struct noc_pfilter_t *)		\
		ea->var;						\
	struct packet_filter_regs_t *pfilter_reg =			\
		(struct packet_filter_regs_t *)(entry->probe->nocm->mbase +\
		entry->macro_offset);					\
	int ret;							\
									\
	ret = kstrtou32(buf, 0, &entry->name);				\
	if (ret)							\
		return ret;						\
									\
	ret = probe_enable_clk(entry->probe);				\
	if (ret)							\
		return ret;						\
									\
	writel_relaxed(entry->name, &pfilter_reg->filters_n_##name);	\
									\
	probe_disable_clk(entry->probe);				\
									\
	return len;							\
}


define_pfilter_attr_show_func(user_base);
define_pfilter_attr_store_func(user_base);
define_pfilter_attr_show_func(user_mask);
define_pfilter_attr_store_func(user_mask);
define_pfilter_attr_show_func(route_id_base);
define_pfilter_attr_store_func(route_id_base);
define_pfilter_attr_show_func(route_id_mask);
define_pfilter_attr_store_func(route_id_mask);
define_pfilter_attr_show_func(addr_base_low);
define_pfilter_attr_store_func(addr_base_low);
define_pfilter_attr_show_func(window_size);
define_pfilter_attr_store_func(window_size);
define_pfilter_attr_show_func(opcode);
define_pfilter_attr_store_func(opcode);
define_pfilter_attr_show_func(status);
define_pfilter_attr_store_func(status);
define_pfilter_attr_show_func(length);
define_pfilter_attr_store_func(length);
define_pfilter_attr_show_func(urgency);
define_pfilter_attr_store_func(urgency);


int noc_probe_sysfs_init(struct noc_macro *nocm)
{
	struct platform_device *pdev = nocm->pdev;
	u32 i = 0, j = 0, k = 0;
	int ret = 0;

	nocm->probe_kobj = kobject_create_and_add("bw_probe",
		&pdev->dev.kobj);
	if (!nocm->probe_kobj)
		return -ENOMEM;

	nocm->probe_out_attr.attr.attr.name = "out";
	nocm->probe_out_attr.attr.attr.mode = S_IRUGO;
	nocm->probe_out_attr.attr.show = probe_out_show;
	nocm->probe_out_attr.var = (void *)nocm;
	ret = sysfs_create_file(nocm->probe_kobj,
		&nocm->probe_out_attr.attr.attr);
	if (ret)
		return ret;

	nocm->probe_en_attr.attr.attr.name = "en";
	nocm->probe_en_attr.attr.attr.mode = (S_IWUSR | S_IRUGO);
	nocm->probe_en_attr.attr.show = probe_en_show;
	nocm->probe_en_attr.attr.store = probe_en_store;
	nocm->probe_en_attr.var = (void *)nocm;
	ret = sysfs_create_file(nocm->probe_kobj,
		&nocm->probe_en_attr.attr.attr);
	if (ret)
		return ret;

	nocm->probe_manual_mode_attr.attr.attr.name = "manual_mode";
	nocm->probe_manual_mode_attr.attr.attr.mode = (S_IWUSR | S_IRUGO);
	nocm->probe_manual_mode_attr.attr.show = probe_manual_mode_show;
	nocm->probe_manual_mode_attr.attr.store = probe_manual_mode_store;
	nocm->probe_manual_mode_attr.var = (void *)nocm;
	ret = sysfs_create_file(nocm->probe_kobj,
		&nocm->probe_manual_mode_attr.attr.attr);
	if (ret)
		return ret;

	nocm->probe_port_rotate_attr.attr.attr.name = "port_rotate";
	nocm->probe_port_rotate_attr.attr.attr.mode = (S_IWUSR | S_IRUGO);
	nocm->probe_port_rotate_attr.attr.show = probe_port_rotate_show;
	nocm->probe_port_rotate_attr.attr.store = probe_port_rotate_store;
	nocm->probe_port_rotate_attr.var = (void *)nocm;
	ret = sysfs_create_file(nocm->probe_kobj,
		&nocm->probe_port_rotate_attr.attr.attr);
	if (ret)
		return ret;

	for (i = 0; i < nocm->probe_size; i++) {
		struct noc_probe_t *entry = nocm->probe_tbl + i;

		j = 0;
		entry->probe_kobj = kobject_create_and_add(entry->name,
			nocm->probe_kobj);
		if (!entry->probe_kobj)
			return -ENOMEM;

		init_probe_attr_rw(period);
		init_probe_attr_rw(max);
		init_probe_attr_rw(min);
		init_probe_attr_rw(mode);
		init_probe_attr_rw(disabled);
		init_probe_attr_rw(probe_event);

		entry->probe_attr_group.name = NULL;
		entry->probe_attr_group.attrs = entry->probe_attrs;

		ret = sysfs_create_group(entry->probe_kobj,
			&entry->probe_attr_group);
		if (ret)
			return ret;

		if (!entry->extra_size)
			continue;

		for (j = 0; j < entry->extra_size; j++) {
			k = 0;

			init_extra_probe_attr_rw(enabled);
			init_extra_probe_attr_rw(probe_event);

			entry->extra_probe[j].probe_attr_group.name =
				entry->extra_probe[j].name;
			entry->extra_probe[j].probe_attr_group.attrs =
				entry->extra_probe[j].probe_attrs;

			ret = sysfs_create_group(entry->probe_kobj,
				&entry->extra_probe[j].probe_attr_group);
			if (ret)
				return ret;
		}

		if (!entry->pfilter_size)
			continue;

		entry->attr_pfilter_lut.attr.attr.name = "pfilter_lut";
		entry->attr_pfilter_lut.attr.attr.mode = (S_IWUSR | S_IRUGO);
		entry->attr_pfilter_lut.attr.show = probe_pfilter_lut_show;
		entry->attr_pfilter_lut.attr.store = probe_pfilter_lut_store;
		entry->attr_pfilter_lut.var = (void *)entry;
		ret = sysfs_create_file(entry->probe_kobj,
			&entry->attr_pfilter_lut.attr.attr);
		if (ret)
			return ret;

		for (j = 0; j < entry->pfilter_size; j++) {
			k = 0;

			init_pfilter_attr_rw(user_base);
			init_pfilter_attr_rw(user_mask);
			init_pfilter_attr_rw(route_id_base);
			init_pfilter_attr_rw(route_id_mask);
			init_pfilter_attr_rw(addr_base_low);
			init_pfilter_attr_rw(window_size);
			init_pfilter_attr_rw(opcode);
			init_pfilter_attr_rw(status);
			init_pfilter_attr_rw(length);
			init_pfilter_attr_rw(urgency);

			entry->pfilter[j].pfilter_attr_group.name =
				entry->pfilter[j].name;
			entry->pfilter[j].pfilter_attr_group.attrs =
				entry->pfilter[j].pfilter_attrs;

			ret = sysfs_create_group(entry->probe_kobj,
				&entry->pfilter[j].pfilter_attr_group);
			if (ret)
				return ret;
		}
	}

	return ret;
}

int noc_probe_init(struct noc_macro *nocm)
{
	struct platform_device *pdev = nocm->pdev;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *bnp, *pp;
	struct noc_probe_t *entry;
	u32 i = 0, j = 0;
	int ret = 0;

	ret = noc_probe_common_init();
	if (ret)
		return ret;

	bnp = of_get_child_by_name(np, "bw_probe");
	if (!bnp) {
		pr_debug("bw_probe not found\n");
		return -ENODEV;
	}

	nocm->probe_size = of_get_child_count(bnp);
	nocm->probe_tbl = devm_kzalloc(&pdev->dev, nocm->probe_size *
				sizeof(struct noc_probe_t), GFP_KERNEL);
	if (!nocm->probe_tbl)
		return -ENOMEM;
	nocm->probe_port_rotate = 1;

	pr_debug("probe table[%d]\n", nocm->probe_size);

	for_each_child_of_node(bnp, pp) {
		entry = nocm->probe_tbl + i;
		i++;

		entry->name = strrchr(of_node_full_name(pp), '/') + 1;
		entry->nocm = nocm;
		entry->port = PROBE_SINGLE_PORT;
		entry->max = cmn_cfg.max;
		entry->mode = ALARM_MODE_MIN_MAX;
		entry->probe_event = PE_BYTE;

		ret = of_property_read_u32(pp, "reg_offset",
			&entry->macro_offset);
		if (ret) {
			pr_err("of_property_read_u32 off failed\n");
			return ret;
		}

		ret = of_property_read_u32(pp, "freq", &entry->mclk);
		if (ret) {
			pr_err("of_property_read_u32 freq failed\n");
			return ret;
		}

		entry->period = noc_probe_get_period(entry->mclk);
		ret = of_property_read_u32(pp, "port", &entry->port);
		ret = of_property_read_u32(pp, "disabled", &entry->disabled);
		ret = of_property_read_string(pp, "clock_name",
			&entry->clock_name);
		ret = of_property_read_u32(pp, "divider", &entry->divider);

		pr_debug("%s\t0x%x\t%d\t%d\t%d\t%s\n",
			entry->name, entry->macro_offset, entry->mclk,
			entry->port, entry->disabled, entry->clock_name);

		ret = of_property_read_u32(pp, "extra_probe_cnt",
			&entry->extra_size);
		pr_debug("extra probe cnt %d\n", entry->extra_size);
		if (entry->extra_size) {
			u32 vals[MAX_PROBE_UNITS];
			u32 cnt;

			cnt = of_property_count_elems_of_size(
				pp,
				"extra_probe_events",
				sizeof(u32));
			cnt = min(cnt, entry->extra_size);
			pr_debug("extra probe events[%d]:\n", cnt);

			ret = of_property_read_u32_array(pp,
				"extra_probe_events",
				vals,
				cnt);
			if (ret)
				continue;

			for (j = 0; j < cnt; j++) {
				entry->extra_probe[j].enabled = 1;
				entry->extra_probe[j].probe_event = vals[j];
				pr_debug("\t0x%x\n", vals[j]);
			}
		}

		ret = of_property_read_u32(pp, "filter_cnt",
			&entry->pfilter_size);
		pr_debug("filter_cnt %d\n", entry->pfilter_size);
		if (entry->pfilter_size) {
			u32 off;

			ret = of_property_read_u32(pp, "filter_offset",
				&off);
			if (ret) {
				pr_warn("filter_offset not exist!\n");
				continue;
			}

			for (j = 0; j < entry->pfilter_size; j++) {
				entry->pfilter[j].macro_offset = off +
					sizeof(struct packet_filter_regs_t)
					* j;
				pr_debug("\t0x%x\n",
					entry->pfilter[j].macro_offset);
			}
		}
	}

	for (i = 0; i < nocm->probe_size; i++) {
		entry = nocm->probe_tbl + i;

		if (entry->clock_name && entry->clk == NULL) {
			entry->clk = devm_clk_get(&nocm->pdev->dev,
				entry->clock_name);
			if (IS_ERR(entry->clk)) {
				pr_err("%s: failed get clk of %s!\n",
					__func__, entry->clock_name);
				entry->clk = NULL;
				return -1;
			}
		}
	}

	ret = noc_probe_sysfs_init(nocm);
	if (ret)
		return ret;

	nocm->probe_enable = 1;

	return 0;
}
