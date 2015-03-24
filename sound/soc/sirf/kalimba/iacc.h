#ifndef _KAS_IACC_H
#define _KAS_IACC_H

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
void iacc_start(int playback, int channels, dma_addr_t dma_buff_addr,
		unsigned long buff_size);
void iacc_stop(int playback);
void atlas7_codec_release(void);
#endif /* _KAS_IACC_H */
