#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/clk.h>

#include "mjpegdev.h"

static struct jpeg_data jpeg;

static void jpeg_get_hw_pool(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *vdec_memory;
	struct jpg_hw_pool *hw_pool;

	hw_pool = &(jpeg.hw_pool);
	vdec_memory = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!vdec_memory) {
		dbg_msg(1, KERN_INFO "Get reserved memory error\n");
		return;
	}

	hw_pool->paddr = of_translate_address(vdec_memory,
		of_get_address(vdec_memory, 0,
		(u64 *)(&(hw_pool->size)), NULL));
	hw_pool->vaddr =
		(u32) ioremap_nocache(hw_pool->paddr, hw_pool->size);
	dbg_msg(1, KERN_INFO "hw_pool.paddr=0x%x\n", hw_pool->paddr);
	dbg_msg(1, KERN_INFO "hw_pool.vaddr=0x%x\n", hw_pool->vaddr);
}

static void jpeg_alloc_buf(unsigned int size, struct jpg_hw_buf *hwbuf)
{
	unsigned int realsize = ((size + 7) >> 3) << 3;
	struct jpg_hw_pool *hw_pool;

	hw_pool = &(jpeg.hw_pool);
	dbg_msg(1, KERN_INFO "jpeg_alloc_buf realsize = %d\n", (int)realsize);
	mutex_lock(&jpeg.pool_lock);
	if ((!hwbuf) || hw_pool->used_size + realsize > hw_pool->size
	    || size <= 0) {
		if (!hwbuf) {
			hwbuf->vaddr = 0;
			hwbuf->paddr = 0;
			hwbuf->size = 0;
		}
		dbg_msg(1, "JPG:no enough HW buffer, alloc fail\r\n");
		return;
	}
	hwbuf->vaddr = hw_pool->vaddr + hw_pool->used_size;
	hwbuf->paddr = hw_pool->paddr + hw_pool->used_size;
	hwbuf->size = size;
	hwbuf->real_size = realsize;
	hw_pool->used_size += realsize;
	dbg_msg(1, "JPG:jpeg_alloc_buf hwbuf->vaddr %x hwbuf->paddr %x\n",
		hwbuf->vaddr, hwbuf->paddr);
	dbg_msg(1, "hwbuf->size %lx,hw_pool->used_size %lx\r\n",
		hwbuf->size, hw_pool->used_size);
	mutex_unlock(&jpeg.pool_lock);
}

static void jpeg_free_buf(struct jpg_hw_buf *hwbuf)
{
	struct jpg_hw_pool *hw_pool;

	hw_pool = &(jpeg.hw_pool);
	mutex_lock(&jpeg.pool_lock);
	if ((!hwbuf) ||
	    (hw_pool->used_size - hwbuf->real_size > hw_pool->size)) {
		if (!hwbuf) {
			hwbuf->vaddr = 0;
			hwbuf->paddr = 0;
			hwbuf->size = 0;
		}
		dbg_msg(1,
			  "JPG:no available HW buffer, free fail\r\n");
		return;
	}
	hw_pool->used_size -= hwbuf->real_size;
	dbg_msg(1, "JPG:jpeg_free_buf vaddr %x paddr %x size %lx\r\n",
		  hwbuf->vaddr, hwbuf->paddr, hwbuf->size);
	mutex_unlock(&jpeg.pool_lock);
}

static void write_reg(unsigned long reg_offset, unsigned long data)
{
	writel(data, (void *)(jpeg.dev_info.reg_vaddr + reg_offset));
}

static unsigned long read_reg(unsigned long reg_offset)
{
	return readl((void *)(jpeg.dev_info.reg_vaddr + reg_offset));
}

