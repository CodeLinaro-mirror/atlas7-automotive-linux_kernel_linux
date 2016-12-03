/*
 * kalimba firmware driver
 *
 * Copyright (c) 2015, 2016 The Linux Foundation. All rights reserved.
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
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "debug.h"
#include "ps.h"
#include "regs.h"

#define KAS_ADDR_CONST16 0x007F9A
#define KAS_ADDR_CONST32 0x007F9C

struct firmware_code_head {
	int code_size;
	int pm_unpacker_offset;
	int pm_offset;
	int dm1_offset;
	int dm2_offset;
	int const16_offset;
	int const32_offset;
};

struct firmware_pm_unpacker_image {
	/* Start address of parameter block in DM2 */
	u32 params_start_addr;
	/* Start address to load block in PM */
	u32 pm_unpacker_start_addr;
	/* pm unpacker size */
	u32 pm_unpacker_size;
	/* PM program image data */
	u32 pm_unpacker_code;
};

struct firmware_pm_dm_block {
	u32 start_addr;
	u32 size;
	u32 data;
};

struct firmware_code {
	struct firmware_code_head head;
	void *code;
	dma_addr_t code_dma_addr;
};

static void *const16_data_virt_addr;
static dma_addr_t const16_data_phy_addr;
static int const16_data_size;
static void *const32_data_virt_addr;
static dma_addr_t const32_data_phy_addr;
static int const32_data_size;
static struct regmap *kalimba_regs_regmap;

static u32 read_kalimba_reg(u32 reg_addr)
{
	u32 val;

	regmap_read(kalimba_regs_regmap, reg_addr, &val);
	return val;
}

static void write_kalimba_reg(u32 reg_addr, u32 val)
{
	regmap_write(kalimba_regs_regmap, reg_addr, val);
}

static void update_bits_kalimba_reg(u32 reg_addr, u32 mask, u32 val)
{
	regmap_update_bits(kalimba_regs_regmap, reg_addr, mask, val);
}

static void firmware_run_pm(u32 start_addr)
{
	if (!(start_addr >= KAS_PM_SRAM_START_ADDR &&
			start_addr <= KAS_PM_SRAM_END_ADDR)) {
		pr_err("%s: the start address(0x%x) is not correct.\n",
			__func__, start_addr);
		return;
	}
	write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR, (KAS_DEBUG << 2) | (0x2 << 30));
	write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, KAS_DEBUG_STOP);

	/* Set KAS program counter */
	write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR,
			(KAS_REGFILE_PC << 2) | (0x2 << 30));
	write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, start_addr);

	write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR, (KAS_DEBUG << 2) | (0x2 << 30));
	write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, KAS_DEBUG_RUN);
	write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR,
			(KAS_REGFILE_PC << 2) | (0x2 << 30));
	start_addr = read_kalimba_reg(KAS_CPU_KEYHOLE_DATA);
}

void firmware_stop_pm(void)
{
	write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR, (KAS_DEBUG << 2) | (0x2 << 30));
	write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, KAS_DEBUG_STOP);
	write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR,
			(KAS_REGFILE_PC << 2) | (0x2 << 30));
	write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, 0);

}

static void firmware_run_pm_unpacker(u32 dm_block_src, u32 pm_block_dest,
		u32 pm_bytes_len, u32 params_start_addr,
		u32 pm_unpacker_image_start_addr)
{
	u32 unpack_status;

	write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR, (KAS_DEBUG << 2) | (0x2 << 30));
	write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, KAS_DEBUG_STOP);

	write_kalimba_reg(KAS_CPU_KEYHOLE_MODE, 4);
	write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR,
			(PM_UNPACKER_PARAMS_DM_SRC(params_start_addr) <<
			 2) | (0x2 << 30));
	write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, dm_block_src);
	write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, pm_block_dest / 4);
	write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, pm_bytes_len / 4);
	/* IPC_TRGT.lo16 */
	write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, 0);
	/* IPC_TRGT.hi16 */
	write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, 0);
	/* UNPACK_STATUS */
	write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, 0);

	/* Set KAS program counter */
	firmware_run_pm(pm_unpacker_image_start_addr);

	/* Wait PM-unpacker complete */
	write_kalimba_reg(KAS_CPU_KEYHOLE_MODE, 0);
	write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR,
			(PM_UNPACKER_PARAMS_UNPACK_STATUS(params_start_addr) <<
			 2) | (0x2 << 30));
	do {
		unpack_status = read_kalimba_reg(KAS_CPU_KEYHOLE_DATA);
	} while (!(unpack_status & 1));
}

