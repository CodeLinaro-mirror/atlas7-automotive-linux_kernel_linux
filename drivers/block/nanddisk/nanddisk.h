/*
 * Nanddisk definitions for CSR Prima/ATLAS series.
 *
 * Copyright (c) 2013-2014, 2016, The Linux Foundation. All rights reserved.
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

#if !defined(_NANDDISK_H_)
#define _NANDDISK_H_


/*
 * Note: the interface version is only for nanddisk.h, which is the
 * exported interface by nanddisk binary.This version MUST be updated
 * after nanddisk.h is modified, so that force the wrap driver know the
 * inconsistent between the head file it using and the running binary.
 * format: unsigned integer, increased in each update.
 */

#define NANDDISK_INTERFACE_VERSION       13

/*
 * global struct define
 */
struct BLK_DEV_INFO {
	unsigned   byte_per_sector;
	unsigned   sector_per_block;
	unsigned   block_num;
};

/*
 * chip rleated defines
 */
enum NAND_CELL_TYPE {
	CELL_TYPE_UNKNOWN,
	CELL_SLC,
	CELL_MLC,
	CELL_TLC,
	CELL_TYPE_MAX
};

enum NAND_PARALLEL_LAYOUT {
	PARALLEL_LAYOUT_NULL = 0x0,
	/*
	 * blks in same plane are adjacent
	 */
	PARALLEL_LAYOUT_A = 0x10,
	/*
	 * blk has same order in each plan are adjacent
	 */
	PARALLEL_LAYOUT_B = 0x20,
};

#define NAND_PARALLEL_LAYOUT_MASK 0xf0

enum NAND_PARALLEL_TYPE {
	PARALLEL_TYPE_NULL = (PARALLEL_LAYOUT_NULL) + 0,
	PARALLEL_TYPE_MULTIPLANE_A = (PARALLEL_LAYOUT_A) + 0,
	PARALLEL_TYPE_MULTIPLANE_B = (PARALLEL_LAYOUT_B) + 0,
	PARALLEL_TYPE_INTERLEAVING_A = (PARALLEL_LAYOUT_B) + 1,
	PARALLEL_TYPE_INTERLEAVING_B = (PARALLEL_LAYOUT_B) + 2,
	PARALLEL_TYPE_MULTI_CHIPS = (PARALLEL_LAYOUT_B) + 3,
	PARALLEL_TYPE_MAX,
};

#define CHIP_CATEGORY_CELL_SHIFT            16
#define NAND_CATEGORY(cell, parallel_type) \
	(((cell)<<CHIP_CATEGORY_CELL_SHIFT)|(parallel_type))
#define CHIP_CATEGORY_PARALLEL_TYPE_MASK \
	((1<<CHIP_CATEGORY_CELL_SHIFT)-1)
#define CELL_TYPE(category) \
	((NAND_CELL_TYPE)((category)>>CHIP_CATEGORY_CELL_SHIFT))
#define PARALLEL_TYPE(category) \
	((NAND_PARALLEL_TYPE)((category)&CHIP_CATEGORY_PARALLEL_TYPE_MASK))
#define PARALLEL_LAYOUT(category) \
	((NAND_PARALLEL_LAYOUT)((category)&NAND_PARALLEL_LAYOUT_MASK))

struct CHIP_FEATURE {
	unsigned   reliable_mode:1;
	unsigned   randmizer:1;
	unsigned   interleave_mode:1;
	/* log2(plane_num) */
	unsigned   plane_num_bits:4;
	unsigned   interleave_num_bits:4;
};

struct NAND_CHIP_INFO {
	unsigned		chip_category;
	struct BLK_DEV_INFO	phy_bdev_info;
	unsigned          reserved_block_percent;
	struct CHIP_FEATURE      chip_feature;
	/* this chip has been actived? */
	unsigned          actived;
	/* all below valid if actived = true. */
	unsigned		chip_mode;
	struct BLK_DEV_INFO	active_bdev_info;
	struct BLK_DEV_INFO	io_bdev_info;
};


/* chip mode flag define */
#define CHIP_FLAG_MAX_IO_SECTOR_SIZE_SHIFT_BITS      4
#define CHIP_FLAG_MAX_IO_SECTOR_SIZE_SHIFT_MASK      \
	((1<<(CHIP_FLAG_MAX_IO_SECTOR_SIZE_SHIFT_BITS)) - 1)
