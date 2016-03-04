static const struct kasdb_link link[] = {
	/* Music, 1ch */
	{
		/* Music -> Passthrough, 1ch */
		.name = __S("lk_music_pass_1"),
		.source_name = __S("Music"),
		.sink_name = __S("op_pass_music"),
		.source_pins_mask = 0x1,
		.sink_pins_mask = 0x1,
		.channels = 1,
	},
	{
		/* Passthrough -> Resampler, 1ch */
		.name = __S("lk_pass_src_1"),
		.source_name = __S("op_pass_music"),
		.sink_name = __S("op_src_music"),
		.source_pins_mask = 0x1,
		.sink_pins_mask = 0x1,
		.channels = 1,
	},
	{
		/* Resampler -> Splitter1x2, 1ch */
		.name = __S("lk_src_split1x2"),
		.source_name = __S("op_src_music"),
		.sink_name = __S("op_split_music_1x2"),
		.source_pins_mask = 0x1,
		.sink_pins_mask = 0x1,
		.channels = 1,
	},
	{
		/* Splitter1x2 - > Splitter, 2ch */
		.name = __S("lk_split1x2_split"),
		.source_name = __S("op_split_music_1x2"),
		.sink_name = __S("op_split_music"),
		.source_pins_mask = 0x3,
		.sink_pins_mask = 0x3,
		.channels = 2,
	},
	{
		/* Splitter -> Mixer1, 4ch (1-4) */
		.name = __S("lk_music_mixer"),
		.source_name = __S("op_split_music"),
		.sink_name = __S("op_mixer"),
		.source_pins_mask = 0xF,
		.sink_pins_mask = 0xF,
		.channels = 4,
	},

	/* Music, 2ch */
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

	/* Music, 4ch */
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
		/* Resampler -> Mixer1, 4ch */
		.name = __S("lk_src_mixer"),
		.source_name = __S("op_src_music"),
		.sink_name = __S("op_mixer"),
		.source_pins_mask = 0xF,
		.sink_pins_mask = 0xF,
		.channels = 4,
	},

	/* Navigation, 4ch */
	{
		/* Navigation -> Mixer1, 4ch (5-8) */
		.name = __S("lk_navi_mixer"),
		.source_name = __S("Navigation"),
		.sink_name = __S("op_mixer"),
		.source_pins_mask = 0xF,
		.sink_pins_mask = 0xF0,
		.channels = 4,
	},

	/* Alarm, 1ch */
	{
		/* Alarm -> Resampler, 1ch */
		.name = __S("lk_alarm_src"),
		.source_name = __S("Alarm"),
		.sink_name = __S("op_src_alarm"),
		.source_pins_mask = 0x1,
		.sink_pins_mask = 0x1,
		.channels = 1,
	},
	{
		/* Resampler -> Splitter_1x2, 1ch */
		.name = __S("lk_alarm_split_1x2"),
		.source_name = __S("op_src_alarm"),
		.sink_name = __S("op_split_alarm_1x2"),
		.source_pins_mask = 0x1,
		.sink_pins_mask = 0x1,
		.channels = 1,
	},
	{
		/* Splitter_1x2 -> Splitter_2x4, 2ch */
		.name = __S("lk_alarm_split"),
		.source_name = __S("op_split_alarm_1x2"),
		.sink_name = __S("op_split_alarm"),
		.source_pins_mask = 0x3,
		.sink_pins_mask = 0x3,
		.channels = 2,
	},
	{
		/* Splitter -> Mixer1, 4ch (9-12) */
		.name = __S("lk_alarm_mixer"),
		.source_name = __S("op_split_alarm"),
		.sink_name = __S("op_mixer"),
		.source_pins_mask = 0xF,
		.sink_pins_mask = 0xF00,
		.channels = 4,
	},

	/* Mixer -> IACC, 4ch */
	{
		/* Mixer1 -> Mixer2, 4ch */
		.name = __S("lk_mixer_mixer2"),
		.source_name = __S("op_mixer"),
		.sink_name = __S("op_mixer2"),
		.source_pins_mask = 0xF,
		.sink_pins_mask = 0xF,
		.channels = 4,
	},
	{
		/* Mixer2 -> IACC, 4ch */
		.name = __S("lk_mixer2_iacc"),
		.source_name = __S("op_mixer2"),
		.sink_name = __S("si_iacc"),
		.source_pins_mask = 0xF,
		.sink_pins_mask = 0xF,
		.channels = 4,
	},

	/* Microphone: Analog Capture */
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

	/* Line-In */
	{
		/* Line-In -> Passthrough, 1ch */
		.name = __S("lk_lin_pass_1"),
		.source_name = __S("so_iacc"),
		.sink_name = __S("op_pass_music"),
		.source_pins_mask = 0x1,
		.sink_pins_mask = 0x1,
		.channels = 1,
	},
	{
		/* Line-In -> Passthrough, 2ch */
		.name = __S("lk_lin_pass_2"),
		.source_name = __S("so_iacc"),
		.sink_name = __S("op_pass_music"),
		.source_pins_mask = 0x3,
		.sink_pins_mask = 0x3,
		.channels = 2,
	},
};