static void jpeg_update_thumbnail_mb_geometry(struct jpeg_codec_param *param)
{
	unsigned int h_block_num, v_block_num;
	unsigned int v_inc_start;

	write_reg(REGISTER_THUMB_GG_MBLK_ENABLE, 1);
	write_reg(REGISTER_THUMB_GG_MBLK_START_ADDR,
		 ((param->path.extra_frame.hw_buf_info)->paddr >> 2));

	h_block_num = (param->path.extra_frame.framewidthpixels + 15) >> 4;
	if (param->yuv_format == JPEG_PATH_FORMAT_422) {
		v_block_num =
		    (param->path.extra_frame.frameheight + 7) >> 3;
		v_inc_start = h_block_num;
	} else {
		v_block_num =
		    (param->path.extra_frame.frameheight + 15) >> 4;
		v_inc_start = h_block_num * 2;
	}

	write_reg(REGISTER_THUMB_GG_MBLK_STRIDE,
		 h_block_num);
	write_reg(REGISTER_THUMB_GG_MBLK_BLOCKS_NUM,
		 h_block_num);
	write_reg(REGISTER_THUMB_GG_MBLK_H_STRIPS_NUM,
		 v_block_num);
	write_reg(REGISTER_THUMB_GG_MBLK_V_INC_ADDR_START,
		 v_inc_start);
	write_reg(REGISTER_THUMB_GG_MBLK_V_INC_ADDR_MIDDLE,
		 v_inc_start);
	write_reg(REGISTER_THUMB_GG_MBLK_LINE_SIZE_START,
		 0x00000001);
	write_reg(REGISTER_THUMB_GG_MBLK_LINE_SIZE_MIDDLE,
		 0x00000001);
	write_reg(REGISTER_THUMB_GG_MBLK_LINE_SIZE_LAST,
		 0x00000001);

	if (param->yuv_format == JPEG_PATH_FORMAT_422) {
		write_reg(REGISTER_THUMB_GG_MBLK_LINE_NUM_START, 1);
		write_reg(REGISTER_THUMB_GG_MBLK_LINE_NUM_MIDDLE,
			 1);
		write_reg(REGISTER_THUMB_GG_MBLK_LINE_NUM_LAST, 1);
		write_reg(REGISTER_THUMB_GG_MBLK_LAST, 0x00000001);
	} else {
		write_reg(REGISTER_THUMB_GG_MBLK_LINE_NUM_START, 2);
		write_reg(REGISTER_THUMB_GG_MBLK_LINE_NUM_MIDDLE,
			 2);
		write_reg(REGISTER_THUMB_GG_MBLK_LINE_NUM_LAST, 2);
		write_reg(REGISTER_THUMB_GG_MBLK_LAST, 0x00000002);
	}
	write_reg(REGISTER_THUMB_GG_MBLK_FLIP, 0x00000000);
	write_reg(REGISTER_THUMB_GG_MBLK_H_INC_ADDR_START,
		 0x00000001);
	write_reg(REGISTER_THUMB_GG_MBLK_H_INC_ADDR_MIDDLE,
		 0x00000001);
	write_reg(REGISTER_THUMB_GG_MBLK_H_INC_ADDR_LAST,
		 0x00000001);
	write_reg(REGISTER_THUMB_GG_MBLK_RD_WR, 1);
	write_reg(REGISTER_THUMB_GG_MBLK_VALID, 1);
}

static int jpeg_wait_interrupt(struct jpeg_codec_param *codec_param)
{
	int rc = 0;
	bool ret = 0;
	int poll = 5;
	unsigned long data;
	struct dev_intr_info *jpeg_info = &jpeg.jpeg_info;

	dbg_msg(1, KERN_INFO"wait cond %d\n", 100);
	rc = wait_event_interruptible_timeout(
		jpeg_info->wq,
		1,
		100);
	if (!rc) {
		dbg_msg(1, "wait timeout!\n");
		ret = -ETIMEDOUT;
	}

	data = read_reg(REGISTER_JPEG_INT_CTRL_STAT);
	while (poll) {
		data = read_reg(REGISTER_JPEG_INT_CTRL_STAT);
		poll--;
	}

	if (codec_param->mode == JPEG_PATH_MODE_ENCODER_FINAL)
		data = read_reg(REGISTER_VLC_BTS_CNT);
	else if (codec_param->mode == JPEG_PATH_MODE_DECODER)
		write_reg(REGISTER_CODE_GG_LINE_ABORT, 1);

	if (jpeg_info->bfail) {
		dbg_msg(1, "JPEGInterruptFunc:handle the fail\n");
		jpeg_info->bfail = false;
		ret = -EINVAL;
	} else {
		dbg_msg(1, "JPEGInterruptFunc:clear the interrupts\n");
	}

	return ret;
}

