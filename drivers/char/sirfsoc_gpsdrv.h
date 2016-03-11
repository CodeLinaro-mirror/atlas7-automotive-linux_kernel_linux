/*
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

#ifndef __SIRFSOC_GPSDRV_H__
#define __SIRFSOC_GPSDRV_H__

#include <linux/cdev.h>
#include <asm-generic/ioctl.h>

/*device node used by DSP&TriG*/
#define GPS_NODEPATH_DTS	"/axi/dsp-iobg/gps@a8010000"
#define DSPIF_NODEPATH_DTS	"/axi/dsp-iobg/dspif@a8000000"
#define DSP_NODEPATH_DTS	"/axi/dsp-iobg/dsp@a9000000"
#define CPHIF_NODEPATH_DTS	"/axi/sys-iobg/cphifbg@88030000"
#define GPSRTC_NODEPATH_DTS	"/axi/rtc-iobg/gpsrtc@1000"
#define SYSRTC_NODEPATH_DTS	"/axi/rtc-iobg/sysrtc@2000"
#define INTC_NODEPATH_DTS	"/axi/interrupt-controller@80020000"

/*DSP regs and defines*/
#define DSPREG_MODE                 0x0000
#define DSPDMA_MODE                 0x0004
#define DSPDMA_DMX_START_ADD        0x0008
#define DSPDMA_PM_START_ADD         0x000c
#define DSPDMA_LENGTH               0x0010
#define DSPDMA_DRAM_START_ADD       0x0014
#define DSPDMA_PITCH                0x0018
#define DSPDMA_BYTEMODE             0x001c
#define DSP_MEM_MODE                0x0020
#define DSPDMA_DMY_START_ADD        0x0024
#define DSPIDMA_PM_ENDADD           0x0040
#define DSPFSM_RST_B                0x0044
#define DSP_DIV_CLK                 0x0048
#define RISC_INT_DSP                0x0060
#define DSP_INT_RISC                0x0064
#define DSP_GEN_REG0                0x0080
#define DSP_GEN_REG1                0x0084
#define DSP_GEN_REG2                0x0088
#define DSP_GEN_REG3                0x008c

#define DSPIDMA_SET_ADDR            0x0
#define DSPIDMA_SET_TYPE            0x8
#define DSPIDMA_DATA                0x10
#define DSPIDMA_REBOOT              0x18

#define DM_ADDR_OFFSET				0x4000

#define DMX_START_ADDR				0x0000
#define DMY_START_ADDR				0x3c00
#define PM_START_ADDR				0x0000
#define DMX_IN_SIZE					0x0400
#define DMX_SWAP_SIZE				0x0400
#define DMY_SIZE					0x0400
#define PM_IN_SIZE					0x0400
#define PM_SWAP_SIZE				0x0400

#define PM_DOUBLE_BUFFER			0x00000001L
#define SDRAM_ACCESS_PM				0x00000002L
#define DM_DOUBLE_BUFFER			0x00000004L
#define SDRAM_ACCESS_DM				0x00000008L
#define DMA_BUSY					0x00000010L
#define DMA_FOR_DMX					0x00000020L
#define DMA_TO_SDRAM				0x00000040L
#define DMX_ENDIAN					0x00100000L
#define SRAM_OE						0x00200000L

#define CPU_CLOCK				1
#define DSP_CLOCK				2

/*DSP related structures declare*/
struct DSP_BUF_INFO {
	/* start address of buffer */
	int addr;
	/* length of buffer */
	int length;
	/* data buffer, type depend on the data type */
	void *p_buf;
};

struct DSP_RW_GENERAL_REG {
	/* index of general register */
	int index;
	unsigned long value;
};

struct DSP_R_GET_RETURN_CODE {
	char dummy[16];
};

struct DSP_MEM_INFO {
	int addr;
	int size;
};

struct GPS_RTC_CLK_INFO {
	unsigned int clock_id;
	/* If -1 then divider will not be set */
	int divider;
	unsigned int gps_rtc_value;
};

/*DSP IOCTL numbers*/
#define DSP_IOC_MAGIC  'D'

#define IOCTL_DSP_READ_DMX_BUFFER	_IOR(DSP_IOC_MAGIC,  1,\
					struct DSP_BUF_INFO)
#define IOCTL_DSP_WRITE_DMX_BUFFER	_IOR(DSP_IOC_MAGIC,  2,\
					struct DSP_BUF_INFO)
#define IOCTL_DSP_READ_DMY_BUFFER	_IOR(DSP_IOC_MAGIC,  3,\
					struct DSP_BUF_INFO)
#define IOCTL_DSP_WRITE_DMY_BUFFER	_IOR(DSP_IOC_MAGIC,  4,\
					struct DSP_BUF_INFO)
#define IOCTL_DSP_READ_PM_BUFFER	_IOR(DSP_IOC_MAGIC,  5,\
					struct DSP_BUF_INFO)
#define IOCTL_DSP_WRITE_PM_BUFFER	_IOR(DSP_IOC_MAGIC,  6,\
					struct DSP_BUF_INFO)
#define IOCTL_DSP_EXECUTE			_IOR(DSP_IOC_MAGIC,  7, int)
#define IOCTL_DSP_START_DSP			_IO(DSP_IOC_MAGIC,  8)
#define IOCTL_DSP_WAIT_COMPLETE		_IOR(DSP_IOC_MAGIC,  9, int)
#define IOCTL_DSP_GET_STATUS		_IOR(DSP_IOC_MAGIC, 10, int)
#define IOCTL_DSP_READ_GEN_REG		_IOR(DSP_IOC_MAGIC, 11,\
					struct DSP_RW_GENERAL_REG)
#define IOCTL_DSP_WRITE_GEN_REG		_IOR(DSP_IOC_MAGIC, 12,\
					struct DSP_RW_GENERAL_REG)
/*#define IOCTL_DSP_GET_RETUEN_CODE	_IOR(DSP_IOC_MAGIC, 13,\
						unsigned short)*/
#define IOCTL_DSP_GET_RETUEN_CODE	_IOR(DSP_IOC_MAGIC, 13,\
					struct DSP_R_GET_RETURN_CODE)
