/*
 * linux/drivers/video/fbdev/sirfsoc/vdss/vdss.h
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc
 * group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __VDSS_H
#define __VDSS_H

#ifdef pr_fmt
#undef pr_fmt
#endif

#ifdef VDSS_SUBSYS_NAME
#define pr_fmt(fmt) VDSS_SUBSYS_NAME ": " fmt
#else
#define pr_fmt(fmt) fmt
#endif

#define VDSSDBG(format, ...) pr_debug(format)

#ifdef VDSS_SUBSYS_NAME
#define VDSSERR(format, ...) pr_err(format)
#else
#define VDSSERR(format, ...) pr_err(format)
#endif

#ifdef VDSS_SUBSYS_NAME
#define VDSSINFO(format, ...) pr_info(format)
#else
#define VDSSINFO(format, ...) pr_info(format)
#endif

#ifdef VDSS_SUBSYS_NAME
#define VDSSWARN(format, ...) pr_warn(format)
#else
#define VDSSWARN(format, ...) pr_warn(format)
#endif

#endif
