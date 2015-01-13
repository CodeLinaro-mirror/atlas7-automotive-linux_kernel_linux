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
#include <asm/mach/map.h>
#include "common.h"

static struct gpio_extcon_platform_data h2w_extcon_data;

#ifdef CONFIG_NANDDISK
#define NANDDISK_PHY_BASE 0x46000000UL
static struct map_desc sirfsoc_nanddisk_map[] __initdata = {
	 { /* nanddisk */
		 .virtual = 0xC6000000,
		 .pfn = __phys_to_pfn(NANDDISK_PHY_BASE),
		 .length = SZ_2M,
		 .type = MT_MEMORY_RWX,
	 },
};
#endif
static int __init sirf_fdt_handle_pre_rsv_mem(unsigned long node,
	const char *uname, int depth, void *data)
{
	const __be32 *mem_info;
	int len;
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

static void __init sirfsoc_reserve(void)
{
	sirfsoc_pre_reserve();
	sirfsoc_gps_reserve_memblock();
	sirfsoc_pbb_reserve_memblock();
}

static void __init prima2_reserve(void)
{
	sirfsoc_reserve();
	sirfsoc_video_codec_reserve_memblock();
}

/* specific device names for some device node */
static struct of_dev_auxdata sirf_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("pwm-backlight", 0, "sirf-backlight", NULL),
	{ /* end */ },
};

static void __init sirfsoc_init_mach(void)
{
	of_platform_populate(NULL, of_default_bus_match_table,
		sirf_auxdata_lookup, NULL);

	platform_device_register_simple("cpufreq-cpu0", -1, NULL, 0);
	platform_device_register_simple("bt-sco", -1, NULL, 0);

	sirfsoc_noc_init();
}

static void __init sirfsoc_init_late(void)
{
	struct device_node *np;

	sirfsoc_pm_init();
	sirfsoc_gps_nosave_memblock();
	sirfsoc_pbb_nosave_memblock();

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
	debug_ll_io_init();
#if defined(CONFIG_CSRVISOR_DUALOS) && defined(CONFIG_SECURITY_MODE)
	iotable_init(sirfsoc_csrvisor_map, ARRAY_SIZE(sirfsoc_csrvisor_map));
#endif

#ifdef CONFIG_NANDDISK
	iotable_init(sirfsoc_nanddisk_map, ARRAY_SIZE(sirfsoc_nanddisk_map));
#endif
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
	.l2c_aux_val	= 0,
	.l2c_aux_mask	= ~0,
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
	.l2c_aux_val	= 0,
	.l2c_aux_mask	= ~0,
	.map_io         = sirfsoc_map_io,
	.init_irq	= sirfsoc_init_irq,
	.init_machine   = sirfsoc_init_mach,
	.dma_zone_size	= SZ_256M,
	.init_late	= sirfsoc_init_late,
	.dt_compat      = prima2_dt_match,
MACHINE_END
#endif

#ifdef CONFIG_ARCH_ATLAS7
static const char *atlas7_dt_match[] __initconst = {
	"sirf,atlas7",
	NULL
};

DT_MACHINE_START(ATLAS7_DT, "Generic ATLAS7 (Flattened Device Tree)")
	/* Maintainer: Barry Song <baohua.song@csr.com> */
	.smp            = smp_ops(sirfsoc_smp_ops),
	.map_io         = sirfsoc_map_io,
	.init_machine   = sirfsoc_init_mach,
	.init_late	= sirfsoc_init_late,
	.dt_compat      = atlas7_dt_match,
MACHINE_END
#endif
