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
#include <linux/slab.h>
#include "kasobj.h"
#include "kasop.h"
#include "kcm.h"
#include "../dsp.h"

/**
 * list_for_each_entry_backward - iterate over list of given type backward
 * @pos:		the type * to use as a loop cursor.
 * @head:		the head for your list.
 * @member:		the name of the list_head within the struct.
 */
#define list_for_each_entry_backward(pos, head, member)			\
	for (pos = list_entry((head)->prev, typeof(*pos), member);	\
		&pos->member != (head);					\
		pos = list_entry(pos->member.prev, typeof(*pos), member))

#define KCM_CHAIN_LENGTH 512

struct kcm_chain {
	const struct kasdb_chain *db;
	const char *name;
	struct list_head link;		/* Used by chain_list */
	struct list_head ex_list;	/* Exclusive chains, kcm_chain_ex */
	const struct kasobj_fe *trg_fe;
	struct list_head lk_list;	/* kcm_chain_obj */
	struct list_head fe_list;	/* " */
	struct list_head hw_list;	/* " */
	struct list_head op_list;	/* " */
	int prepared;			/* 0 - idle */
};

/* Object list in lk_list, fe_list, hw_list, op_list */
struct kcm_chain_obj {
	struct kasobj *obj;
	struct list_head link;
};

/* Object list in ex_list */
struct kcm_chain_ex {
	struct kcm_chain *chain;
	struct list_head link;
};

static struct list_head chain_list = LIST_HEAD_INIT(chain_list);

static struct kcm_chain *kcm_find_chain(const char *name)
{
	struct kcm_chain *chain;

	list_for_each_entry(chain, &chain_list, link) {
		if (strcasecmp(chain->name, name) == 0)
			return chain;
	}
	return NULL;
}

#if __KCM_DEBUG > 1
static void __init kcm_print_list(struct list_head *list, const char *prefix)
{
	struct kcm_chain_obj *chain_obj;

	kcm_debug("%s:\n", prefix);
	list_for_each_entry(chain_obj, list, link)
		kcm_debug("  %s\n", chain_obj->obj->name);
}

static void __init kcm_print_list_ex(struct list_head *list)
{
	struct kcm_chain_ex *chain_ex;

	kcm_debug("Exclusive Chains:\n");
	list_for_each_entry(chain_ex, list, link)
		kcm_debug("  %s\n", chain_ex->chain->name);
}

static void __init kcm_print_chains(void)
{
	struct kcm_chain *chain;

	list_for_each_entry(chain, &chain_list, link) {
		kcm_debug("-----------------------------------------------\n");
		kcm_debug("Chain: %s\n", chain->name);
		kcm_debug("Trigger: FE=%s, Channels=%d\n",
			chain->trg_fe->obj.name, chain->db->trg_channels);
		kcm_print_list(&chain->lk_list, "Link");
		kcm_print_list(&chain->fe_list, "Front End");
		kcm_print_list(&chain->op_list, "Operator");
		kcm_print_list(&chain->hw_list, "Hardware");
		kcm_print_list_ex(&chain->ex_list);
	}
	kcm_debug("-----------------------------------------------\n");
}
#else
static void __init kcm_print_chains(void)
{
}
#endif

static int __init kcm_init_chain1_obj(struct kcm_chain *chain,
		struct kasobj *obj)
{
	struct kcm_chain_obj *chain_obj;
	struct list_head *list;

	if (obj->type == kasobj_type_fe)
		list = &chain->fe_list;
	else if (obj->type == kasobj_type_hw)
		list = &chain->hw_list;
	else if (obj->type == kasobj_type_op)
		list = &chain->op_list;
	else
		return -EINVAL;

	/* Ignore if already present, most of the time the collision will be
	 * the last added one if the links are arranged in order.
	 */
	list_for_each_entry_reverse(chain_obj, list, link)
		if (chain_obj->obj == obj)
			return 0;

	chain_obj = kmalloc(sizeof(struct kcm_chain_obj), GFP_KERNEL);
	chain_obj->obj = obj;
	list_add_tail(&chain_obj->link, list);
	return 0;
}

