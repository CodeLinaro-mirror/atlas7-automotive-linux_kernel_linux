/*
 * SiRF bt sco ALSA SoC Audio board driver
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/module.h>
#include <linux/of.h>

#include <sound/soc.h>

static int sirf_bt_sco_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_card *card = codec->card;
	unsigned int fmt;
	int ret;

	fmt = card->dai_link[0].dai_fmt;

	/* Set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, fmt);
	if (ret < 0) {
		dev_err(card->dev, "can't set codec DAI configuration\n");
		return ret;
	}

	/* Set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (ret < 0) {
		dev_err(card->dev, "can't set cpu DAI configuration\n");
		return ret;
	}

	return 0;
}

static struct snd_soc_ops sirf_bt_sco_ops = {
	.hw_params = sirf_bt_sco_hw_params,
};

/* Digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link sirf_bt_sco_dai_links[] = {
	{
		.name = "SiRF BT SCO",
		.stream_name = "SiRF BT_SCO",
		.codec_dai_name = "bt-sco-pcm",
		.platform_name = "sirf-pcm-audio.2",
		.codec_name = "bt-sco",
		.ops = &sirf_bt_sco_ops,
	},
};

static struct snd_soc_card snd_soc_sirf_bt_sco_card = {
	.name = "SiRF BT SCO",
	.owner = THIS_MODULE,
	.dai_link = sirf_bt_sco_dai_links,
	.num_links = ARRAY_SIZE(sirf_bt_sco_dai_links),
};

static int sirf_bt_sco_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_sirf_bt_sco_card;
	int ret;
	u32 codec_fmt = 0;

	sirf_bt_sco_dai_links[0].cpu_of_node =
		of_find_compatible_node(NULL, NULL, "sirf,prima2-usp-pcm");
	ret = of_property_read_u32(pdev->dev.of_node,
		"codec-clock-frame-master",	&codec_fmt);
	if (ret == 0 && codec_fmt != 0)
		sirf_bt_sco_dai_links[0].dai_fmt = SND_SOC_DAIFMT_CBM_CFM;
	else
		sirf_bt_sco_dai_links[0].dai_fmt = SND_SOC_DAIFMT_CBM_CFS;
	card->dev = &pdev->dev;
	ret = snd_soc_register_card(card);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, card);
	return 0;
}

static int sirf_bt_sco_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	snd_soc_unregister_card(card);

	return 0;
}

static const struct of_device_id sirf_bt_sco_of_match[] = {
	{ .compatible = "sirf,sirf-bt-sco", },
	{}
};
MODULE_DEVICE_TABLE(of, sirf_bt_sco_of_match);

static struct platform_driver sirf_bt_sco_driver = {
	.driver = {
		.name = "sirf-bt-sco",
		.owner = THIS_MODULE,
		.of_match_table = sirf_bt_sco_of_match,
	},
	.probe = sirf_bt_sco_probe,
	.remove = sirf_bt_sco_remove,
};

module_platform_driver(sirf_bt_sco_driver);

MODULE_DESCRIPTION("SIRF BT SCO ALSA SoC Audio board driver");
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
MODULE_LICENSE("GPL v2");
