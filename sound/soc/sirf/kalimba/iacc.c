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
#ifdef CONFIG_SND_SOC_SIRF_KALIMBA_DEBUG
#include "../../codecs/sirf-atlas7-codec.h"
#endif

#include "dma-hack.h"

#define IACC_TX_CHANNELS	4
#define IACC_RX_CHANNELS	2

struct atlas7_iacc {
	struct clk *clk;
	struct regmap *regmap;
};

static struct atlas7_iacc *atlas7_iacc;

#ifdef CONFIG_SND_SOC_SIRF_KALIMBA_DEBUG
struct regmap *atlas7_codec_regmap;

void debug_setup_codec_regmap(struct regmap *regmap)
{
	atlas7_codec_regmap = regmap;
}
EXPORT_SYMBOL(debug_setup_codec_regmap);
#endif

static void atlas7_iacc_tx_enable(struct atlas7_iacc *atlas7_iacc,
	int channels)
{
	int i;

	if (channels == IACC_TX_CHANNELS)
		regmap_update_bits(atlas7_iacc->regmap, INTCODECCTL_MODE_CTRL,
			TX_SYNC_EN | TX_START_SYNC_EN,
			TX_SYNC_EN | TX_START_SYNC_EN);

	for (i = 0; i < channels; i++)
		regmap_update_bits(atlas7_iacc->regmap, INTCODECCTL_TX_RX_EN,
			DAC_EN << i, DAC_EN << i);
}

static void atlas7_iacc_tx_disable(struct atlas7_iacc *atlas7_iacc)
{
	int i;

	for (i = 0; i < IACC_TX_CHANNELS; i++)
		regmap_update_bits(atlas7_iacc->regmap, INTCODECCTL_TX_RX_EN,
			DAC_EN << i, 0);
}

static void atlas7_iacc_rx_enable(struct atlas7_iacc *atlas7_iacc,
	int channels)
{
	int i;

	for (i = 0; i < channels; i++)
		regmap_update_bits(atlas7_iacc->regmap, INTCODECCTL_TX_RX_EN,
			ADC_EN << i, ADC_EN << i);
}

static void atlas7_iacc_rx_disable(struct atlas7_iacc *atlas7_iacc)
{
	int i;

	for (i = 0; i < IACC_RX_CHANNELS; i++)
		regmap_update_bits(atlas7_iacc->regmap, INTCODECCTL_TX_RX_EN,
			ADC_EN << i, 0);
}

#ifdef CONFIG_SND_SOC_SIRF_KALIMBA_DEBUG
struct rate_reg_values_t {
	unsigned int rate;
	u32 value;
};

static struct rate_reg_values_t rate_dac_reg_values[] = {
	{32000, DAC_BASE_SMAPLE_RATE_32K0},
	{44100, DAC_BASE_SMAPLE_RATE_44K1},
	{48000, DAC_BASE_SMAPLE_RATE_48K0},
	{96000, DAC_BASE_SMAPLE_RATE_96K0},
	{192000, DAC_BASE_SMAPLE_RATE_192K0},
};

static struct rate_reg_values_t rate_adc_reg_values[] = {
	{8000, ADC_SAMPLE_RATE_08K},
	{11025, ADC_SAMPLE_RATE_11K},
	{16000, ADC_SAMPLE_RATE_16K},
	{22050, ADC_SAMPLE_RATE_22K},
	{32000, ADC_SAMPLE_RATE_32K},
	{44100, ADC_SAMPLE_RATE_44K},
	{48000, ADC_SAMPLE_RATE_48K},
	{96000, ADC_SAMPLE_RATE_96K},
};

static u32 rate_to_reg(int playback, int rate)
{
	int i;

	if (playback) {
		for (i = 0; i < ARRAY_SIZE(rate_dac_reg_values); i++) {
			if (rate_dac_reg_values[i].rate == rate)
				return KCODEC_DAC_SELECT_EXT
					| rate_dac_reg_values[i].value
					<< KCODEC_DAC_EXT_BASE_SAMP_RATE_SHIFT;
		}
	} else {
		for (i = 0; i < ARRAY_SIZE(rate_adc_reg_values); i++) {
			if (rate_adc_reg_values[i].rate == rate)
				return rate_adc_reg_values[i].value;
		}
	}
	return 0;
}

