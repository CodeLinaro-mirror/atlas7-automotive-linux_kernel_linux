/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
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

#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/soc.h>
#include "dsp.h"
#include "kcm/kcm.h"

static struct snd_soc_card kas_audio_card = {
	.name = "kas-audio-card",
	.owner = THIS_MODULE,
	.fully_routed = true,
};

static int kas_audio_probe(struct platform_device *pdev)
{
	int ret, num_links, free_links, widget_num, route_num, val;
	struct device_node *np = pdev->dev.of_node;
	struct kcm_card_data *data;
	const char *fs_mode;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = kcm_drv_status();
	if (ret)
		return ret;

	if (of_get_property(np, "frame-master", NULL))
		data->fmt |= SND_SOC_DAIFMT_CBM_CFM;
	else
		data->fmt |= SND_SOC_DAIFMT_CBS_CFS;

	if (!of_property_read_u32(pdev->dev.of_node, "mclk-fs", &val))
		data->mclk_fs = val;
	else
		data->mclk_fs = 512;

	if (of_property_read_bool(np, "i2s-force-iacc-cap"))
		kcm_force_iacc_cap = true;

	ret = of_property_read_string(pdev->dev.of_node, "i2s-frame-sync-mode",
			&fs_mode);
	if (ret == 0) {
		if (!strcmp("i2s", fs_mode))
			data->fmt |= SND_SOC_DAIFMT_I2S;
		else
			data->fmt |= SND_SOC_DAIFMT_DSP_A;
	} else
		data->fmt |= SND_SOC_DAIFMT_DSP_A;

	kas_audio_card.dapm_widgets = kcm_get_card_widget(&widget_num);
	kas_audio_card.num_dapm_widgets = widget_num;
	kas_audio_card.dai_link = kcm_get_dai_link(&num_links, &free_links);
	kas_audio_card.num_links = num_links;
	kas_audio_card.dapm_routes = kcm_get_card_route(&route_num);
	kas_audio_card.num_dapm_routes = route_num;
	kas_audio_card.dev = &pdev->dev;
	snd_soc_card_set_drvdata(&kas_audio_card, data);
	return devm_snd_soc_register_card(&pdev->dev, &kas_audio_card);
}

static const struct of_device_id kas_audio_match[] = {
	{.compatible = "csr,kas-audio", },
	{},
};
static struct platform_driver kas_audio_driver = {
	.probe = kas_audio_probe,
	.driver = {
		.name = "kas-audio",
		.of_match_table = kas_audio_match,
	},
};
module_platform_driver(kas_audio_driver);

/* Module information */
MODULE_DESCRIPTION("Kalimba audio driver for SiRF A7DA");
MODULE_LICENSE("GPL v2");
