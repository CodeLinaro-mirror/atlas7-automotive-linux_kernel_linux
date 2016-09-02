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

#include "../kasop.h"
#include "../../dsp.h"

static const struct kasop_impl *op_find_cap(int cap_id)
{
	const struct kasop_impl *impl = kcm_find_cap(cap_id);

	if (!impl) {
		pr_err("KASOP: unsupported capability %d!\n", cap_id);
		return ERR_PTR(-EINVAL);
	}
	return impl;
}

static int op_init(struct kasobj *obj)
{
	struct kasobj_op *op = kasobj_to_op(obj);
	const struct kasop_impl *impl = op_find_cap(op->db->cap_id);

	if (op->impl) {
		pr_err("KASOP(%s): double initialization?\n", obj->name);
		return -EINVAL;
	}

	op->op_id = KCM_INVALID_EP_ID;
	if (IS_ERR(impl))
		return PTR_ERR(impl);

	op->impl = impl;
	if (impl->init)
		return impl->init(op);
	return 0;
}

static int op_get(struct kasobj *obj, const struct kasobj_param *param)
{
	int ret = 0;
	struct kasobj_op *op = kasobj_to_op(obj);

	if (obj->life_cnt++ == 0) {
		op->cap_id = op->db->cap_id;
		if (op->impl->prepare)
			ret = op->impl->prepare(op, param);
		kalimba_create_operator(op->cap_id, &op->op_id, __kcm_resp);
		if (op->impl->create)
			ret = op->impl->create(op, param);
		kcm_debug("OP '%s' created, id = 0x%X\n", obj->name, op->op_id);
	} else if (op->impl->reconfig) {
		/* Some OP(resampler) depends on stream property and requires
		 * re-configuration even it's already created.
		 */
		ret = op->impl->reconfig(op, param);
		kcm_debug("OP '%s' refcnt++: %d\n", obj->name, obj->life_cnt);
	}
	return ret;
}

static int op_put(struct kasobj *obj)
{
	struct kasobj_op *op = kasobj_to_op(obj);

	BUG_ON(!obj->life_cnt);
	if (--obj->life_cnt == 0) {
		BUG_ON(obj->start_cnt);
		kalimba_destroy_operator(&op->op_id, 1, __kcm_resp);
		op->op_id = KCM_INVALID_EP_ID;
		op->used_sink_pins = op->used_source_pins = 0;
		op->active_sink_pins = op->active_source_pins = 0;
		kcm_debug("OP '%s' destroyed\n", obj->name);
	} else {
		kcm_debug("OP '%s' refcnt--: %d\n", obj->name, obj->life_cnt);
	}
	return 0;
}

static u16 op_get_ep(struct kasobj *obj, unsigned pin, int is_sink)
{
	u16 ep_id;
	struct kasobj_op *op = kasobj_to_op(obj);

	BUG_ON(op->op_id == KCM_INVALID_EP_ID);
	BUG_ON(pin >= 32);

	if (is_sink) {
		if (op->used_sink_pins & BIT(pin)) {
			pr_err("KASOP(%s): sink pin %d occupied!\n",
					obj->name, pin);
			return KCM_INVALID_EP_ID;
		}
		op->used_sink_pins |= BIT(pin);
		ep_id = op->op_id + pin + 0xA000;
	} else {
		if (op->used_source_pins & BIT(pin)) {
			pr_err("KASOP(%s): source pin %d occupied!\n",
					obj->name, pin);
			return KCM_INVALID_EP_ID;
		}
		op->used_source_pins |= BIT(pin);
		ep_id = op->op_id + pin + 0x2000;
	}

	return ep_id;
}

static void op_put_ep(struct kasobj *obj, unsigned pin, int is_sink)
{
	struct kasobj_op *op = kasobj_to_op(obj);

	BUG_ON(op->op_id == KCM_INVALID_EP_ID);
	BUG_ON(pin >= 32);

	if (is_sink)
		op->used_sink_pins &= ~BIT(pin);
	else
		op->used_source_pins &= ~BIT(pin);
}

static void op_start_ep(struct kasobj *obj, unsigned pin_mask, int is_sink)
{
	struct kasobj_op *op = kasobj_to_op(obj);

	BUG_ON(op->op_id == KCM_INVALID_EP_ID);
	if (is_sink) {
		op->active_sink_pins |= pin_mask;
		if ((op->used_sink_pins & pin_mask) != pin_mask)
			pr_err("KASOP(%s): sink pins error!\n", obj->name);
	} else {
		op->active_source_pins |= pin_mask;
		if ((op->used_source_pins & pin_mask) != pin_mask)
			pr_err("KASOP(%s): source pins error!\n", obj->name);
	}

	if (op->impl->trigger)
		op->impl->trigger(op,
			KASOP_MAKE_EVENT(kasop_event_start_ep, is_sink));
}

static void op_stop_ep(struct kasobj *obj, unsigned pin_mask, int is_sink)
{
	struct kasobj_op *op = kasobj_to_op(obj);

	BUG_ON(op->op_id == KCM_INVALID_EP_ID);
	if (is_sink)
		op->active_sink_pins &= ~pin_mask;
	else
		op->active_source_pins &= ~pin_mask;

	if (op->impl->trigger)
		op->impl->trigger(op,
			KASOP_MAKE_EVENT(kasop_event_stop_ep, is_sink));
}

static struct kasobj_ops op_ops = {
	.init = op_init,
	.get = op_get,
	.put = op_put,
	.get_ep = op_get_ep,
	.put_ep = op_put_ep,
	.start_ep = op_start_ep,
	.stop_ep = op_stop_ep,
};
