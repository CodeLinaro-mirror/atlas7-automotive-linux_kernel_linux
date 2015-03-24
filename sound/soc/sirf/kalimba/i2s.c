/*
 * SiRF I2S driver
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/delay.h>

#include "../sirf-i2s.h"
#include "i2s.h"

#include "dma-hack.h"

struct sirf_i2s {
	struct device *dev;
	struct regmap *regmap;
	struct clk *clk;
	u32 i2s_ctrl;
	u32 i2s_ctrl_tx_rx_en;
	bool master;
	bool clkout;
	int clk_id;
	int sysclk;
	struct clk *clk_audioif, *clk_mux, *clk_dto;
};

static struct sirf_i2s *i2s;

static void sirf_i2s_tx_enable(struct sirf_i2s *i2s)
{
	/* First start the FIFO, then enable the tx/rx */
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_TXFIFO_OP,
		AUDIO_FIFO_RESET, AUDIO_FIFO_RESET);
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_TXFIFO_OP,
		AUDIO_FIFO_RESET, 0);
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_TXFIFO_OP,
		AUDIO_FIFO_START, AUDIO_FIFO_START);
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_TX_RX_EN,
		I2S_TX_ENABLE | I2S_DOUT_OE,
		I2S_TX_ENABLE | I2S_DOUT_OE);
}

static void sirf_i2s_tx_disable(struct sirf_i2s *i2s)
{
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_TX_RX_EN,
		I2S_TX_ENABLE, ~I2S_TX_ENABLE);
	/* First disable the tx/rx, then stop the FIFO */
	regmap_write(i2s->regmap, AUDIO_CTRL_I2S_TXFIFO_OP, 0);
}

static void sirf_i2s_rx_enable(struct sirf_i2s *i2s)
{
	/* First start the FIFO, then enable the tx/rx */
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_RXFIFO_OP,
		AUDIO_FIFO_RESET, AUDIO_FIFO_RESET);
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_RXFIFO_OP,
		AUDIO_FIFO_RESET, 0);
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_RXFIFO_OP,
		AUDIO_FIFO_START, AUDIO_FIFO_START);
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_TX_RX_EN,
		I2S_RX_ENABLE, I2S_RX_ENABLE);
}

static void sirf_i2s_rx_disable(struct sirf_i2s *i2s)
{
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_TX_RX_EN,
		I2S_RX_ENABLE, ~I2S_RX_ENABLE);
	/* First disable the tx/rx, then stop the FIFO */
	regmap_write(i2s->regmap, AUDIO_CTRL_I2S_RXFIFO_OP, 0);
}

/* TODO: Remove this function after Kalimba takes over the job */
void sirf_i2s_start(int playback, dma_addr_t dma_buff_addr,
		unsigned long buff_size)
{
	if (playback) {
		__dmac_enable(DMAC_I2S, CH_I2S_TX, dma_buff_addr,
				buff_size / 2, MEM_TO_DEV, DMA_SINGLE);
		sirf_i2s_tx_enable(i2s);
	} else {
		__dmac_enable(DMAC_I2S, CH_I2S_RX,
				dma_buff_addr + buff_size / 2,
				buff_size / 2, DEV_TO_MEM, DMA_SINGLE);
		sirf_i2s_rx_enable(i2s);
	}
}

/* TODO: Remove this function after Kalimba takes over the job */
void sirf_i2s_stop(int playback)
{
	if (playback) {
		sirf_i2s_tx_disable(i2s);
		__dmac_disable(DMAC_I2S, CH_I2S_TX);
	} else {
		sirf_i2s_rx_disable(i2s);
		__dmac_disable(DMAC_I2S, CH_I2S_RX);
	}
}