#define IOCTL_DSP_WAIT_INTERRUPT	_IOR(DSP_IOC_MAGIC, 14, int)
#define IOCTL_DSP_FORCE_INTERRUPT	_IO(DSP_IOC_MAGIC, 15)
#define IOCTL_DSP_GET_RES_MEM_INFO	_IOR(DSP_IOC_MAGIC, 16,\
					struct DSP_MEM_INFO)
#define IOCTL_DSP_GET_CLOCK_INFO	_IOR(DSP_IOC_MAGIC, 17, int)
#define IOCTL_DSP_GPS_RF_POWER_CTRL	_IOR(DSP_IOC_MAGIC, 18, int)
#define IOCTL_DSP_GPS_RF_RESET		_IOR(DSP_IOC_MAGIC, 19, unsigned int)
#define IOCTL_DSP_SET_CLK_SRC		_IOR(DSP_IOC_MAGIC, 20,\
					struct GPS_RTC_CLK_INFO)
#define IOCTL_DSP_INIT_GPS		_IO(DSP_IOC_MAGIC, 21)
#define IOCTL_DSP_UNINIT_GPS		_IO(DSP_IOC_MAGIC, 22)
#define IOCTL_DSP_READ_RTC		_IO(DSP_IOC_MAGIC, 23)
#define IOCTL_RF_CLK_OUT		_IO(DSP_IOC_MAGIC, 24)
#define IOCTL_GPIO_REQUEST		_IO(DSP_IOC_MAGIC, 25)
#define IOCTL_GPIO_FREE			_IO(DSP_IOC_MAGIC, 26)

#define IOCTL_SET_CPU_FREQ_TO_MAX	_IO(DSP_IOC_MAGIC, 27)
#define IOCTL_RESET_CPU_FREQ_TO_DEFAULT	_IO(DSP_IOC_MAGIC, 28)

#define DSP_IOC_MAXNR	29

