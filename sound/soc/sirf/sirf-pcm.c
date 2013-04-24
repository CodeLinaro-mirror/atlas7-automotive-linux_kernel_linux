/*
 * ALSA PCM interface for the SiRF SoC
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/sirfsoc_dma.h>

#include <sound/dmaengine_pcm.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/control.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include "sirf-pcm.h"

#ifdef CONFIG_SND_SIRF_DEBUG
static struct device *dev;
#define debug_info(x...) dev_info(dev, x)
#else
#define debug_info(x...)
#endif

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

static int sirf_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sirf_pcm_dma_data *dma_data;
	substream->runtime->hw = sirf_pcm_hardware;
	snd_soc_set_runtime_hwparams(substream, &sirf_pcm_hardware);

	dma_data = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	return snd_dmaengine_pcm_open(substream,
			(dma_filter_fn)sirfsoc_dma_filter_id,
			(void *)(dma_data->dma_req));
}

static int sirf_pcm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sirf_pcm_dma_data *dma_data;
	struct dma_slave_config config;
	struct dma_chan *chan;
	int err = 0;

	dma_data = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	/* return if this is a bufferless transfer e.g.
	 * codec <--> BT codec or GSM modem -- lg FIXME */
	if (!dma_data)
		return 0;

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	runtime->dma_bytes = params_buffer_bytes(params);

	chan = snd_dmaengine_pcm_get_chan(substream);
	if (!chan)
		return -EINVAL;

	/* fills in addr_width and direction */
	err = snd_hwparams_to_dma_slave_config(substream, params, &config);
	if (err)
		return err;

	config.dst_addr_width = DMA_SLAVE_BUSWIDTH_8_BYTES;
	config.src_addr_width = DMA_SLAVE_BUSWIDTH_8_BYTES;

	config.src_addr = runtime->dma_addr;
	config.dst_addr = runtime->dma_addr;
	config.src_maxburst = DMA_SLAVE_BUSWIDTH_8_BYTES;
	config.dst_maxburst = DMA_SLAVE_BUSWIDTH_8_BYTES;

	return dmaengine_slave_config(chan, &config);
}

static int sirf_pcm_hw_free(struct snd_pcm_substream *substream)
{
	snd_pcm_set_runtime_buffer(substream, NULL);
	return 0;
}

static int sirf_pcm_mmap(struct snd_pcm_substream *substream,
		struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	return dma_mmap_coherent(substream->pcm->card->dev, vma,
			runtime->dma_area,
			runtime->dma_addr,
			runtime->dma_bytes);
}

static struct snd_pcm_ops sirf_pcm_ops = {
	.open		= sirf_pcm_open,
	.close		= snd_dmaengine_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= sirf_pcm_hw_params,
	.hw_free	= sirf_pcm_hw_free,
	.trigger	= snd_dmaengine_pcm_trigger,
	.pointer	= snd_dmaengine_pcm_pointer,
	.mmap		= sirf_pcm_mmap,
};

static void sirf_pcm_free_dma_buffers(struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;
	struct snd_dma_buffer *buf;
	int stream;

	for (stream = 0; stream < 2; stream++) {
		substream = pcm->streams[stream].substream;
		if (!substream)
			continue;

		buf = &substream->dma_buffer;
		if (!buf->area)
			continue;

		dma_free_coherent(pcm->card->dev, buf->bytes,
				buf->area, buf->addr);
		buf->area = NULL;
	}
}

static int sirf_pcm_preallocate_dma_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size = sirf_pcm_hardware.buffer_bytes_max;

	buf->dev.type = SNDRV_DMA_TYPE_DEV;
	buf->dev.dev = pcm->card->dev;
	buf->area = dma_alloc_coherent(pcm->card->dev, size,
			&buf->addr, GFP_KERNEL);
	if (!buf->area)
		return -ENOMEM;

	buf->bytes = size;
	debug_info("%s: the buf addr is %#x, the area is %p, the size is %#x\n",
			__func__, buf->addr, buf->area, buf->bytes);

	return 0;
}

static int sirf_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_pcm *pcm = rtd->pcm;
	int ret = 0;

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream) {
		ret = sirf_pcm_preallocate_dma_buffer(pcm,
				SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			goto out;
	}

	if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		ret = sirf_pcm_preallocate_dma_buffer(pcm,
				SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			goto out;
	}

out:
	/* free preallocated buffers in case of error */
	if (ret)
		sirf_pcm_free_dma_buffers(pcm);

	return ret;
}

static struct snd_soc_platform_driver sirf_soc_platform = {
	.ops		= &sirf_pcm_ops,
	.pcm_new	= sirf_pcm_new,
	.pcm_free	= sirf_pcm_free_dma_buffers,
};

static int sirf_pcm_probe(struct platform_device *pdev)
{
#ifdef CONFIG_SND_SIRF_DEBUG
	dev = &pdev->dev;
#endif
	debug_info("%s\n", __func__);
	return snd_soc_register_platform(&pdev->dev,
			&sirf_soc_platform);
}

static int sirf_pcm_remove(struct platform_device *pdev)
{
#ifdef CONFIG_SND_SIRF_DEBUG
	dev = NULL;
#endif
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

static const struct of_device_id sirf_pcm_of_match[] = {
	{ .compatible = "sirf,pcm-audio", },
	{}
};
MODULE_DEVICE_TABLE(of, sirf_pcm_of_match);

static struct platform_driver sirf_pcm_driver = {
	.driver = {
		.name = "sirf-pcm-audio",
		.owner = THIS_MODULE,
		.of_match_table = sirf_pcm_of_match,
	},
	.probe = sirf_pcm_probe,
	.remove = sirf_pcm_remove,
};
module_platform_driver(sirf_pcm_driver);

MODULE_DESCRIPTION("SiRF PCM audio interface driver");
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
MODULE_LICENSE("GPL v2");
