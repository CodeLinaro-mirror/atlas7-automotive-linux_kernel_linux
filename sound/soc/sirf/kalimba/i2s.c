/*
 * SiRF I2S driver
 *
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/delay.h>

#include "../sirf-i2s.h"
#include "i2s.h"

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
	/* Reset TDM playback logic */
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_TDM_CTRL,
			I2S_TDM_TX_RESET, I2S_TDM_TX_RESET);
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_TDM_CTRL,
			I2S_TDM_TX_RESET, 0);

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
	/* Reset TDM record logic */
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_TDM_CTRL,
			I2S_TDM_RX_RESET, I2S_TDM_RX_RESET);
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_TDM_CTRL,
			I2S_TDM_RX_RESET, 0);

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
void sirf_i2s_start(int playback)
{
	if (playback)
		sirf_i2s_tx_enable(i2s);
	else
		sirf_i2s_rx_enable(i2s);
}

/* TODO: Remove this function after Kalimba takes over the job */
void sirf_i2s_stop(int playback)
{
	if (playback)
		sirf_i2s_tx_disable(i2s);
	else
		sirf_i2s_rx_disable(i2s);
}

void sirf_i2s_set_sysclk(int freq)
{
	i2s->sysclk = freq;
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
	clk_set_rate(i2s->clk_mux, rate * 1024);

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

int sirf_i2s_params_adv(struct i2s_params *param)
{
	u32 i2s_ctrl = 0;
	u32 i2s_tx_rx_en = 0;
	u32 tdm_ctrl = 0;
	u32 tdm_mask;
	u32 left_len, frame_len;
	u32 bclk_div, bclk_ratio;

	if (!i2s->sysclk)
		i2s->sysclk = param->rate * 512;

	clk_set_rate(i2s->clk_dto, i2s->sysclk * 2);
	clk_set_rate(i2s->clk_mux, i2s->sysclk * 2);

	switch (param->channels) {
	case 2:
	case 6:
		i2s_ctrl |= I2S_SIX_CHANNELS;
		bclk_ratio = 32;
		break;
	case 4:
		regmap_read(i2s->regmap, AUDIO_CTRL_I2S_TDM_CTRL,
			&tdm_ctrl);

		if (param->playback) {
			tdm_mask = I2S_TDM_MASK_TX;
			tdm_ctrl &= ~tdm_mask;
			tdm_ctrl |= (I2S_TDM_WORD_SIZE_TX(32))
				|(I2S_TDM_DAC_CH(param->channels))
				| I2S_TDM_DATA_ALIGN_TX_LEFT_J
				| I2S_TDM_WORD_ALIGN_TX_LEFT_J
				| I2S_TDM_FRAME_POLARITY_LOW
				| I2S_TDM_FRAME_SYNC_DSP0;
		} else {
			tdm_mask = I2S_TDM_MASK_RX;
			tdm_ctrl &= ~tdm_mask;
			tdm_ctrl |= (I2S_TDM_WORD_SIZE_RX(32))
				|(I2S_TDM_ADC_CH(param->channels))
				|I2S_TDM_DATA_ALIGN_RX_LEFT_J
				|I2S_TDM_WORD_ALIGN_RX_I2S0
				|I2S_TDM_FRAME_POLARITY_HIGH
				|I2S_TDM_FRAME_SYNC_DSP0;
		}
		tdm_ctrl |= I2S_TDM_ENA;
		bclk_ratio = 128;
		break;
	case 8:
		regmap_read(i2s->regmap, AUDIO_CTRL_I2S_TDM_CTRL,
			&tdm_ctrl);

		if (param->playback) {
			tdm_mask = I2S_TDM_MASK_TX;
			tdm_ctrl &= ~tdm_mask;
			tdm_ctrl |= (I2S_TDM_WORD_SIZE_TX(32))
					|(I2S_TDM_DAC_CH(param->channels))
					|I2S_TDM_DATA_ALIGN_TX_LEFT_J
					|I2S_TDM_WORD_ALIGN_TX_I2S0
					|I2S_TDM_FRAME_POLARITY_HIGH
					|I2S_TDM_FRAME_SYNC_DSP0;
		} else {
			tdm_mask = I2S_TDM_MASK_RX;
			tdm_ctrl &= ~tdm_mask;
			tdm_ctrl |= (I2S_TDM_WORD_SIZE_RX(32))
					|(I2S_TDM_ADC_CH(param->channels))
					|I2S_TDM_DATA_ALIGN_RX_LEFT_J
					|I2S_TDM_WORD_ALIGN_RX_I2S0
					|I2S_TDM_FRAME_POLARITY_HIGH
					|I2S_TDM_FRAME_SYNC_DSP0;
		}
		tdm_ctrl |= I2S_TDM_ENA;
		bclk_ratio = 256;
		break;
	default:
		dev_err(i2s->dev, "%d channels unsupported\n", param->channels);
		return -EINVAL;
	}

	left_len = 16;
	frame_len = left_len * 2;

	if (!param->slave) {
		i2s_ctrl &= ~I2S_SLAVE_MODE;
		bclk_div = (i2s->sysclk / (param->rate * bclk_ratio * 2)) - 1;

		if (!bclk_div) {
			dev_err(i2s->dev, "bclk div %d  error\n", bclk_div);
			return -EINVAL;
		}
		i2s_ctrl |= (bclk_div << I2S_BITCLK_DIV_SHIFT);
	} else
		i2s_ctrl |= I2S_SLAVE_MODE;

	i2s_ctrl |= ((frame_len - 1) << I2S_FRAME_LEN_SHIFT)
		| ((left_len - 1) << I2S_L_CHAN_LEN_SHIFT);

	regmap_read(i2s->regmap, AUDIO_CTRL_I2S_TX_RX_EN,
		&i2s_tx_rx_en);
	i2s_tx_rx_en &= ~I2S_REF_CLK_SEL_EXT;
	i2s_tx_rx_en |= I2S_MCLK_EN;

	regmap_write(i2s->regmap, AUDIO_CTRL_I2S_CTRL, i2s_ctrl);
	regmap_write(i2s->regmap, AUDIO_CTRL_I2S_TX_RX_EN, i2s_tx_rx_en);
	regmap_write(i2s->regmap, AUDIO_CTRL_I2S_TDM_CTRL, tdm_ctrl);
	return 0;
}

static const struct regmap_config sirf_i2s_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = AUDIO_CTRL_I2S_RXFIFO_INT_MSK,
	.cache_type = REGCACHE_NONE,
};

