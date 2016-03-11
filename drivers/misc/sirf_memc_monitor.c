/*
 * CSR SiRF SoC Memory Controller Monitor Driver
 *
 * Copyright (c) 2013, 2016, The Linux Foundation. All rights reserved.
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

#define pr_fmt(fmt) "sirf_memcmon: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/err.h>
#include "sirf_memc_monitor.h"

#define DRIVER_NAME "sirf_memc_monitor"
#define PROC_NUMBUF 50
#define GFXFREQ_ADAPT_MS 1000
#define GFX_DCOUNT_REF 15000000
#define VXDFREQ_ADAPT_MS 1000

static struct proc_dir_entry *proc_root;
static const char proc_root_name[] = "sirf_memcmon";
static const char port_name[PORT_NUM][10] = {
	{"CPU"},
	{"DSPGPS"},
	{"MEDIA"},
	{"GFX"},
	{"LCD"},
	{"VPP"},
	{"UUSAXI"},
	{"SUBAXI"},
};

static long calc_elapse_time(struct timeval stop, struct timeval start)
{
	return (stop.tv_sec - start.tv_sec) * 1000 +
		(stop.tv_usec - start.tv_usec) / 1000;
}

static void sirfsoc_memcmon_port_enable(u32 __iomem *control, int port)
{
	u32 control_val = ioread32(control);
	control_val |= 1 << port;
	iowrite32(control_val, control);
}

static void sirfsoc_memcmon_port_disable(u32 __iomem *control, int port)
{
	u32 control_val = ioread32(control);
	control_val &= ~(1 << port);
	iowrite32(control_val, control);
}

static u64 sirfsoc_memcmon_scale_read(u32 __iomem *select, u32 __iomem *reg)
{
	u32 reg_val;
	u32 select_val;
	u64 count;

	select_val = ioread32(select);
	select_val &= ~SCALE_MASK; /* SCALE 1 */
	iowrite32(select_val, select);

	reg_val = ioread32(reg);
	if ((reg_val & OVFLOW_MASK) == 0)
		count = reg_val * 1;
	else {
		select_val |= SCALE_K; /* SCALE 1K */
		iowrite32(select_val, select);
		reg_val = ioread32(reg);
		if ((reg_val & OVFLOW_MASK) == 0)
			count = reg_val * 1024ULL;
		else {
			select_val &= ~SCALE_MASK;
			select_val |= SCALE_M; /* SCALE 1M */
			iowrite32(select_val, select);
			reg_val = ioread32(reg);
			if ((reg_val & OVFLOW_MASK) == 0)
				count = reg_val * 1024ULL * 1024ULL;
			else
				count = (u64)-1;
		}
	}

	return count;
}

static void sirfsoc_bwmon_get_port_info(
		struct sirfsoc_memcmon *memcmon, int port)
{
	u32 select_val;

	/* Select the port */
	select_val = ioread32(&memcmon->bw_regs->select);
	select_val &= ~PORT_MASK;
	select_val |= port;
	iowrite32(select_val, &memcmon->bw_regs->select);

	memcmon->bw_info[port].rdat = sirfsoc_memcmon_scale_read(
			&memcmon->bw_regs->select,
			&memcmon->bw_regs->result_rdat);
	memcmon->bw_info[port].wdat = sirfsoc_memcmon_scale_read(
			&memcmon->bw_regs->select,
			&memcmon->bw_regs->result_wdat);
	memcmon->bw_info[port].rcmd = sirfsoc_memcmon_scale_read(
			&memcmon->bw_regs->select,
			&memcmon->bw_regs->result_rcmd);
	memcmon->bw_info[port].wcmd = sirfsoc_memcmon_scale_read(
			&memcmon->bw_regs->select,
			&memcmon->bw_regs->result_wcmd);
	memcmon->bw_info[port].busy = sirfsoc_memcmon_scale_read(
			&memcmon->bw_regs->select,
			&memcmon->bw_regs->result_busy);
}

static void sirfsoc_bwmon_start(struct sirfsoc_memcmon *memcmon)
{
	int i;
	u32 control_val;
	u32 config_size;
	config_size = ioread32(&memcmon->bw_regs->config_size);
	config_size &= ~(0xf | 0x1f << 8);
	config_size |= memcmon->bw_master_size & 0xf;
	config_size |= (memcmon->bw_master_len & 0x1f) << 8;
	for (i = 0; i < PORT_NUM; i++) {
		iowrite32(i, &memcmon->bw_regs->select);
		iowrite32(0xffff0000, &memcmon->bw_regs->config_id);
		iowrite32(config_size, &memcmon->bw_regs->config_size);
	}
	/* Enable all ports */
	control_val = ioread32(&memcmon->bw_regs->control);
	control_val |= 0x1ff;
	iowrite32(control_val, &memcmon->bw_regs->control);
	if (memcmon->gfxfreq_auto) {
		queue_delayed_work(memcmon->bw_wq,
				&memcmon->gfxfreq_dwork,
				msecs_to_jiffies(GFXFREQ_ADAPT_MS));
	}
	if (memcmon->vxd_valid && memcmon->vxdfreq_auto) {
		queue_delayed_work(memcmon->bw_wq,
				&memcmon->vxdfreq_dwork,
				msecs_to_jiffies(VXDFREQ_ADAPT_MS));
	}
	do_gettimeofday(&memcmon->bwmon_start);
}

