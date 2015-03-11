/* CSRatlas7 I2S + CS42xx8 machine driver.  */
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/dmaengine_pcm.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#include "sirf-i2s.h"

struct sirf_hdmi_data {
	int clk_id;
	unsigned int fmt;
	int gpio_rst, gpio_sw1, gpio_sw2, gpio_sw3;
	bool olm;	/* One line mode */
};

static int sirf_hdmi_init(struct snd_soc_pcm_runtime *rtd)
{
	struct sirf_hdmi_data *data =
		snd_soc_card_get_drvdata(rtd->card);

	gpio_set_value_cansleep(data->gpio_rst, 1);

	/* Enable slot#0 audio pins */
	gpio_set_value_cansleep(data->gpio_sw1, 0);
	gpio_set_value_cansleep(data->gpio_sw2, 1);
	if (!data->olm)
		gpio_set_value_cansleep(data->gpio_sw3, 1);

	return 0;
}

/* Per CS42XX8 codec datasheet */
static int rate_to_mclk(struct snd_soc_card *card, unsigned int rate,
		unsigned int *mclk)
{
	*mclk = 0;

	if (rate < 4000 || rate > 200000) {
		dev_err(card->dev, "Unsupported sample rate.\n");
		return -EINVAL;
	}

	if (rate <= 50000)
		*mclk = rate * 1024;	/* 256, 512, 1024 */
	else if (rate <= 100000)
		*mclk = rate * 512;	/* 128, 256, 512 */
	else
		*mclk = rate * 256;	/* 64, 128, 256 */

	return 0;
}

static int sirf_hdmi_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_card *card = rtd->card;
	struct sirf_hdmi_data *data = snd_soc_card_get_drvdata(card);
	unsigned int mclk;
	int ret;

	ret = rate_to_mclk(card, params_rate(params), &mclk);
	if (ret)
		return ret;

	/* Double reference clock as the output mclk is half of it */
	if (snd_soc_dai_set_sysclk(cpu_dai, data->clk_id, mclk * 2,
				SND_SOC_CLOCK_OUT) ||
			snd_soc_dai_set_fmt(cpu_dai, data->fmt)) {
		dev_err(card->dev, "Can't set cpu dai hw params\n");
		return -EINVAL;
	}

	if (snd_soc_dai_set_sysclk(codec_dai, 0, mclk, 0) ||
			snd_soc_dai_set_fmt(codec_dai, data->fmt)) {
		dev_err(card->dev, "Can't set codec dai hw params\n");
		return -EINVAL;
	}

	return 0;
}

static struct snd_soc_ops sirf_hdmi_ops = {
	.hw_params = sirf_hdmi_hw_params,
};

static struct snd_soc_dai_link sirf_hdmi_dai_links[] = {
	{
		.name = "sirf-hdmi",
		.stream_name = "sirf-hdmi",
		.codec_dai_name = "cs42888",
		.init = sirf_hdmi_init,
		.ops = &sirf_hdmi_ops,
	},
};

static struct snd_soc_card snd_soc_sirf_hdmi_card = {
	.name = "sirf-hdmi",
	.owner = THIS_MODULE,
	.dai_link = sirf_hdmi_dai_links,
	.num_links = ARRAY_SIZE(sirf_hdmi_dai_links),
};

static int sirf_hdmi_card_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_sirf_hdmi_card;
	struct snd_soc_dai_link *dl = &sirf_hdmi_dai_links[0];
	struct device_node *np = pdev->dev.of_node;
	struct sirf_hdmi_data *data;
	int ret;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	dl->cpu_of_node = of_parse_phandle(np, "i2s-controller", 0);
	dl->codec_of_node = of_parse_phandle(np, "audio-codec", 0);
	dl->platform_of_node = dl->cpu_of_node;
	if (!dl->cpu_of_node || !dl->codec_of_node)
		return -ENODEV;

	data->clk_id = SIRF_I2S_DTO_CLK;

	data->fmt = SND_SOC_DAIFMT_I2S;
	if (of_get_property(np, "frame-master", NULL))
		data->fmt |= SND_SOC_DAIFMT_CBM_CFM;
	else
		data->fmt |= SND_SOC_DAIFMT_CBS_CFS;

	data->olm = of_device_is_compatible(np, "sirf,hdmi-card-olm");

	/* Request slot#0 audio pins */
	data->gpio_rst = of_get_named_gpio(pdev->dev.of_node, "ext-rst", 0);
	data->gpio_sw1 = of_get_named_gpio(pdev->dev.of_node, "sw1-sel", 0);
	data->gpio_sw2 = of_get_named_gpio(pdev->dev.of_node, "sw2-sel", 0);
	if (!data->olm)
		data->gpio_sw3 = of_get_named_gpio(pdev->dev.of_node,
				"sw3-sel", 0);
	if (!gpio_is_valid(data->gpio_rst) ||
			!gpio_is_valid(data->gpio_sw1) ||
			!gpio_is_valid(data->gpio_sw2) ||
			(!data->olm && !gpio_is_valid(data->gpio_sw3))) {
		dev_err(&pdev->dev, "Failed to parse GPIO pins\n");
		return -EINVAL;
	}
	ret = 0;
	ret |= devm_gpio_request_one(&pdev->dev, data->gpio_rst,
			GPIOF_OUT_INIT_HIGH, "ext-rst");
	ret |= devm_gpio_request_one(&pdev->dev, data->gpio_sw1,
			GPIOF_OUT_INIT_LOW, "sw1-sel");
	ret |= devm_gpio_request_one(&pdev->dev, data->gpio_sw2,
			GPIOF_OUT_INIT_HIGH, "sw2-sel");
	if (!data->olm)
		ret |= devm_gpio_request_one(&pdev->dev, data->gpio_sw3,
				GPIOF_OUT_INIT_HIGH, "sw3-sel");
	if (ret) {
		dev_err(&pdev->dev, "Failed to request GPIO pins\n");
		return -EIO;
	}

	/* Register card */
	card->dev = &pdev->dev;
	snd_soc_card_set_drvdata(card, data);
	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret)
		return ret;
	platform_set_drvdata(pdev, card);

	return 0;
}

static int sirf_hdmi_card_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct sirf_hdmi_data *data = snd_soc_card_get_drvdata(card);

	snd_soc_unregister_card(card);
	gpio_set_value_cansleep(data->gpio_rst, 0);

	return 0;
}

static const struct of_device_id sirf_hdmi_card_of_match[] = {
	{ .compatible = "sirf,hdmi-card", },
	{ .compatible = "sirf,hdmi-card-olm", },
	{},
};
MODULE_DEVICE_TABLE(of, sirf_hdmi_card_of_match);

static struct platform_driver sirf_hdmi_card_driver = {
	.driver = {
		.name = "sirf-hdmi-card",
		.pm = &snd_soc_pm_ops,
		.of_match_table = sirf_hdmi_card_of_match,
	},
	.probe = sirf_hdmi_card_probe,
	.remove = sirf_hdmi_card_remove,
};

static __init int sirf_hdmi_drv_init(void)
{
	return platform_driver_register(&sirf_hdmi_card_driver);
}

static __exit void sirf_hdmi_drv_exit(void)
{
	platform_driver_unregister(&sirf_hdmi_card_driver);
}

late_initcall(sirf_hdmi_drv_init);
module_exit(sirf_hdmi_drv_exit);

MODULE_LICENSE("GPL v2");
