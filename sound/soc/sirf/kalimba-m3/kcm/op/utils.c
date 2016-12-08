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
#include "../../audio-protocol.h"
#include "../kasobj.h"
#include "../kcm.h"
#include "utils.h"

#define CTRL_GET 0
#define CTRL_PUT 1

static int op_ctrl_single_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ctrl_idx, *ctrl_v, *ctrl_f;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &ctrl_idx);
	u32 ret;

	kasobj_register_m3_op();
	if (!(op->op_m3)) {
		kcm_err("KCM: %s() op_m3(%s) is NULL\n",
			__func__, op->obj.name);
		return -EINVAL;
	}

	ctrl_v = op->ctrl_value;
	ctrl_f = op->ctrl_flag;
	if (ctrl_f[ctrl_idx]) {	/* if ctrl value keep same then return */
		ucontrol->value.integer.value[0] = ctrl_v[ctrl_idx];
		return 0;
	}
	kas_ctrl_msg(CTRL_GET, op->op_m3, ctrl_idx, 0, 0, &ret);
	ucontrol->value.integer.value[0] = ret;
	ctrl_v[ctrl_idx] = ret;
	ctrl_f[ctrl_idx] = 1;

	return 0;
}

static int op_ctrl_single_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ctrl_idx, ret, *ctrl_v, *ctrl_f;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &ctrl_idx);
	int value = ucontrol->value.integer.value[0];

	kasobj_register_m3_op();
	if (!(op->op_m3)) {
		kcm_err("KCM: %s() op_m3(%s) is NULL\n",
			__func__, op->obj.name);
		return -EINVAL;
	}

	kas_ctrl_msg(CTRL_PUT, op->op_m3, ctrl_idx, 0, value, &ret);
	if (ret) {
		kcm_err("KCM: %s() %s failed, ctrl_id = %d, value = %d\n",
			__func__, op->obj.name, ctrl_idx, value);
		return -EINVAL;
	}

	ctrl_v = op->ctrl_value;
	ctrl_f = op->ctrl_flag;
	ctrl_v[ctrl_idx] = value;
	ctrl_f[ctrl_idx] = 0;	/* set ctrl value modified */

	return 0;
}

/* SOC_SINGLE_EXT_TLV(name, reg, shift, max, invert, get, put, tlv) */
struct snd_kcontrol_new *kasop_ctrl_single_ext_tlv(const char *name,
		struct kasobj_op *op, int max, const unsigned int *tlv,
		int param)
{
	/* Allocate snd_control_new and soc_mixer_control altogether */
	struct snd_kcontrol_new *ctrl = kzalloc((sizeof(struct snd_kcontrol_new)
		+ sizeof(struct soc_mixer_control)), GFP_KERNEL);
	struct soc_mixer_control *mixer =
		(struct soc_mixer_control *)(ctrl + 1);

	ctrl->iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	ctrl->name = kstrdup(name, GFP_KERNEL);
	ctrl->access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |
		SNDRV_CTL_ELEM_ACCESS_READWRITE;
	ctrl->tlv.p = tlv;
	ctrl->info = snd_soc_info_volsw;
	ctrl->get = op_ctrl_single_get;
	ctrl->put = op_ctrl_single_put;
	ctrl->private_value = (unsigned long)mixer;

	mixer->max = mixer->platform_max = max;
	kasobj_ctrl_set_op(ctrl, op, param);

	return ctrl;
}

static int op_ctrl_double_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ctrl_idx, *ctrl_v, *ctrl_f;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &ctrl_idx);
	u32 ret0, ret1;

	kasobj_register_m3_op();
	if (!(op->op_m3)) {
		kcm_err("KCM: %s() op_m3(%s) is NULL\n",
			__func__, op->obj.name);
		return -EINVAL;
	}

	ctrl_v = op->ctrl_value;
	ctrl_f = op->ctrl_flag;
	if (ctrl_f[ctrl_idx] & 0x01) {	/* bit0 is used as modfy flag */
		ucontrol->value.integer.value[0] = ctrl_v[ctrl_idx];
		ucontrol->value.integer.value[1] = ctrl_f[ctrl_idx] >> 1;
		return 0;
	}
	kas_ctrl_msg(CTRL_GET, op->op_m3, ctrl_idx, 0, 0, &ret0);
	kas_ctrl_msg(CTRL_GET, op->op_m3, ctrl_idx, 1, 0, &ret1);
	ucontrol->value.integer.value[0] = ret0;
	ucontrol->value.integer.value[1] = ret1;
	ctrl_v[ctrl_idx] = ret0;
	ctrl_f[ctrl_idx] = ret1 << 1;
	ctrl_f[ctrl_idx] += 1;

	return 0;
}

static int op_ctrl_double_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ctrl_idx, ret, *ctrl_v, *ctrl_f;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &ctrl_idx);
	int value0 = ucontrol->value.integer.value[0];
	int value1 = ucontrol->value.integer.value[1];

	kasobj_register_m3_op();
	if (!(op->op_m3)) {
		kcm_err("KCM: %s() op_m3(%s) is NULL\n",
			__func__, op->obj.name);
		return -EINVAL;
	}

	kas_ctrl_msg(CTRL_PUT, op->op_m3, ctrl_idx, 0, value0, &ret);
	if (ret) {
		kcm_err("KCM: %s() %s failed, ctrl_id = %d, value0 = %d\n",
			__func__, op->obj.name, ctrl_idx, value0);
		return -EINVAL;
	}
	kas_ctrl_msg(CTRL_PUT, op->op_m3, ctrl_idx, 1, value1, &ret);
	if (ret) {
		kcm_err("KCM: %s() %s failed, ctrl_id = %d, value1 = %d\n",
			__func__, op->obj.name, ctrl_idx, value1);
		return -EINVAL;
	}

	ctrl_v = op->ctrl_value;
	ctrl_f = op->ctrl_flag;
	ctrl_v[ctrl_idx] = value0;
	ctrl_f[ctrl_idx] = value1 << 1;

	return 0;
}

/* SOC_DOUBLE_EXT(xname, reg, shift_left, shift_right, max, invert, get, put) */
struct snd_kcontrol_new *kasop_ctrl_double_ext_tlv(const char *name,
		struct kasobj_op *op, int max, const unsigned int *tlv,
		int param)
{
	/* Allocate snd_control_new and soc_mixer_control altogether */
	struct snd_kcontrol_new *ctrl = kzalloc((sizeof(struct snd_kcontrol_new)
		+ sizeof(struct soc_mixer_control)), GFP_KERNEL);
	struct soc_mixer_control *mixer =
		(struct soc_mixer_control *)(ctrl + 1);

	ctrl->iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	ctrl->name = kstrdup(name, GFP_KERNEL);
	ctrl->access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |
		SNDRV_CTL_ELEM_ACCESS_READWRITE;
	ctrl->tlv.p = tlv;
	ctrl->info = snd_soc_info_volsw;
	ctrl->get = op_ctrl_double_get;
	ctrl->put = op_ctrl_double_put;
	ctrl->private_value = (unsigned long)mixer;

	mixer->max = mixer->platform_max = max;
	kasobj_ctrl_set_op_double(ctrl, op, param);

	return ctrl;
}