/*regs for TriG
*Imported from \depot\digital\chips\trig\dev\source\fpga\rtl\io_map.v */
#define ISP_BE_ON_OFF_CTL                   0x0278   /*  RW  28 bits */
#define ISP_BE_STAGGER_CFG                  0x027C   /*  RW   4 bits */
#define ISP_CWREM_FINE_LAT_CFG              0x0280   /*  RW   2 bits */
#define ISP_TC_0_LOOPS_CFG                  0x0284   /*  RW  18 bits */
#define ISP_TC_0_FREQ_CFG                   0x0288   /*  RW  24 bits */
#define ISP_TC_0_GAIN_CFG                   0x028C   /*  RW  20 bits */
#define ISP_TC_1_LOOPS_CFG                  0x0298   /*  RW  18 bits */
#define ISP_TC_1_FREQ_CFG                   0x029C   /*  RW  24 bits */
#define ISP_TC_1_GAIN_CFG                   0x02A0   /*  RW  20 bits */
#define ISP_GPS_QUANT_CFG_A                 0x02AC   /*  RW  32 bits */
#define ISP_GPS_QUANT_CFG_B                 0x02B0   /*  RW   5 bits */
#define ISP_GLO_QUANT_CFG_A                 0x02B8   /*  RW  32 bits */
#define ISP_GLO_QUANT_CFG_B                 0x02BC   /*  RW   5 bits */
#define ISP_SPEC_LDR_0_QUANT_CFG_A          0x02CC   /*  RW  32 bits */
#define ISP_SPEC_LDR_0_QUANT_CFG_B          0x02D0   /*  RW   5 bits */
#define ISP_SPEC_LDR_1_QUANT_CFG_A          0x02E0   /*  RW  32 bits */
#define ISP_SPEC_LDR_1_QUANT_CFG_B          0x02E4   /*  RW   5 bits */
#define ISP_CWDET_0_RUN_CTL                 0x033C   /*  RW   5 bits */
#define ISP_CWDET_0_SCAN_RANGE_CFG          0x0344   /*  RW  19 bits */
#define ISP_CWDET_0_FREQ_BLOCK_CFG          0x0348   /*  RW  24 bits */
#define ISP_CWDET_0_SCAN_DWELL_CFG          0x034C   /*  RW  19 bits */
#define ISP_CWDET_0_SCAN_DELTA_FREQ_CFG     0x0350   /*  RW  24 bits */
#define ISP_CWDET_0_SCAN_START_FREQ_CFG     0x0354   /*  RW  24 bits */
#define ISP_CWDET_0_SCAN_NOISE_PERIOD_CFG   0x0358   /*  RW  12 bits */
#define ISP_CWDET_0_MON_NUM_CFG             0x03A0   /*  RW   3 bits */
#define ISP_CWDET_0_MON_FREQ0_CFG           0x03A4   /*  RW  24 bits */
#define ISP_CWDET_0_MON_FREQ1_CFG           0x03A8   /*  RW  24 bits */
#define ISP_CWDET_0_MON_FREQ2_CFG           0x03AC   /*  RW  24 bits */
#define ISP_CWDET_0_MON_FREQ3_CFG           0x03B0   /*  RW  24 bits */
#define ISP_CWDET_0_MON_FREQ4_CFG           0x03B4   /*  RW  24 bits */
#define ISP_CWDET_0_MON_FREQ5_CFG           0x03B8   /*  RW  24 bits */
#define ISP_CWDET_0_MON_FREQ6_CFG           0x03BC   /*  RW  24 bits */
#define ISP_CWDET_0_MON_FREQ7_CFG           0x03C0   /*  RW  24 bits */
#define ISP_CWDET_0_MON_DWELL0_CFG          0x03C4   /*  RW  19 bits */
#define ISP_CWDET_0_MON_DWELL1_CFG          0x03C8   /*  RW  19 bits */
#define ISP_CWDET_0_MON_DWELL2_CFG          0x03CC   /*  RW  19 bits */
#define ISP_CWDET_0_MON_DWELL3_CFG          0x03D0   /*  RW  19 bits */
#define ISP_CWDET_0_MON_DWELL4_CFG          0x03D4   /*  RW  19 bits */
#define ISP_CWDET_0_MON_DWELL5_CFG          0x03D8   /*  RW  19 bits */
#define ISP_CWDET_0_MON_DWELL6_CFG          0x03DC   /*  RW  19 bits */
#define ISP_CWDET_0_MON_DWELL7_CFG          0x03E0   /*  RW  19 bits */
#define ISP_CWDET_0_MON_DELTA_FREQ0_CFG     0x03E4   /*  RW  24 bits */
#define ISP_CWDET_0_MON_DELTA_FREQ1_CFG     0x03E8   /*  RW  24 bits */
#define ISP_CWDET_0_MON_DELTA_FREQ2_CFG     0x03EC   /*  RW  24 bits */
#define ISP_CWDET_0_MON_DELTA_FREQ3_CFG     0x03F0   /*  RW  24 bits */
#define ISP_CWDET_0_MON_DELTA_FREQ4_CFG     0x03F4   /*  RW  24 bits */
#define ISP_CWDET_0_MON_DELTA_FREQ5_CFG     0x03F8   /*  RW  24 bits */
#define ISP_CWDET_0_MON_DELTA_FREQ6_CFG     0x03FC   /*  RW  24 bits */
#define ISP_CWDET_0_MON_DELTA_FREQ7_CFG     0x0400   /*  RW  24 bits */
#define ISP_CWDET_1_RUN_CTL                 0x0464   /*  RW   5 bits */
#define ISP_CWDET_1_SCAN_RANGE_CFG          0x046C   /*  RW  19 bits */
#define ISP_CWDET_1_FREQ_BLOCK_CFG          0x0470   /*  RW  24 bits */
#define ISP_CWDET_1_SCAN_DWELL_CFG          0x0474   /*  RW  19 bits */
#define ISP_CWDET_1_SCAN_DELTA_FREQ_CFG     0x0478   /*  RW  24 bits */
#define ISP_CWDET_1_SCAN_START_FREQ_CFG     0x047C   /*  RW  24 bits */
#define ISP_CWDET_1_SCAN_NOISE_PERIOD_CFG   0x0480   /*  RW  12 bits */
#define ISP_CWDET_1_MON_NUM_CFG             0x04C8   /*  RW   3 bits */
#define ISP_CWDET_1_MON_FREQ0_CFG           0x04CC   /*  RW  24 bits */
#define ISP_CWDET_1_MON_FREQ1_CFG           0x04D0   /*  RW  24 bits */
#define ISP_CWDET_1_MON_FREQ2_CFG           0x04D4   /*  RW  24 bits */
#define ISP_CWDET_1_MON_FREQ3_CFG           0x04D8   /*  RW  24 bits */
#define ISP_CWDET_1_MON_FREQ4_CFG           0x04DC   /*  RW  24 bits */
#define ISP_CWDET_1_MON_FREQ5_CFG           0x04E0   /*  RW  24 bits */
#define ISP_CWDET_1_MON_FREQ6_CFG           0x04E4   /*  RW  24 bits */
#define ISP_CWDET_1_MON_FREQ7_CFG           0x04E8   /*  RW  24 bits */
#define ISP_CWDET_1_MON_DWELL0_CFG          0x04EC   /*  RW  19 bits */
#define ISP_CWDET_1_MON_DWELL1_CFG          0x04F0   /*  RW  19 bits */
#define ISP_CWDET_1_MON_DWELL2_CFG          0x04F4   /*  RW  19 bits */
#define ISP_CWDET_1_MON_DWELL3_CFG          0x04F8   /*  RW  19 bits */
#define ISP_CWDET_1_MON_DWELL4_CFG          0x04FC   /*  RW  19 bits */
#define ISP_CWDET_1_MON_DWELL5_CFG          0x0500   /*  RW  19 bits */
#define ISP_CWDET_1_MON_DWELL6_CFG          0x0504   /*  RW  19 bits */
#define ISP_CWDET_1_MON_DWELL7_CFG          0x0508   /*  RW  19 bits */
#define ISP_CWDET_1_MON_DELTA_FREQ0_CFG     0x050C   /*  RW  24 bits */
#define ISP_CWDET_1_MON_DELTA_FREQ1_CFG     0x0510   /*  RW  24 bits */
#define ISP_CWDET_1_MON_DELTA_FREQ2_CFG     0x0514   /*  RW  24 bits */
#define ISP_CWDET_1_MON_DELTA_FREQ3_CFG     0x0518   /*  RW  24 bits */
#define ISP_CWDET_1_MON_DELTA_FREQ4_CFG     0x051C   /*  RW  24 bits */
#define ISP_CWDET_1_MON_DELTA_FREQ5_CFG     0x0520   /*  RW  24 bits */
#define ISP_CWDET_1_MON_DELTA_FREQ6_CFG     0x0524   /*  RW  24 bits */
#define ISP_CWDET_1_MON_DELTA_FREQ7_CFG     0x0528   /*  RW  24 bits */
#define ISP_CWREM_CFG                       0x058C   /*  RW   1 bits */
#define ISP_CWREM_BINS_31_0                 0x0590   /*  RW  32 bits */
#define ISP_CWREM_BINS_63_32                0x0594   /*  RW  32 bits */
#define ISP_CWREM_BINS_95_64                0x0598   /*  RW  32 bits */
#define ISP_CWREM_BINS_127_96               0x059C   /*  RW  32 bits */
#define ISP_CWREM_BINS_159_128              0x05A0   /*  RW  32 bits */
#define ISP_CWREM_BINS_191_160              0x05A4   /*  RW  32 bits */
#define ISP_CWREM_BINS_223_192              0x05A8   /*  RW  32 bits */
#define ISP_CWREM_BINS_255_224              0x05AC   /*  RW  32 bits */
#define ISP_TC_2_LOOPS_CFG                  0x0D0C   /*  RW  18 bits */
#define ISP_TC_2_FREQ_CFG                   0x0D10   /*  RW  24 bits */
#define ISP_TC_2_GAIN_CFG                   0x0D14   /*  RW  20 bits */
#define ISP_TC_3_LOOPS_CFG                  0x0D20   /*  RW  18 bits */
#define ISP_TC_3_FREQ_CFG                   0x0D24   /*  RW  24 bits */
#define ISP_TC_3_GAIN_CFG                   0x0D28   /*  RW  20 bits */
#define ISP_TC_4_LOOPS_CFG                  0x0D34   /*  RW  18 bits */
#define ISP_TC_4_FREQ_CFG                   0x0D38   /*  RW  24 bits */
#define ISP_TC_4_GAIN_CFG                   0x0D3C   /*  RW  20 bits */
#define ISP_TC_5_LOOPS_CFG                  0x0D48   /*  RW  18 bits */
#define ISP_TC_5_FREQ_CFG                   0x0D4C   /*  RW  24 bits */
#define ISP_TC_5_GAIN_CFG                   0x0D50   /*  RW  20 bits */

