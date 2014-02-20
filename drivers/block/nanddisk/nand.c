/*
 * Nanddisk driver for PRIMA/ATLAS series
 *
 * Copyright 2011 (C) CSR plc.
 *	Peiyu Li <peiyu.li@csr.com>
 *	Huayi Li <huayi.li@csr.com>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_fdt.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/clk.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/platform_device.h>
#include <linux/bootmem.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <linux/semaphore.h>
#include <linux/dmaengine.h>
#include <linux/memblock.h>
#include <linux/suspend.h>
#include <linux/sirfsoc_dma.h>

#include "nanddisk.h"

/* device info */
struct nanddisk_device {
	/* os disk info */
	int major_num;
	unsigned sector_size_shift;
	int sectors_num;
	u64 size;
	spinlock_t lock;
	struct gendisk *gd;
	struct request_queue *queue;
	struct request *req;

	struct mutex mutex;

	/* data for transferring */
	void *data_buf;

	/* buffer for u-boot */
	void *uboot_buf;
	unsigned char *uboot_write_map;
	unsigned uboot_sec_num;

	/* nand info */
	struct NAND_CHIP_INFO	nand_chip_info;
	unsigned bytes_per_block;

	/* power status */
	unsigned power;

	/* information got from dtb */
	unsigned nandinsert;
	unsigned boot_zone_log_sector_start;
	unsigned boot_zone_log_sector_num;
	unsigned nanddisk_code_start;
	unsigned nanddisk_code_size;
	unsigned uboot_commit_flag;

	/* address map and irq resource */
	struct ADDRMAP *addr_map_tbl;
	unsigned addr_entry_num;
	int irq;
	unsigned irq_num;
	unsigned int dma_num;
	struct dma_chan *rw_chan;
	struct pinctrl *pinctrl;

	struct clk *nand_clk;

	unsigned irq_pending;

	/* nanddisk call entry */
	PFN_NANDDISK_IOCTRL pfn_ioctrl;

	struct task_struct *pending_task;
	unsigned pending_async_status;

	struct tasklet_struct ist_tasklet;
	struct task_struct *wearlevel_task;
	int need_wearlevel;

	struct task_struct *transfer_task;

	unsigned irq_count;
};

static struct nanddisk_device   nand_dev;

/* param area in uboot is 2K */
#define BOOT_IMAGE_SECURE_HEADER_SIZE 0x800

#define UBOOT_MAX_SECTOR 1024
#define UBOOT_MAX_LENGTH 0x80000

static int __init sirf_fdt_handle_rsv_mem(unsigned long node, const char *uname,
				int depth, void *data)
{
	__be32 *mem_info;
	unsigned long len;

	mem_info = of_get_flat_dt_prop(node,
			"sirf,nanddisk-rsvmem-range", &len);
	if (!mem_info || (len != 2 * sizeof(unsigned long)))
		return 0;

	nand_dev.nanddisk_code_start = be32_to_cpu(mem_info[0]);
	nand_dev.nanddisk_code_size = be32_to_cpu(mem_info[1]);

	if (memblock_reserve(nand_dev.nanddisk_code_start,
			nand_dev.nanddisk_code_size))
		pr_err("failed to reserve memory(0x%x bytes at 0x%x)\n",
			nand_dev.nanddisk_code_size,
			nand_dev.nanddisk_code_start);

	pr_debug("get rsv memory 0x%x-0x%x\n", nand_dev.nanddisk_code_start,
		nand_dev.nanddisk_code_size);

	return 1;
}

void  __init sirfsoc_nand_reserve_memblock(void)
{
	if (!of_scan_flat_dt(sirf_fdt_handle_rsv_mem, NULL))
		pr_err("failed to find reserved memory.\n");
}
EXPORT_SYMBOL(sirfsoc_nand_reserve_memblock);

void  __init sirfsoc_nand_nosave_memblock(void)
{
	register_nosave_region_late(
		__phys_to_pfn(nand_dev.nanddisk_code_start),
		__phys_to_pfn(nand_dev.nanddisk_code_start +
				nand_dev.nanddisk_code_size));
}
EXPORT_SYMBOL(sirfsoc_nand_nosave_memblock);

static int nanddisk_io_session(unsigned handle,
		unsigned ioctrl_code,
		void	 *in_buf,
		unsigned in_buf_size,
		void	 *out_buf,
		unsigned out_buf_size,
		unsigned *async_status)
{
	int error;

	nand_dev.pending_task = NULL;
	nand_dev.pending_async_status = 0;
	nand_dev.irq_num = 0;

	/*
	 * disable interrupt of nanddisk during each function
	 * call to avoid reentrancy
	 */
	disable_irq_nosync(nand_dev.irq);
	error = nand_dev.pfn_ioctrl(handle, ioctrl_code, in_buf, in_buf_size,
				out_buf, out_buf_size, async_status);

	if (error) {
		error = 0;
		if (async_status) {
			if (*async_status & ASYNC_STATUS_PENDING) {
				nand_dev.pending_task = current;
				nand_dev.pending_async_status = *async_status;
			}

			enable_irq(nand_dev.irq);

			set_current_state(TASK_INTERRUPTIBLE);
			if (nand_dev.pending_async_status &
					ASYNC_STATUS_PENDING)
				schedule();
			set_current_state(TASK_RUNNING);

			if (nand_dev.pending_async_status & ASYNC_STATUS_ERR) {
				error = -EIO;
				pr_err("%s:async stat 0x%x, ioctrl(0x%x).\n",
					__func__,
					nand_dev.pending_async_status,
					ioctrl_code);
			}

			disable_irq_nosync(nand_dev.irq);
		}
	} else {
		error = -EIO;
		pr_err("%s:ioctrl(0x%x) failed.\n", __func__, ioctrl_code);
	}

	enable_irq(nand_dev.irq);

	return error;
}

