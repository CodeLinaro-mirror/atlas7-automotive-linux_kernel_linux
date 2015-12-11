/*
 * kailimba components PCM drive
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

#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "dsp.h"
#include "ipc.h"
#include "kcm.h"

#define BUFF_BYTES_EACH_CHANNEL		256
#define BUFF_BYTES_IACC_SCO_PLAYBACK	192
#define BUFF_BYTES_IACC_SCO_CAPTURE	64
#define BUFF_BYTES_USP_SCO_PLAYBACK	480
#define BUFF_BYTES_USP_SCO_CAPTURE	480

#define SET_PRIMARY_STREAM		0
#define CLEAR_PRIMARY_STREAM		1

static struct kcm_t *kcm;

static struct list_head components_chain_list;

static struct component components_shared[256];
static struct component components_cvc_shared[256];
static int shared_components_size;
static int cvc_shared_components_size;
static unsigned long active_stream;
static int curr_primary_stream;
static u16 *volume_control_op_id;
static u16 *mixer_op_id;
static u16 *peq_op_id[PEQ_NUM_MAX];

/* PEQ default sample rate: 48KHz, for init */
static u16 peq_sample_rate = PEQ_SAMPLE_RATE;

/* Hard code for init the components chain */
static u32 ep_configure_key[] = {
	ENDPOINT_CONF_AUDIO_SAMPLE_RATE,
	ENDPOINT_CONF_AUDIO_DATA_FORMAT,
	ENDPOINT_CONF_DRAM_PACKING_FORMAT,
	ENDPOINT_CONF_INTERLEAVING_MODE,
	ENDPOINT_CONF_CLOCK_MASTER,
	ENDPOINT_CONF_PERIOD_SIZE,
};

static char *sw_ep_configure_params_key[] = {
	"sw_ep_conf_audio_sample_rate",
	"sw_ep_conf_audio_data_format",
	"sw_ep_conf_dram_packing_format",
	"sw_ep_conf_interleaving_mode",
	"sw_ep_conf_clock_master",
	"sw_ep_period_size",
	"sw_ep_channles",
	"sw_ep_handle_addr",
};

