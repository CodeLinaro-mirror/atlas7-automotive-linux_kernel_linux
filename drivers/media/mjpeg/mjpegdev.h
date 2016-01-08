/*
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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

#ifndef __TYPES_H
#define __TYPES_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/mutex.h>

#define MJPEG_DEV_BUSY    0
#define VLC_PUSH_POP_NUMBIT__FIELD_0__WIDTH       4
#define MAX_VAL_OF_VLC_PUSH_POP_NUMBIT  \
	((1 << VLC_PUSH_POP_NUMBIT__FIELD_0__WIDTH) - 1)
#define DRAM_WIDTH_IN_BYTES         4

#define JPEG_PATH_MODE_DECODER                         0x0000
#define JPEG_PATH_MODE_ENCODER_FINAL                   0x0001

/* Format of JPEG code to be decoded/encoded. */
#define JPEG_PATH_FORMAT_422                       0x0000
#define JPEG_PATH_FORMAT_420                       0x0001

#define JPEG_READ_CODE_GG_LINE_STATUS_TIMEOUT 1000
#define JPEG_CMP_QTTABLE_SIZE                                64

#define ALIGN_DOWN(a, b)     (((a) / (b)) * (b))

#define MAX_DEVICE_NUM 2

#define VLM_HW_TABLE_JPEG                   0x31312020

/* Basic double link list item. This object must be the first item in any */
struct jpg_hw_buf {
	void *vaddr;
	unsigned long paddr;
	unsigned long size;
	unsigned long real_size;
};

/* Jpeg buffer addresses
 * @vaddr virtual address
 * @paddr phy address correspond to @vaddr
 */
struct jpg_buf_addrs {
	unsigned int vaddr;
	unsigned int paddr;
};


/*/ An enum for describing possible frame width unit types. */
enum EFrameWidthType {
	FRAME_WIDTH_PIXELS,
	FRAME_WIDTH_BYTES
};

/****************************************************************************/
/*/ block_buffer is used to define a rectangle area inside a larger frame. */
/*/ The units of the parmeters depend on the specific context, but all the */
/*/ parameters use the same units. */
/*/ The type of units which is used for the frame width is held in field */
/*/ 'frameWidthUnits', and current frame width is held in one of the two */
/*/ fields - 'framewidthpixels' or 'framewidthbytes'. */
/*/ Before each use of one of these fields, we have to make sure current*/
/*/ units type is correct. */
/****************************************************************************/

struct block_buffer {
	/* The frame buffer address. */
	unsigned char *pbybuffer;
	struct jpg_hw_buf *hw_buf_info;
	unsigned char color_fmt;	/*1 - {B3, B2, B1, B0}     y0 u y1 v  */
	/*2 - {B2, B3, B1, B0}     u y0 y1 v */
	/*3 - {B2, B3, B0, B1}     u y0 v y1 */
	/*4 - {B3, B2, B0, B1}     y0 u v y1 */
	/*5 - {B1, B0, B3, B2}     y1 v y0 u */
	/*6 - {B0, B1, B3, B2}     v y1 y0 u */
	/*7 - {B1, B0, B2, B3}     y1 v u y0 */
	/*8 - {B0, B1, B2, B3}     v y1 u y0 */
	/*0 - undefined */

	enum EFrameWidthType framewidthunits;
	/* Frame width and height. */
	unsigned int framewidthpixels;
	unsigned int framewidthbytes;
	unsigned int frameheight;

	unsigned int left;
	unsigned int top;

	/* Block width and height. */
	unsigned int width;
	unsigned int height;

};

enum e_path_event_group_id {
	PATH_EVENT_GROUP_MAIN,	/* used by default */
	PATH_EVENT_GROUP_1,	/* used by isp2 scale and convert paths */

	PATH_EVENT_GROUPS_NUM
};

struct s_path {
	/* Path type */
	unsigned int type;

	/* Source buffer */
	struct block_buffer in_frame;

	/* Destination buffer */
	struct block_buffer out_frame;
	/* Extra buffer */
	struct block_buffer extra_frame;

	/* New data indication */
	unsigned long update;
	unsigned long ulsubmit;

	/* New data indication extension */
	unsigned long ulupdate2;
	unsigned long ulsubmit2;

	/* Holds all the the changes until current point. */
	unsigned long ulhistory;
	unsigned long ulhistory2;

	/* This mutex will be used to synchronize changes in the path */
	/* attributes. */
	/*TX_MUTEX txParamLock; */