static u32 firmware_download_pm_unpacker(struct firmware_code *code,
	u32 *pm_unpacker_start_addr)
{
	u32 i;
	struct firmware_pm_unpacker_image *pm_unpacker_image;
	u32 size;
	u32 *pm_unpacker_code;

	pm_unpacker_image = code->code + code->head.pm_unpacker_offset;
	*pm_unpacker_start_addr = pm_unpacker_image->pm_unpacker_start_addr;
	size = pm_unpacker_image->pm_unpacker_size;
	pm_unpacker_code = &pm_unpacker_image->pm_unpacker_code;

	write_kalimba_reg(KAS_CPU_KEYHOLE_MODE, 4);
	write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR,
			*pm_unpacker_start_addr | (0x3 << 30));
	for (i = 0; i < size; i++)
		write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, pm_unpacker_code[i]);

	return pm_unpacker_image->params_start_addr;
}

static void firmware_load_pm_though_keyhole(u32 start_addr, u32 size_bytes,
	void *data)
{
	u32 i;
	u32 *code = (u32 *)data;

	write_kalimba_reg(KAS_CPU_KEYHOLE_MODE, 4);
	write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR, start_addr | (0x3 << 30));
	for (i = 0; i < size_bytes / 4; i++)
		write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, code[i]);
}

static void firmware_load_pm_though_dma(struct firmware_code *code,
		u32 start_addr, u32 size_bytes, void *data)
{
	void *pm_code_dma_addr;
	u32 val;

	pm_code_dma_addr = (void *)code->code_dma_addr + (data - code->code);

	do {
		val = read_kalimba_reg(KAS_DMA_STATUS);
	} while (!(val & KAS_DMAC_IDLE));

	write_kalimba_reg(KAS_DMAC_DMA_XLEN, size_bytes / 4);
	write_kalimba_reg(KAS_DMAC_DMA_WIDTH, size_bytes / 4);
	write_kalimba_reg(KAS_DMA_ADDR, start_addr / 4);
	write_kalimba_reg(KAS_DMAC_DMA_ADDR, (u32)pm_code_dma_addr);

	do {
		val = read_kalimba_reg(KAS_DMAC_DMA_INT);
	} while (!(val & KAS_DMAC_FINISH_INT));
	val = read_kalimba_reg(KAS_DMAC_DMA_CUR_DATA_ADDR);
	update_bits_kalimba_reg(KAS_DMAC_DMA_INT,
			KAS_DMAC_FINISH_INT, KAS_DMAC_FINISH_INT);
}

static void firmware_init_dma(u32 transfer_mode)
{
	/* Reset DMA client */
	write_kalimba_reg(KAS_DMAC_DMA_VALID, 1);
	update_bits_kalimba_reg(KAS_DMA_MODE, KAS_RESET_DMA_CLIENT,
		KAS_RESET_DMA_CLIENT);
	update_bits_kalimba_reg(KAS_DMA_MODE, KAS_RESET_DMA_CLIENT, 0);

	/* Set KAS DMA as Single transaction */
	update_bits_kalimba_reg(KAS_DMA_MODE, KAS_DMA_CHAIN_MODE, 0);

	write_kalimba_reg(KAS_TRANSLATE, transfer_mode);
	write_kalimba_reg(KAS_DMAC_DMA_CTRL, KAS_DMAC_TRANS_MEM_TO_FIFO);
	write_kalimba_reg(KAS_DMAC_DMA_YLEN, 0);
	write_kalimba_reg(KAS_DMAC_DMA_INT_EN, 0);
	write_kalimba_reg(KAS_DMA_INC, 0);
	write_kalimba_reg(KAS_DMAC_DMA_INT, 0xFFFFFFFF);
}

