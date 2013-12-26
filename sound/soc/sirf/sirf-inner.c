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

struct sirf_inner_card {
	unsigned int            gpio_hp_pa;
	unsigned int            gpio_spk_pa;
};

static int sirf_inner_hp_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *ctrl, int event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct sirf_inner_card *sinner_card = snd_soc_card_get_drvdata(card);
	int on = !SND_SOC_DAPM_EVENT_OFF(event);
	if (gpio_is_valid(sinner_card->gpio_hp_pa))
		gpio_set_value(sinner_card->gpio_hp_pa, on);
	return 0;
}

static int sirf_inner_spk_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *ctrl, int event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct sirf_inner_card *sinner_card = snd_soc_card_get_drvdata(card);
	int on = !SND_SOC_DAPM_EVENT_OFF(event);

	if (gpio_is_valid(sinner_card->gpio_spk_pa))
		gpio_set_value(sinner_card->gpio_spk_pa, on);

	return 0;
}
static const struct snd_soc_dapm_widget sirf_inner_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Hp", sirf_inner_hp_event),
	SND_SOC_DAPM_SPK("Ext Spk", sirf_inner_spk_event),
	SND_SOC_DAPM_MIC("Ext Mic", NULL),
};

static const struct snd_soc_dapm_route intercon[] = {
	{"Hp", NULL, "HPOUTL"},
	{"Hp", NULL, "HPOUTR"},
	{"Ext Spk", NULL, "SPKOUT"},
	{"MICIN1", NULL, "Mic Bias"},
	{"Mic Bias", NULL, "Ext Mic"},
};

/* Digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link sirf_inner_dai_links[] = {
	{
		.name = "SiRF inner",
		.stream_name = "SiRF inner",
		.codec_dai_name = "sirf-soc-inner",
		.platform_name = "sirf-pcm-audio.1",
	},
};

/* Audio machine driver */
static struct snd_soc_card snd_soc_sirf_inner_card = {
	.name = "SiRF inner",
	.owner = THIS_MODULE,
	.dai_link = sirf_inner_dai_links,
	.num_links = ARRAY_SIZE(sirf_inner_dai_links),
	.dapm_widgets = sirf_inner_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sirf_inner_dapm_widgets),
	.dapm_routes = intercon,
	.num_dapm_routes = ARRAY_SIZE(intercon),
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

	sirf_inner_dai_links[0].cpu_of_node =
		of_parse_phandle(pdev->dev.of_node, "sirf,inner-platform", 0);
	sirf_inner_dai_links[0].codec_of_node =
		of_parse_phandle(pdev->dev.of_node, "sirf,inner-codec", 0);
	sinner_card->gpio_spk_pa = of_get_named_gpio(pdev->dev.of_node,
			"spk-pa-gpios", 0);
	sinner_card->gpio_hp_pa =  of_get_named_gpio(pdev->dev.of_node,
			"hp-pa-gpios", 0);
	if (gpio_is_valid(sinner_card->gpio_spk_pa)) {
		ret = devm_gpio_request_one(&pdev->dev,
				sinner_card->gpio_spk_pa,
				GPIOF_OUT_INIT_LOW, "SPA_PA_SD");
		if (ret) {
			dev_err(&pdev->dev,
				"Failed to request GPIO_%d for reset: %d\n",
				sinner_card->gpio_spk_pa, ret);
			return ret;
		}
	}
	if (gpio_is_valid(sinner_card->gpio_hp_pa)) {
		ret = devm_gpio_request_one(&pdev->dev,
				sinner_card->gpio_hp_pa,
				GPIOF_OUT_INIT_LOW, "HP_PA_SD");
		if (ret) {
			dev_err(&pdev->dev,
				"Failed to request GPIO_%d for reset: %d\n",
				sinner_card->gpio_hp_pa, ret);
			return ret;
		}
	}

	card->dev = &pdev->dev;
	snd_soc_card_set_drvdata(card, sinner_card);
	platform_set_drvdata(pdev, card);

	ret = snd_soc_register_card(card);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card() failed:%d\n", ret);

	return 0;
}

static int sirf_inner_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);
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
		.pm = &snd_soc_pm_ops,
		.of_match_table = sirf_inner_of_match,
	},
	.probe = sirf_inner_probe,
	.remove = sirf_inner_remove,
};
module_platform_driver(sirf_inner_driver);

MODULE_AUTHOR("RongJun Ying <RongJun.Ying@csr.com>");
MODULE_DESCRIPTION("ALSA SoC SIRF inner AUDIO driver");
MODULE_LICENSE("GPL v2");
