static const struct kasdb_op op[] = {
	{
		.name = __S("op_pass_music"),
		.ctrl_base = __S("Music pregain"),
		.ctrl_names = __S("Playback Volume"),
		.cap_id = CAPABILITY_ID_BASIC_PASSTHROUGH,
		.pre_create = 0,
		.rate = 0,
		.param.dummy = 0,
	},
	{
		.name = __S("op_src_music"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(NULL),
		.cap_id = CAPABILITY_ID_RESAMPLER,
		.pre_create = 0,
		.rate = 48000,
		.param.dummy = 0,
	},
	{
		.name = __S("op_split_music"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(NULL),
		.cap_id = CAPABILITY_ID_SPLITTER,
		.pre_create = 0,
		.rate = 0,
		.param.dummy = 0,
	},
	{
		/* Only used if mono stream */
		.name = __S("op_split_music_1x2"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(NULL),
		.cap_id = CAPABILITY_ID_SPLITTER,
		.pre_create = 0,
		.rate = 0,
		.param.dummy = 0,
	},
	{
		.name = __S("op_pass_cap"),
		.ctrl_base = __S("Analog Capture"),
		.ctrl_names = __S("Capture Volume;Mute"),
		.cap_id = CAPABILITY_ID_BASIC_PASSTHROUGH,
		.pre_create = 0,
		.rate = 0,
		.param.dummy = 0,
	},
	{
		.name = __S("op_mixer"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S("Music Stream Playback Volume;"
				"Navigation Stream Playback Volume;"
				"Alarm Stream Playback Volume;"
				"Music Stream Mute;"
				"Navigation Stream Mute;"
				"Alarm Stream Mute"),
		.cap_id = CAPABILITY_ID_MIXER,
		.pre_create = 0,
		.rate = 48000,
		.param.mixer_streams = 3,
	},
};