/*Addresses of read only registers*/
#define ISP_TC_0_FREQ_STAT                  0x0290   /*  R   24 bits */
#define ISP_TC_0_GAIN_STAT                  0x0294   /*  R   12 bits */
#define ISP_TC_1_FREQ_STAT                  0x02A4   /*  R   24 bits */
#define ISP_TC_1_GAIN_STAT                  0x02A8   /*  R   12 bits */
#define ISP_GPS_QUANT_STAT                  0x02B4   /*  R   23 bits */
#define ISP_GLO_QUANT_STAT                  0x02C0   /*  R   23 bits */
#define ISP_SPEC_LDR_0_QUANT_STAT           0x02D4   /*  R   23 bits */
#define ISP_SPEC_LDR_1_QUANT_STAT           0x02E8   /*  R   23 bits */
#define ISP_CWDET_0_RUN_STAT                0x0340   /*  R    2 bits */
#define ISP_CWDET_0_SCAN_NOISE_STAT         0x035C   /*  R   24 bits */
#define ISP_CWDET_0_SCAN_PKID0_STAT         0x0360   /*  R   26 bits */
#define ISP_CWDET_0_SCAN_PKID1_STAT         0x0364   /*  R   26 bits */
#define ISP_CWDET_0_SCAN_PKID2_STAT         0x0368   /*  R   26 bits */
#define ISP_CWDET_0_SCAN_PKID3_STAT         0x036C   /*  R   26 bits */
#define ISP_CWDET_0_SCAN_PKID4_STAT         0x0370   /*  R   26 bits */
#define ISP_CWDET_0_SCAN_PKID5_STAT         0x0374   /*  R   26 bits */
#define ISP_CWDET_0_SCAN_PKID6_STAT         0x0378   /*  R   26 bits */
#define ISP_CWDET_0_SCAN_PKID7_STAT         0x037C   /*  R   26 bits */
#define ISP_CWDET_0_SCAN_PKAMPL0_STAT       0x0380   /*  R   14 bits */
#define ISP_CWDET_0_SCAN_PKAMPL1_STAT       0x0384   /*  R   14 bits */
#define ISP_CWDET_0_SCAN_PKAMPL2_STAT       0x0388   /*  R   14 bits */
#define ISP_CWDET_0_SCAN_PKAMPL3_STAT       0x038C   /*  R   14 bits */
#define ISP_CWDET_0_SCAN_PKAMPL4_STAT       0x0390   /*  R   14 bits */
#define ISP_CWDET_0_SCAN_PKAMPL5_STAT       0x0394   /*  R   14 bits */
#define ISP_CWDET_0_SCAN_PKAMPL6_STAT       0x0398   /*  R   14 bits */
#define ISP_CWDET_0_SCAN_PKAMPL7_STAT       0x039C   /*  R   14 bits */
#define ISP_CWDET_0_MON_LAMPL0_STAT         0x0404   /*  R   14 bits */
#define ISP_CWDET_0_MON_LAMPL1_STAT         0x0408   /*  R   14 bits */
#define ISP_CWDET_0_MON_LAMPL2_STAT         0x040C   /*  R   14 bits */
#define ISP_CWDET_0_MON_LAMPL3_STAT         0x0410   /*  R   14 bits */
#define ISP_CWDET_0_MON_LAMPL4_STAT         0x0414   /*  R   14 bits */
#define ISP_CWDET_0_MON_LAMPL5_STAT         0x0418   /*  R   14 bits */
#define ISP_CWDET_0_MON_LAMPL6_STAT         0x041C   /*  R   14 bits */
#define ISP_CWDET_0_MON_LAMPL7_STAT         0x0420   /*  R   14 bits */
#define ISP_CWDET_0_MON_CAMPL0_STAT         0x0424   /*  R   14 bits */
#define ISP_CWDET_0_MON_CAMPL1_STAT         0x0428   /*  R   14 bits */
#define ISP_CWDET_0_MON_CAMPL2_STAT         0x042C   /*  R   14 bits */
#define ISP_CWDET_0_MON_CAMPL3_STAT         0x0430   /*  R   14 bits */
#define ISP_CWDET_0_MON_CAMPL4_STAT         0x0434   /*  R   14 bits */
#define ISP_CWDET_0_MON_CAMPL5_STAT         0x0438   /*  R   14 bits */
#define ISP_CWDET_0_MON_CAMPL6_STAT         0x043C   /*  R   14 bits */
#define ISP_CWDET_0_MON_CAMPL7_STAT         0x0440   /*  R   14 bits */
#define ISP_CWDET_0_MON_UAMPL0_STAT         0x0444   /*  R   14 bits */
#define ISP_CWDET_0_MON_UAMPL1_STAT         0x0448   /*  R   14 bits */
#define ISP_CWDET_0_MON_UAMPL2_STAT         0x044C   /*  R   14 bits */
#define ISP_CWDET_0_MON_UAMPL3_STAT         0x0450   /*  R   14 bits */
#define ISP_CWDET_0_MON_UAMPL4_STAT         0x0454   /*  R   14 bits */
#define ISP_CWDET_0_MON_UAMPL5_STAT         0x0458   /*  R   14 bits */
#define ISP_CWDET_0_MON_UAMPL6_STAT         0x045C   /*  R   14 bits */
#define ISP_CWDET_0_MON_UAMPL7_STAT         0x0460   /*  R   14 bits */
#define ISP_CWDET_1_RUN_STAT                0x0468   /*  R    2 bits */
#define ISP_CWDET_1_SCAN_NOISE_STAT         0x0484   /*  R   24 bits */
#define ISP_CWDET_1_SCAN_PKID0_STAT         0x0488   /*  R   26 bits */
#define ISP_CWDET_1_SCAN_PKID1_STAT         0x048C   /*  R   26 bits */
#define ISP_CWDET_1_SCAN_PKID2_STAT         0x0490   /*  R   26 bits */
#define ISP_CWDET_1_SCAN_PKID3_STAT         0x0494   /*  R   26 bits */
#define ISP_CWDET_1_SCAN_PKID4_STAT         0x0498   /*  R   26 bits */
#define ISP_CWDET_1_SCAN_PKID5_STAT         0x049C   /*  R   26 bits */
#define ISP_CWDET_1_SCAN_PKID6_STAT         0x04A0   /*  R   26 bits */
#define ISP_CWDET_1_SCAN_PKID7_STAT         0x04A4   /*  R   26 bits */
#define ISP_CWDET_1_SCAN_PKAMPL0_STAT       0x04A8   /*  R   14 bits */
#define ISP_CWDET_1_SCAN_PKAMPL1_STAT       0x04AC   /*  R   14 bits */
#define ISP_CWDET_1_SCAN_PKAMPL2_STAT       0x04B0   /*  R   14 bits */
#define ISP_CWDET_1_SCAN_PKAMPL3_STAT       0x04B4   /*  R   14 bits */
#define ISP_CWDET_1_SCAN_PKAMPL4_STAT       0x04B8   /*  R   14 bits */
#define ISP_CWDET_1_SCAN_PKAMPL5_STAT       0x04BC   /*  R   14 bits */
#define ISP_CWDET_1_SCAN_PKAMPL6_STAT       0x04C0   /*  R   14 bits */
#define ISP_CWDET_1_SCAN_PKAMPL7_STAT       0x04C4   /*  R   14 bits */
#define ISP_CWDET_1_MON_LAMPL0_STAT         0x052C   /*  R   14 bits */
#define ISP_CWDET_1_MON_LAMPL1_STAT         0x0530   /*  R   14 bits */
#define ISP_CWDET_1_MON_LAMPL2_STAT         0x0534   /*  R   14 bits */
#define ISP_CWDET_1_MON_LAMPL3_STAT         0x0538   /*  R   14 bits */
#define ISP_CWDET_1_MON_LAMPL4_STAT         0x053C   /*  R   14 bits */
#define ISP_CWDET_1_MON_LAMPL5_STAT         0x0540   /*  R   14 bits */
#define ISP_CWDET_1_MON_LAMPL6_STAT         0x0544   /*  R   14 bits */
#define ISP_CWDET_1_MON_LAMPL7_STAT         0x0548   /*  R   14 bits */
#define ISP_CWDET_1_MON_CAMPL0_STAT         0x054C   /*  R   14 bits */
#define ISP_CWDET_1_MON_CAMPL1_STAT         0x0550   /*  R   14 bits */
#define ISP_CWDET_1_MON_CAMPL2_STAT         0x0554   /*  R   14 bits */
#define ISP_CWDET_1_MON_CAMPL3_STAT         0x0558   /*  R   14 bits */
#define ISP_CWDET_1_MON_CAMPL4_STAT         0x055C   /*  R   14 bits */
#define ISP_CWDET_1_MON_CAMPL5_STAT         0x0560   /*  R   14 bits */
#define ISP_CWDET_1_MON_CAMPL6_STAT         0x0564   /*  R   14 bits */
#define ISP_CWDET_1_MON_CAMPL7_STAT         0x0568   /*  R   14 bits */
#define ISP_CWDET_1_MON_UAMPL0_STAT         0x056C   /*  R   14 bits */
#define ISP_CWDET_1_MON_UAMPL1_STAT         0x0570   /*  R   14 bits */
#define ISP_CWDET_1_MON_UAMPL2_STAT         0x0574   /*  R   14 bits */
#define ISP_CWDET_1_MON_UAMPL3_STAT         0x0578   /*  R   14 bits */
#define ISP_CWDET_1_MON_UAMPL4_STAT         0x057C   /*  R   14 bits */
#define ISP_CWDET_1_MON_UAMPL5_STAT         0x0580   /*  R   14 bits */
#define ISP_CWDET_1_MON_UAMPL6_STAT         0x0584   /*  R   14 bits */
#define ISP_CWDET_1_MON_UAMPL7_STAT         0x0588   /*  R   14 bits */
#define ISP_TC_2_FREQ_STAT                  0x0D18   /*  R   24 bits */
#define ISP_TC_2_GAIN_STAT                  0x0D1C   /*  R   12 bits */
#define ISP_TC_3_FREQ_STAT                  0x0D2C   /*  R   24 bits */
#define ISP_TC_3_GAIN_STAT                  0x0D30   /*  R   12 bits */
#define ISP_TC_4_FREQ_STAT                  0x0D40   /*  R   24 bits */
#define ISP_TC_4_GAIN_STAT                  0x0D44   /*  R   12 bits */
#define ISP_TC_5_FREQ_STAT                  0x0D54   /*  R   24 bits */
#define ISP_TC_5_GAIN_STAT                  0x0D58   /*  R   12 bits */

