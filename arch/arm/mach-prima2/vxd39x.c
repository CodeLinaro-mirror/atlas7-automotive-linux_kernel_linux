/*
 *
 *opyright (c) 2012 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/memblock.h>

static phys_addr_t sirfsoc_video_codec_phy_base;
static int sirfsoc_video_codec_phy_size;
void __init sirfsoc_video_codec_reserve_memblock(void)
{
	sirfsoc_video_codec_phy_size = 24 * SZ_1M;
	sirfsoc_video_codec_phy_base =
		 memblock_alloc(sirfsoc_video_codec_phy_size, PAGE_SIZE);
	memblock_remove(sirfsoc_video_codec_phy_base,
			sirfsoc_video_codec_phy_size);
}

void sirfsoc_video_codec_get_mem(phys_addr_t *addr, int *size)
{
	*addr = sirfsoc_video_codec_phy_base;
	*size = sirfsoc_video_codec_phy_size;
}
EXPORT_SYMBOL(sirfsoc_video_codec_get_mem);
