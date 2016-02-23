/*
 * kailimba error handling code
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

#define pr_fmt(fmt) KBUILD_MODNAME ":%s:%d: " fmt, __func__, __LINE__

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/timer.h>

#include "kerror.h"
#include "dsp.h"
#include "regs.h"
#include "ipc.h"
#include "firmware.h"

#define HEADER_SIZE   1024

#define WAIT_TIMEOUT	msecs_to_jiffies(1000)
#define WDOG_TIMEOUT	msecs_to_jiffies(1000)

static struct delayed_work kdump_dwork;
static struct delayed_work kreset_dwork;
static bool kas_crashed;
static struct workqueue_struct *reset_workq;
static struct completion kdump_done;
static struct timer_list wd_timer;


static struct {
	char *text;
	u16 error_id;
} kerror_text[] = {
	{"Success", KAS_CMD_SUCCESS},
	{"Command failed", KAS_CMD_FAILED},
	{"Command not supported", KAS_CMD_NOT_SUPPORTED},
	{"Command has invalid arguments", KAS_CMD_INVALID_ARGS},
	{"Command invalid length", KAS_CMD_INVALID_LENGTH},
	{"Connection ID used is invalid", KAS_CMD_INVALID_CONN_ID},
};

enum mtype {KAS_PM, KAS_DM1, KAS_DM2, KAS_RM};

static struct kas_mem {
	enum mtype mem_type;
	char *mem_name;
	u32 mem_start;
	u32 mem_size;
} kas_memory[] = {
	{ KAS_PM,  "DC", KAS_PM_SRAM_START_ADDR,  0x10000},
	{ KAS_DM1, "DD", KAS_DM1_SRAM_START_ADDR, 0x8000},
	{ KAS_DM2, "DD", KAS_DM2_SRAM_START_ADDR, 0x8000},
	{ KAS_RM,  "DR", 0x00FFFE00, 0x00200},
};

char *kregs[] = {
	"R PC",
	"R rMAC2",
	"R rMAC1",
	"R rMAC0",
	"R rMAC24",
	"R R0",
	"R R1",
	"R R2",
	"R R3",
	"R R4",
	"R R5",
	"R R6",
	"R R7",
	"R R8",
	"R R9",
	"R R10",
	"R RLINK",
	"R FLAGS",
	"R RMACB24",
	"R I0",
	"R I1",
	"R I2",
	"R I3",
	"R I4",
	"R I5",
	"R I6",
	"R I7",
	"R M0",
	"R M1",
	"R M2",
	"R M3",
	"R L0",
	"R L1",
	"R L3",
	"R L4",
	"R RUNCLKS",
	"R NUMINSTRS",
	"R NUMSTALLS",
	"R rMACB2",
	"R rMACB1",
	"R rMACB0",
	"R B0",
	"R B1",
	"R B4",
	"R B5",
	"R FP",
	"R SP"
};

/*
 * kas dump memory
 */
static void kerror_dumpmem(enum mtype mt,
			u32 start_addr, u32 length, u32 *data)
{
	u32 i, addr;

	/* Set address auto incriment mode on every 4 bytes */
	write_kalimba_reg(KAS_CPU_KEYHOLE_MODE, 4);

	/* Check if the memory type is Program Memory (PM) */
	if (mt == KAS_PM)
		addr = start_addr | (0x3 << 30);
	else
		addr = (start_addr << 2) | (0x2 << 30);

	write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR, addr);

	for (i = 0; i < length; i++)
		*(data + i) = read_kalimba_reg(KAS_CPU_KEYHOLE_DATA);

}

/*
 * stop kalimba
 */
static void kerror_stopdsp(void)
{
	/* Stop kalimba */
	write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR,
			(KAS_DEBUG << 2) | (0x2 << 30));
	write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, KAS_DEBUG_STOP);
}

/*
 * kas error number to string
 */
const char *kerror_str(u16 err_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(kerror_text); i++) {
		if (kerror_text[i].error_id == err_id)
			return kerror_text[i].text;
	}
	return "unknown error";
}