void sirf_i2s_params(int channels, int rate, int slave)
{
	u32 i2s_ctrl = 0;
	u32 i2s_tx_rx_ctrl = 0;
	u32 left_len, frame_len;
	u32 bitclk;
	u32 bclk_div;
	u32 div;

	/* FIXME: DTO frequency is fixed to rate*512*2 now. It should be
	 * calculated in machine driver per codec requirement.
	 */
	clk_set_rate(i2s->clk_dto, rate * 1024);
	if (clk_get_parent(i2s->clk_mux) != i2s->clk_dto)
		clk_set_parent(i2s->clk_mux, i2s->clk_dto);

	switch (channels) {
	case 2:
		i2s_ctrl &= ~I2S_SIX_CHANNELS;
		break;
	case 6:
		i2s_ctrl |= I2S_SIX_CHANNELS;
		break;
	default:
		dev_err(i2s->dev, "%d channels unsupported\n", channels);
		return;
	}

	left_len = 16;

	frame_len = left_len * 2;
	/* Fill the actual len - 1 */
	i2s_ctrl |= ((frame_len - 1) << I2S_FRAME_LEN_SHIFT)
		| ((left_len - 1) << I2S_L_CHAN_LEN_SHIFT);

	if (!slave) {
		i2s_ctrl &= ~I2S_SLAVE_MODE;
		bitclk = rate * frame_len;
		div = rate * 1024 / bitclk;
		/* MCLK divide-by-2 from source clk */
		div /= 2;
		bclk_div = div / 2 - 1;
		i2s_ctrl |= (bclk_div << I2S_BITCLK_DIV_SHIFT);
		/*
		 * MCLK coefficient must set to 0, means
		 * divide-by-two from reference clock.
		 */
		i2s_ctrl &= ~I2S_MCLK_DIV_MASK;
	} else
		i2s_ctrl |= I2S_SLAVE_MODE;

	i2s_tx_rx_ctrl &= ~I2S_REF_CLK_SEL_EXT;

	i2s_tx_rx_ctrl |= I2S_MCLK_EN;

	regmap_write(i2s->regmap, AUDIO_CTRL_I2S_CTRL, i2s_ctrl);
	regmap_write(i2s->regmap, AUDIO_CTRL_I2S_TX_RX_EN, i2s_tx_rx_ctrl);
}

static const struct regmap_config sirf_i2s_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = AUDIO_CTRL_I2S_RXFIFO_INT_MSK,
	.cache_type = REGCACHE_NONE,
};

static int sirf_i2s_probe(struct platform_device *pdev)
{
	int ret;
	void __iomem *base;
	struct resource *mem_res;

	i2s = devm_kzalloc(&pdev->dev, sizeof(struct sirf_i2s),
			GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap(&pdev->dev, mem_res->start,
		resource_size(mem_res));
	if (base == NULL)
		return -ENOMEM;

	i2s->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					    &sirf_i2s_regmap_config);
	if (IS_ERR(i2s->regmap))
		return PTR_ERR(i2s->regmap);

	i2s->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(i2s->clk)) {
		dev_err(&pdev->dev, "Failed to get 'i2s' clock.\n");
		return PTR_ERR(i2s->clk);
	}

	i2s->clk_audioif = devm_clk_get(&pdev->dev, "audioif");
	if (IS_ERR(i2s->clk_audioif)) {
		dev_err(&pdev->dev, "Failed to get 'audioif' clock.\n");
		return PTR_ERR(i2s->clk_audioif);
	}

	i2s->clk_mux = devm_clk_get(&pdev->dev, "i2s_mux");
	if (IS_ERR(i2s->clk_mux)) {
		dev_err(&pdev->dev, "Failed to get 'i2s_mux' clock.\n");
		return PTR_ERR(i2s->clk_mux);
	}

	i2s->clk_dto = devm_clk_get(&pdev->dev, "audio_dto");
	if (IS_ERR(i2s->clk_dto)) {
		dev_err(&pdev->dev, "Failed to get 'audio_dto' clock.\n");
		return PTR_ERR(i2s->clk_dto);
	}

	ret = clk_prepare_enable(i2s->clk);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable 'i2s' clock.\n");
		return ret;
	}

	ret = clk_prepare_enable(i2s->clk_audioif);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable 'audioif' clock.\n");
		goto err1;
	}

	ret = clk_prepare_enable(i2s->clk_mux);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable 'i2s_mux' clock.\n");
		goto err2;
	}

	ret = clk_prepare_enable(i2s->clk_dto);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable 'audio_dto' clock.\n");
		goto err3;
	}

	platform_set_drvdata(pdev, i2s);

	return 0;

err3:
	clk_disable_unprepare(i2s->clk_mux);
err2:
	clk_disable_unprepare(i2s->clk_audioif);
err1:
	clk_disable_unprepare(i2s->clk);

	return ret;
}

static const struct of_device_id sirf_i2s_of_match[] = {
	{ .compatible = "sirf,atlas7-i2s", },
	{}
};
MODULE_DEVICE_TABLE(of, sirf_i2s_of_match);

static struct platform_driver sirf_i2s_driver = {
	.driver = {
		.name = "sirf-i2s",
		.owner = THIS_MODULE,
		.of_match_table = sirf_i2s_of_match,
	},
	.probe = sirf_i2s_probe,
};

module_platform_driver(sirf_i2s_driver);

MODULE_DESCRIPTION("SiRF SoC I2S driver");
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
MODULE_LICENSE("GPL v2");
