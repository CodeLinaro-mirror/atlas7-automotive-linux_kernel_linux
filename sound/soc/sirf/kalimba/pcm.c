/*
 * kailimba audio system PCM drive
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

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "dsp.h"
#include "i2s.h"
#include "iacc.h"
#include "kcm.h"
#include "usp-pcm.h"
#include "kerror.h"

bool enable_2mic_cvc = false;
module_param(enable_2mic_cvc, bool, 0);
bool disable_uwb_cvc = false;
module_param(disable_uwb_cvc, bool, 0);
#define KAS_PCM_COUNT	15

struct kas_pcm_data {
	struct snd_pcm_substream *substream;
	u16 kalimba_notify_ep_id;
	struct endpoint_handle *sw_ep_handle;
	u32 sw_ep_handle_phy_addr;
	u32 pos;
	snd_pcm_uframes_t last_appl_ptr;
	void *action_id;
	bool kas_started;
	struct components_chain *components_chain;
	int (*hw_params)(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params);
	int (*hw_free)(struct snd_pcm_substream *substream);
	int (*trigger)(struct snd_pcm_substream *substream, int cmd);
};

struct kas_priv_data {
	struct kas_pcm_data pcm[KAS_PCM_COUNT][2];
	struct kcm_t *kcm;
	int pre_channel_volume[4];
	int stream_channel_volume[MIXER_SUPPORT_STREAMS * 2][4];
	int stream_volume[MIXER_SUPPORT_STREAMS * 2];
	int stream_ramp[2][MIXER_SUPPORT_STREAMS * 2];
	int stream_mute[MIXER_SUPPORT_STREAMS * 2];
	u16 pre_gain;
	u16 master_gain;
	u16 master_mute;
	u16 peq_switch_mode[PEQ_NUM_MAX];
	u16 peq_params_array[PEQ_NUM_MAX][PEQ_PARAMS_ARRAY_LEN_16B];
	u16 dbe_switch_mode;
	u16 dbe_params_array[DBE_PARAMS_ARRAY_LEN_16B];
	u16 delay_params_array[DELAY_PARAMS_ARRAY_LEN_16B];
};

static int i2s_master;
static int bt_usp_port;

static const struct snd_pcm_hardware kas_pcm_hardware = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_RESUME |
		SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |
		SNDRV_PCM_FMTBIT_S24_LE |
		SNDRV_PCM_FMTBIT_S32_LE,
	.period_bytes_min	= 32,
	.period_bytes_max	= 256 * 1024,
	.periods_min		= 2,
	.periods_max		= 128,
	.buffer_bytes_max	= 512 * 1024, /* 512 kbytes */
};

static int kas_playback_delay_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct kas_priv_data *pdata = snd_soc_component_get_drvdata(cmpnt);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int cntl = mc->reg & DELAY_CNTL_MASK;
	u16 pos = 0;
	int val = 0;

	switch (cntl) {
	case DELAY_PARAM_CHAN1_DELAY:
	case DELAY_PARAM_CHAN2_DELAY:
	case DELAY_PARAM_CHAN3_DELAY:
	case DELAY_PARAM_CHAN4_DELAY:
		pos = cntl;
		val = get24bit((u8 *)pdata->delay_params_array, pos);
		ucontrol->value.integer.value[0] = val;
		break;
	case DELAY_PARAM_CHAN5_DELAY:
	case DELAY_PARAM_CHAN6_DELAY:
	case DELAY_PARAM_CHAN7_DELAY:
	case DELAY_PARAM_CHAN8_DELAY:
		/* not support currently */
		return -EINVAL;
	}
	return 0;
}

static int kas_playback_delay_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct kas_priv_data *pdata = snd_soc_component_get_drvdata(cmpnt);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int cntl = mc->reg & DBE_CNTL_MASK;
	u16 pos = 0;
	int val = 0;

	switch (cntl) {
	case DELAY_PARAM_CHAN1_DELAY:
	case DELAY_PARAM_CHAN2_DELAY:
	case DELAY_PARAM_CHAN3_DELAY:
	case DELAY_PARAM_CHAN4_DELAY:
		pos = cntl;
		val = ucontrol->value.integer.value[0] & Q24_MASK;
		put24bit((u8 *)pdata->delay_params_array, pos, val);
		kalimba_set_delay_params(pos, val);
		break;
	case DELAY_PARAM_CHAN5_DELAY:
	case DELAY_PARAM_CHAN6_DELAY:
	case DELAY_PARAM_CHAN7_DELAY:
	case DELAY_PARAM_CHAN8_DELAY:
		/* not support currently */
		return -EINVAL;
	}
	return 0;
}

#define MIN_DBE_GAIN_DB		-32
/* TLV used by dbe gain */
static const DECLARE_TLV_DB_SCALE(kas_dbe_gain_tlv,
		MIN_DBE_GAIN_DB * 100, 100, 0);

static int kas_playback_dbe_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct kas_priv_data *pdata = snd_soc_component_get_drvdata(cmpnt);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int cntl = mc->reg & DBE_CNTL_MASK;
	u16 pos = 0;
	int val = 0;

	switch (mc->reg) {
	case DBE_CNTL_SWITCH:
		ucontrol->value.integer.value[0] =
			pdata->dbe_switch_mode;
		break;
	case DBE_PARAM_MIX_BALANCE:
	case DBE_PARAM_EFFECT_STRENGTH:
	case DBE_PARAM_HARM_CONTENT:
		pos = cntl;
		val = get24bit((u8 *)pdata->dbe_params_array, pos);
		ucontrol->value.integer.value[0] = val;
		break;
	case DBE_PARAM_AMP_LIMIT:
		pos = cntl;
		/* 0~32 <- -32~0 <- Q24:12.N */
		val = get24bit((u8 *)pdata->dbe_params_array, pos);
		if (val & 0x00800000) {
			ucontrol->value.integer.value[0] =
				((val >> 12) | 0xFFFFF000) - MIN_DBE_GAIN_DB;
		} else
			ucontrol->value.integer.value[0] =
				(val >> 12) - MIN_DBE_GAIN_DB;
		break;
	case DBE_PARAM_XOVER_FC:
	case DBE_PARAM_LP_FC:
	case DBE_PARAM_HP_FC:
		pos = cntl;
		/* 40~1000/50~300/30~300 <- Q24:20.N */
		val = get24bit((u8 *)pdata->dbe_params_array, pos);
		ucontrol->value.integer.value[0] = val >> 4;
		break;
	}
	return 0;
}