static void init_shared_components(void)
{
	int i;
	static u16 mixer_oper_conf_channels[3] = {0x4, 0x4, 0x4};
	/*
	 * Mixer need set the sample rate for avoid noise.
	 * The value is "sample rate / 25".
	 */
	static u16 mixer_sample_rate = 48000 / 25;

	components_shared[0].component_id = CREATE_OPERATOR_REQ;
	components_shared[0].execute_phase = EXEC_PHASE_HW_PARAMS;
	components_shared[0].params[0] = CAPABILITY_ID_MIXER;

	mixer_op_id = &(components_shared[0].ret[0]);

	components_shared[1].component_id = OPERATOR_MESSAGE_REQ;
	components_shared[1].execute_phase = EXEC_PHASE_HW_PARAMS;
	components_shared[1].params[0] = (u32)(&components_shared[0].ret[0]);
	components_shared[1].params[1] = OPERATOR_MSG_SET_CHANNELS;
	components_shared[1].params[2] = 3;
	components_shared[1].params[3] = (u32)(mixer_oper_conf_channels);

	components_shared[2].component_id = OPERATOR_MESSAGE_REQ;
	components_shared[2].execute_phase = EXEC_PHASE_HW_PARAMS;
	components_shared[2].params[0] = (u32)(&components_shared[0].ret[0]);
	components_shared[2].params[1] = OPMSG_COMMON_SET_SAMPLE_RATE;
	components_shared[2].params[2] = 1;
	components_shared[2].params[3] = (u32)(&mixer_sample_rate);

	components_shared[3].component_id = GET_SINK_REQ;
	components_shared[3].execute_phase = EXEC_PHASE_HW_PARAMS;
	components_shared[3].params[0] = ENDPOINT_TYPE_IACC;
	components_shared[3].params[1] = ENDPOINT_PHY_DEV_IACC;
	components_shared[3].params[2] = (u32)(&kcm->playback_iacc_ep.channels);
	components_shared[3].params[3] =
		(u32)(&kcm->playback_iacc_ep.handle_phy_addr);

	components_shared[4].component_id = CREATE_OPERATOR_REQ;
	components_shared[4].execute_phase = EXEC_PHASE_HW_PARAMS;
	components_shared[4].params[0] = CAPABILITY_ID_VOLUME_CONTROL;

	/* Init the volume control operator id pointer */
	volume_control_op_id = &(components_shared[4].ret[0]);

	for (i = 0; i < 4; i++) {
		components_shared[5 + i].component_id = ENDPOINT_CONFIGURE_REQ;
		components_shared[5 + i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components_shared[5 + i].params[0] =
			(u32)(&components_shared[3].ret[i]);
		components_shared[5 + i].params[1] =
			ENDPOINT_CONF_AUDIO_SAMPLE_RATE;
		components_shared[5 + i].params[2] =
			(u32)(&kcm->playback_iacc_ep.sample_rate);
	}

	for (i = 0; i < 4; i++) {
		components_shared[9 + i].component_id = ENDPOINT_CONFIGURE_REQ;
		components_shared[9 + i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components_shared[9 + i].params[0] =
			(u32)(&components_shared[3].ret[i]);
		components_shared[9 + i].params[1] =
			ENDPOINT_CONF_AUDIO_DATA_FORMAT;
		components_shared[9 + i].params[2] =
			(u32)(&kcm->playback_iacc_ep.audio_data_format);
	}

	for (i = 0; i < 4; i++) {
		components_shared[13 + i].component_id = ENDPOINT_CONFIGURE_REQ;
		components_shared[13 + i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components_shared[13 + i].params[0] =
			(u32)(&components_shared[3].ret[i]);
		components_shared[13 + i].params[1] =
			ENDPOINT_CONF_DRAM_PACKING_FORMAT;
		components_shared[13 + i].params[2] =
			(u32)(&kcm->playback_iacc_ep.packing_format);
	}

	for (i = 0; i < 4; i++) {
		components_shared[17 + i].component_id = ENDPOINT_CONFIGURE_REQ;
		components_shared[17 + i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components_shared[17 + i].params[0] =
			(u32)(&components_shared[3].ret[i]);
		components_shared[17 + i].params[1] =
			ENDPOINT_CONF_INTERLEAVING_MODE;
		components_shared[17 + i].params[2] =
			(u32)(&kcm->playback_iacc_ep.interleaving_format);
	}

	for (i = 0; i < 4; i++) {
		components_shared[21 + i].component_id = ENDPOINT_CONFIGURE_REQ;
		components_shared[21 + i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components_shared[21 + i].params[0] =
			(u32)(&components_shared[3].ret[i]);
		components_shared[21 + i].params[1] =
			ENDPOINT_CONF_CLOCK_MASTER;
		components_shared[21 + i].params[2] =
			(u32)(&kcm->playback_iacc_ep.clock_master);
	}

	for (i = 0; i < 4; i++) {
		components_shared[25 + i].component_id = CONNECT_REQ;
		components_shared[25 + i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components_shared[25 + i].params[0] =
			(u32)(&components_shared[4].ret[0]);
		components_shared[25 + i].params[1] = 0x2000 + i;
		components_shared[25 + i].params[2] =
			(u32)(&components_shared[3].ret[i]);
		components_shared[25 + i].params[3] = 0;
	}

	for (i = 0; i < 4; i++) {
		components_shared[29 + i].component_id = CONNECT_REQ;
		components_shared[29 + i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components_shared[29 + i].params[0] =
			(u32)(&components_shared[0].ret[0]);
		components_shared[29 + i].params[1] = 0x2000 + i;
		components_shared[29 + i].params[2] =
			(u32)(&components_shared[4].ret[0]);
		components_shared[29 + i].params[3] = 0xA000 + i*2;
	}

	components_shared[33].component_id = START_OPERATOR_REQ;
	components_shared[33].execute_phase = EXEC_PHASE_TRIGGER_START;
	components_shared[33].params[0] = (u32)(&components_shared[0].ret[0]);
	components_shared[33].params[1] = 1;

	components_shared[34].component_id = START_OPERATOR_REQ;
	components_shared[34].execute_phase = EXEC_PHASE_TRIGGER_START;
	components_shared[34].params[0] = (u32)(&components_shared[4].ret[0]);
	components_shared[34].params[1] = 1;

	components_shared[35].component_id = STOP_OPERATOR_REQ;
	components_shared[35].execute_phase = EXEC_PHASE_HW_FREE;
	components_shared[35].params[0] = (u32)(&components_shared[0].ret[0]);
	components_shared[35].params[1] = 1;

	components_shared[36].component_id = STOP_OPERATOR_REQ;
	components_shared[36].execute_phase = EXEC_PHASE_HW_FREE;
	components_shared[36].params[0] = (u32)(&components_shared[4].ret[0]);
	components_shared[36].params[1] = 1;

	for (i = 0; i < 8; i++) {
		components_shared[37 + i].component_id = DISCONNECT_REQ;
		components_shared[37 + i].execute_phase = EXEC_PHASE_HW_FREE_1;
		components_shared[37 + i].params[0] = 1;
		components_shared[37 + i].params[1] =
			(u32)(&components_shared[25 + i].ret[0]);
	}

	components_shared[45].component_id = CLOSE_SINK_REQ;
	components_shared[45].execute_phase = EXEC_PHASE_HW_FREE_1;
	components_shared[45].params[0] = 4;
	components_shared[45].params[1] = (u32)(components_shared[3].ret);

	components_shared[46].component_id = DESTROY_OPERATOR_REQ;
	components_shared[46].execute_phase = EXEC_PHASE_HW_FREE_1;
	components_shared[46].params[0] = (u32)(&components_shared[0].ret[0]);
	components_shared[46].params[1] = 1;

	components_shared[47].component_id = DESTROY_OPERATOR_REQ;
	components_shared[47].execute_phase = EXEC_PHASE_HW_FREE_1;
	components_shared[47].params[0] = (u32)(&components_shared[4].ret[0]);
	components_shared[47].params[1] = 1;

	shared_components_size = 48;
}

static void init_cvc_shared_components(void)
{
	static u16 cvc_aec_ref_rate[2] = {0x3E80, 0x3E80};
	static u16 cvc_param = 0x4;
	int i;

	components_cvc_shared[0].component_id = CREATE_OPERATOR_REQ;
	components_cvc_shared[0].execute_phase = EXEC_PHASE_HW_PARAMS;
	components_cvc_shared[0].params[0] = CAPABILITY_ID_AEC_REF_1MIC;

	/* Send the sample rate to AEC Ref */
	components_cvc_shared[1].component_id = OPERATOR_MESSAGE_REQ;
	components_cvc_shared[1].execute_phase = EXEC_PHASE_HW_PARAMS;
	components_cvc_shared[1].params[0] =
		(u32)(&components_cvc_shared[0].ret[0]);
	components_cvc_shared[1].params[1] = AEC_REF_SET_SAMPLE_RATES;
	components_cvc_shared[1].params[2] = 2;
	components_cvc_shared[1].params[3] = (u32)(cvc_aec_ref_rate);

	components_cvc_shared[2].component_id = OPERATOR_MESSAGE_REQ;
	components_cvc_shared[2].execute_phase = EXEC_PHASE_HW_PARAMS;
	components_cvc_shared[2].params[0] =
		(u32)(&components_cvc_shared[0].ret[0]);
	components_cvc_shared[2].params[1] = OPERATOR_MSG_SET_CVC_PARAM;
	components_cvc_shared[2].params[2] = 1;
	components_cvc_shared[2].params[3] = (u32)(&cvc_param);

	/* Create the CVC 1MIC */
	components_cvc_shared[3].component_id = CREATE_OPERATOR_REQ;
	components_cvc_shared[3].execute_phase = EXEC_PHASE_HW_PARAMS;
	components_cvc_shared[3].params[0] = CAPABILITY_ID_CVCHF1MIC_SEND_WB;

	components_cvc_shared[4].component_id = OPERATOR_MESSAGE_REQ;
	components_cvc_shared[4].execute_phase = EXEC_PHASE_HW_PARAMS;
	components_cvc_shared[4].params[0] =
		(u32)(&components_cvc_shared[3].ret[0]);
	components_cvc_shared[4].params[1] = OPERATOR_MSG_SET_CVC_PARAM;
	components_cvc_shared[4].params[2] = 1;
	components_cvc_shared[4].params[3] = (u32)(&cvc_param);

	/* Create the CVC 1MIC */
	components_cvc_shared[5].component_id = CREATE_OPERATOR_REQ;
	components_cvc_shared[5].execute_phase = EXEC_PHASE_HW_PARAMS;
	components_cvc_shared[5].params[0] = CAPABILITY_ID_CVC_RCV_WB;

	components_cvc_shared[6].component_id = OPERATOR_MESSAGE_REQ;
	components_cvc_shared[6].execute_phase = EXEC_PHASE_HW_PARAMS;
	components_cvc_shared[6].params[0] =
		(u32)(&components_cvc_shared[5].ret[0]);
	components_cvc_shared[6].params[1] = OPERATOR_MSG_SET_CVC_PARAM;
	components_cvc_shared[6].params[2] = 1;
	components_cvc_shared[6].params[3] = (u32)(&cvc_param);

	/* Connect CVC_RCV Source 0 to AEC_Ref Sink 0  */
	components_cvc_shared[7].component_id = CONNECT_REQ;
	components_cvc_shared[7].execute_phase = EXEC_PHASE_HW_PARAMS;
	components_cvc_shared[7].params[0] =
		(u32)(&components_cvc_shared[5].ret[0]);
	components_cvc_shared[7].params[1] = 0x2000;
	components_cvc_shared[7].params[2] =
		(u32)(&components_cvc_shared[0].ret[0]);
	components_cvc_shared[7].params[3] = 0xA000;

	/* Connect AEC_Ref Source 3 to CVC_Send Sink 1 - MIC */
	components_cvc_shared[8].component_id = CONNECT_REQ;
	components_cvc_shared[8].execute_phase = EXEC_PHASE_HW_PARAMS;
	components_cvc_shared[8].params[0] =
			(u32)(&components_cvc_shared[0].ret[0]);
	components_cvc_shared[8].params[1] = 0x2000 + 3;
	components_cvc_shared[8].params[2] =
			(u32)(&components_cvc_shared[3].ret[0]);
	components_cvc_shared[8].params[3] = 0xA000 + 1;

	/* Connect the AEC_Ref Source 0 to CVC_Send Sink 0 */
	components_cvc_shared[9].component_id = CONNECT_REQ;
	components_cvc_shared[9].execute_phase = EXEC_PHASE_HW_PARAMS;
	components_cvc_shared[9].params[0] =
		(u32)(&components_cvc_shared[0].ret[0]);
	components_cvc_shared[9].params[1] = 0x2000;
	components_cvc_shared[9].params[2] =
		(u32)(&components_cvc_shared[3].ret[0]);
	components_cvc_shared[9].params[3] = 0xA000;

	components_cvc_shared[10].component_id = START_OPERATOR_REQ;
	components_cvc_shared[10].execute_phase = EXEC_PHASE_TRIGGER_START;
	components_cvc_shared[10].params[0] =
		(u32)(&components_cvc_shared[0].ret[0]);
	components_cvc_shared[10].params[1] = 1;

	components_cvc_shared[11].component_id = START_OPERATOR_REQ;
	components_cvc_shared[11].execute_phase = EXEC_PHASE_TRIGGER_START;
	components_cvc_shared[11].params[0] =
		(u32)(&components_cvc_shared[3].ret[0]);
	components_cvc_shared[11].params[1] = 1;

	components_cvc_shared[12].component_id = START_OPERATOR_REQ;
	components_cvc_shared[12].execute_phase = EXEC_PHASE_TRIGGER_START;
	components_cvc_shared[12].params[0] =
		(u32)(&components_cvc_shared[5].ret[0]);
	components_cvc_shared[12].params[1] = 1;

	components_cvc_shared[13].component_id = STOP_OPERATOR_REQ;
	components_cvc_shared[13].execute_phase = EXEC_PHASE_TRIGGER_STOP;
	components_cvc_shared[13].params[0] =
		(u32)(&components_cvc_shared[5].ret[0]);
	components_cvc_shared[13].params[1] = 1;

	components_cvc_shared[14].component_id = STOP_OPERATOR_REQ;
	components_cvc_shared[14].execute_phase = EXEC_PHASE_TRIGGER_STOP;
	components_cvc_shared[14].params[0] =
		(u32)(&components_cvc_shared[3].ret[0]);
	components_cvc_shared[14].params[1] = 1;

	components_cvc_shared[15].component_id = STOP_OPERATOR_REQ;
	components_cvc_shared[15].execute_phase = EXEC_PHASE_TRIGGER_STOP;
	components_cvc_shared[15].params[0] =
		(u32)(&components_cvc_shared[0].ret[0]);
	components_cvc_shared[15].params[1] = 1;

	for (i = 0; i < 3; i++) {
		components_cvc_shared[16 + i].component_id = DISCONNECT_REQ;
		components_cvc_shared[16 + i].execute_phase =
			EXEC_PHASE_HW_FREE_1;
		components_cvc_shared[16 + i].params[0] = 1;
		components_cvc_shared[16 + i].params[1] =
			(u32)(&components_cvc_shared[7 + i].ret[0]);
	}

	components_cvc_shared[19].component_id = DESTROY_OPERATOR_REQ;
	components_cvc_shared[19].execute_phase = EXEC_PHASE_HW_FREE;
	components_cvc_shared[19].params[0] =
			(u32)(&components_cvc_shared[0].ret[0]);
	components_cvc_shared[19].params[1] = 1;

	components_cvc_shared[20].component_id = DESTROY_OPERATOR_REQ;
	components_cvc_shared[20].execute_phase = EXEC_PHASE_HW_FREE;
	components_cvc_shared[20].params[0] =
			(u32)(&components_cvc_shared[3].ret[0]);
	components_cvc_shared[20].params[1] = 1;

	components_cvc_shared[21].component_id = DESTROY_OPERATOR_REQ;
	components_cvc_shared[21].execute_phase = EXEC_PHASE_HW_FREE;
	components_cvc_shared[21].params[0] =
			(u32)(&components_cvc_shared[5].ret[0]);
	components_cvc_shared[21].params[1] = 1;

	cvc_shared_components_size = 22;
}

struct components_chain *create_components_chain(char *stream_name)
{
	struct components_chain *components_chain;

	components_chain = kzalloc(sizeof(*components_chain), GFP_KERNEL);
	if (components_chain == NULL)
		return NULL;

	components_chain->stream_name = kstrdup(stream_name, GFP_KERNEL);
	if (components_chain->stream_name == NULL) {
		kfree(components_chain);
		return NULL;
	}
	list_add(&components_chain->node, &components_chain_list);
	return components_chain;
}

int set_external_param(struct components_chain *components_chain,
	char *key, u32 value)
{
	int i;

	for (i = 0; i < 64; i++) {
		if (components_chain->external_params_map[i].key == NULL)
			continue;
		if (!strncmp(key, components_chain->external_params_map[i].key,
			strlen(key))) {
			components_chain->external_params_map[i].value = value;
			return 0;
		}
	}
	pr_err("Can't find external params of key: %s\n", key);
	return -EINVAL;
}

static int init_sw_external_param(struct components_chain *components_chain)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sw_ep_configure_params_key); i++) {
		components_chain->external_params_map[i].key =
			kstrdup(sw_ep_configure_params_key[i], GFP_KERNEL);
		if (components_chain->external_params_map[i].key == NULL)
			return -ENOMEM;
		components_chain->external_params_map[i].value = 0;
	}
	return 0;
}

int get_external_param_addr(struct components_chain *components_chain,
	char *key, u32 **value)
{
	int i;

	for (i = 0; i < 64; i++) {
		if (!components_chain->external_params_map[i].key)
			break;
		if (strcmp(components_chain->external_params_map[i].key, key))
			continue;
		*value = &(components_chain->external_params_map[i].value);
		return 0;
	}
	pr_err("Can't find external params of key: %s\n", key);
	return -EINVAL;
}

struct components_chain *get_components_chain(const char *stream_name)
{
	struct components_chain *components_chain;

	list_for_each_entry(components_chain, &components_chain_list, node) {
		if (!strncmp(stream_name, components_chain->stream_name,
			strlen(stream_name)))
			return components_chain;
	}
	return NULL;
}

static void hard_code_init_components_chain_voicecall_iacc_to_bt(
	struct components_chain *components_chain)
{
	struct component *components = components_chain->components;
	int i = 0, k;

	components[i].component_id = GET_SOURCE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = ENDPOINT_TYPE_IACC;
	components[i].params[1] = ENDPOINT_PHY_DEV_IACC;
	components[i].params[2] = (u32)(&kcm->capture_iacc_sco_ep.channels);
	components[i].params[3] =
		(u32)(&kcm->capture_iacc_sco_ep.handle_phy_addr);
	components_chain->component_first = &components[i];
	i++;

	components[i].component_id = GET_SINK_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = ENDPOINT_TYPE_USP;
	components[i].params[1] = ENDPOINT_PHY_DEV_A7CA;
	components[i].params[2] = (u32)(&kcm->playback_usp_sco_ep.channels);
	components[i].params[3] =
		(u32)(&kcm->playback_usp_sco_ep.handle_phy_addr);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_AUDIO_SAMPLE_RATE;
	components[i].params[2] =
		(u32)(&kcm->playback_usp_sco_ep.sample_rate);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_AUDIO_DATA_FORMAT;
	components[i].params[2] =
		(u32)(&kcm->playback_usp_sco_ep.audio_data_format);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_DRAM_PACKING_FORMAT;
	components[i].params[2] =
		(u32)(&kcm->playback_usp_sco_ep.packing_format);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_INTERLEAVING_MODE;
	components[i].params[2] =
		(u32)(&kcm->playback_usp_sco_ep.interleaving_format);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_CLOCK_MASTER;
	components[i].params[2] =
		(u32)(&kcm->playback_usp_sco_ep.clock_master);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_AUDIO_SAMPLE_RATE;
	components[i].params[2] =
		(u32)(&kcm->capture_iacc_sco_ep.sample_rate);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_AUDIO_DATA_FORMAT;
	components[i].params[2] =
		(u32)(&kcm->capture_iacc_sco_ep.audio_data_format);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_DRAM_PACKING_FORMAT;
	components[i].params[2] =
		(u32)(&kcm->capture_iacc_sco_ep.packing_format);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_INTERLEAVING_MODE;
	components[i].params[2] =
		(u32)(&kcm->capture_iacc_sco_ep.interleaving_format);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_CLOCK_MASTER;
	components[i].params[2] =
		(u32)(&kcm->capture_iacc_sco_ep.clock_master);
	components[i - 1].component_next = &components[i];
	i++;

	/* Connect USP-RX with CVC_SEND Source 0*/
	components[i].component_id = CONNECT_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components_cvc_shared[3].ret[0]);
	components[i].params[1] = 0x2000;
	components[i].params[2] = (u32)(&components[1].ret[0]);
	components[i].params[3] = 0x0;
	components[i - 1].component_next = &components[i];
	i++;

	/* Connect "mic" to AEC_REf Sink 2 */
	components[i].component_id = CONNECT_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i].params[1] = 0x0;
	components[i].params[2] = (u32)(&components_cvc_shared[0].ret[0]);
	components[i].params[3] = 0xA000 + 2;
	components[i - 1].component_next = &components[i];
	i++;

	/* Disconnect*/
	for (k = 0; k < 2; k++) {
		components[i].component_id = DISCONNECT_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_FREE;
		components[i].params[0] = 1;
		components[i].params[1] = (u32)(&components[12 + k].ret[0]);
		components[i - 1].component_next = &components[i];
		i++;
	}

	components[i].component_id = CLOSE_SOURCE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = 1;
	components[i].params[1] = (u32)(components[0].ret);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = CLOSE_SINK_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = 1;
	components[i].params[1] = (u32)(components[1].ret);
	components[i - 1].component_next = &components[i];
	i++;

	components[i - 1].component_next = NULL;
}

static void hard_code_init_components_chain_voicecall_bt_to_iacc(
	struct components_chain *components_chain)
{
	struct component *components = components_chain->components;
	int i = 0, k;

	components[i].component_id = GET_SOURCE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = ENDPOINT_TYPE_USP;
	components[i].params[1] = ENDPOINT_PHY_DEV_A7CA;
	components[i].params[2] = (u32)(&kcm->capture_usp_sco_ep.channels);
	components[i].params[3] =
		(u32)(&kcm->capture_usp_sco_ep.handle_phy_addr);
	components_chain->component_first = &components[i];
	i++;

	components[i].component_id = GET_SINK_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = ENDPOINT_TYPE_IACC;
	components[i].params[1] = ENDPOINT_PHY_DEV_IACC;
	components[i].params[2] = (u32)(&kcm->playback_iacc_sco_ep.channels);
	components[i].params[3] =
		(u32)(&kcm->playback_iacc_sco_ep.handle_phy_addr);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_AUDIO_SAMPLE_RATE;
	components[i].params[2] =
		(u32)(&kcm->playback_iacc_sco_ep.sample_rate);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_AUDIO_DATA_FORMAT;
	components[i].params[2] =
		(u32)(&kcm->playback_iacc_sco_ep.audio_data_format);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_DRAM_PACKING_FORMAT;
	components[i].params[2] =
		(u32)(&kcm->playback_iacc_sco_ep.packing_format);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_INTERLEAVING_MODE;
	components[i].params[2] =
		(u32)(&kcm->playback_iacc_sco_ep.interleaving_format);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_CLOCK_MASTER;
	components[i].params[2] =
		(u32)(&kcm->playback_iacc_sco_ep.clock_master);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_AUDIO_SAMPLE_RATE;
	components[i].params[2] =
		(u32)(&kcm->capture_usp_sco_ep.sample_rate);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_AUDIO_DATA_FORMAT;
	components[i].params[2] =
		(u32)(&kcm->capture_usp_sco_ep.audio_data_format);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_DRAM_PACKING_FORMAT;
	components[i].params[2] =
		(u32)(&kcm->capture_usp_sco_ep.packing_format);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_INTERLEAVING_MODE;
	components[i].params[2] =
		(u32)(&kcm->capture_usp_sco_ep.interleaving_format);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_CLOCK_MASTER;
	components[i].params[2] =
		(u32)(&kcm->capture_usp_sco_ep.clock_master);
	components[i - 1].component_next = &components[i];
	i++;

	/* Connect USP-TX to cvc rcv sink 0  */ /* i == 7 */
	components[i].component_id = CONNECT_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i].params[1] = 0;
	components[i].params[2] = (u32)(&components_cvc_shared[5].ret[0]);
	components[i].params[3] = 0xA000;
	components[i - 1].component_next = &components[i];
	i++;

	/* Connect AEC_Ref Source 1 to "Speaker"  */
	components[i].component_id = CONNECT_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components_cvc_shared[0].ret[0]);
	components[i].params[1] = 0x2000 + 1;
	components[i].params[2] = (u32)(&components[1].ret[0]);
	components[i].params[3] = 0x0;
	components[i - 1].component_next = &components[i];
	i++;

	/* Disconnect*/
	for (k = 0; k < 2; k++) {
		components[i].component_id = DISCONNECT_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_FREE;
		components[i].params[0] = 1;
		components[i].params[1] = (u32)(&components[12 + k].ret[0]);
		components[i - 1].component_next = &components[i];
		i++;
	}

	components[i].component_id = CLOSE_SOURCE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = 1;
	components[i].params[1] = (u32)(components[0].ret);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = CLOSE_SINK_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = 1;
	components[i].params[1] = (u32)(components[1].ret);
	components[i - 1].component_next = &components[i];
	i++;

	components[i - 1].component_next = NULL;
}

