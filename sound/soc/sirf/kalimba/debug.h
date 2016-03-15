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

#ifndef _DEBUG_H
#define _DEBUG_H

#include <linux/platform_device.h>
/*DSP IOCTL numbers*/
#define KALIMBA_IOC_MAGIC  'K'

#define IOCTL_KALIMBA_DOWNLOAD_BOOTCODE		_IO(KALIMBA_IOC_MAGIC, 0)
#define IOCTL_KALIMBA_DUMP_BOOTCODE		_IO(KALIMBA_IOC_MAGIC, 1)
#define IOCTL_KALIMBA_WRITE_PM			_IO(KALIMBA_IOC_MAGIC, 2)
#define IOCTL_KALIMBA_READ_PM			_IO(KALIMBA_IOC_MAGIC, 3)
#define IOCTL_KALIMBA_WRITE_DM			_IO(KALIMBA_IOC_MAGIC, 4)
#define IOCTL_KALIMBA_READ_DM			_IO(KALIMBA_IOC_MAGIC, 5)
#define IOCTL_KALIMBA_RUN_PM			_IO(KALIMBA_IOC_MAGIC, 6)
#define IOCTL_KALIMBA_STOP_PM			_IO(KALIMBA_IOC_MAGIC, 7)
#define IOCTL_KALIMBA_RESUME_PM			_IO(KALIMBA_IOC_MAGIC, 8)
#define IOCTL_KALIMBA_SETUP_AUDIO_UNIT		_IO(KALIMBA_IOC_MAGIC, 9)
#define IOCTL_KALIMBA_START_AUDIO_UNIT		_IO(KALIMBA_IOC_MAGIC, 10)
#define IOCTL_KALIMBA_STOP_AUDIO_UNIT		_IO(KALIMBA_IOC_MAGIC, 11)
#define IOCTL_KALIMBA_RELEASE_AUDIO_UNIT	_IO(KALIMBA_IOC_MAGIC, 12)
#define IOCTL_KALIMBA_API			_IO(KALIMBA_IOC_MAGIC, 13)
#define IOCTL_KALIMBA_WRITE_DRAM		_IO(KALIMBA_IOC_MAGIC, 14)
#define IOCTL_KALIMBA_READ_DRAM			_IO(KALIMBA_IOC_MAGIC, 15)
#define IOCTL_KALIMBA_DRAM_ALLOC                _IO(KALIMBA_IOC_MAGIC, 16)
#define IOCTL_KALIMBA_DRAM_FREE                 _IO(KALIMBA_IOC_MAGIC, 17)
#define IOCTL_KALIMBA_DRAM_FILL                 _IO(KALIMBA_IOC_MAGIC, 18)
#define KALIMBA_IOC_MAXNR			19

#define CTRL_DEVICE_TYPE_IACC			0
#define CTRL_DEVICE_TYPE_I2S			1
#define CTRL_DEVICE_TYPE_AC97			2
#define CTRL_DEVICE_TYPE_USP			3
#define CTRL_DEVICE_TYPE_SPDIF			4
#define CTRL_DEVICE_TYPE_AUDIODATA		5

int debug_init(void);
void debug_deinit(void);
#endif /* _DEBUG_H */