static int nanddisk_zone_io(unsigned zone, unsigned sector, unsigned  nsect,
				char *buffer, int write)
{
	struct NAND_IO nand_io;
	unsigned async_status;
	unsigned flag_wearlevel = 0;
	unsigned i;
	unsigned uboot_commit_flag_sector =
		   nand_dev.uboot_commit_flag >> nand_dev.sector_size_shift;

	if (nsect == 0)
		return 0;

	if (write && nand_dev.nandinsert
		&& sector >= nand_dev.boot_zone_log_sector_start
		&& sector + nsect <= nand_dev.boot_zone_log_sector_start
		+ nand_dev.boot_zone_log_sector_num) {
		if (sector == uboot_commit_flag_sector) {
			unsigned phy_sector_size =
				nand_dev.nand_chip_info.
				phy_bdev_info.byte_per_sector;
			nand_io.start_sector =
				nand_dev.boot_zone_log_sector_start;
			/* phy sector size align */
			nand_io.sector_num = (((nand_dev.uboot_sec_num <<
				nand_dev.sector_size_shift) +
				phy_sector_size - 1) & ~(phy_sector_size - 1))
				>> nand_dev.sector_size_shift;
			nand_io.sector_buf = (void *)nand_dev.uboot_buf;
			pr_info("sync boot_zone, total %d sector.\n",
				nand_io.sector_num);
			for (i = 0; i < nand_dev.uboot_sec_num; i++) {
				if (!nand_dev.uboot_write_map[i]) {
					pr_info("sect %d is not valid.\n", i);
					return -1;
				}
			}
			nand_dev.uboot_sec_num = 0;

			if (!nand_dev.pfn_ioctrl(
				MAP_HANDLE,
				NAND_IOCTRL_WRITE_SECTOR,
				&nand_io, sizeof(nand_io),
				NULL, 0, NULL))	{
				pr_info("%s:write %d,%d,0x%p) failed.\n",
					__func__, sector, nand_io.sector_num,
					nand_dev.uboot_buf);
				vfree(nand_dev.uboot_buf);
				return -1;
			}
			pr_info("%s: write boot zone successfully\n", __func__);
			vfree(nand_dev.uboot_buf);
		} else {
			for (i = sector - nand_dev.boot_zone_log_sector_start;
				i < sector + nsect -
				nand_dev.boot_zone_log_sector_start;
				i++) {
				memcpy(nand_dev.uboot_buf +
					(i << nand_dev.sector_size_shift),
					(void *)buffer,
					(1 << nand_dev.sector_size_shift));
				if (!nand_dev.uboot_write_map[i]) {
					nand_dev.uboot_write_map[i] = 1;
					nand_dev.uboot_sec_num++;
				} else {
					pr_info("%d is written again!\n", i);
				}
				buffer += (1 << nand_dev.sector_size_shift);
			}
		}
	} else {
		nand_io.start_sector = sector;
		nand_io.sector_num = nsect;
		nand_io.sector_buf = (void *)buffer;
		if (nanddisk_io_session(zone,
			write ? NAND_IOCTRL_WRITE_SECTOR :
			NAND_IOCTRL_READ_SECTOR,
			&nand_io, sizeof(nand_io), &flag_wearlevel,
			sizeof(flag_wearlevel), &async_status)) {
			pr_err("%s:pfn_ioctrl(%s,%d,%d,0x%x) failed.\n"
				, __func__, write ? "NAND_IOCTRL_WRITE_SECTOR" :
						"NAND_IOCTRL_READ_SECTOR",
				(int)sector, (int)nsect, (unsigned)buffer);
			return -1;
		}
	}

	if (flag_wearlevel) {
		nand_dev.need_wearlevel = 1;
		wake_up_process(nand_dev.wearlevel_task);
	}
	return 0;
}

