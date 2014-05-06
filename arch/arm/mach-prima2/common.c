/*
 * Defines machines for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/irqchip.h>
#include <linux/interrupt.h>
#include <linux/memblock.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/extcon/extcon-gpio.h>
#include <linux/dma-mapping.h>
#include <linux/dma-contiguous.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/sizes.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include "common.h"

static struct gpio_extcon_platform_data h2w_extcon_data;
static struct device fake_cma_dev;

static int __init sirf_fdt_handle_pre_rsv_mem(unsigned long node,
	const char *uname, int depth, void *data)
{
	__be32 *mem_info;
	unsigned long len;
	unsigned int rc_addr, rc_sz, dqs_addr, dqs_sz;

	mem_info = of_get_flat_dt_prop(node,
		"rc-range", &len);
	if (!mem_info || (len != 2 * sizeof(unsigned long)))
		return 0;

	rc_addr = be32_to_cpu(mem_info[0]);
	rc_sz = be32_to_cpu(mem_info[1]);

	if (memblock_reserve(rc_addr, rc_sz))
		pr_err("failed to reserve romcode memory(0x%x bytes at 0x%x)\n",
			rc_addr, rc_sz);

	mem_info = of_get_flat_dt_prop(node,
		"dqs-range", &len);
	if (!mem_info || (len != 2 * sizeof(unsigned long)))
		return 0;

	dqs_addr = be32_to_cpu(mem_info[0]);
	dqs_sz = be32_to_cpu(mem_info[1]);

	if (memblock_reserve(dqs_addr, dqs_sz))
		pr_err("failed to reserve dqs memory(0x%x bytes at 0x%x)\n",
			dqs_addr, dqs_sz);

	return 1;
}

/*
 * FIXME: kernel memblock reserve for:
 *      1. sdram init training dqs,
 *      2. SiRFsoc romcode page table,
 * so here reserve the space so as not to let kernel access.
 */
void __init sirfsoc_pre_reserve(void)
{
	if (!of_scan_flat_dt(sirf_fdt_handle_pre_rsv_mem, NULL))
		pr_err("failed to find reserved memory.\n");
}

#ifdef CONFIG_SECURITY_MODE
#define SWITCH_TO_NON_SECURE 0

static void smc_switch_to_non_secure(void)
{
	__asm__ __volatile__(".arch_extension sec\n\t"
		"mov r0, %0\n\t"
		"smc #0\n\t" :
		: "I"(SWITCH_TO_NON_SECURE)
		: "r0", "memory");
}
#endif

static void __init csrvisor_reserve(void)
{
#ifdef CONFIG_SECURITY_MODE
#define CSRVISOR_PHY_BASE 0x5FC00000UL
	memblock_reserve(CSRVISOR_PHY_BASE, SZ_1M);
	arm_pm_idle = smc_switch_to_non_secure;
#endif
}

static int __init sirfsoc_fdt_handle_fb_rsv_mem(unsigned long node,
						const char *uname,
						int depth, void *data)
{
	__be32 *mem_info;
	unsigned long len;

	mem_info = of_get_flat_dt_prop(node,
					"sirf,rsvmem_size", &len);
	if (!mem_info || (len != 4 * sizeof(unsigned long)))
		return 0;

	/* assume the max reserve size of fb0 is 8M(1024*600,32bpp,tri-buf) */
	*((unsigned long *)data) = 8 * SZ_1M + be32_to_cpu(mem_info[1]) +
		be32_to_cpu(mem_info[2]) + be32_to_cpu(mem_info[3]);

	return 1;
}

static int __init sirfsoc_fdt_handle_vip_rsv_mem(unsigned long node,
						const char *uname,
						int depth, void *data)
{
	__be32 *mem_info;
	unsigned long len;

	mem_info = of_get_flat_dt_prop(node,
				"sirf,vip_cma_size", &len);
	if (!mem_info || (len != sizeof(unsigned long)))
		return 0;

	*((unsigned long *)data) = be32_to_cpu(mem_info[0]);

	return 1;
}

static void __init sirfsoc_reserve_cma(void)
{
	int ret;
	unsigned long rsv_size = 0, size;

	if (!of_scan_flat_dt(sirfsoc_fdt_handle_fb_rsv_mem, &rsv_size))
		pr_err("failed to get fb reserved memory size from dt\n");
	size = rsv_size;

	rsv_size = 0;
	if (!of_scan_flat_dt(sirfsoc_fdt_handle_vip_rsv_mem, &rsv_size))
		pr_err("failed to get vip reserved memory size from dt\n");
	size += rsv_size;

	ret = dma_declare_contiguous(&fake_cma_dev, size, 0, 0xFFFFFFFF);
	if (ret)
		pr_err("%s: failed to reserve cma %d\n", __func__, ret);
}

void __init sirfsoc_reserve(void)
{
	csrvisor_reserve();
	sirfsoc_pre_reserve();
	sirfsoc_nand_reserve_memblock();
	sirfsoc_gps_reserve_memblock();
	sirfsoc_pbb_reserve_memblock();
	sirfsoc_reserve_cma();
}

void __init prima2_reserve(void)
{
	sirfsoc_reserve();
	sirfsoc_video_codec_reserve_memblock();
}

void __init atlas7_reserve(void)
{
	csrvisor_reserve();
}

/* specific device names for some device node */
static struct of_dev_auxdata sirf_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("pwm-backlight", 0, "sirf-backlight", NULL),
	{ /* end */ },
};

