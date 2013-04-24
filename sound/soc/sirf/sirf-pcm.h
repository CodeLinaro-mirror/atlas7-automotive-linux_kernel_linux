/*
 * SiRD pcm dma data struct
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __SIRF_PCM_H__
#define __SIRF_PCM_H__

struct sirf_pcm_dma_data {
	char	*name;		/* Stream name */
	int		dma_req;	/* DMA request line */
};

#endif