static u32 dac_sample_rate_regs[] = {
	KCODEC_DAC_A_SAMP_RATE,
	KCODEC_DAC_B_SAMP_RATE,
	KCODEC_DAC_C_SAMP_RATE,
	KCODEC_DAC_D_SAMP_RATE
};

static int atlas7_codec_setup(int pchannels, int rchannels,
	enum iacc_input_path path, u32 SampleRate)
{
	u32 pll_status;
	int i;

	regmap_update_bits(atlas7_codec_regmap, AUDIO_PLL_CTRL_1,
		1 << 11, 0);
	regmap_update_bits(atlas7_codec_regmap, AUDIO_PLL_CTRL_1,
			CLKSYNR_NRST, CLKSYNR_NRST);
	msleep(100);
	/* Wait for PLL lock */
	regmap_read(atlas7_codec_regmap, AUDIO_PLL_STATUS, &pll_status);
	if (!pll_status)
		return -EINVAL;
	/*Enable VBG trim*/
	regmap_update_bits(atlas7_codec_regmap, ANA_PMUCTL2,
		PMUCTRL2_VBG_TRIM, PMUCTRL2_VBG_TRIM_0XF);

	/* Workaround for some registers update fail. */
	regmap_update_bits(atlas7_codec_regmap,  0x58,
			(0x3 << 5), (2 << 5));
	regmap_update_bits(atlas7_codec_regmap, 0x50,
			1, 1);
	regmap_update_bits(atlas7_codec_regmap, 0x50,
			1, 0);
	regmap_update_bits(atlas7_codec_regmap,  0x58,
			(0x3 << 5), (0 << 5));

