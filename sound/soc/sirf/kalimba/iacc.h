#ifndef _KAS_IACC_H
#define _KAS_IACC_H

#include <linux/regmap.h>

enum iacc_input_path {
	NO_USED,
	MONO_DIFF,
	STEREO_SINGLE,
	STEREO_DIGITAL,
	STEREO_LINEIN,
	MONO_LINEIN
};

int iacc_setup(int pchannels, int rchannels,
	enum iacc_input_path path, u32 SampleRate, u32 format);
void iacc_start(int playback, int channels);
void iacc_stop(int playback);
void atlas7_codec_release(void);

#ifdef CONFIG_SND_SOC_SIRF_KALIMBA_DEBUG
void debug_setup_codec_regmap(struct regmap *regmap);
#endif

#endif /* _KAS_IACC_H */
