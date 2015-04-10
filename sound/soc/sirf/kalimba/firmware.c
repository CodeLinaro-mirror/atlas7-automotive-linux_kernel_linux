/*
 * kailimba firmware load driver
 *
 * Copyright (c) 2015 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#ifdef CONFIG_SND_SOC_SIRF_KALIMBA_DEBUG
#include "debug.h"
#endif
#include "regs.h"

struct firmware_code_head {
	int code_size;
	int pm_unpacker_offset;
	int pm_offset;
	int dm1_offset;
	int dm2_offset;
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

static void firmware_run_pm(struct regmap *regmap, u32 start_addr)
{
	regmap_write(regmap, KAS_CPU_KEYHOLE_ADDR,
			(KAS_DEBUG << 2) | (0x2 << 30));
	regmap_write(regmap, KAS_CPU_KEYHOLE_DATA, KAS_DEBUG_STOP);

	/* Set KAS program counter */
	regmap_write(regmap, KAS_CPU_KEYHOLE_ADDR,
			(KAS_REGFILE_PC << 2) | (0x2 << 30));
	regmap_write(regmap, KAS_CPU_KEYHOLE_DATA, start_addr);

	regmap_write(regmap, KAS_CPU_KEYHOLE_ADDR,
			(KAS_DEBUG << 2) | (0x2 << 30));
	regmap_write(regmap, KAS_CPU_KEYHOLE_DATA, KAS_DEBUG_RUN);
	regmap_write(regmap, KAS_CPU_KEYHOLE_ADDR,
			(KAS_REGFILE_PC << 2) | (0x2 << 30));
	regmap_read(regmap, KAS_CPU_KEYHOLE_DATA, &start_addr);
}

void firmware_stop_pm(struct regmap *regmap)
{
	regmap_write(regmap, KAS_CPU_KEYHOLE_ADDR,
			(KAS_DEBUG << 2) | (0x2 << 30));
	regmap_write(regmap, KAS_CPU_KEYHOLE_DATA, KAS_DEBUG_STOP);
	regmap_write(regmap, KAS_CPU_KEYHOLE_ADDR,
			(KAS_REGFILE_PC << 2) | (0x2 << 30));
	regmap_write(regmap, KAS_CPU_KEYHOLE_DATA, 0);

}

static void firmware_run_pm_unpacker(
		struct regmap *regmap,
		u32 dm_block_src,
		u32 pm_block_dest,
		u32 pm_bytes_len,
		u32 params_start_addr,
		u32 pm_unpacker_image_start_addr)
{
	u32 unpack_status;

	regmap_write(regmap, KAS_CPU_KEYHOLE_ADDR,
			(KAS_DEBUG << 2) | (0x2 << 30));
	regmap_write(regmap, KAS_CPU_KEYHOLE_DATA, KAS_DEBUG_STOP);

	regmap_write(regmap, KAS_CPU_KEYHOLE_MODE, 4);
	regmap_write(regmap, KAS_CPU_KEYHOLE_ADDR,
			(PM_UNPACKER_PARAMS_DM_SRC(params_start_addr) <<
			 2) | (0x2 << 30));
	regmap_write(regmap, KAS_CPU_KEYHOLE_DATA, dm_block_src);
	regmap_write(regmap, KAS_CPU_KEYHOLE_DATA,
			pm_block_dest / 4);
	regmap_write(regmap, KAS_CPU_KEYHOLE_DATA,
			pm_bytes_len / 4);
	/* IPC_TRGT.lo16 */
	regmap_write(regmap, KAS_CPU_KEYHOLE_DATA, 0);
	/* IPC_TRGT.hi16 */
	regmap_write(regmap, KAS_CPU_KEYHOLE_DATA, 0);
	/* UNPACK_STATUS */
	regmap_write(regmap, KAS_CPU_KEYHOLE_DATA, 0);

	/* Set KAS program counter */
	firmware_run_pm(regmap, pm_unpacker_image_start_addr);

	/* Wait PM-unpacker complete */
	regmap_write(regmap, KAS_CPU_KEYHOLE_MODE, 0);
	regmap_write(regmap, KAS_CPU_KEYHOLE_ADDR,
			(PM_UNPACKER_PARAMS_UNPACK_STATUS(params_start_addr) <<
			 2) | (0x2 << 30));
	do {
		regmap_read(regmap, KAS_CPU_KEYHOLE_DATA,
				&unpack_status);
	} while (!(unpack_status & 1));
}

