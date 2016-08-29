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
		struct kasobj_op *op, int param)
{
	struct soc_mixer_control *mixer =
		(struct soc_mixer_control *)ctrl->private_value;

	mixer->reg = mixer->rreg = (int)op;	/* Bug on 64-bit platform! */
	mixer->shift = mixer->rshift = param;
}

static inline void kasobj_ctrl_set_op_double(struct snd_kcontrol_new *ctrl,
		struct kasobj_op *op, int param)
{
	struct soc_mixer_control *mixer =
		(struct soc_mixer_control *)ctrl->private_value;

	mixer->reg = mixer->rreg = (int)op; /* Bug on 64-bit platform! */
	mixer->shift = param;
	mixer->rshift = param + 1;
}

/* Retrieve operator object from control context */
static inline struct kasobj_op *kasobj_ctrl_get_op(struct snd_kcontrol *ctrl,
		int *param)
{
	struct soc_mixer_control *mixer =
		(struct soc_mixer_control *)ctrl->private_value;

	if (param)
		*param = mixer->shift;
	return (struct kasobj_op *)mixer->reg;
}

/* SOC_SINGLE_EXT_TLV(name, reg, shift, max, invert, get, put, tlv) */
struct snd_kcontrol_new *kasop_ctrl_single_ext_tlv(const char *name,
		struct kasobj_op *op, int max, const unsigned int *tlv,
		int param);

/* SOC_DOUBLE_EXT(xname, reg, shift_left, shift_right, max, invert, get, put) */
struct snd_kcontrol_new *kasop_ctrl_double_ext_tlv(const char *name,
		struct kasobj_op *op, int max, const unsigned int *tlv,
		int param);