static int kas_playback_dbe_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct kas_priv_data *pdata = snd_soc_component_get_drvdata(cmpnt);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int cntl = mc->reg & DBE_CNTL_MASK;
	u16 pos = 0;
	int val = 0;

	switch (cntl) {
	case DBE_CNTL_SWITCH:
		pdata->dbe_switch_mode =
			ucontrol->value.integer.value[0];
		/* 0~2 -> 1~3 */
		val = ucontrol->value.integer.value[0] + 1;
		kalimba_set_dbe_control(val);
		break;
	case DBE_PARAM_MIX_BALANCE:
	case DBE_PARAM_EFFECT_STRENGTH:
	case DBE_PARAM_HARM_CONTENT:
		pos = cntl;
		val = ucontrol->value.integer.value[0] & Q24_MASK;
		put24bit((u8 *)pdata->dbe_params_array, pos, val);
		kalimba_set_dbe_params(pos, val);
		break;
	case DBE_PARAM_AMP_LIMIT:
		pos = cntl;
		/* 0~32 -> -32~0 -> Q24:12.N */
		val = ((ucontrol->value.integer.value[0] + MIN_DBE_GAIN_DB)
			<< 12) & Q24_MASK;
		put24bit((u8 *)pdata->dbe_params_array, pos, val);
		kalimba_set_dbe_params(pos, val);
		break;
	case DBE_PARAM_XOVER_FC:
	case DBE_PARAM_LP_FC:
	case DBE_PARAM_HP_FC:
		pos = cntl;
		/* 40~1000/50~300/30~300 -> Q24:20.N */
		if (ucontrol->value.integer.value[0] < mc->shift)
			return -EINVAL;
		val = (ucontrol->value.integer.value[0] << 4) & Q24_MASK;
		put24bit((u8 *)pdata->dbe_params_array, pos, val);
		kalimba_set_dbe_params(pos, val);
		break;
	}
	return 0;
}

static int kas_playback_peq_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
static int kas_playback_peq_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
#define MIN_PEQ_GAIN_DB		-60
/* TLV used by peq gain */
static const DECLARE_TLV_DB_SCALE(kas_peq_gain_tlv,
		MIN_PEQ_GAIN_DB * 100, 100, 0);

/* Convenience kcontrol builders for PEQ */
#define KAS_PEQ_PER_BAND_CONTROLS(name, base, band)			\
	SOC_SINGLE_EXT(name " Band" #band " FC",			\
		base + band * 0x100 + PEQ_PARAM_BAND_FC, 20, 24000, 0,	\
		kas_playback_peq_get, kas_playback_peq_put),		\
	SOC_SINGLE_EXT_TLV(name " Band" #band " Gain",			\
		base + band * 0x100 + PEQ_PARAM_BAND_GAIN, 0, 80, 0,	\
		kas_playback_peq_get, kas_playback_peq_put,		\
		kas_peq_gain_tlv)

#define KAS_PEQ_ALL_BANDS_CONTROLS(name, base)		\
	KAS_PEQ_PER_BAND_CONTROLS(name, base, 1),	\
	KAS_PEQ_PER_BAND_CONTROLS(name, base, 2),	\
	KAS_PEQ_PER_BAND_CONTROLS(name, base, 3),	\
	KAS_PEQ_PER_BAND_CONTROLS(name, base, 4),	\
	KAS_PEQ_PER_BAND_CONTROLS(name, base, 5),	\
	KAS_PEQ_PER_BAND_CONTROLS(name, base, 6),	\
	KAS_PEQ_PER_BAND_CONTROLS(name, base, 7),	\
	KAS_PEQ_PER_BAND_CONTROLS(name, base, 8),	\
	KAS_PEQ_PER_BAND_CONTROLS(name, base, 9),	\
	KAS_PEQ_PER_BAND_CONTROLS(name, base, 10)

#define KAS_PEQ_CONTROLS(name, base)					\
	SOC_SINGLE_EXT(name " Switch Mode",				\
		base + PEQ_CNTL_SWITCH, 0, 2, 0,			\
		kas_playback_peq_get, kas_playback_peq_put),		\
	SOC_SINGLE_EXT(name " Core Type",				\
		base + PEQ_PARAM_CORE_TYPE, 0, 2, 0,			\
		kas_playback_peq_get, kas_playback_peq_put),		\
	SOC_SINGLE_EXT(name " Bands Num",				\
		base + PEQ_PARAM_BANDS_NUM, 0, 10, 0,			\
		kas_playback_peq_get, kas_playback_peq_put),		\
	SOC_SINGLE_EXT_TLV(name " Master Gain",				\
		base + PEQ_PARAM_MASTER_GAIN, 0, 80, 0,			\
		kas_playback_peq_get, kas_playback_peq_put,		\
		kas_peq_gain_tlv),					\
	KAS_PEQ_ALL_BANDS_CONTROLS(name, base)

static int kas_playback_peq_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct kas_priv_data *pdata = snd_soc_component_get_drvdata(cmpnt);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	u16 index = (mc->reg & PEQ_BASE_MASK) >> PEQ_BASE_SHIFT;
	int band = (mc->reg & PEQ_BAND_MASK) >> PEQ_BAND_SHIFT;
	int cntl = mc->reg & PEQ_CNTL_MASK;
	u16 pos = 0;
	int val = 0;

	switch (cntl) {
	case PEQ_CNTL_SWITCH:
		ucontrol->value.integer.value[0] =
			pdata->peq_switch_mode[index];
		break;
	case PEQ_PARAM_CORE_TYPE:
	case PEQ_PARAM_BANDS_NUM:
		pos = cntl;
		val = get24bit((u8 *)pdata->peq_params_array[index], pos);
		ucontrol->value.integer.value[0] = val;
		break;
	case PEQ_PARAM_MASTER_GAIN:
		pos = cntl;
		/* 0~80 <- -60~20 <- Q24:12.N */
		val = get24bit((u8 *)pdata->peq_params_array[index], pos);
		if (val & 0x00800000) {
			ucontrol->value.integer.value[0] =
				((val >> 12) | 0xFFFFF000) - MIN_PEQ_GAIN_DB;
		} else
			ucontrol->value.integer.value[0] =
				(val >> 12) - MIN_PEQ_GAIN_DB;
		break;
	case PEQ_PARAM_BAND_FC:
		pos = (band - 1) * 4 + cntl;
		/* 20~24000 <- Q24:20.N */
		val = get24bit((u8 *)pdata->peq_params_array[index], pos);
		ucontrol->value.integer.value[0] = val >> 4;
		break;
	case PEQ_PARAM_BAND_GAIN:
		pos = (band - 1) * 4 + cntl;
		/* 0~80 <- -60~20 <- Q24:12.N */
		val = get24bit((u8 *)pdata->peq_params_array[index], pos);
		if (val & 0x00800000) {
			ucontrol->value.integer.value[0] =
				((val >> 12) | 0xFFFFF000) - MIN_PEQ_GAIN_DB;
		} else
			ucontrol->value.integer.value[0] =
				(val >> 12) - MIN_PEQ_GAIN_DB;
		break;
	case PEQ_PARAM_CONFIG:
	case PEQ_PARAM_BAND_FILTER:
	case PEQ_PARAM_BAND_Q:
		/* not support currently */
		return -EINVAL;
	}
	return 0;
}

