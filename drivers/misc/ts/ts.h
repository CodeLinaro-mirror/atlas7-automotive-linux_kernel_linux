#ifndef __ATLAS7_TS_H__
#define __ATLAS7_TS_H__


/***********usp****************/

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

/***********vip****************/


#define CAM_CTRL			0x10
#define CAM_PIXEL_SHIFT		0x14
#define CAM_INT_EN			0x28
#define CAM_INT_CTRL		0x2C
#define CAM_DMA_CTRL		0x44
#define CAM_DMA_LEN			0x48
#define CAM_FIFO_CTRL_REG	0x4C
#define CAM_FIFO_LEVEL_CHECK	0x50
#define CAM_FIFO_OP_REG			0x54
#define CAM_FIFO_STATUS_REG		0x58

#define CAM_TS_CTRL			    0x60
#define CAM_PXCLK_CFG			0x68



#define CAM_PIXEL_SHIFT_0TO7		(1 << 0)
#define CAM_INT_EN_TS_OVER		    (1 << 3)
#define CAM_INT_EN_FIFO_UFLOW		(1 << 2)
#define CAM_INT_EN_FIFO_OFLOW		(1 << 1)

#define CAM_INT_CTRL_TS_OVER		(1 << 3)
#define CAM_INT_CTRL_FIFO_UFLOW		(1 << 2)
#define CAM_INT_CTRL_FIFO_OFLOW		(1 << 1)
#define CAM_INT_CTRL_SENSOR_INT		(1 << 0)

#define CAM_DMA_CTRL_ENDIAN_MODE_MASK	(0x3 << 4)
#define CAM_DMA_CTRL_ENDIAN_NO_CHG	(0 << 4)
#define CAM_DMA_CTRL_ENDIAN_BXDW	(1 << 4)
#define CAM_DMA_CTRL_ENDIAN_WXDW	(2 << 4)
#define CAM_DMA_CTRL_ENDIAN_BXW		(3 << 4)
#define CAM_DMA_CTRL_DMA_FLUSH		(1 << 2)
#define CAM_DMA_CTRL_DMA_OP		(0 << 0)
#define CAM_DMA_CTRL_IO_OP		(1 << 0)

#define CAM_FIFO_OP_FIFO_RESET		(1 << 1)
#define CAM_FIFO_OP_FIFO_START		(1 << 0)
#define CAM_FIFO_OP_FIFO_STOP		(0 << 0)

#define CAM_CTRL_INIT			(1 << 31)

#define CAM_TS_CTRL_INIT		(1 << 31)
#define CAM_TS_CTRL_BIG_ENDIAN		(1 << 7)
#define CAM_TS_CTRL_SINGLE		(1 << 6)
#define CAM_TS_CTRL_NEG_SAMPLE		(1 << 5)
#define CAM_TS_CTRL_VIP_TS		(1 << 4)

#define CAM_INT_CTRL_MASK_A7		(0x3F << 0)



#define CAM_PXCLK_CFG			0x68
#define CAM_INPUT_BIT_SEL_0		0x6C
#define CAM_INPUT_BIT_SEL_1		0x70
#define CAM_INPUT_BIT_SEL_2		0x74
#define CAM_INPUT_BIT_SEL_3		0x78
#define CAM_INPUT_BIT_SEL_4		0x7C
#define CAM_INPUT_BIT_SEL_5		0x80
#define CAM_INPUT_BIT_SEL_6		0x84
#define CAM_INPUT_BIT_SEL_7		0x88
#define CAM_INPUT_BIT_SEL_8		0x8C
#define CAM_INPUT_BIT_SEL_9		0x90
#define CAM_INPUT_BIT_SEL_10		0x94
#define CAM_INPUT_BIT_SEL_11		0x98
#define CAM_INPUT_BIT_SEL_12		0x9C
#define CAM_INPUT_BIT_SEL_13		0xA0
#define CAM_INPUT_BIT_SEL_14		0xA4
#define CAM_INPUT_BIT_SEL_15		0xA8
#define CAM_INPUT_BIT_SEL_HSYNC		0xAC
#define CAM_INPUT_BIT_SEL_VSYNC		0xB0

/* DMAC register */
#define DMAN_ADDR			0x400
#define DMAN_XLEN			0x404
#define DMAN_YLEN			0x408
#define DMAN_CTRL			0x40C
#define DMAN_CTRL_TABLE_NUM(x)		(((x) & 0xF) << 7)
#define DMAN_CTRL_CHAIN_EN		(1 << 3)
#define DMAN_WIDTH			0x410
#define DMAN_VALID			0x414
#define DMAN_INT			0x418
#define DMAN_FINI_INT			(1 << 0)
#define DMAN_CNT_INT			(1 << 1)
#define DMAN_INT_MASK			(0x7F << 0)
#define DMAN_INTMASK_FINI		(0x1 << 0)
#define DMAN_INTMASK_CNT		(0x1 << 1)
#define DMAN_INT_EN			0x41C
#define DMAN_LOOP_CTRL			0x420
#define DMAN_INT_CNT			0x424
#define DMAN_TIMEOUT_CNT		0x428
#define DMAN_PAU_TIME_CNT		0x42C
#define DMAN_CUR_TABLE_ADDR		0x430
#define DMAN_CUR_DATA_ADDR		0x434
#define DMAN_MUL			0x438
#define DMAN_STATE0			0x43C
#define DMAN_STATE1			0x440
#define DMAN_MATCH_ADDR1		0x448
#define DMAN_MATCH_ADDR2		0x44c
#define DMAN_MATCH_ADDR3		0x450
#define DMAN_MATCH_ADDR_EN		0x454




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

struct ts_dev;

struct ts_ops {
	long (*hw_start)(struct ts_dev *);
	long (*hw_stop)(struct ts_dev *);
	void (*hw_irq)(struct ts_dev *, int);
	void (*hw_dump_registers)(struct ts_dev *);
	unsigned int (*dma_get_pos)(struct ts_dev *);
	void (*dma_irq)(struct ts_dev *);
	int (*dma_setup)(struct ts_dev *, struct platform_device *);
	unsigned int (*dma_start)(struct ts_dev *);
	void (*dma_stop)(struct ts_dev *);
};

struct ts_portdata {
	unsigned int port_base;
	const struct ts_ops *ops;
};


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
	unsigned int	dma_irq;
	struct dma_chan	*rx_chan;
	struct dma_async_tx_descriptor *desc;
	dma_cookie_t	dma_cookie;
	unsigned int   dma_running_pos;
	unsigned int   dma_intr_num;
	dma_addr_t		buffer_addr_dma;
	unsigned int  *buffer_addr_cpu;
	unsigned int   buffer_size;
	spinlock_t		buffer_lock;

	const struct ts_ops *ops;
	bool            data_is_ready;
	bool            is_usp_port;
};

#define TS_IOC_MAGIC  'T'

#define IOCTL_START		    _IOR(TS_IOC_MAGIC,  1, int)
#define IOCTL_STOP		    _IOR(TS_IOC_MAGIC,  2, int)
#define IOCTL_REQ_BUFFER    _IOR(TS_IOC_MAGIC,  3, int)
#define IOCTL_GET_BUFFER_INFO	_IOR(TS_IOC_MAGIC,  4, struct ts_buffer_info *)
#define IOCTL_GET_PARAM		_IOR(TS_IOC_MAGIC,  5, struct ts_para *)
#define IOCTL_SET_PARAM		_IOR(TS_IOC_MAGIC,  6, struct ts_para *)



#endif
