static const struct kasdb_link link[] = {
	{
		/* Music -> Passthrough, 2ch */
		.name = __S("lk_music_pass_2"),
		.source_name = __S("Music"),
		.sink_name = __S("op_pass_music"),
		.source_pins_mask = 0x3,
		.sink_pins_mask = 0x3,
		.channels = 2,
	},
	{
		/* Passthrough -> Resampler, 2ch */
		.name = __S("lk_pass_src_2"),
		.source_name = __S("op_pass_music"),
		.sink_name = __S("op_src_music"),
		.source_pins_mask = 0x3,
		.sink_pins_mask = 0x3,
		.channels = 2,
	},
	{
		/* Resampler -> Splitter, 2ch */
		.name = __S("lk_src_split"),
		.source_name = __S("op_src_music"),
		.sink_name = __S("op_split_music"),
		.source_pins_mask = 0x3,
		.sink_pins_mask = 0x3,
		.channels = 2,
	},
	{
		/* Splitter -> Mixer, 4ch */
		.name = __S("lk_split_mixer"),
		.source_name = __S("op_split_music"),
		.sink_name = __S("op_mixer"),
		.source_pins_mask = 0xF,
		.sink_pins_mask = 0xF,
		.channels = 4,
	},

	{
		/* Music -> Passthrough, 4ch */
		.name = __S("lk_music_pass_4"),
		.source_name = __S("Music"),
		.sink_name = __S("op_pass_music"),
		.source_pins_mask = 0xF,
		.sink_pins_mask = 0xF,
		.channels = 4,
	},
	{
		/* Passthrough -> Resampler, 4ch */
		.name = __S("lk_pass_src_4"),
		.source_name = __S("op_pass_music"),
		.sink_name = __S("op_src_music"),
		.source_pins_mask = 0xF,
		.sink_pins_mask = 0xF,
		.channels = 4,
	},
	{
		/* Resampler -> Mixer, 4ch */
		.name = __S("lk_src_mixer"),
		.source_name = __S("op_src_music"),
		.sink_name = __S("op_mixer"),
		.source_pins_mask = 0xF,
		.sink_pins_mask = 0xF,
		.channels = 4,
	},

	{
		/* IACC -> Passthrough */
		.name = __S("lk_iacc_pass"),
		.source_name = __S("so_iacc"),
		.sink_name = __S("op_pass_cap"),
		.source_pins_mask = 0x1,
		.sink_pins_mask = 0x1,
		.channels = 1,
	},
	{
		/* Passthrough -> Analog Capture */
		.name = __S("lk_pass_cap"),
		.source_name = __S("op_pass_cap"),
		.sink_name = __S("AnalogCapture"),
		.source_pins_mask = 0x1,
		.sink_pins_mask = 0x1,
		.channels = 1,
	},

	{
		/* Navigation -> Mixer, 4ch */
		.name = __S("lk_navi_mixer"),
		.source_name = __S("Navigation"),
		.sink_name = __S("op_mixer"),
		.source_pins_mask = 0xF,
		.sink_pins_mask = 0xF0,
		.channels = 4,
	},

	{
		/* Mixer -> IACC, 4ch */
		.name = __S("lk_mixer_iacc"),
		.source_name = __S("op_mixer"),
		.sink_name = __S("si_iacc"),
		.source_pins_mask = 0xF,
		.sink_pins_mask = 0xF,
		.channels = 4,
	},
};
