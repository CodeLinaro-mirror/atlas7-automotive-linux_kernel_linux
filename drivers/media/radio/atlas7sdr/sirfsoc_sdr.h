#ifndef _SIRFSOC_SDR_H_
#define _SIRFSOC_SDR_H_

#include <asm-generic/ioctl.h>

#define SDR_VSS_CONTROL_0	0x00
#define SDR_VSS_CONTROL_1	0x04
#define SDR_VSS_CONTROL_2	0x08
#define SDR_VSS_CONTROL_3	0x0C
#define SDR_VSS_VIT_STATUS	0x10
#define SDR_VSS_RESERVED	0x14
#define SDR_VSS_STATUS		0x18
#define SDR_VSS_DEBUG_RESET	0x1C

#define DMA_RD_BASE	(1 << 11)
#define DMA_WT_BASE	(2 << 11)

#define DMA_ADDR	0x0
#define DMA_XLEN	0x4
#define DMA_YLEN	0x8
#define DMA_CTRL	0xc
#define DMA_WIDTH	0x10
#define DMA_VALID	0x14
#define DMA_INT		0x18
#define DMA_INT_EN	0x1c
#define DMA_LOOP_CTRL	0x20
#define DMA_INT_CNT	0x24
#define DMA_TIMEOUT_CNT	0x28
#define DMA_PAU_TIME_CNT	0x2c
#define DMA_CUR_TABLE_ADDR	0x30
#define DMA_CUR_DATA_ADDR	0x34
#define DMA_STATE0		0x3c
#define DMA_STATE1		0x40

struct sdr_buf_info {
	int size;
	int property;
};

struct config_info {
	int dab_mode;
	int rd_dma_size;
	int wt_dma_size;
	int rd_dma_chain;
	int wt_dma_chain;
	int rd_dma_entry_cnt;
	int wt_dma_entry_cnt;
	unsigned int rd_dma_addr;
	unsigned int wt_dma_addr;
};

#define SDR_IOC_MAGIC  'V'

#define IOCTL_ALLOC_INPUT_BUFFER	_IOR(SDR_IOC_MAGIC,  1,\
		struct sdr_buf_info)
#define IOCTL_ALLOC_OUTPUT_BUFFER	_IOR(SDR_IOC_MAGIC,  2,\
		struct sdr_buf_info)
#define IOCTL_DECODER		_IOR(SDR_IOC_MAGIC,  3,\
		struct config_info)

#define IOCTL_FREE_INPUT_BUFFER		_IOR(SDR_IOC_MAGIC,  4, int)
#define IOCTL_FREE_OUTPUT_BUFFER	_IOR(SDR_IOC_MAGIC,  5, int)
#define IOCTL_SDR_RESET			_IOR(SDR_IOC_MAGIC,  6, int)

#endif