/*
 * kas generate core dump header
 */
static u32 kerror_coredump_header(char *pos)
{
	u32 dsp_ver = 0x00600019;
	char *p = pos;

	p += sprintf(p, "XCD2\n");
	p += sprintf(p, "AV %08x\n", dsp_ver);
	p += sprintf(p, "P DSP\n");
	p += sprintf(p, "AT KALIMBA5\n");

	return (u32)(p - pos);
}

/*
 * kas dump all memory regions
 */
static u32 kerror_coredump_memregions(char *pos)
{
	int i, k;
	u32 *buf;
	dma_addr_t phy_addr;
	char *p = pos;

	/* We know that PS has the largest memory region */
	buf = dma_alloc_coherent(NULL,
			kas_memory[KAS_PM].mem_size << 2,
			&phy_addr, GFP_KERNEL);
	if (buf == NULL) {
		pr_err("No memory to for the buffer\n");
		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(kas_memory); i++) {

		p += sprintf(p, "%s %08x %08x\n",
				kas_memory[i].mem_name,
				kas_memory[i].mem_start,
				kas_memory[i].mem_size);

		kerror_dumpmem(kas_memory[i].mem_type,
				kas_memory[i].mem_start,
				kas_memory[i].mem_size,
				buf);

		for (k = 0; k < kas_memory[i].mem_size; k++)
			p += sprintf(p, "%08x%s",
				(u32)*(buf + k),
				(k + 1) % 8 ? " ":"\n");

		/* zero the region */
		memset(buf, 0, kas_memory[KAS_PM].mem_size << 2);
	}

	kerror_dumpmem(KAS_RM, 0xffffc0,
			ARRAY_SIZE(kregs), buf);

	for (i = 0; i < ARRAY_SIZE(kregs); i++)
		p += sprintf(p, "%s %06x\n",
				kregs[i], *(buf + i));

	dma_free_coherent(NULL,
			kas_memory[KAS_PM].mem_size << 2,
			buf, phy_addr);

	return (u32)(p - pos);
}

/*
 * calculate the buffer needed for
 * coredump file
 *
 */
static u32 kerror_buf_size_calc(void)
{
	u32 i, k;
	u32 size;
	char buf[256];

	/* Frist, the size of the header */
	size  = HEADER_SIZE;

	/* Calculate buffer size for the coredump of the memory regions */
	for (i = 0; i < ARRAY_SIZE(kas_memory); i++) {
		size += sprintf(buf, "%s %08x %08x\n",
				kas_memory[i].mem_name,
				kas_memory[i].mem_start,
				kas_memory[i].mem_size);

		for (k = 0; k < kas_memory[i].mem_size; k++)
			size += sprintf(buf, "%08x%s", 0,
					(k + 1) % 8 ? " ":"\n");
	}

	/* Calculate the buffer size for the registers */
	for (i = 0; i < ARRAY_SIZE(kregs); i++)
		size += sprintf(buf, "%s %06x\n",
				kregs[i], 0);

	return size;
}

/*
 * kas generate a core dump
 */
static int do_kcoredump(void)
{
	struct file *cdfile;
	int ret;
	dma_addr_t phy_addr;
	u32 pos = 0, fsize;
	void *dp;

	kas_crashed = true;
	/* stop the dsp */
	kerror_stopdsp();

	fsize = kerror_buf_size_calc();

	/* allocate buffers */
	dp = dma_alloc_coherent(NULL, fsize, &phy_addr,
				GFP_KERNEL);
	if (dp == NULL) {
		pr_err("Alloc dram failed.\n");
		return -ENOMEM;
	}

	cdfile = filp_open("/var/lib/kalimba/coredump.xcd",
			O_RDWR | O_CREAT | O_TRUNC | O_DSYNC, 0600);
	if (IS_ERR(cdfile)) {
		ret = PTR_ERR(cdfile);
		goto open_err;
	}

	/* generate the header */
	pos = kerror_coredump_header(dp);
	/* generate coredump of mem regions */
	pos += kerror_coredump_memregions(dp + pos);

	/* write all the core dump data into a file */
	ret = kernel_write(cdfile, dp, pos, 0);
	if (ret < 0) {
		pr_err("Error writing coredump file:\n");
		ret = -EIO;
	}

	vfs_fsync(cdfile, 0);
	filp_close(cdfile, NULL);
	pr_info("kcoredump completed:\n");
open_err:
	dma_free_coherent(NULL, fsize, dp, phy_addr);

	complete(&kdump_done);
	return ret;
}