static void hard_code_init_components_chain_a2dp_playback(
	struct components_chain *components_chain)
{
	struct component *components = components_chain->components;
	int i = 0, k;

	components[i].component_id = CREATE_OPERATOR_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = CAPABILITY_ID_SPLITTER;
	components_chain->component_first = &components[i];
	i++;

	components[i].component_id = GET_SOURCE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = ENDPOINT_TYPE_USP;
	components[i].params[1] = ENDPOINT_PHY_DEV_A7CA;
	components[i].params[2] = (u32)(&kcm->capture_usp_a2dp_ep.channels);
	components[i].params[3] =
		(u32)(&kcm->capture_usp_a2dp_ep.handle_phy_addr);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = GET_SINK_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = ENDPOINT_TYPE_IACC;
	components[i].params[1] = ENDPOINT_PHY_DEV_IACC;
	components[i].params[2] = (u32)(&kcm->playback_iacc_ep.channels);
	components[i].params[3] =
		(u32)(&kcm->playback_iacc_ep.handle_phy_addr);
	components[i - 1].component_next = &components[i];
	i++;

	/* i == 3 */
	for (k = 0; k < 2; k++) {
		components[i].component_id = ENDPOINT_CONFIGURE_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32)(&components[1].ret[k]);
		components[i].params[1] = ENDPOINT_CONF_AUDIO_SAMPLE_RATE;
		components[i].params[2] =
			(u32)(&kcm->capture_usp_a2dp_ep.sample_rate);
		components[i - 1].component_next = &components[i];
		i++;
	}

	for (k = 0; k < 2; k++) {
		components[i].component_id = ENDPOINT_CONFIGURE_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32)(&components[1].ret[k]);
		components[i].params[1] = ENDPOINT_CONF_AUDIO_DATA_FORMAT;
		components[i].params[2] =
			(u32)(&kcm->capture_usp_a2dp_ep.audio_data_format);
		components[i - 1].component_next = &components[i];
		i++;
	}

	for (k = 0; k < 2; k++) {
		components[i].component_id = ENDPOINT_CONFIGURE_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32)(&components[1].ret[k]);
		components[i].params[1] = ENDPOINT_CONF_DRAM_PACKING_FORMAT;
		components[i].params[2] =
			(u32)(&kcm->capture_usp_a2dp_ep.packing_format);
		components[i - 1].component_next = &components[i];
		i++;
	}

	for (k = 0; k < 2; k++) {
		components[i].component_id = ENDPOINT_CONFIGURE_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32)(&components[1].ret[k]);
		components[i].params[1] = ENDPOINT_CONF_INTERLEAVING_MODE;
		components[i].params[2] =
			(u32)(&kcm->capture_usp_a2dp_ep.interleaving_format);
		components[i - 1].component_next = &components[i];
		i++;
	}

	for (k = 0; k < 2; k++) {
		components[i].component_id = ENDPOINT_CONFIGURE_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32)(&components[1].ret[k]);
		components[i].params[1] = ENDPOINT_CONF_CLOCK_MASTER;
		components[i].params[2] =
			(u32)(&kcm->capture_usp_a2dp_ep.clock_master);
		components[i - 1].component_next = &components[i];
		i++;
	}

	for (k = 0; k < 4; k++) {
		components[i].component_id = ENDPOINT_CONFIGURE_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32)(&components[2].ret[k]);
		components[i].params[1] = ENDPOINT_CONF_AUDIO_SAMPLE_RATE;
		components[i].params[2] =
			(u32)(&kcm->playback_iacc_ep.sample_rate);
		components[i - 1].component_next = &components[i];
		i++;
	}

	for (k = 0; k < 4; k++) {
		components[i].component_id = ENDPOINT_CONFIGURE_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32)(&components[2].ret[k]);
		components[i].params[1] = ENDPOINT_CONF_AUDIO_DATA_FORMAT;
		components[i].params[2] =
			(u32)(&kcm->playback_iacc_ep.audio_data_format);
		components[i - 1].component_next = &components[i];
		i++;
	}

	for (k = 0; k < 4; k++) {
		components[i].component_id = ENDPOINT_CONFIGURE_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32)(&components[2].ret[k]);
		components[i].params[1] = ENDPOINT_CONF_DRAM_PACKING_FORMAT;
		components[i].params[2] =
			(u32)(&kcm->playback_iacc_ep.packing_format);
		components[i - 1].component_next = &components[i];
		i++;
	}

	for (k = 0; k < 4; k++) {
		components[i].component_id = ENDPOINT_CONFIGURE_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32)(&components[2].ret[k]);
		components[i].params[1] = ENDPOINT_CONF_INTERLEAVING_MODE;
		components[i].params[2] =
			(u32)(&kcm->playback_iacc_ep.interleaving_format);
		components[i - 1].component_next = &components[i];
		i++;
	}

	for (k = 0; k < 4; k++) {
		components[i].component_id = ENDPOINT_CONFIGURE_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32)(&components[2].ret[k]);
		components[i].params[1] = ENDPOINT_CONF_CLOCK_MASTER;
		components[i].params[2] =
			(u32)(&kcm->playback_iacc_ep.clock_master);
		components[i - 1].component_next = &components[i];
		i++;
	}

	/* i == 33 */
	for (k = 0; k < 2; k++) {
		components[i].component_id = CONNECT_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32)(&components[1].ret[k]);
		components[i].params[1] = 0;
		components[i].params[2] = (u32)(&components[0].ret[0]);
		components[i].params[3] = 0xA000 + k;
		components[i - 1].component_next = &components[i];
		i++;
	}

	for (k = 0; k < 4; k++) {
		components[i].component_id = CONNECT_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32)(&components[0].ret[0]);
		components[i].params[1] = 0x2000 + k;
		components[i].params[2] = (u32)(&components[2].ret[k]);
		components[i].params[3] = 0;
		components[i - 1].component_next = &components[i];
		i++;
	}

	components[i].component_id = START_OPERATOR_REQ;
	components[i].execute_phase = EXEC_PHASE_TRIGGER_START;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i].params[1] = 1;
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = STOP_OPERATOR_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i].params[1] = 1;
	components[i - 1].component_next = &components[i];
	i++;

	for (k = 0; k < 6; k++) {
		components[i].component_id = DISCONNECT_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_FREE;
		components[i].params[0] = 1;
		components[i].params[1] = (u32)(&components[33 + k].ret[0]);
		components[i - 1].component_next = &components[i];
		i++;
	}

	components[i].component_id = CLOSE_SINK_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = kcm->playback_iacc_ep.channels;
	components[i].params[1] = (u32)(components[2].ret);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = CLOSE_SOURCE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = kcm->capture_usp_a2dp_ep.channels;
	components[i].params[1] = (u32)(components[1].ret);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = DESTROY_OPERATOR_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i].params[1] = 1;
	components[i - 1].component_next = &components[i];
	i++;

	components[i - 1].component_next = NULL;
}

