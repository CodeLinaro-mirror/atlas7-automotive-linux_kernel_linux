#ifndef _SIRFSOC_DMA_H_
#define _SIRFSOC_DMA_H_
/*
 * create a custom slave config struct for CSR SiRFprimaII and pass that,
 * and make dma_slave_config a member of that struct
 */
struct sirfsoc_dma_slave_config {
	struct dma_slave_config generic_config;

	/* CSR SiRFprimaII 2D-DMA config */
	int             xlen;           /* DMA xlen */
	int             ylen;           /* DMA ylen */
	int             width;          /* DMA width */
};

bool sirfsoc_dma_filter_id(struct dma_chan *chan, void *chan_id);

#endif
