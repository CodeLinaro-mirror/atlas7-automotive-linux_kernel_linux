/*
 * Temporary workaround to initialize DMAC by ARM.
 * TODO: Remove this file after Kalimba takes over the job.
 */
#ifndef _KAS_DMA_HACK
#define _KAS_DMA_HACK

#define DMAC_IACC	3
#define DMAC_I2S	3

#define CH_IACC_RX	0
#define CH_IACC_TX0	7
#define CH_IACC_TX1	8
#define CH_IACC_TX2	3
#define CH_IACC_TX3	9

#define CH_I2S_RX	1
#define CH_I2S_TX	2

#define MEM_TO_DEV	1
#define DEV_TO_MEM	0

#define DMA_BURST	1
#define DMA_SINGLE	0

void __dmac_enable(int dmac, int ch, u32 addr, int buf_len, int dir, int burst);
void __dmac_disable(int dmac, int ch);

#endif