static u32 firmware_download_pm_unpacker(struct regmap *regmap,
		struct firmware_code *code, u32 *pm_unpacker_start_addr)
{
	u32 i;
	struct firmware_pm_unpacker_image *pm_unpacker_image;
	u32 size;
	u32 *pm_unpacker_code;

	pm_unpacker_image = code->code + code->head.pm_unpacker_offset;
	*pm_unpacker_start_addr = pm_unpacker_image->pm_unpacker_start_addr;
	size = pm_unpacker_image->pm_unpacker_size;
	pm_unpacker_code = &pm_unpacker_image->pm_unpacker_code;

	regmap_write(regmap, KAS_CPU_KEYHOLE_MODE, 4);
	regmap_write(regmap, KAS_CPU_KEYHOLE_ADDR,
			*pm_unpacker_start_addr | (0x3 << 30));
	for (i = 0; i < size; i++)
		regmap_write(regmap, KAS_CPU_KEYHOLE_DATA,
				pm_unpacker_code[i]);

	return pm_unpacker_image->params_start_addr;
}

static void firmware_load_pm_though_keyhole(
		struct regmap *regmap,
		u32 start_addr, u32 size_bytes, void *data)
{
	u32 i;
	u32 *code = (u32 *)data;

	regmap_write(regmap, KAS_CPU_KEYHOLE_MODE, 4);
	regmap_write(regmap, KAS_CPU_KEYHOLE_ADDR,
			start_addr | (0x3 << 30));
	for (i = 0; i < size_bytes / 4; i++)
		regmap_write(regmap, KAS_CPU_KEYHOLE_DATA,
				code[i]);
}

static void firmware_load_pm_though_dma(
		struct regmap *regmap, struct firmware_code *code,
		u32 start_addr, u32 size_bytes, void *data)
{
	void *pm_code_dma_addr;
	u32 val;

	pm_code_dma_addr = (void *)code->code_dma_addr + (data - code->code);

	do {
		regmap_read(regmap, KAS_DMA_STATUS, &val);
	} while (!(val & KAS_DMAC_IDLE));

	regmap_write(regmap, KAS_DMAC_DMA_XLEN,
			size_bytes / 4);
	regmap_write(regmap, KAS_DMAC_DMA_WIDTH,
			size_bytes / 4);
	regmap_write(regmap, KAS_DMA_ADDR,
			start_addr / 4);
	regmap_write(regmap, KAS_DMAC_DMA_ADDR,
			(u32)pm_code_dma_addr);

	do {
		regmap_read(regmap, KAS_DMAC_DMA_INT, &val);
	} while (!(val & KAS_DMAC_FINISH_INT));
	regmap_read(regmap, KAS_DMAC_DMA_CUR_DATA_ADDR, &val);
	regmap_update_bits(regmap, KAS_DMAC_DMA_INT,
			KAS_DMAC_FINISH_INT, KAS_DMAC_FINISH_INT);
}

static void firmware_init_dma(struct regmap *regmap,
		u32 transfer_mode)
{
	/* Reset DMA client */
	regmap_update_bits(regmap, KAS_DMA_MODE,
			KAS_RESET_DMA_CLIENT, KAS_RESET_DMA_CLIENT);
	regmap_update_bits(regmap, KAS_DMA_MODE,
			KAS_RESET_DMA_CLIENT, 0);

	/* Set KAS DMA as Single transaction */
	regmap_update_bits(regmap, KAS_DMA_MODE,
			KAS_DMA_CHAIN_MODE, 0);

	regmap_write(regmap, KAS_TRANSLATE, transfer_mode);
	regmap_write(regmap, KAS_DMAC_DMA_CTRL,
			KAS_DMAC_TRANS_MEM_TO_FIFO);
	regmap_write(regmap, KAS_DMAC_DMA_YLEN, 0);
	regmap_write(regmap, KAS_DMAC_DMA_INT_EN, 0);
	regmap_write(regmap, KAS_DMA_INC, 0);
	regmap_write(regmap, KAS_DMAC_DMA_INT, 0xFFFFFFFF);
}