	/* PathWait timeout in mili-seconds */
	/* If a Path doesn't not finish within this period of time, */
	/* OsSysHalt will be called. */
	unsigned long ulpathwaittimeoutmilisec;

	/* the path event group ID */
	enum e_path_event_group_id epatheventgroupid;

	bool bimplicitacquirelock;

};

struct jpeg_codec_param {
	/* Compress / statistic / decompress / MPEG */
	struct s_path path;
	void __iomem *reg_vaddr;
	unsigned short mode;

	/* 422 / 420 */
	unsigned short yuv_format;

	/* 422 / 420 */
	unsigned short coded_format;

	/* First HW set of quantization tables (needed in compression and */
	/* decompression and acts as first of two sets in statistics mode). */
	unsigned long y_qt[JPEG_CMP_QTTABLE_SIZE];
	unsigned long c_qt[JPEG_CMP_QTTABLE_SIZE];

	/* Maximum block code volume */
	unsigned short mbcv;

	/* Maximum allowed memory size */
	unsigned long code_buf_size;

	/* Saves the byte alignment of the code buffer. */
	unsigned char code_align;

	/* Pointer to block of data containing Huffman tables */
	unsigned char *huffman_tables;
	bool create_dc;

	/* Pointers to new TMEM and CMEM */
	unsigned long *t_mem;
	unsigned long *c_mem;

	/* New CMEM's size */
	unsigned long c_mem_size;
	unsigned int table_size;
};

struct jpeg_dev_info {
	void __iomem *reg_vaddr;
	int irq;
	unsigned int reg_size;
};

struct jpg_hw_pool {
	void *vaddr;
	unsigned long paddr;
	unsigned long size;
	unsigned long used_size;
};

struct dev_intr_info {
	unsigned int irq_id;
	struct completion ready;
};


/*define the JPEG codec register structures */

union un_code_mode {
	struct {
		unsigned int enc_dec_mode:1;	/*0:decoding 1:encoding */
		unsigned int standard:3;	/*2:jpeg  3-7: reserved */
		unsigned int jpeg_color_format:1;
		unsigned int reserve1:4;
		unsigned int inout_color_format:1;
		unsigned int reserved2:22;
	} s_code_mode;
	unsigned int reg_code_mode;
};

union un_vlc_stuffing_type {
	struct {
		unsigned int all_stuf_bits_but_msb:1;
		unsigned int msb_stuf_bit:1;
	} s_vlc_stuffing_type;
	unsigned int reg_vlc_stuffing_type;
};

union un_codec_jpeg_config {
	struct {
		unsigned int dc_create:1;
		unsigned int code_create:1;
		unsigned int reserve:30;
	} s_codec_jpeg_config;
	unsigned int reg_codec_jpeg_config;
};

union reg_jpeg_int_mask {
	struct {
		unsigned int jpeg_end:1;
		unsigned int sample_done:1;
		unsigned int code_end_int:1;
		unsigned int image_end_int:1;
		unsigned int thumb_end_int:1;
		unsigned int reserve:27;
	} s_jpeg_int_mask;
	unsigned int reg_jpeg_int_mask;
};

struct jpeg_data {
	struct jpg_hw_pool hw_pool;
	struct jpeg_dev_info dev_info;
	struct dev_intr_info jpeg_info;
	struct class *jpeg_class;
	struct cdev jpeg_cdev;
	struct mutex pool_lock;
	struct clk *ck;
	dev_t devno;
	struct device *dev;
	unsigned int jpeg_busy;
	wait_queue_head_t query_wait;
};

enum jpeg_status {
	JPEG_IDLE,
	JPEG_START,
	JPEG_GETBUFFER,
	JPEG_SET_DEFAULT,
	JPEG_UPDATEQT,
	JPEG_UPDATE_VLC_TABLE,
	JPEG_SETCLIENTS,
	JPEG_ALIGN,
	JPEG_GO,
	JPEG_WAIT,
	JPEG_FREEBUFFER,
	JPEG_GET_PADDR,
	JPEG_FINISH
};

