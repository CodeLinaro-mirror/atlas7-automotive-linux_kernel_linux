/*
 * ALSA PCM interface for the SiRF SoC
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <sound/dmaengine_pcm.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

static struct dma_chan *sirf_pcm_request_chan(struct snd_soc_pcm_runtime *rtd,
	struct snd_pcm_substream *substream)
{
	struct snd_dmaengine_dai_dma_data *dma_data;

	dma_data = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	return dma_request_slave_channel(rtd->cpu_dai->dev,
			dma_data->chan_name);
}

static const struct snd_dmaengine_pcm_config sirf_dmaengine_pcm_config = {
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
	.compat_request_channel = sirf_pcm_request_chan,
};

static int sirf_pcm_probe(struct platform_device *pdev)
{
	return devm_snd_dmaengine_pcm_register(&pdev->dev,
		&sirf_dmaengine_pcm_config,
		SND_DMAENGINE_PCM_FLAG_NO_DT |
		SND_DMAENGINE_PCM_FLAG_COMPAT);
}

static struct platform_driver sirf_pcm_driver = {
	.driver = {
		.name = "sirf-pcm-audio",
		.owner = THIS_MODULE,
	},
	.probe = sirf_pcm_probe,
};

static int __init sirf_pcm_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&sirf_pcm_driver);
	if (ret)
		pr_err("failed to register platform driver\n");
	return ret;
}

static void __exit sirf_pcm_exit(void)
{
	platform_driver_unregister(&sirf_pcm_driver);
}

module_init(sirf_pcm_init);
module_exit(sirf_pcm_exit);

MODULE_DESCRIPTION("SiRF PCM audio interface driver");
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
MODULE_LICENSE("GPL v2");
