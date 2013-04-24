/*
 * SiRF inner audio device driver
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/jack.h>

#ifdef CONFIG_SND_SIRF_DEBUG
static struct device *dev;
#define debug_info(x...) dev_info(dev, x)
#else
#define debug_info(x...)
#endif

struct sirf_inner_card {
	unsigned int            gpio_hp_pa;
	unsigned int            gpio_spk_pa;

#ifndef CONFIG_ANDROID
	unsigned int            gpio_hp_detect;
	struct snd_soc_jack     hp_jack;
#endif
};

#ifndef CONFIG_ANDROID
static int sirf_inner_jack_status_check(void);

static struct snd_soc_jack_gpio hp_jack_gpios[] = {
	{
		.name = "hpdet-gpio",
		.report = SND_JACK_HEADPHONE,
		.debounce_time = 200,
		.jack_status_check = sirf_inner_jack_status_check,
	},
};

static int sirf_inner_jack_status_check(void)
{
	int spk_out = 0;
	struct snd_soc_codec *codec = hp_jack_gpios[0].jack->codec;
	struct snd_soc_card *card = codec->card;
	struct sirf_inner_card *sinner_card = snd_soc_card_get_drvdata(card);

	if (gpio_is_valid(sinner_card->gpio_hp_detect))
		spk_out = gpio_get_value(sinner_card->gpio_hp_detect);

	debug_info("%s\n", spk_out ? "Headphone plugin" : "Headphone unplugin");

	if (gpio_is_valid(sinner_card->gpio_hp_pa))
		gpio_direction_output(sinner_card->gpio_hp_pa, !spk_out);
	if (gpio_is_valid(sinner_card->gpio_spk_pa))
		gpio_direction_output(sinner_card->gpio_spk_pa, spk_out);
	return SND_JACK_HEADPHONE;
}

static int sirf_inner_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_card *card = codec->card;
	struct sirf_inner_card *sinner_card = snd_soc_card_get_drvdata(card);
	int ret;
	debug_info("%s\n", __func__);
	hp_jack_gpios[0].gpio = sinner_card->gpio_hp_detect;
	ret = snd_soc_jack_new(codec, "Headphone Jack", SND_JACK_HEADPHONE,
			&sinner_card->hp_jack);
	if (ret)
		return ret;
	return snd_soc_jack_add_gpios(&sinner_card->hp_jack,
			ARRAY_SIZE(hp_jack_gpios),
			hp_jack_gpios);
}
#endif

/* Digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link sirf_inner_dai_links[] = {
	{
		.name = "SiRF inner",
		.stream_name = "SiRF inner",
		.codec_dai_name = "sirf-soc-inner",
#ifndef CONFIG_ANDROID
		.init = sirf_inner_init,
#endif
	},
};

static int sirf_inner_headphone_out_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int hp_out = 0;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct snd_soc_card *card = codec->card;
	struct sirf_inner_card *sinner_card = snd_soc_card_get_drvdata(card);
	debug_info("%s\n", __func__);

	if (gpio_is_valid(sinner_card->gpio_hp_pa))
		hp_out = gpio_get_value(sinner_card->gpio_hp_pa);

	*ucontrol->value.integer.value = hp_out;
	return 0;
}

static int sirf_inner_headphone_out_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int is_hp_out = ucontrol->value.integer.value[0];
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct snd_soc_card *card = codec->card;
	struct sirf_inner_card *sinner_card = snd_soc_card_get_drvdata(card);
	debug_info("%s\n", __func__);
	if (gpio_is_valid(sinner_card->gpio_hp_pa))
		gpio_direction_output(sinner_card->gpio_hp_pa, is_hp_out);
	return 0;
}
static int sirf_inner_speaker_out_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int spk_out = 0;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct snd_soc_card *card = codec->card;
	struct sirf_inner_card *sinner_card = snd_soc_card_get_drvdata(card);
	debug_info("%s\n", __func__);

	if (gpio_is_valid(sinner_card->gpio_spk_pa))
		spk_out = gpio_get_value(sinner_card->gpio_spk_pa);

	*ucontrol->value.integer.value = spk_out;
	return 0;
}
static int sirf_inner_speaker_out_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int is_spk_out = ucontrol->value.integer.value[0];
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct snd_soc_card *card = codec->card;
	struct sirf_inner_card *sinner_card = snd_soc_card_get_drvdata(card);
	debug_info("%s\n", __func__);

	if (gpio_is_valid(sinner_card->gpio_spk_pa))
		gpio_direction_output(sinner_card->gpio_spk_pa, is_spk_out);
	return 0;
}
static int sirf_inner_speaker_out_info(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	return 0;
}

static int sirf_inner_headphone_out_info(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	return 0;
}

static struct snd_kcontrol_new snd_sirf_inner_out_route_controls[] = {
	{
		.iface          =       SNDRV_CTL_ELEM_IFACE_MIXER,
		.name           =       "Speaker Out",
		.index          =       0,
		.access         =       SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info           =	sirf_inner_speaker_out_info,
		.get            =       sirf_inner_speaker_out_get,
		.put            =       sirf_inner_speaker_out_put,
	},
	{
		.iface          =       SNDRV_CTL_ELEM_IFACE_MIXER,
		.name           =       "Headphone Out",
		.index          =       0,
		.access         =       SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info           =	sirf_inner_headphone_out_info,
		.get            =       sirf_inner_headphone_out_get,
		.put            =       sirf_inner_headphone_out_put,
	},
};

/* Audio machine driver */
static struct snd_soc_card snd_soc_sirf_inner_card = {
	.name = "SiRF inner",
	.owner = THIS_MODULE,
	.dai_link = sirf_inner_dai_links,
	.num_links = ARRAY_SIZE(sirf_inner_dai_links),
	.controls = snd_sirf_inner_out_route_controls,
	.num_controls = ARRAY_SIZE(snd_sirf_inner_out_route_controls),
};

