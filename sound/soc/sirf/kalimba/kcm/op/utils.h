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

static inline void kasobj_ctrl_set_op(struct snd_kcontrol_new *ctrl,
		struct kasobj_op *op)
{
	struct soc_mixer_control *mixer =
		(struct soc_mixer_control *)ctrl->private_value;

	mixer->reg = mixer->rreg = (int)op;	/* Bug on 64-bit platform! */
}

/* Retrieve operator object from control context */
static inline struct kasobj_op *kasobj_ctrl_get_op(struct snd_kcontrol *ctrl)
{
	struct soc_mixer_control *mixer =
		(struct soc_mixer_control *)ctrl->private_value;

	return (struct kasobj_op *)mixer->reg;
}

/* SOC_SINGLE_EXT_TLV(name, reg, shift, max, invert, get, put, tlv) */
struct snd_kcontrol_new *kasop_ctrl_single_ext_tlv(const char *name,
		struct kasobj_op *op, int max, snd_kcontrol_get_t get,
		snd_kcontrol_put_t put, const unsigned int *tlv);
