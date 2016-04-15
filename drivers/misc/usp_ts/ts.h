#ifndef __ATLAS7_HSI2S_H__
#define __ATLAS7_HSI2S_H__


/* USP Registers */
#define USP_MODE1			0x00
#define USP_MODE2			0x04
#define USP_TX_FRAME_CTRL	0x08
#define USP_RX_FRAME_CTRL	0x0C
#define USP_TX_RX_ENABLE	0x10
#define USP_INT_ENABLE_SET	0x14
#define USP_INT_STATUS		0x18
#define USP_PIN_IO_DATA		0x1C
#define USP_RISC_DSP_MODE	0x20
#define USP_IRDA_X_MODE_DIV	0x28
#define USP_SM_CFG			0x2C
#define USP_TRX_LEN_HI		0x30

#define USP_TX_DMA_IO_CTRL	0x100
#define USP_TX_DMA_IO_LEN	0x104
#define USP_TX_FIFO_CTRL	0x108
#define USP_TX_FIFO_LEVEL_CHK	0x10C
#define USP_TX_FIFO_OP		0x110
#define USP_TX_FIFO_STATUS	0x114
#define USP_TX_FIFO_DATA	0x118
#define USP_RX_DMA_IO_CTRL	0x120
#define USP_RX_DMA_IO_LEN	0x124
#define USP_RX_FIFO_CTRL	0x128
#define USP_RX_FIFO_LEVEL_CHK	0x12C
#define USP_RX_FIFO_OP		0x130
#define USP_RX_FIFO_STATUS	0x134
#define USP_RX_FIFO_DATA	0x138

#define USP_INT_ENABLE_CLR	0x140

#define USP_RX_TSIF_ERR              (1<<16)
#define USP_RX_TSIF_PROTOCOL_ERR     (1<<17)
#define USP_RX_TSIF_SYNC_BYTE_ERR    (1<<18)



#define USP_RX_FIFO_RESET		0x00000001
#define USP_RX_FIFO_START		0x00000002


#define USP_RX_ENA		0x00000001

#define USP_FRAME_CTRL_MODE			(1<<17)
#define USP_TFS_CLK_SLAVE_MODE		(1<<20)
#define USP_RFS_CLK_SLAVE_MODE		(1<<19)
#define USP_RXD_DELAY_LEN_OFFSET	0

#define USP_SYNC_MODE				0x00000001
#define USP_CLOCK_MODE_SLAVE		0x00000002
#define USP_EN						0x00000020
#define USP_RXD_ACT_EDGE_FALLING	0x00000040
#define USP_RFS_ACT_LEVEL_LOGIC1	0x00000100
#define USP_TFS_ACT_LEVEL_LOGIC1	0x00000200
#define USP_SCLK_IDLE_MODE_TOGGLE	0x00000400
#define USP_SCLK_IDLE_LEVEL_LOGIC1	0x00000800

#define USP_TFS_PIN_MODE_IO	        0x00004000
#define USP_TXD_PIN_MODE_IO	        0x00010000
#define USP_TFS_IO_MODE_INPUT	    0x00080000
#define USP_TXD_IO_MODE_INPUT	    0x00200000

#define USP_RXD_IO_MODE_INPUT	    0x00100000
#define USP_RXD_PIN_MODE_IO	        0x00008000

#define USP_SCLK_IO_MODE_INPUT	0x00020000
#define USP_SCLK_PIN_MODE_IO	0x00001000


#define USP_TSIF_VALID_MODE     (1<<14)
#define USP_TSIF_SYNC_BYTE      (0x47 << 6)
#define USP_TSIF_EN             0x00000010

#define USP_RX_ENDIAN_MODE		0x00000020


#define USP_RXC_DATA_LEN_MASK		0x000000FF
#define USP_RXC_DATA_LEN_OFFSET		0

#define USP_RXC_FRAME_LEN_MASK		0x0000FF00
#define USP_RXC_FRAME_LEN_OFFSET	8

#define USP_RXC_SHIFTER_LEN_MASK	0x001F0000
#define USP_RXC_SHIFTER_LEN_OFFSET	16