#define CHIP_FLAG_MAX_IO_SECTOR_SIZE_SHIFT(flag)     \
	(flag & CHIP_FLAG_MAX_IO_SECTOR_SIZE_SHIFT_MASK)

#define CHIP_FLAG_PARALLEL_MODE	\
	(0x1 << (CHIP_FLAG_MAX_IO_SECTOR_SIZE_SHIFT_BITS))
#define CHIP_FLAG_INVALID_VALUE	0xffffffff

#define CHIP_FLAG(flags, max_io_sector_shift) \
	((flags) | \
	((max_io_sector_shift) & CHIP_FLAG_MAX_IO_SECTOR_SIZE_SHIFT_MASK))

/*
 * block rleated defines
 * block flag bits, define how to access block
 */

#define BLOCK_FLAG_RANDMIZER              0x1
#define BLOCK_FLAG_RELIABLE_MODE          0x2

/* BLOCK_FLAG_PARALLEL_MODE: enable/disable the parallel mode in zone */
#define BLOCK_FLAG_PARALLEL_MODE          0x4

/*
 * overide all parallen setting, use 1:1 mode to access physical block.
 * nanddisk internally use or debug use. user can not create such zone
 */
#define BLOCK_FLAG_PARALLEL_MODE_RAW      0x8

/*
 * below flag for debug only
 * BLOCK_FLAG_NO_ECC read/write the data with ecc disabled
 * BLOCK_FLAG_DEBUG  in debug mode, bypass all valid check.
 */
#define BLOCK_FLAG_NO_ECC                 0x10
#define BLOCK_FLAG_DEBUG                  0x20

/*
 * zone rleated defines--------
 * the max zone number can be created on the chip
 */
#define MAX_ZONE_NUM        10

#define ZONE_FLAG_PERMANENT_BITS          16
#define ZONE_FLAG_UPDATABLE_START         (0x1<<(ZONE_FLAG_PERMANENT_BITS))
#define ZONE_FLAG_PERMANENT_MASK          ((ZONE_FLAG_UPDATABLE_START) - 1)

/* permanent zone atrribute */
#define ZONE_FLAG_GUARANTEE_USABLE_SIZE   0x1


/* updatable zone attribute */

#define ZONE_FLAG_READONLY                (0x1 << (ZONE_FLAG_PERMANENT_BITS))


enum ZONE_TYPE {
	/* nanddisk internally used */
	RSVE_ZONE_TYPE,
	/* boot zone */
	BOOT_ZONE_TYPE,
	/* ftl zone */
	FTL_ZONE_TYPE,
	MAX_ZONE_TYPE,
};

/* this zone can use buffer. */
#define ZONE_MGR_ATTR_BUFFERABLE   0x1

/* this zone can use cache. */
#define ZONE_MGR_ATTR_CACHEABLE    0x2


/*
 * nanddisk provide two ways to access sectors: by zone or by map.
 * 1. By zone is more low level, you can directly read/write/delete sectors
 *   in each mounted zone.
 * 2. zone map is one higher level module, which provide the unified sectors
 *    map to user, and access sectos by zone. It is a wrap level on way
 *    by zone.
 * before access sectors by map, you should correctly set the map
 * 0xC001Beef : the dummy handle to do io on zone map
 */
#define MAP_HANDLE      0xC001Beef
struct ZONE_MAP {
	/* the smaller value, the higher priority
	 * if two zones be mapped to same sectors,
	 * use the higher priority map.
	 */
	unsigned     priority;
	/* the start sector to which maps this zone */
	unsigned     start;
	/* the number of sector to map */
	unsigned     sector_num;
};

struct NAND_ZONE {
	/*
	 * provided by upper driver when create this zone.
	 * nanddisk does not care this value.
	 */
	unsigned     magic;
	unsigned     start_active_block;
	unsigned     active_block_num;
	/*
	 * this flag determine the active block type
	 */
	unsigned     block_flag;
	/*
	 * zone attribute
	 */
	unsigned     zone_flag;
	/*
	 * the zone mgr type
	 */
	enum ZONE_TYPE    type;
	/*
	 * the zone mgr's attribute
	 */
	unsigned     zone_mgr_attr;
	/*
	 * usable blk info, used by zone mgr
	 */
	struct BLK_DEV_INFO bdev_info;
	/*
	 * used by io
	 */
	struct BLK_DEV_INFO io_bdev_info;
	/*
	 * number good block num when create this zone
	 */
	unsigned     init_good_block_num;
	/*
	 * logical block number, which is accessable by user
	 */
	unsigned     log_block_num;
	/*
	 * the rough number of log block num in using
	 */
	unsigned     inuse_log_block_num;
	struct ZONE_MAP     zone_map;

