/*
 * Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
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

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "atlas7-iacc.h"

#define IACC_DMA_CHANNELS			4

struct atlas7_dma_data {
	struct dma_chan *chan[IACC_DMA_CHANNELS];
	dma_cookie_t cookie[IACC_DMA_CHANNELS];
};

struct atlas7_pcm_runtime_data {
	struct hrtimer hrt;
	int poll_time_ns;
	struct snd_pcm_substream *substream;
	unsigned int pos;
};

struct atlas7_iacc {
	struct clk *clk;
	struct regmap *regmap;
	struct atlas7_dma_data tx_dma_data;
	struct atlas7_dma_data rx_dma_data;
};

static void atlas7_iacc_tx_enable(struct atlas7_iacc *atlas7_iacc,
	int channels)
{
	int i;

	if (channels == 4)
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

	for (i = 0; i < 4; i++) {
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
	u32 rx_dma_ctrl = 0;

	for (i = 0; i < channels; i++) {
		regmap_update_bits(atlas7_iacc->regmap, INTCODECCTL_TX_RX_EN,
			ADC_EN << i, ADC_EN << i);
		rx_dma_ctrl |= (1 << i);
	}
	regmap_update_bits(atlas7_iacc->regmap, INTCODECCTL_RXFIFO0_OP,
		RX_DMA_CTRL_MASK, rx_dma_ctrl << RX_DMA_CTRL_SHIFT);
	regmap_update_bits(atlas7_iacc->regmap, INTCODECCTL_RXFIFO0_OP,
		FIFO_RESET, FIFO_RESET);
	regmap_update_bits(atlas7_iacc->regmap, INTCODECCTL_RXFIFO0_OP,
		FIFO_RESET, ~FIFO_RESET);
	regmap_write(atlas7_iacc->regmap, INTCODECCTL_RXFIFO0_INT_MSK, 0);
	regmap_update_bits(atlas7_iacc->regmap, INTCODECCTL_RXFIFO0_OP,
		FIFO_START, FIFO_START);
}

static void atlas7_iacc_rx_disable(struct atlas7_iacc *atlas7_iacc)
{
	int i;

	for (i = 0; i < 2; i++)
		regmap_update_bits(atlas7_iacc->regmap, INTCODECCTL_TX_RX_EN,
			ADC_EN << i, 0);
	regmap_write(atlas7_iacc->regmap, INTCODECCTL_RXFIFO0_OP, 0);
}

static int atlas7_iacc_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct atlas7_iacc *atlas7_iacc = snd_soc_dai_get_drvdata(dai);
	int playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		if (playback)
			regmap_update_bits(atlas7_iacc->regmap,
				INTCODECCTL_MODE_CTRL, TX_24BIT, 0);
		else
			regmap_update_bits(atlas7_iacc->regmap,
				INTCODECCTL_MODE_CTRL, RX_24BIT, 0);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		if (playback)
			regmap_update_bits(atlas7_iacc->regmap,
				INTCODECCTL_MODE_CTRL, TX_24BIT, TX_24BIT);
		else
			regmap_update_bits(atlas7_iacc->regmap,
				INTCODECCTL_MODE_CTRL, RX_24BIT, RX_24BIT);
		break;
	default:
		dev_err(dai->dev, "Format unsupported\n");
		return -EINVAL;
	}
	return 0;
}

static int atlas7_iacc_trigger(struct snd_pcm_substream *substream,
		int cmd,
		struct snd_soc_dai *dai)
{
	struct atlas7_iacc *atlas7_iacc = snd_soc_dai_get_drvdata(dai);
	int playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (playback)
			atlas7_iacc_tx_disable(atlas7_iacc);
		else
			atlas7_iacc_rx_disable(atlas7_iacc);
		break;
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (playback)
			atlas7_iacc_tx_enable(atlas7_iacc,
				substream->runtime->channels);
		else
			atlas7_iacc_rx_enable(atlas7_iacc,
				substream->runtime->channels);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

struct snd_soc_dai_ops atlas7_iacc_dai_ops = {
	.hw_params = atlas7_iacc_hw_params,
	.trigger = atlas7_iacc_trigger,
};

#define AUDIO_IF_DAC_RATES	(SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000 \
				| SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000)

#define AUDIO_IF_ADC_RATES	(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 \
				| SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000 \
				| SNDRV_PCM_RATE_96000)

#define AUDIO_IF_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE \
				| SNDRV_PCM_FMTBIT_S24_LE)

static int atlas7_iacc_dai_probe(struct snd_soc_dai *dai)
{
	struct atlas7_iacc *atlas7_iacc = snd_soc_dai_get_drvdata(dai);

	dai->playback_dma_data = &atlas7_iacc->tx_dma_data;
	dai->capture_dma_data = &atlas7_iacc->rx_dma_data;
	return 0;
}

static struct snd_soc_dai_driver atlas7_iacc_dai = {
	.probe = atlas7_iacc_dai_probe,
	.name = "sirf-atlas7-iacc",
	.id = 0,
	.playback = {
		.channels_min = 1,
		.channels_max = 4,
		.rates = AUDIO_IF_DAC_RATES,
		.formats = AUDIO_IF_FORMATS,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = AUDIO_IF_ADC_RATES,
		.formats = AUDIO_IF_FORMATS,
	},
	.ops = &atlas7_iacc_dai_ops,
};

static const struct snd_soc_component_driver atlas7_iacc_component = {
	.name       = "sirf-atlas7-iacc",
};

static int atlas7_iacc_runtime_suspend(struct device *dev)
{
	struct atlas7_iacc *atlas7_atlas7_iacc = dev_get_drvdata(dev);

	clk_disable_unprepare(atlas7_atlas7_iacc->clk);
	return 0;
}

static int atlas7_iacc_runtime_resume(struct device *dev)
{
	struct atlas7_iacc *atlas7_iacc = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(atlas7_iacc->clk);
	if (ret) {
		dev_err(dev, "clk_enable failed: %d\n", ret);
		return ret;
	}
	regmap_write(atlas7_iacc->regmap, INTCODECCTL_MODE_CTRL, 0);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int atlas7_iacc_suspend(struct device *dev)
{
	if (!pm_runtime_status_suspended(dev))
		atlas7_iacc_runtime_suspend(dev);

	return 0;
}

static int atlas7_iacc_resume(struct device *dev)
{
	int ret;

	if (!pm_runtime_status_suspended(dev)) {
		ret = atlas7_iacc_runtime_resume(dev);
		if (ret)
			return ret;
	}
	return 0;
}
#endif

static const struct regmap_config atlas7_iacc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = INTCODECCTL_RXFIFO2_INT_MSK,
	.cache_type = REGCACHE_NONE,
};

static const struct snd_pcm_hardware atlas7_pcm_hardware_playback = {
	.info                   = SNDRV_PCM_INFO_MMAP |
				SNDRV_PCM_INFO_MMAP_VALID |
				SNDRV_PCM_INFO_NONINTERLEAVED |
				SNDRV_PCM_INFO_PAUSE |
				SNDRV_PCM_INFO_RESUME |
				SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.period_bytes_min	= 32,
	.period_bytes_max	= 256 * 1024,
	.periods_min		= 2,
	.periods_max		= 128,
	.buffer_bytes_max	= 512 * 1024, /* 512 kbytes */
	.fifo_size		= 16,
};

