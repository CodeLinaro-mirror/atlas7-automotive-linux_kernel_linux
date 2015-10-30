#ifndef __ATLAS7_HSI2S_H__
#define __ATLAS7_HSI2S_H__

#define I2S_DMA_BUF_SIZE  (1024*1024)

#define I2S_CTRL		0x0
#define I2S_RXFIFO_OP		0x4
#define I2S_RXFIFO_LEV_CHK	0x8
#define I2S_RXFIFO_STS		0xC
#define I2S_RXFIFO_INT		0x10
#define I2S_RXFIFO_INT_MSK	0x14

/* I2S_CTRL */
#define RX0_EN		0x1
#define RX1_EN		(0x1 << 1)
#define FRAME_SYNC_SFT		2
#define FRAME_POLARITY_SFT	4
#define WORD_ALIGN_SFT		5
#define DATA_ALIGN_SFT		7
#define SAMPLE_SIZE_SFT		8
#define WORD_SIZE_SFT		15
#define BURST_MODE_SFT		23

/* HS_I2S_RXFIFO_OP */
#define FIFO_RESET		0x2
#define FIFO_START		0x1

/* bit clock mode */
enum {
	BCLK_MODE_CONTINUOUS = 0,
	BCLK_MODE_BURST,
	BCLK_MODE_LAST = BCLK_MODE_BURST,
	BCLK_MODE_MAX = BCLK_MODE_LAST + 1,
};

/* data_align data MSB in word bit */
enum {
	DATA_ALIGN_LEFT = 0,
	DATA_ALIGN_I2S,
	DATA_ALIGN_LAST  = DATA_ALIGN_I2S,
	DATA_ALIGN_MAX = DATA_ALIGN_LAST + 1,
};

/* word_align first word bit after sync */
enum {
	WORD_ALIGN_LEFT = 0,
	WORD_ALIGN_I2S0,
	WORD_ALIGN_I2S1,
	WORD_ALIGN_LAST = WORD_ALIGN_I2S1,
	WORD_ALIGN_MAX = WORD_ALIGN_LAST + 1,
};

/* frame polarity */
enum {
	FRAME_POLARITY_RAISING = 0,
	FRAME_POLARITY_FAILING,
	FRAME_POLARITY_LAST = FRAME_POLARITY_FAILING,
	FRAME_POLARITY_MAX = FRAME_POLARITY_LAST + 1,
};

/*frame sync(WS) I2S or DSP mode */
enum {
	FRAME_SYNC_I2S = 0,
	FRAME_SYNC_DSP0,
	FRAME_SYNC_DSP1,
	FRAME_SYNC_LAST = FRAME_SYNC_DSP1,
	FRAME_SYNC_MAX = FRAME_SYNC_LAST + 1,
};

/* rx mode 1 (MUX) or 2 (SPLIT) received pin */
enum {
	RXMODE_MUX = 0,
	RXMODE_SPLIT,
	RXMODE_LAST = RXMODE_SPLIT,
	RXMODE_MAX = RXMODE_LAST + 1,
};

struct i2s_ctrl_t {
	union {
		unsigned long val;
		struct {
			unsigned long rx_mode : 2;
			unsigned long frame_sync : 2;
			unsigned long frame_polarity : 1;
			unsigned long word_align : 2;
			unsigned long data_align : 1;
			unsigned long sample_size : 7;
			unsigned long word_size : 8;
			unsigned long bclk_mode : 1;
			unsigned long adc_ch : 2;
			unsigned long rx_act_edge: 1;
			unsigned long reserve : 4;
			unsigned long reset : 1;
		};
	};
};

struct atlas7_hsi2s {
	struct miscdevice	misc_i2s;
	int			dev_num;
	struct clk		*clk;
	void __iomem		*regbase;
	struct dma_chan		*rx_chan;
	struct dma_async_tx_descriptor *desc;
	dma_cookie_t		dma_cookie;
	struct platform_device	*pdev;
	dma_addr_t		rx_dma_addr;
	unsigned long		*virt_dma_addr;
	struct hrtimer		hrt;
	u32			timer_interval;
	u32			in;
	u32			last_pos;
	struct i2s_ctrl_t	i2s_ctrl;
};

#define SDR_IOC_MAGIC  'S'

#define IOCTL_GET_BUF_POINTER	_IOR(SDR_IOC_MAGIC,  1, int *)
#define IOCTL_START_RX		_IOR(SDR_IOC_MAGIC,  2, int)
#define IOCTL_STOP_RX		_IOR(SDR_IOC_MAGIC,  3, int)
#define IOCTL_SET_PARAM		_IOR(SDR_IOC_MAGIC,  4, struct i2s_ctrl *)
#endif
