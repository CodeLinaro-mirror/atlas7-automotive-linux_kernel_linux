/*
 * Pulse Counter Driver for CSR SiRFSoC
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

#ifndef _MISC_SIRFSOC_PULSEC_H
#define _MISC_SIRFSOC_PULSEC_H

#include <linux/types.h>

#define SIRFSOC_PULSEC_FORWARD	1
#define SIRFSOC_PULSEC_BACKWARD	0

struct pulsec_data {
	u32	left_num;
	u32	right_num;
};

void sirfsoc_pulsec_set_direction(int direction);
void sirfsoc_pulsec_get_count(struct pulsec_data *pdata);
void sirfsoc_pulsec_set_count(struct pulsec_data *pdata);
bool sirfsoc_pulsec_inited(void);

#endif
