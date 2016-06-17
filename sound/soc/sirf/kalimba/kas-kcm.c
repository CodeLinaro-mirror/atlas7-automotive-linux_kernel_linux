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
	int ret, num_links, free_links, widget_num, route_num;

	ret = kcm_drv_status();
	if (ret)
		return ret;

	kas_audio_card.dapm_widgets = kcm_get_card_widget(&widget_num);
	kas_audio_card.num_dapm_widgets = widget_num;
	kas_audio_card.dai_link = kcm_get_dai_link(&num_links, &free_links);
	kas_audio_card.num_links = num_links;
	kas_audio_card.dapm_routes = kcm_get_card_route(&route_num);
	kas_audio_card.num_dapm_routes = route_num;
	kas_audio_card.dev = &pdev->dev;
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
