/*
 * Copyright (c) 2014, 2016, The Linux Foundation. All rights reserved.
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

#ifndef __SIRFSOC_V4L2_H_
#define __SIRFSOC_V4L2_H_

/* v4l2 csr extensions */
/* 12 YUV 4:2:0 w-stride 64 aligned h-stride 16 aligned */
#define V4L2_PIX_FMT_Q420 v4l2_fourcc('Q', '4', '2', '0')

/* Y component range[0,255], planes exactly like NV12 */
#define V4L2_PIX_FMT_NJ12 v4l2_fourcc('N', 'J', '1', '2')

#endif /* __SIRFSOC_V4L2_H_ */