static void firmware_download_pm(struct regmap *regmap,
		struct firmware_code *code)
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
	regmap_write(regmap, KAS_CPU_KEYHOLE_ADDR,
			(KAS_DEBUG << 2) | (0x2 << 30));
	regmap_write(regmap, KAS_CPU_KEYHOLE_DATA, KAS_DEBUG_STOP);

	params_start_addr =
		firmware_download_pm_unpacker(regmap, code,
				&pm_unpacker_start_addr);

	firmware_init_dma(regmap, KAS_TRANSLATE_24BIT);

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
						regmap, current_start_addr,
						remaining_bytes, data);
				remaining_bytes = 0;
			} else if (current_start_addr >=
				DOWN_ALIGN_12BYTES(pm_unpacker_start_addr)) {
				/* If the start address overlaps the
				 * PM-unpacker area,use the keyhole to download
				 */
				firmware_load_pm_though_keyhole(
						regmap, current_start_addr,
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
						regmap, current_start_addr,
						remaining_bytes, data);
					break;
				}

				firmware_load_pm_though_dma(regmap, code,
						KAS_DM1_SRAM_START_ADDR,
						dma_transfer_bytes, data);
				firmware_run_pm_unpacker(regmap,
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

static void firmware_download_dm(struct regmap *regmap,
		struct firmware_code *code, void *dm_image)
{
	u32 block_num = ((u32 *)dm_image)[0];
	struct firmware_pm_dm_block *dm_block_ptr =
		dm_image + sizeof(u32);
	u32 i;
	u32 val;
	void *dm_code_dma_addr;

	firmware_init_dma(regmap, KAS_TRANSLATE_24BIT_RIGHT_ALIGNED);

	for (i = 0; i < block_num; i++) {
		dm_code_dma_addr = (void *)code->code_dma_addr +
			((void *)&dm_block_ptr->data -
			 code->code);
		do {
			regmap_read(regmap, KAS_DMA_STATUS, &val);
		} while (!(val & KAS_DMAC_IDLE));

		regmap_write(regmap, KAS_DMAC_DMA_XLEN,
				dm_block_ptr->size);
		regmap_write(regmap, KAS_DMAC_DMA_WIDTH,
				dm_block_ptr->size);
		regmap_write(regmap, KAS_DMA_ADDR,
				dm_block_ptr->start_addr);
		regmap_write(regmap, KAS_DMAC_DMA_ADDR,
				(u32) dm_code_dma_addr);

		do {
			regmap_read(regmap, KAS_DMAC_DMA_INT,
					&val);
		} while (!(val & KAS_DMAC_FINISH_INT));
		regmap_update_bits(regmap, KAS_DMAC_DMA_INT,
				KAS_DMAC_FINISH_INT, KAS_DMAC_FINISH_INT);

		dm_block_ptr = (((void *)dm_block_ptr) + 8
				+ dm_block_ptr->size * sizeof(u32));
	}

}

static void firmware_download_code(struct regmap *regmap,
		struct firmware_code *code)
{
	firmware_download_pm(regmap, code);
	firmware_download_dm(regmap, code, code->code + code->head.dm1_offset);
	firmware_download_dm(regmap, code, code->code + code->head.dm2_offset);
}

static void dump_pm_codes(struct regmap *regmap,
		u32 __user *buffer)
{
	u32 i;
	u32 val;

	regmap_write(regmap, KAS_CPU_KEYHOLE_MODE, 4);
	regmap_write(regmap, KAS_CPU_KEYHOLE_ADDR, 0 | 0x3 << 30);
	for (i = 0; i < KAS_PM_SRAM_SIZE; i++) {
		regmap_read(regmap, KAS_CPU_KEYHOLE_DATA, &val);
		put_user(val, buffer + i);
	}
}

int firmware_ioctl(struct regmap *regmap, struct device *dev,
		unsigned int cmd, unsigned long arg)
{
	struct firmware_code code;
	u32 start_addr, length, i;
	void *data;

	switch (cmd) {
	case IOCTL_KALIMBA_WRITE_PM:
		get_user(start_addr, (u32 __user *)arg);
		get_user(length, (u32 __user *)(arg + 4));
		data = kmalloc(length, GFP_KERNEL);
		if (copy_from_user(data, (void __user *)(arg + 8),
				length)) {
			dev_err(dev, "Get PM code failed.\n");
			kfree(data);
			return -EINVAL;
		}
		firmware_load_pm_though_keyhole(regmap, start_addr,
			length, data);
		kfree(data);
		break;
	case IOCTL_KALIMBA_READ_PM:
		get_user(start_addr, (u32 __user *)arg);
		get_user(length, (u32 __user *)(arg + 4));
		arg += 8;
		regmap_write(regmap, KAS_CPU_KEYHOLE_MODE, 4);
		regmap_write(regmap, KAS_CPU_KEYHOLE_ADDR,
			start_addr | (0x3 << 30));
		for (i = 0; i < length / 4; i++) {
			u32 tmp;

			regmap_read(regmap, KAS_CPU_KEYHOLE_DATA,
			&tmp);
			put_user(tmp, (u32 __user *)(arg + i * 4));
		}
		break;
	case IOCTL_KALIMBA_WRITE_DM:
		get_user(start_addr, (u32 __user *)arg);
		get_user(length, (u32 __user *)(arg + 4));
		arg += 8;
		regmap_write(regmap, KAS_CPU_KEYHOLE_MODE, 4);
		regmap_write(regmap, KAS_CPU_KEYHOLE_ADDR,
			(start_addr << 2) | (0x2 << 30));
		for (i = 0; i < length / 4; i++) {
			u32 tmp;

			get_user(tmp, (u32 __user *)(arg + i * 4));
			regmap_write(regmap, KAS_CPU_KEYHOLE_DATA,
				tmp);
		}
		break;
	case IOCTL_KALIMBA_READ_DM:
		get_user(start_addr, (u32 __user *)arg);
		get_user(length, (u32 __user *)(arg + 4));
		arg += 8;
		regmap_write(regmap, KAS_CPU_KEYHOLE_MODE, 4);
		regmap_write(regmap, KAS_CPU_KEYHOLE_ADDR,
			(start_addr << 2) | (0x2 << 30));
		for (i = 0; i < length / 4; i++) {
			u32 tmp;

			regmap_read(regmap, KAS_CPU_KEYHOLE_DATA,
			&tmp);
			put_user(tmp, (u32 __user *)(arg + i * 4));
		}
		break;
	case IOCTL_KALIMBA_RUN_PM:
		get_user(start_addr, (u32 __user *)arg);
		firmware_run_pm(regmap, start_addr);
		break;
	case IOCTL_KALIMBA_STOP_PM:
		dev_info(dev, "Pause PM\n");
		regmap_write(regmap, KAS_CPU_KEYHOLE_ADDR,
			(KAS_DEBUG << 2) | (0x2 << 30));
		regmap_write(regmap, KAS_CPU_KEYHOLE_DATA,
			KAS_DEBUG_STOP);
		break;
	case IOCTL_KALIMBA_RESUME_PM:
		dev_info(dev, "Resume PM\n");
		regmap_write(regmap, KAS_CPU_KEYHOLE_ADDR,
			(KAS_DEBUG << 2) | (0x2 << 30));
		regmap_write(regmap, KAS_CPU_KEYHOLE_DATA,
			KAS_DEBUG_RUN);
		break;
	case IOCTL_KALIMBA_DUMP_BOOTCODE:
		dump_pm_codes(regmap, (u32 __user *)arg);
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
		firmware_download_code(regmap, &code);
		firmware_stop_pm(regmap);
		dma_free_coherent(dev,
			code.head.code_size, code.code, code.code_dma_addr);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
