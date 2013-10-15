/*
 * SIRF HDMI ALSA SoC Audio board driver
 *
 * Copyright (c) 2013 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/of.h>

#include <sound/soc.h>

static int sirf_hdmi_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_card *card = codec->card;
	unsigned int fmt;
	int ret;

	fmt = card->dai_link[0].dai_fmt;

	ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (ret < 0) {
		dev_err(card->dev, "can't set cpu DAI configuration\n");
		return ret;
	}

	return 0;
}

static struct snd_soc_ops sirf_hdmi_ops = {
	.hw_params = sirf_hdmi_hw_params,
};
/* Digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link sirf_hdmi_dai_links[] = {
	{
		.name = "SiRF HDMI",
		.stream_name = "SiRF HDMI",
		.codec_dai_name = "hdmi-hifi",
		.platform_name = "sirf-pcm-audio.0",
		.ops = &sirf_hdmi_ops,
	},
};

static struct snd_soc_card snd_soc_sirf_hdmi_card = {
	.name = "SiRF HDMI",
	.owner = THIS_MODULE,
	.dai_link = sirf_hdmi_dai_links,
	.num_links = ARRAY_SIZE(sirf_hdmi_dai_links),
};

static int sirf_hdmi_card_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_sirf_hdmi_card;
	int ret;

	sirf_hdmi_dai_links[0].cpu_of_node =
		of_find_compatible_node(NULL, NULL, "sirf,prima2-i2s");
	sirf_hdmi_dai_links[0].codec_of_node =
		of_find_compatible_node(NULL, NULL, "hdmi-audio-codec");

	sirf_hdmi_dai_links[0].dai_fmt = SND_SOC_DAIFMT_CBM_CFM |
		SND_SOC_DAIFMT_I2S;

	card->dev = &pdev->dev;
	ret = snd_soc_register_card(card);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, card);
	return 0;
}

static int sirf_hdmi_card_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	snd_soc_unregister_card(card);
	return 0;
}
static const struct of_device_id sirf_hdmi_card_of_match[] = {
	{ .compatible = "sirf,hdmi-card", },
	{}
};
MODULE_DEVICE_TABLE(of, sirf_hdmi_card_of_match);

static struct platform_driver sirf_hdmi_card_driver = {
	.driver = {
		.name = "sirf-hdmi-card",
		.owner = THIS_MODULE,
		.of_match_table = sirf_hdmi_card_of_match,
	},
	.probe = sirf_hdmi_card_probe,
	.remove = sirf_hdmi_card_remove,
};

module_platform_driver(sirf_hdmi_card_driver);

MODULE_DESCRIPTION("SIRF HDMI ALSA SoC Audio board driver");
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
MODULE_LICENSE("GPL v2");