static int kas_playback_peq_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct kas_priv_data *pdata = snd_soc_component_get_drvdata(cmpnt);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	u16 index = (mc->reg & PEQ_BASE_MASK) >> PEQ_BASE_SHIFT;
	int band = (mc->reg & PEQ_BAND_MASK) >> PEQ_BAND_SHIFT;
	int cntl = mc->reg & PEQ_CNTL_MASK;
	u16 pos = 0;
	int val = 0;

	switch (cntl) {
	case PEQ_CNTL_SWITCH:
		pdata->peq_switch_mode[index] =
			ucontrol->value.integer.value[0];
		/* 0~2 -> 1~3 */
		val = ucontrol->value.integer.value[0] + 1;
		kalimba_set_peq_control(index, val);
		break;
	case PEQ_PARAM_CORE_TYPE:
	case PEQ_PARAM_BANDS_NUM:
		pos = cntl;
		val = ucontrol->value.integer.value[0] & Q24_MASK;
		put24bit((u8 *)pdata->peq_params_array[index], pos, val);
		kalimba_set_peq_params(index, pos, val);
		break;
	case PEQ_PARAM_MASTER_GAIN:
		pos = cntl;
		/* 0~80 -> -60~20 -> Q24:12.N */
		val = ((ucontrol->value.integer.value[0] + MIN_PEQ_GAIN_DB)
			<< 12) & Q24_MASK;
		put24bit((u8 *)pdata->peq_params_array[index], pos, val);
		kalimba_set_peq_params(index, pos, val);
		break;
	case PEQ_PARAM_BAND_FC:
		pos = (band - 1) * 4 + cntl;
		/* 20~24000 -> Q24:20.N */
		if (ucontrol->value.integer.value[0] < mc->shift)
			return -EINVAL;
		val = (ucontrol->value.integer.value[0] << 4) & Q24_MASK;
		put24bit((u8 *)pdata->peq_params_array[index], pos, val);
		kalimba_set_peq_params(index, pos, val);
		break;
	case PEQ_PARAM_BAND_GAIN:
		pos = (band - 1) * 4 + cntl;
		/* 0~80 -> -60~20 -> Q24:12.N */
		val = ((ucontrol->value.integer.value[0] + MIN_PEQ_GAIN_DB)
			<< 12) & Q24_MASK;
		put24bit((u8 *)pdata->peq_params_array[index], pos, val);
		kalimba_set_peq_params(index, pos, val);
		break;
	case PEQ_PARAM_CONFIG:
	case PEQ_PARAM_BAND_FILTER:
	case PEQ_PARAM_BAND_Q:
		/* not support currently */
		return -EINVAL;
	}
	return 0;
}

#define MIN_CHANNEL_GAIN_DB		-120
#define MIN_MUSIC_PREGAIN_DB		-60
#define MIN_STREAM_GAIN_DB		-96
#define MIXER_GAIN_REG			4
#define PREGAIN_REG			9
#define MIXER_MUTE_REG			10
#define MIXER_RAMP_REG			15
#define MIXER_CHAN_REG                  20
#define MASTER_GAIN_REG			40
#define MASTER_MUTE_REG			41
#define MAX_RAMP_NUM_SAMPLES		0x00ffffff

static int kas_playback_ramp_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct kas_priv_data *pdata = snd_soc_component_get_drvdata(cmpnt);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	ucontrol->value.integer.value[0] =
		pdata->stream_ramp[0][mc->reg - MIXER_RAMP_REG];
	ucontrol->value.integer.value[1] =
		pdata->stream_ramp[1][mc->reg - MIXER_RAMP_REG];
	return 0;
}

static int kas_playback_ramp_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct kas_priv_data *pdata = snd_soc_component_get_drvdata(cmpnt);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	pdata->stream_ramp[0][mc->reg - MIXER_RAMP_REG] =
		ucontrol->value.integer.value[0];
	pdata->stream_ramp[1][mc->reg - MIXER_RAMP_REG] =
		ucontrol->value.integer.value[1];
	return 0;
}

static int kas_playback_mute_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct kas_priv_data *pdata = snd_soc_component_get_drvdata(cmpnt);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	if (mc->reg == MASTER_MUTE_REG) /* Volume-Control: Master Mute */
		ucontrol->value.integer.value[0] =
			pdata->master_mute;
	else /* Mixer: Per-Stream Mute */
		ucontrol->value.integer.value[0] =
			pdata->stream_mute[mc->reg - MIXER_MUTE_REG];
	return 0;
}

static int kas_playback_mute_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct kas_priv_data *pdata = snd_soc_component_get_drvdata(cmpnt);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int mute = ucontrol->value.integer.value[0];
	u16 stream = 0;
	int i = 0;

	if (mc->reg == MASTER_MUTE_REG) { /* Volume-Control: Master Mute */
		pdata->master_mute = ucontrol->value.integer.value[0];
		if (mute)
			kalimba_set_master_gain(MIN_CHANNEL_GAIN_DB);
		else
			kalimba_set_master_gain(MIN_CHANNEL_GAIN_DB +
				pdata->master_gain);
	} else { /* Mixer: Per-Stream Mute */
		pdata->stream_mute[mc->reg - MIXER_MUTE_REG] = mute;
		stream = mc->reg - MIXER_MUTE_REG;

		if (mute)
			kalimba_set_stream_volume(stream,
				MIN_STREAM_GAIN_DB,
				pdata->stream_ramp[1][stream]);
		else {
			for (i = 0; i < 4; i++)
				kalimba_set_stream_channel_volume(stream, i,
					MIN_STREAM_GAIN_DB +
					pdata->stream_channel_volume[stream][i],
					pdata->stream_ramp[1][stream]);
		}
	}
	return 0;
}

static int kas_playback_volume_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct kas_priv_data *pdata = snd_soc_component_get_drvdata(cmpnt);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	u16 stream = 0;
	u16 channel = 0;

	if (mc->reg < MIXER_GAIN_REG) /* Volume-Control: Per Spk Volume */
		ucontrol->value.integer.value[0] =
			pdata->pre_channel_volume[mc->reg];
	else if (mc->reg == PREGAIN_REG) /* Pass-Through: Pre Gain */
		ucontrol->value.integer.value[0] = pdata->pre_gain;
	else if (mc->reg == MASTER_GAIN_REG) /* Volume-Control: Master Gain */
		ucontrol->value.integer.value[0] = pdata->master_gain;
	else if (mc->reg >= MIXER_CHAN_REG) { /* Mixer: Stream Channel Volume */
		stream = (mc->reg - MIXER_CHAN_REG) / 4;
		channel = (mc->reg - MIXER_CHAN_REG) - 4 * stream;
		ucontrol->value.integer.value[0] =
			pdata->stream_channel_volume[stream][channel];
	} else /* Mixer: Stream Volume */
		ucontrol->value.integer.value[0] =
			pdata->stream_volume[mc->reg - MIXER_GAIN_REG];
	return 0;
}