/*ISP control and status registers*/

/*Addresses of read/write registers*/
#define ISP_FE_ON_OFF_CTL                   0x0200   /*  RW  18 bits */
#define ISP_FE_STAGGER_CFG                  0x0204   /*  RW   1 bits */
#define ISP_GPS_8F0_FINE_LAT_CFG            0x0208   /*  RW   3 bits */
#define ISP_ADC_CFG                         0x020C   /*  RW   8 bits */
#define ISP_AGC_0_CFG_A                     0x0210   /*  RW  30 bits */
#define ISP_AGC_0_CFG_B                     0x0214   /*  RW  10 bits */
#define ISP_AGC_0_GAIN_CFG                  0x0218   /*  RW   5 bits */
#define ISP_AGC_1_CFG_A                     0x0224   /*  RW  30 bits */
#define ISP_AGC_1_CFG_B                     0x0228   /*  RW  10 bits */
#define ISP_AGC_1_GAIN_CFG                  0x022C   /*  RW   5 bits */
#define ISP_BLANK_0_CFG                     0x0238   /*  RW   6 bits */
#define ISP_ACTIVE_BLANK_0_CFG              0x0244   /*  RW  30 bits */
#define ISP_BLANK_1_CFG                     0x024C   /*  RW   6 bits */
#define ISP_ACTIVE_BLANK_1_CFG              0x0258   /*  RW  30 bits */
#define ISP_GPS_LPF_CFG                     0x0270   /*  RW   2 bits */
#define ISP_LO_AGILITY_CFG                  0x0274   /*  RW  13 bits */
#define ISP_ACTIVE_BLANK_0_CFG_B            0x0C0C   /*  RW  25 bits */
#define ISP_ACTIVE_BLANK_1_CFG_B            0x0C10   /*  RW  25 bits */

