/*
 * linux/drivers/video/fbdev/sirfsoc/vdss/vdss.h
 *
 * Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
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

/*
 * In inline mode, the NOCFIFO address register of DCU and VPP
 * must has the same value, so the two modules consulte the
 * address as the following
 */
#define INLINE_NOCFIFO_ADDR 0x132B0000

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

/* functions export from screen-sysfs.c and used by other vdss core files*/
int vdss_init_screens_sysfs(u32 lcdc_index);
void vdss_uninit_screens_sysfs(u32 lcdc_index);

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

bool lcdc_check_size(struct vdss_rect *src_rect,
	struct vdss_rect *dst_rect);
bool lcdc_get_layer_status(u32 lcdc_index, enum vdss_layer layer);

int vpp_init_platform_driver(void) __init;
void vpp_uninit_platform_driver(void);
bool vpp_passthrough_check_size(struct vdss_surface *src_surf,
	struct vdss_rect *src_rect,
	int *psrc_skip,
	struct vdss_rect *dst_rect,
	int *pdst_skip);

int lvdsc_init_platform_driver(void) __init;
void lvdsc_uninit_platform_driver(void) __init;
int lvdsc_setup(enum vdss_lvdsc_fmt fmt);
int lvdsc_select_src(u32 lcdc_index);
bool lvdsc_is_syn_mode(void);

int dcu_init_platform_driver(void) __init;
void dcu_uninit_platform_driver(void);
void dcu_enable(void);
void dcu_disable(void);
bool dcu_inline_check_size(struct vdss_surface *src_surf,
	struct vdss_rect *src_rect,
	int *psrc_skip,
	struct vdss_rect *dst_rect,
	int *pdst_skip);

#endif