static int kas_playback_volume_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct kas_priv_data *pdata = snd_soc_component_get_drvdata(cmpnt);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	u16 stream = 0;
	u16 channel = 0;

	if (mc->reg < MIXER_GAIN_REG) { /* Volume-Control: Per Spk Volume */
		pdata->pre_channel_volume[mc->reg] =
			ucontrol->value.integer.value[0];
		kalimba_set_channel_volume(mc->reg, MIN_CHANNEL_GAIN_DB +
			pdata->pre_channel_volume[mc->reg]);
	} else if (mc->reg == PREGAIN_REG) { /* Pass-Through: Pre Gain */
		pdata->pre_gain = ucontrol->value.integer.value[0];
		kalimba_set_music_passthrough_volume(
			MIN_MUSIC_PREGAIN_DB + pdata->pre_gain);
	} else if (mc->reg == MASTER_GAIN_REG) {
		/* Volume-Control: Master Gain */
		pdata->master_gain = ucontrol->value.integer.value[0];
		kalimba_set_master_gain(MIN_CHANNEL_GAIN_DB +
			pdata->master_gain);
	} else if (mc->reg >= MIXER_CHAN_REG) {
		/* Mixer: Stream Channel Volume */
		stream = (mc->reg - MIXER_CHAN_REG) / 4;
		channel = (mc->reg - MIXER_CHAN_REG) - 4 * stream;
		pdata->stream_channel_volume[stream][channel] =
			ucontrol->value.integer.value[0];
		kalimba_set_stream_channel_volume(stream, channel,
			MIN_STREAM_GAIN_DB +
			pdata->stream_channel_volume[stream][channel],
			pdata->stream_ramp[0][stream]);
	} else { /* Mixer: Stream Volume */
		stream = mc->reg - MIXER_GAIN_REG;
		pdata->stream_volume[stream] =
			ucontrol->value.integer.value[0];
		for (channel = 0; channel < 4; channel++)
			pdata->stream_channel_volume[stream][channel] =
				ucontrol->value.integer.value[0];
		kalimba_set_stream_volume(stream,
			MIN_STREAM_GAIN_DB +
			pdata->stream_volume[stream],
			pdata->stream_ramp[0][stream]
			);
	}
	return 0;
}

/* TLV used by volume control volumes */
static const DECLARE_TLV_DB_SCALE(kas_channel_vol_tlv,
		MIN_CHANNEL_GAIN_DB * 100, 100, 0);
/* TLV used by stream volumes */
static const DECLARE_TLV_DB_SCALE(kas_stream_vol_tlv,
		MIN_STREAM_GAIN_DB * 100, 100, 0);
/* TLV used by music pregain */
static const DECLARE_TLV_DB_SCALE(kas_music_pregain_tlv,
		MIN_MUSIC_PREGAIN_DB * 100, 100, 0);

static const struct snd_kcontrol_new kas_controls[] = {
	SOC_SINGLE_EXT_TLV("Front Left Playback Volume", 0, 0, 129, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_channel_vol_tlv),
	SOC_SINGLE_EXT_TLV("Front Right Playback Volume", 1, 0, 129, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_channel_vol_tlv),
	SOC_SINGLE_EXT_TLV("Rear Left Playback Volume", 2, 0, 129, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_channel_vol_tlv),
	SOC_SINGLE_EXT_TLV("Rear Right Playback Volume", 3, 0, 129, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_channel_vol_tlv),
	SOC_SINGLE_EXT_TLV("Music Stream Playback Volume", 4, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Navigation Stream Playback Volume", 5, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Alarm Stream Playback Volume", 6, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Multimedia Volume", 7, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Voicecall Volume", 8, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Music pregain Volume", 9, 0, 60, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_music_pregain_tlv),
	SOC_SINGLE_EXT("Music Stream Mute", 10, 0, 1, 0,
		kas_playback_mute_get, kas_playback_mute_put),
	SOC_SINGLE_EXT("Navigation Stream Mute", 11, 0, 1, 0,
		kas_playback_mute_get, kas_playback_mute_put),
	SOC_SINGLE_EXT("Alarm Stream Mute", 12, 0, 1, 0,
		kas_playback_mute_get, kas_playback_mute_put),
	SOC_SINGLE_EXT("Multimedia Mute", 13, 0, 1, 0,
		kas_playback_mute_get, kas_playback_mute_put),
	SOC_SINGLE_EXT("Voicecall Mute", 14, 0, 1, 0,
		kas_playback_mute_get, kas_playback_mute_put),
	SOC_DOUBLE_EXT("Music Stream Ramp",
		15, 0, 1, MAX_RAMP_NUM_SAMPLES, 0,
		kas_playback_ramp_get, kas_playback_ramp_put),
	SOC_DOUBLE_EXT("Navigation Stream Ramp",
		16, 0, 1, MAX_RAMP_NUM_SAMPLES, 0,
		kas_playback_ramp_get, kas_playback_ramp_put),
	SOC_DOUBLE_EXT("Alarm Stream Ramp",
		17, 0, 1, MAX_RAMP_NUM_SAMPLES, 0,
		kas_playback_ramp_get, kas_playback_ramp_put),
	SOC_DOUBLE_EXT("Multimedia Ramp",
		18, 0, 1, MAX_RAMP_NUM_SAMPLES, 0,
		kas_playback_ramp_get, kas_playback_ramp_put),
	SOC_DOUBLE_EXT("Voicecall Ramp",
		19, 0, 1, MAX_RAMP_NUM_SAMPLES, 0,
		kas_playback_ramp_get, kas_playback_ramp_put),
	SOC_SINGLE_EXT_TLV("Music Stream Front Left",
		20, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Music Stream Front Right",
		21, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Music Stream Rear Left",
		22, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Music Stream Rear Right",
		23, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Navigation Stream Front Left",
		24, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Navigation Stream Front Right",
		25, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Navigation Stream Rear Left",
		26, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Navigation Stream Rear Right",
		27, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Alarm Stream Front Left",
		28, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Alarm Stream Front Right",
		29, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Alarm Stream Rear Left",
		30, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Alarm Stream Rear Right",
		31, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Multimedia Front Left",
		32, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Multimedia Front Right",
		33, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Multimedia Rear Left",
		34, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Multimedia Rear Right",
		35, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Voicecall Front Left",
		36, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Voicecall Front Right",
		37, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Voicecall Rear Left",
		38, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Voicecall Rear Right",
		39, 0, 96, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_stream_vol_tlv),
	SOC_SINGLE_EXT_TLV("Master Gain", 40, 0, 129, 0,
		kas_playback_volume_get, kas_playback_volume_put,
		kas_channel_vol_tlv),
	SOC_SINGLE_EXT("Master Mute", 41, 0, 1, 0,
		kas_playback_mute_get, kas_playback_mute_put),
	/* peq */
	KAS_PEQ_CONTROLS("User PEQ", USER_PEQ_BASE),
	KAS_PEQ_CONTROLS("Spk1 PEQ", SPK1_PEQ_BASE),
	KAS_PEQ_CONTROLS("Spk2 PEQ", SPK2_PEQ_BASE),
	KAS_PEQ_CONTROLS("Spk3 PEQ", SPK3_PEQ_BASE),
	KAS_PEQ_CONTROLS("Spk4 PEQ", SPK4_PEQ_BASE),
	/* dbe */
	SOC_SINGLE_EXT("DBE Switch Mode", DBE_CNTL_SWITCH, 0, 2, 0,
		kas_playback_dbe_get, kas_playback_dbe_put),
	SOC_SINGLE_EXT("DBE Xover FC", DBE_PARAM_XOVER_FC, 40, 1000, 0,
		kas_playback_dbe_get, kas_playback_dbe_put),
	SOC_SINGLE_EXT("DBE Mix Balance", DBE_PARAM_MIX_BALANCE, 0, 100, 0,
		kas_playback_dbe_get, kas_playback_dbe_put),
	SOC_SINGLE_EXT("DBE Effect Strength",
		DBE_PARAM_EFFECT_STRENGTH, 0, 100, 0,
		kas_playback_dbe_get, kas_playback_dbe_put),
	SOC_SINGLE_EXT_TLV("DBE Amp Limit", DBE_PARAM_AMP_LIMIT, 0, 32, 0,
		kas_playback_dbe_get, kas_playback_dbe_put,
		kas_dbe_gain_tlv),
	SOC_SINGLE_EXT("DBE LP FC", DBE_PARAM_LP_FC, 50, 300, 0,
		kas_playback_dbe_get, kas_playback_dbe_put),
	SOC_SINGLE_EXT("DBE HP FC", DBE_PARAM_HP_FC, 50, 300, 0,
		kas_playback_dbe_get, kas_playback_dbe_put),
	SOC_SINGLE_EXT("DBE Harm Content", DBE_PARAM_HARM_CONTENT, 0, 100, 0,
		kas_playback_dbe_get, kas_playback_dbe_put),
	/* delay */
	SOC_SINGLE_EXT("Delay Chan1 Delay", DELAY_PARAM_CHAN1_DELAY, 0, 768, 0,
		kas_playback_delay_get, kas_playback_delay_put),
	SOC_SINGLE_EXT("Delay Chan2 Delay", DELAY_PARAM_CHAN2_DELAY, 0, 768, 0,
		kas_playback_delay_get, kas_playback_delay_put),
	SOC_SINGLE_EXT("Delay Chan3 Delay", DELAY_PARAM_CHAN3_DELAY, 0, 768, 0,
		kas_playback_delay_get, kas_playback_delay_put),
	SOC_SINGLE_EXT("Delay Chan4 Delay", DELAY_PARAM_CHAN4_DELAY, 0, 768, 0,
		kas_playback_delay_get, kas_playback_delay_put)
};

static int kas_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kas_pcm_data *pcm_data =
		&pdata->pcm[rtd->cpu_dai->id][substream->stream];

	if (kaschk_crash())
		return -EPIPE;

	if (open_stream(rtd->cpu_dai->id) == PIPELINE_BUSY)
		return -EBUSY;

	pcm_data->substream = substream;
	snd_soc_set_runtime_hwparams(substream, &kas_pcm_hardware);
	return snd_pcm_hw_constraint_integer(substream->runtime,
		SNDRV_PCM_HW_PARAM_PERIODS);
}