/* Setup FE/HW/OP/Link list */
static int __init kcm_init_chain1(struct kcm_chain *chain)
{
	char lkbuf[KCM_CHAIN_LENGTH], *lkbuf_ptr = lkbuf, *lk_name;
	struct kcm_chain_obj *chain_obj;
	struct kasobj *obj;

	/* Find trigger FE object */
	obj = kasobj_find_obj(chain->db->trg_fe_name.s, kasobj_type_fe);
	if (!obj) {
		pr_err("KASCHAIN: cannot find FE '%s'!\n",
				chain->db->trg_fe_name.s);
		return -EINVAL;
	}
	chain->trg_fe = kasobj_to_fe(obj);

	if (!chain->db->links.s)
		return 0;	/* Dummy chain only to trigger codec */

	if (snprintf(lkbuf, KCM_CHAIN_LENGTH, "%s",
		chain->db->links.s) >= KCM_CHAIN_LENGTH) {
		pr_err("KASCHAIN(%s): links too long!\n", chain->name);
		return -EINVAL;
	}

	/* Setup Link object lists */
	while ((lk_name = strsep(&lkbuf_ptr, ":;"))) {
		obj = kasobj_find_obj(lk_name, kasobj_type_lk);
		if (!obj) {
			pr_err("KASCHAIN: cannot find link '%s'!\n", lk_name);
			return -EINVAL;
		}

		chain_obj = kmalloc(sizeof(struct kcm_chain_obj), GFP_KERNEL);
		chain_obj->obj = obj;
		list_add_tail(&chain_obj->link, &chain->lk_list);
	}

	/* Setup FE/HW/OP object list */
	list_for_each_entry(chain_obj, &chain->lk_list, link) {
		struct kasobj_link *link = kasobj_to_link(chain_obj->obj);

		if (kcm_init_chain1_obj(chain, link->source) ||
				kcm_init_chain1_obj(chain, link->sink))
			return -EINVAL;
	}
	return 0;
}

/* Setup exclusive list */
static int __init kcm_init_chain1_ex(struct kcm_chain *chain)
{
	char exbuf[KCM_CHAIN_LENGTH], *exbuf_ptr = exbuf, *ex_name;
	struct kcm_chain_ex *chain_ex;

	if (!chain->db->mutexs.s)
		return 0;	/* No exclusive list */

	if (snprintf(exbuf, KCM_CHAIN_LENGTH, "%s",
		chain->db->mutexs.s) >= KCM_CHAIN_LENGTH) {
		pr_err("KASCHAIN(%s): mutexs too long!\n", chain->name);
		return -EINVAL;
	}

	while ((ex_name = strsep(&exbuf_ptr, ":;"))) {
		struct kcm_chain *exchain = kcm_find_chain(ex_name);

		if (!exchain) {
			pr_err("KASCHAIN: cannot find ex-chain '%s'!\n",
					ex_name);
			return -EINVAL;
		}

		chain_ex = kmalloc(sizeof(struct kcm_chain_ex), GFP_KERNEL);
		chain_ex->chain = exchain;
		list_add_tail(&chain_ex->link, &chain->ex_list);
	}
	return 0;
}

int __init kcm_init_chain(void)
{
	int ret;
	struct kcm_chain *chain;

	/* Setup FE/HW/OP/Link list for each chain */
	list_for_each_entry(chain, &chain_list, link) {
		ret = kcm_init_chain1(chain);
		if (ret)
			return ret;
	}

	/* Setup exclusive list for each chain */
	list_for_each_entry(chain, &chain_list, link) {
		ret = kcm_init_chain1_ex(chain);
		if (ret)
			return ret;
	}

	kcm_print_chains();
	return 0;
}

void __init kcm_add_chain(const void *db)
{
	struct kcm_chain *chain = kzalloc(sizeof(struct kcm_chain), GFP_KERNEL);

	chain->db = db;
	chain->name = chain->db->name.s;
	chain->prepared = 0;
	INIT_LIST_HEAD(&chain->ex_list);
	INIT_LIST_HEAD(&chain->lk_list);
	INIT_LIST_HEAD(&chain->fe_list);
	INIT_LIST_HEAD(&chain->hw_list);
	INIT_LIST_HEAD(&chain->op_list);

	list_add_tail(&chain->link, &chain_list);
}

