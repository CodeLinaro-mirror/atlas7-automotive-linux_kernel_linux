/*
 * CSR hypervisor software FIFO support.
 *
 * Copyright (c) 2013 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#ifndef _LINUX_CSRVISOR_FIFO_H_
#define _LINUX_CSRVISOR_FIFO_H_

#define FIFO_SOFT_IRQ_NS	62
#define FIFO_SOFT_IRQ_S		63

#define SIRF_RPROC_RESOURCE_TABLE_BASE	0xD0200000
#define SIRF_RPROC_RESOURCE_TABLE_LEN	0x200000

#define SW_FIFO_IOMEM_BASE		0xD0400000
#define SW_FIFO_S_IOMEM_BASE		SW_FIFO_IOMEM_BASE
#define SW_FIFO_NS_IOMEM_BASE		(SW_FIFO_IOMEM_BASE + 0x400)

#define SW_FIFO_CHN0_TX_BASE		(SW_FIFO_IOMEM_BASE + 0x800)
#define SW_FIFO_CHN0_RX_BASE		(SW_FIFO_IOMEM_BASE + 0x800 * 2)
#define SW_FIFO_CHN1_TX_BASE		(SW_FIFO_IOMEM_BASE + 0x800 * 3)
#define SW_FIFO_CHN1_RX_BASE		(SW_FIFO_IOMEM_BASE + 0x800 * 4)

/* This register is used to control IRQ enable */
#define SW_FIFO_IRQ_CTRL_REG		0
/* This register is identify the IRQ number */
#define SW_FIFO_IRQ_NUM_REG		1
/* This register is identify the write FIFO Channel */
#define SW_FIFO_W_CHANNEL_REG		2
/* This register is identify the read FIFO Channel */
#define SW_FIFO_R_CHANNEL_REG		3
/* This register is identify the address of GIC
 * set pending register address */
#define SW_FIFO_SET_PENDING_ADDR_REG	4

/* Definition of SW FIFO IRQ STAT REG */
/* Normal status */
#define SW_FIFO_INTR_OK		0x00000000
/* there is no enough space for new data */
#define SW_FIFO_INTR_BUSY	0x00000001
/* there is enough space for new data */
#define SW_FIFO_INTR_AVAIL	0x00000002
/* there is some data has been placed at RX dma memory */
#define SW_FIFO_INTR_IN_DATA	0x00000004
/* data has been written to fifo successfully */
#define SW_FIFO_INTR_TX_DONE	0x00000008
/* data size is larger than FIFO's capacity */
#define SW_FIFO_INTR_BIG_SIZE	0x00000010
/* there is no data had been placed at RX dma memory */
#define SW_FIFO_INTR_EMPTY	0x00000020


/* Definition of SW FIFO's channel number and channel size */
#define CSRVISOR_FIFO_NUM_CHANNELS      2
#define CSRVISOR_FIFO_CHANNEL_SIZE      128

/* FIFO TX/RX DMA PAGE STRUCTURE
|---------------------------------------|
|      FIFO MSG HEADER(bytes,rw_bytes)  |
|---------------------------------------|
|      FIFO STATUS(rx/tx status)        |
|---------------------------------------|
|      FIFO MSG DATA (buffer)           |
|---------------------------------------|
*/
struct csrvisor_fifo_msg {
	unsigned int bytes;
	unsigned int rw_bytes;
	unsigned int rx_stat;
	unsigned int tx_stat;
	unsigned char buffer[4];
};

/* FIFO IOMEM Request DMA PAGE STRUCTURE
|---------------------------------------|
|         FIFO IOMEM OFFSET             |
|---------------------------------------|
|         FIFO IOMEM Write value        |
|---------------------------------------|
|         FIFO IOMEM Read value         |
|---------------------------------------|
*/
struct csrvisor_fifo_io_req {
	unsigned int offset;
	unsigned int w_value;
	unsigned int r_value;
};

#endif /* _LINUX_CSRVISOR_FIFO_H_ */