static void jpeg_set_default(struct jpeg_codec_param *param)
{
	union un_codec_jpeg_config codec_config;
	union un_code_mode codec_mode;

	codec_mode.reg_code_mode = 0;
	codec_mode.s_code_mode.enc_dec_mode = 1;
	codec_mode.s_code_mode.jpeg_color_format = 0;
	codec_mode.s_code_mode.standard = 2;

	if (param->yuv_format == JPEG_PATH_FORMAT_422)
		codec_mode.s_code_mode.inout_color_format = 0;
	else
		codec_mode.s_code_mode.inout_color_format = 1;
	if (param->mode == JPEG_PATH_MODE_ENCODER_FINAL) {
		if (param->yuv_format == JPEG_PATH_FORMAT_422)
			codec_mode.reg_code_mode = 0x00000005;
		else
			codec_mode.reg_code_mode = 0x00000015;
		write_reg(REGISTER_CODEC_MODE,
			 codec_mode.reg_code_mode);
		if (param->create_dc) {
			codec_config.reg_codec_jpeg_config = 3;
			codec_config.s_codec_jpeg_config.dc_create = 1;
		} else {
			codec_config.reg_codec_jpeg_config = 2;
			codec_config.s_codec_jpeg_config.dc_create = 0;
		}
		codec_config.s_codec_jpeg_config.code_create = 1;
		write_reg(REGISTER_CODEC_JPEG_CONFIG,
			 codec_config.reg_codec_jpeg_config);
	} else if (param->mode == JPEG_PATH_MODE_DECODER) {

		if (param->yuv_format == JPEG_PATH_FORMAT_422)
			codec_mode.reg_code_mode = 0x00000004;
		else
			codec_mode.reg_code_mode = 0x00000014;
		write_reg(REGISTER_CODEC_MODE, codec_mode.reg_code_mode);
	}
	write_reg(REGISTER_CODEC_ENCDEC_RESET, 0x00000001);
}

static void jpeg_update_thumbnail_burst(unsigned char addr_align)
{
	write_reg(REGISTER_THUMB_BG_MAX_BURST_SIZE, (addr_align << 1));
	write_reg(REGISTER_THUMB_BG_MIN_BURST_SIZE, (addr_align >> 2));
	write_reg(REGISTER_THUMB_BG_ADDR_ALIGN, addr_align);
}

static void jpeg_update_code_line_burst(unsigned char addr_align)
{
	write_reg(REGISTER_CODE_BG_MAX_BURST_SIZE, addr_align << 2);
	write_reg(REGISTER_CODE_BG_MIN_BURST_SIZE, addr_align >> 1);
	write_reg(REGISTER_CODE_BG_ADDR_ALIGN, addr_align);
}