struct kcm_chain *kcm_prepare_chain(const struct kasobj_fe *fe,
		int playback, int channels)
{
	int okay = 0, mic_num;
	struct kcm_chain *chain;
	struct kcm_chain_ex *chain_ex;

	if (kcm_enable_2mic_cvc)
		mic_num = 2;
	else
		mic_num = 1;

	/* Find chain by FE */
	list_for_each_entry(chain, &chain_list, link) {
		if (chain->trg_fe == fe &&
			(chain->db->trg_channels == channels ||
			 chain->db->trg_channels == 0) &&
			(chain->db->cvc_mic == mic_num ||
			 chain->db->cvc_mic == 0)) {
			okay = 1;
			break;
		}
	}
	if (!okay) {
		pr_err("KASCHAIN: cannot find chain(FE=%s, %d channels)!\n",
				fe->obj.name, channels);
		return ERR_PTR(-EINVAL);
	}
	if (chain->prepared) {
		pr_err("KASCHAIN: chain '%s' busy!\n", chain->name);
		return ERR_PTR(-EINVAL);
	}

	/* Check exclusive chains */
	kcm_lock();

	list_for_each_entry(chain_ex, &chain->ex_list, link) {
		if (chain_ex->chain->prepared) {
			okay = 0;
			pr_err("KASCHAIN: conflict with '%s'!\n",
					chain_ex->chain->name);
			break;
		}
	}
	if (okay)
		chain->prepared = 1;

	kcm_unlock();

	if (okay) {
		kcm_debug("Chain '%s' prepared\n", chain->name);
		return chain;
	} else {
		return ERR_PTR(-EBUSY);
	}
}
EXPORT_SYMBOL(kcm_prepare_chain);

void kcm_unprepare_chain(struct kcm_chain *chain)
{
	chain->prepared = 0;
}
EXPORT_SYMBOL(kcm_unprepare_chain);

int kcm_get_chain(struct kcm_chain *chain, const struct kasobj_param *param)
{
	int ret = 0;
	struct kcm_chain_obj *chain_obj;

	if (!chain->prepared) {
		pr_err("KASCHAIN: chain '%s' unready!\n", chain->name);
		return -EINVAL;
	}

	kcm_lock();

	/* Propagate get() to individual objects */
	list_for_each_entry(chain_obj, &chain->fe_list, link) {
		ret = chain_obj->obj->ops->get(chain_obj->obj, param);
		if (ret)
			goto out;
	}
	list_for_each_entry(chain_obj, &chain->hw_list, link) {
		ret = chain_obj->obj->ops->get(chain_obj->obj, param);
		if (ret)
			goto out;
	}
	list_for_each_entry(chain_obj, &chain->op_list, link) {
		ret = chain_obj->obj->ops->get(chain_obj->obj, param);
		if (ret)
			goto out;
	}
	/* Link must be last, it requires FE/HW/OP endpoints ready */
	list_for_each_entry(chain_obj, &chain->lk_list, link) {
		ret = chain_obj->obj->ops->get(chain_obj->obj, param);
		if (ret)
			goto out;
	}

out:
	if (ret) {
		/* TODO: In theory, we can revert all operations before the
		 * error point to recover a consistent internal status.
		 */
		pr_err("KCMCHAIN: inconsistent internal status!\n");
		pr_err("KCMCHAIN: cannot recover cleanly, reboot required!\n");
	} else {
		kcm_debug("Chain '%s' instantiated\n", chain->name);
	}

	kcm_unlock();
	return ret;
}
EXPORT_SYMBOL(kcm_get_chain);

int kcm_put_chain(struct kcm_chain *chain)
{
	struct kcm_chain_obj *chain_obj;

	kcm_lock();

	/* Link must be first, to disconnect endpoints */
	list_for_each_entry_backward(chain_obj, &chain->lk_list, link)
		chain_obj->obj->ops->put(chain_obj->obj);
	list_for_each_entry_backward(chain_obj, &chain->op_list, link)
		chain_obj->obj->ops->put(chain_obj->obj);
	list_for_each_entry_backward(chain_obj, &chain->hw_list, link)
		chain_obj->obj->ops->put(chain_obj->obj);
	list_for_each_entry_backward(chain_obj, &chain->fe_list, link)
		chain_obj->obj->ops->put(chain_obj->obj);

	chain->prepared = 0;
	kcm_debug("Chain '%s' torn down\n", chain->name);

	kcm_unlock();
	return 0;
}
EXPORT_SYMBOL(kcm_put_chain);