/* handle an I/O request */
static int nanddisk_io_sort(unsigned sector, unsigned nsect, char *buffer,
				int write)
{
	unsigned cur_sec_num;

	/* the part before boot zone shadow */
	if (nsect && sector < nand_dev.boot_zone_log_sector_start) {
		cur_sec_num = min(nsect,
				nand_dev.boot_zone_log_sector_start - sector);
		if (nanddisk_zone_io(MAP_HANDLE,
					sector, cur_sec_num, buffer, write))
			return -1;
		sector += cur_sec_num;
		nsect -= cur_sec_num;
		buffer += cur_sec_num<<nand_dev.sector_size_shift;
	}

	/* the part in boot zone shadow, and within the boot zone */
	if (nsect && sector < nand_dev.boot_zone_log_sector_start +
			nand_dev.boot_zone_log_sector_num) {
		cur_sec_num = min(nsect, nand_dev.boot_zone_log_sector_start +
		  nand_dev.boot_zone_log_sector_num - sector);
		if ((write && nand_dev.nandinsert) || !write)
			if (nanddisk_zone_io(MAP_HANDLE, sector, cur_sec_num,
				buffer, write))
				return -1;
		sector += cur_sec_num;
		nsect -= cur_sec_num;
		buffer += cur_sec_num<<nand_dev.sector_size_shift;
	}

	/* the part after zone shadow */
	if (nsect && sector >= nand_dev.boot_zone_log_sector_start +
			nand_dev.boot_zone_log_sector_num) {
		cur_sec_num = nsect;
		if (nanddisk_zone_io(MAP_HANDLE,
			sector, cur_sec_num, buffer, write))
			return -1;
	}

	return 0;
}

static irqreturn_t nanddisk_isr(int irq, void *dev_id)
{
	unsigned enable = 0;

	if (!nand_dev.pfn_ioctrl(0, NAND_IOCTRL_ENABLE_INTR, &enable,
		sizeof(enable), NULL, 0, NULL)) {
		pr_err("%s:disable isr failed.\n", __func__);
	}

	nand_dev.irq_pending = 1;
	tasklet_schedule(&nand_dev.ist_tasklet);
	nand_dev.irq_num++;
	return IRQ_HANDLED;
}

static void nanddisk_ist(unsigned long data)
{
	unsigned async_status;
	nand_dev.irq_pending = 0;
	async_status = 0;

	disable_irq_nosync(nand_dev.irq);
	if (!nand_dev.pfn_ioctrl(0, NAND_IOCTRL_ASYNC_EVENT, NULL, 0,
		NULL, 0, &async_status)) {
		pr_err("%s:sync event failed.\n",  __func__);
	}
	enable_irq(nand_dev.irq);

	if (!(async_status & ASYNC_STATUS_PENDING) &&
		(nand_dev.pending_async_status&ASYNC_STATUS_PENDING)) {
		nand_dev.pending_async_status = async_status;
		wake_up_process(nand_dev.pending_task);
	}
}

static int nanddisk_wearlevel_thread(void *arg)
{
	unsigned async_status;
	set_user_nice(current, 19);
	do {
		set_current_state(TASK_INTERRUPTIBLE);
		if (!nand_dev.need_wearlevel)
			schedule();
		set_current_state(TASK_RUNNING);
		if (kthread_should_stop())
			break;

		mutex_lock(&nand_dev.mutex);
		while (nand_dev.need_wearlevel) {
			if (kthread_should_stop())
				break;

			if (nanddisk_io_session(MAP_HANDLE,
				NAND_IOCTRL_TRIGGER_BACKGROUNDTASK, NULL, 0,
				&nand_dev.need_wearlevel,
				sizeof(nand_dev.need_wearlevel),
				&async_status)) {
				pr_err("%s:wear level failed.\n",  __func__);
				nand_dev.need_wearlevel = 0;
			}

			mutex_unlock(&nand_dev.mutex);
			schedule_timeout(500);
			mutex_lock(&nand_dev.mutex);
		}
		mutex_unlock(&nand_dev.mutex);
	} while (1);

	return 0;
}

static int nanddisk_merge_and_transfer(struct request *req,
		unsigned sector, unsigned total_sectors, bool dir)
{
	unsigned total_bytes, cur_bytes, remain_bytes, merged_bytes;
	unsigned long flags;
	int ret = 0;
	struct request_queue *q = nand_dev.queue;
	total_bytes = blk_rq_bytes(req);
	merged_bytes = 0;
	remain_bytes = total_bytes;
	if (!dir)
		ret = nanddisk_io_sort(sector,
			total_sectors, nand_dev.data_buf, dir);
	while (remain_bytes != 0) {
		cur_bytes = blk_rq_cur_bytes(req);
		if (dir)
			memcpy(nand_dev.data_buf + merged_bytes,
				req->buffer, cur_bytes);
		else
			memcpy(req->buffer,
				nand_dev.data_buf + merged_bytes, cur_bytes);
		merged_bytes += cur_bytes;
		remain_bytes -= cur_bytes;
		if (remain_bytes == 0 && dir)
			ret = nanddisk_io_sort(sector,
				total_sectors, nand_dev.data_buf, dir);
		spin_lock_irqsave(q->queue_lock, flags);
		ret = __blk_end_request(req, 0, cur_bytes);
		spin_unlock_irqrestore(q->queue_lock, flags);
	}
	return ret;
}