static const struct snd_pcm_hardware atlas7_pcm_hardware_capture = {
	.info                   = SNDRV_PCM_INFO_MMAP |
				SNDRV_PCM_INFO_MMAP_VALID |
				SNDRV_PCM_INFO_INTERLEAVED |
				SNDRV_PCM_INFO_PAUSE |
				SNDRV_PCM_INFO_RESUME |
				SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.period_bytes_min	= 32,
	.period_bytes_max	= 256 * 1024,
	.periods_min		= 2,
	.periods_max		= 128,
	.buffer_bytes_max	= 512 * 1024, /* 512 kbytes */
	.fifo_size		= 16,
};

static enum hrtimer_restart snd_hrtimer_callback(struct hrtimer *hrt)
{
	struct atlas7_pcm_runtime_data *aprtd =
		container_of(hrt, struct atlas7_pcm_runtime_data, hrt);
	struct snd_pcm_substream *substream = aprtd->substream;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct atlas7_dma_data *dma_data;
	struct dma_tx_state state;
	enum dma_status status;
	unsigned int buf_size;
	unsigned int pos = 0;
	int channels = substream->runtime->channels;

	dma_data = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		status = dmaengine_tx_status(dma_data->chan[channels - 1],
				dma_data->cookie[channels - 1], &state);
	else
		status = dmaengine_tx_status(dma_data->chan[0],
				dma_data->cookie[0], &state);
	if (status == DMA_IN_PROGRESS || status == DMA_PAUSED) {
		buf_size = snd_pcm_lib_buffer_bytes(substream);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			if (state.residue > 0 &&
				state.residue * channels <= buf_size)
				pos = buf_size - state.residue * channels;
		} else {
			if (state.residue > 0 && state.residue <= buf_size)
				pos = buf_size - state.residue;
		}
	}

	aprtd->pos = pos;
	snd_pcm_period_elapsed(substream);

	hrtimer_forward_now(hrt, ns_to_ktime(aprtd->poll_time_ns));

	return HRTIMER_RESTART;
}

