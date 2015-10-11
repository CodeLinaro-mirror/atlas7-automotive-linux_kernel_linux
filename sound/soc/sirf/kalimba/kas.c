/*
 * CSR Altas7 kailimba audio
 *
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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
#include <linux/platform_device.h>
#include <sound/soc.h>
#include "kcm.h"
#include "dsp.h"

/* User PEQ */
#define USER_PEQ_SWITCH		0x0000
#define USER_PEQ_BAND1_FC	0x0001
#define USER_PEQ_BAND2_FC	0x0002
#define USER_PEQ_BAND3_FC	0x0003
#define USER_PEQ_BAND4_FC	0x0004
#define USER_PEQ_BAND5_FC	0x0005
#define USER_PEQ_BAND6_FC	0x0006
#define USER_PEQ_BAND7_FC	0x0007
#define USER_PEQ_BAND8_FC	0x0008
#define USER_PEQ_BAND9_FC	0x0009
#define USER_PEQ_BAND10_FC	0x000a
#define USER_PEQ_BAND1_GAIN	0x0011
#define USER_PEQ_BAND2_GAIN	0x0012
#define USER_PEQ_BAND3_GAIN	0x0013
#define USER_PEQ_BAND4_GAIN	0x0014
#define USER_PEQ_BAND5_GAIN	0x0015
#define USER_PEQ_BAND6_GAIN	0x0016
#define USER_PEQ_BAND7_GAIN	0x0017
#define USER_PEQ_BAND8_GAIN	0x0018
#define USER_PEQ_BAND9_GAIN	0x0019
#define USER_PEQ_BAND10_GAIN	0x001a
/* Spk1 PEQ */
#define SPK1_PEQ_SWITCH		0x1000
#define SPK1_PEQ_BAND1_FC	0x1001
#define SPK1_PEQ_BAND2_FC	0x1002
#define SPK1_PEQ_BAND3_FC	0x1003
#define SPK1_PEQ_BAND4_FC	0x1004
#define SPK1_PEQ_BAND5_FC	0x1005
#define SPK1_PEQ_BAND6_FC	0x1006
#define SPK1_PEQ_BAND7_FC	0x1007
#define SPK1_PEQ_BAND8_FC	0x1008
#define SPK1_PEQ_BAND9_FC	0x1009
#define SPK1_PEQ_BAND10_FC	0x100a
#define SPK1_PEQ_BAND1_GAIN	0x1011
#define SPK1_PEQ_BAND2_GAIN	0x1012
#define SPK1_PEQ_BAND3_GAIN	0x1013
#define SPK1_PEQ_BAND4_GAIN	0x1014
#define SPK1_PEQ_BAND5_GAIN	0x1015
#define SPK1_PEQ_BAND6_GAIN	0x1016
#define SPK1_PEQ_BAND7_GAIN	0x1017
#define SPK1_PEQ_BAND8_GAIN	0x1018
#define SPK1_PEQ_BAND9_GAIN	0x1019
#define SPK1_PEQ_BAND10_GAIN	0x101a
/* Spk2 PEQ */
#define SPK2_PEQ_SWITCH		0x2000
#define SPK2_PEQ_BAND1_FC	0x2001
#define SPK2_PEQ_BAND2_FC	0x2002
#define SPK2_PEQ_BAND3_FC	0x2003
#define SPK2_PEQ_BAND4_FC	0x2004
#define SPK2_PEQ_BAND5_FC	0x2005
#define SPK2_PEQ_BAND6_FC	0x2006
#define SPK2_PEQ_BAND7_FC	0x2007
#define SPK2_PEQ_BAND8_FC	0x2008
#define SPK2_PEQ_BAND9_FC	0x2009
#define SPK2_PEQ_BAND10_FC	0x200a
#define SPK2_PEQ_BAND1_GAIN	0x2011
#define SPK2_PEQ_BAND2_GAIN	0x2012
#define SPK2_PEQ_BAND3_GAIN	0x2013
#define SPK2_PEQ_BAND4_GAIN	0x2014
#define SPK2_PEQ_BAND5_GAIN	0x2015
#define SPK2_PEQ_BAND6_GAIN	0x2016
#define SPK2_PEQ_BAND7_GAIN	0x2017
#define SPK2_PEQ_BAND8_GAIN	0x2018
#define SPK2_PEQ_BAND9_GAIN	0x2019
#define SPK2_PEQ_BAND10_GAIN	0x201a
/* Spk3 PEQ */
#define SPK3_PEQ_SWITCH		0x3000
#define SPK3_PEQ_BAND1_FC	0x3001
#define SPK3_PEQ_BAND2_FC	0x3002
#define SPK3_PEQ_BAND3_FC	0x3003
#define SPK3_PEQ_BAND4_FC	0x3004
#define SPK3_PEQ_BAND5_FC	0x3005
#define SPK3_PEQ_BAND6_FC	0x3006
#define SPK3_PEQ_BAND7_FC	0x3007
#define SPK3_PEQ_BAND8_FC	0x3008
#define SPK3_PEQ_BAND9_FC	0x3009
#define SPK3_PEQ_BAND10_FC	0x300a
#define SPK3_PEQ_BAND1_GAIN	0x3011
#define SPK3_PEQ_BAND2_GAIN	0x3012
#define SPK3_PEQ_BAND3_GAIN	0x3013
#define SPK3_PEQ_BAND4_GAIN	0x3014
#define SPK3_PEQ_BAND5_GAIN	0x3015
#define SPK3_PEQ_BAND6_GAIN	0x3016
#define SPK3_PEQ_BAND7_GAIN	0x3017
#define SPK3_PEQ_BAND8_GAIN	0x3018
#define SPK3_PEQ_BAND9_GAIN	0x3019
#define SPK3_PEQ_BAND10_GAIN	0x301a
/* Spk4 PEQ */
#define SPK4_PEQ_SWITCH		0x4000
#define SPK4_PEQ_BAND1_FC	0x4001
#define SPK4_PEQ_BAND2_FC	0x4002
#define SPK4_PEQ_BAND3_FC	0x4003
#define SPK4_PEQ_BAND4_FC	0x4004
#define SPK4_PEQ_BAND5_FC	0x4005
#define SPK4_PEQ_BAND6_FC	0x4006
#define SPK4_PEQ_BAND7_FC	0x4007
#define SPK4_PEQ_BAND8_FC	0x4008
#define SPK4_PEQ_BAND9_FC	0x4009
#define SPK4_PEQ_BAND10_FC	0x400a
#define SPK4_PEQ_BAND1_GAIN	0x4011
#define SPK4_PEQ_BAND2_GAIN	0x4012
#define SPK4_PEQ_BAND3_GAIN	0x4013
#define SPK4_PEQ_BAND4_GAIN	0x4014
#define SPK4_PEQ_BAND5_GAIN	0x4015
#define SPK4_PEQ_BAND6_GAIN	0x4016
#define SPK4_PEQ_BAND7_GAIN	0x4017
#define SPK4_PEQ_BAND8_GAIN	0x4018
#define SPK4_PEQ_BAND9_GAIN	0x4019
#define SPK4_PEQ_BAND10_GAIN	0x401a
/* Delay Control */
#define DELAY_CHANNEL0_DELAY	0x5000
#define DELAY_CHANNEL1_DELAY	0x5001
#define DELAY_CHANNEL2_DELAY	0x5002
#define DELAY_CHANNEL3_DELAY	0x5003