	/* loutbais */
	regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_REF_CTRL0,
		1, 1);
	regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_REF_CTRL0,
			AUDIO_ANA_REF_AUDBIAS_VAG_RX_EN,
			AUDIO_ANA_REF_AUDBIAS_VAG_RX_EN);
	regmap_update_bits(atlas7_codec_regmap, AUDIO_REF_CTRL2,
			AUDIO_REF_BIAS_BG_VTH_TRIM_MASK, 4);
	regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_REF_CTRL0,
			AUDIO_ANA_REF_AUDBIAS_IREF_TRIM_MASK,
			(7 << AUDIO_ANA_REF_AUDBIAS_IREF_TRIM_SHIFT));
	regmap_update_bits(atlas7_codec_regmap, AUDIO_REF_CTRL,
			AUDIO_REF_BOOST_EN_DACBUFF_IREF_MASK, 3);

	/* DACx clock enable */
	for (i = 0; i < pchannels; i++)
		regmap_update_bits(atlas7_codec_regmap, AUDIO_REGS_CLK_CTRL,
			1 << (6 + i), 1 << (6 + i));

	/* DACx config */
	regmap_update_bits(atlas7_codec_regmap, AUDIO_KCODEC_CTRL,
			KCODEC_DAC_EN, KCODEC_DAC_EN);
	regmap_update_bits(atlas7_codec_regmap, KCODEC_CONFIG,
			1 << 12, 1 << 12);
	if (pchannels == 4) {
		regmap_update_bits(atlas7_codec_regmap, KCODEC_CONFIG,
			1 << 13, 1 << 13);
		regmap_update_bits(atlas7_codec_regmap, KCODEC_CONFIG2,
			1 << 12, 1 << 12);
		regmap_update_bits(atlas7_codec_regmap, KCODEC_CONFIG2,
			1 << 13, 1 << 13);
	}

	/* VGEN enable */
	regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_DAC_CTRL0, 1, 1);

	/* LOUTx PGA */
	for (i = 0; i < pchannels; i++)
		regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_DAC_CTRL0,
			1 << (1 + i), 1 << (1 + i));

	/* LOUTx Buff PGA */
	for (i = 0; i < pchannels; i++)
		regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_DAC_CTRL0,
			1 << (7 + i), 1 << (7 + i));

	/* DACx reset */
	for (i = 0; i < pchannels; i++)
		regmap_update_bits(atlas7_codec_regmap, AUDIO_DAC_CTRL,
			1 << i, 0);

	/* Dither EN */
	regmap_update_bits(atlas7_codec_regmap, KCODEC_CONFIG_EXTENSION2,
		1 << 5, 0);
	regmap_update_bits(atlas7_codec_regmap, KCODEC_CONFIG_EXTENSION2,
		KCODEC_CONFIG_DEM_DITHER_CFG_MASK, 0);
	if (pchannels == 4) {
		regmap_update_bits(atlas7_codec_regmap,
				KCODEC_CONFIG2_EXTENSION2,
				1 << 5, 0);
		regmap_update_bits(atlas7_codec_regmap,
			KCODEC_CONFIG2_EXTENSION2,
			KCODEC_CONFIG_DEM_DITHER_CFG_MASK, 0);
	}

	/* Sample rate */
	for (i = 0; i < pchannels; i++)
		regmap_write(atlas7_codec_regmap, dac_sample_rate_regs[i],
				rate_to_reg(1, SampleRate));

	if (rchannels == 0)
		return 0;

	/* LINBIAS */
	regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_REF_CTRL0,
			AUDIO_ANA_REF_AUDBIAS_VAG_TX_EN
			| AUDIO_ANA_REF_AUDBIAS_VAG_RX_EN,
			AUDIO_ANA_REF_AUDBIAS_VAG_TX_EN
			| AUDIO_ANA_REF_AUDBIAS_VAG_RX_EN);
	regmap_update_bits(atlas7_codec_regmap, AUDIO_CTRL_SPARE_0,
			TXADC_IREF_EN, TXADC_IREF_EN);
	regmap_update_bits(atlas7_codec_regmap, AUDIO_REF_CTRL2,
			AUDIO_REF_BIAS_BG_VTH_TRIM_MASK
			| AUDIO_REF_BIAS_BG_PTAT_TRIM_MASK,
			4 | (0xC << AUDIO_REF_BIAS_BG_PTAT_TRIM_SHIFT));
	regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_REF_CTRL0,
			AUDIO_ANA_REF_AUDBIAS_IREF_TRIM_MASK,
			(7 << AUDIO_ANA_REF_AUDBIAS_IREF_TRIM_SHIFT));

	/* ADCx CLK */
	for (i = 0; i < rchannels; i++)
		regmap_update_bits(atlas7_codec_regmap, AUDIO_REGS_CLK_CTRL,
			1 << (4 + i), 1 << (4 + i));

	/* ADCx */
	regmap_update_bits(atlas7_codec_regmap, AUDIO_KCODEC_CTRL,
		KCODEC_ADC_EN, KCODEC_ADC_EN);
	for (i = 0; i < rchannels; i++)
		regmap_update_bits(atlas7_codec_regmap, KCODEC_CONFIG,
			1 << (10 + i), 1 << (10 + i));

	/* ADCx ANA EN */
	regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_ADC_CTRL2, 1, 1);
	if (rchannels == 2)
		regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_ADC_CTRL3,
			1, 1);

	/* ADCx ANA Dither EN */
	regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_ADC_CTRL2,
		1 << 1, 1 << 1);
	if (rchannels == 2)
		regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_ADC_CTRL3,
			1 << 1, 1 << 1);

	/* ADCx ANA Dither EN */
	regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_ADC_CTRL2,
		1 << 2, 1 << 2);
	if (rchannels == 2)
		regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_ADC_CTRL3,
			1 << 2, 1 << 2);

	/* ADC RESET */
	regmap_write(atlas7_codec_regmap, AUDIO_ANA_ADC_CTRL0, 0x1850);
	regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_CAL_CTRL0,
			AUDIO_ANA_CTRL_ADC_EN, 0);
	regmap_update_bits(atlas7_codec_regmap, AUDIO_REGS_CLK_CTRL,
			AUDIO_ANA_CAL_CLK_EN, AUDIO_ANA_CAL_CLK_EN);
	regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_CAL_CTRL0, 1, 1);

	/* Sample rate */
	for (i = 0; i < rchannels; i++)
		regmap_write(atlas7_codec_regmap, KCODEC_ADC_A_SAMP_RATE
				+ (i * 0x20),
				rate_to_reg(0, SampleRate));
	return 0;
}