static int atlas7_pcm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct atlas7_pcm_runtime_data *aprtd = runtime->private_data;
	struct atlas7_dma_data *dma_data;

	aprtd->poll_time_ns = 1000000000 / params_rate(params) *
				params_period_size(params);

	dma_data = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
}

static unsigned int buffer_bytes_each_channels(
	struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_buffer_bytes(substream) /
		substream->runtime->channels;
}

static int atlas7_pcm_dma_prep_and_submit(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct dma_chan *chan;
	struct dma_async_tx_descriptor *desc;
	enum dma_transfer_direction direction;
	unsigned long flags = DMA_CTRL_ACK;
	struct atlas7_dma_data *dma_data;
	int i;

	dma_data = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	direction = snd_pcm_substream_to_dma_direction(substream);

	if (!substream->runtime->no_period_wakeup)
		flags |= DMA_PREP_INTERRUPT;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		for (i = 0; i < substream->runtime->channels; i++) {
			chan = dma_data->chan[i];

			desc = dmaengine_prep_dma_cyclic(chan,
				substream->runtime->dma_addr +
				buffer_bytes_each_channels(substream) * i,
				buffer_bytes_each_channels(substream),
				buffer_bytes_each_channels(substream) / 2,
				direction, flags);

			if (!desc)
				return -ENOMEM;

			dma_data->cookie[i] = dmaengine_submit(desc);
		}
	} else {
		desc = dmaengine_prep_dma_cyclic(dma_data->chan[0],
				substream->runtime->dma_addr,
				snd_pcm_lib_buffer_bytes(substream),
				snd_pcm_lib_buffer_bytes(substream) / 2,
				direction, flags);

		if (!desc)
			return -ENOMEM;

		dma_data->cookie[0] = dmaengine_submit(desc);
	}
	return 0;
}

static int atlas7_pcm_hw_free(struct snd_pcm_substream *substream)
{
	snd_pcm_lib_free_pages(substream);
	return 0;
}

static snd_pcm_uframes_t atlas7_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct atlas7_pcm_runtime_data *aprtd = runtime->private_data;

	return bytes_to_frames(substream->runtime, aprtd->pos);
}

static int atlas7_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct atlas7_pcm_runtime_data *aprtd = runtime->private_data;
	struct atlas7_dma_data *dma_data;
	int i;

	dma_data = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	/* With playback, driver wants to start and stop
	 * many dma, each channels have own dma channels.
	 * With capture, only one dma channel wants to start
	 * and stop.
	 */
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		atlas7_pcm_dma_prep_and_submit(substream);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			for (i = 0; i < substream->runtime->channels; i++)
				dma_async_issue_pending(dma_data->chan[i]);
		} else
			dma_async_issue_pending(dma_data->chan[0]);
		hrtimer_start(&aprtd->hrt, ns_to_ktime(aprtd->poll_time_ns),
		      HRTIMER_MODE_REL);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			for (i = 0; i < substream->runtime->channels; i++)
				dmaengine_terminate_all(dma_data->chan[i]);
		else
			dmaengine_terminate_all(dma_data->chan[0]);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int atlas7_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct atlas7_pcm_runtime_data *aprtd;
	const struct snd_pcm_hardware *ppcm;
	int ret;

	aprtd = kzalloc(sizeof(*aprtd), GFP_KERNEL);
	if (aprtd == NULL)
		return -ENOMEM;
	runtime->private_data = aprtd;

	aprtd->substream = substream;
	hrtimer_init(&aprtd->hrt, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	aprtd->hrt.function = snd_hrtimer_callback;

	ppcm = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
			&atlas7_pcm_hardware_playback
			: &atlas7_pcm_hardware_capture;
	snd_soc_set_runtime_hwparams(substream, ppcm);

	ret = snd_pcm_hw_constraint_integer(runtime,
			SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		return ret;

	return 0;
}

static int atlas7_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct atlas7_pcm_runtime_data *aprtd = runtime->private_data;

	hrtimer_cancel(&aprtd->hrt);
	kfree(aprtd);

	return 0;
}

static struct snd_pcm_ops atlas7_pcm_iacc_ops = {
	.open		= atlas7_pcm_open,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= atlas7_pcm_hw_params,
	.hw_free	= atlas7_pcm_hw_free,
	.trigger	= atlas7_pcm_trigger,
	.pointer	= atlas7_pcm_pointer,
	.close		= atlas7_pcm_close,
};