static void jpeg_update_image_mb_geometry(struct jpeg_codec_param *param)
{
	unsigned int h_block_num, v_block_num, pixels;
	unsigned int h_inc_start, h_inc_middle, h_inc_last;
	unsigned int v_inc_start, v_inc_middle, line_num_start, line_num_middle,
	    line_num_last, stride;
	unsigned char *pdata;
	unsigned int i;

	pdata = (unsigned char *)((param->path.in_frame.hw_buf_info)->vaddr);
	dbg_msg(1, "vaddr = %x\n",
	       (param->path.in_frame.hw_buf_info)->vaddr);
	dbg_msg(1, "paddr = %x\n",
	       (param->path.in_frame.hw_buf_info)->paddr);
	pixels =
	    param->path.in_frame.frameheight *
	    param->path.in_frame.framewidthbytes;
	for (i = 0; i < 1; i++) {
		mdelay(10);
		dbg_msg(1, "!! delay 10 ms\n");
	}
	dbg_msg(1, "pixels = %d\n", pixels);
	dbg_msg(1, "JPG:Input data image:\r\n");

	dbg_msg(1, "%x %x %x %x %x\r\n", pdata[pixels - 10],
		  pdata[pixels - 9], pdata[pixels - 8], pdata[pixels - 7],
		  pdata[pixels - 6]);
	dbg_msg(1, "%x %x %x %x %x\r\n", pdata[pixels - 5], pdata[pixels - 4],
		  pdata[pixels - 3], pdata[pixels - 2], pdata[pixels - 1]);

	h_block_num = (param->path.in_frame.framewidthpixels + 15) >> 4;
	stride = (h_block_num << 3);
	if (param->yuv_format == JPEG_PATH_FORMAT_422) {
		v_block_num = (param->path.in_frame.frameheight + 7) >> 3;
		h_inc_start = 8;
		h_inc_middle = 8;
		h_inc_last = 8;
		v_inc_start = stride << 3;
		line_num_start = 8;
		line_num_middle = 8;
		line_num_last = 8;
	} else {
		v_block_num = (param->path.in_frame.frameheight + 15) >> 4;
		h_inc_start = 8;
		h_inc_middle = 8;
		h_inc_last = 8;
		v_inc_start = stride << 4;
		line_num_start = 16;
		line_num_middle = 16;
		line_num_last = 16;
	}
	v_inc_middle = v_inc_start;

	/*write Multi-block Geometry Gen parameter*/
	write_reg(REGISTER_IMAGE_GG_MBLK_BLOCKS_NUM,
		 h_block_num);
	write_reg(REGISTER_IMAGE_GG_MBLK_STRIDE, stride);
	write_reg(REGISTER_IMAGE_GG_MBLK_LINE_SIZE_START,
		 0x00000008);
	write_reg(REGISTER_IMAGE_GG_MBLK_LINE_SIZE_MIDDLE,
		 0x00000008);
	write_reg(REGISTER_IMAGE_GG_MBLK_LINE_SIZE_LAST,
		 0x00000008);
	write_reg(REGISTER_IMAGE_GG_MBLK_LINE_NUM_START,
		 line_num_start);
	write_reg(REGISTER_IMAGE_GG_MBLK_LINE_NUM_MIDDLE,
		 line_num_middle);
	write_reg(REGISTER_IMAGE_GG_MBLK_LINE_NUM_LAST,
		 line_num_last);
	write_reg(REGISTER_IMAGE_GG_MBLK_LAST,
	0x00000001);
	write_reg(REGISTER_IMAGE_GG_MBLK_FLIP, 0);
	write_reg(REGISTER_IMAGE_GG_MBLK_H_INC_ADDR_START,
		 h_inc_start);
	write_reg(REGISTER_IMAGE_GG_MBLK_H_INC_ADDR_MIDDLE,
		 h_inc_middle);
	write_reg(REGISTER_IMAGE_GG_MBLK_H_INC_ADDR_LAST,
		 h_inc_last);
	write_reg(REGISTER_IMAGE_GG_MBLK_V_INC_ADDR_START,
		 v_inc_start);
	write_reg(REGISTER_IMAGE_GG_MBLK_V_INC_ADDR_MIDDLE,
		 v_inc_middle);
	write_reg(REGISTER_IMAGE_GG_MBLK_H_STRIPS_NUM,
		 v_block_num);

	jpeg_update_code_line_burst(16);
	/*write Line Geometry Gen parameter.*/
	write_reg(REGISTER_CODE_GG_LINE_SIZE, 0x00FFFFFF);
	write_reg(REGISTER_CODE_GG_LINE_LAST, 0x00000001);
	/*write the number of macroblocks in an encoded/decoded image.*/
	write_reg(REGISTER_CODEC_JPEG_MB_NUM,
		 h_block_num * v_block_num);

	/*Indicates the access direction. Set to Read
	(0) when encoding and Write (1) when decoding*/
	if (param->mode == JPEG_PATH_MODE_ENCODER_FINAL)
		write_reg(REGISTER_IMAGE_GG_MBLK_RD_WR, 0);
	else if (param->mode == JPEG_PATH_MODE_DECODER)
		write_reg(REGISTER_IMAGE_GG_MBLK_RD_WR, 1);
	/*write DRAM Start address of transaction*/
	write_reg(REGISTER_IMAGE_GG_MBLK_START_ADDR,
		 ((param->path.in_frame.hw_buf_info)->paddr >> 2));
	/*Indicates that entire multi-block descriptor
	parameters group is available.*/
	write_reg(REGISTER_IMAGE_GG_MBLK_VALID, 1);
}

static void jpeg_update_quant_table(struct jpeg_codec_param *param)
{
	unsigned long *pulqt = (unsigned long *)REGISTER_QT_FIRST_Q_MATRIX;
	short i;

	dbg_msg(1, ("JPG:jpeg_update_quant_table\r\n"));
	for (i = 0; i < 64; i++)
		write_reg(REGISTER_QT_FIRST_Q_MATRIX,
			(unsigned int)(param->y_qt[i]));

	pulqt = (unsigned long *)REGISTER_QT_SECOND_Q_MATRIX;
	for (i = 0; i < 64; i++)
		write_reg(REGISTER_QT_SECOND_Q_MATRIX,
			 (unsigned int)(param->c_qt[i]));

	dbg_msg(1, ("JPG:jpeg_update_quant_table end\r\n"));
}

static void jpeg_update_vlc_table(struct jpeg_codec_param *param)
{
	short i;
	union un_vlc_stuffing_type vlc_stuff_t;

	write_reg(REGISTER_VLC_START_FILL_ADDR, 0);
	for (i = 0; i < param->table_size; i++)
		write_reg(REGISTER_VLC_TABLE_MEM_FILL,
			 (param->t_mem[i]));
	write_reg(REGISTER_VLC_START_FILL_ADDR, 0);
	for (i = 0; i < param->c_mem_size; i++)
		write_reg(REGISTER_VLC_CODE_MEM_FILL, param->c_mem[i]);

	write_reg(REGISTER_VLC_HW_POINTERS, VLM_HW_TABLE_JPEG);

	vlc_stuff_t.reg_vlc_stuffing_type = 0;
	vlc_stuff_t.s_vlc_stuffing_type.all_stuf_bits_but_msb = 1;
	vlc_stuff_t.s_vlc_stuffing_type.msb_stuf_bit = 1;
	write_reg(REGISTER_VLC_STUFFING_TYPE,
		vlc_stuff_t.reg_vlc_stuffing_type);
	write_reg(REGISTER_VLC_ENABLE_AND_RESET, 0x00000001);
}