static void hard_code_init_components_chain_navigation_playback(
	struct components_chain *components_chain)
{
	struct component *components = components_chain->components;
	int i, j, k;
	u32 *value;
	static u16 mixer_oper_conf_primary_stream = 0x2;
	static u16 resampler_oper_conf_conversion_rate = 0x38;

	i = 0;

	components[i].component_id = GET_SOURCE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = ENDPOINT_TYPE_FILE;
	components[i].params[1] = 0;
	get_external_param_addr(components_chain, "sw_ep_channles", &value);
	components[i].params[2] = (u32)value;
	get_external_param_addr(components_chain, "sw_ep_handle_addr", &value);
	components[i].params[3] = (u32)value;
	components_chain->component_first = &components[i];
	i++;

	components[i].component_id = CREATE_OPERATOR_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = CAPABILITY_ID_RESAMPLER;
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = OPERATOR_MESSAGE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = RESAMPLER_SET_CONVERSION_RATE;
	components[i].params[2] = 1;
	components[i].params[3] = (u32)(&resampler_oper_conf_conversion_rate);
	components[i - 1].component_next = &components[i];
	i++;

	/* i == 3 */
	for (k = 0; k < 4; k++) {
		for (j = 0; j < 6; j++) {
			components[i].component_id = ENDPOINT_CONFIGURE_REQ;
			components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
			components[i].params[0] =
				(u32)(&components[0].ret[0]);
			components[i].params[1] = (u32)(ep_configure_key[j]);
			get_external_param_addr(components_chain,
					sw_ep_configure_params_key[j], &value);
			components[i].params[2] = (u32)value;
			components[i - 1].component_next = &components[i];
			i++;
		}
	}

