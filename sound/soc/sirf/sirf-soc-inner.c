/*
 * SiRF inner audio codec driver
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.*
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include "sirf-inner-audio-controller.h"

#include "sirf-pcm.h"

#ifdef CONFIG_SND_SIRF_DEBUG
static struct device *dev;
#define debug_info(x...) dev_info(dev, x)
#else
#define debug_info(x...)
#endif

#define SYS_PWR_BASE          0x3000
#define PWRC_SCRATCH_PAD11     0x40
#define PWRC_PDN_CTRL          0x0
#define SYS_PWRC_SCRATCH_PAD11		(SYS_PWR_BASE + PWRC_SCRATCH_PAD11)
#define SYS_PWRC_PDN_CTRL		(SYS_PWR_BASE + PWRC_PDN_CTRL)

#define AUDIO_POWER_EN_BIT     (0xE)

struct sirf_soc_inner_audio {
	void __iomem            *base;
	unsigned int            irq;
	unsigned int            playing;
	struct clk              *clk;
	spinlock_t              lock;
};

#define SIRF_INNER_PLAYBACK_VOLUME(xname, xindex, addr) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
	.info = sirf_inner_playback_volume_info, \
	.get = sirf_inner_playback_volume_get, \
	.put = sirf_inner_playback_volume_put, \
	.private_value = addr }

static int sirf_inner_playback_volume_info(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;

	uinfo->count = 2;
	uinfo->value.integer.min = 0x0;
	uinfo->value.integer.max = 0x7F;
	return 0;
}

static int sirf_inner_playback_volume_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sirf_soc_inner_audio *sinner_audio = dev_get_drvdata(codec->dev);
	int reg = kcontrol->private_value;

	ucontrol->value.integer.value[0] =
		readl(sinner_audio->base + reg) >> 21 & 0x7F;

	ucontrol->value.integer.value[1] =
		readl(sinner_audio->base + reg) >> 14 & 0x7F;

	return 0;
}

static int sirf_inner_playback_volume_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sirf_soc_inner_audio *sinner_audio = dev_get_drvdata(codec->dev);
	unsigned int reg = kcontrol->private_value;
	unsigned int val, val2, val_mask;
	unsigned int old, new;

	val = ucontrol->value.integer.value[0] & 0x7F;
	val_mask = 0x7F << 21;
	val = val << 21;
	val2 = ucontrol->value.integer.value[1] & 0x7F;
	val_mask |= 0x7F << 14;
	val |= val2 << 14;

	old = readl(sinner_audio->base + reg);
	new = (old & ~val_mask) | val;
	if (old != new)
		writel(new, sinner_audio->base + reg);
	return 0;
}

#define SIRF_INNER_RECORD_VOLUME(xname, xindex, addr) \
{                                               \
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
	.info = sirf_inner_record_volume_info, \
	.get = sirf_inner_record_volume_get, \
	.put = sirf_inner_record_volume_put, \
	.private_value = addr \
}

static int sirf_inner_record_volume_info(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;

	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = IC_MIC_MAX_GAIN;
	return 0;
}

static int sirf_inner_record_volume_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sirf_soc_inner_audio *sinner_audio = dev_get_drvdata(codec->dev);
	int reg = kcontrol->private_value;

	ucontrol->value.integer.value[0] =
		readl(sinner_audio->base + reg) >> IC_ADC_LEFT_GAIN_SHIFT &
		IC_ADC_GAIN_MASK;

	ucontrol->value.integer.value[1] =
		readl(sinner_audio->base + reg) >> IC_ADC_RIGHT_GAIN_SHIFT &
		IC_ADC_GAIN_MASK;

	return 0;
}

static int sirf_inner_record_volume_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sirf_soc_inner_audio *sinner_audio = dev_get_drvdata(codec->dev);
	unsigned int reg = kcontrol->private_value;
	unsigned int val, val2, val_mask;
	unsigned int old, new;

	val = ucontrol->value.integer.value[0] & IC_ADC_GAIN_MASK;
	val_mask = IC_ADC_GAIN_MASK << IC_ADC_LEFT_GAIN_SHIFT;
	val = val << IC_ADC_LEFT_GAIN_SHIFT;
	val2 = ucontrol->value.integer.value[1] & IC_ADC_GAIN_MASK;
	val_mask |= IC_ADC_GAIN_MASK << IC_ADC_RIGHT_GAIN_SHIFT;
	val |= val2 << IC_ADC_RIGHT_GAIN_SHIFT;
	/* workaround for minimum catpture volume doesn't mute issue */
	if (val == 0) {
		old = readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
		new = (old & ~0x3);
		if (old != new)
			writel(new, sinner_audio->base + AUDIO_IC_CODEC_CTRL1);

	} else {
		old = readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
		new = (old | 0x3);
		if (old != new)
			writel(new, sinner_audio->base + AUDIO_IC_CODEC_CTRL1);

	}
	old = readl(sinner_audio->base + reg);
	new = (old & ~val_mask) | val;
	if (old != new)
		writel(new, sinner_audio->base + reg);
	return 0;
}