static void jpeg_set_interrupt_mask(bool enable)
{
	union reg_jpeg_int_mask int_mask;

	int_mask.reg_jpeg_int_mask = 0;
	if (enable) {
		dbg_msg(1, "JPG:JPEG_INT_MASK enable mask 1\r\n");
		int_mask.s_jpeg_int_mask.image_end_int = 0;
		int_mask.s_jpeg_int_mask.jpeg_end = 1;
		int_mask.s_jpeg_int_mask.sample_done = 0;
		int_mask.s_jpeg_int_mask.code_end_int = 0;
		int_mask.s_jpeg_int_mask.thumb_end_int = 0;
	}

	write_reg(REGISTER_JPEG_INT_MASK, int_mask.reg_jpeg_int_mask);
}

static void jpeg_update_image_burst(unsigned int addr_align)
{
	write_reg(REGISTER_IMAGE_BG_MAX_BURST_SIZE, addr_align);
	write_reg(REGISTER_IMAGE_BG_MIN_BURST_SIZE, addr_align >> 2);
	write_reg(REGISTER_IMAGE_BG_ADDR_ALIGN, addr_align);
}

static void
jpeg_update_code_line_geometry(unsigned int size,
			       unsigned int addr, unsigned int read_or_write)
{
	write_reg(REGISTER_CODE_GG_LINE_RD_WR, read_or_write);
	write_reg(REGISTER_CODE_GG_LINE_START_ADDR, (addr >> 2));
	write_reg(REGISTER_CODE_GG_LINE_VALID, 1);
}

static void jpeg_setclients(struct jpeg_codec_param *param)
{
	unsigned int read_or_write;

	jpeg_set_interrupt_mask(true);
	write_reg(REGISTER_ARBITER_CL_CODE_DISABLE, 1);
	write_reg(REGISTER_ARBITER_CL_IMAGE_DISABLE,
		 0x00000001);
	write_reg(REGISTER_ARBITER_CL_THUMB_DISABLE, 1);

	if (param->path.in_frame.color_fmt < 8)
		write_reg(REGISTER_BRIDGE_CL_IMAGE_BYTE_SWAP,
			 (unsigned int)(param->path.in_frame.color_fmt));

	if (param->path.extra_frame.color_fmt < 8)
		write_reg(REGISTER_BRIDGE_CL_THUMB_BYTE_SWAP,
			 (unsigned int)(param->path.extra_frame.color_fmt));

	if (param->path.out_frame.color_fmt < 8)
		write_reg(REGISTER_BRIDGE_CL_CODE_BYTE_SWAP,
			 (unsigned int)(param->path.out_frame.color_fmt));

	/* IMAGE_BG */
	jpeg_update_image_burst(32);
	/* IMAGE_GG */
	jpeg_update_image_mb_geometry(param);
	/* CODE_BG */

	/* CODE_GG */
	dbg_msg(1, "TEST output code Frame 0x%x\r\n",
		  (int)param->path.out_frame.hw_buf_info);
	read_or_write = param->mode == JPEG_PATH_MODE_ENCODER_FINAL ? 1 : 0;
	jpeg_update_code_line_geometry(param->code_buf_size,
				       (param->path.out_frame.hw_buf_info)->
				       paddr, read_or_write);

	if (param->path.extra_frame.color_fmt && (read_or_write == 1)) {
		/* THUMB_BG */
		jpeg_update_thumbnail_burst(32);
		/* THUMB_GG  */
		jpeg_update_thumbnail_mb_geometry(param);
	}

}