/* Start/stop operators in batch mode
 * XXX: Following code breaks encapsulation
 */
#define KCM_CHAIN_MAX_OPS	32	/* Maximum operators in a chain */

int __kcm_start_chain_op(struct kcm_chain *chain)
{
	struct kcm_chain_obj *chain_obj;
	int op_cnt = 0;
	u16 op_ids[KCM_CHAIN_MAX_OPS];

	list_for_each_entry(chain_obj, &chain->op_list, link) {
		struct kasobj_op *op = kasobj_to_op(chain_obj->obj);

		BUG_ON(!op->obj.life_cnt);
		if (op->obj.start_cnt++ == 0) {
			BUG_ON(op_cnt >= KCM_CHAIN_MAX_OPS ||
					op->op_id == KCM_INVALID_EP_ID);
			op_ids[op_cnt++] = op->op_id;
			kcm_debug("OP '%s' started\n", op->obj.name);
		}
	}
	if (op_cnt)
		kalimba_start_operator(op_ids, op_cnt, __kcm_resp);
	return 0;
}
EXPORT_SYMBOL(__kcm_start_chain_op);

int __kcm_stop_chain_op(struct kcm_chain *chain)
{
	struct kcm_chain_obj *chain_obj;
	int op_cnt = 0;
	u16 op_ids[KCM_CHAIN_MAX_OPS];

	list_for_each_entry_backward(chain_obj, &chain->op_list, link) {
		struct kasobj_op *op = kasobj_to_op(chain_obj->obj);

		BUG_ON(!op->obj.life_cnt);
		if (op->obj.start_cnt && --op->obj.start_cnt == 0) {
			BUG_ON(op_cnt >= KCM_CHAIN_MAX_OPS ||
					op->op_id == KCM_INVALID_EP_ID);
			op_ids[op_cnt++] = op->op_id;
			kcm_debug("OP '%s' stopped\n", op->obj.name);
		}
	}
	if (op_cnt)
		kalimba_stop_operator(op_ids, op_cnt, __kcm_resp);
	return 0;
}
EXPORT_SYMBOL(__kcm_stop_chain_op);

int __kcm_start_chain_hw(struct kcm_chain *chain)
{
	struct kcm_chain_obj *chain_obj;

	list_for_each_entry(chain_obj, &chain->hw_list, link)
		chain_obj->obj->ops->start(chain_obj->obj);
	return 0;
}
EXPORT_SYMBOL(__kcm_start_chain_hw);

int __kcm_stop_chain_hw(struct kcm_chain *chain)
{
	struct kcm_chain_obj *chain_obj;

	list_for_each_entry_backward(chain_obj, &chain->hw_list, link)
		chain_obj->obj->ops->stop(chain_obj->obj);
	return 0;
}
EXPORT_SYMBOL(__kcm_stop_chain_hw);

int __kcm_start_chain_link(struct kcm_chain *chain)
{
	struct kcm_chain_obj *chain_obj;

	list_for_each_entry(chain_obj, &chain->lk_list, link)
		chain_obj->obj->ops->start(chain_obj->obj);
	return 0;
}
EXPORT_SYMBOL(__kcm_start_chain_link);

int __kcm_stop_chain_link(struct kcm_chain *chain)
{
	struct kcm_chain_obj *chain_obj;

	list_for_each_entry_backward(chain_obj, &chain->lk_list, link)
		chain_obj->obj->ops->stop(chain_obj->obj);
	return 0;
}
EXPORT_SYMBOL(__kcm_stop_chain_link);

int kcm_start_chain(struct kcm_chain *chain)
{
	kcm_lock();
	__kcm_start_chain_link(chain);
	__kcm_start_chain_hw(chain);
	__kcm_start_chain_op(chain);
	kcm_unlock();
	return 0;
}
EXPORT_SYMBOL(kcm_start_chain);

int kcm_stop_chain(struct kcm_chain *chain)
{
	kcm_lock();
	__kcm_stop_chain_link(chain);
	__kcm_stop_chain_hw(chain);
	__kcm_stop_chain_op(chain);
	kcm_unlock();
	return 0;
}
EXPORT_SYMBOL(kcm_stop_chain);
