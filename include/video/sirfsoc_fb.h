/*
 * linux/include/video/sirfsoc_fb.h
 *
 * Copyright (c) 2013-2016, The Linux Foundation. All rights reserved.
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

#ifndef __SIRFSOC_FB__H
#define __SIRFSOC_FB__H

/* sirfsocfb specific ioctls*/
#define SIRFSOCFB_SET_GAMMA	_IOW('S', 0x0, __u8[256 * 3])
#define SIRFSOCFB_GET_GAMMA	_IOR('S', 0x0, __u8[256 * 3])
#define SIRFSOCFB_SET_TOPLAYER _IOW('S', 0x1, __u8)
#define SIRFSOCFB_GET_TOPLAYER _IOR('S', 0x1, __u8)

#endif
