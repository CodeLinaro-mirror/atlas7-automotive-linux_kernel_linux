/*
 *
 *opyright (c) 2012 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/memblock.h>

int sirfsoc_video_codec_phy_base = 0;

void __init sirfsoc_video_codec_reserve_memblock(void)
{
	int sirfsoc_video_codec_phy_size = 32 * SZ_1M;
	sirfsoc_video_codec_phy_base = memblock_alloc(sirfsoc_video_codec_phy_size, PAGE_SIZE);
	memblock_remove(sirfsoc_video_codec_phy_base, sirfsoc_video_codec_phy_size);
}

int sirfsoc_video_codec_get_mem(void)
{
	return sirfsoc_video_codec_phy_base;
}
EXPORT_SYMBOL(sirfsoc_video_codec_get_mem);
