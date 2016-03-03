#include "../../dsp.h"

/* Counting set bits by Brain Kernighan's way.
 * https://graphics.stanford.edu/~seander/bithacks.html
 */
static int popcount(int v)
{
	int c;

	for (c = 0; v; c++)
		v &= v - 1;
	return c;
}

static int link_init(struct kasobj *obj)
{
	int i;
	const int types = kasobj_type_hw | kasobj_type_fe | kasobj_type_op;
	struct kasobj_link *link = kasobj_to_link(obj);
	const struct kasdb_link *db = link->db;

	/* Validate channels and pin masks. It should have been checked by
	 * parser. Just to make sure as this error will be very hard to find.
	 */
	if ((db->channels != popcount(db->source_pins_mask)) ||
		db->channels != popcount(db->sink_pins_mask)) {
		pr_err("KASLINK: unmatched channel count in link '%s'!\n",
				link->obj.name);
		return -EINVAL;
	}

	for (i = 0; i < db->channels; i++)
		link->conn_id[i] = KCM_INVALID_EP_ID;

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
	struct kasobj_link *link = kasobj_to_link(obj);
	const struct kasdb_link *db = link->db;
	struct kasobj *source = link->source, *sink = link->sink;
	int source_pins_mask = db->source_pins_mask;
	int sink_pins_mask = db->sink_pins_mask;
	int pin = 0, source_pin = 0, sink_pin = 0;

	if (obj->life_cnt++) {
		kcm_debug("LK '%s' refcnt++: %d\n", obj->name, obj->life_cnt);
		return 0;
	}

	/* Request source/sink pins and connect them */
	while (source_pins_mask && sink_pins_mask) {
		u16 source_ep, sink_ep;

		while (!(source_pins_mask & 0x1)) {
			source_pins_mask >>= 1;
			source_pin++;
		}
		while (!(sink_pins_mask & 0x1)) {
			sink_pins_mask >>= 1;
			sink_pin++;
		}

		source_ep = source->ops->get_ep(source, source_pin, 0);
		sink_ep = sink->ops->get_ep(sink, sink_pin, 1);
		if (source_ep == KCM_INVALID_EP_ID ||
				sink_ep == KCM_INVALID_EP_ID) {
			pr_err("KASLINK: Pin confliction! ");
			pr_err("Forget adding exclusive chains?\n");
			return -EINVAL;
		}

		kalimba_connect_endpoints(source_ep, sink_ep,
				&link->conn_id[pin], __kcm_resp);

		source_pins_mask >>= 1;
		sink_pins_mask >>= 1;
		source_pin++;
		sink_pin++;
		pin++;
	}

	kcm_debug("LK '%s' connected: '%s'-->'%s', 0x%X-->0x%X\n",
			obj->name, source->name, sink->name,
			db->source_pins_mask, db->sink_pins_mask);
	return 0;
}

static int link_put(struct kasobj *obj)
{
	int i;
	struct kasobj_link *link = kasobj_to_link(obj);
	const struct kasdb_link *db = link->db;
	struct kasobj *source = link->source, *sink = link->sink;
	int source_pins_mask = db->source_pins_mask;
	int sink_pins_mask = db->sink_pins_mask;
	int source_pin = 0, sink_pin = 0;

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
	while (source_pins_mask && sink_pins_mask) {
		while (!(source_pins_mask & 0x1)) {
			source_pins_mask >>= 1;
			source_pin++;
		}
		while (!(sink_pins_mask & 0x1)) {
			sink_pins_mask >>= 1;
			sink_pin++;
		}

		if (source->ops->put_ep)
			source->ops->put_ep(source, source_pin, 0);
		if (sink->ops->put_ep)
			sink->ops->put_ep(sink, sink_pin, 1);

		source_pins_mask >>= 1;
		sink_pins_mask >>= 1;
		source_pin++;
		sink_pin++;
	}

	kcm_debug("LK '%s' disconnected\n", obj->name);
	return 0;
}

static int link_start(struct kasobj *obj)
{
	struct kasobj_link *link = kasobj_to_link(obj);
	const struct kasdb_link *db = link->db;
	struct kasobj *source = link->source, *sink = link->sink;

	BUG_ON(!obj->life_cnt);
	if (obj->start_cnt++)
		return 0;

	if (source->ops->start_ep)
		source->ops->start_ep(source, db->source_pins_mask, 0);
	if (sink->ops->start_ep)
		sink->ops->start_ep(sink, db->sink_pins_mask, 1);
	return 0;
}

static int link_stop(struct kasobj *obj)
{
	struct kasobj_link *link = kasobj_to_link(obj);
	const struct kasdb_link *db = link->db;
	struct kasobj *source = link->source, *sink = link->sink;

	BUG_ON(!obj->life_cnt);
	if (obj->start_cnt && --obj->start_cnt == 0) {
		if (source->ops->stop_ep)
			source->ops->stop_ep(source, db->source_pins_mask, 0);
		if (sink->ops->stop_ep)
			sink->ops->stop_ep(sink, db->sink_pins_mask, 1);
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