static void sirfsoc_bwmon_stop(struct sirfsoc_memcmon *memcmon)
{
	/* Disable all ports */
	u32 control_val = ioread32(&memcmon->bw_regs->control);
	control_val &= ~0x1ff;
	iowrite32(control_val, &memcmon->bw_regs->control);
	do_gettimeofday(&memcmon->bwmon_stop);
}

static ssize_t sirfsoc_bwmon_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct sirfsoc_memcmon *memcmon = PDE_DATA(file_inode(file));
	char buffer[PROC_NUMBUF];
	size_t len;

	if (!memcmon)
		return -ESRCH;
	len = snprintf(buffer, sizeof(buffer), "%d %d %d\n",
			memcmon->bw_on,
			memcmon->bw_master_size,
			memcmon->bw_master_len);
	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t sirfsoc_bwmon_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	struct sirfsoc_memcmon *memcmon = PDE_DATA(file_inode(file));
	char buffer[PROC_NUMBUF];
	int bw_on;
	int bw_master_size, bw_master_len;
	int rt;
	int i;

	if (!memcmon)
		return -ESRCH;
	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;
	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	rt = sscanf(buffer, "%d %d %d\n",
			&bw_on, &bw_master_size, &bw_master_len);

	if (rt < 3) {
		pr_warn("Please input 3 params as:\n"
			"param1: 0 - off, 1 - on\n"
			"param2: master size\n"
			"param3: master len\n");
		return -EINVAL;
	}

	if ((bw_on == 1) && (memcmon->bw_on == 0)) {
		memcmon->bw_master_size = bw_master_size;
		memcmon->bw_master_len = bw_master_len;
		sirfsoc_bwmon_start(memcmon);
		memcmon->bw_on = 1;
	}
	if (bw_on == 0) {
		sirfsoc_bwmon_stop(memcmon);
		memcmon->bw_on = 0;
		for (i = 0; i < PORT_NUM; i++)
			sirfsoc_bwmon_get_port_info(memcmon, i);
	}

	return count;
}

static const struct file_operations sirfsoc_bwmon_fops = {
	.owner		= THIS_MODULE,
	.read		= sirfsoc_bwmon_read,
	.write		= sirfsoc_bwmon_write,
	.llseek		= generic_file_llseek,
};

static int sirfsoc_bwinfo_proc_show(struct seq_file *m, void *v)
{
	int i;
	long ms;
	struct sirfsoc_memcmon *memcmon = m->private;

	ms = calc_elapse_time(memcmon->bwmon_stop, memcmon->bwmon_start);
	seq_printf(m, "Bandwidth Monitor @ %p\n"
		"BW_CONTROL:\t\t%x\n"
		"BW_SELECT:\t\t%x\n"
		"BW_CONFIG_ID:\t\t%x\n"
		"BW_CONFIG_SIZE:\t\t%x\n"
		"elapsed time:\t\t%ldms\n"
		,
		memcmon->bw_regs,
		ioread32(&memcmon->bw_regs->control),
		ioread32(&memcmon->bw_regs->select),
		ioread32(&memcmon->bw_regs->config_id),
		ioread32(&memcmon->bw_regs->config_size),
		ms
		);

	for (i = 0; i < PORT_NUM; i++) {
		seq_printf(m, "Port@%d: %s\n"
			"\tRDAT:	%lld\n"
			"\tWDAT:	%lld\n"
			"\tRCMD:	%lld\n"
			"\tWCMD:	%lld\n"
			"\tBUSY:	%lld\n"
			,
			i,
			port_name[i],
			memcmon->bw_info[i].rdat,
			memcmon->bw_info[i].wdat,
			memcmon->bw_info[i].rcmd,
			memcmon->bw_info[i].wcmd,
			memcmon->bw_info[i].busy
			);
	}

	return 0;
}

static int sirfsoc_bwinfo_proc_open(struct inode *inode, struct file *file)
{
	struct sirfsoc_memcmon *memcmon = PDE_DATA(file_inode(file));
	return single_open(file, sirfsoc_bwinfo_proc_show, memcmon);
}

