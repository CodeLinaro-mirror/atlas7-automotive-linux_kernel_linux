/*
 * kailimba components PCM drive
 *
 * Copyright (c) 2015 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "ipc.h"
#include "kcm.h"

#define BUFF_BYTES_EACH_CHANNEL		256

#define SET_PRIMARY_STREAM		0
#define CLEAR_PRIMARY_STREAM		1

static struct kcm_t *kcm;

static struct list_head components_chain_list;
static struct ipc_data *ipc_data;

static struct component components_shared[256];
static int shared_components_size;
static unsigned long active_stream;
static int curr_primary_stream;

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
	static u16 mixer_oper_conf_channels[3] = {0x4, 0x4, 0x2};
	static u16 mixer_oper_conf_gains[6] = {0x20, 0, 0x20, 0, 0x20, 0};

	components_shared[0].component_id = CREATE_OPERATOR_REQ;
	components_shared[0].execute_phase = EXEC_PHASE_HW_PARAMS;
	components_shared[0].params[0] = CAPABILITY_ID_MIXER;

	components_shared[1].component_id = OPERATOR_MESSAGE_REQ;
	components_shared[1].execute_phase = EXEC_PHASE_HW_PARAMS;
	components_shared[1].params[0] = (u32)(&components_shared[0].ret[0]);
	components_shared[1].params[1] = OPERATOR_MSG_SET_CHANNELS;
	components_shared[1].params[2] = 3;
	components_shared[1].params[3] = (u32)(mixer_oper_conf_channels);

	components_shared[2].component_id = OPERATOR_MESSAGE_REQ;
	components_shared[2].execute_phase = EXEC_PHASE_HW_PARAMS;
	components_shared[2].params[0] = (u32)(&components_shared[0].ret[0]);
	components_shared[2].params[1] = OPERATOR_MSG_SET_GAINS;
	components_shared[2].params[2] = 6;
	components_shared[2].params[3] = (u32)(mixer_oper_conf_gains);

	components_shared[3].component_id = GET_SINK_REQ;
	components_shared[3].execute_phase = EXEC_PHASE_HW_PARAMS;
	components_shared[3].params[0] = ENDPOINT_TYPE_IACC;
	components_shared[3].params[1] = ENDPOINT_PHY_DEV_IACC;
	components_shared[3].params[2] = (u32)(&kcm->hw_playback_channels);
	components_shared[3].params[3] =
		(u32)(&kcm->playback_hw_ep_handle_phy_addr);

	for (i = 0; i < 4; i++) {
		components_shared[4 + i].component_id = ENDPOINT_CONFIGURE_REQ;
		components_shared[4 + i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components_shared[4 + i].params[0] =
			(u32)(&components_shared[3].ret[i]);
		components_shared[4 + i].params[1] =
			ENDPOINT_CONF_AUDIO_SAMPLE_RATE;
		components_shared[4 + i].params[2] =
			(u32)(&kcm->hw_playback_sample_rate);
	}

	for (i = 0; i < 4; i++) {
		components_shared[8 + i].component_id = ENDPOINT_CONFIGURE_REQ;
		components_shared[8 + i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components_shared[8 + i].params[0] =
			(u32)(&components_shared[3].ret[i]);
		components_shared[8 + i].params[1] =
			ENDPOINT_CONF_AUDIO_DATA_FORMAT;
		components_shared[8 + i].params[2] =
			(u32)(&kcm->hw_audio_data_format);
	}

	for (i = 0; i < 4; i++) {
		components_shared[12 + i].component_id = ENDPOINT_CONFIGURE_REQ;
		components_shared[12 + i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components_shared[12 + i].params[0] =
			(u32)(&components_shared[3].ret[i]);
		components_shared[12 + i].params[1] =
			ENDPOINT_CONF_DRAM_PACKING_FORMAT;
		components_shared[12 + i].params[2] =
			(u32)(&kcm->hw_packing_format);
	}

	for (i = 0; i < 4; i++) {
		components_shared[16 + i].component_id = ENDPOINT_CONFIGURE_REQ;
		components_shared[16 + i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components_shared[16 + i].params[0] =
			(u32)(&components_shared[3].ret[i]);
		components_shared[16 + i].params[1] =
			ENDPOINT_CONF_INTERLEAVING_MODE;
		components_shared[16 + i].params[2] =
			(u32)(&kcm->hw_interleaving_format);
	}

	for (i = 0; i < 4; i++) {
		components_shared[20 + i].component_id = ENDPOINT_CONFIGURE_REQ;
		components_shared[20 + i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components_shared[20 + i].params[0] =
			(u32)(&components_shared[3].ret[i]);
		components_shared[20 + i].params[1] =
			ENDPOINT_CONF_CLOCK_MASTER;
		components_shared[20 + i].params[2] =
			(u32)(&kcm->hw_clock_master);
	}

	for (i = 0; i < 4; i++) {
		components_shared[24 + i].component_id = CONNECT_REQ;
		components_shared[24 + i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components_shared[24 + i].params[0] =
			(u32)(&components_shared[0].ret[0]);
		components_shared[24 + i].params[1] = 0x2000 + i;
		components_shared[24 + i].params[2] =
			(u32)(&components_shared[3].ret[i]);
		components_shared[24 + i].params[3] = 0;
	}

	components_shared[28].component_id = START_OPERATOR_REQ;
	components_shared[28].execute_phase = EXEC_PHASE_TRIGGER_START;
	components_shared[28].params[0] = (u32)(&components_shared[0].ret[0]);
	components_shared[28].params[1] = 1;

	components_shared[29].component_id = STOP_OPERATOR_REQ;
	components_shared[29].execute_phase = EXEC_PHASE_HW_FREE;
	components_shared[29].params[0] = (u32)(&components_shared[0].ret[0]);
	components_shared[29].params[1] = 1;

	for (i = 0; i < 4; i++) {
		components_shared[30 + i].component_id = DISCONNECT_REQ;
		components_shared[30 + i].execute_phase = EXEC_PHASE_HW_FREE_1;
		components_shared[30 + i].params[0] = 1;
		components_shared[30 + i].params[1] =
			(u32)(&components_shared[24 + i].ret[0]);
	}

	components_shared[34].component_id = CLOSE_SINK_REQ;
	components_shared[34].execute_phase = EXEC_PHASE_HW_FREE_1;
	components_shared[34].params[0] = 4;
	components_shared[34].params[1] = (u32)(components_shared[3].ret);

	components_shared[35].component_id = DESTROY_OPERATOR_REQ;
	components_shared[35].execute_phase = EXEC_PHASE_HW_FREE_1;
	components_shared[35].params[0] = (u32)(&components_shared[0].ret[0]);
	components_shared[35].params[1] = 1;

	shared_components_size = 36;
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

static void hard_code_init_components_chain_notify_playback(
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

	components[i].component_id = CREATE_OPERATOR_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = CAPABILITY_ID_BASIC_PASSTHOUGH;
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

	/* i == 4 */
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

	/* i == 28 */
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
		components[i].params[2] = (u32)(&components[2].ret[0]);
		components[i].params[3] = 0xA000 + k;
		components[i - 1].component_next = &components[i];
		i++;
	}

	for (k = 0; k < 4; k++) {
		components[i].component_id = CONNECT_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32)(&components[2].ret[0]);
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

	for (k = 0; k < 2; k++) {
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

	for (k = 0; k < 2; k++) {
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
		components[i].params[1] = (u32)(&components[28 + k].ret[0]);
		components[i - 1].component_next = &components[i];
		i++;
	}

	components[i].component_id = CLOSE_SOURCE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = 4;
	components[i].params[1] = (u32)(components[0].ret);
	components[i - 1].component_next = &components[i];
	i++;

	for (k = 0; k < 2; k++) {
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

	components[i].component_id = CREATE_OPERATOR_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = CAPABILITY_ID_BASIC_PASSTHOUGH;
	components[i - 1].component_next = &components[i];
	i++;

	/* i == 2 */
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

	/* i == 26*/
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
		components[i].params[1] = (u32)(&components[k + 26].ret[0]);
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
	components[i].params[2] = (u32)(&kcm->hw_capture_channels);
	components[i].params[3] = (u32)(&kcm->capture_hw_ep_handle_phy_addr);
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
	components[i].params[2] = (u32)(&kcm->hw_capture_sample_rate);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i] .execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_AUDIO_DATA_FORMAT;
	components[i].params[2] = (u32)(&kcm->hw_audio_data_format);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i] .execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_DRAM_PACKING_FORMAT;
	components[i].params[2] = (u32)(&kcm->hw_packing_format);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i] .execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_INTERLEAVING_MODE;
	components[i].params[2] = (u32)(&kcm->hw_interleaving_format);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = ENDPOINT_CONFIGURE_REQ;
	components[i] .execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32)(&components[1].ret[0]);
	components[i].params[1] = ENDPOINT_CONF_CLOCK_MASTER;
	components[i].params[2] = (u32)(&kcm->hw_clock_master);
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