static void jpeg_setalign(struct jpeg_codec_param *param)
{
	unsigned int cur_align_bits;
	unsigned int total_align_bits;
	unsigned long last_marker_bits = 0x11003F00;

	param->code_align =
	    (unsigned long)(param->path.out_frame.pbybuffer) %
	    DRAM_WIDTH_IN_BYTES;

	total_align_bits = 8 * param->code_align;

	if (total_align_bits > 0) {
		cur_align_bits = ALIGN_DOWN(total_align_bits,
			       MAX_VAL_OF_VLC_PUSH_POP_NUMBIT);

		if (cur_align_bits < total_align_bits) {
			/* Make sure that there is a  reminder,  */
			/* and  we're not writing  zeros */
			write_reg(REGISTER_VLC_PUSH_POP_DATA,
				 last_marker_bits >> cur_align_bits);
			write_reg(REGISTER_VLC_PUSH_POP_NUMBIT,
				 total_align_bits - cur_align_bits);
		}

		while (cur_align_bits > 0) {
			unsigned long mask =
			    0xFFFFFFFFUL >> (32 - cur_align_bits);

			unsigned long val;

			cur_align_bits -= MAX_VAL_OF_VLC_PUSH_POP_NUMBIT;

			val = (last_marker_bits & mask) >> cur_align_bits;

			write_reg(REGISTER_VLC_PUSH_POP_DATA, val);

			write_reg(REGISTER_VLC_PUSH_POP_NUMBIT,
				 MAX_VAL_OF_VLC_PUSH_POP_NUMBIT);
		}
	}
}

static void jpeg_go(struct jpeg_codec_param *param)
{
	unsigned long code_stat;
	unsigned long int_stat;

	write_reg(REGISTER_CODE_GG_LINE_ENABLE, 1);
	do {
		code_stat =
		    read_reg(REGISTER_CODE_GG_LINE_STATUS);
	} while (code_stat != 1);

	if (param->mode == JPEG_PATH_MODE_DECODER) {
		int poll = 10;
		unsigned long vlc_stat;

		vlc_stat = read_reg(REGISTER_VLC_CODE_FIFO_GAP);
		while (poll) {
			vlc_stat = read_reg(REGISTER_VLC_CODE_FIFO_GAP);
			poll--;
		}
		poll = 5;
		write_reg(REGISTER_VLC_VLCD_LOAD, 1);
		vlc_stat =
		    read_reg(REGISTER_VLC_VLCD_LOADED);
		while (poll) {
			vlc_stat = read_reg(REGISTER_VLC_VLCD_LOADED);
			poll--;
		}
	}
	int_stat = read_reg(REGISTER_JPEG_INT_CTRL_STAT);
	write_reg(REGISTER_CONVERTER_RESET, 0x00000001);
	write_reg(REGISTER_CODEC_JPEG_GO, 1);
}

static int jpeg_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int jpeg_close(struct inode *inode, struct file *filp)
{
	return 0;
}