static int sirf_inner_snd_mute_info(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	WARN_ON(!uinfo);
	WARN_ON(!kcontrol);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int sirf_inner_snd_mute_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sirf_soc_inner_audio *sinner_audio = dev_get_drvdata(codec->dev);
	unsigned int reg = kcontrol->private_value;

	ucontrol->value.integer.value[0] = readl(sinner_audio->base + reg) & 0x01;
	ucontrol->value.integer.value[1] = readl(sinner_audio->base + reg) >> 1
		& 0x01;

	return 0;
}

static int sirf_inner_snd_mute_set(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sirf_soc_inner_audio *sinner_audio = dev_get_drvdata(codec->dev);
	unsigned int reg = kcontrol->private_value;
	unsigned int val, val2, val_mask;
	unsigned int old, new;

	val = ucontrol->value.integer.value[0];
	val_mask = 0x01;
	val2 = ucontrol->value.integer.value[1];
	val_mask |= 0x01 << 1;
	val |= val2 << 1;

	old = readl(sinner_audio->base + reg);
	new = (old & ~val_mask) | val;
	if (old != new)
		writel(new, sinner_audio->base + reg);

	return 0;
}
static int sirf_inner_control(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol,
		int get, char *name)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct snd_soc_card *card = codec->card;
	int i;
	for (i = 0; i < card->num_controls; i++) {
		if (!strcmp(card->controls[i].name, name)) {
			if (card->controls[i].get && get)
				return card->controls[i].get(kcontrol, ucontrol);
			else if (card->controls[i].put && !get)
				return card->controls[i].put(kcontrol, ucontrol);
		}
	}
	return 0;
}
static int sirf_inner_snd_speaker_info(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	WARN_ON(!uinfo);
	WARN_ON(!kcontrol);

	debug_info("%s\n", __func__);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;

	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int sirf_inner_snd_speaker_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	debug_info("%s\n", __func__);
	sirf_inner_control(kcontrol, ucontrol, 1, "Speaker Out");
	return 0;
}

static int sirf_inner_snd_speaker_set(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sirf_soc_inner_audio *sinner_audio = dev_get_drvdata(codec->dev);
	unsigned long flags;

	spin_lock_irqsave(&sinner_audio->lock, flags);
	sirf_inner_control(kcontrol, ucontrol, 0, "Speaker Out");

	if (ucontrol->value.integer.value[0]) {
		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0)
				| IC_RDACEN | IC_SPSELR,
				sinner_audio->base + AUDIO_IC_CODEC_CTRL0);

		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1) |
				IC_FIRDAC_LOUT_EN, sinner_audio->base + AUDIO_IC_CODEC_CTRL1);

		writel((readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0) |
					IC_SPEN), sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
	} else {
		writel((readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0)
					& ~(IC_SPEN | IC_SPSELR | IC_HSLEN)),
				sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1)
				& ~IC_FIRDAC_LOUT_EN,
				sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
	}

	spin_unlock_irqrestore(&sinner_audio->lock, flags);

	return 0;
}

