/*
 * ALSA SoC codec driver for HDMI audio on SiRF processors.
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/module.h>
#include <sound/soc.h>

static struct snd_soc_codec_driver sirf_hdmi_codec;

static struct snd_soc_dai_driver sirf_hdmi_codec_dai = {
	.name = "sirf-hdmi-codec",
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_32000 |
			SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S24_LE,
	},
};

static int sirf_hdmi_codec_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev, &sirf_hdmi_codec,
			&sirf_hdmi_codec_dai, 1);
}

static int sirf_hdmi_codec_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static const struct of_device_id sirf_hdmi_codec_of_match[] = {
	{ .compatible = "sirf,hdmi-codec", },
	{}
};
MODULE_DEVICE_TABLE(of, sirf_hdmi_codec_of_match);

static struct platform_driver sirf_hdmi_codec_driver = {
	.driver		= {
		.name	= "sirf-hdmi-codec",
		.owner	= THIS_MODULE,
		.of_match_table = sirf_hdmi_codec_of_match,
	},

	.probe		= sirf_hdmi_codec_probe,
	.remove		= sirf_hdmi_codec_remove,
};

module_platform_driver(sirf_hdmi_codec_driver);

MODULE_AUTHOR("Rongjun Ying <rongjun.ying@csr.com>");
MODULE_DESCRIPTION("ASoC SiRF HDMI codec driver");
MODULE_LICENSE("GPL");
