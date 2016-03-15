/*
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

#ifndef _KERROR_H
#define _KERROR_H

#define KAS_ERROR_PANIC			0x0020
#define KAS_ERROR_FAULT			0x0021

#define KAS_CMD_SUCCESS			0x0000
#define KAS_CMD_FAILED			0x1000
#define KAS_CMD_INVALID			0x1001
#define KAS_CMD_NOT_SUPPORTED		0x1002
#define KAS_CMD_INVALID_ARGS		0x1003
#define KAS_CMD_INVALID_LENGTH		0x1004
#define KAS_CMD_INVALID_CONN_ID		0x1005

#define EKASMSGTYPE			20
#define EKASPLD				21
#define EKASMSGRSP			22
#define EKASCRASH			23
#define EKASIPC				24

const char *kerror_str(u16 status);
int kcoredump_init(void);
void kcoredump(void);
bool kaschk_crash(void);
void kwatchdog_start(void);
void kwatchdog_clear(void);
void kwatchdog_stop(void);
#endif /* _KERROR_H */
