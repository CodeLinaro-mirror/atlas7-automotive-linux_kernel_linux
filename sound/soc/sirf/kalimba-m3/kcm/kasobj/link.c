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

#include "../../dsp.h"

static int link_init(struct kasobj *obj)
{
	int i;
	const int types = kasobj_type_hw | kasobj_type_fe | kasobj_type_op;
	struct kasobj_link *link = kasobj_to_link(obj);
	const struct kasdb_link *db = link->db;

	for (i = 0; i < db->channels; i++) {
		link->conn_id[i] = KCM_INVALID_EP_ID;
		if (db->source_pins[i] < 1 || db->source_pins[i] > 32 ||
				db->sink_pins[i] < 1 || db->sink_pins[i] > 32) {
			pr_err("KASLINK: invalid pin definition!\n");
			return -EINVAL;
		}
	}

	link->source = kasobj_find_obj(db->source_name.s, types);
	link->sink = kasobj_find_obj(db->sink_name.s, types);
	if (!link->source || !link->sink) {
		pr_err("KASLINK: cannot find link source/sink!\n");
		return -EINVAL;
	}
	return 0;
}

static int link_get(struct kasobj *obj, const struct kasobj_param *param)
{
	int i;
	struct kasobj_link *link = kasobj_to_link(obj);
	const struct kasdb_link *db = link->db;
	struct kasobj *source = link->source, *sink = link->sink;

	if (obj->life_cnt++) {
		kcm_debug("LK '%s' refcnt++: %d\n", obj->name, obj->life_cnt);
		return 0;
	}

	/* Request source/sink pins and connect them */
	for (i = 0; i < db->channels; i++) {
		u16 source_ep = source->ops->get_ep(source,
				db->source_pins[i] - 1, 0);
		u16 sink_ep = sink->ops->get_ep(sink, db->sink_pins[i] - 1, 1);

		if (source_ep == KCM_INVALID_EP_ID ||
				sink_ep == KCM_INVALID_EP_ID) {
			pr_err("KASLINK: Pin confliction! ");
			pr_err("Forget adding exclusive chains?\n");
			return -EINVAL;
		}

		kalimba_connect_endpoints(source_ep, sink_ep,
				&link->conn_id[i], __kcm_resp);
	}

	kcm_debug("LK '%s' connected: '%s'-->'%s', %d channels\n",
			obj->name, source->name, sink->name, db->channels);
	return 0;
}

static int link_put(struct kasobj *obj)
{
	int i;
	struct kasobj_link *link = kasobj_to_link(obj);
	const struct kasdb_link *db = link->db;
	struct kasobj *source = link->source, *sink = link->sink;

	BUG_ON(!obj->life_cnt);
	if (--obj->life_cnt) {
		kcm_debug("OP '%s' refcnt--: %d\n", obj->name, obj->life_cnt);
		return 0;
	}
	BUG_ON(obj->start_cnt);

	kalimba_disconnect_endpoints(db->channels, link->conn_id, __kcm_resp);
	for (i = 0; i < db->channels; i++)
		link->conn_id[i] = KCM_INVALID_EP_ID;

	/* Free source/sink pins */
	for (i = 0; i < db->channels; i++) {
		if (source->ops->put_ep)
			source->ops->put_ep(source, db->source_pins[i] - 1, 0);
		if (sink->ops->put_ep)
			sink->ops->put_ep(sink, db->sink_pins[i] - 1, 1);
	}

	kcm_debug("LK '%s' disconnected\n", obj->name);
	return 0;
}

static int link_start(struct kasobj *obj)
{
	int i;
	struct kasobj_link *link = kasobj_to_link(obj);
	const struct kasdb_link *db = link->db;
	struct kasobj *source = link->source, *sink = link->sink;
	unsigned source_pins_mask = 0, sink_pins_mask = 0;

	BUG_ON(!obj->life_cnt);
	if (obj->start_cnt++)
		return 0;

	for (i = 0; i < db->channels; i++) {
		source_pins_mask |= BIT(db->source_pins[i] - 1);
		sink_pins_mask |= BIT(db->sink_pins[i] - 1);
	}

	if (source->ops->start_ep)
		source->ops->start_ep(source, source_pins_mask, 0);
	if (sink->ops->start_ep)
		sink->ops->start_ep(sink, sink_pins_mask, 1);
	return 0;
}

static int link_stop(struct kasobj *obj)
{
	int i;
	struct kasobj_link *link = kasobj_to_link(obj);
	const struct kasdb_link *db = link->db;
	struct kasobj *source = link->source, *sink = link->sink;
	unsigned source_pins_mask = 0, sink_pins_mask = 0;

	BUG_ON(!obj->life_cnt);

	for (i = 0; i < db->channels; i++) {
		source_pins_mask |= BIT(db->source_pins[i] - 1);
		sink_pins_mask |= BIT(db->sink_pins[i] - 1);
	}

	if (obj->start_cnt && --obj->start_cnt == 0) {
		if (source->ops->stop_ep)
			source->ops->stop_ep(source, source_pins_mask, 0);
		if (sink->ops->stop_ep)
			sink->ops->stop_ep(sink, sink_pins_mask, 1);
	}
	return 0;
}

static struct kasobj_ops link_ops = {
	.init = link_init,
	.get = link_get,
	.put = link_put,
	.start = link_start,
	.stop = link_stop,
};