static int sirf_inner_snd_headphone_info(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	WARN_ON(!uinfo);
	WARN_ON(!kcontrol);

	debug_info("%s\n", __func__);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int sirf_inner_snd_headphone_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	debug_info("%s\n", __func__);
	sirf_inner_control(kcontrol, ucontrol, 1, "Headphone Out");
	return 0;
}

static int sirf_inner_snd_headphone_set(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sirf_soc_inner_audio *sinner_audio = dev_get_drvdata(codec->dev);
	unsigned long flags;

	spin_lock_irqsave(&sinner_audio->lock, flags);
	sirf_inner_control(kcontrol, ucontrol, 0, "Headphone Out");
	if (ucontrol->value.integer.value[0])
		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0)
				| IC_HSLEN | IC_HSREN | IC_HPRSELR
				| IC_HPLSELL,
				sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
	else
		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0)
				& ~(IC_HSLEN | IC_HSREN | IC_HPRSELR
					| IC_HPLSELL),
				sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
	spin_unlock_irqrestore(&sinner_audio->lock, flags);
	return 0;
}

static struct snd_kcontrol_new snd_sirf_inner_volume_controls[] = {
	SIRF_INNER_PLAYBACK_VOLUME("Speaker Volume", 0, AUDIO_IC_CODEC_CTRL0),
	SIRF_INNER_RECORD_VOLUME("Capture Volume", 1, AUDIO_IC_CODEC_CTRL1),
	{
		.iface          =       SNDRV_CTL_ELEM_IFACE_MIXER,
		.name           =       "Capture Switch",
		.index          =       1,
		.access         =       SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info           =       sirf_inner_snd_mute_info,
		.get            =       sirf_inner_snd_mute_get,
		.put            =       sirf_inner_snd_mute_set,
		.private_value  =       AUDIO_IC_CODEC_CTRL1,
	},
	{
		.iface          =       SNDRV_CTL_ELEM_IFACE_MIXER,
		.name           =       "Speaker Switch",
		.index          =       0,
		.access         =       SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info           =       sirf_inner_snd_speaker_info,
		.get            =       sirf_inner_snd_speaker_get,
		.put            =       sirf_inner_snd_speaker_set,
	},
	{
		.iface          =       SNDRV_CTL_ELEM_IFACE_MIXER,
		.name           =       "Headphone Switch",
		.index          =       0,
		.access         =       SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info           =       sirf_inner_snd_headphone_info,
		.get            =       sirf_inner_snd_headphone_get,
		.put            =       sirf_inner_snd_headphone_set,
	},
};

static int sirf_inner_codec_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct sirf_soc_inner_audio *sinner_audio = snd_soc_dai_get_drvdata(dai);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		sirfsoc_rtc_iobrg_writel(
				sirfsoc_rtc_iobrg_readl(SYS_PWRC_SCRATCH_PAD11) | 0x3,
				SYS_PWRC_SCRATCH_PAD11);

		writel((readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1)
					| IC_CODEC_CLK_EN | IC_POR),
				sinner_audio->base + AUDIO_IC_CODEC_CTRL1);

		writel((readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0) | IC_HSINVEN)
				& ~IC_MONOR,
				sinner_audio->base + AUDIO_IC_CODEC_CTRL0);

		mdelay(50);

		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0) | IC_RDACEN |
				IC_LDACEN | IC_HPRSELR | IC_HPLSELL,
				sinner_audio->base + AUDIO_IC_CODEC_CTRL0);

		writel((readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1) |
					IC_FIRDAC_HSL_EN | IC_FIRDAC_HSR_EN),
				sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
		udelay(300);

		writel((readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0)
					| IC_HSREN | IC_HSLEN),
				sinner_audio->base + AUDIO_IC_CODEC_CTRL0);

		/* avoid break noise when sound loud, set RX gain -1dB */
		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0) &
				~((IC_RXPGAR_MASK << IC_RXPGAR_SHIFT) |
					(IC_RXPGAL_MASK << IC_RXPGAL_SHIFT)),
				sinner_audio->base + AUDIO_IC_CODEC_CTRL0);

		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0) |
				((IC_RXPGAR << IC_RXPGAR_SHIFT) |
				 (IC_RXPGAL << IC_RXPGAL_SHIFT)),
				sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
	} else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		sirfsoc_rtc_iobrg_writel(sirfsoc_rtc_iobrg_readl(
					SYS_PWRC_PDN_CTRL) | (1 << AUDIO_POWER_EN_BIT),
				SYS_PWRC_PDN_CTRL);

		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1) |
				IC_CODEC_CLK_EN | IC_POR,
				sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
		mdelay(50);

		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_PWR) |
				MICBIASEN, sinner_audio->base + AUDIO_IC_CODEC_PWR);
		udelay(300);

		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1)
				| IC_MICINREN | IC_MICINLEN,
				sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
		udelay(100);
		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1) | IC_RADCEN |
				IC_LADCEN, sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
		udelay(100);
		writel((readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1) | IC_MICIN1SEL
					| IC_MICDIFSEL) & (~IC_MICIN2SEL),
				sinner_audio->base + AUDIO_IC_CODEC_CTRL1);

		writel((readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1) &
					~(IC_ADC_GAIN_MASK << IC_ADC_LEFT_GAIN_SHIFT) &
					~(IC_ADC_GAIN_MASK << IC_ADC_RIGHT_GAIN_SHIFT)) |
				(IC_MIC_MAX_GAIN << IC_ADC_LEFT_GAIN_SHIFT) |
				(IC_MIC_MAX_GAIN << IC_ADC_RIGHT_GAIN_SHIFT),
				sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
	}
	return 0;
}

