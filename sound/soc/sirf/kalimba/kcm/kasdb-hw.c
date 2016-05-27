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

static const struct kasdb_codec codec[] = {
	{
		.name = __S("IACC-Codec"),
	},
	{
		.name = __S("I2S-CS42888"),
	},
};

/* Sink, Source */
static const struct kasdb_hw hw[] = {
	{
		.name = __S("so_i2s"),
		.is_sink = 0,
		.is_slave = 0,
		.max_channels = 2,
		.def_channels = 0,
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 0,
		.bytes_per_ch = 192,
	},
	{
		.name = __S("si_usp3"),
		.is_sink = 1,
		.is_slave = 0,
		.instance_id = ENDPOINT_PHY_DEV_A7CA,
		.max_channels = 4,
		.def_channels = 0,
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 0,
		.bytes_per_ch = 192,
	},
	{
		.name = __S("so_usp3"),
		.is_sink = 0,
		.is_slave = 0,
		.instance_id = ENDPOINT_PHY_DEV_A7CA,
		.max_channels = 4,
		.def_channels = 0,	/* Stream dependent */
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 0,
		.bytes_per_ch = 192,
	},
	{
		.name = __S("si_usp2"),
		.is_sink = 1,
		.is_slave = 0,
		.instance_id = ENDPOINT_PHY_DEV_A7CA,
		.max_channels = 4,
		.def_channels = 0,
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 0,
		.bytes_per_ch = 192,
	},
	{
		.name = __S("so_usp2"),
		.is_sink = 0,
		.is_slave = 0,
		.instance_id = ENDPOINT_PHY_DEV_PCM2,
		.max_channels = 4,
		.def_channels = 0,	/* Stream dependent */
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 0,
		.bytes_per_ch = 192,
	},
	{
		.name = __S("si_usp1"),
		.is_sink = 1,
		.is_slave = 0,
		.instance_id = ENDPOINT_PHY_DEV_A7CA,
		.max_channels = 4,
		.def_channels = 0,
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 0,
		.bytes_per_ch = 192,
	},
	{
		.name = __S("so_usp1"),
		.is_sink = 0,
		.is_slave = 0,
		.instance_id = ENDPOINT_PHY_DEV_PCM1,
		.max_channels = 4,
		.def_channels = 0,	/* Stream dependent */
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 0,
		.bytes_per_ch = 192,
	},
	{
		.name = __S("si_usp0"),
		.is_sink = 1,
		.is_slave = 0,
		.instance_id = ENDPOINT_PHY_DEV_A7CA,
		.max_channels = 4,
		.def_channels = 0,
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 0,
		.bytes_per_ch = 192,
	},
	{
		.name = __S("so_usp0"),
		.is_sink = 0,
		.is_slave = 0,
		.instance_id = ENDPOINT_PHY_DEV_PCM0,
		.max_channels = 4,
		.def_channels = 0,	/* Stream dependent */
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 0,
		.bytes_per_ch = 192,
	},
	{
		.name = __S("si_iacc"),
		.is_sink = 1,
		.is_slave = 0,
		.max_channels = 4,
		.def_channels = 4,
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 48000,
		.bytes_per_ch = 192,
	},
	{
		.name = __S("so_iacc"),
		.is_sink = 0,
		.is_slave = 0,
		.max_channels = 2,
		.def_channels = 0,		/* Stream dependent */
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 48000,
		.bytes_per_ch = 192,
	},
	{
		.name = __S("so_iacc_cvc"),
		.is_sink = 0,
		.is_slave = 0,
		.max_channels = 2,
		.def_channels = 2,		/* For two mic cvc */
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 48000,
		.bytes_per_ch = 192,
	},
};