/*
 * Notify for fault
 */
static int kerror_fault_notify(u16 message, void *priv_data,
			u16 *message_data)
{
	pr_alert("kalimba has produced a fault signal\n");
	pr_alert("error code: 0x%04x, reason: 0x%04x\n",
		message_data[0], message_data[1]);
	return ACTION_HANDLED;
}

/*
 * Notify for panic
 */
static int kerror_panic_notify(u16 message, void *priv_data,
			u16 *message_data)
{
	/* Generate a code dump */
	pr_alert("generated coredump in /var/lib/kalimba directory\n");
	queue_delayed_work(system_wq, &kdump_dwork, 10);

	return ACTION_HANDLED;
}

/*
 * A coredump work function that is pushed to
 * the system working queue
 */
static void kcoredump_work(struct work_struct *work)
{
	do_kcoredump();
}

/*
 * The call to the kcoredump will schedule a work
 * that is because the application may be "zombie"
 */
void kcoredump(void)
{
	queue_delayed_work(system_wq,
			&kdump_dwork, msecs_to_jiffies(10));
}

/*
 * Performs a firmware reload, and DSP reset
 */
static void do_kreset(void)
{
	u32 fw_version;
	u16 resp[64];

	if (kalimba_get_version_id(&fw_version, resp) < 0) {
		pr_err("Failed to communicate with kas\n");
		/*
		 * Before we reload the DSP firmware,
		 * need to wait coredump to complete
		 */
		if (wait_for_completion_timeout(&kdump_done,
				WAIT_TIMEOUT) == 0)
				pr_err("kcoredump timed out\n");
			pr_err("kalimba fatal error, need reload firmware\n");
	}
}

static void kreset_work(struct work_struct *work)
{
	do_kreset();
}

void kwatchdog_clear(void)
{
	mod_timer(&wd_timer, jiffies + WDOG_TIMEOUT);
}

/*
 * Check if DSP has crashed
 */
bool kaschk_crash(void)
{
	return kas_crashed;
}

static void kwatchdog_timeout(unsigned long data)
{
	queue_delayed_work(reset_workq,
			&kreset_dwork, 0);
}

/*
 * Initialise the watchdog
 */
static void kwatchdog_init(void)
{
	init_timer(&wd_timer);
	wd_timer.function = kwatchdog_timeout;
}

void kwatchdog_start(void)
{

	wd_timer.expires = jiffies + WDOG_TIMEOUT;
	wd_timer.function = kwatchdog_timeout;
	if (!timer_pending(&wd_timer))
		add_timer(&wd_timer);
	else
		mod_timer(&wd_timer, jiffies + WDOG_TIMEOUT);
}

void kwatchdog_stop(void)
{
	del_timer(&wd_timer);
}

/*
 * kas error handling initialisation
 */
int kcoredump_init(void)
{

	INIT_DELAYED_WORK(&kdump_dwork, kcoredump_work);
	INIT_DELAYED_WORK(&kreset_dwork, kreset_work);

	kas_crashed = false;
	init_completion(&kdump_done);
	reset_workq = create_singlethread_workqueue("kasreset");

	/*
	 * At this point init the watchdog
	 */
	kwatchdog_init();

	/*
	 * Register the callback for kas panic
	 */
	register_kalimba_msg_action(KAS_ERROR_PANIC,
			kerror_panic_notify, NULL);

	/*
	 * Register the callback for kas fault
	 */
	register_kalimba_msg_action(KAS_ERROR_FAULT,
			kerror_fault_notify, NULL);

	return 0;
}