static int kas_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	close_stream(rtd->cpu_dai->id);

	return 0;
}

static int kas_data_notify(u16 message, void *priv_data, u16 *message_data)
{
	struct kas_pcm_data *pcm_data = (struct kas_pcm_data *)priv_data;

	if (message_data[0] == pcm_data->kalimba_notify_ep_id) {
		pcm_data->pos = (message_data[1] << 16 | message_data[2]) * 4;
		snd_pcm_period_elapsed(pcm_data->substream);
		/* Move the watchdog forward */
		return ACTION_HANDLED;
	} else
		return ACTION_NONE;
}

static int kas_pcm_generic_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kas_pcm_data *pcm_data =
		&pdata->pcm[rtd->cpu_dai->id][substream->stream];
	int playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	struct snd_dma_buffer *dmab;
	int stream = rtd->cpu_dai->id;

	if (stream == MUSIC_STREAM) {
		switch (params_channels(params)) {
		case 1:
			stream = MUSIC_MONO_STREAM;
			break;
		case 2:
			stream = MUSIC_STEREO_STREAM;
			break;
		case 4:
			stream = MUSIC_4CHANNELS_STREAM;
			break;
		default:
			break;
		}
	}
	if (stream == ANALOG_CAPTURE_STREAM) {
		switch (params_channels(params)) {
		case 1:
			stream = CAPTURE_MONO_STREAM;
			break;
		case 2:
			stream = CAPTURE_STEREO_STREAM;
			break;
		default:
			break;
		}
	}

	dmab = snd_pcm_get_dma_buf(substream);

	pcm_data->sw_ep_handle->buff_addr = dmab->addr;
	pcm_data->sw_ep_handle->buff_length =
		params_buffer_bytes(params) / 4;
	pcm_data->sw_ep_handle->write_pointer = 0;
	pcm_data->sw_ep_handle->read_pointer = 0;

	memset(dmab->area, 0, params_buffer_bytes(params));

	pcm_data->kalimba_notify_ep_id = prepare_stream(stream,
		params_channels(params),
		pcm_data->sw_ep_handle_phy_addr, params_rate(params),
		1, params_period_bytes(params) / 4);
	if (playback)
		pcm_data->action_id = register_kalimba_msg_action(
			DATA_CONSUMED, kas_data_notify, pcm_data);
	else
		pcm_data->action_id = register_kalimba_msg_action(
			DATA_PRODUCED, kas_data_notify, pcm_data);
	return 0;
}

static int kas_pcm_voicecall_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kcm_t *kcm = pdata->kcm;
	int playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;

	if (playback) {
		memset(kcm->playback_usp_sco_ep.buff, 0,
			kcm->playback_usp_sco_ep.buff_bytes);
		sirf_usp_pcm_params(bt_usp_port, playback,
			kcm->playback_usp_sco_ep.channels,
			kcm->playback_usp_sco_ep.sample_rate);
	} else {
		memset(kcm->capture_usp_sco_ep.buff, 0,
			kcm->capture_usp_sco_ep.buff_bytes);
		sirf_usp_pcm_params(bt_usp_port, playback,
			kcm->capture_usp_sco_ep.channels,
			kcm->capture_usp_sco_ep.sample_rate);
	}
	prepare_stream(rtd->cpu_dai->id, params_channels(params), 0,
		params_rate(params), 1, params_period_bytes(params) / 4);
	return 0;
}

static int kas_pcm_usp_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kcm_t *kcm = pdata->kcm;
	int stream = rtd->cpu_dai->id;
	int usp_port;

	switch (stream) {
	case USP0_TO_IACC_LOOPBACK_STREAM:
		usp_port = 0;
		break;
	case USP1_TO_IACC_LOOPBACK_STREAM:
		usp_port = 1;
		break;
	case USP2_TO_IACC_LOOPBACK_STREAM:
		usp_port = 2;
		break;
	case A2DP_STREAM:
		usp_port = 3;
		break;
	default:
		break;
	}

	memset(kcm->capture_usp_stereo_ep[usp_port].buff, 0,
		kcm->capture_usp_stereo_ep[usp_port].buff_bytes);
	sirf_usp_pcm_params(usp_port, 0,
		kcm->capture_usp_stereo_ep[usp_port].channels,
		kcm->capture_usp_stereo_ep[usp_port].sample_rate);
	prepare_stream(rtd->cpu_dai->id, params_channels(params), 0,
		params_rate(params), 0, params_period_bytes(params) / 4);
	return 0;
}