	/* i == 27 */
	for (k = 0; k < 4; k++) {
		components[i].component_id = CONNECT_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32)(&components[0].ret[k]);
		components[i].params[1] = 0;
		components[i].params[2] = (u32)(&components[1].ret[0]);
		components[i].params[3] = 0xA000 + k;
		components[i - 1].component_next = &components[i];
		i++;
	}

	for (k = 0; k < 4; k++) {
		components[i].component_id = CONNECT_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32)(&components[1].ret[0]);
		components[i].params[1] = 0x2000 + k;
		components[i].params[2] = (u32)(&components_shared[0].ret[0]);
		components[i].params[3] = 0xA000 + 4 + k;
		components[i - 1].component_next = &components[i];
		i++;
	}

	components[i].component_id = OPERATOR_MESSAGE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components_shared[0].ret[0]);
	components[i].params[1] = OPERATOR_MSG_SET_PRIMARY_STREAM;
	components[i].params[2] = 1;
	components[i].params[3] = (u32)(&mixer_oper_conf_primary_stream);
	components[i].params[4] = SET_PRIMARY_STREAM;
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = START_OPERATOR_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = 1;
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = DATA_PRODUCED;
	components[i].execute_phase = EXEC_PHASE_TRIGGER_START;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = DATA_PRODUCED;
	components[i].execute_phase = EXEC_PHASE_ACK;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = STOP_OPERATOR_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = 1;
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = OPERATOR_MESSAGE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = (u32)(&components_shared[0].ret[0]);
	components[i].params[1] = OPERATOR_MSG_SET_PRIMARY_STREAM;
	components[i].params[2] = 1;
	components[i].params[3] = (u32)(&mixer_oper_conf_primary_stream);
	components[i].params[4] = CLEAR_PRIMARY_STREAM;
	components[i - 1].component_next = &components[i];
	i++;

	for (k = 0; k < 8; k++) {
		components[i].component_id = DISCONNECT_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_FREE;
		components[i].params[0] = 1;
		components[i].params[1] = (u32)(&components[27 + k].ret[0]);
		components[i - 1].component_next = &components[i];
		i++;
	}

	components[i].component_id = CLOSE_SOURCE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = 4;
	components[i].params[1] = (u32)(components[0].ret);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = DESTROY_OPERATOR_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = 1;
	components[i - 1].component_next = &components[i];
	i++;

	components[i - 1].component_next = NULL;

	components_chain->notify_ep_id = &components[0].ret[0];
}

static void hard_code_init_components_chain_alarm_playback(
	struct components_chain *components_chain)
{
	struct component *components = components_chain->components;
	int i, j, k;
	u32 *value;
	static u16 mixer_oper_conf_primary_stream = 0x3;
	static u16 resampler_oper_conf_conversion_rate = 0x38;

	i = 0;

	components[i].component_id = GET_SOURCE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = ENDPOINT_TYPE_FILE;
	components[i].params[1] = 0;
	get_external_param_addr(components_chain, "sw_ep_channles", &value);
	components[i].params[2] = (u32)value;
	get_external_param_addr(components_chain, "sw_ep_handle_addr", &value);
	components[i].params[3] = (u32)value;
	components_chain->component_first = &components[i];
	i++;

	components[i].component_id = CREATE_OPERATOR_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = CAPABILITY_ID_RESAMPLER;
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = OPERATOR_MESSAGE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = RESAMPLER_SET_CONVERSION_RATE;
	components[i].params[2] = 1;
	components[i].params[3] = (u32)(&resampler_oper_conf_conversion_rate);
	components[i - 1].component_next = &components[i];
	i++;

	/* i == 3 */
	for (k = 0; k < 4; k++) {
		for (j = 0; j < 6; j++) {
			components[i].component_id = ENDPOINT_CONFIGURE_REQ;
			components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
			components[i].params[0] =
				(u32)(&components[0].ret[0]);
			components[i].params[1] = (u32)(ep_configure_key[j]);
			get_external_param_addr(components_chain,
					sw_ep_configure_params_key[j], &value);
			components[i].params[2] = (u32)value;
			components[i - 1].component_next = &components[i];
			i++;
		}
	}