static const struct file_operations sirfsoc_bwinfo_fops = {
	.open		= sirfsoc_bwinfo_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void sirfsoc_gfxfreq_adapt(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct sirfsoc_memcmon *memcmon = container_of(dwork,
			struct sirfsoc_memcmon, gfxfreq_dwork);
	unsigned long cur_rate = clk_get_rate(memcmon->gfx_clk);

	sirfsoc_memcmon_port_disable(&memcmon->bw_regs->control, MONITOR_GFX);
	sirfsoc_bwmon_get_port_info(memcmon, MONITOR_GFX);
	if (cur_rate == memcmon->gfx_rate &&
			memcmon->bw_info[MONITOR_GFX].rdat < GFX_DCOUNT_REF &&
			memcmon->bw_info[MONITOR_GFX].wdat < GFX_DCOUNT_REF)
		clk_set_rate(memcmon->gfx_clk, memcmon->gfx_rate >> 1);
	else if ((cur_rate == memcmon->gfx_rate >> 1) &&
			memcmon->bw_info[MONITOR_GFX].rdat > GFX_DCOUNT_REF &&
			memcmon->bw_info[MONITOR_GFX].wdat > GFX_DCOUNT_REF)
		clk_set_rate(memcmon->gfx_clk, memcmon->gfx_rate);

	sirfsoc_memcmon_port_enable(&memcmon->bw_regs->control, MONITOR_GFX);

	if (memcmon->bw_on && memcmon->gfxfreq_auto)
		queue_delayed_work(memcmon->bw_wq, &memcmon->gfxfreq_dwork,
			msecs_to_jiffies(GFXFREQ_ADAPT_MS));
	else
		clk_set_rate(memcmon->gfx_clk, memcmon->gfx_rate);
}

static ssize_t sirfsoc_gfxfreq_auto_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct sirfsoc_memcmon *memcmon = PDE_DATA(file_inode(file));
	char buffer[PROC_NUMBUF];
	size_t len;

	if (!memcmon)
		return -ESRCH;
	len = snprintf(buffer, sizeof(buffer), "%d\n", memcmon->gfxfreq_auto);
	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t sirfsoc_gfxfreq_auto_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct sirfsoc_memcmon *memcmon = PDE_DATA(file_inode(file));
	int gfxfreq_auto;

	if (!memcmon)
		return -ESRCH;

	if (!access_ok(VERIFY_READ, buf, count))
		goto out;

	sscanf(buf, "%d", &gfxfreq_auto);
	if (gfxfreq_auto > 0)
		memcmon->gfxfreq_auto = 1;
	else
		memcmon->gfxfreq_auto = 0;
out:
	return count;
}

static const struct file_operations sirfsoc_gfxfreq_auto_fops = {
	.owner		= THIS_MODULE,
	.read		= sirfsoc_gfxfreq_auto_read,
	.write		= sirfsoc_gfxfreq_auto_write,
	.llseek		= generic_file_llseek,
};

static void sirfsoc_vxdfreq_adapt(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct sirfsoc_memcmon *memcmon = container_of(dwork,
			struct sirfsoc_memcmon, vxdfreq_dwork);
	struct bandwidth_info *mm_info = &memcmon->bw_info[MONITOR_MEDIA];

	sirfsoc_memcmon_port_disable(&memcmon->bw_regs->control,
			MONITOR_MEDIA);
	sirfsoc_bwmon_get_port_info(memcmon, MONITOR_MEDIA);

	if (mm_info->rdat == 0 && mm_info->wdat == 0)
		clk_set_rate(memcmon->mm_clk, memcmon->mm_idle_freq);
	else if (mm_info->rdat < memcmon->mm_rdat_ref &&
			mm_info->wdat < memcmon->mm_wdat_ref)
		clk_set_rate(memcmon->mm_clk, memcmon->mm_min_freq);
	else
		clk_set_rate(memcmon->mm_clk, memcmon->mm_max_freq);

	sirfsoc_memcmon_port_enable(&memcmon->bw_regs->control, MONITOR_MEDIA);

	if (memcmon->bw_on && memcmon->vxdfreq_auto)
		queue_delayed_work(memcmon->bw_wq, &memcmon->vxdfreq_dwork,
			msecs_to_jiffies(VXDFREQ_ADAPT_MS));
	else
		clk_set_rate(memcmon->mm_clk, memcmon->mm_rate);
}

static ssize_t sirfsoc_vxdfreq_auto_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct sirfsoc_memcmon *memcmon = PDE_DATA(file_inode(file));
	char buffer[PROC_NUMBUF];
	size_t len;

	if (!memcmon)
		return -ESRCH;
	len = snprintf(buffer, sizeof(buffer), "%d\n", memcmon->vxdfreq_auto);
	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t sirfsoc_vxdfreq_auto_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct sirfsoc_memcmon *memcmon = PDE_DATA(file_inode(file));
	int vxdfreq_auto;

	if (!memcmon)
		return -ESRCH;

	if (!access_ok(VERIFY_READ, buf, count))
		goto out;

	sscanf(buf, "%d", &vxdfreq_auto);
	memcmon->vxdfreq_auto = !!vxdfreq_auto;
out:
	return count;
}

static const struct file_operations sirfsoc_vxdfreq_auto_fops = {
	.owner		= THIS_MODULE,
	.read		= sirfsoc_vxdfreq_auto_read,
	.write		= sirfsoc_vxdfreq_auto_write,
	.llseek		= generic_file_llseek,
};

static void sirfsoc_latmon_get_port_info(struct sirfsoc_memcmon *memcmon,
		int port)
{
	u32 select_val;

	/* Select the port */
	select_val = ioread32(&memcmon->lat_regs->select);
	select_val &= ~PORT_MASK;
	select_val |= port;
	iowrite32(select_val, &memcmon->lat_regs->select);

	memcmon->lat_info[port].rmax = sirfsoc_memcmon_scale_read(
			&memcmon->lat_regs->select,
			&memcmon->lat_regs->result_rmax);
	memcmon->lat_info[port].ravgcnt = sirfsoc_memcmon_scale_read(
			&memcmon->lat_regs->select,
			&memcmon->lat_regs->result_ravgcnt);
	memcmon->lat_info[port].ravgsum = sirfsoc_memcmon_scale_read(
			&memcmon->lat_regs->select,
			&memcmon->lat_regs->result_ravgsum);
	memcmon->lat_info[port].rthrcnt = sirfsoc_memcmon_scale_read(
			&memcmon->lat_regs->select,
			&memcmon->lat_regs->result_rthrcnt);
	memcmon->lat_info[port].wmax = sirfsoc_memcmon_scale_read(
			&memcmon->lat_regs->select,
			&memcmon->lat_regs->result_wmax);
	memcmon->lat_info[port].wavgcnt = sirfsoc_memcmon_scale_read(
			&memcmon->lat_regs->select,
			&memcmon->lat_regs->result_wavgcnt);
	memcmon->lat_info[port].wavgsum = sirfsoc_memcmon_scale_read(
			&memcmon->lat_regs->select,
			&memcmon->lat_regs->result_wavgsum);
	memcmon->lat_info[port].wthrcnt = sirfsoc_memcmon_scale_read(
			&memcmon->lat_regs->select,
			&memcmon->lat_regs->result_wthrcnt);
}