static void sirf_inner_codec_shutdown(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
}


static int sirf_inner_codec_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	return 0;
}

static int sirf_inner_codec_trigger(struct snd_pcm_substream *substream,
		int cmd,
		struct snd_soc_dai *dai)
{
	struct sirf_soc_inner_audio *sinner_audio = snd_soc_dai_get_drvdata(dai);
	int playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	unsigned long irqs;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		local_irq_save(irqs);
		if (playback) {
			spin_lock(&sinner_audio->lock);
			writel(readl(sinner_audio->base + AUDIO_CTRL_IC_CODEC_TX_CTRL)
				& ~IC_TX_ENABLE,
				sinner_audio->base + AUDIO_CTRL_IC_CODEC_TX_CTRL);
			sinner_audio->playing = false;
			writel((readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0)
				& ~(IC_SPEN | IC_SPSELR | IC_HSLEN)),
				sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
			writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1)
				& ~IC_FIRDAC_LOUT_EN,
				sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
			writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0)
				& ~(IC_HSLEN | IC_HSREN | IC_HPRSELR | IC_HPLSELL),
				sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
			spin_unlock(&sinner_audio->lock);
		} else {
			writel(readl(sinner_audio->base + AUDIO_CTRL_IC_CODEC_RX_CTRL)
				& ~IC_RX_ENABLE,
				sinner_audio->base + AUDIO_CTRL_IC_CODEC_RX_CTRL);
		}
		local_irq_restore(irqs);
		break;
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		local_irq_save(irqs);
		if (playback) {
			spin_lock(&sinner_audio->lock);
			writel(0, sinner_audio->base + AUDIO_CTRL_IC_TXFIFO_INT_MSK);
			writel(AUDIO_FIFO_START,
				sinner_audio->base + AUDIO_CTRL_IC_TXFIFO_OP);
			writel(IC_TX_ENABLE,
				sinner_audio->base + AUDIO_CTRL_IC_CODEC_TX_CTRL);
			sinner_audio->playing = true;
			writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0)
				| IC_RDACEN | IC_SPSELR,
				sinner_audio->base + AUDIO_IC_CODEC_CTRL0);

			writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1)
				|IC_FIRDAC_LOUT_EN,
				sinner_audio->base + AUDIO_IC_CODEC_CTRL1);

			writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0)
				| IC_SPEN, sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
			writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0)
				| IC_HSLEN | IC_HSREN
				| IC_HPRSELR | IC_HPLSELL,
				sinner_audio->base
				+ AUDIO_IC_CODEC_CTRL0);
			spin_unlock(&sinner_audio->lock);
		} else {
			/* unmask rx fifo interrupt */
			writel(0, sinner_audio->base
				+ AUDIO_CTRL_IC_RXFIFO_INT_MSK);

				/* First start the FIFO, then enable the tx/rx */
			writel(AUDIO_FIFO_START, sinner_audio->base
				+ AUDIO_CTRL_IC_RXFIFO_OP);
			/* mono capture from dacr*/
			if (substream->runtime->channels == 1)
				writel(0x01, sinner_audio->base
					+ AUDIO_CTRL_IC_CODEC_RX_CTRL);
			else
				writel(IC_RX_ENABLE, sinner_audio->base
					+ AUDIO_CTRL_IC_CODEC_RX_CTRL);
		}
		local_irq_restore(irqs);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