#define USP_START_EDGE_MODE	0x00800000
#define USP_I2S_SYNC_CHG	0x00200000

#define USP_RXC_CLK_DIVISOR_MASK	0x0F000000
#define USP_RXC_CLK_DIVISOR_OFFSET	24
#define USP_SINGLE_SYNC_MODE		0x00400000



#define USP_RFS_PIN_MODE_IO	0x00002000
#define USP_TFS_PIN_MODE_IO	0x00004000
#define USP_RXD_PIN_MODE_IO	0x00008000
#define USP_TXD_PIN_MODE_IO	0x00010000
#define USP_SCLK_IO_MODE_INPUT	0x00020000
#define USP_RFS_IO_MODE_INPUT	0x00040000
#define USP_TFS_IO_MODE_INPUT	0x00080000
#define USP_RXD_IO_MODE_INPUT	0x00100000
#define USP_TXD_IO_MODE_INPUT	0x00200000

#define USP_RX_FIFO_WIDTH_OFFSET	0
#define USP_RX_FIFO_THD_OFFSET		2

#define USP_RX_FIFO_SC_OFFSET	0
#define USP_RX_FIFO_LC_OFFSET	10
#define USP_RX_FIFO_HC_OFFSET	20

#define RX_FIFO_SC(x)		((x) << USP_RX_FIFO_SC_OFFSET)
#define RX_FIFO_LC(x)		((x) << USP_RX_FIFO_LC_OFFSET)
#define RX_FIFO_HC(x)		((x) << USP_RX_FIFO_HC_OFFSET)

#define RX_DATA_LEN_L(len)  (((len - 1)&0xFF) << USP_RXC_DATA_LEN_OFFSET)
#define RX_FRAME_LEN_L(len) (((len - 1)&0xFF) << USP_RXC_FRAME_LEN_OFFSET)
#define RX_SHIFT_LEN_L(len) (((len - 1)&0x1F) << USP_RXC_SHIFTER_LEN_OFFSET)

#define RX_DATA_LEN_H(len)	(((len - 1)>>8) << 16)
#define RX_FRAME_LEN_H(len)	(((len - 1)>>8) << 24)

#define UPDATE_FLAGS_INTR  0x01
#define UPDATE_FLAGS_POS   0x02



struct ts_buffer_info {
	unsigned int buffer_size;
	unsigned int running_pos;
	unsigned int seq_num;
};

struct ts_para {
u32 frame_length;
};


#define TS_FLAG_OPEN   0x01
#define TS_FLAG_START  0x02
#define TS_FLAG_TIMER  0x04
#define TS_FLAG_DMA    0x08


struct ts_dev {
	struct device *dev;
	struct miscdevice	misc_dev;
	wait_queue_head_t wait_read;
	struct clk		*clk;
	void __iomem	*regbase;
	u32			    dev_num;
	u32             frame_len;
	unsigned long	device_flags;
	unsigned int	irq;


	struct dma_chan	*rx_chan;
	struct dma_async_tx_descriptor *desc;
	dma_cookie_t	dma_cookie;
	unsigned int   dma_running_pos;
	unsigned int   dma_intr_num;
	dma_addr_t		buffer_addr_dma;
	unsigned int  *buffer_addr_cpu;
	unsigned int   buffer_size;
	spinlock_t		buffer_lock;

	bool            data_is_ready;
};

#define TS_IOC_MAGIC  'T'

#define IOCTL_START		    _IOR(TS_IOC_MAGIC,  1, int)
#define IOCTL_STOP		    _IOR(TS_IOC_MAGIC,  2, int)
#define IOCTL_REQ_BUFFER    _IOR(TS_IOC_MAGIC,  3, int)
#define IOCTL_GET_BUFFER_INFO	_IOR(TS_IOC_MAGIC,  4, struct ts_buffer_info *)
#define IOCTL_GET_PARAM		_IOR(TS_IOC_MAGIC,  5, struct ts_para *)
#define IOCTL_SET_PARAM		_IOR(TS_IOC_MAGIC,  6, struct ts_para *)



#endif
