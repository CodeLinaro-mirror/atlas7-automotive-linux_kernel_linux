/*
 * CSR SiRF Memory Controller Monitor Driver
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

#ifndef __MEMCMON_H
#define __MEMCMON_H

struct proc_table {
	const char *name;
	umode_t mode;
	const struct file_operations *fops;
};

struct bandwidth_regs {
	u32 control;
	u32 resv_0;
	u32 select;
	u32 config_id;
	u32 config_size;
	u32 result_rdat;
	u32 result_wdat;
	u32 result_rcmd;
	u32 result_wcmd;
	u32 result_busy;
};

struct bandwidth_info {
	u64 rdat;
	u64 wdat;
	u64 rcmd;
	u64 wcmd;
	u64 busy;
};

#define DEFAULT_RLAT_THRESHOLD 0x8000
#define DEFAULT_WLAT_THRESHOLD 0x8000
struct latency_regs {
	u32 control;
	u32 resv_0;
	u32 select;
	u32 threshold;
	u32 result_rmax;
	u32 result_ravgcnt;
	u32 result_ravgsum;
	u32 result_rthrcnt;
	u32 result_wmax;
	u32 result_wavgcnt;
	u32 result_wavgsum;
	u32 result_wthrcnt;
};

struct latency_info {
	u64 rmax;
	u64 ravgcnt;
	u64 ravgsum;
	u64 rthrcnt;
	u64 wmax;
	u64 wavgcnt;
	u64 wavgsum;
	u64 wthrcnt;
};

struct address_regs {
	u32 control;
	u32 status;
	u32 select;
	u32 inten;
	u32 config_id;
	u32 saddr0;
	u32 eaddr0;
	u32 saddr1;
	u32 eaddr1;
	u32 saddr2;
	u32 eaddr2;
	u32 saddr3;
	u32 eaddr3;
	u32 resv_0;
	u32 result_id0;
	u32 result_addr0;
	u32 result_id1;
	u32 result_addr1;
	u32 result_id2;
	u32 result_addr2;
	u32 result_id3;
	u32 result_addr3;
};

#define DEFAULT_TIMEOUT_THRESHOLD 1000
struct timeout_regs {
	u32 control;
	u32 status;
	u32 select;
	u32 inten;
	u32 config;
	u32 result_id;
};

struct timeout_info {
	u8 port;
	u8 direction;
	u16 id;
};

#define PORT_NUM 8
#define MAX_TIMEOUT_INT 100
struct sirfsoc_memcmon {
	void __iomem *base;
	struct bandwidth_regs __iomem *bw_regs;
	struct latency_regs __iomem *lat_regs;
	struct address_regs __iomem *addr_regs;
	struct timeout_regs __iomem *to_regs;
	u32 __iomem *int_status;
	u32 __iomem *port_status;
	u32 __iomem *wresp_config;
	u32 __iomem *version;
	struct device *dev;
	int bw_on;
	int bw_master_size; /* bit[3:0] of BW_CONFIG_SIZE */
	int bw_master_len; /* bit[12:8] of BW_CONFIG_SIZE */
	struct workqueue_struct *bw_wq;
	struct delayed_work gfxfreq_dwork;
	int gfxfreq_auto;
	struct clk *gfx_clk;
	unsigned long gfx_rate;
	struct delayed_work vxdfreq_dwork;
	int vxdfreq_auto;
	int vxd_valid;
	struct clk *mm_clk;
	unsigned long mm_rate;
	u32 mm_rdat_ref;
	u32 mm_wdat_ref;
	u32 mm_idle_freq;
	u32 mm_min_freq;
	u32 mm_max_freq;
	int lat_on;
	int addr_on;
	int to_on;
	int rlat_threshold;
	int wlat_threshold;
	int to_threshold;
	int to_int_num;
	struct bandwidth_info bw_info[PORT_NUM];
	struct latency_info lat_info[PORT_NUM];
	struct timeout_info to_info[MAX_TIMEOUT_INT];
	struct timeval bwmon_start;
	struct timeval bwmon_stop;
	struct timeval latmon_start;
	struct timeval latmon_stop;
};

#define EN_MASK			0xff
#define BUSY_EN_MASK		(0x1 << 8)
#define PORT_MASK		0x7
#define SCALE_MASK		(0x3 << 8)
#define DAT_MASK		0x7fff
#define OVFLOW_MASK		(0x1 << 31)
#define SCALE_K			0x100
#define SCALE_M			0x200
#define ADDR_INT_MASK		0xff
#define ADDR_CFG_MASK		0x3ffffffc
#define TIMEOUT_INT_MASK	(0xff << 8)
#define RTIMEOUT_STS_MASK	0xff
#define WTIMEOUT_STS_MASK	(0xff << 16)

/* Monitor ports */
#define MONITOR_CPU			0
#define MONITOR_DSPGPS			1
#define MONITOR_MEDIA			2
#define MONITOR_GFX			3
#define MONITOR_LCD			4
#define MONITOR_VPP			5
#define MOINTOR_UUSAXI			6
#define MONITOR_SUBAXI			7

#endif /*__MEMCMON_H*/