int execute_component(struct component *component)
{
	int ret = 0;
	u16 primary_stream = 0;

	mutex_lock(&ipc_data->msg_send_mutex);

	switch (component->component_id) {
	case CREATE_OPERATOR_REQ:
		ret = ipc_create_operator(ipc_data,
			(u16)(component->params[0]),
			component->ret);
		if (ret == 0)
			if ((u16)(component->params[0]) == CAPABILITY_ID_MIXER)
				curr_primary_stream = 0;
		break;
	case OPERATOR_MESSAGE_REQ:
		if ((u16)(component->params[1]) !=
				OPERATOR_MSG_SET_PRIMARY_STREAM) {
			ret = ipc_operator_message(ipc_data,
				*((u16 *)(component->params[0])),
				(u16)(component->params[1]),
				(u16)(component->params[2]),
				(u16 *)(component->params[3]),
				NULL, NULL);
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
		if (curr_primary_stream == primary_stream)
			break;
		curr_primary_stream = primary_stream;
		if (primary_stream == 0)
			break;
		ret = ipc_operator_message(ipc_data,
				*((u16 *)(component->params[0])),
				(u16)(component->params[1]),
				(u16)(component->params[2]),
				&primary_stream,
				NULL, NULL);
		break;
	case GET_SINK_REQ:
		ret = ipc_get_sink(ipc_data, (u16)(component->params[0]),
			(u16)(component->params[1]),
			*((u16 *)(component->params[2])),
			*((u32 *)(component->params[3])),
			component->ret);
		break;
	case GET_SOURCE_REQ:
		ret = ipc_get_source(ipc_data, (u16)(component->params[0]),
			(u16)(component->params[1]),
			*((u16 *)(component->params[2])),
			*((u32 *)(component->params[3])),
			component->ret);
		break;
	case ENDPOINT_CONFIGURE_REQ:
		ret = ipc_config_endpoint(ipc_data,
			*((u16 *)(component->params[0])),
			(u16)(component->params[1]),
			*((u32 *)(component->params[2])));
		break;
	case CONNECT_REQ:
		ret = ipc_connect_endpoints(ipc_data,
			*((u16 *)(component->params[0])) +
			(u16)(component->params[1]),
			*((u16 *)(component->params[2])) +
			(u16)(component->params[3]),
			component->ret);
		break;
	case START_OPERATOR_REQ:
		ret = ipc_start_operator(ipc_data,
			((u16 *)(component->params[0])),
			(u16)(component->params[1]));
		break;
	case DATA_PRODUCED:
		ipc_data_produced(ipc_data,
			*((u16 *)(component->params[0])));
		ret = 0;
		break;
	case STOP_OPERATOR_REQ:
		ret = ipc_stop_operator(ipc_data,
			((u16 *)(component->params[0])),
			(u16)(component->params[1]));
		break;
	case DISCONNECT_REQ:
		ret = ipc_disconnect_endpoints(ipc_data,
			(u16)(component->params[0]),
			(u16 *)(component->params[1]));
		break;
	case CLOSE_SINK_REQ:
		ret = ipc_close_sink(ipc_data,
			(u16)(component->params[0]),
			(u16 *)(component->params[1]));
		break;
	case CLOSE_SOURCE_REQ:
		ret = ipc_close_source(ipc_data,
			(u16)(component->params[0]),
			(u16 *)(component->params[1]));
		break;
	case DESTROY_OPERATOR_REQ:
		ret = ipc_destroy_operator(ipc_data,
			(u16 *)(component->params[0]),
			(u16)(component->params[1]));
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret != 0)
		pr_err("ipc command executed failed: command id: 0x%04x\n",
			component->component_id);
	mutex_unlock(&ipc_data->msg_send_mutex);

	return ret;
}

struct component *get_data_produced_ack_component(
	struct components_chain *components_chain)
{
	struct component *component;

	if (unlikely(ipc_data == NULL))
		ipc_data = ipc_get_data();

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

int execute_shared_components(u32 exec_phase)
{
	int i;
	int ret = 0;

	for (i = 0; i < shared_components_size; i++) {
		if (components_shared[i].execute_phase != exec_phase)
			continue;
		ret = execute_component(&components_shared[i]);
		if (ret < 0)
			break;
	}
	return 0;
}

int execute_components_chain(struct components_chain *components_chain,
	u32 exec_phase)
{
	struct component *component;
	int ret = 0;

	ipc_data = ipc_get_data();
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

struct kcm_t *kcm_init(struct device *dev)
{
	struct components_chain *components_chain;
	u32 playback_buff_bytes, capture_buff_bytes;

	INIT_LIST_HEAD(&components_chain_list);

	kcm = devm_kzalloc(dev, sizeof(struct kcm_t), GFP_KERNEL);
	if (kcm == NULL)
		return ERR_PTR(-ENOMEM);

	kcm->hw_playback_sample_rate = 48000;
	kcm->hw_capture_sample_rate = 16000;
	kcm->hw_playback_channels = 4;
	kcm->hw_capture_channels = 1;
	kcm->hw_audio_data_format = 0;
	kcm->hw_packing_format = 2;
	kcm->hw_interleaving_format = 1;
	kcm->hw_clock_master = 1;

	playback_buff_bytes = BUFF_BYTES_EACH_CHANNEL
			* kcm->hw_playback_channels;
	capture_buff_bytes = BUFF_BYTES_EACH_CHANNEL * kcm->hw_capture_channels;

	kcm->playback_hw_ep_handle = dma_alloc_coherent(dev,
		sizeof(struct endpoint_handle),
		&kcm->playback_hw_ep_handle_phy_addr, GFP_KERNEL);
	if (kcm->playback_hw_ep_handle == NULL) {
		pr_err("Can't allocate playback hw endpoint handle buffer.\n");
		return ERR_PTR(-ENOMEM);
	}

	kcm->playback_hw_ep_buff = dma_alloc_coherent(dev,
		playback_buff_bytes,
		&kcm->playback_hw_ep_handle->buff_addr, GFP_KERNEL);
	if (kcm->playback_hw_ep_buff == NULL) {
		pr_err("Can't allocate playback hw endpoint buffer.\n");
		goto error_playback_hw_ep_buffer;
	}
	kcm->playback_hw_ep_handle->buff_length = playback_buff_bytes
		/ sizeof(u32);

	kcm->capture_hw_ep_handle = dma_alloc_coherent(dev,
		sizeof(struct endpoint_handle),
		&kcm->capture_hw_ep_handle_phy_addr, GFP_KERNEL);
	if (kcm->capture_hw_ep_handle == NULL) {
		pr_err("Can't allocate capture hw endpoint handle buffer.\n");
		goto error_capture_hw_ep_handle_buffer;
	}

	kcm->capture_hw_ep_buff = dma_alloc_coherent(dev,
		capture_buff_bytes,
		&kcm->capture_hw_ep_handle->buff_addr, GFP_KERNEL);
	if (kcm->capture_hw_ep_buff == NULL) {
		pr_err("Can't allocate capture hw endpoint buffer.\n");
		goto error_capture_hw_ep_buffer;
	}

	kcm->capture_hw_ep_handle->buff_length = capture_buff_bytes
		/ sizeof(u32);

	init_shared_components();
	components_chain = create_components_chain("Music Playback");
	init_sw_external_param(components_chain);
	hard_code_init_components_chain_music_playback(components_chain);

	components_chain = create_components_chain("Notify Playback");
	init_sw_external_param(components_chain);
	hard_code_init_components_chain_notify_playback(components_chain);

	components_chain = create_components_chain("Analog Capture");
	init_sw_external_param(components_chain);
	hard_code_init_components_chain_capture(components_chain);

	return kcm;
error_capture_hw_ep_buffer:
	dma_free_coherent(dev, sizeof(struct endpoint_handle),
		kcm->capture_hw_ep_handle, kcm->capture_hw_ep_handle_phy_addr);
error_capture_hw_ep_handle_buffer:
	dma_free_coherent(dev,
		BUFF_BYTES_EACH_CHANNEL * kcm->hw_playback_channels,
		kcm->playback_hw_ep_buff,
		kcm->playback_hw_ep_handle->buff_addr);
error_playback_hw_ep_buffer:
	dma_free_coherent(dev, sizeof(struct endpoint_handle),
		kcm->playback_hw_ep_handle,
		kcm->playback_hw_ep_handle_phy_addr);
	return ERR_PTR(-ENOMEM);
}

void kcm_deinit(struct device *dev)
{
	dma_free_coherent(dev,
		BUFF_BYTES_EACH_CHANNEL * kcm->hw_capture_channels,
		kcm->capture_hw_ep_buff, kcm->capture_hw_ep_handle->buff_addr);
	dma_free_coherent(dev, sizeof(struct endpoint_handle),
		kcm->capture_hw_ep_handle, kcm->capture_hw_ep_handle_phy_addr);
	dma_free_coherent(dev,
		BUFF_BYTES_EACH_CHANNEL * kcm->hw_playback_channels,
		kcm->playback_hw_ep_buff,
		kcm->playback_hw_ep_handle->buff_addr);
	dma_free_coherent(dev, sizeof(struct endpoint_handle),
		kcm->playback_hw_ep_handle,
		kcm->playback_hw_ep_handle_phy_addr);
}