static void sirfsoc_latmon_start(struct sirfsoc_memcmon *memcmon)
{
	int i;
	u32 control_val;
	for (i = 0; i < PORT_NUM; i++) {
		iowrite32(i, &memcmon->lat_regs->select);
		iowrite32(memcmon->rlat_threshold
			| memcmon->wlat_threshold << 16,
			&memcmon->lat_regs->threshold);
	}
	/* Enable all ports */
	control_val = ioread32(&memcmon->lat_regs->control);
	control_val |= 0x1ff;
	iowrite32(control_val, &memcmon->lat_regs->control);
	do_gettimeofday(&memcmon->latmon_start);
}

static void sirfsoc_latmon_stop(struct sirfsoc_memcmon *memcmon)
{
	/* Disable all ports */
	u32 control_val = ioread32(&memcmon->lat_regs->control);
	control_val &= ~0x1ff;
	iowrite32(control_val, &memcmon->lat_regs->control);
	do_gettimeofday(&memcmon->latmon_stop);
}

static ssize_t sirfsoc_latmon_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct sirfsoc_memcmon *memcmon = PDE_DATA(file_inode(file));
	char buffer[PROC_NUMBUF];
	size_t len;

	if (!memcmon)
		return -ESRCH;
	len = snprintf(buffer, sizeof(buffer), "%d %d %d\n",
			memcmon->lat_on,
			memcmon->rlat_threshold,
			memcmon->wlat_threshold);
	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t sirfsoc_latmon_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	struct sirfsoc_memcmon *memcmon = PDE_DATA(file_inode(file));
	char buffer[PROC_NUMBUF];
	int lat_on;
	int rlat_threshold, wlat_threshold;
	int rt;
	int i;

	if (!memcmon)
		return -ESRCH;
	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;
	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	rt = sscanf(buffer, "%d %d %d\n",
			&lat_on, &rlat_threshold, &wlat_threshold);

	if (rt < 3) {
		pr_warn("Please input 3 params as:\n"
			"param1: 0 - off, 1 - on\n"
			"param2: rlat_threshold\n"
			"param3: wlat_threshold\n");
		return -EINVAL;
	}

	if ((lat_on == 1) && (memcmon->lat_on == 0)) {
		memcmon->rlat_threshold = rlat_threshold;
		memcmon->wlat_threshold = wlat_threshold;
		sirfsoc_latmon_start(memcmon);
		memcmon->lat_on = 1;
	}
	if (lat_on == 0) {
		sirfsoc_latmon_stop(memcmon);
		memcmon->lat_on = 0;
		for (i = 0; i < PORT_NUM; i++)
			sirfsoc_latmon_get_port_info(memcmon, i);
	}

	return count;
}

static const struct file_operations sirfsoc_latmon_fops = {
	.owner		= THIS_MODULE,
	.read		= sirfsoc_latmon_read,
	.write		= sirfsoc_latmon_write,
	.llseek		= generic_file_llseek,
};

static int sirfsoc_latinfo_proc_show(struct seq_file *m, void *v)
{
	int i;
	long ms;
	struct sirfsoc_memcmon *memcmon = m->private;

	ms = calc_elapse_time(memcmon->latmon_stop, memcmon->latmon_start);
	seq_printf(m, "Latency Monitor @ %p\n"
		"LAT_CONTROL:\t\t%x\n"
		"LAT_SELECT:\t\t%x\n"
		"LAT_THRESHOLD:\t\t%x\n"
		"elapsed time:\t\t%ldms\n"
		,
		memcmon->lat_regs,
		ioread32(&memcmon->lat_regs->control),
		ioread32(&memcmon->lat_regs->select),
		ioread32(&memcmon->lat_regs->threshold),
		ms
		);

	for (i = 0; i < PORT_NUM; i++) {
		seq_printf(m, "Port@%d: %s\n"
			"\tRMAX:	%lld\n"
			"\tRAVGCNT:	%lld\n"
			"\tRAVGSUM:	%lld\n"
			"\tRTHRCNT:	%lld\n"
			"\tWMAX:	%lld\n"
			"\tWAVGCNT:	%lld\n"
			"\tWAVGSUM:	%lld\n"
			"\tWTHRCNT:	%lld\n"
			,
			i,
			port_name[i],
			memcmon->lat_info[i].rmax,
			memcmon->lat_info[i].ravgcnt,
			memcmon->lat_info[i].ravgsum,
			memcmon->lat_info[i].rthrcnt,
			memcmon->lat_info[i].wmax,
			memcmon->lat_info[i].wavgcnt,
			memcmon->lat_info[i].wavgsum,
			memcmon->lat_info[i].wthrcnt
			);
	}

	return 0;
}

