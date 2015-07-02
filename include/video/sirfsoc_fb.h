/*
 * linux/include/video/sirfsoc_fb.h
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc
 * group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __SIRFSOC_FB__H
#define __SIRFSOC_FB__H

/* sirfsocfb specific ioctls*/
#define SIRFSOCFB_SET_GAMMA	_IOW('S', 0x0, __u8[256 * 3])
#define SIRFSOCFB_GET_GAMMA	_IOR('S', 0x0, __u8[256 * 3])
#define SIRFSOCFB_SET_TOPLAYER _IOW('S', 0x1, __u8)
#define SIRFSOCFB_GET_TOPLAYER _IOR('S', 0x1, __u8)

#endif