static int kas_pcm_iacc_loopback_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kcm_t *kcm = pdata->kcm;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		memset(kcm->playback_iacc_ep.buff, 0,
				kcm->playback_iacc_ep.buff_bytes);
		prepare_stream(rtd->cpu_dai->id, params_channels(params), 0,
			params_rate(params), 1,
			params_period_bytes(params) / 4);
	} else
		memset(kcm->capture_iacc_stereo_ep.buff, 0,
			kcm->capture_iacc_stereo_ep.buff_bytes);

	return 0;
}

static int kas_pcm_i2s_to_iacc_loopback_hw_params(
	struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kcm_t *kcm = pdata->kcm;

	memset(kcm->playback_iacc_ep.buff, 0,
		kcm->playback_iacc_ep.buff_bytes);
	memset(kcm->capture_i2s_stereo_ep.buff, 0,
		kcm->capture_i2s_stereo_ep.buff_bytes);
	sirf_i2s_params(params_channels(params), params_rate(params),
		!i2s_master);
	prepare_stream(rtd->cpu_dai->id, params_channels(params), 0,
		params_rate(params), i2s_master,
		params_period_bytes(params) / 4);

	return 0;
}

static int kas_pcm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kas_pcm_data *pcm_data =
		&pdata->pcm[rtd->cpu_dai->id][substream->stream];
	int ret;

	pcm_data->pos = 0;
	pcm_data->last_appl_ptr = 0;

	ret = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
	if (ret < 0) {
		dev_err(rtd->dev, "allocate %d bytes for PCM failed: %d\n",
			params_buffer_bytes(params), ret);
		return ret;
	}

	if (pcm_data->hw_params) {
		ret = pcm_data->hw_params(substream, params);
		if (ret < 0)
			goto failed;
	}
	pcm_data->kas_started = true;
	return 0;
failed:
	snd_pcm_lib_free_pages(substream);
	return ret;
}

static int kas_pcm_generic_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kas_pcm_data *pcm_data =
		&pdata->pcm[rtd->cpu_dai->id][substream->stream];
	int stream = rtd->cpu_dai->id;

	if (stream == MUSIC_STREAM) {
		switch (substream->runtime->channels) {
		case 1:
			stream = MUSIC_MONO_STREAM;
			break;
		case 2:
			stream = MUSIC_STEREO_STREAM;
			break;
		case 4:
			stream = MUSIC_4CHANNELS_STREAM;
			break;
		default:
			break;
		}
	}
	if (stream == ANALOG_CAPTURE_STREAM) {
		switch (substream->runtime->channels) {
		case 1:
			stream = CAPTURE_MONO_STREAM;
			break;
		case 2:
			stream = CAPTURE_STEREO_STREAM;
			break;
		default:
			break;
		}
	}

	stop_stream(stream);
	destroy_stream(stream);
	unregister_kalimba_msg_action(pcm_data->action_id);

	return 0;
}

static int kas_pcm_voicecall_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	stop_stream(rtd->cpu_dai->id);
	destroy_stream(rtd->cpu_dai->id);
	return 0;
}

static int kas_pcm_usp_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	stop_stream(rtd->cpu_dai->id);
	destroy_stream(rtd->cpu_dai->id);
	return 0;
}

static int kas_pcm_iacc_loopback_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		stop_stream(rtd->cpu_dai->id);
		destroy_stream(rtd->cpu_dai->id);
	}

	return 0;
}

static int kas_pcm_i2s_to_iacc_loopback_hw_free(
	struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	stop_stream(rtd->cpu_dai->id);
	destroy_stream(rtd->cpu_dai->id);

	return 0;
}

static int kas_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kas_pcm_data *pcm_data =
		&pdata->pcm[rtd->cpu_dai->id][substream->stream];

	if (!pcm_data->kas_started)
		return 0;

	if (pcm_data->hw_free)
		pcm_data->hw_free(substream);

	snd_pcm_lib_free_pages(substream);
	pcm_data->kas_started = false;

	return 0;
}

static int kas_pcm_generic_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kas_pcm_data *pcm_data =
		&pdata->pcm[rtd->cpu_dai->id][substream->stream];
	int playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	struct kcm_t *kcm = pdata->kcm;
	int stream = rtd->cpu_dai->id;
	int ret;

	if (stream == MUSIC_STREAM) {
		switch (substream->runtime->channels) {
		case 1:
			stream = MUSIC_MONO_STREAM;
			break;
		case 2:
			stream = MUSIC_STEREO_STREAM;
			break;
		case 4:
			stream = MUSIC_4CHANNELS_STREAM;
			break;
		default:
			return -EINVAL;
		}
	}
	if (stream == ANALOG_CAPTURE_STREAM) {
		switch (substream->runtime->channels) {
		case 1:
			stream = CAPTURE_MONO_STREAM;
			break;
		case 2:
			stream = CAPTURE_STEREO_STREAM;
			break;
		default:
			return -EINVAL;
		}
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (playback)
			iacc_start(playback, kcm->playback_iacc_ep.channels);
		else
			iacc_start(playback, substream->runtime->channels);
		ret = start_stream(stream,
			!!atomic_read(&substream->mmap_count));
		if (ret < 0)
			goto error;
		if (playback)
			data_produced(pcm_data->kalimba_notify_ep_id);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (playback) {
			/* Currently work around via DAC enable always on for
			 * pop noise issue fix, which will introduce the side
			 * effect of baseline noise. */
			/* iacc_stop(playback); */
			/* Buffer pointer must be reset */
			pcm_data->pos = 0;
		} else
			iacc_stop(playback);
		break;
	default:
		return -EINVAL;
	}
	return 0;

error:
	if (playback) {
		iacc_stop(playback);
		/* Buffer pointer must be reset */
		pcm_data->pos = 0;
	} else
		iacc_stop(playback);

	return -EPIPE;
}

static int kas_pcm_voicecall_trigger(struct snd_pcm_substream *substream,
	int cmd)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	int playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	struct kcm_t *kcm = pdata->kcm;
	int ret;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (playback) {
			sirf_usp_pcm_start(bt_usp_port, 0);
			iacc_start(playback, kcm->playback_iacc_ep.channels);
		} else {
			iacc_start(playback, kcm->capture_iacc_sco_ep.channels);
			sirf_usp_pcm_start(bt_usp_port, 1);
		}
		ret = start_stream(rtd->cpu_dai->id, 1);
		if (ret < 0)
			goto error;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (playback) {
			sirf_usp_pcm_stop(bt_usp_port, 0);
			iacc_stop(1);
		} else {
			iacc_stop(0);
			sirf_usp_pcm_stop(bt_usp_port, 1);
		}
		break;
	}
	return 0;

error:
	if (playback) {
		sirf_usp_pcm_stop(bt_usp_port, 0);
		iacc_stop(1);
	} else {
		iacc_stop(0);
		sirf_usp_pcm_stop(bt_usp_port, 1);
	}

	return -EPIPE;
}

