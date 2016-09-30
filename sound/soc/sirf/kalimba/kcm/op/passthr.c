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
#include <sound/soc.h>
#include <sound/tlv.h>
#include "../kasobj.h"
#include "../kasop.h"
#include "../kcm.h"
#include "../../dsp.h"
#include "utils.h"

#define MIN_DB	(-120)
#define STEP_DB	1
#define MAXV	(-MIN_DB / STEP_DB)

static const DECLARE_TLV_DB_SCALE(vol_tlv, MIN_DB*100, STEP_DB*100, 0);

/* Context for each instance */
struct passthr_ctx {
	int gain;	/* gain = 0 ~ 120 => -120dB ~ 0dB */
	int muted;	/* 1 - muted, 0 - unmuted */
};

static void set_gain(struct kasobj_op *op)
{
	struct passthr_ctx *ctx = op->context;
	short db;

	/* IPC only if operator is instantiated */
	if (!op->obj.life_cnt)
		return;

	if (ctx->muted)
		db = -32768;	/* Minimal value */
	else
		db = (ctx->gain - MAXV) * 60;
	kalimba_operator_message(op->op_id, OPERATOR_MSG_SET_PASSTHROUGH_GAIN,
			1, &db, NULL, NULL, __kcm_resp);
}

static int vol_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, NULL);
	struct passthr_ctx *ctx = op->context;

	ucontrol->value.integer.value[0] = ctx->gain;
	return 0;
}

static int vol_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, NULL);
	struct passthr_ctx *ctx = op->context;
	int gain = ucontrol->value.integer.value[0];

	if (gain == ctx->gain)
		return 0;

	kcm_lock();
	ctx->gain = gain;
	if (!ctx->muted)
		set_gain(op);
	kcm_unlock();

	return 0;
}

static int mute_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, NULL);
	struct passthr_ctx *ctx = op->context;

	ucontrol->value.integer.value[0] = ctx->muted;
	return 0;
}

static int mute_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, NULL);
	struct passthr_ctx *ctx = op->context;
	int tomute = ucontrol->value.integer.value[0];

	if (tomute == ctx->muted)
		return 0;

	kcm_lock();
	ctx->muted = tomute;
	set_gain(op);
	kcm_unlock();

	return 0;
}

/* Create control interfaces */
static int passthr_init(struct kasobj_op *op)
{
	char names_buf[256], *names = names_buf, *name;
	struct passthr_ctx *ctx = kzalloc(sizeof(struct passthr_ctx),
		GFP_KERNEL);
	struct snd_kcontrol_new *ctrl;
	int ctrl_id = 0;

	ctx->gain = MAXV;	/* default 0dB */
	op->context = ctx;

	if (!op->db->ctrl_names.s)
		return 0;

	if (snprintf(names_buf, 256, "%s", op->db->ctrl_names.s) >= 256) {
		pr_err("KASOP(%s): control names too long!\n", op->obj.name);
		return -EINVAL;
	}

	while ((name = strsep(&names, ":;"))) {
		if (kcm_strcasestr(name, "Pregain")) {
			/* Volume control */
			ctrl = kasop_ctrl_single_ext_tlv(name, op, MAXV,
					vol_get, vol_put, vol_tlv, ctrl_id);
			kcm_register_ctrl(ctrl);
			ctrl_id++;
		} else if (kcm_strcasestr(name, "Premute")) {
			/* Mute control */
			ctrl = kasop_ctrl_single_ext_tlv(name, op, 1,
					mute_get, mute_put, NULL, ctrl_id);
			kcm_register_ctrl(ctrl);
			ctrl_id++;
		} else {
			pr_err("KASOP(%s): unknown control '%s'!\n",
				       op->obj.name, name);
			return -EINVAL;
		}
	}

	return 0;
}

/* Called after operator is created */
static int passthr_create(struct kasobj_op *op, const struct kasobj_param *parm)
{
	set_gain(op);
	return 0;
}

static struct kasop_impl passthr_impl = {
	.init = passthr_init,
	.create = passthr_create,
};

static int __init kasop_init_passthr(void)
{
	return kcm_register_cap(CAPABILITY_ID_BASIC_PASSTHROUGH, &passthr_impl);
}

/* Must be earlier than kcm driver init() */
subsys_initcall(kasop_init_passthr);