static long jpeg_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	bool ret = true;
	struct jpeg_codec_param codec_param;

	switch (cmd) {
	case IOCTL_JPEG_OPEN_DEVICE: {
		dbg_msg(1, "IOCTL_JPEG_OPEN_DEVICE\r\n");
		break;
	}
	case IOCTL_JPEG_UPDATE_VLC_TABLE: {
		dbg_msg(1, ("IOCTL_JPEG_UPDATE_VLC_TABLE\r\n"));
		if (copy_from_user
		    (&codec_param, (void __user *)arg,
		     sizeof(codec_param))) {
			dbg_msg(1,
				  ("[ERR]: UPDATE_VLC_TABLE Invalid Param!"));
			ret = false;
		}
		jpeg_update_vlc_table(&codec_param);
		break;
	}
	case IOCTL_JPEG_CLOSE_DEV_BY_ID: {
		dbg_msg(1, ("IOCTL_JPEG_CLOSE_DEV_BY_ID\r\n"));
		break;
	}
	case IOCTL_JPEG_SET_DEFAULT: {
		dbg_msg(1, ("IOCTL_JPEG_SET_DEFAULT\r\n"));
		if (copy_from_user
		    (&codec_param, (void __user *)arg,
		     sizeof(codec_param))) {
			dbg_msg(1,
				  ("[ERR]: JPEG_SET_DEFAULT Invalid Param!"));
			ret = false;
		}
		jpeg_set_default(&codec_param);
		break;
	}
	case IOCTL_JPEG_UPDATEQT: {
		dbg_msg(1, ("IOCTL_JPEG_UPDATEQT\r\n"));
		if (copy_from_user
		    (&codec_param, (void __user *)arg,
		     sizeof(codec_param))) {
			dbg_msg(1,
				  ("[ERR]: JPEG_SET_DEFAULT Invalid Param!"));
			ret = false;
		}
		jpeg_update_quant_table(&codec_param);
		break;
	}
	case IOCTL_JPEG_GETBUFFER: {
		struct jpg_hw_buf buf_info = {0};
		struct jpg_hw_buf *phwbuf;

		dbg_msg(1, ("IOCTL_JPEG_GETBUFFER\r\n"));
		phwbuf = kzalloc(sizeof(*phwbuf), GFP_KERNEL);
		if (copy_from_user(phwbuf, (void __user *)arg,
			sizeof(*phwbuf))) {
			dbg_msg(1, "[ERR]: JPEG_GETBUFFER Invalid Param!");
			ret = false;
		}
		dbg_msg(1, "getbuffer copy size %lx from user",
			  phwbuf->size);
		jpeg_alloc_buf(phwbuf->size, &buf_info);
		phwbuf->vaddr = buf_info.vaddr;
		phwbuf->paddr = buf_info.paddr;
		phwbuf->size = buf_info.size;
		phwbuf->real_size = buf_info.real_size;
		if (copy_to_user((void __user *)arg, phwbuf,
			sizeof(*phwbuf))) {
			dbg_msg(1, "[ERR]: JPEG_GETBUFFER Invalid Param!");
			ret = false;
		}
		kfree(phwbuf);
		break;
	}
	case IOCTL_JPEG_FREEBUFFER: {
		struct jpg_hw_buf *phwbuf;

		dbg_msg(1, ("IOCTL_JPEG_FREEBUFFER\r\n"));
		phwbuf = kzalloc(sizeof(*phwbuf), GFP_KERNEL);
		if (copy_from_user(phwbuf, (void __user *)arg,
			sizeof(*phwbuf))) {
			dbg_msg(1,
				("[ERR]: JPEG_FREEBUFFER Invalid Param!"));
			ret = false;
		}
		jpeg_free_buf(phwbuf);
		/*unmap the viradd in user mode. */
		break;
	}
	case IOCTL_JPEG_GO: {
		dbg_msg(1, ("IOCTL_JPEG_GO\r\n"));
		if (copy_from_user
		    (&codec_param, (void __user *)arg,
		     sizeof(codec_param))) {
			dbg_msg(1,
				  ("[ERR]: JPEG_SET_DEFAULT Invalid Param!"));
			ret = false;
		}
		jpeg_go(&codec_param);
		break;
	}
	case IOCTL_JPEG_WAIT: {
		dbg_msg(1, ("IOCTL_JPEG_WAIT\r\n"));
		if (copy_from_user(&codec_param, (void __user *)arg,
			sizeof(codec_param))) {
			dbg_msg(1,
				("[ERR]: IOCTL_JPEG_WAIT Invalid Param!"));
			ret = false;
		}
		dbg_msg(1, "JPEG: -- JPEG_WAIT --\r\n");
		if (0 != jpeg_wait_interrupt(&codec_param))
			ret = false;
		break;
	}
	case IOCTL_JPEG_SETCLIENTS: {
		dbg_msg(1, ("IOCTL_JPEG_SETCLIENTS\r\n"));
		if (copy_from_user(&codec_param, (void __user *)arg,
			sizeof(codec_param))) {
			dbg_msg(1,
			("[ERR]: IOCTL_JPEG_SETCLIENTS Invalid Param!"));
			ret = false;
		}
		dbg_msg(1, ("JPEG: -- IOCTL_JPEG_SETCLIENTS --\r\n"));
		mutex_lock(&jpeg.pool_lock);
		jpeg_setclients(&codec_param);
		mutex_unlock(&jpeg.pool_lock);
		break;
	}
	case IOCTL_JPEG_ALIGN: {
		dbg_msg(1, ("IOCTL_JPEG_ALIGN\r\n"));
		if (copy_from_user(&codec_param, (void __user *)arg,
			sizeof(codec_param))) {
			dbg_msg(1,
				("[ERR]: IOCTL_JPEG_ALIGN Invalid Param!"));
			ret = false;
		}
		dbg_msg(1, ("JPEG: -- IOCTL_JPEG_ALIGN -- \r\n"));
		jpeg_setalign(&codec_param);
		break;
	}
	default:
		dbg_msg(1, ("[ERR]: default\r\n"));
		ret = false;
		break;
	}

	return ret;
}

static int jpeg_mmap(struct file *filp, struct vm_area_struct *vma)
{
	/*the struct has only reg physical address,  */
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			    vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
		dbg_msg(1, "Mmap failed for address %lX\n", vma->vm_pgoff);
		return -EINVAL;
	}

	return 0;
}

static const struct file_operations jpeg_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = jpeg_ioctl,
	.open = jpeg_open,
	.mmap = jpeg_mmap,
	.release = jpeg_close,
};

static struct of_device_id jpeg_match_tbl[] = {
	{.compatible = "sirf,atlas7-jpeg",},
	{ /*end */ }
};