#ifdef CONFIG_PM_SLEEP
static int sirf_i2s_suspend(struct device *dev)
{
	struct sirf_i2s *i2s = dev_get_drvdata(dev);

	clk_disable_unprepare(i2s->clk_mux);
	clk_disable_unprepare(i2s->clk_audioif);
	clk_disable_unprepare(i2s->clk);

	return 0;
}

static int sirf_i2s_resume(struct device *dev)
{
	struct sirf_i2s *i2s = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(i2s->clk);
	if (ret) {
		dev_err(dev, "Failed to enable 'i2s' clock.\n");
		return ret;
	}

	ret = clk_prepare_enable(i2s->clk_audioif);
	if (ret) {
		dev_err(dev, "Failed to enable 'audioif' clock.\n");
		return ret;
	}

	ret = clk_prepare_enable(i2s->clk_mux);
	if (ret) {
		dev_err(dev, "Failed to enable 'i2s_mux' clock.\n");
		return ret;
	}

	ret = clk_prepare_enable(i2s->clk_dto);
	if (ret) {
		dev_err(dev, "Failed to enable 'audio_dto' clock.\n");
		return ret;
	}

	return 0;
}
#endif

static int sirf_i2s_probe(struct platform_device *pdev)
{
	void __iomem *base;
	struct resource *mem_res;
	int gpio_sw;
	u32 sw_sel_val;

	i2s = devm_kzalloc(&pdev->dev, sizeof(struct sirf_i2s),
			GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap(&pdev->dev, mem_res->start,
		resource_size(mem_res));
	if (base == NULL)
		return -ENOMEM;

	/*
	 *              ---------------
	 *             |               |--- i2s LD port
	 * sw1 --------|               |--- i2s VP port
	 * sw2 --------|               |--- i2s AU port
	 *              ---------------
	 */
	gpio_sw = of_get_named_gpio(pdev->dev.of_node, "sw1-sel", 0);
	if (gpio_is_valid(gpio_sw)) {
		if (!of_property_read_u32(pdev->dev.of_node, "sw1-sel-val",
			&sw_sel_val))
			devm_gpio_request_one(&pdev->dev, gpio_sw,
				sw_sel_val ? GPIOF_OUT_INIT_HIGH :
				GPIOF_OUT_INIT_LOW, "sw1-sel");
	}

	gpio_sw = of_get_named_gpio(pdev->dev.of_node, "sw2-sel", 0);
	if (gpio_is_valid(gpio_sw)) {
		if (!of_property_read_u32(pdev->dev.of_node, "sw2-sel-val",
			&sw_sel_val))
			devm_gpio_request_one(&pdev->dev, gpio_sw,
				sw_sel_val ? GPIOF_OUT_INIT_HIGH :
				GPIOF_OUT_INIT_LOW, "sw2-sel");
	}

	gpio_sw = of_get_named_gpio(pdev->dev.of_node, "sw3-sel", 0);
	if (gpio_is_valid(gpio_sw)) {
		if (!of_property_read_u32(pdev->dev.of_node, "sw3-sel-val",
			&sw_sel_val))
			devm_gpio_request_one(&pdev->dev, gpio_sw,
				sw_sel_val ? GPIOF_OUT_INIT_HIGH :
				GPIOF_OUT_INIT_LOW, "sw3-sel");
	}

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

	i2s->clk_mux = devm_clk_get(&pdev->dev, "audmscm_i2s");
	if (IS_ERR(i2s->clk_mux)) {
		dev_err(&pdev->dev, "Failed to get 'i2s_mux' clock.\n");
		return PTR_ERR(i2s->clk_mux);
	}

	i2s->clk_dto = devm_clk_get(&pdev->dev, "audio_dto");
	if (IS_ERR(i2s->clk_dto)) {
		dev_err(&pdev->dev, "Failed to get 'audio_dto' clock.\n");
		return PTR_ERR(i2s->clk_dto);
	}

	platform_set_drvdata(pdev, i2s);
	return sirf_i2s_resume(&pdev->dev);
}

static const struct dev_pm_ops sirf_i2s_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sirf_i2s_suspend, sirf_i2s_resume)
};

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
		.pm = &sirf_i2s_pm_ops,
	},
	.probe = sirf_i2s_probe,
};

module_platform_driver(sirf_i2s_driver);

MODULE_DESCRIPTION("SiRF SoC I2S driver");
MODULE_LICENSE("GPL v2");