/*Addresses of read only registers*/
#define ISP_AGC_0_GAIN_STAT                 0x021C   /*  R   24 bits */
#define ISP_AGC_0_ONES_DENS_STAT            0x0220   /*  R   14 bits */
#define ISP_AGC_1_GAIN_STAT                 0x0230   /*  R   24 bits */
#define ISP_AGC_1_ONES_DENS_STAT            0x0234   /*  R   14 bits */
#define ISP_BLANK_0_START_TIME_STAT         0x023C   /*  R   32 bits */
#define ISP_BLANK_0_STOP_TIME_STAT          0x0240   /*  R   32 bits */
#define ISP_ACTIVE_BLANK_0_STAT             0x0248   /*  R    2 bits */
#define ISP_BLANK_1_START_TIME_STAT         0x0250   /*  R   32 bits */
#define ISP_BLANK_1_STOP_TIME_STAT          0x0254   /*  R   32 bits */
#define ISP_ACTIVE_BLANK_1_STAT             0x025C   /*  R    2 bits */
#define ISP_GPS_DCOC_STAT                   0x0260   /*  R   28 bits */
#define ISP_GPS_IQ_BAL_STAT                 0x0264   /*  R   10 bits */
#define ISP_GLO_DCOC_STAT                   0x0268   /*  R   28 bits */
#define ISP_GLO_IQ_BAL_STAT                 0x026C   /*  R   10 bits */

/*ISP control and status registers specific to the TriG chip*/

/*Addresses of read/write registers*/
#define ISP_P2_GPS_QUANT_CFG_A              0x0F00   /*  RW  32 bits */
#define ISP_P2_GPS_QUANT_CFG_B              0x0F04   /*  RW   5 bits */
#define ISP_P2_GLO_QUANT_CFG_A              0x0F0C   /*  RW  32 bits */
#define ISP_P2_GLO_QUANT_CFG_B              0x0F10   /*  RW   5 bits */
#define ISP_P2_CONTROL                      0x0F30   /*  RW  32 bits */
#define ISP_P2_GLO1_NCO                     0x0F34   /*  RW  32 bits */
#define ISP_P2_GLO2_NCO                     0x0F38   /*  RW  32 bits */
#define ISP_P2_GLO3_NCO                     0x0F3C   /*  RW  32 bits */
#define ISP_P2_GLO4_NCO                     0x0F40   /*  RW  32 bits */
#define ISP_P2_GLO5_NCO                     0x0F44   /*  RW  32 bits */
#define ISP_P2_GLO6_NCO                     0x0F48   /*  RW  32 bits */
#define ISP_P2_GLO7_NCO                     0x0F4C   /*  RW  32 bits */
#define ISP_P3_CONTROL                      0x0F50   /*  RW   2 bits */

/*Addresses of read only registers*/
#define ISP_P2_GPS_QUANT_STAT               0x0F08   /*  R   23 bits */
#define ISP_P2_GLO1_QUANT_STAT              0x0F14   /*  R   23 bits */
#define ISP_P2_GLO2_QUANT_STAT              0x0F18   /*  R   23 bits */
#define ISP_P2_GLO3_QUANT_STAT              0x0F1C   /*  R   23 bits */
#define ISP_P2_GLO4_QUANT_STAT              0x0F20   /*  R   23 bits */
#define ISP_P2_GLO5_QUANT_STAT              0x0F24   /*  R   23 bits */
#define ISP_P2_GLO6_QUANT_STAT              0x0F28   /*  R   23 bits */
#define ISP_P2_GLO7_QUANT_STAT              0x0F2C   /*  R   23 bits */

/*TriG control and status registers*/

/*Addresses of read/write registers*/
#define TRIG_CTRL                           0x1000   /*  RW  14 bits */
#define TRIG_INT_EN_MASK                    0x1008   /*  RW  22 bits */
#define TRIG_IO_CTRL1                       0x1010   /*  RW  30 bits */
#define TRIG_IO_CTRL2                       0x1014   /*  RW  30 bits */
#define TRIG_IO_CTRL3                       0x1018   /*  RW  32 bits */
#define TRIG_TEST                           0x101C   /*  RW  21 bits */

/*Addresses of read only registers*/
#define TRIG_INT_STATUS                     0x1004   /*  R   14 bits */
#define TRIG_ID                             0x100C   /*  R   16 bits */

/*GNSS analogue control/status registers for Trig*/

/*Addresses of read/write registers*/
#define TRIG_GNSS_ANA_CTRL_POWER_ENABLES    0x0000   /*  RW  32 bits */
#define TRIG_GNSS_ANA_CTRL_BIAS             0x0004   /*  RW  28 bits */
#define TRIG_GNSS_ANA_CTRL_CLK_A            0x0008   /*  RW  29 bits */
#define TRIG_GNSS_ANA_CTRL_CLK_B            0x000C   /*  RW  31 bits */
#define TRIG_GNSS_ANA_CTRL_IFA_ADC          0x0014   /*  RW  13 bits */
#define TRIG_GNSS_ANA_CTRL_RFA_PWR_DET      0x0018   /*  RW   9 bits */
#define TRIG_GNSS_ANA_CTRL_TESTA            0x0020   /*  RW  32 bits */
#define TRIG_GNSS_ANA_CTRL_TESTB            0x0024   /*  RW  29 bits */
#define TRIG_GNSS_ANA_CTRL_MIXER            0x0028   /*  RW  32 bits */
#define TRIG_GNSS_ANA_CTRL_GLO_AGC          0x002C   /*  RW   9 bits */
#define TRIG_GNSS_ANA_CTRL_GPS_AGC          0x0030   /*  RW   9 bits */
#define TRIG_GNSS_ANA_CTRL_PRODUCTION_TEST  0x0034   /*  RW  17 bits */
#define TRIG_GNSS_ANA_CTRL_CSM              0x0040   /*  RW  26 bits */
#define TRIG_GNSS_ANA_CTRL_BOXO             0x0048   /*  RW   6 bits */
#define TRIG_GNSS_ANA_CTRL_VCO              0x0050   /*  RW  18 bits */
#define TRIG_GNSS_ANA_CTRL_DDS              0x0058   /*  RW  21 bits */
#define TRIG_GNSS_ANA_CTRL_DDS_RISE1        0x005C   /*  RW  32 bits */
#define TRIG_GNSS_ANA_CTRL_DDS_RISE2        0x0060   /*  RW  16 bits */
#define TRIG_GNSS_ANA_CTRL_IFCAL_A          0x0068   /*  RW  22 bits */
#define TRIG_GNSS_ANA_CTRL_IFCAL_B          0x006C   /*  RW  25 bits */
#define TRIG_GNSS_ANA_CTRL_IFCAL_C          0x0070   /*  RW  32 bits */
#define TRIG_GNSS_ANA_CTRL_RFCAL            0x0078   /*  RW  32 bits */
#define TRIG_GNSS_ANA_CTRL_ALL_DCOC         0x0088   /*  RW  10 bits */
#define TRIG_GNSS_ANA_CTRL_GPS_DCOC         0x008C   /*  RW  32 bits */
#define TRIG_GNSS_ANA_CTRL_GLO_DCOC         0x0094   /*  RW  32 bits */
#define TRIG_GNSS_ANA_CTRL_IP2CAL_A         0x009C   /*  RW  20 bits */
#define TRIG_GNSS_ANA_CTRL_IP2CAL_B         0x00A0   /*  RW  29 bits */

