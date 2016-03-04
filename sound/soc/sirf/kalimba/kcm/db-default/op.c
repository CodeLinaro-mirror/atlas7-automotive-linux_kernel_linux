static const struct kasdb_op op[] = {
	{
		/* Music passthrough */
		.name = __S("op_pass_music"),
		.ctrl_base = __S("Music pregain"),
		.ctrl_names = __S("Playback Volume"),
		.cap_id = CAPABILITY_ID_BASIC_PASSTHROUGH,
		.rate = 0,
		.param.dummy = 0,
	},
	{
		/* Music resampler */
		.name = __S("op_src_music"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(NULL),
		.cap_id = CAPABILITY_ID_RESAMPLER,
		.rate = 48000,
		.param.dummy = 0,
	},
	{
		/* Music splitter: 2 -> 4 */
		.name = __S("op_split_music"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(NULL),
		.cap_id = CAPABILITY_ID_SPLITTER,
		.rate = 0,
		.param.dummy = 0,
	},
	{
		/* Music splitter: 1 -> 2 (only used by mono stream) */
		.name = __S("op_split_music_1x2"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(NULL),
		.cap_id = CAPABILITY_ID_SPLITTER,
		.rate = 0,
		.param.dummy = 0,
	},
	{
		/* Capture passthrough */
		.name = __S("op_pass_cap"),
		.ctrl_base = __S("Analog Capture"),
		.ctrl_names = __S("Capture Volume;Mute"),
		.cap_id = CAPABILITY_ID_BASIC_PASSTHROUGH,
		.rate = 0,
		.param.dummy = 0,
	},
	{
		/* Mixer: music, navigation, alarm */
		.name = __S("op_mixer"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S("Music Stream Playback Volume;"
				"Navigation Stream Playback Volume;"
				"Alarm Stream Playback Volume;"
				"Music Stream Mute;"
				"Navigation Stream Mute;"
				"Alarm Stream Mute"),
		.cap_id = CAPABILITY_ID_MIXER,
		.rate = 48000,
		.param.mixer_streams = 3,
	},
	{
		/* Mixer: mixer1, voice */
		.name = __S("op_mixer2"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(NULL),
		.cap_id = CAPABILITY_ID_MIXER,
		.rate = 48000,
		.param.mixer_streams = 3,
	},
	{
		/* Alaram resampler */
		.name = __S("op_src_alarm"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(NULL),
		.cap_id = CAPABILITY_ID_RESAMPLER,
		.rate = 48000,
		.param.dummy = 0,
	},
	{
		/* Alarm splitter: 1 -> 2 */
		.name = __S("op_split_alarm_1x2"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(NULL),
		.cap_id = CAPABILITY_ID_SPLITTER,
		.rate = 0,
		.param.dummy = 0,
	},
	{
		/* Alarm splitter: 2 -> 4 */
		.name = __S("op_split_alarm"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(NULL),
		.cap_id = CAPABILITY_ID_SPLITTER,
		.rate = 0,
		.param.dummy = 0,
	},
};