static int sirfsoc_latinfo_proc_open(struct inode *inode, struct file *file)
{
	struct sirfsoc_memcmon *memcmon = PDE_DATA(file_inode(file));
	return single_open(file, sirfsoc_latinfo_proc_show, memcmon);
}

static const struct file_operations sirfsoc_latinfo_fops = {
	.open		= sirfsoc_latinfo_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void sirfsoc_addrmon_set_addr(struct sirfsoc_memcmon *memcmon,
		u32 addr[])
{
	int i;
	for (i = 0; i < PORT_NUM; i++) {
		iowrite32(i, &memcmon->addr_regs->select);
		iowrite32(addr[0] & ADDR_CFG_MASK,
				&memcmon->addr_regs->saddr0);
		iowrite32(addr[1] & ADDR_CFG_MASK,
				&memcmon->addr_regs->eaddr0);
		iowrite32(addr[2] & ADDR_CFG_MASK,
				&memcmon->addr_regs->saddr1);
		iowrite32(addr[3] & ADDR_CFG_MASK,
				&memcmon->addr_regs->eaddr1);
		iowrite32(addr[4] & ADDR_CFG_MASK,
				&memcmon->addr_regs->saddr2);
		iowrite32(addr[5] & ADDR_CFG_MASK,
				&memcmon->addr_regs->eaddr2);
		iowrite32(addr[6] & ADDR_CFG_MASK,
				&memcmon->addr_regs->saddr3);
		iowrite32(addr[7] & ADDR_CFG_MASK,
				&memcmon->addr_regs->eaddr3);
	}
}

static void sirfsoc_addrmon_start(struct sirfsoc_memcmon *memcmon)
{
	/* Enable all ports */
	iowrite32(0xffffffff, &memcmon->addr_regs->inten);
	iowrite32(0xff, &memcmon->addr_regs->control);
}

static void sirfsoc_addrmon_stop(struct sirfsoc_memcmon *memcmon)
{
	/* Disable all ports */
	iowrite32(0x0, &memcmon->addr_regs->inten);
	iowrite32(0x0, &memcmon->addr_regs->control);
}

static ssize_t sirfsoc_addrmon_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct sirfsoc_memcmon *memcmon = PDE_DATA(file_inode(file));
	char buffer[PROC_NUMBUF];
	size_t len;

	if (!memcmon)
		return -ESRCH;
	len = snprintf(buffer, sizeof(buffer), "%d\n", memcmon->addr_on);
	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t sirfsoc_addrmon_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct sirfsoc_memcmon *memcmon = PDE_DATA(file_inode(file));
	char buffer[PROC_NUMBUF + 100];
	int addr_on;
	int rt;
	u32 addr[8];

	if (!memcmon)
		return -ESRCH;
	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;
	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	rt = sscanf(buffer, "%d 0x%x-0x%x 0x%x-0x%x 0x%x-0x%x 0x%x-0x%x\n",
			&addr_on,
			&addr[0], &addr[1],
			&addr[2], &addr[3],
			&addr[4], &addr[5],
			&addr[6], &addr[7]);
	if ((rt == 1) && (addr_on == 0)) {
		sirfsoc_addrmon_stop(memcmon);
		memcmon->addr_on = 0;
	} else if ((rt == 9) && (addr_on == 1) && (memcmon->addr_on == 0)) {
		sirfsoc_addrmon_set_addr(memcmon, addr);
		sirfsoc_addrmon_start(memcmon);
		memcmon->addr_on = 1;
	} else {
		pr_warn("Usage:\n"
			"\tstart:\n"
			"\techo 1 0x<saddr0>-0x<eaddr0> 0x<saddr1>-0x<eaddr1>"
			"0x<saddr2>-0x<eaddr2> 0x<saddr3>-0x<eaddr3> > addrmon\n"
			"\tstop:\n"
			"\techo 0 > addrmon\n");
		return -EINVAL;
	}

	return count;
}

static const struct file_operations sirfsoc_addrmon_fops = {
	.owner		= THIS_MODULE,
	.read		= sirfsoc_addrmon_read,
	.write		= sirfsoc_addrmon_write,
	.llseek		= generic_file_llseek,
};

static void sirfsoc_tomon_start(struct sirfsoc_memcmon *memcmon)
{
	memcmon->to_int_num = 0;
	memset(memcmon->to_info, 0,
			sizeof(struct timeout_info) * MAX_TIMEOUT_INT);
	iowrite32(memcmon->to_threshold, &memcmon->to_regs->config);
	iowrite32(0x00ff00ff, &memcmon->to_regs->status);
	iowrite32(0xff, &memcmon->to_regs->inten);
	iowrite32(0xff, &memcmon->to_regs->control);
}

static void sirfsoc_tomon_stop(struct sirfsoc_memcmon *memcmon)
{
	iowrite32(0x0, &memcmon->to_regs->inten);
	iowrite32(0x0, &memcmon->to_regs->control);
}

