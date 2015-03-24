/*
 * SiRF I2S controllers define
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#ifndef _KAS_I2S_H
#define _KAS_I2S_H

void sirf_i2s_start(int playback, dma_addr_t dma_buff_addr,
		unsigned long buff_size);
void sirf_i2s_stop(int playback);
void sirf_i2s_params(int channels, int rate, int slave);

#endif /*_KAS_I2S_H*/