#define REGISTER_CODEC_MODE                      0x1400
#define REGISTER_QT_FIRST_Q_MATRIX               0x1404
#define REGISTER_QT_SECOND_Q_MATRIX              0x1408
#define REGISTER_CODEC_Y_DC_PRED_VALUE           0x1524
#define REGISTER_CODEC_U_DC_PRED_VALUE           0x1528
#define REGISTER_CODEC_V_DC_PRED_VALUE           0x152C
#define REGISTER_CODEC_REG_FILE_CLK              0x154C
#define REGISTER_CODEC_CODE_OVERFLOW             0x1600
#define REGISTER_CODEC_CODE_OVERFLOW_CURRENT     0x1604
#define REGISTER_CODEC_JPEG_GO                   0x1800
#define REGISTER_CODEC_JPEG_SUBREQ_DELAY         0x1804
#define REGISTER_CODEC_JPEG_CONFIG               0x1808
#define REGISTER_CONVERTER_RESET                 0x180C
#define REGISTER_CODEC_ENCDEC_RESET              0x1810
#define REGISTER_CODEC_JPEG_MASK_PIX_END_INTRPT  0x1814
#define REGISTER_CODEC_JPEG_CONVERTER_RESUME     0x1818
#define REGISTER_CONV_Y_LIMITS                   0x1A00
#define REGISTER_CONV_C_LIMITS                   0x1A04
#define REGISTER_CONV_CS_ENABLE                  0x1A08
#define REGISTER_CONV_CS_YIO                     0x1A0C
#define REGISTER_CONV_CS_CBIO                    0x1A10
#define REGISTER_CONV_CS_CRIO                    0x1A14
#define REGISTER_CONV_CS_C11                     0x1A18
#define REGISTER_CONV_CS_C12                     0x1A1C
#define REGISTER_CONV_CS_C13                     0x1A20
#define REGISTER_CONV_CS_C21                     0x1A24
#define REGISTER_CONV_CS_C22                     0x1A28
#define REGISTER_CONV_CS_C23                     0x1A2C
#define REGISTER_CONV_CS_C31                     0x1A30
#define REGISTER_CONV_CS_C32                     0x1A34
#define REGISTER_CONV_CS_C33                     0x1A38
#define REGISTER_CONV_CS_YOO                     0x1A3C
#define REGISTER_CONV_CS_CBOO                    0x1A40
#define REGISTER_CONV_CS_CROO                    0x1A44
#define REGISTER_VLC_ENABLE_AND_RESET            0x1C00
#define REGISTER_VLC_DATA1                       0x1C04
#define REGISTER_VLC_LEVEL                       0x1C0C
#define REGISTER_VLC_START_FILL_ADDR             0x1C14
#define REGISTER_VLC_TABLE_MEM_FILL              0x1C18
#define REGISTER_VLC_CODE_MEM_FILL               0x1C1C
#define REGISTER_VLC_HW_POINTERS                 0x1C28
#define REGISTER_VLC_FLUSH                       0x1C2C
#define REGISTER_VLC_BTS_CNT                     0x1C34
#define REGISTER_VLC_VLCD_LOAD                   0x1C38
#define REGISTER_VLC_VLCD_LOADED                 0x1C3C
#define REGISTER_VLC_CODE_REQUEST                0x1C4C
#define REGISTER_VLC_CODEREQSPACE                0x1C50
#define REGISTER_VLC_CODE_FIFO_GAP               0x1C54
#define REGISTER_VLC_VLCD_HEAD                   0x1C58
#define REGISTER_VLC_PUSH_POP_NUMBIT             0x1C5C
#define REGISTER_VLC_PUSH_POP_DATA               0x1C60
#define REGISTER_VLC_TRIGGER_SAME_TABLE          0x1C68
#define REGISTER_VLC_STUFFING_TYPE               0x1C84
#define REGISTER_VLC_TETRISFULLNES               0x1C98
#define REGISTER_VLC_JPEG_MBCV                   0x1C9C
#define REGISTER_VLC_DEC_ERROR                   0x1CA0
#define REGISTER_VLC_JPEG_TCV                    0x1CA4
#define REGISTER_CODEC_JPEG_FLAGS                0x1CAC
#define REGISTER_VLC_JPEG_PARAM_READY            0x1CB8
#define REGISTER_VLC_JPEG_SAMPLE_INTERVAL        0x1CBC
#define REGISTER_VLC_CODEFIFOTHR                 0x1CCC
#define REGISTER_CODEC_JPEG_MB_NUM               0x1CD4
#define REGISTER_CODEC_JPEG_CODE_RESUME          0x1CD8
#define REGISTER_CODEC_QUANT_MODE                0x1CDC
#define REGISTER_CODEC_PRIVATE_QUANT_SCALE_Y0    0x1CE0
#define REGISTER_CODEC_PRIVATE_QUANT_SCALE_Y1    0x1CE4
#define REGISTER_CODEC_PRIVATE_QUANT_SCALE_Y2    0x1CE8
#define REGISTER_CODEC_PRIVATE_QUANT_SCALE_Y3    0x1CEC
#define REGISTER_CODEC_PRIVATE_QUANT_SCALE_C0    0x1CF0
#define REGISTER_VLC_JPEG_TETRIS_FULLNES_AT_FLUSH 0x1D4C
#define REGISTER_VLC_POP_COUNT                   0x1D50
#define REGISTER_VLC_TOTAL_POP_COUNT             0x1D54
#define REGISTER_CODE_BG_MAX_BURST_SIZE          0x4000
#define REGISTER_CODE_BG_MIN_BURST_SIZE          0x4004
#define REGISTER_CODE_BG_UNIFY_BURST_EN          0x4008
#define REGISTER_CODE_BG_ADDR_ALIGN              0x400C
#define REGISTER_CODE_GG_LINE_ABORT              0x4010
#define REGISTER_CODE_GG_LINE_FLUSH              0x4014
#define REGISTER_BRIDGE_CL_CODE_PENDING_TRANS    0x401C
#define REGISTER_BRIDGE_CL_CODE_BYTE_SWAP        0x4020
#define REGISTER_CODE_GG_LINE_ENABLE             0x4028
#define REGISTER_CODE_GG_LINE_START_ADDR         0x402C
#define REGISTER_CODE_GG_LINE_SIZE               0x4030
#define REGISTER_CODE_GG_LINE_RD_WR              0x4034
#define REGISTER_CODE_GG_LINE_LAST               0x4038
#define REGISTER_CODE_GG_LINE_VALID              0x403C
#define REGISTER_CODE_GG_LINE_STATUS             0x4040
#define REGISTER_IMAGE_BG_MAX_BURST_SIZE         0x5000
#define REGISTER_IMAGE_BG_MIN_BURST_SIZE         0x5004
#define REGISTER_IMAGE_BG_UNIFY_BURST_EN         0x5008
#define REGISTER_IMAGE_BG_ADDR_ALIGN             0x500C
#define REGISTER_IMAGE_GG_MBLK_ABORT             0x5010
#define REGISTER_BRIDGE_CL_IMAGE_PENDING_TRANS   0x501C
#define REGISTER_BRIDGE_CL_IMAGE_BYTE_SWAP       0x5020
#define REGISTER_IMAGE_GG_MBLK_START_ADDR        0x5044
#define REGISTER_IMAGE_GG_MBLK_STRIDE            0x5048
#define REGISTER_IMAGE_GG_MBLK_LINE_SIZE_START   0x504C
#define REGISTER_IMAGE_GG_MBLK_LINE_SIZE_MIDDLE  0x5050
#define REGISTER_IMAGE_GG_MBLK_LINE_SIZE_LAST    0x5054
#define REGISTER_IMAGE_GG_MBLK_LINE_NUM_START    0x5058
#define REGISTER_IMAGE_GG_MBLK_LINE_NUM_MIDDLE   0x505C
#define REGISTER_IMAGE_GG_MBLK_LINE_NUM_LAST     0x5060
#define REGISTER_IMAGE_GG_MBLK_RD_WR             0x5064
#define REGISTER_IMAGE_GG_MBLK_LAST              0x5068
#define REGISTER_IMAGE_GG_MBLK_FLIP              0x506C
#define REGISTER_IMAGE_GG_MBLK_VALID             0x5070
#define REGISTER_IMAGE_GG_MBLK_ENABLE            0x5074
#define REGISTER_IMAGE_GG_MBLK_BLOCKS_NUM        0x5078
#define REGISTER_IMAGE_GG_MBLK_H_INC_ADDR_START  0x507C
#define REGISTER_IMAGE_GG_MBLK_H_INC_ADDR_MIDDLE 0x5080
#define REGISTER_IMAGE_GG_MBLK_H_INC_ADDR_LAST   0x5084
#define REGISTER_IMAGE_GG_MBLK_V_INC_ADDR_START  0x5088
#define REGISTER_IMAGE_GG_MBLK_V_INC_ADDR_MIDDLE 0x508C
#define REGISTER_IMAGE_GG_MBLK_H_STRIPS_NUM      0x5090
#define REGISTER_IMAGE_GG_MBLK_STATUS            0x5094
#define REGISTER_THUMB_BG_MAX_BURST_SIZE         0x6000
#define REGISTER_THUMB_BG_MIN_BURST_SIZE         0x6004
#define REGISTER_THUMB_BG_UNIFY_BURST_EN         0x6008
#define REGISTER_THUMB_BG_ADDR_ALIGN             0x600C
#define REGISTER_THUMB_GG_MBLK_ABORT             0x6010
#define REGISTER_BRIDGE_CL_THUMB_PENDING_TRANS   0x601C
#define REGISTER_BRIDGE_CL_THUMB_BYTE_SWAP       0x6020
#define REGISTER_THUMB_GG_MBLK_START_ADDR        0x6044
#define REGISTER_THUMB_GG_MBLK_STRIDE            0x6048
#define REGISTER_THUMB_GG_MBLK_LINE_SIZE_START   0x604C
#define REGISTER_THUMB_GG_MBLK_LINE_SIZE_MIDDLE  0x6050
#define REGISTER_THUMB_GG_MBLK_LINE_SIZE_LAST    0x6054
#define REGISTER_THUMB_GG_MBLK_LINE_NUM_START    0x6058
#define REGISTER_THUMB_GG_MBLK_LINE_NUM_MIDDLE   0x605C
#define REGISTER_THUMB_GG_MBLK_LINE_NUM_LAST     0x6060
#define REGISTER_THUMB_GG_MBLK_RD_WR             0x6064
#define REGISTER_THUMB_GG_MBLK_LAST              0x6068
#define REGISTER_THUMB_GG_MBLK_FLIP              0x606C
#define REGISTER_THUMB_GG_MBLK_VALID             0x6070
#define REGISTER_THUMB_GG_MBLK_ENABLE            0x6074
#define REGISTER_THUMB_GG_MBLK_BLOCKS_NUM        0x6078
#define REGISTER_THUMB_GG_MBLK_H_INC_ADDR_START  0x607C
#define REGISTER_THUMB_GG_MBLK_H_INC_ADDR_MIDDLE 0x6080
#define REGISTER_THUMB_GG_MBLK_H_INC_ADDR_LAST   0x6084
#define REGISTER_THUMB_GG_MBLK_V_INC_ADDR_START  0x6088
#define REGISTER_THUMB_GG_MBLK_V_INC_ADDR_MIDDLE 0x608C
#define REGISTER_THUMB_GG_MBLK_H_STRIPS_NUM      0x6090
#define REGISTER_THUMB_GG_MBLK_STATUS            0x6094
#define REGISTER_BRIDGE_MAX_PENDING_TRANS        0x7000
#define REGISTER_ARBITER_CL_CODE_DISABLE         0x7008
#define REGISTER_ARBITER_CL_IMAGE_DISABLE        0x700C
#define REGISTER_ARBITER_CL_THUMB_DISABLE        0x7010
#define REGISTER_JPEG_INT_MASK                   0x7014
#define REGISTER_JPEG_INT_CTRL_STAT              0x7018
#define REGISTER_JPEG_CIO_BYTE_SWAP              0x701C
#define REGISTER_ARBITER_RESET                   0x7020