static ssize_t sirfsoc_tomon_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct sirfsoc_memcmon *memcmon = PDE_DATA(file_inode(file));
	char buffer[PROC_NUMBUF];
	size_t len;

	if (!memcmon)
		return -ESRCH;
	len = snprintf(buffer, sizeof(buffer), "%d %d\n",
			memcmon->to_on,
			memcmon->to_threshold);
	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t sirfsoc_tomon_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	struct sirfsoc_memcmon *memcmon = PDE_DATA(file_inode(file));
	char buffer[PROC_NUMBUF];
	int to_on;
	int to_threshold;
	int rt;

	if (!memcmon)
		return -ESRCH;
	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;
	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	rt = sscanf(buffer, "%d %d\n", &to_on, &to_threshold);

	if (rt < 2) {
		pr_warn("Please input 2 params as:\n"
			"param1: 0 - off, 1 - on\n"
			"param2: to_threshold\n");
		return -EINVAL;
	}

	if ((to_on == 1) && (memcmon->to_on == 0)) {
		memcmon->to_threshold = to_threshold;
		sirfsoc_tomon_start(memcmon);
		memcmon->to_on = 1;
	}
	if (to_on == 0) {
		sirfsoc_tomon_stop(memcmon);
		memcmon->to_on = 0;
	}

	return count;
}

static const struct file_operations sirfsoc_tomon_fops = {
	.owner		= THIS_MODULE,
	.read		= sirfsoc_tomon_read,
	.write		= sirfsoc_tomon_write,
	.llseek		= generic_file_llseek,
};

static int sirfsoc_toinfo_proc_show(struct seq_file *m, void *v)
{
	int i;
	struct sirfsoc_memcmon *memcmon = m->private;

	if (memcmon->to_on == 1) {
		pr_warn("Please stop timeout monitor by 'echo 0 0 > tomon' before getting info\n");
		goto out;
	}

	seq_printf(m, "Timeout Monitor @ %p, interrupts=%d\n"
		"TIMEOUT_CONTROL:\t\t%x\n"
		"TIMEOUT_STATUS:\t\t%x\n"
		"TIMEOUT_INTEN:\t\t%x\n"
		"TIMEOUT_THRESHOLD:\t\t%x\n"
		,
		memcmon->to_regs, memcmon->to_int_num,
		ioread32(&memcmon->to_regs->control),
		ioread32(&memcmon->to_regs->status),
		ioread32(&memcmon->to_regs->inten),
		ioread32(&memcmon->to_regs->config)
		);

	seq_puts(m, "Format: [Port,Direction,Master ID]\n");
	for (i = 0; i < memcmon->to_int_num; i++) {
		seq_printf(m, "[%d, %d, %d]\t",
				memcmon->to_info[i].port,
				memcmon->to_info[i].direction,
				memcmon->to_info[i].id);
	}

out:
	return 0;
}

static int sirfsoc_toinfo_proc_open(struct inode *inode, struct file *file)
{
	struct sirfsoc_memcmon *memcmon = PDE_DATA(file_inode(file));
	return single_open(file, sirfsoc_toinfo_proc_show, memcmon);
}