/* PEQ Switch: Mute-0, Process-1, Bypass-2 */
static int peq_switch[PEQ_NUM_MAX] = { 1, 1, 1, 1, 1 };
static u16 peq_oper_conf_switch[] = { 0x0001, 0x0001, 0x0000, 0x0002 };
static u16 peq_oper_conf_param[] = {
	/* blocks, offset, param_num, param[] */
	0x0001, 0x0000, 0x0001, 0x0000, 0x0000, 0x0000
};

static int kas_control_set(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
	    (struct soc_mixer_control *) kcontrol->private_value;
	int value = ucontrol->value.integer.value[0];
	unsigned int reg = mc->reg;
	int max = mc->max;
	int shift = mc->shift;
	int band;
	int type;
	int value_24b;
	struct component *component_control;

	if (value > max)
		return -EINVAL;

	type = (reg & CTYPE_MASK) >> CTYPE_SHIFT;
	component_control = get_control_component(type);
	switch (type) {
	case CTYPE_USER_PEQ:
	case CTYPE_SPK1_PEQ:
	case CTYPE_SPK2_PEQ:
	case CTYPE_SPK3_PEQ:
	case CTYPE_SPK4_PEQ:
		if (reg & CTYPE_PEQ_GAIN_MASK) {	/* gain */
			band = reg & CTYPE_PEQ_BAND_MASK;
			/* 0 ~ 80 --> -60db ~ +20db */
			value = value - shift;
			/* Q24: 12.N */
			value_24b = (value << 12) & 0x00FFFFFF;
			if (*(u16 *) component_control->params[0]) {
				peq_oper_conf_param[1] = PEQ_PARAM_MAIN_LEN_24B
				    + PEQ_PARAM_BAND_LEN_24B * (band - 1)
				    + PEQ_PARAM_BAND_GAIN;
				peq_oper_conf_param[3] =
				    (value_24b >> 8) & 0x0000FFFF;
				peq_oper_conf_param[4] =
				    (value_24b & 0x000000FF) << 8;
				component_control->params[1] =
				    OPMSG_COMMON_SET_PARAMS;
				component_control->params[2] = 6;
				component_control->params[3] =
				    (u32) (peq_oper_conf_param);
				execute_component(component_control);
			}
			set_peq_param(type, band, PEQ_PARAM_BAND_GAIN,
				      value_24b);
		} else if (reg & CTYPE_PEQ_FC_MASK) {	/* FC */
			if (value < shift)
				return -EINVAL;
			band = reg & CTYPE_PEQ_BAND_MASK;
			/* Q24: 20.N */
			value_24b = (value << 4) & 0x00FFFFFF;
			if (*(u16 *) component_control->params[0]) {
				peq_oper_conf_param[1] = PEQ_PARAM_MAIN_LEN_24B
				    + PEQ_PARAM_BAND_LEN_24B * (band - 1)
				    + PEQ_PARAM_BAND_FC;
				peq_oper_conf_param[1] = 4 * band + 1;
				peq_oper_conf_param[3] =
				    (value_24b >> 8) & 0x0000FFFF;
				peq_oper_conf_param[4] =
				    (value_24b & 0x000000FF) << 8;
				component_control->params[1] =
				    OPMSG_COMMON_SET_PARAMS;
				component_control->params[2] = 6;
				component_control->params[3] =
				    (u32) (peq_oper_conf_param);
				execute_component(component_control);
			}
			set_peq_param(type, band, PEQ_PARAM_BAND_FC,
				      value_24b);
		} else {	/* switch */

			if (*(u16 *) component_control->params[0]) {
				peq_oper_conf_switch[3] = value + 1;
				component_control->params[1] =
				    OPMSG_COMMON_SET_CONTROL;
				component_control->params[2] = 4;
				component_control->params[3] =
				    (u32) (peq_oper_conf_switch);
				execute_component(component_control);
				peq_switch[type] = value;
			}
		}
		break;
	case CTYPE_DELAY:
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int kas_control_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
	    (struct soc_mixer_control *) kcontrol->private_value;
	unsigned int reg = mc->reg;
	int shift = mc->shift;
	int type = (reg & CTYPE_MASK) >> CTYPE_SHIFT;
	int band;
	int value_24b;

	switch (type) {
	case CTYPE_USER_PEQ:
	case CTYPE_SPK1_PEQ:
	case CTYPE_SPK2_PEQ:
	case CTYPE_SPK3_PEQ:
	case CTYPE_SPK4_PEQ:
		if (reg & CTYPE_PEQ_GAIN_MASK) {	/* gain */
			band = reg & CTYPE_PEQ_BAND_MASK;
			value_24b =
			    get_peq_param(type, band, PEQ_PARAM_BAND_GAIN);
			if (value_24b & 0x00800000) {
				ucontrol->value.integer.value[0] =
				    ((value_24b >> 12) | 0xFFFFF000) + shift;
			} else
				ucontrol->value.integer.value[0] =
				    (value_24b >> 12) + shift;
		} else if (reg & CTYPE_PEQ_FC_MASK) {	/* FC */
			band = reg & CTYPE_PEQ_BAND_MASK;
			value_24b =
			    get_peq_param(type, band, PEQ_PARAM_BAND_FC);
			ucontrol->value.integer.value[0] = value_24b >> 4;
		} else {	/* switch */

			ucontrol->value.integer.value[0] = peq_switch[type];
		}
		break;
	case CTYPE_DELAY:
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct snd_kcontrol_new atlas7_kas_controls[] = {
	/* User PEQ */
	SOC_SINGLE_EXT("User PEQ Switch Mode", USER_PEQ_SWITCH, 0, 2, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("User PEQ Band1 FC", USER_PEQ_BAND1_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("User PEQ Band2 FC", USER_PEQ_BAND2_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("User PEQ Band3 FC", USER_PEQ_BAND3_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("User PEQ Band4 FC", USER_PEQ_BAND4_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("User PEQ Band5 FC", USER_PEQ_BAND5_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("User PEQ Band6 FC", USER_PEQ_BAND6_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("User PEQ Band7 FC", USER_PEQ_BAND7_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("User PEQ Band8 FC", USER_PEQ_BAND8_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("User PEQ Band9 FC", USER_PEQ_BAND9_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("User PEQ Band10 FC", USER_PEQ_BAND10_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("User PEQ Band1 Gain", USER_PEQ_BAND1_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("User PEQ Band2 Gain", USER_PEQ_BAND2_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("User PEQ Band3 Gain", USER_PEQ_BAND3_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("User PEQ Band4 Gain", USER_PEQ_BAND4_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("User PEQ Band5 Gain", USER_PEQ_BAND5_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("User PEQ Band6 Gain", USER_PEQ_BAND6_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("User PEQ Band7 Gain", USER_PEQ_BAND7_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("User PEQ Band8 Gain", USER_PEQ_BAND8_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("User PEQ Band9 Gain", USER_PEQ_BAND9_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("User PEQ Band10 Gain", USER_PEQ_BAND10_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	/* Spk1 PEQ */
	SOC_SINGLE_EXT("Spk1 PEQ Switch Mode", SPK1_PEQ_SWITCH, 0, 2, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk1 PEQ Band1 FC", SPK1_PEQ_BAND1_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk1 PEQ Band2 FC", SPK1_PEQ_BAND2_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk1 PEQ Band3 FC", SPK1_PEQ_BAND3_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk1 PEQ Band4 FC", SPK1_PEQ_BAND4_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk1 PEQ Band5 FC", SPK1_PEQ_BAND5_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk1 PEQ Band6 FC", SPK1_PEQ_BAND6_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk1 PEQ Band7 FC", SPK1_PEQ_BAND7_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk1 PEQ Band8 FC", SPK1_PEQ_BAND8_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk1 PEQ Band9 FC", SPK1_PEQ_BAND9_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk1 PEQ Band10 FC", SPK1_PEQ_BAND10_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk1 PEQ Band1 Gain", SPK1_PEQ_BAND1_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk1 PEQ Band2 Gain", SPK1_PEQ_BAND2_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk1 PEQ Band3 Gain", SPK1_PEQ_BAND3_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk1 PEQ Band4 Gain", SPK1_PEQ_BAND4_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk1 PEQ Band5 Gain", SPK1_PEQ_BAND5_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk1 PEQ Band6 Gain", SPK1_PEQ_BAND6_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk1 PEQ Band7 Gain", SPK1_PEQ_BAND7_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk1 PEQ Band8 Gain", SPK1_PEQ_BAND8_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk1 PEQ Band9 Gain", SPK1_PEQ_BAND9_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk1 PEQ Band10 Gain", SPK1_PEQ_BAND10_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	/* Spk2 PEQ */
	SOC_SINGLE_EXT("Spk2 PEQ Switch Mode", SPK2_PEQ_SWITCH, 0, 2, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk2 PEQ Band1 FC", SPK2_PEQ_BAND1_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk2 PEQ Band2 FC", SPK2_PEQ_BAND2_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk2 PEQ Band3 FC", SPK2_PEQ_BAND3_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk2 PEQ Band4 FC", SPK2_PEQ_BAND4_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk2 PEQ Band5 FC", SPK2_PEQ_BAND5_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk2 PEQ Band6 FC", SPK2_PEQ_BAND6_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk2 PEQ Band7 FC", SPK2_PEQ_BAND7_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk2 PEQ Band8 FC", SPK2_PEQ_BAND8_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk2 PEQ Band9 FC", SPK2_PEQ_BAND9_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk2 PEQ Band10 FC", SPK2_PEQ_BAND10_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk2 PEQ Band1 Gain", SPK2_PEQ_BAND1_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk2 PEQ Band2 Gain", SPK2_PEQ_BAND2_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk2 PEQ Band3 Gain", SPK2_PEQ_BAND3_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk2 PEQ Band4 Gain", SPK2_PEQ_BAND4_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk2 PEQ Band5 Gain", SPK2_PEQ_BAND5_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk2 PEQ Band6 Gain", SPK2_PEQ_BAND6_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk2 PEQ Band7 Gain", SPK2_PEQ_BAND7_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk2 PEQ Band8 Gain", SPK2_PEQ_BAND8_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk2 PEQ Band9 Gain", SPK2_PEQ_BAND9_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk2 PEQ Band10 Gain", SPK2_PEQ_BAND10_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	/* Spk3 PEQ */
	SOC_SINGLE_EXT("Spk3 PEQ Switch Mode", SPK3_PEQ_SWITCH, 0, 2, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk3 PEQ Band1 FC", SPK3_PEQ_BAND1_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk3 PEQ Band2 FC", SPK3_PEQ_BAND2_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk3 PEQ Band3 FC", SPK3_PEQ_BAND3_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk3 PEQ Band4 FC", SPK3_PEQ_BAND4_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk3 PEQ Band5 FC", SPK3_PEQ_BAND5_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk3 PEQ Band6 FC", SPK3_PEQ_BAND6_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk3 PEQ Band7 FC", SPK3_PEQ_BAND7_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk3 PEQ Band8 FC", SPK3_PEQ_BAND8_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk3 PEQ Band9 FC", SPK3_PEQ_BAND9_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk3 PEQ Band10 FC", SPK3_PEQ_BAND10_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk3 PEQ Band1 Gain", SPK3_PEQ_BAND1_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk3 PEQ Band2 Gain", SPK3_PEQ_BAND2_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk3 PEQ Band3 Gain", SPK3_PEQ_BAND3_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk3 PEQ Band4 Gain", SPK3_PEQ_BAND4_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk3 PEQ Band5 Gain", SPK3_PEQ_BAND5_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk3 PEQ Band6 Gain", SPK3_PEQ_BAND6_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk3 PEQ Band7 Gain", SPK3_PEQ_BAND7_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk3 PEQ Band8 Gain", SPK3_PEQ_BAND8_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk3 PEQ Band9 Gain", SPK3_PEQ_BAND9_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk3 PEQ Band10 Gain", SPK3_PEQ_BAND10_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	/* Spk4 PEQ */
	SOC_SINGLE_EXT("Spk4 PEQ Switch Mode", SPK4_PEQ_SWITCH, 0, 2, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk4 PEQ Band1 FC", SPK4_PEQ_BAND1_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk4 PEQ Band2 FC", SPK4_PEQ_BAND2_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk4 PEQ Band3 FC", SPK4_PEQ_BAND3_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk4 PEQ Band4 FC", SPK4_PEQ_BAND4_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk4 PEQ Band5 FC", SPK4_PEQ_BAND5_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk4 PEQ Band6 FC", SPK4_PEQ_BAND6_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk4 PEQ Band7 FC", SPK4_PEQ_BAND7_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk4 PEQ Band8 FC", SPK4_PEQ_BAND8_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk4 PEQ Band9 FC", SPK4_PEQ_BAND9_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk4 PEQ Band10 FC", SPK4_PEQ_BAND10_FC, 20, 24000, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk4 PEQ Band1 Gain", SPK4_PEQ_BAND1_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk4 PEQ Band2 Gain", SPK4_PEQ_BAND2_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk4 PEQ Band3 Gain", SPK4_PEQ_BAND3_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk4 PEQ Band4 Gain", SPK4_PEQ_BAND4_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk4 PEQ Band5 Gain", SPK4_PEQ_BAND5_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk4 PEQ Band6 Gain", SPK4_PEQ_BAND6_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk4 PEQ Band7 Gain", SPK4_PEQ_BAND7_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk4 PEQ Band8 Gain", SPK4_PEQ_BAND8_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk4 PEQ Band9 Gain", SPK4_PEQ_BAND9_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
	SOC_SINGLE_EXT("Spk4 PEQ Band10 Gain", SPK4_PEQ_BAND10_GAIN, 60, 80, 0,
		kas_control_get, kas_control_set),
};

static const struct snd_soc_dapm_widget kas_audio_widgets[] = {
	SND_SOC_DAPM_HP("Headphones", NULL),
	SND_SOC_DAPM_LINE("LINEIN", NULL),
	SND_SOC_DAPM_MIC("MICIN", NULL),
};

static const struct snd_soc_dapm_route kas_audio_map[] = {
	{"Headphones", NULL, "LOUT0"},
	{"Headphones", NULL, "LOUT1"},
	{"Headphones", NULL, "LOUT2"},
	{"Headphones", NULL, "LOUT3"},
	{"AIF Playback", NULL, "Codec OUT"},
	{"LIN0", NULL, "LINEIN"},
	{"MICIN0", NULL, "MICIN"},
	{"Codec IN", NULL, "AIF Capture"},
};

static struct snd_soc_dai_link kas_audio_dais[] = {
	/* Front End DAI links */
	{
		.name = "Music",
		.stream_name = "Music Playback",
		.cpu_dai_name = "Music Pin",
		.platform_name = "kas-pcm-audio",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
	},
	{
		.name = "Navigation",
		.stream_name = "Navigation Playback",
		.cpu_dai_name = "Navigation Pin",
		.platform_name = "kas-pcm-audio",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
	},
	{
		.name = "Alarm",
		.stream_name = "Alarm Playback",
		.cpu_dai_name = "Alarm Pin",
		.platform_name = "kas-pcm-audio",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
	},
	{
		.name = "Analog Capture",
		.stream_name = "Analog Capture",
		.cpu_dai_name = "Capture Pin",
		.platform_name = "kas-pcm-audio",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_capture = 1,
	},
	/* Back End DAI links */
	{
		/* IACC - Codec */
		.name = "IACC Codec",
		.be_id = 0,
		.cpu_dai_name = "snd-soc-dummy-dai",
		.platform_name = "snd-soc-dummy",
		.no_pcm = 1,
		.codec_name = "10e30000.atlas7_codec",
		.codec_dai_name = "atlas7-codec-hifi",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
};

static struct snd_soc_card kas_audio_card = {
	.name = "kas-audio-card",
	.owner = THIS_MODULE,
	.controls = atlas7_kas_controls,
	.num_controls = ARRAY_SIZE(atlas7_kas_controls),
	.dai_link = kas_audio_dais,
	.num_links = ARRAY_SIZE(kas_audio_dais),
	.dapm_widgets = kas_audio_widgets,
	.num_dapm_widgets = ARRAY_SIZE(kas_audio_widgets),
	.dapm_routes = kas_audio_map,
	.num_dapm_routes = ARRAY_SIZE(kas_audio_map),
	.fully_routed = true,
};

static int kas_audio_probe(struct platform_device *pdev)
{
	kas_audio_card.dev = &pdev->dev;
	return devm_snd_soc_register_card(&pdev->dev, &kas_audio_card);
}

static const struct of_device_id kas_audio_match[] = {
	{.compatible = "csr,kas-audio", },
	{},
};
static struct platform_driver kas_audio_driver = {
	.probe = kas_audio_probe,
	.driver = {
		.name = "kas-audio",
		.of_match_table = kas_audio_match,
	},
};
module_platform_driver(kas_audio_driver);

/* Module information */
MODULE_DESCRIPTION("Kalimba audio driver for SiRF A7DA");
MODULE_LICENSE("GPL v2");
