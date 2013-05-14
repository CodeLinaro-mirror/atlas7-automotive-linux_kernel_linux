/*
 * SIRF_CSRBT ALSA SoC Audio board driver
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/module.h>
#include <linux/of.h>

#include <sound/soc.h>

#ifdef CONFIG_SND_SIRF_DEBUG
static struct device *dev;
#define debug_info(x...) dev_info(dev, x)
#else
#define debug_info(x...)
#endif

/* Digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link sirf_csrbt_dai_links[] = {
	{
		.name = "SiRF CSRBT",
		.stream_name = "SiRF CSRBT",
		.codec_dai_name = "csr-bluetooth-codec",
	},
};

static struct snd_soc_card snd_soc_sirf_csrbt_card = {
	.name = "SiRF CSRBT",
	.owner = THIS_MODULE,
	.dai_link = sirf_csrbt_dai_links,
	.num_links = ARRAY_SIZE(sirf_csrbt_dai_links),
};

static int sirf_csrbt_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_sirf_csrbt_card;
	int ret;

#ifdef CONFIG_SND_SIRF_DEBUG
	dev = &pdev->dev;
#endif
	debug_info("%s\n", __func__);

	sirf_csrbt_dai_links[0].platform_of_node =
		of_find_compatible_node(NULL, NULL, "sirf,pcm-audio");
	sirf_csrbt_dai_links[0].cpu_of_node =
		of_find_compatible_node(NULL, NULL, "sirf,prima2-usp-pcm");
	sirf_csrbt_dai_links[0].codec_of_node =
		of_find_compatible_node(NULL, NULL, "csr,bluetooth");

	card->dev = &pdev->dev;
	ret = snd_soc_register_card(card);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, card);
	return 0;
}

static int sirf_csrbt_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
#ifdef CONFIG_SND_SIRF_DEBUG
	dev = NULL;
#endif
	snd_soc_unregister_card(card);
	platform_set_drvdata(pdev, NULL);
	return 0;
}
static const struct of_device_id sirf_csrbt_of_match[] = {
	{ .compatible = "sirf,sirf-csrbt", },
	{}
};
MODULE_DEVICE_TABLE(of, sirf_csrbt_of_match);

static struct platform_driver sirf_csrbt_driver = {
	.driver = {
		.name = "sirf-csrbt",
		.owner = THIS_MODULE,
		.of_match_table = sirf_csrbt_of_match,
	},
	.probe = sirf_csrbt_probe,
	.remove = sirf_csrbt_remove,
};

module_platform_driver(sirf_csrbt_driver);

MODULE_DESCRIPTION("SIRF_CSRBT ALSA SoC Audio board driver");
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
MODULE_LICENSE("GPL v2");