	/*
	 * sector map of boot zone
	 */
	unsigned        boot_sec_num;
	unsigned        *boot_sec_map;
};


/*
 * define the IOCTRL code exported by NANDDisk
 */
#define IOCTRL_CATEGORY_BITS         8
#define IOCTRL_CATEGORY_SHIFT        \
	(sizeof(unsigned)*8 - (IOCTRL_CATEGORY_BITS))
#define IOCTRL_CATEGORY_MASK         \
	(((0x1<<(IOCTRL_CATEGORY_BITS)) - 1)<<IOCTRL_CATEGORY_SHIFT)
#define IOCTRL_CATEGORY(ioctrl)      \
	(((ioctrl) & IOCTRL_CATEGORY_MASK) >> IOCTRL_CATEGORY_SHIFT)

enum NAND_IOCTRL_CATEGORY {
	CONFIG_IOCTRL = 1,
	CHIP_IOCTRL,
	ZONE_IOCTRL,
	CACHE_IOCTRL,
	BUF_IOCTRL,
	ZONEMAP_IOCTRL,
	IO_IOCTRL = ZONEMAP_IOCTRL,
	ASYNC_IOCTRL,
	INTR_IOCTRL,
	DBG_IOCTRL,
	MAX_IOCTRL
};


#define NAND_IOCTRL_CODE(category, function) \
	(((category##_IOCTRL)<<IOCTRL_CATEGORY_SHIFT) | (function))

/*
 * config IOCTRL
 */
#define NAND_IOCTRL_RUNTIME_INIT            NAND_IOCTRL_CODE(CONFIG, 0)
#define NAND_IOCTRL_GET_INTERFACE_VERSION   NAND_IOCTRL_CODE(CONFIG, 1)
#define NAND_IOCTRL_GET_VERSION             NAND_IOCTRL_CODE(CONFIG, 2)
#define NAND_IOCTRL_ENABLE_MSG              NAND_IOCTRL_CODE(CONFIG, 3)
#define NAND_IOCTRL_NEW_ADDRESS_MAP         NAND_IOCTRL_CODE(CONFIG, 4)
#define NAND_IOCTRL_ASYNC_MODE              NAND_IOCTRL_CODE(CONFIG, 5)




/*
 * chip level IOCTRL
 */
#define NAND_IOCTRL_POWER                   NAND_IOCTRL_CODE(CHIP, 0)
#define NAND_IOCTRL_GET_CHIPINFO            NAND_IOCTRL_CODE(CHIP, 1)
#define NAND_IOCTRL_SET_CHIPMODE            NAND_IOCTRL_CODE(CHIP, 2)
#define NAND_IOCTRL_RESET_CHIP              NAND_IOCTRL_CODE(CHIP, 3)

/*
 * zone level IOCTRL
 */
#define NAND_IOCTRL_CREATE_ZONE             NAND_IOCTRL_CODE(ZONE, 0)
#define NAND_IOCTRL_OPEN_ZONE               NAND_IOCTRL_CODE(ZONE, 1)
#define NAND_IOCTRL_DELETE_ZONE             NAND_IOCTRL_CODE(ZONE, 2)
#define NAND_IOCTRL_GET_ZONEINFO            NAND_IOCTRL_CODE(ZONE, 3)
#define NAND_IOCTRL_SET_ZONEFLAG            NAND_IOCTRL_CODE(ZONE, 4)
#define NAND_IOCTRL_MOUNT_ZONE              NAND_IOCTRL_CODE(ZONE, 5)
#define NAND_IOCTRL_DISMOUNT_ZONE           NAND_IOCTRL_CODE(ZONE, 6)
#define NAND_IOCTRL_QUERY_BACKGROUNDTASK      NAND_IOCTRL_CODE(ZONE, 7)
#define NAND_IOCTRL_TRIGGER_BACKGROUNDTASK    NAND_IOCTRL_CODE(ZONE, 8)

/*
 * cache IOCTRL
 */
#define NAND_IOCTRL_CACHE_SIZE              NAND_IOCTRL_CODE(CACHE, 0)


/*
 * buf IOCTRL
 */
#define NAND_IOCTRL_BUFFER_SIZE             NAND_IOCTRL_CODE(BUF, 0)
#define NAND_IOCTRL_DRAIN_BUFFER            NAND_IOCTRL_CODE(BUF, 1)

/*sector map IOCTRL */
#define NAND_IOCTRL_SET_ZONEMAP            NAND_IOCTRL_CODE(ZONEMAP, 0)
#define NAND_IOCTRL_GET_ZONEMAP_SIZE       NAND_IOCTRL_CODE(ZONEMAP, 1)
/*
 * io IOCTRL, IO_IOCTRL is alias of other category, offset must big enough
 */
#define NAND_IOCTRL_READ_SECTOR             NAND_IOCTRL_CODE(IO, 1000)
#define NAND_IOCTRL_WRITE_SECTOR            NAND_IOCTRL_CODE(IO, 1001)
#define NAND_IOCTRL_DELETE_SECTOR           NAND_IOCTRL_CODE(IO, 1002)


/*
 * async event IOCTRL
 */
#define NAND_IOCTRL_ASYNC_EVENT             NAND_IOCTRL_CODE(ASYNC, 0)

/*
 * intr IOCTRL
 */
#define NAND_IOCTRL_ENABLE_INTR             NAND_IOCTRL_CODE(INTR, 0)

/*
 * debug IOCTRL
 */
#define NAND_IOCTRL_DBG_GET_LOWLEVEL_CHIP_INFO  NAND_IOCTRL_CODE(DBG, 0)
#define NAND_IOCTRL_DBG_IS_BAD_BLOCK            NAND_IOCTRL_CODE(DBG, 1)
#define NAND_IOCTRL_DBG_SET_BAD_BLOCK           NAND_IOCTRL_CODE(DBG, 2)
#define NAND_IOCTRL_DBG_CLEAR_BAD_BLOCK         NAND_IOCTRL_CODE(DBG, 3)
#define NAND_IOCTRL_DBG_ERASE_BLOCK             NAND_IOCTRL_CODE(DBG, 4)
#define NAND_IOCTRL_DBG_READ_SECTOR             NAND_IOCTRL_CODE(DBG, 5)
#define NAND_IOCTRL_DBG_WRITE_SECTOR            NAND_IOCTRL_CODE(DBG, 6)
#define NAND_IOCTRL_DBG_IS_RSVE_BLOCK            NAND_IOCTRL_CODE(DBG, 7)
#define NAND_IOCTRL_DBG_SET_FEATURE            NAND_IOCTRL_CODE(DBG, 8)
#define NAND_IOCTRL_DBG_GET_FEATURE            NAND_IOCTRL_CODE(DBG, 9)

/*user IOCTRL*/
#define NAND_IOCTRL_USR_SET_CONFIG     NAND_IOCTRL_CODE(USR, 0)
#define NAND_IOCTRL_USR_GET_CONFIG     NAND_IOCTRL_CODE(USR, 1)


/*
 * define the interface for each IOCTRL
 *
 * CONFIG IOCTRL
 * NAND_IOCTRL_GET_INTERFACE_VERSION
 *   handle: ignored
 *   in_buf:  ignored
 *   out_buf: unsigned, the nanddisk interface (this file, nanddisk.h) version.
 *
 * NAND_IOCTRL_GET_VERSION
 *   handle: ignored
 *   in_buf:  ignored
 *   out_buf: unsigned, the nanddisk binary version.
 *
 * NAND_IOCTRL_ENABLE_MSG
 *   handle: ignored
 *   in_buf: unsigned: zero to disable debug message; non-zero to enable.
 *   out_buf: unsigned: the old status.
 * note: in_buf and out_buf can not be both null
 *
 * NAND_IOCTRL_NEW_ADDRESS_MAP
 *   handle: ignored
 *   in_buf: ADDRMAP struct array which end with one zero entry
 *   out_buf: original address map array in ADDRMAP format
 * note: this must be the first ioctrl into nanddisk after address map changed.
 */

struct ADDRMAP {
	void     *va;
	unsigned pa;
	unsigned size;
	unsigned flag;
};
#define ADDR_MAP_FLAG_CACHE	0x1
#define ADDR_MAP_FLAG_DMABUF	0x2

/*
 * NAND_IOCTRL_ASYNC_MODE
 *   handle: ignored
 *   in_buf: ASYNC_MODE: new async mode. see ASYNC_MODE struct description
 *   out_buf: ASYNC_MODE: current async mode, see ASYNC_MODE struct description
 * note: in_buf and out_buf can not be both null
 */
#define ASYNC_AUTO_ADAPT_CHECK_MIN_DURATION   2
struct ASYNC_MODE {
	/*
	 * zero: sync mode, all request are in sync mode; other fields
	 * are invalid;
	 * non-zero: enable async mode, auto_adapt field is valid.
	 * if async mode enabled, the specific request's sync/async mode are
	 * determined by the async_status parameter in PFN_NANDDISK_IOCTRL.
	 */
	unsigned enable;
	/*
	 * zero: disable async mode auto adapt.
	 * non-zero: enable async mode auto adapt.
	 * nanddisk will temporary disable async mode, if in all continuous
	 * 'duration' seconds, either read or write speed is higher than hi
	 * level. then, nanddisk will enable async mode again, if in all
	 * continuous 'duration' seconds, both read and write speed is lower
	 * than low levle.
	 */
	unsigned auto_adapt;
	/*
	 * seconds, should not less than ASYNC_AUTO_ADAPT_CHECK_MIN_DURATION
	 */
	unsigned duration;
	/*
	 * read bytes per second
	 */
	unsigned hi_level_read;
	/*
	 * write bytes per second
	 */
	unsigned hi_level_write;
	/*
	 * read bytes per second
	 */
	unsigned low_level_read;
	/*
	 * write bytes per second
	 */
	unsigned low_level_write;
};

/*
 * CHIP IOCTRL
 * NAND_IOCTRL_POWER
 *   handle: ignored
 *   in_buf: unsigned: zero to power down the nand chip; non-zero to power up.
 *   out_buf: the old status.
 * note: in_buf and out_buf can not be both null
 *
 * NAND_IOCTRL_GET_CHIPINFO
 *   handle: ignored
 *   in_buf:  null
 *   out_buf: NAND_CHIP_INFO struct.
 *
 * NAND_IOCTRL_SET_CHIPMODE
 *   handle: ignored
 *   in_buf:  unsigned: the chip mode
 *   out_buf: ignored.
 *
 * NAND_IOCTRL_RESET_CHIP
 *     DANGER!! reset the existed disk mgr info. all data/zone/chipmode info
 *     will be lost
 *   handle: ignored
 *   in_buf:  unsigned. for this is a very danger ioctrl, must match the
 *   signature NAND_RESET_CHIP_SIGNATURE 'NRST'
 *   out_buf: ignored.
 */
#define NAND_RESET_CHIP_SIGNATURE    0x5453524E

/*
 * ZONE IOCTRL
 * NAND_IOCTRL_CREATE_ZONE
 *   handle: ignored
 *   in_buf:  CREATE_ZONE struct
 *   out_buf: unsigned: the handle of the new created zone.
 */
#define NAND_SECTOR_SIZE_UNIT        512
struct CREATE_ZONE {
	unsigned   start_block;
	/*
	 * if ZONE_FLAG_GUARANTEE_USABLE_SIZE is set in zone_flag,
	 * 'size' * NAND_SECTOR_SIZE_UNIT is usable bytes;
	 * or else it means block number.
	 */
	unsigned   size;
	/* for mass production use, indicate the number of log blocks
	 * in source chip
	 */
	unsigned   log_block_num;
	unsigned   block_flag;
	unsigned   zone_flag;
	enum ZONE_TYPE  type;
	unsigned   magic;
};

/*
 * NAND_IOCTRL_OPEN_ZONE
 *   handle: do not care.
 *   in_buf:  unsigned: the order of zone.
 *   out_buf: unsigned: the handle of the opened zone.
 *
 * NAND_IOCTRL_DELETE_ZONE
 *   handle: zone handle
 *   in_buf:  ignored
 *   out_buf: ignored
 *
 * NAND_IOCTRL_GET_ZONEINFO
 *   handle: zone handle
 *   in_buf:  ignored
 *   out_buf: NAND_ZONE struct
 *
 * NAND_IOCTRL_SET_ZONEFLAG
 *   handle: zone handle
 *   in_buf: unsigned: the flag to set. this ioctrl can not modify permanent
 *      part of zone flag, so the permanent part must the same as the one
 *      create the zone.
 *   out_buf: ignored
 *
 * NAND_IOCTRL_MOUNT_ZONE
 *   handle: zone handle
 *   in_buf:  ignored
 *   out_buf: ignored
 *
 * NAND_IOCTRL_DISMOUNT_ZONE
 *   handle: zone handle
 *   in_buf:  ignored
 *   out_buf: ignored
 *
 * NAND_IOCTRL_QUERY_WEARLEVELING/NAND_IOCTRL_TRIGGER_WEARLEVELING
 *   handle: zone handle
 *   in_buf:  ignored
 *   out_buf: unsigned: non-zero means this zone need wear leveling
 * note: NAND_IOCTRL_QUERY_WEARLEVELING just return the wearleveling status,
 *	and does not trigger the wearleveling. NAND_IOCTRL_TRIGGER_WEARLEVELING
 *	will triger wearleveling, and return new wearleveling status in out_buf.
 */

/*
 * IO IOCTR
 * NAND_IOCTRL_CACHE_SIZE
 *   handle: ignored
 *   in_buf: unsigned:  new cache size,
 *	if zero, disable cache.
 *	if >BUF_MAX_CACHE_SIZE, use BUF_MAX_CACHE_SIZE;
 *     if <BUF_MIN_CACHE_SIZE and non-zero, use BUF_MIN_CACHE_SIZE
 *   out_buf: unsigned: the cache size
 * note:1. in_buf and out_buf can not be both null.
 *      2. by default, the cache is enabled
 */
#define NAND_MAX_CACHE_SIZE      (256*1024)
#define NAND_MIN_CACHE_SIZE      (64*1024)

/*
 * NAND_IOCTRL_READ_SECTOR/NAND_IOCTRL_WRITE_SECTOR/NAND_IOCTRL_DELETE_SECTOR
 *   handle: zone handle
 *   in_buf: NAND_IO struct. note: if NAND_IOCTRL_DELETE_SECTOR, sector_buf will
 *	be ignored.
 *   out_buf: if not null, unsigned, return whether this zone need do
 *   wearleveling.
 */
struct NAND_IO {
	unsigned   start_sector;
	unsigned   sector_num;
	void       *sector_buf;
};

/*
 * BUF IOCTRL
 * NAND_IOCTRL_BUFFER_SIZE
 *   handle: ignored
 *   in_buf: new write buffer size,
 *	if zero, disable buffer.
 *      if >BUF_MAX_BUFFER_SIZE, use BUF_MAX_BUFFER_SIZE;
 *      if <BUF_MIN_BUFFER_SIZE and non-zero, use BUF_MIN_BUFFER_SIZE
 *   out_buf: unsigned: the current buffer size
 * note: in_buf and out_buf can not be both null.
 */
#define NAND_MAX_BUFFER_SIZE      (128*1024)
#define NAND_MIN_BUFFER_SIZE      (32*1024)

/*
 * NAND_IOCTRL_DRAIN_BUFFER
 *   handle: ignored
 *   in_buf: ignored
 *   out_buf: ignored
 *
 * ASYNC IOCTRL
 * NAND_IOCTRL_ASYNC_EVENT
 *   handle: ignored
 *   in_buf:  ignored
 *   out_buf: ignored
 * note: this ioctrl has not any actual request. we use this
 *      ioctrl to resume nanddisk internal io thread.
 *      the most possible case to call this ioctrl is in interrupt handler.
 *
 * INTR IOCTRL
 * NAND_IOCTRL_ENABLE_INTR
 *   handle: ignored
 *   in_buf: unsigned: enable or diable nand interrupt.
 *	(actually, wrap driver only need disable it)
 *   out_buf: ignored
 *   note: this ioctrl can be call even if other ioctrl is pending in nanddisk.
 *
 * DBG IOCTRL
 *NAND_IOCTRL_DBG_GET_LOWLEVEL_CHIP_INFO
 *   handle: ignored
 *   in_buf:  ignored
 *   out_buf: NANDDBG_LOWLEVEL_CHIP_INFO
 */
struct NANDDBG_LOWLEVEL_CHIP_INFO {
	/* raw page size, include data,si and ecc. */
	unsigned   raw_page_size;
	/* TBD */
};

/*
 * NAND_IOCTRL_DBG_IS_BAD_BLOCK
 *   handle: ignored
 *   in_buf:  NANDDBG_BLOCK_INFO: the active block info to get its status
 *   out_buf: unsigned: true: bad blk; false: good block
 * note: return the info from bad block table; not read from the
 *      block's sector info
 *
 * NAND_IOCTRL_DBG_SET_BAD_BLOCK
 *   handle: ignored
 *   in_buf:  NANDDBG_BLOCK_INFO: the active block info to set bad
 *   out_buf: ignored
 *
 * NAND_IOCTRL_DBG_CLEAR_BAD_BLOCK
 *   handle: ignored
 *   in_buf:  NANDDBG_BLOCK_INFO: the active block info to set good
 *   out_buf: ignored
 *
 * NAND_IOCTRL_DBG_ERASE_BLOCK
 *   handle: ignored
 *   in_buf:  NANDDBG_BLOCK_INFO: the active block info to be erased
 *   out_buf: ignored
 */
struct NANDDBG_BLOCK_INFO {
	unsigned   block_index;
	unsigned   block_flag;
};

/*
 * NAND_IOCTRL_DBG_READ_SECTOR/NAND_IOCTRL_DBG_WRITE_SECTOR
 *   handle: ignored
 *   in_buf:  NANDDBG_IO: info to do the IO
 *   out_buf: ignored
 */
struct NANDDBG_SECTOR_INFO {
	unsigned       rsve1;
	unsigned char  blk_type;
	unsigned char  bad_blk;
	unsigned short rsve2;
};

/*
 * if BLOCK_FLAG_NO_ECC set in block_flag, data_buf save the raw data in
 * nand, si_buf is ignored;otherwise, data_buf save the data part,
 * and si_buf save the sector info
 */
struct NANDDBG_IO {
	unsigned              sector_index;
	void                  *data_buf;
	struct NANDDBG_SECTOR_INFO  *si_buf;
	unsigned              block_flag;
};

/*
 * PFN_NANDDISK_IOCTRL
 *    the entry function exported by NANDDisk.
 *    this function is at the offset 0 of NANDDisk binary
 * parameters:
 *    handle:
 *      the object handle who will response the ioctrl. if null, the object
 *      will be the whole nand.
 *    ioctrl_code:
 *      the code to select which function on the object specified by handle.
 *    in_buf, in_buf_size:
 *      the input buffer point and its size in unit byte.
 *    out_buf, out_buf_size:
 *      the output buffer point and its size in unit byte.
 *    async_status:
 *      - if null, means this request is in sync mode. the request must be
 *      finished before return.
 *      - if not null, this request is in async mode if async mode has been
 *      enabled by NAND_IOCTRL_ENABLE_ASYNC_MODE. when return, *async_status
 *      will save async status. if the request not finished, wrap driver
 *      should use NAND_IOCTRL_ASYNC_EVENT to check the request's handle
 *      status.
 * return:
 *    zero: failed; non-zero: successful.
 */
typedef unsigned (*PFN_NANDDISK_IOCTRL)(unsigned handle,
		unsigned ioctrl_code,
		void     *in_buf,
		unsigned in_buf_size,
		void     *out_buf,
		unsigned out_buf_size,
		unsigned *async_status);

/*
 * async status bit flag define for PFN_NANDDISK_IOCTRL's (*async_status)
 * parameter
 *
 * ASYNC_STATUS_PENDING: there is request not finished
 * ASYNC_STATUS_ERR: the request meet error.
 */
#define ASYNC_STATUS_PENDING         0x1
#define ASYNC_STATUS_ERR             0x2
/*
 * for NAND_IOCTRL_ASYNC_EVENT, this bit show the previous request's return
 * status.for other io ctrl, this bit is equal with PFN_NANDDISK_IOCTRL's
 * return value.
 */

#endif
