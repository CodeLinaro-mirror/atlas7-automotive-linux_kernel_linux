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

#include <linux/extcon.h>
#include <linux/extcon/extcon-gpio.h>

#define SIRF_JACK_GPIO_DEBOUNCE_TIME	200 /* in ms */
struct sirf_inner_extcon_info {
	struct platform_device pdev;
	struct gpio_extcon_platform_data extcon_data;
};

struct sirf_inner_card {
	unsigned int            gpio_hp_pa;
	unsigned int            gpio_spk_pa;
	struct sirf_inner_extcon_info	extcon_info;
};

/* Digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link sirf_inner_dai_links[] = {
	{
		.name = "SiRF inner",
		.stream_name = "SiRF inner",
		.codec_dai_name = "sirf-soc-inner",
	},
};

static int sirf_inner_headphone_out_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int hp_out = 0;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct snd_soc_card *card = codec->card;
	struct sirf_inner_card *sinner_card = snd_soc_card_get_drvdata(card);

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
	}, {
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
	struct sirf_inner_card *sinner_card;
	int ret;

	sinner_card = devm_kzalloc(&pdev->dev, sizeof(struct sirf_inner_card),
			GFP_KERNEL);
	if (sinner_card == NULL)
		return -ENOMEM;

	sirf_inner_dai_links[0].platform_of_node =
		of_find_compatible_node(NULL, NULL, "sirf,pcm-audio");
	sirf_inner_dai_links[0].cpu_of_node =
		of_parse_phandle(pdev->dev.of_node, "sirf,inner-platform", 0);
	sirf_inner_dai_links[0].codec_of_node =
		of_parse_phandle(pdev->dev.of_node, "sirf,inner-codec", 0);
	sinner_card->gpio_spk_pa = of_get_named_gpio(pdev->dev.of_node,
			"spk-pa-gpios", 0);
	sinner_card->gpio_hp_pa =  of_get_named_gpio(pdev->dev.of_node,
			"hp-pa-gpios", 0);
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
	if (ret < 0)
		return ret;

	sinner_card->extcon_info.extcon_data.name = "h2w";
	sinner_card->extcon_info.extcon_data.debounce =
		SIRF_JACK_GPIO_DEBOUNCE_TIME;
	sinner_card->extcon_info.extcon_data.irq_flags =
		IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_SHARED;
	sinner_card->extcon_info.extcon_data.state_on = "0";
	sinner_card->extcon_info.extcon_data.state_off = "1";
	sinner_card->extcon_info.extcon_data.gpio =
		of_get_named_gpio(pdev->dev.of_node,
		"hp-switch-gpios", 0);

	sinner_card->extcon_info.pdev.name = "extcon-gpio";
	sinner_card->extcon_info.pdev.id = pdev->id;
	sinner_card->extcon_info.pdev.dev.platform_data =
		&sinner_card->extcon_info.extcon_data;

	return platform_device_register(&sinner_card->extcon_info.pdev);
}

static int sirf_inner_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct sirf_inner_card *sinner_card = snd_soc_card_get_drvdata(card);

	if (gpio_is_valid(sinner_card->gpio_hp_pa))
		gpio_free(sinner_card->gpio_hp_pa);
	if (gpio_is_valid(sinner_card->gpio_spk_pa))
		gpio_free(sinner_card->gpio_spk_pa);

	snd_soc_unregister_card(card);
	return 0;
}

#ifdef CONFIG_PM
static int sirf_inner_resume(struct device *dev)
{
	struct snd_soc_card *card = dev_get_drvdata(dev);
	struct sirf_inner_card *sinner_card = snd_soc_card_get_drvdata(card);
	struct extcon_dev *edev;
	int state;

	edev = extcon_get_extcon_dev(sinner_card->extcon_info.extcon_data.name);
	state = gpio_get_value(sinner_card->extcon_info.extcon_data.gpio);
	extcon_set_state(edev, state);

	return 0;
}

static const struct dev_pm_ops sirf_inner_pm_ops = {
	.resume = sirf_inner_resume,
	.restore = sirf_inner_resume,
};
#endif

static const struct of_device_id sirf_inner_of_match[] = {
	{.compatible = "sirf,sirf-inner", },
	{ },
};
MODULE_DEVICE_TABLE(of, sirf_inner_of_match);

static struct platform_driver sirf_inner_driver = {
	.driver = {
		.name = "sirf-inner",
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &sirf_inner_pm_ops,
#endif
		.of_match_table = sirf_inner_of_match,
	},
	.probe = sirf_inner_probe,
	.remove = sirf_inner_remove,
};
module_platform_driver(sirf_inner_driver);

MODULE_AUTHOR("RongJun Ying <RongJun.Ying@csr.com>");
MODULE_DESCRIPTION("ALSA SoC SIRF inner AUDIO driver");
MODULE_LICENSE("GPL v2");
