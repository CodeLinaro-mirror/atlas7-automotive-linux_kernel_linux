static const struct kasdb_chain chain[] = {
	{
		.name = __S("chain_music_2"),
		.trg_fe_name = __S("Music"),
		.trg_channels = 2,
		.links = __S("lk_music_pass_2;lk_pass_src_2;lk_src_split;"
				"lk_split_iacc"),
		.mutexs = __S(NULL),
	},
	{
		.name = __S("chain_music_4"),
		.trg_fe_name = __S("Music"),
		.trg_channels = 4,
		.links = __S("lk_music_pass_4;lk_pass_src_4;lk_src_iacc"),
		.mutexs = __S(NULL),
	},
	{
		.name = __S("chain_cap"),
		.trg_fe_name = __S("AnalogCapture"),
		.trg_channels = 1,
		.links = __S("lk_iacc_pass;lk_pass_cap"),
		.mutexs = __S(NULL),
	},
};