static void firmware_download_pm(struct firmware_code *code)
{
#define UP_ALIGN_12BYTES(x)	((x + 11) / 12 * 12)
#define DOWN_ALIGN_12BYTES(x)	(x / 12 * 12)
	u32 i;
	u32 pm_unpacker_start_addr;
	u32 params_start_addr;
	void *pm_image = code->code + code->head.pm_offset;
	u32 block_num = ((u32 *)pm_image)[0];
	struct firmware_pm_dm_block *pm_block_ptr =
		pm_image + sizeof(u32);

	/* Stop pm running */
	write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR,	(KAS_DEBUG << 2) | (0x2 << 30));
	write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, KAS_DEBUG_STOP);

	params_start_addr =
		firmware_download_pm_unpacker(code, &pm_unpacker_start_addr);

	firmware_init_dma(KAS_TRANSLATE_24BIT);

	/* The KAS's DMA must transfer multiples-of-12 bytes */
	for (i = 0; i < block_num; i++) {
		u32 current_start_addr;
		int remaining_bytes;
		void *data = &pm_block_ptr->data;

		current_start_addr = pm_block_ptr->start_addr;
		remaining_bytes = pm_block_ptr->size * 4;
		while (remaining_bytes > 0) {
			if (remaining_bytes < 12) {
				/* If remaining_bytes is less than 12 bytes,
				 * use the keyhole to download
				 */
				firmware_load_pm_though_keyhole(
						current_start_addr,
						remaining_bytes, data);
				remaining_bytes = 0;
			} else if (current_start_addr >=
				DOWN_ALIGN_12BYTES(pm_unpacker_start_addr)) {
				/* If the start address overlaps the
				 * PM-unpacker area,use the keyhole to download
				 */
				firmware_load_pm_though_keyhole(
						current_start_addr,
						remaining_bytes, data);
				remaining_bytes = 0;
			} else {
				u32 dma_transfer_bytes;
				/* Round-up to multiples-of-12 bytes */
				dma_transfer_bytes =
					UP_ALIGN_12BYTES(remaining_bytes) >
					DOWN_ALIGN_12BYTES(KAS_DM1_SRAM_BYTES)
					? DOWN_ALIGN_12BYTES(
							KAS_DM1_SRAM_BYTES)
					: UP_ALIGN_12BYTES(remaining_bytes);
				if ((current_start_addr  + dma_transfer_bytes)
					>= DOWN_ALIGN_12BYTES(
					pm_unpacker_start_addr)) {
					dma_transfer_bytes =
						DOWN_ALIGN_12BYTES(
							pm_unpacker_start_addr)
						- current_start_addr;
					}
				/* If the current_start_addr is near the
				 * pm-unpacker start address, then use
				 * the keyhole to download remaining bytes
				 */
				if (dma_transfer_bytes < 12) {
					firmware_load_pm_though_keyhole(
						current_start_addr,
						remaining_bytes, data);
					break;
				}

				firmware_load_pm_though_dma(code,
						KAS_DM1_SRAM_START_ADDR,
						dma_transfer_bytes, data);
				firmware_run_pm_unpacker(
						KAS_DM1_SRAM_START_ADDR,
						current_start_addr,
						dma_transfer_bytes,
						params_start_addr,
						pm_unpacker_start_addr);

				current_start_addr += dma_transfer_bytes;
				remaining_bytes -= dma_transfer_bytes;
				data += dma_transfer_bytes;
			}
		}

		pm_block_ptr = (void *)(&pm_block_ptr->data +
				pm_block_ptr->size);
	}
}

