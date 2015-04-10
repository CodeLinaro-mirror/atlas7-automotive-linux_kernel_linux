#ifndef _KAS_DSP_H
#define _KAS_DSP_H

struct ipc_data;

struct kalimba {
	struct regmap *regmap;
	struct clk *clk_kas;
	struct clk *clk_audmscm;
	struct clk *clk_gpum;
	struct ipc_data *ipc_data;
#ifdef CONFIG_SND_SOC_SIRF_KALIMBA_DEBUG
	struct device *debug_dev;
#endif
};
#endif /* _KAS_DSP_H */
