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

	if (IS_ERR(impl))
		return PTR_ERR(impl);

	op->impl = impl;
	if (impl->init)
		return impl->init(op);
	return 0;
}

static struct kasobj_ops op_ops = {
	.init = op_init,
};
