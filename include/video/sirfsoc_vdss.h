/*
 * linux/include/video/sirfsoc_vdss.h
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc
 * group company.
 *
 * Licensed under GPLv2 or later.
 */
#ifndef __SIRFSOC_VDSS_H
#define __SIRFSOC_VDSS_H

struct sirfsoc_vdss_board_info {
	const char *default_display_name;
};

bool sirfsoc_vdss_is_initialized(void);
const char *sirfsoc_vdss_get_default_panel_name(void);
#endif