static int nanddisk_transfer_thread(void *arg)
{
	struct request *req;
	int ret;
	unsigned sector;
	unsigned nsect;
	int write;
	unsigned long  flags;
	struct request_queue *q = nand_dev.queue;
	unsigned total_sectors, total_bytes;
	bool is_merge;

	set_user_nice(current, -20);
	mutex_lock(&nand_dev.mutex);

	do {
		req = NULL;
		spin_lock_irqsave(q->queue_lock, flags);
		set_current_state(TASK_INTERRUPTIBLE);
		req = blk_fetch_request(q);

		nand_dev.req = req;
		spin_unlock_irqrestore(q->queue_lock, flags);

		if (!req) {
			if (kthread_should_stop()) {
				set_current_state(TASK_RUNNING);
				break;
			}
			mutex_unlock(&nand_dev.mutex);
			schedule();
			mutex_lock(&nand_dev.mutex);
			continue;
		}

		set_current_state(TASK_RUNNING);

		if (req->cmd_type != REQ_TYPE_FS) {
			pr_info("Skip non-CMD request\n");
			spin_lock_irqsave(q->queue_lock, flags);
			__blk_end_request_all(req, -EIO);
			spin_unlock_irqrestore(q->queue_lock, flags);
			continue;
		}
		total_bytes = (unsigned)blk_rq_bytes(req);
		total_sectors = total_bytes >> nand_dev.sector_size_shift;
		write = rq_data_dir(req);
		sector = (unsigned)(blk_rq_pos(req)>>
				(nand_dev.sector_size_shift - 9));
		if ((sector + total_sectors) > nand_dev.sectors_num) {
			pr_err("nand: Beyond-end %s(%d %d)\n",
				write ? "write" : "read", (int)sector,
				(int)total_sectors);
			ret = -1;
		}
		is_merge = false;
		if (total_sectors > 1)
			is_merge = true;
		do {
			if (is_merge) {
				ret = nanddisk_merge_and_transfer(req,
					sector, total_sectors, write);
			} else {
				sector = (unsigned)(blk_rq_pos(req)>>
					(nand_dev.sector_size_shift - 9));
				nsect = blk_rq_cur_sectors(req)	>>
					(nand_dev.sector_size_shift - 9);
				ret = nanddisk_io_sort(sector,
					nsect, req->buffer, write);
				spin_lock_irqsave(q->queue_lock, flags);
				ret = __blk_end_request(req, ret,
					nsect << nand_dev.sector_size_shift);
				spin_unlock_irqrestore(q->queue_lock, flags);
			}
		} while (ret); /* no finished? */
	} while (1);
	mutex_unlock(&nand_dev.mutex);

	return 0;
}

static int nanddisk_init(struct platform_device *pdev)
{
	unsigned  enable, version, buffer_size;
	struct device *dev = &pdev->dev;
	struct ASYNC_MODE async_mode;

	/* tell nanddisk the address map */
	if (!nand_dev.pfn_ioctrl(0, NAND_IOCTRL_NEW_ADDRESS_MAP,
		nand_dev.addr_map_tbl,
		sizeof(struct ADDRMAP) * nand_dev.addr_entry_num,
		NULL, 0, NULL)) {
		dev_err(dev, "NAND_IOCTRL_NEW_ADDRESS_MAP failed.\r\n");
		return -1;
	}

	/* check the interface version */
	if (!nand_dev.pfn_ioctrl(0, NAND_IOCTRL_GET_INTERFACE_VERSION,
		NULL, 0, &version, sizeof(unsigned), NULL)) {
		dev_err(dev, "NAND_IOCTRL_GET_INTERFACE_VERSION failed.\n");
		return -1;
	}

	dev_info(dev, "nanddisk binary's interface version is %d\n",
			version);
	if (NANDDISK_INTERFACE_VERSION != version) {
		dev_err(dev, "interface version mismatch (using ver %d)\n.",
			NANDDISK_INTERFACE_VERSION);
		dev_err(dev, "please update your nanddisk.h file.\n");
		return -1;
	}

	/* get binary version */
	if (!nand_dev.pfn_ioctrl(0, NAND_IOCTRL_GET_VERSION, NULL, 0, &version,
		sizeof(unsigned), NULL)) {
		dev_err(dev, "NAND_IOCTRL_GET_VERSION failed.\n");
		return -1;
	}
	dev_info(dev, "nanddisk binary version is 0x%x\n", version);

	/* enable nanddisk debug message */
	enable = 0;
	if (!nand_dev.pfn_ioctrl(0, NAND_IOCTRL_ENABLE_MSG, &enable,
		sizeof(unsigned), NULL, 0, NULL)) {
		dev_err(dev, "NAND_IOCTRL_ENABLE_MSG failed.\r\n");
		return -1;
	}

	enable = 1;
	if (!nand_dev.pfn_ioctrl(0, NAND_IOCTRL_POWER, &enable,
		sizeof(unsigned), NULL, 0, NULL)) {
		dev_err(dev, "NAND_IOCTRL_POWER failed.\r\n");
		return -1;
	}

	/* get chip info */
	if (!nand_dev.pfn_ioctrl(0, NAND_IOCTRL_GET_CHIPINFO, NULL, 0,
	  &nand_dev.nand_chip_info, sizeof(struct NAND_CHIP_INFO), NULL)) {
		dev_err(dev, "get chio info failed.\r\n");
		return -1;
	}
	nand_dev.bytes_per_block =
		nand_dev.nand_chip_info.phy_bdev_info.byte_per_sector
		* nand_dev.nand_chip_info.phy_bdev_info.sector_per_block;

	dev_info(dev, "find valid nand chip.\n");
	dev_info(dev, "page size %d, %d page per block, total %d block.\n",
		nand_dev.nand_chip_info.phy_bdev_info.byte_per_sector,
		nand_dev.nand_chip_info.phy_bdev_info.sector_per_block,
		nand_dev.nand_chip_info.phy_bdev_info.block_num);

	if (!nand_dev.nand_chip_info.actived) {
		dev_err(dev, "err: nanddisk not actived.\r\n");
		return -1;
	}

	nand_dev.sector_size_shift =
	  blksize_bits(nand_dev.nand_chip_info.io_bdev_info.byte_per_sector);
	nand_dev.size = nand_dev.sectors_num << nand_dev.sector_size_shift;

	nand_dev.boot_zone_log_sector_start = 1;
	nand_dev.boot_zone_log_sector_num =
		nand_dev.nanddisk_code_size >> nand_dev.sector_size_shift;

	/* enable async adapt mode */
	async_mode.enable = 1;
	async_mode.auto_adapt = 0;

	if (!nand_dev.pfn_ioctrl(0, NAND_IOCTRL_ASYNC_MODE, &async_mode,
		sizeof(async_mode), NULL, 0, NULL)) {
		dev_err(dev, "NAND_IOCTRL_ASYNC_MODE failed.\r\n");
		return -1;
	}

	/* enable write buffer */
	buffer_size = NAND_MAX_BUFFER_SIZE;
	if (!nand_dev.pfn_ioctrl(0, NAND_IOCTRL_BUFFER_SIZE, &buffer_size,
		sizeof(buffer_size), NULL, 0, NULL)) {
		dev_err(dev, "NAND_IOCTRL_BUFFER_SIZE failed.\r\n");
		return -1;
	}

	/* get max sector */
	if (!nand_dev.pfn_ioctrl(0, NAND_IOCTRL_GET_ZONEMAP_SIZE, NULL,
		0, &nand_dev.sectors_num, sizeof(nand_dev.sectors_num), NULL)) {
		dev_err(dev, "NAND_IOCTRL_GET_ZONEMAP_SIZE failed.\r\n");
		return -1;
	}

	if (!nand_dev.sectors_num) {
		dev_err(dev, "err! not set zone map!\r\n");
		return -1;
	}
	return 0;
}

