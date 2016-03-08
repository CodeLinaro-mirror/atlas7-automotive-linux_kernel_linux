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

#include <linux/module.h>
#include "kasobj.h"

/* User database layout
 * +=============+
 * | kasdb_head  |
 * +=============+
 * |  kasdb_elm  |
 * +-------------+
 * |  kasdb_fe1  |
 * +=============+
 * |  kasdb_elm  |
 * +-------------+
 * |  kasdb_fe2  |
 * +=============+
 * |   ......    |
 * +=============+
 * | string pool |
 * +=============+
 * | relocation  | --> offset of all kasdb_str
 * |    table    |
 * +=============+
*/

/* xor of all ints should be 0 */
static int __init kasdb_cksum(const void *data, size_t sz)
{
	int cksum = 0;
	const int *pd = data;

	sz >>= 2;
	while (sz--)
		cksum ^= *pd++;

	return cksum;
}

int __init kasdb_load_user(struct kasdb_head *db, size_t sz)
{
	int i;
	void *base = db, *eptr = db->data;
	struct kasdb_reloc *reloc = base + db->reloc_off;

	BUG_ON(sz & 0x3);

	if (db->magic != KASDB_MAGIC || kasdb_cksum(db, sz) != 0) {
		pr_err("KASDB: invalid database!\n");
		return -EINVAL;
	}
	if (db->version != KASDB_VERSION) {
		pr_err("KASDB: version mismatch!\n");
		return -EINVAL;
	}

	/* Relocate: change string offset to pointer */
	for (i = 0; i < reloc->cnt; i++) {
		union kasdb_str *pstr = base + reloc->offset[i];

		pstr->s += (size_t)base;
	}

	for (i = 0; i < db->elements; i++) {
		int ret;
		struct kasdb_elm *e = eptr;

		if (e->magic != KASDB_ELM_MAGIC) {
			pr_err("KASDB: corrupted database!\n");
			return -EINVAL;
		}
		ret = kasobj_add(e->data, e->type);
		if (ret)
			return ret;

		eptr += e->size;
	}

	return 0;
}
