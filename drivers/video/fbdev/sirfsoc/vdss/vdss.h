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

#define NUM_LCDC	2


struct lcdc_prop {
	bool error_diffusion;
};

/* functions export from core.c and used by other vdss core files*/
struct platform_device *vdss_get_core_pdev(void);

/* functions export from layer_screen.c and used by other vdss core files*/
int vdss_init_screens(u32 lcdc_index);
void vdss_uninit_screens(u32 lcdc_index);
void vdss_init_layers(u32 lcdc_index);
void vdss_uninit_layers(u32 lcdc_index);
int vdss_screen_set_output(struct sirfsoc_vdss_screen *scn,
	struct sirfsoc_vdss_output *output);
int vdss_screen_unset_output(struct sirfsoc_vdss_screen *scn);
void vdss_screen_set_timings(struct sirfsoc_vdss_screen *scn,
	const struct sirfsoc_video_timings *timings);
void vdss_screen_set_data_lines(struct sirfsoc_vdss_screen *scn,
	int data_lines);
int vdss_screen_enable(struct sirfsoc_vdss_screen *scn);
void vdss_screen_disable(struct sirfsoc_vdss_screen *scn);
void vdss_screen_update_regs_extra(struct sirfsoc_vdss_screen *scn);
void vdss_restore_screen_layer(u32 lcdc_index);

/* functions export from layer-sysfs.c and used by other vdss core files*/
int vdss_init_layers_sysfs(u32 lcdc_index);
void vdss_uninit_layers_sysfs(u32 lcdc_index);

/* functions export from display.c and used by other vdss core files*/
int vdss_suspend_all_panels(void);
int vdss_resume_all_panels(void);
void vdss_disable_all_panels(void);

int vdss_debugfs_create_file(const char *name,
	void (*dump)(struct seq_file *));

/* functions export from lcdc.c and used by other vdss core files*/
int lcdc_init_platform_driver(void) __init;
void lcdc_uninit_platform_driver(void);
void lcdc_screen_set_timings(u32 lcdc_index, enum vdss_screen scn_id,
	const struct sirfsoc_video_timings *timings);
void lcdc_screen_set_data_lines(u32 lcdc_index, enum vdss_screen scn_id,
	int data_lines);
void lcdc_screen_set_error_diffusion(u32 lcdc_index, enum vdss_screen scn_id,
	int data_lines, bool error_diffusion);
void lcdc_screen_set_gamma(u32 lcdc_index, enum vdss_screen scn_id,
	const u8 *gamma);
void lcdc_screen_setup(u32 lcdc_index, enum vdss_screen scn_id,
	const struct sirfsoc_vdss_screen_info *info);
void lcdc_layer_setup(u32 lcdc_index, enum vdss_layer layer,
	struct sirfsoc_vdss_layer_info *info,
	struct sirfsoc_video_timings *timing);
void lcdc_layer_enable(u32 lcdc_index, enum vdss_layer layer,
	bool enable, bool passthrough);
void lcdc_flip(u32 lcdc_index, enum vdss_layer layer,
	struct sirfsoc_vdss_layer_info *info);
struct lcdc_prop *lcdc_get_prop(u32 lcdc_index);

int vpp_init_platform_driver(void) __init;
void vpp_uninit_platform_driver(void);

int lvdsc_init_platform_driver(void) __init;
void lvdsc_uninit_platform_driver(void) __init;
int lvdsc_setup(enum vdss_lvdsc_fmt fmt);
int lvdsc_select_src(u32 lcdc_index);
bool lvdsc_is_syn_mode(void);

#endif
