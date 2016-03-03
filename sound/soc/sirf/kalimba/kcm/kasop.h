#ifndef _KCM_KASOP_H
#define _KCM_KASOP_H

enum {
	kasop_event_start_ep,
	kasop_event_stop_ep,
	/* Cannot exceed 16 events, see following macro */
};

#define KASOP_MAKE_EVENT(event, param)	((event) | ((param) << 4))
#define KASOP_GET_EVENT(event_param)	((event_param) & 0xF)
#define KASOP_GET_PARAM(event_param)	((event_param) >> 4)

struct kasop_impl {
	int (*init)(struct kasobj_op *op);
	int (*create)(struct kasobj_op *op, const struct kasobj_param *param);
	int (*reconfig)(struct kasobj_op *op, const struct kasobj_param *param);
	int (*trigger)(struct kasobj_op *op, int event);
};

#endif
