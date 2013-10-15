/*
 * ALSA PCM interface for the SiRF SoC
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/sirfsoc_dma.h>
#include <sound/dmaengine_pcm.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

static struct snd_pcm_hardware sirf_pcm_hardware = {
	.info                   = (SNDRV_PCM_INFO_MMAP
			| SNDRV_PCM_INFO_MMAP_VALID
			| SNDRV_PCM_INFO_INTERLEAVED
			| SNDRV_PCM_INFO_BLOCK_TRANSFER
			| SNDRV_PCM_INFO_RESUME
			| SNDRV_PCM_INFO_PAUSE),
	.formats                = (SNDRV_PCM_FMTBIT_S16_LE),
	.rates                  = (SNDRV_PCM_RATE_48000),
	.rate_min               = 512,
	.rate_max               = 115200,
	.channels_min           = 1,
	.channels_max           = 2,
	.buffer_bytes_max       = 64 * 1024,
	.period_bytes_min       = 128,
	.period_bytes_max       = 32 * 1024,
	.periods_min            = 2,
	.periods_max            = 2,
};

static bool filter(struct dma_chan *chan, void *param)
{
	struct snd_dmaengine_dai_dma_data *dma_data = param;

	if (!sirfsoc_dma_filter_id(chan, dma_data->filter_data))
		return false;

	chan->private = dma_data->filter_data;

	return true;
}

static const struct snd_dmaengine_pcm_config sirf_dmaengine_pcm_config = {
	.pcm_hardware = &sirf_pcm_hardware,
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
	.compat_filter_fn = filter,
	.prealloc_buffer_size = 64 * 1024,
};

static int sirf_pcm_probe(struct platform_device *pdev)
{
	return snd_dmaengine_pcm_register(&pdev->dev,
		&sirf_dmaengine_pcm_config,
		SND_DMAENGINE_PCM_FLAG_NO_DT |
		SND_DMAENGINE_PCM_FLAG_COMPAT);
}

static int sirf_pcm_remove(struct platform_device *pdev)
{
	snd_dmaengine_pcm_unregister(&pdev->dev);
	return 0;
}

static struct platform_driver sirf_pcm_driver = {
	.driver = {
		.name = "sirf-pcm-audio",
		.owner = THIS_MODULE,
	},
	.probe = sirf_pcm_probe,
	.remove = sirf_pcm_remove,
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