static void firmware_download_dm(struct firmware_code *code, void *dm_image)
{
	u32 block_num = ((u32 *)dm_image)[0];
	struct firmware_pm_dm_block *dm_block_ptr =
		dm_image + sizeof(u32);
	u32 i;
	u32 val;
	void *dm_code_dma_addr;

	firmware_init_dma(KAS_TRANSLATE_24BIT_RIGHT_ALIGNED);

	for (i = 0; i < block_num; i++) {
		dm_code_dma_addr = (void *)code->code_dma_addr +
			((void *)&dm_block_ptr->data -
			 code->code);
		do {
			val = read_kalimba_reg(KAS_DMA_STATUS);
		} while (!(val & KAS_DMAC_IDLE));

		write_kalimba_reg(KAS_DMAC_DMA_XLEN, dm_block_ptr->size);
		write_kalimba_reg(KAS_DMAC_DMA_WIDTH, dm_block_ptr->size);
		write_kalimba_reg(KAS_DMA_ADDR,	dm_block_ptr->start_addr);
		write_kalimba_reg(KAS_DMAC_DMA_ADDR, (u32) dm_code_dma_addr);

		do {
			val = read_kalimba_reg(KAS_DMAC_DMA_INT);
		} while (!(val & KAS_DMAC_FINISH_INT));
		update_bits_kalimba_reg(KAS_DMAC_DMA_INT,
				KAS_DMAC_FINISH_INT, KAS_DMAC_FINISH_INT);

		dm_block_ptr = (((void *)dm_block_ptr) + 8
				+ dm_block_ptr->size * sizeof(u32));
	}

}

static int firmware_download_const(struct firmware_code *code,
	void *const_image, int kas_addr_ptr)
{
	int size;
	u32 start_addr_dm[2];
	void *virt_addr;
	dma_addr_t phy_addr;

	/* If the old constant data is exists, free this memory firstly */
	if (kas_addr_ptr == KAS_ADDR_CONST16 && const16_data_virt_addr) {
		dma_free_coherent(NULL, const16_data_size,
			const16_data_virt_addr, const16_data_phy_addr);
		const16_data_virt_addr = NULL;
	} else if (kas_addr_ptr == KAS_ADDR_CONST32 && const32_data_virt_addr) {
		dma_free_coherent(NULL, const32_data_size,
			const32_data_virt_addr, const32_data_phy_addr);
		const32_data_virt_addr = NULL;
	}

	if (kas_addr_ptr == KAS_ADDR_CONST16)
		size = (code->code - code->head.const16_offset)
			 - (code->code - code->head.const32_offset);
	else if (kas_addr_ptr == KAS_ADDR_CONST32)
		size = (code->head.code_size - code->head.const32_offset);

	virt_addr = dma_alloc_coherent(NULL, size, &phy_addr, GFP_KERNEL);
	if (virt_addr == NULL) {
		dev_err(NULL, "Alloc dram failed.\n");
		return -ENOMEM;
	}
	/* Copy the data into the memory */
	memcpy(virt_addr, const_image, size);

	start_addr_dm[0] = phy_addr >> 24;
	start_addr_dm[1] = phy_addr;

	/* Update kas pointers in DM*/
	write_kalimba_reg(KAS_CPU_KEYHOLE_MODE, 4);
	write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR,
			(kas_addr_ptr << 2) | (0x2 << 30));
	write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, start_addr_dm[0]);
	write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, start_addr_dm[1]);

	if (kas_addr_ptr == KAS_ADDR_CONST16) {
		const16_data_size = size;
		const16_data_phy_addr = phy_addr;
		const16_data_virt_addr = virt_addr;
	} else if (kas_addr_ptr == KAS_ADDR_CONST32) {
		const32_data_size = size;
		const32_data_phy_addr = phy_addr;
		const32_data_virt_addr = virt_addr;
	}
	return 0;
}

