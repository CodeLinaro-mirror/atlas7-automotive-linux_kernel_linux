#include <linux/delay.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk.h>
#include <linux/regmap.h>

#include "../atlas7-iacc.h"
#include "iacc.h"

#include "dma-hack.h"

#define IACC_TX_CHANNELS	4
#define IACC_RX_CHANNELS	2

struct atlas7_iacc {
	struct clk *clk;
	struct regmap *regmap;
};

static struct atlas7_iacc *atlas7_iacc;

static void atlas7_iacc_tx_enable(struct atlas7_iacc *atlas7_iacc,
	int channels)
{
	int i;

	if (channels == IACC_TX_CHANNELS)
		regmap_update_bits(atlas7_iacc->regmap, INTCODECCTL_MODE_CTRL,
			TX_SYNC_EN | TX_START_SYNC_EN,
			TX_SYNC_EN | TX_START_SYNC_EN);

	for (i = 0; i < channels; i++) {
		regmap_update_bits(atlas7_iacc->regmap, INTCODECCTL_TX_RX_EN,
			DAC_EN << i, DAC_EN << i);
		regmap_update_bits(atlas7_iacc->regmap,
			INTCODECCTL_TXFIFO0_OP + (i * 20),
			FIFO_RESET, FIFO_RESET);
		regmap_update_bits(atlas7_iacc->regmap,
			INTCODECCTL_TXFIFO0_OP + (i * 20),
			FIFO_RESET, ~FIFO_RESET);

		regmap_write(atlas7_iacc->regmap,
			INTCODECCTL_TXFIFO0_INT_MSK + (i * 20), 0);
		regmap_update_bits(atlas7_iacc->regmap,
			INTCODECCTL_TXFIFO0_OP + (i * 20),
			FIFO_START, FIFO_START);
	}
}

static void atlas7_iacc_tx_disable(struct atlas7_iacc *atlas7_iacc)
{
	int i;

	for (i = 0; i < IACC_TX_CHANNELS; i++) {
		regmap_write(atlas7_iacc->regmap,
			INTCODECCTL_TXFIFO0_OP + (i * 20), 0);
		regmap_update_bits(atlas7_iacc->regmap, INTCODECCTL_TX_RX_EN,
			DAC_EN << i, 0);
	}
}

static void atlas7_iacc_rx_enable(struct atlas7_iacc *atlas7_iacc,
	int channels)
{
	int i;

	for (i = 0; i < channels; i++)
		regmap_update_bits(atlas7_iacc->regmap, INTCODECCTL_TX_RX_EN,
			ADC_EN << i, ADC_EN << i);
	regmap_update_bits(atlas7_iacc->regmap, INTCODECCTL_RXFIFO_OP,
		FIFO_RESET, FIFO_RESET);
	regmap_update_bits(atlas7_iacc->regmap, INTCODECCTL_RXFIFO_OP,
		FIFO_RESET, ~FIFO_RESET);
	regmap_write(atlas7_iacc->regmap, INTCODECCTL_RXFIFO_INT_MSK, 0);
	regmap_update_bits(atlas7_iacc->regmap, INTCODECCTL_RXFIFO_OP,
		FIFO_START, FIFO_START);
}

static void atlas7_iacc_rx_disable(struct atlas7_iacc *atlas7_iacc)
{
	int i;

	for (i = 0; i < IACC_RX_CHANNELS; i++)
		regmap_update_bits(atlas7_iacc->regmap, INTCODECCTL_TX_RX_EN,
			ADC_EN << i, 0);
	regmap_write(atlas7_iacc->regmap, INTCODECCTL_RXFIFO_OP, 0);
}

/* TODO: Remove this function after Kalimba takes over the job */
void iacc_start(int playback, int channels, dma_addr_t dma_buff_addr,
		unsigned long buff_size)
{
	int i;
	const int ch[] = { CH_IACC_TX0, CH_IACC_TX1, CH_IACC_TX2, CH_IACC_TX3 };

	if (playback) {
		for (i = 0; i < channels; i++)
			__dmac_enable(DMAC_IACC, ch[i],
					dma_buff_addr + i * buff_size,
					buff_size, MEM_TO_DEV, DMA_SINGLE);
		atlas7_iacc_tx_enable(atlas7_iacc, channels);
	} else {
		__dmac_enable(DMAC_IACC, CH_IACC_RX, dma_buff_addr,
				buff_size, DEV_TO_MEM, DMA_SINGLE);
		atlas7_iacc_rx_enable(atlas7_iacc, channels);
	}
}

/* TODO: Remove this function after Kalimba takes over the job */
void iacc_stop(int playback)
{
	int i;
	const int ch[] = { CH_IACC_TX0, CH_IACC_TX1, CH_IACC_TX2, CH_IACC_TX3 };

	if (playback) {
		atlas7_iacc_tx_disable(atlas7_iacc);
		for (i = 0; i < IACC_TX_CHANNELS; i++)
			__dmac_disable(DMAC_IACC, ch[i]);
	} else {
		atlas7_iacc_rx_disable(atlas7_iacc);
		__dmac_disable(DMAC_IACC, CH_IACC_RX);
	}
}

static const struct regmap_config atlas7_iacc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = INTCODECCTL_RXFIFO_INT_MSK,
	.cache_type = REGCACHE_NONE,
};

static int atlas7_iacc_probe(struct platform_device *pdev)
{
	int ret;
	struct clk *clk;
	struct resource *mem_res;
	void __iomem *base;

	atlas7_iacc = devm_kzalloc(&pdev->dev,
			sizeof(struct atlas7_iacc), GFP_KERNEL);
	if (!atlas7_iacc)
		return -ENOMEM;

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, mem_res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	atlas7_iacc->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					    &atlas7_iacc_regmap_config);
	if (IS_ERR(atlas7_iacc->regmap))
		return PTR_ERR(atlas7_iacc->regmap);

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "Get clock failed.\n");
		ret = PTR_ERR(clk);
		return ret;
	}

	atlas7_iacc->clk = clk;

	ret = clk_prepare_enable(atlas7_iacc->clk);
	if (ret) {
		dev_err(&pdev->dev, "clk_enable failed: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, atlas7_iacc);

	return 0;
}

static int atlas7_iacc_remove(struct platform_device *pdev)
{
	struct atlas7_iacc *atlas7_iacc = platform_get_drvdata(pdev);

	clk_disable_unprepare(atlas7_iacc->clk);

	return 0;
}

static const struct of_device_id atlas7_iacc_of_match[] = {
	{ .compatible = "sirf,atlas7-iacc", },
	{}
};
MODULE_DEVICE_TABLE(of, atlas7_iacc_of_match);

static struct platform_driver atlas7_iacc_driver = {
	.driver = {
		.name = "sirf-atlas7-iacc",
		.of_match_table = atlas7_iacc_of_match,
	},
	.probe = atlas7_iacc_probe,
	.remove = atlas7_iacc_remove,
};

module_platform_driver(atlas7_iacc_driver);

MODULE_DESCRIPTION("SiRF ATLAS7 IACC(internal audio codec cotroller) driver");
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
MODULE_LICENSE("GPL v2");