static const struct file_operations sirfsoc_toinfo_fops = {
	.open		= sirfsoc_toinfo_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static struct proc_table sirfsoc_memcmon_pt[] = {
	{
		.name	= "bwmon",
		.mode	= S_IRWXUGO,
		.fops	= &sirfsoc_bwmon_fops,
	},
	{
		.name	= "bwinfo",
		.mode	= S_IRUGO,
		.fops	= &sirfsoc_bwinfo_fops,
	},
	{
		.name	= "gfxfreq_auto",
		.mode	= S_IRWXUGO,
		.fops	= &sirfsoc_gfxfreq_auto_fops,
	},
	{
		.name	= "latmon",
		.mode	= S_IRWXUGO,
		.fops	= &sirfsoc_latmon_fops,
	},
	{
		.name	= "latinfo",
		.mode	= S_IRUGO,
		.fops	= &sirfsoc_latinfo_fops,
	},
	{
		.name	= "addrmon",
		.mode	= S_IRWXUGO,
		.fops	= &sirfsoc_addrmon_fops,
	},
	{
		.name	= "tomon",
		.mode	= S_IRWXUGO,
		.fops	= &sirfsoc_tomon_fops,
	},
	{
		.name	= "toinfo",
		.mode	= S_IRUGO,
		.fops	= &sirfsoc_toinfo_fops,
	},
	{ }
};

static int sirfsoc_memcmon_proc_init(struct sirfsoc_memcmon *memcmon)
{
	struct proc_table *entry;
	proc_root = proc_mkdir_mode(proc_root_name, S_IRWXUGO, NULL);
	if (!proc_root) {
		pr_err("cannot create /proc/%s\n", proc_root_name);
		return 1;
	}

	for (entry = sirfsoc_memcmon_pt; entry->name; entry++)
		proc_create_data(entry->name, entry->mode, proc_root,
				entry->fops, memcmon);

	return 0;
}

static void sirfsoc_memcmon_bw_init(struct bandwidth_regs *bw_regs)
{
	int i;

	iowrite32(0x0, &bw_regs->control);
	for (i = 0; i < PORT_NUM; i++) {
		iowrite32(i, &bw_regs->select);
		iowrite32(0xffff0000, &bw_regs->config_id);
		iowrite32(0x1 << 3 | 0x1 << 12, &bw_regs->config_size);
	}
}

static void sirfsoc_memcmon_lat_init(struct latency_regs *lat_regs)
{
	int i;

	iowrite32(0x0, &lat_regs->control);
	for (i = 0; i < PORT_NUM; i++) {
		iowrite32(i, &lat_regs->select);
		iowrite32(DEFAULT_RLAT_THRESHOLD | DEFAULT_WLAT_THRESHOLD << 16,
				&lat_regs->threshold);
	}
}

static void sirfsoc_memcmon_addr_init(struct address_regs *addr_regs)
{
	iowrite32(0x0, &addr_regs->control);
	iowrite32(0xffffffff, &addr_regs->status);
	iowrite32(0x0, &addr_regs->inten);
	iowrite32(0xffff0000, &addr_regs->config_id);
}

static void sirfsoc_memcmon_to_init(struct timeout_regs *to_regs)
{
	iowrite32(0x0, &to_regs->control);
	iowrite32(0x00ff00ff, &to_regs->status);
	iowrite32(DEFAULT_TIMEOUT_THRESHOLD, &to_regs->config);
	iowrite32(0x0, &to_regs->inten);
}

static void sirfsoc_memcmon_init(struct sirfsoc_memcmon *memcmon)
{
	memcmon->bw_regs = memcmon->base;
	memcmon->lat_regs = memcmon->base + 0x40;
	memcmon->addr_regs = memcmon->base + 0x80;
	memcmon->to_regs = memcmon->base + 0x100;
	memcmon->int_status = memcmon->base + 0x180;
	memcmon->rlat_threshold = DEFAULT_RLAT_THRESHOLD;
	memcmon->wlat_threshold = DEFAULT_WLAT_THRESHOLD;
	memcmon->to_threshold = DEFAULT_TIMEOUT_THRESHOLD;

	sirfsoc_memcmon_bw_init(memcmon->bw_regs);
	sirfsoc_memcmon_lat_init(memcmon->lat_regs);
	sirfsoc_memcmon_addr_init(memcmon->addr_regs);
	sirfsoc_memcmon_to_init(memcmon->to_regs);
}

static irqreturn_t sirfsoc_memcmon_addr_irq(struct sirfsoc_memcmon *memcmon,
		u32 int_status)
{
	u32 addr_status;
	u32 result_id = 0, result_addr = 0;
	int port, port_status, range;
	unsigned long addr_int;

	addr_int = int_status & ADDR_INT_MASK;
	port = find_first_bit(&addr_int, 32);
	addr_status = ioread32(&memcmon->addr_regs->status);
	port_status = (addr_status >> (port * 4)) & 0xf;
	if (port_status & 0x1) {
		range = 0;
		result_id = ioread32(&memcmon->addr_regs->result_id0);
		result_addr = ioread32(&memcmon->addr_regs->result_addr0);
	}
	if (port_status & 0x2) {
		range = 1;
		result_id = ioread32(&memcmon->addr_regs->result_id1);
		result_addr = ioread32(&memcmon->addr_regs->result_addr1);
	}
	if (port_status & 0x4) {
		range = 2;
		result_id = ioread32(&memcmon->addr_regs->result_id2);
		result_addr = ioread32(&memcmon->addr_regs->result_addr2);
	}
	if (port_status & 0x8) {
		range = 3;
		result_id = ioread32(&memcmon->addr_regs->result_id3);
		result_addr = ioread32(&memcmon->addr_regs->result_addr3);
	}
	pr_alert("addr range%d be hit, id=0x%x, addr=0x%x\n",
			range, result_id, result_addr);

	iowrite32(addr_status, &memcmon->addr_regs->status);
	sirfsoc_addrmon_stop(memcmon);

	return IRQ_HANDLED;
}

static irqreturn_t sirfsoc_memcmon_timeout_irq(
		struct sirfsoc_memcmon *memcmon, u32 int_status)
{
	u32 result_id;
	unsigned long timeout_int;
	unsigned long timeout_status;
	unsigned long control;
	int port;
	int direction;

	timeout_int = (int_status & TIMEOUT_INT_MASK) >> 8;
	port = find_first_bit(&timeout_int, 32);

	timeout_status = ioread32(&memcmon->to_regs->status);
	if (timeout_status & RTIMEOUT_STS_MASK)
		direction = 0; /* Read */
	else
		direction = 1; /* Write */

	/* Get master ID */
	iowrite32(port, &memcmon->to_regs->select);
	result_id = ioread32(&memcmon->to_regs->result_id);
	if (direction == 1)
		result_id = result_id >> 16;
	result_id &= 0xffff;

	memcmon->to_int_num++;
	if (memcmon->to_int_num > MAX_TIMEOUT_INT) {
		iowrite32(0x00, &memcmon->to_regs->inten);
		pr_warn("Over MAX_TIMEOUNT_INT(%d), should stop timeout monitor!\n",
				MAX_TIMEOUT_INT);
		goto out;
	}

	/* Record timeout interrupt info */
	memcmon->to_info[memcmon->to_int_num - 1].port = port;
	memcmon->to_info[memcmon->to_int_num - 1].direction = direction;
	memcmon->to_info[memcmon->to_int_num - 1].id  = result_id;

	/* Wirte back to clear the set bit */
	iowrite32(timeout_status, &memcmon->to_regs->status);
	/* Disable and reset the port monitor */
	control = ioread32(&memcmon->to_regs->control);
	clear_bit(port, &control);
	iowrite32(control, &memcmon->to_regs->control);
	set_bit(port, &control);
	iowrite32(control, &memcmon->to_regs->control);

out:
	return IRQ_HANDLED;
}

static irqreturn_t sirfsoc_memcmon_irq(int irq, void *dev_id)
{
	struct sirfsoc_memcmon *memcmon = dev_id;
	u32 int_status;

	int_status = ioread32(memcmon->int_status);
	if (int_status & TIMEOUT_INT_MASK)
		return sirfsoc_memcmon_timeout_irq(memcmon, int_status);
	else if (int_status & ADDR_INT_MASK)
		return sirfsoc_memcmon_addr_irq(memcmon, int_status);
	else
		return IRQ_NONE;
}

static int sirfsoc_memcmon_prepare_vxd(struct sirfsoc_memcmon *memcmon)
{
		memcmon->mm_clk = clk_get(memcmon->dev, "mm");
		if (IS_ERR(memcmon->mm_clk)) {
			pr_err("get mm_clk failed!\n");
			return PTR_ERR(memcmon->mm_clk);
		}
		proc_create_data("vxdfreq_auto", S_IRWXUGO, proc_root,
				&sirfsoc_vxdfreq_auto_fops, memcmon);
		memcmon->mm_rate = clk_get_rate(memcmon->mm_clk);
		INIT_DELAYED_WORK(&memcmon->vxdfreq_dwork,
				sirfsoc_vxdfreq_adapt);
		of_property_read_u32(memcmon->dev->of_node, "mm-rdat-ref",
				&memcmon->mm_rdat_ref);
		of_property_read_u32(memcmon->dev->of_node, "mm-wdat-ref",
				&memcmon->mm_wdat_ref);
		of_property_read_u32(memcmon->dev->of_node, "mm-idle-freq",
				&memcmon->mm_idle_freq);
		of_property_read_u32(memcmon->dev->of_node, "mm-min-freq",
				&memcmon->mm_min_freq);
		of_property_read_u32(memcmon->dev->of_node, "mm-max-freq",
				&memcmon->mm_max_freq);
		return 0;
}

static int sirfsoc_memcmon_probe(struct platform_device *pdev)
{
	struct sirfsoc_memcmon *memcmon;
	struct resource *mem_res;
	struct device_node *mm_node = NULL;
	int irq;
	int ret;

	memcmon = devm_kzalloc(&pdev->dev, sizeof(*memcmon), GFP_KERNEL);
	if (!memcmon) {
		dev_err(&pdev->dev, "Can't allocated driver data\n");
		return -ENOMEM;
	}

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	memcmon->base = devm_ioremap_resource(&pdev->dev, mem_res);
	if (IS_ERR(memcmon->base))
		return PTR_ERR(memcmon->base);
	dev_dbg(&pdev->dev, "memcmon->base:%p\n", memcmon->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "Can't allocate irq!\n");
		return -ENXIO;
	}
	ret = devm_request_irq(&pdev->dev, irq, sirfsoc_memcmon_irq, 0,
			DRIVER_NAME, memcmon);
	if (ret) {
		dev_err(&pdev->dev, "Can't register irq handle!\n");
		return ret;
	}

	memcmon->dev = &pdev->dev;
	platform_set_drvdata(pdev, memcmon);

	sirfsoc_memcmon_init(memcmon);

	if (sirfsoc_memcmon_proc_init(memcmon))
		return -ENOMEM;

	memcmon->bw_wq = create_workqueue("bw_wq");
	if (!memcmon->bw_wq) {
		dev_err(&pdev->dev, "create workqueue failed!\n");
		return -ENOMEM;
	}
	INIT_DELAYED_WORK(&memcmon->gfxfreq_dwork, sirfsoc_gfxfreq_adapt);

	memcmon->gfx_clk = clk_get(&pdev->dev, "gfx");
	if (IS_ERR(memcmon->gfx_clk)) {
		dev_err(&pdev->dev, "get gfx_clk failed!\n");
		return PTR_ERR(memcmon->gfx_clk);
	}
	memcmon->gfx_rate = clk_get_rate(memcmon->gfx_clk);

	mm_node = of_find_node_by_name(NULL, "multimedia");
	if (mm_node) {
		memcmon->vxd_valid = !sirfsoc_memcmon_prepare_vxd(memcmon);
		of_node_put(mm_node);
	}

	return 0;
}