static int kas_pcm_usp_trigger(struct snd_pcm_substream *substream,
	int cmd)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kcm_t *kcm = pdata->kcm;
	int ret;
	int stream = rtd->cpu_dai->id;
	int usp_port;

	switch (stream) {
	case USP0_TO_IACC_LOOPBACK_STREAM:
		usp_port = 0;
		break;
	case USP1_TO_IACC_LOOPBACK_STREAM:
		usp_port = 1;
		break;
	case USP2_TO_IACC_LOOPBACK_STREAM:
		usp_port = 2;
		break;
	case A2DP_STREAM:
		usp_port = 3;
		break;
	default:
		break;
	}

	memset(kcm->capture_usp_stereo_ep[usp_port].buff, 0,
			kcm->capture_usp_stereo_ep[usp_port].buff_bytes);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		iacc_start(1, kcm->playback_iacc_ep.channels);
		sirf_usp_pcm_start(usp_port, 0);
		ret = start_stream(rtd->cpu_dai->id, 1);
		if (ret < 0)
			goto error;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		iacc_stop(1);
		sirf_usp_pcm_stop(usp_port, 0);
		break;
	}
	return 0;

error:
	iacc_stop(1);
	sirf_usp_pcm_stop(usp_port, 0);
	return -EPIPE;

}

static int kas_pcm_iacc_loopback_trigger(struct snd_pcm_substream *substream,
	int cmd)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kcm_t *kcm = pdata->kcm;
	int playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	int channels;
	int ret;

	if (playback)
		channels = kcm->playback_iacc_ep.channels;
	else
		channels = kcm->capture_iacc_stereo_ep.channels;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		iacc_start(playback, channels);
		if (playback) {
			ret = start_stream(rtd->cpu_dai->id, 1);
			if (ret < 0)
				goto error;
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		iacc_stop(playback);
		break;
	}
	return 0;

error:
	iacc_stop(playback);
	return -EPIPE;

}

static int kas_pcm_i2s_to_iacc_loopback_trigger(
	struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kcm_t *kcm = pdata->kcm;
	int ret;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		iacc_start(1, kcm->playback_iacc_ep.channels);
		sirf_i2s_start(0);
		ret = start_stream(rtd->cpu_dai->id, i2s_master);
		if (ret < 0)
			goto error;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		iacc_stop(0);
		sirf_i2s_stop(0);
	}
	return 0;
error:
	iacc_stop(0);
	sirf_i2s_stop(0);
	return -EPIPE;
}

static int kas_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kas_pcm_data *pcm_data =
		&pdata->pcm[rtd->cpu_dai->id][substream->stream];
	int ret = 0;

	if (kaschk_crash())
		return -EPIPE;

	if (pcm_data->trigger)
		ret = pcm_data->trigger(substream, cmd);
	return ret;
}

static snd_pcm_uframes_t kas_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kas_pcm_data *pcm_data =
		&pdata->pcm[rtd->cpu_dai->id][substream->stream];

	if (kaschk_crash())
		return SNDRV_PCM_POS_XRUN;

	return bytes_to_frames(substream->runtime, pcm_data->pos);
}

static int kas_pcm_ack(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kas_pcm_data *pcm_data =
		&pdata->pcm[rtd->cpu_dai->id][substream->stream];

	if (runtime->status->state != SNDRV_PCM_STATE_RUNNING)
		return 0;
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		return 0;

	if (runtime->control->appl_ptr - pcm_data->last_appl_ptr >=
		runtime->period_size)
		pcm_data->last_appl_ptr = runtime->control->appl_ptr;
	else
		return 0;

	pcm_data->sw_ep_handle->write_pointer =	frames_to_bytes(runtime,
		pcm_data->last_appl_ptr % runtime->buffer_size) / 4;

	data_produced(pcm_data->kalimba_notify_ep_id);
	return 0;
}

static struct snd_pcm_ops kas_pcm_ops = {
	.open = kas_pcm_open,
	.close = kas_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = kas_pcm_hw_params,
	.hw_free = kas_pcm_hw_free,
	.trigger = kas_pcm_trigger,
	.pointer = kas_pcm_pointer,
	.ack = kas_pcm_ack,
};

static int kas_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_pcm *pcm = rtd->pcm;
	struct snd_card *card = rtd->card->snd_card;
	struct snd_soc_platform *platform = rtd->platform;
	struct device *dev = platform->dev;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kas_pcm_data *pcm_data;
	struct snd_pcm_substream *substream;
	int ret = 0;
	int stream;

	ret = dma_coerce_mask_and_coherent(card->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;
	/* Enable PCM operations are in non-atomic context */
	pcm->nonatomic = true;

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream ||
			pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		ret = snd_pcm_lib_preallocate_pages_for_all(pcm,
				SNDRV_DMA_TYPE_DEV_IRAM,
				dev,
				kas_pcm_hardware.buffer_bytes_max,
				kas_pcm_hardware.buffer_bytes_max);
		if (ret) {
			dev_err(rtd->dev, "dma buffer allocation failed %d\n",
					ret);
			return ret;
		}
	}

	for (stream = 0; stream < 2; stream++) {
		const char *stream_name = rtd->dai_link->stream_name;

		substream = pcm->streams[stream].substream;
		if (!substream)
			continue;
		pcm_data = &pdata->pcm[rtd->cpu_dai->id][substream->stream];
		pcm_data->sw_ep_handle = dma_alloc_coherent(rtd->platform->dev,
				sizeof(struct endpoint_handle),
				&pcm_data->sw_ep_handle_phy_addr, GFP_KERNEL);
		if (!(strcmp(stream_name, "Voicecall-bt-to-iacc") &&
			strcmp(stream_name, "Voicecall-iacc-to-bt"))) {
			pcm_data->hw_params = kas_pcm_voicecall_hw_params;
			pcm_data->hw_free = kas_pcm_voicecall_hw_free;
			pcm_data->trigger = kas_pcm_voicecall_trigger;
		} else if (!(strcmp(stream_name, "A2DP Playback") &&
				strcmp(stream_name, "USP0 Playback") &&
				strcmp(stream_name, "USP1 Playback") &&
				strcmp(stream_name, "USP2 Playback"))) {
			pcm_data->hw_params = kas_pcm_usp_hw_params;
			pcm_data->hw_free = kas_pcm_usp_hw_free;
			pcm_data->trigger = kas_pcm_usp_trigger;
		} else if (!(strcmp(stream_name, "Iacc-loopback-playback") &&
			strcmp(stream_name, "Iacc-loopback-capture"))) {
			pcm_data->hw_params = kas_pcm_iacc_loopback_hw_params;
			pcm_data->hw_free = kas_pcm_iacc_loopback_hw_free;
			pcm_data->trigger = kas_pcm_iacc_loopback_trigger;
		} else if (!strcmp(stream_name, "I2S-to-iacc-loopback")) {
			pcm_data->hw_params =
				kas_pcm_i2s_to_iacc_loopback_hw_params;
			pcm_data->hw_free =
				kas_pcm_i2s_to_iacc_loopback_hw_free;
			pcm_data->trigger =
				kas_pcm_i2s_to_iacc_loopback_trigger;
		} else {
			pcm_data->hw_params = kas_pcm_generic_hw_params;
			pcm_data->hw_free = kas_pcm_generic_hw_free;
			pcm_data->trigger = kas_pcm_generic_trigger;
		}
	}

	return ret;
}

