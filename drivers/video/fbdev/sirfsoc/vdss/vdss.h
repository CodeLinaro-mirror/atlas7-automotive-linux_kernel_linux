/*
 * linux/drivers/video/fbdev/sirfsoc/vdss/vdss.h
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc
 * group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __VDSS_H
#define __VDSS_H

#include <linux/interrupt.h>

#ifdef pr_fmt
#undef pr_fmt
#endif

#ifdef VDSS_SUBSYS_NAME
#define pr_fmt(fmt) VDSS_SUBSYS_NAME ": " fmt
#else
#define pr_fmt(fmt) fmt
#endif

#define VDSSDBG(fmt, ...)	pr_debug(fmt, ##__VA_ARGS__)
#define VDSSINFO(fmt, ...)	pr_info(fmt, ##__VA_ARGS__)
#define VDSSWARN(fmt, ...)	pr_warn(fmt, ##__VA_ARGS__)
#define VDSSERR(fmt, ...)	pr_err(fmt, ##__VA_ARGS__)

/* functions export from layer_screen.c and used by other vdss core files*/
int vdss_init_screens(void);
void vdss_uninit_screens(void);
void vdss_init_layers(void);
void vdss_uninit_layers(void);
int vdss_screen_set_output(struct sirfsoc_vdss_screen *scn,
	struct sirfsoc_vdss_output *output);
int vdss_screen_unset_output(struct sirfsoc_vdss_screen *scn);
void vdss_screen_set_timings(struct sirfsoc_vdss_screen *scn,
	const struct sirfsoc_video_timings *timings);
void vdss_screen_set_data_lines(struct sirfsoc_vdss_screen *scn,
	int data_lines);
int vdss_screen_enable(struct sirfsoc_vdss_screen *scn);
void vdss_screen_disable(struct sirfsoc_vdss_screen *scn);

/* functions export from display.c and used by other vdss core files*/
int vdss_suspend_all_panels(void);
int vdss_resume_all_panels(void);
void vdss_disable_all_panels(void);

/* functions export from lcdc.c and used by other vdss core files*/
int lcdc_init_platform_driver(void) __init;
void lcdc_uninit_platform_driver(void);
void lcdc_screen_set_timings(enum vdss_screen scn_id,
	const struct sirfsoc_video_timings *timings);
void lcdc_screen_setup(enum vdss_screen scn_id,
	const struct sirfsoc_vdss_screen_info *info);
void lcdc_layer_setup(enum vdss_layer layer,
	struct sirfsoc_vdss_layer_info *info,
	struct sirfsoc_video_timings *timing);
void lcdc_layer_enable(enum vdss_layer layer, bool enable);

#endif
