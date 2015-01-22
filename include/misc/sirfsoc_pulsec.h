/*
 * Pulse Counter Driver for CSR SiRFSoC
 *
 * Copyright (c) 2014 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2.
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