/*
 * nandddisk module include nand controller and other controllers,
 * all of these controllers are managed by nanddisk module.
 */

struct arch_other_res {
	char *res_compatible;
	int index;
};

struct arch_nanddisk_resource {
	const char *arch_compatible;
	unsigned char res_num;
	unsigned int sd0_boot_mode_mask;
	unsigned int sd0_boot_mode_value;
	unsigned int sd0_bootp_mode_value;
	struct arch_other_res arch_ores[15];
};

static struct arch_nanddisk_resource arch_nres[] = {
	{
		"sirf,prima2",
		10,
		0xf, 0xd, 0xe,
		{
			{"sirf,prima2-intc", 0},
			{"sirf,prima2-rsc", 0},
			{"sirf,prima2-tick", 0},
			{"sirf,prima2-clkc", 0},
			{"sirf,prima2-uart", 1},
			{"sirf,prima2-pinctrl", 0},
			{"sirf,prima2-dmac", 0},
			{"sirf,prima2-rstc", 0},
			{"sirf,prima2-efuse", 0},
			{"sirf,prima2-pl310-cache", 0}
		}
	},
	{
		"sirf,atlas6",
		9,
		0x7, 0x3, 0x7,
		{
			{"sirf,prima2-intc", 0},
			{"sirf,prima2-rsc", 0},
			{"sirf,prima2-tick", 0},
			{"sirf,atlas6-clkc", 0},
			{"sirf,prima2-uart", 1},
			{"sirf,atlas6-pinctrl", 0},
			{"sirf,prima2-dmac", 0},
			{"sirf,prima2-rstc", 0},
			{"sirf,prima2-efuse", 0}
		},
	}

};

static void nand_request(struct request_queue *q)
{
	if (!nand_dev.req) {
		nand_dev.req = (struct request *)-1;
		wake_up_process(nand_dev.transfer_task);
	}
}

/*
 * The HDIO_GETGEO ioctl is handled in blkdev_ioctl(), which
 * calls this. We need to implement getgeo, since we can't
 * use tools such as fdisk to partition the drive otherwise.
 */
int nand_getgeo(struct block_device *block_device, struct hd_geometry *geo)
{
	u64 size;

	/*
	 * We have no real geometry, of course, so make something up.
	 */
	size = nand_dev.size * (nand_dev.sector_size_shift - 9);
	geo->cylinders = (u16)((size & ~0x3f) >> 6);
	geo->heads = 4;
	geo->sectors = 16;
	geo->start = 0;
	return 0;
}

static const struct block_device_operations nand_ops = {
	.owner  = THIS_MODULE,
	.getgeo = nand_getgeo
};

static int sirfsoc_nand_resume_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	dev_info(dev, "%s ++\n", __func__);

	clk_enable(nand_dev.nand_clk);

	if (nanddisk_init(pdev)) {
		dev_err(dev, "nanddisk_init failed.\r\n");
		return -1;
	}
	return 0;
}

