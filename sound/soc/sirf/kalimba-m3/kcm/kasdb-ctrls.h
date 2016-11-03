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

#ifndef OP_CTRLS_H
#define OP_CTRLS_H

#define KCM_PEQ_ALL_BAND(name, param)	\
	name " PEQ Band1 " param ";"	\
	name " PEQ Band2 " param ";"	\
	name " PEQ Band3 " param ";"	\
	name " PEQ Band4 " param ";"	\
	name " PEQ Band5 " param ";"	\
	name " PEQ Band6 " param ";"	\
	name " PEQ Band7 " param ";"	\
	name " PEQ Band8 " param ";"	\
	name " PEQ Band9 " param ";"	\
	name " PEQ Band10 " param ";"

#define KCM_CTRLS_PEQ(name)		\
	KCM_PEQ_ALL_BAND(name, "Gain")	\
	KCM_PEQ_ALL_BAND(name, "FC")	\
	name " PEQ Bands Num;"		\
	name " PEQ Core Type;"		\
	name " PEQ Master Gain;"	\
	name " PEQ Switch Mode;"	\
	name " PEQ UCID"

#define KCM_MIXER_STREAM(name)		\
	name " Stream CH1 Gain;"	\
	name " Stream CH2 Gain;"	\
	name " Stream CH3 Gain;"	\
	name " Stream CH4 Gain;"	\
	name " Stream CH5 Gain;"	\
	name " Stream CH6 Gain;"	\
	name " Stream Vol;"		\
	name " Stream Mute;"		\
	name " Stream Ramp"

/* If a stream has no controls, fill the parameter with "NOCTRL" */
#define KCM_CTRLS_MIXER(stream1, stream2, stream3)\
	KCM_MIXER_STREAM(stream1) ";"	\
	KCM_MIXER_STREAM(stream2) ";"	\
	KCM_MIXER_STREAM(stream3)

#define KCM_CTRLS_BASICPASS(name)	\
	name " Pregain;"		\
	name " Premute"

#define KCM_CTRLS_BASS(name)		\
	name " DBE Effect Strength;"	\
	name " DBE Amp Limit;"		\
	name " DBE LP FC;"		\
	name " DBE HP FC;"		\
	name " DBE Harm Content;"	\
	name " DBE Xover FC;"		\
	name " DBE Mix Balance;"	\
	name " DBE Switch Mode;"	\
	name " DBE UCID"

#define KCM_CTRLS_DELAY(name)	\
	name " Chan1 Delay;"	\
	name " Chan2 Delay;"	\
	name " Chan3 Delay;"	\
	name " Chan4 Delay"

#define KCM_CTRLS_VOLCTRL(name)		\
	name " Vol Front Left;"		\
	name " Vol Front Right;"	\
	name " Vol Rear Left;"		\
	name " Vol Rear Right;"		\
	name " Vol Master Gain;"	\
	name " Vol Master Mute"

#define KCM_CTRLS_AECREF(name)	\
	name " CVC 2Mic Switch;"\
	"Input Path"

#define KCM_CTRLS_CVCSEND(name)	\
	name " CVC Send Mode;"	\
	name " CVC Send UCID"

#define KCM_CTRLS_CVCRECV(name)	\
	name " CVC Recv Mode;"	\
	name " CVC Recv UCID"

#define KCM_CTRLS_SOURCESYNC(name)	\
	name " Srcsync Active Stream;"	\
	name " Srcsync Purge Flag;"	\
	name " Srcsync Trans Samples"

#define KCM_CTRLS_CHMIXER(name)		\
	name " CH Mixer Gain"

#endif /* OP_CTRLS_H */