struct snd_soc_dai_ops sirf_inner_codec_dai_ops = {
	.startup = sirf_inner_codec_startup,
	.hw_params = sirf_inner_codec_hw_params,
	.shutdown = sirf_inner_codec_shutdown,
	.trigger = sirf_inner_codec_trigger,
};

struct snd_soc_dai_driver sirf_inner_codec_dai = {
	.name = "sirf-soc-inner",
	.playback = {
		.stream_name = "Audio Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "Audio Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &sirf_inner_codec_dai_ops,
};
EXPORT_SYMBOL_GPL(sirf_inner_codec_dai);

static int sirf_inner_codec_probe(struct snd_soc_codec *codec)
{
	return snd_soc_add_codec_controls(codec, snd_sirf_inner_volume_controls,
			ARRAY_SIZE(snd_sirf_inner_volume_controls));
}

static int sirf_inner_codec_remove(struct snd_soc_codec *codec)
{
	return 0;
}

static struct snd_soc_codec_driver soc_codec_device_sirf_inner_codec = {
	.probe = sirf_inner_codec_probe,
	.remove = sirf_inner_codec_remove,
};

static struct sirf_pcm_dma_data sirf_soc_inner_dai_dma_data[2] = {
	{
		.name = "Audio Playback",
	},
	{
		.name = "Audio Capture",
	}
};

static int sirf_soc_inner_dai_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	snd_soc_dai_set_dma_data(dai, substream,
			&sirf_soc_inner_dai_dma_data[substream->stream]);
	return 0;
}

static int sirf_soc_inner_dai_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	int playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	struct sirf_soc_inner_audio *sinner_audio =
			snd_soc_dai_get_drvdata(dai);
	if (playback) {
		writel(AUDIO_FIFO_RESET,
				sinner_audio->base + AUDIO_CTRL_IC_TXFIFO_OP);

		writel(0x00,
				sinner_audio->base + AUDIO_CTRL_IC_TXFIFO_OP);
	} else {
		writel(AUDIO_FIFO_RESET,
				sinner_audio->base + AUDIO_CTRL_IC_RXFIFO_OP);

		writel(0x00,
				sinner_audio->base + AUDIO_CTRL_IC_RXFIFO_OP);
	}
	return 0;
}

static const struct snd_soc_dai_ops sirf_soc_inner_dai_ops = {
	.startup        = sirf_soc_inner_dai_startup,
	.hw_params      = sirf_soc_inner_dai_hw_params,
};