static int sirfsoc_nand_resume(struct device *dev)
{
	unsigned long flags;
	dev_info(dev, "%s ++\n", __func__);

	if (!nand_dev.power) {
		nand_dev.power = 1;
		spin_lock_irqsave(nand_dev.queue->queue_lock, flags);
		blk_start_queue(nand_dev.queue);
		spin_unlock_irqrestore(nand_dev.queue->queue_lock, flags);
		mutex_unlock(&nand_dev.mutex);
	}
	return 0;
}

static int sirfsoc_nand_suspend(struct device *dev)
{
	unsigned enable = 0;
	unsigned long  flags;

	dev_info(dev, "%s ++\n", __func__);
	if (nand_dev.power) {
		mutex_lock(&nand_dev.mutex);
		spin_lock_irqsave(nand_dev.queue->queue_lock, flags);
		blk_stop_queue(nand_dev.queue);
		spin_unlock_irqrestore(nand_dev.queue->queue_lock, flags);

		if (!nand_dev.pfn_ioctrl(0, NAND_IOCTRL_POWER, &enable,
			sizeof(unsigned), NULL, 0, NULL)) {
			dev_err(dev, "NAND_IOCTRL_POWER failed.\r\n");
			nand_dev.power = 0;
			sirfsoc_nand_resume(dev);
			return -1;
		}
		nand_dev.power = 0;

		clk_disable(nand_dev.nand_clk);
	}
	return 0;
}

static int current_log_state;
static ssize_t set_log_state(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long log_state;

	mutex_lock(&nand_dev.mutex);

	if (kstrtoul(buf, 10, &log_state)) {
		dev_info(dev, "wrong input state!\n");
		return 1;
	}

	current_log_state = log_state;
	dev_info(dev, "set log state to %d\n", current_log_state);
	if (!nand_dev.pfn_ioctrl(0, NAND_IOCTRL_ENABLE_MSG, &current_log_state,
		sizeof(unsigned), NULL, 0, NULL)) {
		pr_err("NAND_IOCTRL_ENABLE_MSG failed.\r\n");
		return -1;
	}

	mutex_unlock(&nand_dev.mutex);

	return count;
}

static ssize_t get_log_state(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	dev_info(dev, "get log state is %d\n", current_log_state);
	return 0;
}