	/* i == 27 */
	for (k = 0; k < 4; k++) {
		components[i].component_id = CONNECT_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32)(&components[0].ret[k]);
		components[i].params[1] = 0;
		components[i].params[2] = (u32)(&components[1].ret[0]);
		components[i].params[3] = 0xA000 + k;
		components[i - 1].component_next = &components[i];
		i++;
	}

	for (k = 0; k < 4; k++) {
		components[i].component_id = CONNECT_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32)(&components[1].ret[0]);
		components[i].params[1] = 0x2000 + k;
		components[i].params[2] = (u32)(&components_shared[0].ret[0]);
		components[i].params[3] = 0xA000 + 8 + k;
		components[i - 1].component_next = &components[i];
		i++;
	}

	components[i].component_id = OPERATOR_MESSAGE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components_shared[0].ret[0]);
	components[i].params[1] = OPERATOR_MSG_SET_PRIMARY_STREAM;
	components[i].params[2] = 1;
	components[i].params[3] = (u32)(&mixer_oper_conf_primary_stream);
	components[i].params[4] = SET_PRIMARY_STREAM;
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = START_OPERATOR_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = 1;
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = DATA_PRODUCED;
	components[i].execute_phase = EXEC_PHASE_TRIGGER_START;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = DATA_PRODUCED;
	components[i].execute_phase = EXEC_PHASE_ACK;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = STOP_OPERATOR_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = 1;
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = OPERATOR_MESSAGE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = (u32)(&components_shared[0].ret[0]);
	components[i].params[1] = OPERATOR_MSG_SET_PRIMARY_STREAM;
	components[i].params[2] = 1;
	components[i].params[3] = (u32)(&mixer_oper_conf_primary_stream);
	components[i].params[4] = CLEAR_PRIMARY_STREAM;
	components[i - 1].component_next = &components[i];
	i++;

	for (k = 0; k < 8; k++) {
		components[i].component_id = DISCONNECT_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_FREE;
		components[i].params[0] = 1;
		components[i].params[1] = (u32)(&components[27 + k].ret[0]);
		components[i - 1].component_next = &components[i];
		i++;
	}

	components[i].component_id = CLOSE_SOURCE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = 4;
	components[i].params[1] = (u32)(components[0].ret);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = DESTROY_OPERATOR_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = 1;
	components[i - 1].component_next = &components[i];
	i++;

	components[i - 1].component_next = NULL;

	components_chain->notify_ep_id = &components[0].ret[0];
}

static void hard_code_init_components_chain_music_playback(
	struct components_chain *components_chain)
{
	struct component *components = components_chain->components;
	int i, j, k;
	u32 *value;
	static u16 mixer_oper_conf_primary_stream = 0x1;

	i = 0;

	components[i].component_id = GET_SOURCE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = ENDPOINT_TYPE_FILE;
	components[i].params[1] = 0;
	get_external_param_addr(components_chain, "sw_ep_channles", &value);
	components[i].params[2] = (u32)value;
	get_external_param_addr(components_chain, "sw_ep_handle_addr", &value);
	components[i].params[3] = (u32)value;
	components_chain->component_first = &components[i];
	i++;

	/* peq create */
	for (k = 0; k < 5; k++) {
		components[i].component_id = CREATE_OPERATOR_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = CAPABILITY_ID_PEQ;
		components[i - 1].component_next = &components[i];
		/* Init the peq operator id pointer */
		peq_op_id[k] = &(components[i].ret[0]);
		i++;
	}

	/* i == 6 */
	for (k = 0; k < 4; k++) {
		for (j = 0; j < 6; j++) {
			components[i].component_id = ENDPOINT_CONFIGURE_REQ;
			components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
			components[i].params[0] = (u32)(&components[0].ret[k]);
			components[i].params[1] = (u32)(ep_configure_key[j]);
			get_external_param_addr(components_chain,
				sw_ep_configure_params_key[j], &value);
			components[i].params[2] = (u32)value;
			components[i - 1].component_next = &components[i];
			i++;
		}
	}

	/* peq config */
	for (k = 0; k < 5; k++) {
		/* init PEQ sample rate */
		components[i].component_id = OPERATOR_MESSAGE_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32)(&components[1 + k].ret[0]);
		components[i].params[1] = OPMSG_COMMON_SET_SAMPLE_RATE;
		components[i].params[2] = 1;
		components[i].params[3] = (u32)(&peq_sample_rate);
		components[i - 1].component_next = &components[i];
		i++;
	}

	/* i == 35 */
	for (k = 0; k < 4; k++) {
		components[i].component_id = CONNECT_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32)(&components[0].ret[k]);
		components[i].params[1] = 0;
		components[i].params[2] = (u32)(&components[1].ret[0]);
		components[i].params[3] = 0xA000 + k;
		components[i - 1].component_next = &components[i];
		i++;
	}

	/* peq connect */
	for (k = 0; k < 4; k++) {
		components[i].component_id = CONNECT_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32)(&components[1].ret[0]);
		components[i].params[1] = 0x2000 + k;
		components[i].params[2] = (u32)(&components[2 + k].ret[0]);
		components[i].params[3] = 0xA000;
		components[i - 1].component_next = &components[i];
		i++;
	}

	for (k = 0; k < 4; k++) {
		components[i].component_id = CONNECT_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32)(&components[2 + k].ret[0]);
		components[i].params[1] = 0x2000;
		components[i].params[2] = (u32)(&components_shared[0].ret[0]);
		components[i].params[3] = 0xA000 + k;
		components[i - 1].component_next = &components[i];
		i++;
	}

	components[i].component_id = OPERATOR_MESSAGE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components_shared[0].ret[0]);
	components[i].params[1] = OPERATOR_MSG_SET_PRIMARY_STREAM;
	components[i].params[2] = 1;
	components[i].params[3] = (u32)(&mixer_oper_conf_primary_stream);
	components[i].params[4] = SET_PRIMARY_STREAM;
	components[i - 1].component_next = &components[i];
	i++;

	/* peq start */
	for (k = 0; k < 5; k++) {
		components[i].component_id = START_OPERATOR_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32)(&components[1 + k].ret[0]);
		components[i].params[1] = 1;
		components[i - 1].component_next = &components[i];
		i++;
	}

	components[i].component_id = DATA_PRODUCED;
	components[i].execute_phase = EXEC_PHASE_TRIGGER_START;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = DATA_PRODUCED;
	components[i].execute_phase = EXEC_PHASE_ACK;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i - 1].component_next = &components[i];
	i++;

	/* peq stop */
	for (k = 0; k < 5; k++) {
		components[i].component_id = STOP_OPERATOR_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_FREE;
		components[i].params[0] = (u32)(&components[1 + k].ret[0]);
		components[i].params[1] = 1;
		components[i - 1].component_next = &components[i];
		i++;
	}

	components[i].component_id = OPERATOR_MESSAGE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = (u32)(&components_shared[0].ret[0]);
	components[i].params[1] = OPERATOR_MSG_SET_PRIMARY_STREAM;
	components[i].params[2] = 1;
	components[i].params[3] = (u32)(&mixer_oper_conf_primary_stream);
	components[i].params[4] = CLEAR_PRIMARY_STREAM;
	components[i - 1].component_next = &components[i];
	i++;

	for (k = 0; k < 12; k++) {
		components[i].component_id = DISCONNECT_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_FREE;
		components[i].params[0] = 1;
		components[i].params[1] = (u32)(&components[k + 35].ret[0]);
		components[i - 1].component_next = &components[i];
		i++;
	}

	components[i].component_id = CLOSE_SOURCE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = 4;
	components[i].params[1] = (u32)(components[0].ret);
	components[i - 1].component_next = &components[i];
	i++;

	/* peq stop */
	for (k = 0; k < 5; k++) {
		components[i].component_id = DESTROY_OPERATOR_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_FREE;
		components[i].params[0] = (u32)(&components[1 + k].ret[0]);
		components[i].params[1] = 1;
		components[i - 1].component_next = &components[i];
		i++;
	}

	components[i - 1].component_next = NULL;

	components_chain->notify_ep_id = &components[0].ret[0];
}

static void hard_code_init_components_chain_capture(
	struct components_chain *components_chain)
{
	struct component *components = components_chain->components;
	int i = 0, j;
	u32 *value;

	components[i].component_id = CREATE_OPERATOR_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = CAPABILITY_ID_BASIC_PASSTHOUGH;
	components_chain->component_first = &components[i];
	i++;