static int sirf_inner_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_sirf_inner_card;
	int ret;
	struct sirf_inner_card *sinner_card;
#ifdef CONFIG_SND_SIRF_DEBUG
	dev = &pdev->dev;
#endif
	debug_info("%s\n", __func__);
	sinner_card = devm_kzalloc(&pdev->dev, sizeof(struct sirf_inner_card),
			GFP_KERNEL);
	if (sinner_card == NULL)
		return -ENOMEM;

	sirf_inner_dai_links[0].platform_of_node =
		of_find_compatible_node(NULL, NULL, "sirf,pcm-audio");
	sirf_inner_dai_links[0].cpu_of_node =
		of_find_compatible_node(NULL, NULL, "sirf,prima2-audio");
	sirf_inner_dai_links[0].codec_of_node =
		of_find_compatible_node(NULL, NULL, "sirf,prima2-audio");
	sinner_card->gpio_spk_pa = of_get_named_gpio(pdev->dev.of_node,
			"spk-pa-gpios", 0);
	sinner_card->gpio_hp_pa =  of_get_named_gpio(pdev->dev.of_node,
			"hp-pa-gpios", 0);
#ifndef CONFIG_ANDROID
	sinner_card->gpio_hp_detect = of_get_named_gpio(pdev->dev.of_node,
			"hp-switch-gpios", 0);
	debug_info("gpio_hp_detect = %d\n", sinner_card->gpio_hp_detect);
#endif
	debug_info("gpio_spk_pa = %d\n", sinner_card->gpio_spk_pa);
	debug_info("gpio_hp_pa = %d\n", sinner_card->gpio_hp_pa);
	if (gpio_is_valid(sinner_card->gpio_spk_pa))
		gpio_request(sinner_card->gpio_spk_pa, "SPA_PA_SD");
	if (gpio_is_valid(sinner_card->gpio_hp_pa))
		gpio_request(sinner_card->gpio_hp_pa, "HP_PA_SD");

	card->dev = &pdev->dev;
	snd_soc_card_set_drvdata(card, sinner_card);
	platform_set_drvdata(pdev, card);
	if (gpio_is_valid(sinner_card->gpio_hp_pa))
		gpio_direction_output(sinner_card->gpio_hp_pa, 0);
	if (gpio_is_valid(sinner_card->gpio_spk_pa))
		gpio_direction_output(sinner_card->gpio_spk_pa, 0);
	ret = snd_soc_register_card(card);
	if (ret)
		devm_kfree(&pdev->dev, sinner_card);
	return ret;
}

static int sirf_inner_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct sirf_inner_card *sinner_card = snd_soc_card_get_drvdata(card);
#ifdef CONFIG_SND_SIRF_DEBUG
	dev = NULL;
#endif
	if (gpio_is_valid(sinner_card->gpio_hp_pa))
		gpio_free(sinner_card->gpio_hp_pa);
	if (gpio_is_valid(sinner_card->gpio_spk_pa))
		gpio_free(sinner_card->gpio_spk_pa);
	snd_soc_unregister_card(card);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static const struct of_device_id sirf_inner_of_match[] = {
	{.compatible = "sirf,sirf-inner", },
	{ },
};
MODULE_DEVICE_TABLE(of, sirf_inner_of_match);

static struct platform_driver sirf_inner_driver = {
	.driver = {
		.name = "sirf-inner",
		.owner = THIS_MODULE,
		.of_match_table = sirf_inner_of_match,
	},
	.probe = sirf_inner_probe,
	.remove = sirf_inner_remove,
};

module_platform_driver(sirf_inner_driver);
MODULE_AUTHOR("RongJun Ying <RongJun.Ying@csr.com>");
MODULE_DESCRIPTION("ALSA SoC SIRF inner AUDIO driver");
MODULE_LICENSE("GPL");