static DEVICE_ATTR(log, S_IRUGO | S_IWUSR, get_log_state, set_log_state);
static int sirfsoc_nand_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource res;
	struct device_node *dn = NULL;
	dma_cap_mask_t dma_cap_mask;
	int i, error, addr_map_tbl_size;
	int resource_index;
	unsigned int value;
	struct arch_nanddisk_resource *arch_nres_used;

	arch_nres_used = &arch_nres[0];
	for (i = 0; i < ARRAY_SIZE(arch_nres); i++) {
		dn = of_find_compatible_node(NULL, NULL,
			arch_nres_used->arch_compatible);
		if (dn)
			break;
		arch_nres_used++;
	}

	if (!dn) {
		dev_err(dev, "no suitable nand controller!!\n");
		error = -ENODEV;
		goto err_exit;
	}

	dev_info(dev, "find nand controller(%s).\n",
		 arch_nres_used->arch_compatible);

	/* total other controller */
	nand_dev.addr_entry_num = arch_nres_used->res_num;
	/* add nand controller */
	nand_dev.addr_entry_num += 1;
	/* add 2 item, ram and zero-end */
	nand_dev.addr_entry_num += 2;
	addr_map_tbl_size = sizeof(struct ADDRMAP) * nand_dev.addr_entry_num;
	nand_dev.addr_map_tbl = devm_kzalloc(dev,
					addr_map_tbl_size, GFP_KERNEL);
	if (!nand_dev.addr_map_tbl) {
		dev_err(dev, "unable to malloc buffer for address table.\n");
		error = -ENOMEM;
		goto err_exit;
	}

	/* get resouce from other controller */
	for (i = 0; i < arch_nres_used->res_num; i++) {
		dn = NULL;
		resource_index = arch_nres_used->arch_ores[i].index;
		/* when it is not the first node */
		do {
			dn = of_find_compatible_node(dn, NULL,
				arch_nres_used->arch_ores[i].res_compatible);
			if (!dn) {
				dev_err(dev, "unable to get %s node!\n",
					arch_nres_used->arch_ores[i].res_compatible);
				error = -ENODEV;
				goto err_exit;
			}
		} while (resource_index--);

		error = of_address_to_resource(dn, 0, &res);
		if (error) {
			dev_err(dev, "unable to get resource(%d)!\n", i);
			error = -EINVAL;
			goto err_exit;
		}

		nand_dev.addr_map_tbl[i].pa = res.start;
		nand_dev.addr_map_tbl[i].size = resource_size(&res);
		nand_dev.addr_map_tbl[i].va =
			devm_ioremap(dev, res.start, resource_size(&res));

		if (!nand_dev.addr_map_tbl[i].va) {
			dev_err(dev, "unable to ioremap!\n");
			error = -ENOMEM;
			goto err_exit;
		}
	}

	/* get resource from nand controller */
	dn = pdev->dev.of_node;
	error = of_address_to_resource(dn, 0, &res);
	if (error) {
		dev_err(dev, "unable to get resource(%d)!\n", i);
		error = -EINVAL;
		goto err_exit;
	}

	nand_dev.addr_map_tbl[i].pa = res.start;
	nand_dev.addr_map_tbl[i].size = resource_size(&res);
	nand_dev.addr_map_tbl[i].va =
		devm_ioremap(dev, res.start, resource_size(&res));

	if (!nand_dev.addr_map_tbl[i].va) {
		dev_err(dev, "unable to ioremap!\n");
		error = -ENOMEM;
		goto err_exit;
	}

	/*
	 * nand and sd0 share the same slot
	 * nand can be used when sd0 absent
	 */
	value = readl(nand_dev.addr_map_tbl[i].va) &
			arch_nres_used->sd0_boot_mode_mask;
	if (value == arch_nres_used->sd0_boot_mode_value ||
		value == arch_nres_used->sd0_bootp_mode_value) {
		dev_err(dev, "no nand chip.\n");
		error = -ENODEV;
		goto err_exit;
	}

	/* firmware area */
	if (of_property_read_u32(dn, "sirf,nanddisk-uboot-commit-flag",
		&nand_dev.uboot_commit_flag)) {
		dev_err(dev, "unable to get uboot commit flag!\n");
		error = -ENODEV;
		goto err_exit;
	}

	i++;
	nand_dev.addr_map_tbl[i].pa = nand_dev.nanddisk_code_start;
	nand_dev.addr_map_tbl[i].va =
		(void *)phys_to_virt(nand_dev.nanddisk_code_start);
	nand_dev.addr_map_tbl[i].size = nand_dev.nanddisk_code_size;
	nand_dev.addr_map_tbl[i].flag = ADDR_MAP_FLAG_CACHE;

	/* irq resource */
	nand_dev.irq = platform_get_irq(pdev, 0);
	if (nand_dev.irq < 0) {
		dev_err(dev, "unable to get irq!\n");
		error = -ENODEV;
		goto err_exit;
	}

	error = devm_request_irq(dev, nand_dev.irq, nanddisk_isr,
			0, "nanddisk", &nand_dev);
	if (error) {
		dev_err(dev, "unable to request irq!\n");
		error = -ENODEV;
		goto err_exit;
	}

	/* dma resource */
	if (of_property_read_u32(dn, "sirf,nand-dma-channel",
		&nand_dev.dma_num)) {
		dev_err(dev, "unable to get dma channel!\n");
		error = -ENODEV;
		goto err_exit;
	}

	dma_cap_zero(dma_cap_mask);
	dma_cap_set(DMA_INTERLEAVE, dma_cap_mask);
	nand_dev.rw_chan = dma_request_channel(dma_cap_mask,
		(dma_filter_fn)sirfsoc_dma_filter_id,
		(void *)nand_dev.dma_num);
	if (!nand_dev.rw_chan) {
		dev_err(dev, "unable to allocate dma channel\n");
		error = -ENODEV;
		goto err_exit;
	}

	/* clock */
	nand_dev.nand_clk = devm_clk_get(dev, NULL);
	if (IS_ERR(nand_dev.nand_clk)) {
		dev_err(dev, "unable to get clk!\n");
		error = PTR_ERR(nand_dev.nand_clk);
		goto err_clk_get;
	}

	clk_prepare_enable(nand_dev.nand_clk);

	nand_dev.pfn_ioctrl =
		(PFN_NANDDISK_IOCTRL)phys_to_virt(nand_dev.nanddisk_code_start);

	nand_dev.nandinsert = 1;

	if (nanddisk_init(pdev)) {
		dev_err(dev, "unable to initialize nanddisk.\n");
		error = -ENODEV;
		goto err_nanddisk_init;
	}

	nand_dev.power = 1;

	spin_lock_init(&nand_dev.lock);

	nand_dev.queue = blk_init_queue(nand_request, &nand_dev.lock);
	if (nand_dev.queue == NULL) {
		dev_err(dev, "unable to initialize blk queue.\n");
		error = -ENOMEM;
		goto err_blk_init_queue;
	}
	blk_queue_logical_block_size(nand_dev.queue,
		0x1<<nand_dev.sector_size_shift);
	blk_queue_max_hw_sectors(nand_dev.queue, 1024);

	nand_dev.data_buf = vmalloc(1024 * 512);
	if (!nand_dev.data_buf) {
		dev_err(dev, "unable to malloc buffer for data.\n");
		error = -ENOMEM;
		goto err_vmalloc_data_buf;
	}

	nand_dev.uboot_buf = vmalloc(
			UBOOT_MAX_LENGTH + UBOOT_MAX_SECTOR);
	if (!nand_dev.uboot_buf) {
		dev_err(dev, "unable to malloc buffer for uboot.\n");
		error = -ENOMEM;
		goto err_vmalloc_uboot_buf;
	}
	nand_dev.uboot_write_map = (unsigned char *)(
			nand_dev.uboot_buf + UBOOT_MAX_LENGTH);
	memset(nand_dev.uboot_buf, 0, UBOOT_MAX_LENGTH);
	memset(nand_dev.uboot_write_map, 0, UBOOT_MAX_SECTOR);

	nand_dev.major_num = register_blkdev(nand_dev.major_num, "nandblk");
	if (nand_dev.major_num <= 0) {
		dev_err(dev, "unable to register blkdev.\n");
		error = -EIO;
		goto err_register_blkdev;
	}

	nand_dev.gd = alloc_disk(16);
	if (!nand_dev.gd) {
		dev_err(dev, "unable to alloc disk.\n");
		error = -ENOMEM;
		goto err_alloc_disk;
	}

	nand_dev.gd->major = nand_dev.major_num;
	nand_dev.gd->first_minor = 0;
	nand_dev.gd->fops = &nand_ops;
	nand_dev.gd->private_data = &nand_dev;
	strcpy(nand_dev.gd->disk_name, "nandblk0");
	set_capacity(nand_dev.gd,
		nand_dev.sectors_num<<(nand_dev.sector_size_shift - 9));
	nand_dev.gd->queue = nand_dev.queue;

	mutex_init(&nand_dev.mutex);

	nand_dev.wearlevel_task = kthread_run(nanddisk_wearlevel_thread, NULL,
		"nand_wearlevel");
	if (IS_ERR(nand_dev.wearlevel_task)) {
		dev_err(dev, "unable to run wearlevel task.\n");
		error = PTR_ERR(nand_dev.wearlevel_task);
		goto err_kthread_run_wearlevel_task;
	}

	nand_dev.transfer_task = kthread_run(nanddisk_transfer_thread, NULL,
		"nand_transfer");
	if (IS_ERR(nand_dev.transfer_task)) {
		dev_err(dev, "unable to run transfer task.\n");
		error = PTR_ERR(nand_dev.transfer_task);
		goto err_kthread_run_transfer_task;
	}

	tasklet_init(&nand_dev.ist_tasklet, nanddisk_ist, 0);

	add_disk(nand_dev.gd);

	error = device_create_file(dev, &dev_attr_log);
	if (error) {
		dev_err(dev, "unable to create log sys file.\n");
		goto err_device_create_file;
	}

	return 0;

