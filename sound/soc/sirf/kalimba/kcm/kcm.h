/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
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

#ifndef _KCM_KCM_H
#define _KCM_KCM_H

#define __KCM_DEBUG	1	/* 0 - disable, 1 - enable, 2 - verbose */

#if __KCM_DEBUG
#define kcm_debug(...)	pr_info(__VA_ARGS__)
#else
#define kcm_debug(...)	do {}  while (0)
#endif

extern bool kcm_enable_2mic_cvc;
extern bool kcm_force_iacc_cap;

struct kcm_chain;
struct kasobj_fe;
struct kasobj_param;
struct kasop_impl;
struct snd_soc_dapm_widget;
struct snd_soc_dapm_route;
struct kcm_card_data {
	int mclk_fs;
	int fmt;
};

int kcm_drv_status(void);
void kcm_set_dev(void *dev);
struct device *kcm_get_dev(void);

void kcm_put_codec_widget(const struct snd_soc_dapm_widget *widget,
	int widget_cnt);
const struct snd_soc_dapm_widget *kcm_get_codec_widget(int *cnt);

void kcm_put_card_widget(const struct snd_soc_dapm_widget *widget,
	int widget_cnt);
const struct snd_soc_dapm_widget *kcm_get_card_widget(int *cnt);

void kcm_put_card_route(const struct snd_soc_dapm_route *route, int route_cnt);
const struct snd_soc_dapm_route *kcm_get_card_route(int *cnt);

struct snd_soc_dai_driver *kcm_alloc_dai(void);
struct snd_soc_dai_driver *kcm_get_dai(int *cnt);

struct snd_soc_dapm_route *kcm_alloc_route(void);
struct snd_soc_dapm_route *kcm_get_route(int *cnt);

struct snd_soc_dai_link *kcm_alloc_dai_link(void);
struct snd_soc_dai_link *kcm_get_dai_link(int *cnt, int *free_cnt);

void kcm_register_ctrl(void *ctrl);
struct snd_kcontrol_new *kcm_ctrl_first(void);
struct snd_kcontrol_new *kcm_ctrl_next(void);

const struct kasop_impl *kcm_find_cap(int cap_id);
int kcm_register_cap(int cap_id, const struct kasop_impl *impl);

struct kasobj_fe *kcm_find_fe(const char *dai_name, int playback);
struct kcm_chain *kcm_prepare_chain(const struct kasobj_fe *fe,
		int playback, int channels);
void kcm_unprepare_chain(struct kcm_chain *chain);
int kcm_get_chain(struct kcm_chain *chain, const struct kasobj_param *param);
int kcm_put_chain(struct kcm_chain *chain);
int kcm_start_chain(struct kcm_chain *chain);
int kcm_stop_chain(struct kcm_chain *chain);

void kcm_lock(void);
void kcm_unlock(void);
char *kcm_strcasestr(const char *s1, const char *s2);
void kcm_set_vol_ctrl_gain(int vol);

#endif
