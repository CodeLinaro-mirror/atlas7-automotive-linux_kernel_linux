/*
 *
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

#include <linux/string.h>

enum EIpcSpinLock {
	IPC_SPIN_LOCK_0 = 0,
	IPC_SPIN_LOCK_1,
	IPC_SPIN_LOCK_2,
	IPC_SPIN_LOCK_3,
	IPC_SPIN_LOCK_4,
	IPC_SPIN_LOCK_5,
	IPC_SPIN_LOCK_6,
	IPC_SPIN_LOCK_7,
	IPC_SPIN_LOCK_8,
	IPC_SPIN_LOCK_9,
	IPC_SPIN_LOCK_10,
	IPC_SPIN_LOCK_11,
	IPC_SPIN_LOCK_12,
	IPC_SPIN_LOCK_13,
	IPC_SPIN_LOCK_14,
	IPC_SPIN_LOCK_15,
	IPC_SPIN_LOCK_COUNT,
	IPC_SPIN_LOCK_INVALID = -1
};

struct SCoachSharedParams {
	u32 magic;
	int version;
	/* Used to detect struct mismatch, bumped on every change */
	void *cmdline;                  /* Linux Boot Command Line*/
	void *mem_start;                /* Start of Linux Area */
	void *mem_end;                  /* End of Linux Area */
	void *ipc_discovery_mem_cpu;
	/* Start of memory shared between CPU-COP for IPC discovery channel */
	void *ipc_discovery_mem_cop;
	/* Start of memory shared between COP-CPU for IPC discovery channel */
	u32 uart_port_num;
	/* Which uart port to use - 1 ( Same as ThreadX) , 2 ( Separate port) */
	u32 *uart_control_addr;      /* Address of UART control register */
	u32 uart_clk;
	void *system_mem_start;         /* Start memory address of the system */
	u32 system_mem_size;         /* Size of the system memory */
#ifdef USE_COACH_C2C
	void *c2c_shared_mem_from_cop;
	/* Address of buffers for C2C COP->CPU  fifo */
	void *c2c_shared_mem_from_cpu;
	/* Address of buffers for C2C CPU->COP  fifo */
#endif
	enum EIpcSpinLock  power_off;  /* Externally allocated IPC spinlock*/

	u32  sys_time;              /* rtc time */

	void *fcu_drv_shared_data;      /* some shared data for FCU driver */
};

extern struct SCoachSharedParams *g_sharedParam;



#define sharedparam_get_cmdline() (CKSEG1ADDR(g_sharedParam->cmdline))
#define sharedparam_get_sys_time() (g_sharedParam->sys_time)
#define sharedparam_get_mem_start() (u32)(g_sharedParam->mem_start)
#define sharedparam_get_mem_end() ((u32)g_sharedParam->mem_end)
#define sharedparam_get_discovery_mem_cpu() \
	(CKSEG1ADDR(g_sharedParam->ipc_discovery_mem_cpu)) /*uncache*/
#define sharedparam_get_discovery_mem_cop() \
	(CKSEG1ADDR(g_sharedParam->ipc_discovery_mem_cop)) /*uncache*/
#define sharedparam_get_uart_port_num() \
	((u32)g_sharedParam->uart_port_num)
#define sharedparam_get_uart_control_addr() \
	((u32 *)CKSEG1ADDR(g_sharedParam->uart_control_addr))
#define sharedparam_get_system_mem_start() \
	((u32)g_sharedParam->system_mem_start)
#define sharedparam_get_system_mem_size() \
	(g_sharedParam->system_mem_size)
#define sharedparam_get_power_off_spinlock()\
	(g_sharedParam->power_off)
#define sharedparam_get_fcu_drv_shared_data() \
	(phys_to_virt(g_sharedParam->fcu_drv_shared_data))
#define sharedparam_get_uart_clk() \
	(g_sharedParam->uart_clk)