static void __init sirfsoc_set_up_cma_areas(void)
{
	struct platform_device *pdev;
	struct device_node *np;
	struct cma *cma;

	/* wrap lcd's cma area with fake device's */
	np = of_find_compatible_node(NULL, NULL, "sirf,prima2-lcd");
	if (!np || !of_device_is_available(np)) {
		pr_err("failed to get lcd device node\n");
		return;
	}
	pdev = of_find_device_by_node(np);
	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	cma = dev_get_cma_area(&fake_cma_dev);
	dev_set_cma_area(&pdev->dev, cma);

	/* wrap vip's cma area with fake device's */
	np = of_find_compatible_node(NULL, NULL, "sirf,prima2-vip");
	if (!np || !of_device_is_available(np)) {
		pr_err("failed to get vip device node\n");
		return;
	}
	pdev = of_find_device_by_node(np);
	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	cma = dev_get_cma_area(&fake_cma_dev);
	dev_set_cma_area(&pdev->dev, cma);
}

static void __init sirfsoc_init_mach(void)
{
#ifdef CONFIG_VIDEO_SIRFSOC_VIP
	sirfsoc_add_camera_pdev();
#endif
	of_platform_populate(NULL, of_default_bus_match_table,
		sirf_auxdata_lookup, NULL);

	platform_device_register_simple("cpufreq-cpu0", -1, NULL, 0);
	platform_device_register_simple("bt-sco", -1, NULL, 0);
	sirfsoc_set_up_cma_areas();
}

static void __init sirfsoc_init_late(void)
{
	struct device_node *np;

	sirfsoc_pm_init();
	sirfsoc_gps_nosave_memblock();
	sirfsoc_pbb_nosave_memblock();
	sirfsoc_nand_nosave_memblock();

	np = of_find_node_by_path("/sound");
	if (!np) {
		pr_err("No sound node found\n");
		return;
	}

	h2w_extcon_data.name = "h2w";
	h2w_extcon_data.debounce = 200;
	h2w_extcon_data.irq_flags = IRQF_TRIGGER_RISING |
		IRQF_TRIGGER_FALLING | IRQF_SHARED;
	h2w_extcon_data.state_on = "1";
	h2w_extcon_data.state_off = "0";
	h2w_extcon_data.check_on_resume = true;
	h2w_extcon_data.gpio_active_low = true;
	h2w_extcon_data.gpio =
		of_get_named_gpio(np, "hp-switch-gpios", 0);

	platform_device_register_data(&platform_bus, "extcon-gpio", -1,
		&h2w_extcon_data, sizeof(struct gpio_extcon_platform_data));

	of_node_put(np);

}

static __init void sirfsoc_map_io(void)
{
	sirfsoc_map_lluart();
	sirfsoc_map_scu();
}

static void __init sirfsoc_init_irq(void)
{
	l2x0_of_init(0, 0xfdffffff);
	irqchip_init();
}

#ifdef CONFIG_ARCH_ATLAS6
static const char *atlas6_dt_match[] __initconst = {
	"sirf,atlas6",
	NULL
};

DT_MACHINE_START(ATLAS6_DT, "Generic ATLAS6 (Flattened Device Tree)")
	/* Maintainer: Barry Song <baohua.song@csr.com> */
	.reserve	= sirfsoc_reserve,
	.map_io         = sirfsoc_map_io,
	.init_irq	= sirfsoc_init_irq,
	.init_machine	= sirfsoc_init_mach,
	.init_late	= sirfsoc_init_late,
	.dt_compat      = atlas6_dt_match,
MACHINE_END
#endif

#ifdef CONFIG_ARCH_PRIMA2
static const char *prima2_dt_match[] __initconst = {
	"sirf,prima2",
	NULL
};

DT_MACHINE_START(PRIMA2_DT, "Generic PRIMA2 (Flattened Device Tree)")
	/* Maintainer: Barry Song <baohua.song@csr.com> */
	.reserve	= prima2_reserve,
	.map_io         = sirfsoc_map_io,
	.init_irq	= sirfsoc_init_irq,
	.init_machine   = sirfsoc_init_mach,
	.dma_zone_size	= SZ_256M,
	.init_late	= sirfsoc_init_late,
	.dt_compat      = prima2_dt_match,
MACHINE_END
#endif

#ifdef CONFIG_ARCH_MARCO
static const char *marco_dt_match[] __initconst = {
	"sirf,marco",
	NULL
};

DT_MACHINE_START(MARCO_DT, "Generic MARCO (Flattened Device Tree)")
	/* Maintainer: Barry Song <baohua.song@csr.com> */
	.reserve	= sirfsoc_reserve,
	.smp            = smp_ops(sirfsoc_smp_ops),
	.map_io         = sirfsoc_map_io,
	.init_irq	= sirfsoc_init_irq,
	.init_machine   = sirfsoc_init_mach,
	.init_late	= sirfsoc_init_late,
	.dt_compat      = marco_dt_match,
MACHINE_END
#endif

#ifdef CONFIG_ARCH_ATLAS7
static const char *atlas7_dt_match[] __initdata = {
	"sirf,atlas7",
	NULL
};

DT_MACHINE_START(ATLAS7_DT, "Generic ATLAS7 (Flattened Device Tree)")
	/* Maintainer: Barry Song <baohua.song@csr.com> */
	.reserve	= atlas7_reserve,
	.smp            = smp_ops(sirfsoc_smp_ops),
	.map_io         = sirfsoc_map_io,
	.init_machine   = sirfsoc_init_mach,
	.init_late	= sirfsoc_init_late,
	.dt_compat      = atlas7_dt_match,
MACHINE_END
#endif
