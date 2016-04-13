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
#include "../kasobj.h"
#include "utils.h"

/* SOC_SINGLE_EXT_TLV(name, reg, shift, max, invert, get, put, tlv) */
struct snd_kcontrol_new *kasop_ctrl_single_ext_tlv(const char *name,
		struct kasobj_op *op, int max, snd_kcontrol_get_t get,
		snd_kcontrol_put_t put, const unsigned int *tlv, int param)
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
	ctrl->get = get;
	ctrl->put = put;
	ctrl->private_value = (unsigned long)mixer;

	mixer->max = mixer->platform_max = max;
	kasobj_ctrl_set_op(ctrl, op, param);

	return ctrl;
}

/* SOC_DOUBLE_EXT(xname, reg, shift_left, shift_right, max, invert, get, put) */
struct snd_kcontrol_new *kasop_ctrl_double_ext_tlv(const char *name,
		struct kasobj_op *op, int max, snd_kcontrol_get_t get,
		snd_kcontrol_put_t put, const unsigned int *tlv, int param)
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
	ctrl->get = get;
	ctrl->put = put;
	ctrl->private_value = (unsigned long)mixer;

	mixer->max = mixer->platform_max = max;
	kasobj_ctrl_set_op_double(ctrl, op, param);

	return ctrl;
}