static int atlas7_pcm_iacc_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;
	int ret;

	ret = dma_coerce_mask_and_coherent(card->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream) {
		ret =  snd_pcm_lib_preallocate_pages(
			pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream,
			SNDRV_DMA_TYPE_DEV, card->dev,
			atlas7_pcm_hardware_playback.buffer_bytes_max,
			atlas7_pcm_hardware_playback.buffer_bytes_max);
		if (ret)
			return ret;
	}

	if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		ret =  snd_pcm_lib_preallocate_pages(
			pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream,
			SNDRV_DMA_TYPE_DEV, card->dev,
			atlas7_pcm_hardware_capture.buffer_bytes_max,
			atlas7_pcm_hardware_capture.buffer_bytes_max);

		if (ret)
			return ret;
	}

	return 0;
}

static struct snd_soc_platform_driver atlas7_iacc_soc_platform = {
	.ops		= &atlas7_pcm_iacc_ops,
	.pcm_new	= atlas7_pcm_iacc_new,
};

static int atlas7_iacc_probe(struct platform_device *pdev)
{
	int ret, i;
	struct atlas7_iacc *atlas7_iacc;
	struct clk *clk;
	struct resource *mem_res;
	void __iomem *base;
	struct dma_chan *chan;
	char tx_dma_name[16];
	struct dma_slave_config rx_dma_cfg = {
		.src_maxburst = 2,
	};

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

	for (i = 0; i < IACC_DMA_CHANNELS; i++) {
		sprintf(tx_dma_name, "tx%d", i);
		chan = dma_request_slave_channel_reason(&pdev->dev,
			tx_dma_name);
		if (IS_ERR(chan)) {
			dev_err(&pdev->dev, "Request playback dma channel failed.\n");
			return PTR_ERR(chan);
		}

		atlas7_iacc->tx_dma_data.chan[i] = chan;
	}
	chan = dma_request_slave_channel_reason(&pdev->dev, "rx");
	if (IS_ERR(chan)) {
		dev_err(&pdev->dev, "Request playback dma channel failed.\n");
		ret = PTR_ERR(chan);
		goto out;
	}

	dmaengine_slave_config(chan, &rx_dma_cfg);
	atlas7_iacc->rx_dma_data.chan[0] = chan;

	atlas7_iacc->clk = clk;

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = atlas7_iacc_runtime_resume(&pdev->dev);
		if (ret)
			goto out;
	}

	ret = devm_snd_soc_register_component(&pdev->dev,
			&atlas7_iacc_component,
			&atlas7_iacc_dai, 1);
	if (ret)
		goto out;

	platform_set_drvdata(pdev, atlas7_iacc);
	ret = devm_snd_soc_register_platform(&pdev->dev,
		&atlas7_iacc_soc_platform);
	if (ret < 0)
		goto out;

	return 0;

out:
	for (i = 0; i < IACC_DMA_CHANNELS; i++) {
		if (atlas7_iacc->tx_dma_data.chan[i])
			dma_release_channel(atlas7_iacc->tx_dma_data.chan[i]);
	}
	dma_release_channel(atlas7_iacc->rx_dma_data.chan[0]);
	return ret;
}

static int atlas7_iacc_remove(struct platform_device *pdev)
{
	struct atlas7_iacc *atlas7_iacc = platform_get_drvdata(pdev);
	int i;

	if (!pm_runtime_enabled(&pdev->dev))
		atlas7_iacc_runtime_suspend(&pdev->dev);
	else
		pm_runtime_disable(&pdev->dev);

	for (i = 0; i < IACC_DMA_CHANNELS; i++)
		dma_release_channel(atlas7_iacc->tx_dma_data.chan[i]);
	dma_release_channel(atlas7_iacc->rx_dma_data.chan[0]);

	return 0;
}

static const struct of_device_id atlas7_iacc_of_match[] = {
	{ .compatible = "sirf,atlas7-iacc", },
	{}
};
MODULE_DEVICE_TABLE(of, atlas7_iacc_of_match);

static const struct dev_pm_ops atlas7_iacc_pm_ops = {
	SET_RUNTIME_PM_OPS(atlas7_iacc_runtime_suspend,
		atlas7_iacc_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(atlas7_iacc_suspend,
		atlas7_iacc_resume)
};

static struct platform_driver atlas7_iacc_driver = {
	.driver = {
		.name = "sirf-atlas7-iacc",
		.owner = THIS_MODULE,
		.of_match_table = atlas7_iacc_of_match,
		.pm = &atlas7_iacc_pm_ops,
	},
	.probe = atlas7_iacc_probe,
	.remove = atlas7_iacc_remove,
};

module_platform_driver(atlas7_iacc_driver);

MODULE_DESCRIPTION("SiRF ATLAS7 IACC(internal audio codec cotroller) driver");
MODULE_LICENSE("GPL v2");
