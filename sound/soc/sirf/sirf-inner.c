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
	int last_state;
	int state_changed;
};

struct sirf_inner_card {
	unsigned int            gpio_hp_pa;
	unsigned int            gpio_spk_pa;
	struct platform_device	*sirf_inner_device;
	struct sirf_inner_extcon_info	extcon_info;
};

static int sirf_inner_hp_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *ctrl, int event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct sirf_inner_card *sinner_card = snd_soc_card_get_drvdata(card);
	int on = !SND_SOC_DAPM_EVENT_OFF(event);
	if (gpio_is_valid(sinner_card->gpio_hp_pa))
		gpio_direction_output(sinner_card->gpio_hp_pa, on);
	return 0;
}

static int sirf_inner_spk_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *ctrl, int event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct sirf_inner_card *sinner_card = snd_soc_card_get_drvdata(card);
	int on = !SND_SOC_DAPM_EVENT_OFF(event);

	if (sinner_card->extcon_info.state_changed) {
		sinner_card->extcon_info.state_changed = 0;
		if (!sinner_card->extcon_info.last_state)
			gpio_direction_output(sinner_card->gpio_spk_pa, 0);
		else
			gpio_direction_output(sinner_card->gpio_spk_pa, 1);
	} else {
		if (gpio_is_valid(sinner_card->gpio_spk_pa))
			gpio_direction_output(sinner_card->gpio_spk_pa, on);
	}

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

	sinner_card->sirf_inner_device = platform_device_alloc("soc-audio", -1);
	if (!sinner_card->sirf_inner_device)
		return -ENOMEM;

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

	platform_set_drvdata(sinner_card->sirf_inner_device,
			&snd_soc_sirf_inner_card);

	ret =  platform_device_add(sinner_card->sirf_inner_device);
	if (ret) {
		platform_device_put(sinner_card->sirf_inner_device);
		return ret;
	}

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

	platform_device_put(sinner_card->sirf_inner_device);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sirf_inner_resume(struct device *dev)
{
	struct snd_soc_card *card = dev_get_drvdata(dev);
	struct sirf_inner_card *sinner_card = snd_soc_card_get_drvdata(card);
	struct extcon_dev *edev;
	int state;

	edev = extcon_get_extcon_dev(sinner_card->extcon_info.extcon_data.name);
	state = gpio_get_value(sinner_card->extcon_info.extcon_data.gpio);
	if (state != sinner_card->extcon_info.last_state) {
		sinner_card->extcon_info.state_changed = 1;
		sinner_card->extcon_info.last_state = state;
		extcon_set_state(edev, state);
	}
	return 0;
}

static int sirf_inner_suspend(struct device *dev)
{
	struct snd_soc_card *card = dev_get_drvdata(dev);
	struct sirf_inner_card *sinner_card = snd_soc_card_get_drvdata(card);
	sinner_card->extcon_info.last_state = gpio_get_value(sinner_card->extcon_info.extcon_data.gpio);
	if (gpio_is_valid(sinner_card->gpio_spk_pa))
		gpio_direction_output(sinner_card->gpio_spk_pa, 0);
	return 0;
}
#endif

static const struct of_device_id sirf_inner_of_match[] = {
	{.compatible = "sirf,sirf-inner", },
	{ },
};
MODULE_DEVICE_TABLE(of, sirf_inner_of_match);

static const struct dev_pm_ops sirf_inner_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sirf_inner_suspend, sirf_inner_resume)
};

static struct platform_driver sirf_inner_driver = {
	.driver = {
		.name = "sirf-inner",
		.owner = THIS_MODULE,
		.pm = &sirf_inner_pm_ops,
		.of_match_table = sirf_inner_of_match,
	},
	.probe = sirf_inner_probe,
	.remove = sirf_inner_remove,
};
module_platform_driver(sirf_inner_driver);

MODULE_AUTHOR("RongJun Ying <RongJun.Ying@csr.com>");
MODULE_DESCRIPTION("ALSA SoC SIRF inner AUDIO driver");
MODULE_LICENSE("GPL v2");
