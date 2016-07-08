/*
 * Atlas7 NoC support
 *
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

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/signal.h>
#include <linux/clk.h>
#include "noc.h"
#include "trace.h"

static struct noc_macro *s_cpum;
#ifdef CONFIG_ATLAS7_NOC_FW
struct noc_macro *s_ddrm;
struct noc_macro *s_rtcm;
struct noc_macro *s_audiom;
#endif


/*handler noc macro interrupt*/
static irqreturn_t noc_irq_handle(int irq, void *data)
{
	struct noc_macro *nocm = (struct noc_macro *)data;

	if (nocm->errlogoff)
		noc_dump_errlog(nocm);

	noc_handle_probe(nocm);

	return IRQ_HANDLED;
}

static int noc_abort_handler(unsigned long addr, unsigned int fsr,
		struct pt_regs *regs)
{
	int ret;

	ret = noc_dump_errlog(s_cpum);
	if (0 != ret)
		return 1;
	/*
	* If it was not an imprecise abort (Bit10==0),
	* then we need to correct the
	* return address to be _after_ the instruction.
	*/
	if (!(fsr & (1 << 10)))
		regs->ARM_pc += 4;

	return 0;
}

static int noc_macro_parse(struct noc_macro *nocm)
{
	struct platform_device *pdev = nocm->pdev;
	struct device_node *np = pdev->dev.of_node;
	struct of_phandle_args regofs;

	/*spramfw/dramfw not have 'regofs' fields like other macros range*/
	if (of_parse_phandle_with_fixed_args(np, "regofs", 4, 0, &regofs)) {
#ifdef CONFIG_ATLAS7_NOC_FW
		if (strstr(nocm->name, "dramfw"))
			noc_dramfw_init(nocm);
		else if (strstr(nocm->name, "spramfw"))
			noc_spramfw_init(nocm);
		else if (strstr(nocm->name, "ntfw"))
			ntfw_init(nocm);
#endif
		goto out;
	}

	nocm->errlogoff = regofs.args[0];
	nocm->faultenoff = regofs.args[1];
	nocm->regfwoff = regofs.args[2];
	nocm->schedoff = regofs.args[3];
#ifdef CONFIG_ATLAS7_NOC_FW
	noc_regfw_init(nocm);
#endif
out:
	return 0;
}


static int noc_macro_init(struct noc_macro *nocm)
{
	int ret = 0;
	struct platform_device *pdev = nocm->pdev;

	/*by default btm noc clock is down, so turn it on before operate*/
	nocm->clk = devm_clk_get(&pdev->dev, "nocm");
	if (!IS_ERR(nocm->clk)) {
		ret = clk_prepare_enable(nocm->clk);
		if (ret) {
			dev_err(&pdev->dev, "%s: clk_prepare_enable %d!\n",
					__func__, ret);
			return ret;
		}
	}
	/* ignore qos on pxp for lack some modules*/
	if (!of_machine_is_compatible("sirf,atlas7-pxp")) {
		ret = noc_qos_init(nocm);
		if (ret)
			goto err;
	}

	ret = noc_probe_init(nocm);
	if (ret)
		dev_info(&pdev->dev, "%s:no probe\n", nocm->name);

	/*enable errlog trigger, thus irq/abort could come*/
	noc_macro_parse(nocm);
	ret = of_irq_get(pdev->dev.of_node, 0);
	if (ret <= 0) {
		dev_info(&pdev->dev,
			"Unable to find IRQ number. ret=%d\n", ret);
		return 0;
	}
	nocm->irq = ret;
	ret = devm_request_irq(&pdev->dev,
			nocm->irq,
			noc_irq_handle,
			0,
			nocm->name, nocm);
	if (ret)
		dev_info(&pdev->dev, "err: devm_request_irq %s: ret=%d\n",
			nocm->name, ret);

	noc_errlog_enable(nocm);
	return 0;
err:
	if (!IS_ERR(nocm->clk))
		clk_disable_unprepare(nocm->clk);
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int noc_pm_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct noc_macro *nocm = platform_get_drvdata(pdev);

	if (!IS_ERR(nocm->clk))
		clk_disable_unprepare(nocm->clk);

	noc_probe_suspend(nocm);

	return 0;
}

static int noc_pm_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct noc_macro *nocm = platform_get_drvdata(pdev);

	if (!IS_ERR(nocm->clk))
		clk_prepare_enable(nocm->clk);

	noc_probe_resume(nocm);

	return 0;
}

static const struct dev_pm_ops noc_pm_ops = {
	.suspend_late = noc_pm_suspend,
	.resume_early = noc_pm_resume,
};

#endif

static int sirfsoc_noc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct noc_macro *nocm = NULL;
	int ret;

	/* just for trace point compiling test*/
	dev_info(&pdev->dev, "%s %s\n", __func__,
		of_node_full_name(np));
	trace_noc_bw_data(of_node_full_name(np), 0);

	nocm = devm_kzalloc(&pdev->dev,
			sizeof(struct noc_macro),
			GFP_KERNEL);
	if (!nocm)
		return -ENOMEM;

	nocm->name = strrchr(of_node_full_name(np), '/') + 1;
	nocm->mbase = of_iomap(np, 0);
	if (!nocm->mbase) {
		dev_err(&pdev->dev, "%s: of_iomap error\n", nocm->name);
		return -ENOMEM;
	}

	nocm->pdev = pdev;
	ret = noc_macro_init(nocm);
	if (ret)
		goto err;

	spin_lock_init(&nocm->lock);
	platform_set_drvdata(pdev, nocm);
	if (strstr(nocm->name, "cpum"))
		s_cpum = nocm;
#ifdef CONFIG_ATLAS7_NOC_FW
	else if (strstr(nocm->name, "ddrm"))
		s_ddrm = nocm;
	else if (strstr(nocm->name, "rtcm"))
		s_rtcm = nocm;
	else if (strstr(nocm->name, "audiom"))
		s_audiom = nocm;
#endif
	dev_dbg(&pdev->dev, "initialized nocm:%s, %d\n",
		nocm->name, !!nocm->errlogoff);

	return 0;
err:
	iounmap(nocm->mbase);
	return ret;

}

static int __init noc_hook_abort(void)
{
	if (!of_machine_is_compatible("sirf,atlas7"))
		return 0;

	/*noc can generate these aborts to CA7*/
	hook_fault_code(8, noc_abort_handler, SIGBUS, 0,
		"external abort on non-linefetch");

	hook_fault_code(22, noc_abort_handler, SIGBUS, 0,
		"imprecise external abort");

	return 0;
}

arch_initcall(noc_hook_abort);

static const struct of_device_id sirfsoc_nocfw_ids[] = {
	{ .compatible = "sirf,noc-macro", .data = 0 },
	{ .compatible = "sirf,atlas7-ntfw", .data = 0 },
	{},
};

static struct platform_driver sirfsoc_noc_driver = {
	.driver = {
		   .name = "sirf-noc",
		   .of_match_table = sirfsoc_nocfw_ids,
#ifdef CONFIG_PM_SLEEP
		   .pm = &noc_pm_ops,
#endif
		   },
	.probe = sirfsoc_noc_probe,
};


module_platform_driver(sirfsoc_noc_driver);
