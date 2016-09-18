/*
 * Copyright (c) [2016] The Linux Foundation. All rights reserved.
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

#ifndef _KCM_KASDB_H
#define _KCM_KASDB_H

#include <sound/soc.h>

struct kasdb_head {
#define KASDB_MAGIC	0xFACE
#define	KASDB_VERSION	0x0101	/* To match user and kernel code */
	short magic;
	short version;
	int elements;
	int reloc_off;		/* Offset of relocation table */
	int cksum;
	char data[0];
};

/* Relocation table to change string offset to pointer */
struct kasdb_reloc {
	int cnt;
	int offset[];
};

/* In user db, all string pointers are replaced with offsets to db header,
 * and recovered to pointer in kernel. This introduce unnecessary difficulty
 * to default config, which is setup in kernel. So str is defined as a union.
 */
union kasdb_str {
	int offset;
#ifdef __KERNEL__
	char *s;
#ifdef CONFIG_64BIT
#error "Declare offset as long long!"
#endif
#endif
};

/* Each element is appended by one kasdb_elm structure
 * XXX: must start from 0 and be continuous, see kasobj_add() in kasobj.c
 */
enum {
	kasdb_elm_codec,
	kasdb_elm_hw,
	kasdb_elm_fe,
	kasdb_elm_op,
	kasdb_elm_link,
	kasdb_elm_chain,
	kasdb_elm_max,
};

struct kasdb_elm {
#define KASDB_ELM_MAGIC	0x5A
	char magic;
	char type;
	unsigned short size;
	char data[0];
};

/* Sample rates are represented in kernel by bits. sound/core/pcm_native.c
 * contains according code. Similar behaviour is copied here for user mode
 * script parser.
 */
#ifndef __KERNEL__
static inline int kasdb_rate_alsa(int rate)
{
	int i, rates[] = { 5512, 8000, 11025, 16000, 22050, 32000, 44100, 48000,
		64000, 88200, 96000, 176400, 192000 };

	for (i = 0; i < sizeof(rates)/sizeof(rates[0]); i++)
		if (rate == rates[i])
			return 1 << i;
	return 0;
}
#endif

struct kasdb_codec {
	union kasdb_str name;	/* iacc, i2s */
	union kasdb_str chip_name;/* Description of the codec */
	union kasdb_str dai_name; /* Soc dai driver name */
	char enable;		/* 0: disable, 1: enable */
	int rate;		/* 48000, 96000 ... */
	char playback;		/* 1: support playback, 0: not support */
	char capture;		/* 1: support capture, 0: not support */
	char codec_widget_num;	/* codec widget number, maximum: 2 */
	char card_widget_num;	/* card widget number, maximum: 8 */
	char route_num;		/* Route number, maximum: 16 */
	struct snd_soc_dapm_widget codec_widget[2];
	struct snd_soc_dapm_widget card_widget[8];
	struct snd_soc_dapm_route route[16];
};

/* Value must be consistent with Kalimba definition */
enum {
	kasdb_pack_24r,		/* 32-bit right aligned */
	kasdb_pack_24l,		/* 32-bit left aligned */
	kasdb_pack_16,		/* 16-bit */
	kasdb_pack_24,		/* 24-bit */
};

struct kasdb_hw {
	union kasdb_str name;	/* iacc, usp, i2s */
	char is_sink;		/* 1 - playback, 0 - capture */
	char is_slave;		/* 1 - slave, 0 - master */
	char instance_id;	/* Only for USP */
	char max_channels;	/* Max channels */
	char def_channels;	/* Default channels */
	char audio_format;	/* XXX: Where's the definition? */
	char pack_format;	/* kasdb_pack_24r, ..., kasdb_pack_16, ... */
	int def_rate;		/* 48000, 96000, ..., 0 - stream dependent */
	int bytes_per_ch;	/* Bytes per channel */
};

/* Only supports one stream */
struct kasdb_fe {
	union kasdb_str name;	/* Card name */
	short playback;		/* 1 - playback, 0 - capture */
	short internal;		/* 1 - Internal loopback */
	short flow_ctrl;	/* 1 - enable, 0 - disable */
	union kasdb_str stream_name;
	short channels_min;
	short channels_max;
	int rates;		/* kasdb_rate_alsa(48000) | ... */
	int formats;		/* SND_PCM_FORMAT_S16_LE | ... */
	union kasdb_str sink_codec;
	union kasdb_str source_codec;
};

struct kasdb_op {
#define KASDB_SRCSYNC_CH_MAX	24
	union kasdb_str name;
	union kasdb_str ctrl_base;	/* Base control name */
	union kasdb_str ctrl_names;	/* Control names, separated by ":" */
	short cap_id;
	int rate;	/* Most operators need configure sample rate */
	union {
		int dummy;
		int resampler_custom_output;	/* 1: capture, 0: playback */
		int mixer_streams;	/* 2, 3 */
		int chmixer_io;		/* number of input/output channels */
		int delay_channels;
		int bass_pair_idx;	/* 0: default use, 1~11: user use*/
		struct {
			/* number of channels for each stream */
			char stream_ch[KASDB_SRCSYNC_CH_MAX];
			/* the output pin will be connected to each input pin */
			char input_map[KASDB_SRCSYNC_CH_MAX];
		} srcsync_cfg;
	} param;	/* Operator specific parameter */
};

struct kasdb_link {
#define KASDB_CH_MAX	8
	union kasdb_str name;
	union kasdb_str source_name;
	union kasdb_str sink_name;
	char source_pins[KASDB_CH_MAX];	/* Pin number start from 1 */
	char sink_pins[KASDB_CH_MAX];	/* " */
	int channels;
};

struct kasdb_chain {
	union kasdb_str name;
	union kasdb_str trg_fe_name;	/* Trigger by which FE */
	short trg_channels;		/* Trigger by how many channels */
	enum {ignore, single, doub} cvc_mic; /* Distinguish CVC streams */
	union kasdb_str links;		/* "link1:link2:xxx" */
	union kasdb_str mutexs;		/* Exclusive chains "music-4:music-6" */
};

void kasdb_load_database(void);

#endif
