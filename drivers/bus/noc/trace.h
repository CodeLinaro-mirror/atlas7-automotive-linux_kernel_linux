/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
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

#if !defined(_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)

#include <linux/tracepoint.h>
#include <linux/kernel.h>

#define _TRACE_H_

/* create empty functions when tracing is disabled */
#if !defined(CONFIG_ATLAS7_NOC_TRACING)
#undef TRACE_EVENT
#define TRACE_EVENT(name, proto, ...) \
static inline void trace_ ## name(proto) {}
#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(...)
#undef DEFINE_EVENT
#define DEFINE_EVENT(evt_class, name, proto, ...) \
static inline void trace_ ## name(proto) {}
#endif /* !CONFIG_ATLAS7_NOC_TRACING || __CHECKER__ */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM noc


TRACE_EVENT(noc_bw_data,
	TP_PROTO(const char *master, unsigned int bw),

	TP_ARGS(master, bw),

	TP_STRUCT__entry(
		__string(master, master)
		__field(unsigned int, bw)
	),

	TP_fast_assign(
		__assign_str(master, master);
		__entry->bw = bw;
	),

	TP_printk(
		"%s: %u",
		__get_str(master),
		__entry->bw
	)
);

#endif /* _TRACE_H_ || TRACE_HEADER_MULTI_READ*/

/* we don't want to use include/trace/events */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>