/*Addresses of read only registers*/
#define TRIG_GNSS_ANA_STATUS_RFA_PWR_DET    0x001C   /*  R    5 bits */
#define TRIG_GNSS_ANA_STATUS_CHIP_ID        0x0038   /*  R    8 bits */
#define TRIG_GNSS_ANA_STATUS_CSM            0x0044   /*  R   15 bits */
#define TRIG_GNSS_ANA_STATUS_BOXO           0x004C   /*  R    6 bits */
#define TRIG_GNSS_ANA_STATUS_VCO            0x0054   /*  R   12 bits */
#define TRIG_GNSS_ANA_STATUS_DDS            0x0064   /*  R   30 bits */
#define TRIG_GNSS_ANA_STATUS_IFCAL          0x0074   /*  R   18 bits */
#define TRIG_GNSS_ANA_STATUS_RFCAL          0x007C   /*  R   28 bits */
#define TRIG_GNSS_ANA_STATUS_GPS_DCOC       0x0090   /*  R   24 bits */
#define TRIG_GNSS_ANA_STATUS_GLO_DCOC       0x0098   /*  R   24 bits */
#define TRIG_GNSS_ANA_STATUS_IP2CAL_A       0x00A4   /*  R   24 bits */
#define TRIG_GNSS_ANA_STATUS_IP2CAL_B       0x00A8   /*  R   22 bits */

#define CWI_SCAN_EN_ONLY  0x00000004
#define CWI_MON_EN_ONLY   0x00000008

#define ISP_BE_ON_OFF_CTL_NONE_VALUE 0x00000000
#define ISP_BE_ON_OFF_CTL_SCAN_MONI_VALUE 0x00000030
#define ISP_BE_ON_OFF_CTL_FILTER_VALUE 0x00000070
#define ISP_BE_ON_OFF_CTL_TONCAN0_VALUE 0x00000031
#define ISP_BE_ON_OFF_CTL_TONCAN0_CASCADE_VALUE 0x00000035
#define ISP_BE_ON_OFF_CTL_TRI_TONCAN_VALUE 0x01100031
#define ISP_BE_ON_OFF_CTL_OFFT_VALUE 0x000000F0
#define ISP_P2_CONTROL_DEFAULT_VALUE 0x051210FF
#define ISP_P2_CONTROL_REMOVER_VALUE 0x051010FF
#define ISP_BE_ON_OFF_CTL_INIT_VALUE 0x00000030
#define ISP_P2_CONTROL_INIT_VALUE_ID 0x001210ff
#define ISP_P2_CONTROL_INIT_VALUE_2M 0x001010ff
#define ISP_P2_CONTROL_DEFAULT_VALUE_141_US 0x011210FF
#define ISP_P2_CONTROL_DEFAULT_VALUE_93_US 0x051210FF
#define ISP_P2_CONTROL_COMPASS_VALUE_141_US 0x02124003

#if 0
	#define CWI_GPS_SCAN
	#define CWI_GPS_MONI
	#define CWI_GLO_SCAN
	#define CWI_GLO_MONI
	/*need P2 ctrl register's remover value */
	#define CWI_USE_OFFT
	#define TONE_CANCELLER
	#define TONE_CASCADE
	#define ACTIVE_BLANKING
	#define TONE_PERFORM_TEST
	#define MANUAL_GAIN_CONTROL
#endif

#define P2_GLO_NCO_G01    0x2762ADE6
#define P2_GLO_NCO_G02    0xCF683A1A
#define P2_GLO_NCO_G03    0xac373efb
#define P2_GLO_NCO_G04    0xBDCFBC8A
#define P2_GLO_NCO_G05    0x2762ADE6
#define P2_GLO_NCO_G06    0xCF683A1A
#define P2_GLO_NCO_G07    0x6DC4A423
#define P2_GLO_NCO_G08    0x7F5D21B3
#define P2_GLO_NCO_G09    0xF2993539
#define P2_GLO_NCO_G10    0x9a9ec16c
#define P2_GLO_NCO_G11    0x15CA3056
#define P2_GLO_NCO_G12    0x0431B2C7
#define P2_GLO_NCO_G13    0xF2993539
#define P2_GLO_NCO_G14    0x9a9ec16c
#define P2_GLO_NCO_G15    0x15CA3056
#define P2_GLO_NCO_G16    0x0431B2C7
#define P2_GLO_NCO_G17    0x5C2C2694
#define P2_GLO_NCO_G18    0xE100B7A9
#define P2_GLO_NCO_G19    0x4A93A904
#define P2_GLO_NCO_G20    0x38FB2B75
#define P2_GLO_NCO_G21    0x5C2C2694
#define P2_GLO_NCO_G22    0xE100B7A9
#define P2_GLO_NCO_G23    0x4A93A904
#define P2_GLO_NCO_G24    0x38FB2B75

