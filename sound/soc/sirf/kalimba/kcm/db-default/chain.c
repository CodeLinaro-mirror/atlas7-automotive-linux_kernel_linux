static const struct kasdb_chain chain[] = {
	/* Music */
	{
		.name = __S("chain_music_1"),
		.trg_fe_name = __S("Music"),
		.trg_channels = 1,
		.links = __S("lk_music_pass_1;lk_pass_src_1;lk_src_split1x2;"
				"lk_split1x2_split;lk_music_mixer;"
				"lk_mixer_mixer2;lk_mixer2_iacc"),
		.mutexs = __S("chain_lin_to_lout_1;chain_lin_to_lout_2"),
	},
	{
		.name = __S("chain_music_2"),
		.trg_fe_name = __S("Music"),
		.trg_channels = 2,
		.links = __S("lk_music_pass_2;lk_pass_src_2;lk_src_split;"
				"lk_music_mixer;lk_mixer_mixer2;"
				"lk_mixer2_iacc"),
		.mutexs = __S("chain_lin_to_lout_1;chain_lin_to_lout_2"),
	},
	{
		.name = __S("chain_music_4"),
		.trg_fe_name = __S("Music"),
		.trg_channels = 4,
		.links = __S("lk_music_pass_4;lk_pass_src_4;lk_src_mixer;"
				"lk_mixer_mixer2;lk_mixer2_iacc"),
		.mutexs = __S("chain_lin_to_lout_1;chain_lin_to_lout_2"),
	},

	/* Navigation */
	{
		.name = __S("chain_navi"),
		.trg_fe_name = __S("Navigation"),
		.trg_channels = 4,
		.links = __S("lk_navi_mixer;lk_mixer_mixer2;lk_mixer2_iacc"),
		.mutexs = __S(NULL),
	},

	/* Alarm */
	{
		.name = __S("chain_alarm"),
		.trg_fe_name = __S("Alarm"),
		.trg_channels = 1,
		.links = __S("lk_alarm_src;lk_alarm_split_1x2;lk_alarm_split;"
				"lk_alarm_mixer;lk_mixer_mixer2;"
				"lk_mixer2_iacc"),
		.mutexs = __S(NULL),
	},

	/* Microphone */
	{
		.name = __S("chain_cap"),
		.trg_fe_name = __S("AnalogCapture"),
		.trg_channels = 1,
		.links = __S("lk_iacc_pass;lk_pass_cap"),
		.mutexs = __S("chain_lin_to_lout_1;chain_lin_to_lout_2"),
	},

	/* IACC Line-In to Line-Out */
	{
		.name = __S("chain_lin_to_lout_1"),
		.trg_fe_name = __S("IACC-loopback-playback"),
		.trg_channels = 1,
		.links = __S("lk_lin_pass_1;lk_pass_src_1;lk_src_split1x2;"
				"lk_split1x2_split;lk_music_mixer;"
				"lk_mixer_mixer2;lk_mixer2_iacc"),
		.mutexs = __S("chain_music_1;chain_music_2;chain_music_4;"
				"chain_cap"),
	},
	{
		.name = __S("chain_lin_to_lout_2"),
		.trg_fe_name = __S("IACC-loopback-playback"),
		.trg_channels = 2,
		.links = __S("lk_lin_pass_2;lk_pass_src_2;lk_src_split;"
				"lk_music_mixer;lk_mixer_mixer2;"
				"lk_mixer2_iacc"),
		.mutexs = __S("chain_music_1;chain_music_2;chain_music_4;"
				"chain_cap"),
	},
	{
		/* Only to trigger codec working */
		.name = __S("chain_lin_to_lout_dummy"),
		.trg_fe_name = __S("IACC-loopback-capture"),
		.trg_channels = 0,	/* Any channels */
		.links = __S(NULL),	/* No links */
		.mutexs = __S("chain_music_1;chain_music_2;chain_music_4;"
				"chain_cap"),
	},
};