	components[i].component_id = GET_SOURCE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = ENDPOINT_TYPE_IACC;
	components[i].params[1] = ENDPOINT_PHY_DEV_IACC;
	components[i].params[2] = (u32)(&kcm->capture_iacc_ep.channels);
	components[i].params[3] =
		(u32)(&kcm->capture_iacc_ep.handle_phy_addr);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = GET_SINK_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = ENDPOINT_TYPE_FILE;
	components[i].params[1] = 0;
	get_external_param_addr(components_chain, "sw_ep_channles", &value);
	components[i].params[2] = (u32)value;
	get_external_param_addr(components_chain, "sw_ep_handle_addr", &value);
	components[i].params[3] = (u32)value;
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i] .execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_AUDIO_SAMPLE_RATE;
	components[i].params[2] = (u32)(&kcm->capture_iacc_ep.sample_rate);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i] .execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_AUDIO_DATA_FORMAT;
	components[i].params[2] =
		(u32)(&kcm->capture_iacc_ep.audio_data_format);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i] .execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_DRAM_PACKING_FORMAT;
	components[i].params[2] = (u32)(&kcm->capture_iacc_ep.packing_format);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i] .execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_INTERLEAVING_MODE;
	components[i].params[2] =
		(u32)(&kcm->capture_iacc_ep.interleaving_format);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i] .execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_CLOCK_MASTER;
	components[i].params[2] = (u32)(&kcm->capture_iacc_ep.clock_master);
	components[i - 1].component_next = &components[i];
	i++;

	for (j = 0; j < 6; j++) {
		components[i].component_id = ENDPOINT_CONFIGURE_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32)(&components[2].ret[0]);
		components[i].params[1] = (u32)(ep_configure_key[j]);
		get_external_param_addr(components_chain,
				sw_ep_configure_params_key[j], &value);
		components[i].params[2] = (u32)value;
		components[i - 1].component_next = &components[i];
		i++;
	}

	/* i == 14 */
	components[i].component_id = CONNECT_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i].params[1] = 0x2000;
	components[i].params[2] = (u32)(&components[2].ret[0]);
	components[i].params[3] = 0;
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = CONNECT_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = 0;
	components[i].params[2] = (u32)(&components[0].ret[0]);
	components[i].params[3] = 0xA000;
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = START_OPERATOR_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i].params[1] = 1;
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = STOP_OPERATOR_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i].params[1] = 1;
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = DISCONNECT_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = 1;
	components[i].params[1] = (u32)(&components[14].ret[0]);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = DISCONNECT_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = 1;
	components[i].params[1] = (u32)(&components[15].ret[0]);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = CLOSE_SINK_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = 1;
	components[i].params[1] = (u32)(components[2].ret);
	components[i - 1].component_next = &components[i];
	i++;
	components[i].component_id = CLOSE_SOURCE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = 1;
	components[i].params[1] = (u32)(components[1].ret);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = DESTROY_OPERATOR_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = (u32)(&components[0].ret[0]);
	components[i].params[1] = 1;
	components[i - 1].component_next = &components[i];
	i++;

	components[i - 1].component_next = NULL;
	components_chain->notify_ep_id = &components[2].ret[0];
}

u16 get_notify_ep_id(struct components_chain *components_chain)
{
	return *(components_chain->notify_ep_id);
}

u16 get_volume_control_op_id(void)
{
	return *volume_control_op_id;
}

u16 get_mixer_op_id(void)
{
	return *mixer_op_id;
}

u16 get_peq_op_id(u16 index)
{
	return *peq_op_id[index];
}


int execute_component(struct component *component)
{
	int ret = 0;
	u16 primary_stream = 0;
	u16 resp[64];

	kalimba_msg_send_lock();

	switch (component->component_id) {
	case CREATE_OPERATOR_REQ:
		kalimba_create_operator((u16)(component->params[0]),
			component->ret, resp);
		if ((u16)(component->params[0]) == CAPABILITY_ID_MIXER)
			curr_primary_stream = 0;
		break;
	case CREATE_OPERATOR_EXTENDED_REQ:
		kalimba_create_operator_extended((u16)(component->params[0]),
			(u16)(component->params[1]),
			(u16 *)(component->params[2]),
			component->ret, resp);
		break;
	case OPERATOR_MESSAGE_REQ:
		if ((u16)(component->params[1]) !=
				OPERATOR_MSG_SET_PRIMARY_STREAM) {
			ret = kalimba_operator_message(
				*((u16 *)(component->params[0])),
				(u16)(component->params[1]),
				(u16)(component->params[2]),
				(u16 *)(component->params[3]),
				NULL, NULL, resp);
			break;
		}
		if ((u32)(component->params[4]) == SET_PRIMARY_STREAM)
			set_bit(*(u16 *)(component->params[3]) - 1,
				&active_stream);
		else if ((u32)(component->params[4]) ==
				CLEAR_PRIMARY_STREAM)
			clear_bit(*(u16 *)(component->params[3]) - 1,
				&active_stream);
		if (test_bit(1, &active_stream))
			primary_stream = 2;
		else if (test_bit(0, &active_stream))
			primary_stream = 1;
		else if (test_bit(2, &active_stream))
			primary_stream = 3;
		if (curr_primary_stream == primary_stream)
			break;
		curr_primary_stream = primary_stream;
		if (primary_stream == 0)
			break;
		ret = kalimba_operator_message(
				*((u16 *)(component->params[0])),
				(u16)(component->params[1]),
				(u16)(component->params[2]),
				&primary_stream,
				NULL, NULL, resp);
		break;
	case GET_SINK_REQ:
		kalimba_get_sink((u16)(component->params[0]),
			(u16)(component->params[1]),
			*((u16 *)(component->params[2])),
			*((u32 *)(component->params[3])),
			component->ret, resp);
		break;
	case GET_SOURCE_REQ:
		kalimba_get_source((u16)(component->params[0]),
			(u16)(component->params[1]),
			*((u16 *)(component->params[2])),
			*((u32 *)(component->params[3])),
			component->ret, resp);
		break;
	case ENDPOINT_CONFIGURE_REQ:
		kalimba_config_endpoint(*((u16 *)(component->params[0])),
			(u16)(component->params[1]),
			*((u32 *)(component->params[2])), resp);
		break;
	case CONNECT_REQ:
		kalimba_connect_endpoints(*((u16 *)(component->params[0]))
			+ (u16)(component->params[1]),
			*((u16 *)(component->params[2])) +
			(u16)(component->params[3]),
			component->ret, resp);
		break;
	case START_OPERATOR_REQ:
		ret = kalimba_start_operator((u16 *)(component->params[0]),
			(u16)(component->params[1]), resp);
		break;
	case DATA_PRODUCED:
		kalimba_data_produced(*((u16 *)(component->params[0])));
		ret = 0;
		break;
	case STOP_OPERATOR_REQ:
		ret = kalimba_stop_operator((u16 *)(component->params[0]),
			(u16)(component->params[1]), resp);
		break;
	case DISCONNECT_REQ:
		ret = kalimba_disconnect_endpoints((u16)(component->params[0]),
			(u16 *)(component->params[1]), resp);
		break;
	case CLOSE_SINK_REQ:
		ret = kalimba_close_sink((u16)(component->params[0]),
			(u16 *)(component->params[1]), resp);
		break;
	case CLOSE_SOURCE_REQ:
		ret = kalimba_close_source((u16)(component->params[0]),
			(u16 *)(component->params[1]), resp);
		break;
	case DESTROY_OPERATOR_REQ:
		ret = kalimba_destroy_operator((u16 *)(component->params[0]),
			(u16)(component->params[1]), resp);
		/* Clear operator id, if this operator is destoried */
		*((u16 *)(component->params[0])) = 0;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret != 0)
		pr_err("ipc command executed failed: command id: 0x%04x\n",
			component->component_id);
	kalimba_msg_send_unlock();

	return ret;
}

struct component *get_data_produced_ack_component(
	struct components_chain *components_chain)
{
	struct component *component;

	if (components_chain->component_first == NULL)
		return NULL;

	for (component = components_chain->component_first;
		component != NULL; component = component->component_next) {
		if (component->execute_phase == EXEC_PHASE_ACK &&
			component->component_id == DATA_PRODUCED)
			return component;
	}

	return NULL;
}

static int __execute_shared_components(struct component *components,
		int components_count, u32 exec_phase)
{
	int i;
	int ret = 0;

	for (i = 0; i < components_count; i++) {
		if (components[i].execute_phase != exec_phase)
			continue;
		ret = execute_component(&components[i]);
		if (ret < 0)
			break;
	}
	return 0;
}

int execute_global_shared_components(u32 exec_phase)
{
	return __execute_shared_components(components_shared,
		shared_components_size, exec_phase);
}