static irqreturn_t jpeg_irq_handler(int irq, void *data)
{

	struct dev_intr_info *intr_info;
	unsigned long read_data;

	intr_info = (struct dev_intr_info *)data;
	wake_up_interruptible(&(intr_info->wq));
	read_data = read_reg(REGISTER_JPEG_INT_CTRL_STAT);
	write_reg(REGISTER_JPEG_INT_CTRL_STAT, 0x0000001F);

	return IRQ_HANDLED;
}

static int jpeg_probe(struct platform_device *pdev)
{
	int ret;
	dev_t devno = 0;
	struct resource *regs = NULL;
	int irq = 0;
	struct jpeg_dev_info *dev_info = NULL;
	struct dev_intr_info *jpeg_info = NULL;
	struct class *jpeg_class = jpeg.jpeg_class;

	ret = alloc_chrdev_region(&devno, 0, 1, "jpeg");
	if (ret < 0) {
		dbg_msg(1, KERN_ERR"%s: jpeg register chrdev failed\n",
			  __FILE__);
		return ret;
	}
	jpeg_class = class_create(THIS_MODULE, "jpeg");
	if (IS_ERR(jpeg_class)) {
		dbg_msg(1, KERN_ERR"jpeg class create failed\n");
		goto ERROR;
	}
	if (device_create(jpeg_class, NULL, devno, NULL, "jpeg") == NULL) {
		dbg_msg(1, KERN_ERR"jpeg devic_create failed\n");
		goto ERROR;
	}
	cdev_init(&jpeg.jpeg_cdev, &jpeg_fops);
	if (cdev_add(&jpeg.jpeg_cdev, devno, 1) == -1) {
		dbg_msg(1, KERN_ERR"jpeg cdev_add failed\n");
		goto ERROR;
	}

	dev_info = &jpeg.dev_info;
	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dev_info->reg_vaddr = (u32)devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR((void *)(dev_info->reg_vaddr)))
		goto ERROR;

	jpeg.ck = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(jpeg.ck)) {
		dbg_msg(1, "jpeg_enable_clock error!\n");
		goto ERROR;
	}

	ret = clk_prepare_enable(jpeg.ck);
	if (ret)
		goto ERROR;

	mutex_init(&jpeg.pool_lock);
	jpeg.devno = devno;
	platform_set_drvdata(pdev, &jpeg);
	dbg_msg(1, KERN_INFO"size = %d, vaddr = 0x%x\n",
		  (u32) dev_info->reg_size,
		  (u32) dev_info->reg_vaddr);
	pdev->dev.coherent_dma_mask = ~0;
	jpeg_get_hw_pool(pdev);

	jpeg_info = &jpeg.jpeg_info;
	irq = platform_get_irq(pdev, 0);
	jpeg_info->irq_id = irq;
	if (jpeg_info->irq_id == 0) {
		dbg_msg(1, KERN_ERR"jpeg0: Error mapping IRQ!\n");
		goto ERROR;
	}
	dbg_msg(1, KERN_INFO"DevInfo irq = %d\n", jpeg_info->irq_id);

	dbg_msg(1, KERN_INFO"Install ISR irq = %d\n", jpeg_info->irq_id);
	init_waitqueue_head(&jpeg_info->wq);
	ret = devm_request_irq(&pdev->dev, jpeg_info->irq_id,
			jpeg_irq_handler, 0,
			"sirf,mjpeg", jpeg_info);
	return ret;
ERROR:
	if (jpeg.ck)
		clk_disable_unprepare(jpeg.ck);
	if (jpeg_class && devno)
		device_destroy(jpeg_class, devno);
	if (jpeg_class)
		class_destroy(jpeg_class);
	if (devno)
		unregister_chrdev_region(devno, 1);
	mutex_destroy(&jpeg.pool_lock);

	return ret;
}

static int jpeg_remove(struct platform_device *pdev)
{
	struct clk *ck = jpeg.ck;

	clk_disable_unprepare(ck);
	mutex_destroy(&jpeg.pool_lock);
	device_destroy(jpeg.jpeg_class, jpeg.devno);
	class_destroy(jpeg.jpeg_class);
	unregister_chrdev_region(jpeg.devno, 1);
	return 0;
}

static struct platform_driver jpeg_driver = {
	.driver = {
		.name = "csr_atlas7jpeg",
		.owner = THIS_MODULE,
		.of_match_table = jpeg_match_tbl,
	},
	.probe = jpeg_probe,
	.remove = jpeg_remove,
};

module_platform_driver(jpeg_driver);

MODULE_DESCRIPTION("JPEG base driver module");
MODULE_AUTHOR("Lily Li <Lily.Li@csr.com>");
MODULE_LICENSE("GPL v2");