void atlas7_codec_release(void)
{
	int i;

	regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_CAL_CTRL0, 1, 0);

	regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_ADC_CTRL2,
		1 << 2, 0);
	regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_ADC_CTRL3,
		1 << 2, 0);

	regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_ADC_CTRL2,
		1 << 1, 0);
	regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_ADC_CTRL3,
		1 << 1, 0);

	regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_ADC_CTRL2, 1, 0);
	regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_ADC_CTRL3, 1, 0);

	for (i = 0; i < 2; i++)
		regmap_update_bits(atlas7_codec_regmap, KCODEC_CONFIG,
			1 << (10 + i), 0);

	regmap_update_bits(atlas7_codec_regmap, AUDIO_KCODEC_CTRL,
		KCODEC_ADC_EN, 0);

	for (i = 0; i < 2; i++)
		regmap_update_bits(atlas7_codec_regmap, AUDIO_REGS_CLK_CTRL,
			1 << (4 + i), 0);

	regmap_update_bits(atlas7_codec_regmap, KCODEC_CONFIG2_EXTENSION2,
		1 << 5, 1 << 5);
	regmap_update_bits(atlas7_codec_regmap, KCODEC_CONFIG_EXTENSION2,
		1 << 5, 1 << 5);

	for (i = 0; i < 4; i++)
		regmap_update_bits(atlas7_codec_regmap, AUDIO_DAC_CTRL,
			1 << i, 1 << i);

	for (i = 0; i < 4; i++)
		regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_DAC_CTRL0,
			1 << (7 + i), 0);

	for (i = 0; i < 4; i++)
		regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_DAC_CTRL0,
			1 << (1 + i), 0);

	regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_DAC_CTRL0, 1, 0);

	regmap_update_bits(atlas7_codec_regmap, KCODEC_CONFIG,
			1 << 12, 0);
	regmap_update_bits(atlas7_codec_regmap, KCODEC_CONFIG,
			1 << 13, 0);
	regmap_update_bits(atlas7_codec_regmap, KCODEC_CONFIG2,
			1 << 12, 0);
	regmap_update_bits(atlas7_codec_regmap, KCODEC_CONFIG2,
			1 << 13, 0);
	regmap_update_bits(atlas7_codec_regmap, AUDIO_KCODEC_CTRL,
			KCODEC_DAC_EN, 0);

	for (i = 0; i < 4; i++)
		regmap_update_bits(atlas7_codec_regmap, AUDIO_REGS_CLK_CTRL,
			1 << (6 + i), 0);

	regmap_update_bits(atlas7_codec_regmap, AUDIO_ANA_REF_CTRL0,
		1, 0);

	regmap_update_bits(atlas7_codec_regmap, ANA_PMUCTL2,
			PMUCTRL2_VBG_TRIM, 0);
	regmap_update_bits(atlas7_codec_regmap, AUDIO_PLL_CTRL_1,
		1 << 11, 1 << 11);
}

int iacc_setup(int pchannels, int rchannels,
	enum iacc_input_path path, u32 SampleRate, u32 format)
{
	int ret;

	ret = atlas7_codec_setup(pchannels, rchannels, path, SampleRate);
	if (ret < 0)
		return ret;

	if (format == 0x00000001) {
		regmap_update_bits(atlas7_iacc->regmap,
			INTCODECCTL_MODE_CTRL, TX_24BIT, 0);
		regmap_update_bits(atlas7_iacc->regmap,
			INTCODECCTL_MODE_CTRL, RX_24BIT, 0);
	} else if (format == 0x00000002) {
		regmap_update_bits(atlas7_iacc->regmap,
			INTCODECCTL_MODE_CTRL, TX_24BIT, 1);
		regmap_update_bits(atlas7_iacc->regmap,
			INTCODECCTL_MODE_CTRL, RX_24BIT, 1);
	}
	return 0;
}

void debug_iacc_start(int playback, int channels, dma_addr_t dma_buff_addr,
		unsigned long buff_size)
{
	int i;
	const int ch[] = { CH_IACC_TX0, CH_IACC_TX1, CH_IACC_TX2, CH_IACC_TX3 };

	if (playback)
		for (i = 0; i < channels; i++)
			__dmac_enable(3, ch[i], dma_buff_addr + i*buff_size/6,
					buff_size / 6, 1, 0);
	else
		__dmac_enable(3, 0, dma_buff_addr + 4 * buff_size / 6,
				buff_size / 6, 0, 0);

	if (playback)
		atlas7_iacc_tx_enable(atlas7_iacc, channels);
	else
		atlas7_iacc_rx_enable(atlas7_iacc, channels);
}
#endif

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
	.max_register = INTCODECCTL_RXFIFO2_INT_MSK,
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
MODULE_LICENSE("GPL v2");