int execute_cvc_shared_components(u32 exec_phase)
{
	return __execute_shared_components(components_cvc_shared,
		cvc_shared_components_size, exec_phase);
}

int execute_components_chain(struct components_chain *components_chain,
	u32 exec_phase)
{
	struct component *component;
	int ret = 0;

	if (components_chain->component_first == NULL)
		return -ENODEV;
	for (component = components_chain->component_first;
		component != NULL; component = component->component_next) {
		if (component->execute_phase != exec_phase)
			continue;
		ret = execute_component(component);
		if (ret < 0)
			break;
	}
	return 0;
}

static int alloc_hw_ep_handle_and_buff(struct device *dev,
	struct hw_ep_handle_buff_t *hw_ep_handle_buff,
	int buff_bytes_each_channel, int channels, int rate)
{
	hw_ep_handle_buff->audio_data_format = 0;
	hw_ep_handle_buff->packing_format = 2;
	hw_ep_handle_buff->interleaving_format = 1;
	hw_ep_handle_buff->clock_master = 1;
	hw_ep_handle_buff->sample_rate = rate;
	hw_ep_handle_buff->channels = channels;

	hw_ep_handle_buff->buff_bytes = buff_bytes_each_channel * channels;

	hw_ep_handle_buff->handle = dma_alloc_coherent(dev,
		sizeof(struct endpoint_handle),
		&hw_ep_handle_buff->handle_phy_addr, GFP_KERNEL);
	if (hw_ep_handle_buff->handle == NULL) {
		pr_err("Can't allocate playback hw endpoint handle buffer.\n");
		return -ENOMEM;
	}

	hw_ep_handle_buff->buff = dma_alloc_coherent(dev,
		hw_ep_handle_buff->buff_bytes,
		&hw_ep_handle_buff->handle->buff_addr, GFP_KERNEL);
	if (hw_ep_handle_buff->buff == NULL) {
		pr_err("Can't allocate playback hw endpoint buffer.\n");
		dma_free_coherent(dev, sizeof(struct endpoint_handle),
			hw_ep_handle_buff->handle,
			hw_ep_handle_buff->handle_phy_addr);
		return -ENOMEM;
	}
	hw_ep_handle_buff->handle->buff_length =
		hw_ep_handle_buff->buff_bytes / sizeof(u32);
	return 0;
}

static void free_hw_ep_handle_and_buff(struct device *dev,
	struct hw_ep_handle_buff_t *hw_ep_handle_buff)
{
	dma_free_coherent(dev, hw_ep_handle_buff->buff_bytes,
		hw_ep_handle_buff->buff, hw_ep_handle_buff->handle->buff_addr);
	dma_free_coherent(dev, sizeof(struct endpoint_handle),
		hw_ep_handle_buff->handle, hw_ep_handle_buff->handle_phy_addr);
}

struct kcm_t *kcm_init(struct device *dev)
{
	struct components_chain *components_chain;
	int ret;

	INIT_LIST_HEAD(&components_chain_list);

	kcm = devm_kzalloc(dev, sizeof(struct kcm_t), GFP_KERNEL);
	if (kcm == NULL)
		return ERR_PTR(-ENOMEM);

	ret = alloc_hw_ep_handle_and_buff(dev, &kcm->playback_iacc_ep,
		BUFF_BYTES_EACH_CHANNEL, 4, 48000);
	if (ret) {
		pr_err("Allocate IACC playback endpoint buffer failed.\n");
		return ERR_PTR(ret);
	}
	ret = alloc_hw_ep_handle_and_buff(dev, &kcm->capture_iacc_ep,
		BUFF_BYTES_EACH_CHANNEL, 1, 16000);
	if (ret) {
		pr_err("Allocate IACC capture endpoint buffer failed.\n");
		goto error_alloc_capture_iacc_ep_failed;
	}
	ret = alloc_hw_ep_handle_and_buff(dev, &kcm->playback_usp_sco_ep,
		BUFF_BYTES_USP_SCO_PLAYBACK, 1, 16000);
	if (ret) {
		pr_err("Allocate USP-SCO playback endpoint buffer failed.\n");
		goto error_alloc_playback_usp_sco_ep_failed;
	}
	ret = alloc_hw_ep_handle_and_buff(dev, &kcm->capture_usp_sco_ep,
		BUFF_BYTES_USP_SCO_CAPTURE, 1, 16000);
	if (ret) {
		pr_err("Allocate USP-SCO capture endpoint buffer failed.\n");
		goto error_alloc_capture_usp_sco_ep_failed;
	}
	ret = alloc_hw_ep_handle_and_buff(dev, &kcm->capture_usp_a2dp_ep,
		BUFF_BYTES_EACH_CHANNEL, 2, 48000);
	if (ret) {
		pr_err("Allocate USP-A2DP capture endpoint buffer failed.\n");
		goto error_alloc_capture_usp_a2dp_ep_failed;
	}
	ret = alloc_hw_ep_handle_and_buff(dev, &kcm->capture_iacc_sco_ep,
		BUFF_BYTES_IACC_SCO_CAPTURE, 1, 16000);
	if (ret) {
		pr_err("Allocate IACC-SCO capture endpoint buffer failed.\n");
		goto error_alloc_capture_iacc_sco_ep_failed;
	}
	ret = alloc_hw_ep_handle_and_buff(dev, &kcm->playback_iacc_sco_ep,
		BUFF_BYTES_IACC_SCO_PLAYBACK, 1, 48000);
	if (ret) {
		pr_err("Allocate IACC-SCO capture endpoint buffer failed.\n");
		goto error_alloc_playback_iacc_sco_ep_failed;
	}

	init_shared_components();
	init_cvc_shared_components();
	components_chain = create_components_chain("Music Playback");
	init_sw_external_param(components_chain);
	hard_code_init_components_chain_music_playback(components_chain);

	components_chain = create_components_chain("Navigation Playback");
	init_sw_external_param(components_chain);
	hard_code_init_components_chain_navigation_playback(components_chain);

	components_chain = create_components_chain("Alarm Playback");
	init_sw_external_param(components_chain);
	hard_code_init_components_chain_alarm_playback(components_chain);

	components_chain = create_components_chain("A2DP Playback");
	hard_code_init_components_chain_a2dp_playback(components_chain);

	components_chain = create_components_chain("Voicecall-bt-to-iacc");
	hard_code_init_components_chain_voicecall_bt_to_iacc(components_chain);

	components_chain = create_components_chain("Voicecall-iacc-to-bt");
	hard_code_init_components_chain_voicecall_iacc_to_bt(components_chain);

	components_chain = create_components_chain("Analog Capture");
	init_sw_external_param(components_chain);
	hard_code_init_components_chain_capture(components_chain);

	return kcm;
error_alloc_playback_iacc_sco_ep_failed:
	free_hw_ep_handle_and_buff(dev, &kcm->capture_iacc_sco_ep);
error_alloc_capture_iacc_sco_ep_failed:
	free_hw_ep_handle_and_buff(dev, &kcm->capture_usp_a2dp_ep);
error_alloc_capture_usp_a2dp_ep_failed:
	free_hw_ep_handle_and_buff(dev, &kcm->capture_usp_sco_ep);
error_alloc_capture_usp_sco_ep_failed:
	free_hw_ep_handle_and_buff(dev, &kcm->playback_usp_sco_ep);
error_alloc_playback_usp_sco_ep_failed:
	free_hw_ep_handle_and_buff(dev, &kcm->capture_iacc_ep);
error_alloc_capture_iacc_ep_failed:
	free_hw_ep_handle_and_buff(dev, &kcm->playback_iacc_ep);
	return ERR_PTR(ret);
}

void kcm_deinit(struct device *dev)
{
	free_hw_ep_handle_and_buff(dev, &kcm->playback_iacc_sco_ep);
	free_hw_ep_handle_and_buff(dev, &kcm->capture_iacc_sco_ep);
	free_hw_ep_handle_and_buff(dev, &kcm->capture_usp_a2dp_ep);
	free_hw_ep_handle_and_buff(dev, &kcm->capture_usp_sco_ep);
	free_hw_ep_handle_and_buff(dev, &kcm->playback_usp_sco_ep);
	free_hw_ep_handle_and_buff(dev, &kcm->capture_iacc_ep);
	free_hw_ep_handle_and_buff(dev, &kcm->playback_iacc_ep);
}