static void kas_pcm_free(struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;
	struct snd_soc_pcm_runtime *rtd;
	struct kas_priv_data *pdata;
	struct kas_pcm_data *pcm_data;
	int stream;

	for (stream = 0; stream < 2; stream++) {
		substream = pcm->streams[stream].substream;
		if (!substream)
			continue;
		rtd = substream->private_data;
		pdata = snd_soc_platform_get_drvdata(rtd->platform);
		pcm_data = &pdata->pcm[rtd->cpu_dai->id][substream->stream];
		dma_free_coherent(rtd->platform->dev,
				sizeof(struct endpoint_handle),
				pcm_data->sw_ep_handle,
				pcm_data->sw_ep_handle_phy_addr);
	}
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int kas_pcm_probe(struct snd_soc_platform *platform)
{
	struct kas_priv_data *priv_data;

	priv_data = devm_kzalloc(platform->dev, sizeof(*priv_data), GFP_KERNEL);
	if (priv_data == NULL)
		return -ENOMEM;

	snd_soc_platform_set_drvdata(platform, priv_data);
	priv_data->kcm = kcm_init(bt_usp_port, platform->dev, i2s_master);
	if (IS_ERR(priv_data->kcm))
		return PTR_ERR(priv_data->kcm);
	return 0;
}

static int kas_pcm_remove(struct snd_soc_platform *platform)
{
	kcm_deinit(platform->dev);
	return 0;
}

static struct snd_soc_platform_driver kas_soc_platform = {
	.probe = kas_pcm_probe,
	.remove = kas_pcm_remove,
	.ops = &kas_pcm_ops,
	.pcm_new = kas_pcm_new,
	.pcm_free = kas_pcm_free,
};

#define KAS_RATES		(SNDRV_PCM_RATE_CONTINUOUS | \
				SNDRV_PCM_RATE_8000_192000)
#define KAS_FORMATS		(SNDRV_PCM_FMTBIT_S16_LE | \
				SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_driver kas_dais[] = {
	{
		.name = "Music Pin",
		.playback = {
			.stream_name = "Music Playback",
			.channels_min = 1,
			.channels_max = 4,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "Navigation Pin",
		.playback = {
			.stream_name = "Navigation Playback",
			.channels_min = 1,
			.channels_max = 4,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "Alarm Pin",
		.playback = {
			.stream_name = "Alarm Playback",
			.channels_min = 1,
			.channels_max = 4,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "A2DP Pin",
		.playback = {
			.stream_name = "A2DP Playback",
			.channels_min = 1,
			.channels_max = 4,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "Voicecall-bt-to-iacc Pin",
		.playback = {
			.stream_name = "Voicecall-bt-to-iacc",
			.channels_min = 1,
			.channels_max = 4,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "Voicecall-playback Pin",
		.playback = {
			.stream_name = "Voicecall-playback",
			.channels_min = 1,
			.channels_max = 1,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "Iacc-loopback-playback Pin",
		.playback = {
			.stream_name = "Iacc-loopback-playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "I2S-to-iacc-loopback Pin",
		.playback = {
			.stream_name = "I2S-to-iacc-loopback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "Capture Pin",
		.capture = {
			.stream_name = "Analog Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "Voicecall-iacc-to-bt Pin",
		.capture = {
			.stream_name = "Voicecall-iacc-to-bt",
			.channels_min = 1,
			.channels_max = 1,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "Voicecall-capture Pin",
		.capture = {
			.stream_name = "Voicecall-capture",
			.channels_min = 1,
			.channels_max = 1,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "Iacc-loopback-capture Pin",
		.capture = {
			.stream_name = "Iacc-loopback-capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "USP0 Pin",
		.playback = {
			.stream_name = "USP0 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "USP1 Pin",
		.playback = {
			.stream_name = "USP1 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "USP2 Pin",
		.playback = {
			.stream_name = "USP2 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	}
};

static const struct snd_soc_dapm_widget widgets[] = {
	/* Backend DAIs  */
	SND_SOC_DAPM_AIF_IN("Codec IN", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("Codec OUT", NULL, 0, SND_SOC_NOPM, 0, 0),
	/* Global Playback Mixer */
	SND_SOC_DAPM_MIXER("Playback VMixer", SND_SOC_NOPM, 0, 0, NULL, 0),
};

static const struct snd_soc_dapm_route graph[] = {
	/* Playback Mixer */
	{"Playback VMixer", NULL, "Music Playback"},
	{"Playback VMixer", NULL, "Navigation Playback"},
	{"Playback VMixer", NULL, "Alarm Playback"},
	{"Playback VMixer", NULL, "A2DP Playback"},
	{"Playback VMixer", NULL, "Voicecall-bt-to-iacc"},
	{"Playback VMixer", NULL, "Voicecall-playback"},
	{"Playback VMixer", NULL, "Iacc-loopback-playback"},
	{"Playback VMixer", NULL, "I2S-to-iacc-loopback"},
	{"Codec OUT", NULL, "Playback VMixer"},
	{"Analog Capture", NULL, "Codec IN"},
	{"Voicecall-iacc-to-bt", NULL, "Codec IN"},
	{"Voicecall-capture", NULL, "Codec IN"},
	{"Iacc-loopback-capture", NULL, "Codec IN"},
	{"Playback VMixer", NULL, "USP0 Playback"},
	{"Playback VMixer", NULL, "USP1 Playback"},
	{"Playback VMixer", NULL, "USP2 Playback"},
};

static const struct snd_soc_component_driver kas_dai_component = {
	.name = "kas-dai",
	.controls = kas_controls,
	.num_controls = ARRAY_SIZE(kas_controls),
	.dapm_widgets = widgets,
	.num_dapm_widgets = ARRAY_SIZE(widgets),
	.dapm_routes = graph,
	.num_dapm_routes = ARRAY_SIZE(graph),
};

static int kas_pcm_dev_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *np = pdev->dev.of_node;

	if (of_get_property(np, "i2s-master", NULL))
		i2s_master = 1;

	of_property_read_u32(pdev->dev.of_node, "bt-usp-port", &bt_usp_port);

	ret = devm_snd_soc_register_platform(&pdev->dev, &kas_soc_platform);
	if (ret < 0)
		return ret;

	ret = devm_snd_soc_register_component(&pdev->dev, &kas_dai_component,
			kas_dais, ARRAY_SIZE(kas_dais));
	return ret;
}

static const struct of_device_id kas_pcm_of_match[] = {
	{ .compatible = "csr,kas-pcm", },
	{}
};
MODULE_DEVICE_TABLE(of, kas_pcm_of_match);

static struct platform_driver kas_pcm_driver = {
	.driver = {
		.name = "kas-pcm-audio",
		.owner = THIS_MODULE,
		.of_match_table = kas_pcm_of_match,
	},
	.probe = kas_pcm_dev_probe,
};
module_platform_driver(kas_pcm_driver);

MODULE_DESCRIPTION("SiRF Kalimba pcm audio driver");
