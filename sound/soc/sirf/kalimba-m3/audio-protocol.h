#ifndef __AUDIO_PROTOCOL
#define __AUDIO_PROTOCOL

int audio_protocol_init(void);
void kas_start_stream(u32 stream, u32 sample_rate, u32 channles, u32 buff_addr,
	u32 buff_size, u32 period_size);
void kas_stop_stream(u32 stream);

#endif
