/*
 * Defines machines for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/memblock.h>
#include <asm/sizes.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include "common.h"

static int __init sirf_fdt_handle_pre_rsv_mem(unsigned long node, const char *uname,
                                int depth, void *data)
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

void __init sirfsoc_reserve(void)
{
	sirfsoc_pre_reserve();
	sirfsoc_nand_reserve_memblock();
	sirfsoc_fb_reserve_memblock();
	sirfsoc_vip_reserve_memblock();
}

void __init prima2_reserve(void)
{
	sirfsoc_reserve();
	sirfsoc_video_codec_reserve_memblock();
}

static void __init sirfsoc_init_mach(void)
{
	sirfsoc_of_rstc_init();
	of_platform_populate(NULL, of_default_bus_match_table,
		NULL, NULL);

	platform_device_register_simple("cpufreq-cpu0", -1, NULL, 0);
}

static void __init sirfsoc_init_late(void)
{
	sirfsoc_pm_init();
	sirfsoc_nand_nosave_memblock();
}

static __init void sirfsoc_map_io(void)
{
	sirfsoc_map_lluart();
	sirfsoc_map_scu();
}

#ifdef CONFIG_ARCH_ATLAS6
static const char *atlas6_dt_match[] __initdata = {
	"sirf,atlas6",
	NULL
};

DT_MACHINE_START(ATLAS6_DT, "Generic ATLAS6 (Flattened Device Tree)")
	/* Maintainer: Barry Song <baohua.song@csr.com> */
	.reserve	= sirfsoc_reserve,
	.map_io         = sirfsoc_map_io,
	.init_machine	= sirfsoc_init_mach,
	.init_late	= sirfsoc_init_late,
	.dt_compat      = atlas6_dt_match,
	.restart	= sirfsoc_restart,
MACHINE_END
#endif

#ifdef CONFIG_ARCH_PRIMA2
static const char *prima2_dt_match[] __initdata = {
	"sirf,prima2",
	NULL
};

DT_MACHINE_START(PRIMA2_DT, "Generic PRIMA2 (Flattened Device Tree)")
	/* Maintainer: Barry Song <baohua.song@csr.com> */
	.reserve	= prima2_reserve,
	.map_io         = sirfsoc_map_io,
	.init_machine   = sirfsoc_init_mach,
	.dma_zone_size	= SZ_256M,
	.init_late	= sirfsoc_init_late,
	.dt_compat      = prima2_dt_match,
	.restart	= sirfsoc_restart,
MACHINE_END
#endif

#ifdef CONFIG_ARCH_MARCO
static const char *marco_dt_match[] __initdata = {
	"sirf,marco",
	NULL
};

DT_MACHINE_START(MARCO_DT, "Generic MARCO (Flattened Device Tree)")
	/* Maintainer: Barry Song <baohua.song@csr.com> */
	.reserve	= sirfsoc_reserve,
	.smp            = smp_ops(sirfsoc_smp_ops),
	.map_io         = sirfsoc_map_io,
	.init_machine   = sirfsoc_init_mach,
	.init_late	= sirfsoc_init_late,
	.dt_compat      = marco_dt_match,
	.restart	= sirfsoc_restart,
MACHINE_END
#endif