/*TriG/SDIO drive defines*/
#define TRIG_MAJOR 199
#define TRIG_SDIO_FIFO_DATA	0x8000
#define SDIO_VENDOR_ID_CSR	0x032a
#define SDIO_DEVICE_ID_CSR_TRIG	0x1000

#define SKIP_PACKETS_NUM	4
#define TRIG_SDIO_BLK_SIZE	512
#define TRIG_LOOPDMA_BLK_SIZE	(512 * (1 << LOOPDMA_BUF_SIZE_SHIFT))
#define MAX_GLONASS_CHNUM	7

/*TriG IOCTL numbers*/
#define TRIG_IOC_MAGIC  'T'
#define IOCTL_PBKDRV_CASE_START _IOR(TRIG_IOC_MAGIC,  0, int)
#define IOCTL_PBKDRV_CASE_STOP  _IOR(TRIG_IOC_MAGIC,  1, int)
#define IOCTL_PBKDRV_GET_STATUS _IOR(TRIG_IOC_MAGIC,  2, int)
#define IOCTL_GET_VERSION       _IOR(TRIG_IOC_MAGIC,  3, int)
#define IOCTL_DATA_WRITE	_IOR(TRIG_IOC_MAGIC,  4, int)
#define IOCTL_TRIG_INIT	_IOR(TRIG_IOC_MAGIC,  5, int)
#define IOCTL_TRIG_DEINIT	_IOR(TRIG_IOC_MAGIC,  6, int)
#define IOCTL_TRIG_CONFIG	_IOR(TRIG_IOC_MAGIC,  7, int)
#define IOCTL_TRIG_SDIO_INT_ENABLE	_IOR(TRIG_IOC_MAGIC,  8, int)
#define IOCTL_TRIG_SDIO_INT_DISABLE	_IOR(TRIG_IOC_MAGIC,  9, int)
#define IOCTL_TRIG_GET_PACKET_PARAM	_IOR(TRIG_IOC_MAGIC,  10, int)
#define IOCTL_TRIG_ISPREG_READ	_IOR(TRIG_IOC_MAGIC,  11, int)
#define IOCTL_TRIG_ISPREG_WRITE  _IOR(TRIG_IOC_MAGIC,  12, int)
#define IOCTL_TRIG_RESET_PACKET_PROC  _IOR(TRIG_IOC_MAGIC,  13, int)
#define IOCTL_TRIG_STOP_PACKET_PROC  _IOR(TRIG_IOC_MAGIC,  14, int)
#define IOCTL_TRIG_SET_GLOBB_THREADID   _IOR(TRIG_IOC_MAGIC,  15, int)
#define IOCTL_TRIG_WAIT_FOR_CARD_INT    _IOR(TRIG_IOC_MAGIC, 16, int)

#define IOCTL_TRIG_GET_PBB_MEM	_IOR(TRIG_IOC_MAGIC,  30, int)
#define IOCTL_TRIG_RELEASE_PBB_MEM	_IOR(TRIG_IOC_MAGIC,  31, int)
#define IOCTL_TRIG_GET_CUR_LOOPDMA_ADDR	_IOR(TRIG_IOC_MAGIC,  32, int)
#define IOCTL_TRIG_RELEASE_CUR_LOOPDMA_ADDR  _IOR(TRIG_IOC_MAGIC,  33, int)
#define IOCTL_TRIG_GET_LOOPDMA_VIR_ADDR  _IOR(TRIG_IOC_MAGIC,  34, int)

/*TriG driver extern fun declare*/
extern unsigned int sirfsoc_rtc_iobrg_readl(u32 addr);
extern unsigned int buffer_crc_err;
extern unsigned int sdio_dma_int_complete(void);
extern int sdio_dma_int_handler(void);

/*TriG driver related structures declare*/
struct TRIG_PARA_BUF {
	unsigned int pbb_buf_vir_addr;
	unsigned int pbb_buf_phy_addr;
	unsigned int mem_size;
	unsigned int dma0_buf_kernerl_vir_addr;
	unsigned int dma1_buf_kernerl_vir_addr;
	unsigned int dma0_buf_kernerl_phy_addr;
	unsigned int dma1_buf_kernerl_phy_addr;
	unsigned int need_release_dma_buf;
};

struct TRIG_CONFIG_PARAM {
	/*0--trig, 1--file*/
	int running_mode;
	/*0--93, 1--141;*/
	int trig_mode;
	int trig_valid_ch_num;
	int trig_svid[MAX_GLONASS_CHNUM];
};

struct TRIG_PARAMETER {
	int m_glo_offset_in_dword;
	int m_each_glo_packet_byte;
	int m_each_wraddr_increae_in_sample;
	int m_packet_time_stamp;
	int m_glo_ch_num;
	int m_fifo_num;
	/*int m_wrAddrPosition;*/
	int next_wr_addr[15];
};

struct SDIO_GPS_MSG {
	unsigned int rtc_tick;
	unsigned int buf_adrs;
	unsigned int buf_len;
	bool is_any_lost;
};

struct trig_sdio {
	struct sdio_func *func;
	struct sdhci_host *host;
	void *loopdma_va_buf[2];
	struct cdev cdev;
	struct device dev;
};

struct trig_gpios {
	int clk_out;
	int shutdown;
	int lan_en;
};

struct trig_dev {
	struct file *filp_init;
	struct sdhci_sirf_priv *ss_sirfsoc;
	struct trig_sdio *ss_trig_sdio;
	struct task_struct *trigintthread;
	struct trig_gpios sg_trig_gpios;
	u32 gps_rtc_base;
	void __iomem *pbb_base_addr;
	u32 pbb_phys_addr;

	unsigned int valid_chan_num;
	unsigned int thread_exit;
	unsigned int process_packet;
	unsigned int dma_to_user_counter;

	struct SDIO_GPS_MSG *cur_gps_msg;
	struct SDIO_GPS_MSG *tmp_gps_msg;
	struct SDIO_GPS_MSG msg_buf[2];
	struct TRIG_PARA_BUF *trig_param_buf;
	struct TRIG_CONFIG_PARAM *config_msg;
	struct TRIG_PARAMETER *trig_param;
	struct TRIG_CONFIG_PARAM *config_msg_user;
};

#endif