err_device_create_file:
	kthread_stop(nand_dev.transfer_task);
err_kthread_run_transfer_task:
	kthread_stop(nand_dev.wearlevel_task);
err_kthread_run_wearlevel_task:
	put_disk(nand_dev.gd);
err_alloc_disk:
	unregister_blkdev(nand_dev.major_num, "nandblk");
err_register_blkdev:
	vfree(nand_dev.uboot_buf);
err_vmalloc_uboot_buf:
	vfree(nand_dev.data_buf);
err_vmalloc_data_buf:
	blk_cleanup_queue(nand_dev.queue);
err_blk_init_queue:
err_nanddisk_init:
	clk_disable_unprepare(nand_dev.nand_clk);
err_clk_get:
	dma_release_channel(nand_dev.rw_chan);
err_exit:
	return error;
}

static int sirfsoc_nand_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	dev_info(dev, "%s ++\n ", __func__);
	/* stop new requests from getting into the queue */
	del_gendisk(nand_dev.gd);

	kthread_stop(nand_dev.wearlevel_task);
	kthread_stop(nand_dev.transfer_task);

	blk_cleanup_queue(nand_dev.queue);
	put_disk(nand_dev.gd);
	unregister_blkdev(nand_dev.major_num, "nandblk");

	device_remove_file(dev, &dev_attr_log);

	clk_disable_unprepare(nand_dev.nand_clk);

	dma_release_channel(nand_dev.rw_chan);

	vfree(nand_dev.data_buf);

	vfree(nand_dev.uboot_buf);

	return 0;
}

static void sirfsoc_nand_shutdown(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	pm_message_t pm_message;
	pm_message.event = 0;
	dev_info(dev, "%s ++", __func__);
	sirfsoc_nand_suspend(dev);
}

static const struct dev_pm_ops sirfsoc_nand_pm_ops = {
	.resume_noirq = sirfsoc_nand_resume_noirq,
	.thaw_noirq = sirfsoc_nand_resume_noirq,
	.restore_noirq = sirfsoc_nand_resume_noirq,
	.resume = sirfsoc_nand_resume,
	.restore = sirfsoc_nand_resume,
	.thaw = sirfsoc_nand_resume,
	.suspend = sirfsoc_nand_suspend,
	.freeze = sirfsoc_nand_suspend,
};

static const struct of_device_id nand_sirfsoc_of_match[] = {
	{ .compatible = "sirf,prima2-nand", },
	{ .compatible = "sirf,marco-nand", },
	{}
};

static struct platform_driver sirfsoc_nand_driver = {
	.probe	= sirfsoc_nand_probe,
	.remove = sirfsoc_nand_remove,
	.shutdown = sirfsoc_nand_shutdown,
	.driver = {
		.name = "sirfsoc-nand",
		.owner = THIS_MODULE,
		.pm = &sirfsoc_nand_pm_ops,
		.of_match_table = nand_sirfsoc_of_match,
	},
};
module_platform_driver(sirfsoc_nand_driver);

MODULE_AUTHOR("Peiyu Li <peiyu.li@csr.com>");
MODULE_AUTHOR("Huayi Li <huayi.li@csr.com>");
MODULE_DESCRIPTION("SiRF SOC NANDDisk Driver");
MODULE_LICENSE("GPL");