static struct snd_soc_dai_driver sirf_soc_inner_dai = {
	.name		= "sirf-soc-inner",
	.id			= 0,
	.playback = {
		.stream_name = "inner Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "inner Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &sirf_soc_inner_dai_ops,
};

static int sirf_soc_inner_probe(struct platform_device *pdev)
{
	int ret;
	u32 rx_dma_ch, tx_dma_ch;
	struct sirf_soc_inner_audio *sinner_audio;
	struct resource *mem_res;
#ifdef CONFIG_SND_SIRF_DEBUG
	dev = &pdev->dev;
#endif
	debug_info("%s\n", __func__);
	sinner_audio = devm_kzalloc(&pdev->dev,
		sizeof(struct sirf_soc_inner_audio), GFP_KERNEL);
	if (sinner_audio == NULL)
		return -ENOMEM;
	platform_set_drvdata(pdev, sinner_audio);

	ret = of_property_read_u32(pdev->dev.of_node,
			"sirf,inner-audio-dma-rx-channel", &rx_dma_ch);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to audio capture dma channel\n");
		goto err_dma_rx;
	}
	ret = of_property_read_u32(pdev->dev.of_node,
			"sirf,inner-audio-dma-tx-channel", &tx_dma_ch);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to audio playback dma channel\n");
		goto err_dma_rx;
	}
	sirf_soc_inner_dai_dma_data[0].dma_req = tx_dma_ch;
	sirf_soc_inner_dai_dma_data[1].dma_req = rx_dma_ch;
	debug_info("Record dma channel = %u\n", (unsigned int)rx_dma_ch);
	debug_info("Playback dma channel = %u\n", (unsigned int)tx_dma_ch);

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_res) {
		dev_err(&pdev->dev, "Unable to get IO resource\n");
		ret = -ENODEV;
		goto err_dma_rx;
	}

	sinner_audio->base = devm_ioremap_resource(&pdev->dev, mem_res);
	if (sinner_audio->base == NULL) {
		ret = -ENOMEM;
		goto err_dma_rx;
	}

	sinner_audio->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(sinner_audio->clk)) {
		dev_err(&pdev->dev, "Get clock failed.\n");
		ret = PTR_ERR(sinner_audio->clk);
		goto err_dma_rx;
	}
	clk_prepare_enable(sinner_audio->clk);

	sinner_audio->irq = platform_get_irq(pdev, 0);
	if (sinner_audio->irq < 0) {
		dev_err(&pdev->dev, "Get irq failed.\n");
		ret = -ENXIO;
		goto err_clk_put;
	}
	sinner_audio->playing = false;
	ret = snd_soc_register_dai(&pdev->dev, &sirf_soc_inner_dai);
	if (ret) {
		dev_err(&pdev->dev, "Register Audio SoC dai failed.\n");
		goto err_clk_put;
	}

	ret = snd_soc_register_codec(&(pdev->dev),
			&soc_codec_device_sirf_inner_codec,
			&sirf_inner_codec_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Register Audio Codec dai failed.\n");
		snd_soc_unregister_dai(&pdev->dev);
		return ret;
	}

	spin_lock_init(&sinner_audio->lock);
	writel((readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1)
				| IC_CODEC_CLK_EN),
			sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
	writel((readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1)
			| IC_ADC14B_12),
			sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
	writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0) | IC_CPFREQ,
			sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
	writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0) | IC_CPEN,
			sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
	return 0;

err_clk_put:
	clk_disable_unprepare(sinner_audio->clk);
	clk_put(sinner_audio->clk);
err_dma_rx:
	devm_kfree(&pdev->dev, sinner_audio);
	return ret;
}

static int sirf_soc_inner_remove(struct platform_device *pdev)
{
	struct sirf_soc_inner_audio *sinner_audio;
#ifdef CONFIG_SND_SIRF_DEBUG
	dev = NULL;
#endif
	sinner_audio = platform_get_drvdata(pdev);
	if (sinner_audio)
		devm_kfree(&pdev->dev, sinner_audio);
	platform_set_drvdata(pdev, NULL);
	snd_soc_unregister_codec(&(pdev->dev));
	snd_soc_unregister_dai(&pdev->dev);
	return 0;
}

static const struct of_device_id sirf_soc_inner_of_match[] = {
	{ .compatible = "sirf,prima2-audio", },
	{}
};
MODULE_DEVICE_TABLE(of, sirf_soc_inner_of_match);

static struct platform_driver sirf_soc_inner_driver = {
	.driver = {
		.name = "sirf-soc-inner",
		.owner = THIS_MODULE,
		.of_match_table = sirf_soc_inner_of_match,
	},
	.probe = sirf_soc_inner_probe,
	.remove = sirf_soc_inner_remove,
};

module_platform_driver(sirf_soc_inner_driver);

MODULE_DESCRIPTION("SiRF SoC inner bus and codec driver");
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
MODULE_LICENSE("GPL v2");
