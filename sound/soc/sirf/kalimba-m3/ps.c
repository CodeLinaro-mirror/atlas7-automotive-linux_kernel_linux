/*
 * kailimba Persistent storage driver
 *
 * Copyright (c) 2015 The Linux Foundation. All rights reserved.
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

#include "audio-protocol.h"
#include "ps.h"
#include "regs.h"

struct delayed_work dwork;

struct ps_entry {
	char *kbuf;
	dma_addr_t phy_addr;
	struct file *ps_file;
	const char *file_name;
	size_t size;
};

struct ps_entry ps_area[] = {
	{
		.file_name = "/var/lib/kalimba/kymera_a7da_PS.dat",
		.kbuf = NULL,
		.phy_addr = 0,
	}
};

/*
 * Read the file and get it into DRAM area
 */
static int ps_prepare_region(struct ps_entry *pse)
{
	int size;
	void *buf;
	struct file *cfile;
	int rc;

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
	buf = dma_alloc_coherent(NULL, size, &pse->phy_addr, GFP_KERNEL);
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

	/*
	 * Indicate to kalimaba the PS address in DRAM
	 */
	kas_ps_region_addr_update((u32)(pse->phy_addr));

	return 0;
fail:
	kfree(buf);

err_close:
	return rc;
}

/*
 * Write the file from memory to the filesystem
 */
static int ps_write_file(struct ps_entry *pse)
{
	ssize_t ret;

	if (pse->ps_file != NULL && pse->kbuf != NULL)
		ret = kernel_write(pse->ps_file, pse->kbuf, pse->size, 0);
		if (ret < 0)
			pr_err("Error writing PS file: %s\n", pse->file_name);

	return 0;
}

/*
 * Notify for ps flush
 */
void kas_ps_update(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ps_area); i++)
		ps_write_file(&ps_area[i]);
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
	int i;

	for (i = 0; i < ARRAY_SIZE(ps_area); i++) {
		if (ps_area[i].phy_addr == 0)
			continue;

		/*
		 * Indicate to kalimaba the PS address in DRAM
		 */
		kas_ps_region_addr_update((u32)(ps_area[i].phy_addr));
	}
}

/*
 * Persistent storage initialisation
 */
int ps_init(void)
{
	INIT_DELAYED_WORK(&dwork, ps_waitfs_work);

	queue_delayed_work(system_wq, &dwork, 250);
	return 0;
}