#define CLKC_RSTC_UNIT_SW_RST4_SET		0x350
#define CLKC_RSTC_UNIT_SW_RST4_CLR		0x354
#define CLKC_LEAF_CLK_EN4_SET			0x4E8
#define CLKC_LEAF_CLK_EN4_CLR			0x4EC

#define IOCTL_JPEG_FREEBUFFER	_IOW('J', 1000, struct jpg_hw_buf*)
#define IOCTL_JPEG_START	_IOW('J', 1001, void*)
#define IOCTL_JPEG_UPDATE_VLC_TABLE _IOW('J', 1002, struct jpeg_codec_param*)
#define IOCTL_JPEG_FINISH _IOW('J', 1003, void*)
#define IOCTL_JPEG_SET_DEFAULT	_IOW('J', 1004, struct jpeg_codec_param*)
#define IOCTL_JPEG_UPDATEQT	_IOW('J', 1005, struct jpeg_codec_param*)
#define IOCTL_JPEG_GETBUFFER	_IOWR('J', 1006, struct jpg_hw_buf*)
#define IOCTL_JPEG_GO		_IOW('J', 1007, struct jpeg_codec_param*)
#define IOCTL_JPEG_WAIT		_IOW('J', 1008, struct jpeg_codec_param*)
#define IOCTL_JPEG_SETCLIENTS	_IOW('J', 1009, struct jpeg_codec_param*)
#define IOCTL_JPEG_ALIGN	_IOW('J', 1010, struct jpeg_codec_param*)
#define IOCTL_JPEG_GET_PADDR	_IOWR('J', 1011, struct jpg_hw_buf*)

#endif