static void firmware_download_code(struct firmware_code *code)
{
	firmware_download_pm(code);

	firmware_download_dm(code, code->code + code->head.dm1_offset);
	firmware_download_dm(code, code->code + code->head.dm2_offset);
	firmware_download_const(code, code->code + code->head.const16_offset,
					KAS_ADDR_CONST16);
	firmware_download_const(code, code->code + code->head.const32_offset,
					KAS_ADDR_CONST32);
}

static void dump_pm_codes(u32 __user *buffer)
{
	u32 i;
	u32 val;

	write_kalimba_reg(KAS_CPU_KEYHOLE_MODE, 4);
	write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR, 0 | 0x3 << 30);
	for (i = 0; i < KAS_PM_SRAM_SIZE; i++) {
		val = read_kalimba_reg(KAS_CPU_KEYHOLE_DATA);
		put_user(val, buffer + i);
	}
}

int firmware_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	struct firmware_code code;
	u32 start_addr, length, i;
	void *data;

	switch (cmd) {
	case IOCTL_KALIMBA_WRITE_PM:
		get_user(start_addr, (u32 __user *)arg);
		get_user(length, (u32 __user *)(arg + 4));
		if (!(start_addr >= KAS_PM_SRAM_START_ADDR &&
			start_addr <= KAS_PM_SRAM_END_ADDR))
			return -EINVAL;
		if (length > (KAS_PM_SRAM_END_ADDR - start_addr + 1))
			return -EINVAL;
		data = kmalloc(length, GFP_KERNEL);
		if (!data)
			return -ENOMEM;
		if (copy_from_user(data, (void __user *)(arg + 8),
				length)) {
			dev_err(dev, "Get PM code failed.\n");
			kfree(data);
			return -EINVAL;
		}
		firmware_load_pm_though_keyhole(start_addr, length, data);
		kfree(data);
		break;
	case IOCTL_KALIMBA_READ_PM:
		get_user(start_addr, (u32 __user *)arg);
		get_user(length, (u32 __user *)(arg + 4));
		if (!(start_addr >= KAS_PM_SRAM_START_ADDR &&
			start_addr <= KAS_PM_SRAM_END_ADDR))
			return -EINVAL;
		if (length > (KAS_PM_SRAM_END_ADDR - start_addr + 1))
			return -EINVAL;
		arg += 8;
		write_kalimba_reg(KAS_CPU_KEYHOLE_MODE, 4);
		write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR,
			start_addr | (0x3 << 30));
		for (i = 0; i < length / 4; i++) {
			u32 tmp;

			tmp = read_kalimba_reg(KAS_CPU_KEYHOLE_DATA);
			put_user(tmp, (u32 __user *)(arg + i * 4));
		}
		break;
	case IOCTL_KALIMBA_WRITE_DM:
		get_user(start_addr, (u32 __user *)arg);
		get_user(length, (u32 __user *)(arg + 4));
		if (start_addr >= KAS_DM1_SRAM_START_ADDR &&
			start_addr <= KAS_DM1_SRAM_END_ADDR) {
			if (length > (KAS_DM1_SRAM_END_ADDR - start_addr + 1))
				return -EINVAL;
		} else if (start_addr >= KAS_DM2_SRAM_START_ADDR &&
			start_addr <= KAS_DM2_SRAM_END_ADDR) {
			if (length > (KAS_DM2_SRAM_END_ADDR - start_addr + 1))
				return -EINVAL;
		} else
			return -EINVAL;
		arg += 8;
		write_kalimba_reg(KAS_CPU_KEYHOLE_MODE, 4);
		write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR,
			(start_addr << 2) | (0x2 << 30));
		for (i = 0; i < length / 4; i++) {
			u32 tmp;

			get_user(tmp, (u32 __user *)(arg + i * 4));
			write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, tmp);
		}
		break;
	case IOCTL_KALIMBA_READ_DM:
		get_user(start_addr, (u32 __user *)arg);
		get_user(length, (u32 __user *)(arg + 4));
		if (start_addr >= KAS_DM1_SRAM_START_ADDR &&
			start_addr <= KAS_DM1_SRAM_END_ADDR) {
			if (length > (KAS_DM1_SRAM_END_ADDR - start_addr + 1))
				return -EINVAL;
		} else if (start_addr >= KAS_DM2_SRAM_START_ADDR &&
			start_addr <= KAS_DM2_SRAM_END_ADDR) {
			if (length > (KAS_DM2_SRAM_END_ADDR - start_addr + 1))
				return -EINVAL;
		} else
			return -EINVAL;
		arg += 8;
		write_kalimba_reg(KAS_CPU_KEYHOLE_MODE, 4);
		write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR,
			(start_addr << 2) | (0x2 << 30));
		for (i = 0; i < length / 4; i++) {
			u32 tmp;

			tmp = read_kalimba_reg(KAS_CPU_KEYHOLE_DATA);
			put_user(tmp, (u32 __user *)(arg + i * 4));
		}
		break;
	case IOCTL_KALIMBA_RUN_PM:
		get_user(start_addr, (u32 __user *)arg);
		if (start_addr >= KAS_PM_SRAM_START_ADDR &&
			start_addr <= KAS_PM_SRAM_END_ADDR) {
				firmware_run_pm(start_addr);
				ps_ptr_update();
		}
		break;
	case IOCTL_KALIMBA_STOP_PM:
		dev_info(dev, "Pause PM\n");
		write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR,
			(KAS_DEBUG << 2) | (0x2 << 30));
		write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, KAS_DEBUG_STOP);
		break;
	case IOCTL_KALIMBA_RESUME_PM:
		dev_info(dev, "Resume PM\n");
		write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR,
			(KAS_DEBUG << 2) | (0x2 << 30));
		write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, KAS_DEBUG_RUN);
		break;
	case IOCTL_KALIMBA_DUMP_BOOTCODE:
		dump_pm_codes((u32 __user *)arg);
		break;
	case IOCTL_KALIMBA_DOWNLOAD_BOOTCODE:
		if (copy_from_user(&code.head,
			(void __user *)arg,
			sizeof(struct firmware_code_head))) {
			dev_err(dev, "Get bootcode data head failed.\n");
			return -EINVAL;
		}

		code.code = dma_alloc_coherent(dev, code.head.code_size,
				&code.code_dma_addr, GFP_KERNEL);
		if (!code.code)
			return -ENOMEM;

		if (copy_from_user(code.code,
			(void __user *)arg
			+ sizeof(struct firmware_code_head),
			code.head.code_size)) {
			dev_err(dev, "Get bootcode data failed.\n");
			return -EINVAL;
		}
		firmware_download_code(&code);
		firmware_stop_pm();
		dma_free_coherent(dev,
			code.head.code_size, code.code, code.code_dma_addr);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct regmap_config kalimba_regs_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = KAS_CPU_KEYHOLE_MODE,
	.cache_type = REGCACHE_NONE,
};

static int firmware_probe(struct platform_device *pdev)
{
	void __iomem *base;
	struct resource *mem_res;

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap(&pdev->dev, mem_res->start,
			resource_size(mem_res));
	if (base == NULL)
		return -ENOMEM;

	kalimba_regs_regmap = devm_regmap_init_mmio(&pdev->dev, base,
			&kalimba_regs_regmap_config);

	if (IS_ERR(kalimba_regs_regmap))
		return PTR_ERR(kalimba_regs_regmap);

	return 0;
}

static const struct of_device_id kalimba_firmware_of_match[] = {
	{ .compatible = "csr,kalimba-ipc", },
	{}
};
MODULE_DEVICE_TABLE(of, kalimba_firmware_of_match);

static struct platform_driver kalimba_firmware_driver = {
	.driver = {
		.name = "kalimba-ipc",
		.of_match_table = kalimba_firmware_of_match,
	},
	.probe = firmware_probe,
};

module_platform_driver(kalimba_firmware_driver);

MODULE_DESCRIPTION("SiRF SoC Kalimba DSP IPC driver");
MODULE_LICENSE("GPL v2");