static int sirfsoc_memcmon_remove(struct platform_device *pdev)
{
	struct sirfsoc_memcmon *memcmon = platform_get_drvdata(pdev);
	cancel_delayed_work_sync(&memcmon->gfxfreq_dwork);
	cancel_delayed_work_sync(&memcmon->vxdfreq_dwork);
	destroy_workqueue(memcmon->bw_wq);
	clk_set_rate(memcmon->gfx_clk, memcmon->gfx_rate);
	clk_put(memcmon->gfx_clk);
	clk_set_rate(memcmon->mm_clk, memcmon->mm_rate);
	clk_put(memcmon->mm_clk);
	remove_proc_subtree(proc_root_name, NULL);
	return 0;
}

static const struct of_device_id sirfsoc_memcmon_of_match[] = {
	{ .compatible = "sirf,prima2-memcmon", },
	{}
};
MODULE_DEVICE_TABLE(of, sirfsoc_memcmon_of_match);

static struct platform_driver sirfsoc_memcmon_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = sirfsoc_memcmon_of_match,
	},
	.probe = sirfsoc_memcmon_probe,
	.remove = sirfsoc_memcmon_remove,
};
module_platform_driver(sirfsoc_memcmon_driver);

MODULE_DESCRIPTION("SiRF SoC memory controller monitor driver");
MODULE_LICENSE("GPL v2");
