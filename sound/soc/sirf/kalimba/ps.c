/*
 * kailimba Persistent storage driver
 *
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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

#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/dma-mapping.h>

#include "ps.h"
#include "dsp.h"
#include "regs.h"
#include "ipc.h"

struct delayed_work dwork;

struct ps_entry {
	char *kbuf;
	struct file *ps_file;
	const char *file_name;
	u32 dm_ptr;
	size_t size;
};

struct ps_entry ps_area[] = {
	{
		.file_name = "/var/lib/kalimba/kymera_a7da_PS.dat",
		.dm_ptr = DSP_PS_FILE_BASE_ADDR,
		.kbuf = NULL,
	}
};

/*
 * Copies a buffer to DRAM
 */
static int ps_write_dm(u32 start_addr, u32 length, u32 *buf)
{
	int i;

	write_kalimba_reg(KAS_CPU_KEYHOLE_MODE, 4);

	write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR,
		(start_addr << 2) | (0x2 << 30));
	for (i = 0; i < length / 4; i++) {
		u32 tmp = 0;

		tmp = *(buf + i);
		write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, tmp);
	}

	return 0;
}

/*
 * Read the file and get it into DRAM area
 */
static int ps_prepare_region(struct ps_entry *pse)
{
	int size;
	void *buf;
	struct file *cfile;
	int rc;
	static u32 dmbuf[2];
	dma_addr_t phy_addr;

	cfile = filp_open(pse->file_name, O_RDWR | O_DSYNC, 0);
	if (IS_ERR(cfile))
		return PTR_ERR(cfile);

	if (!(cfile->f_mode & FMODE_CAN_READ)) {
		pr_err("alloc_device: cache file not readable\n");
		rc = -EINVAL;
		goto err_close;
	}
	if (!(cfile->f_mode & FMODE_CAN_WRITE)) {
		rc = -EINVAL;
		goto err_close;
	}

	if (!S_ISREG(file_inode(cfile)->i_mode))
		return -EINVAL;

	size = i_size_read(file_inode(cfile));
	if (size <= 0)
		return -EINVAL;
	/*
	 * Allocate contiguous kernel memory
	 */
	buf = dma_alloc_coherent(NULL, size, &phy_addr, GFP_KERNEL);
	if (buf == NULL) {
		dev_err(NULL, "Alloc dram failed.\n");
		filp_close(cfile, NULL);
		return -ENOMEM;
	}

	rc = kernel_read(cfile, 0, buf, size);
	if (rc != size) {
		if (rc > 0)
			rc = -EIO;
		goto fail;
	}
	pse->size = size;
	pse->kbuf = buf;
	pse->ps_file = cfile;
	dmbuf[0] = phy_addr >> 24;
	dmbuf[1] = phy_addr & 0x00ffffff;

	/*
	 * Indicate to kalimaba the PS address in DRAM
	 */
	ps_write_dm(pse->dm_ptr, 8, dmbuf);

	filp_close(cfile, NULL);

	return 0;
fail:
	kfree(buf);

err_close:
	filp_close(cfile, NULL);
	return rc;
}

/*
 * Write the file from memory to the filesystem
 */
static int ps_write_file(struct ps_entry *pse)
{
	ssize_t ret;
	struct file *cfile;

	cfile = filp_open(pse->file_name, O_RDWR | O_DSYNC, 0);
	if (cfile != NULL && pse->kbuf != NULL) {
		ret = kernel_write(cfile, pse->kbuf, pse->size, 0);
		if (ret < 0)
			pr_err("Error writing PS file: %s\n", pse->file_name);
	}
	filp_close(cfile, NULL);

	return 0;
}

/*
 * Notify for ps flush
 */
static int kas_ps_notify(u16 message, void *priv_data, u16 *message_data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ps_area); i++)
		ps_write_file(&ps_area[i]);

	return ACTION_HANDLED;
}

/*
 * Work for waiting the filesystem to come up
 */
static void ps_waitfs_work(struct work_struct *work)
{
	struct file *cfile;
	int i;

	/* Check if the file is there */
	cfile = filp_open("/var/lib/kalimba/kymera_a7da_PS.dat", O_RDWR, 0);
	if (IS_ERR(cfile)) {
		schedule_delayed_work(&dwork, msecs_to_jiffies(250));
	} else {
		filp_close(cfile, NULL);
		for (i = 0; i < ARRAY_SIZE(ps_area); i++) {
			ps_prepare_region(&ps_area[i]);
			pr_info("kalimba: ps file %s\n",
				ps_area[i].file_name);
		}
	}
}

/*
 * Update the pointers for kas, in case there is a reset
 * from the kastool
 */
void ps_ptr_update(void)
{
	static u32 dmbuf[2], buf_paddr;
	int i;

	for (i = 0; i < ARRAY_SIZE(ps_area); i++) {
		buf_paddr = (u32) virt_to_phys(ps_area[i].kbuf);
		dmbuf[0] = buf_paddr >> 24;
		dmbuf[1] = buf_paddr & 0x00ffffff;

		/*
		 * Indicate to kalimaba the PS address in DRAM
		 */
		ps_write_dm(ps_area[i].dm_ptr, 8, dmbuf);
	}
}

/*
 * Persistent storage initialisation
 */
int ps_init(void)
{
	INIT_DELAYED_WORK(&dwork, ps_waitfs_work);

	/*
	 * Register the callback for PS_FLUSH
	 */
	register_kalimba_msg_action(PS_FLUSH_REQ,
			kas_ps_notify, NULL);
	queue_delayed_work(system_wq, &dwork, 250);
	return 0;
}
