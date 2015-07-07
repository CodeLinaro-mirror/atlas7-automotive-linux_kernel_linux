/*
 * kailimba components PCM drive
 *
 * Copyright (c) 2015 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "ipc.h"
#include "kcm.h"

static struct list_head components_chain_list;
static struct ipc_data *ipc_data;

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

static int init_external_param(struct components_chain *components_chain,
	char *key, u32 value)
{
	int i;

	for (i = 0; i < 64; i++) {
		if (components_chain->external_params_map[i].key)
			continue;
		components_chain->external_params_map[i].key =
			kstrdup(key, GFP_KERNEL);
		if (components_chain->external_params_map[i].key == NULL)
			return -ENOMEM;
		components_chain->external_params_map[i].value = value;
		return 0;
	}
	pr_err("The external params max support 64 pairs\n");
	return -EINVAL;
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

struct components_chain *get_components_chain(char *stream_name)
{
	struct components_chain *components_chain;

	list_for_each_entry(components_chain, &components_chain_list, node) {
		if (!strncmp(stream_name, components_chain->stream_name,
			strlen(stream_name)))
			return components_chain;
	}
	return NULL;
}

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
};

static char *hw_ep_configure_params_key[] = {
	"hw_ep_conf_audio_sample_rate",
	"hw_ep_conf_audio_data_format",
	"hw_ep_conf_dram_packing_format",
	"hw_ep_conf_interleaving_mode",
	"hw_ep_conf_clock_master",
};

static void hard_cord_init_components_chain_playback(
	struct components_chain *components_chain)
{
	struct component *components = components_chain->components;
	int i, j, k;
	u32 *value;

	components[0].component_id = CREATE_OPERATOR_REQ;
	components[0].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[0].params[0] = (u32 *)CAPABILITY_ID_MONO_PASSTHOUGH;
	components_chain->component_first = &components[0];

	components[1].component_id = CREATE_OPERATOR_REQ;
	components[1].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[1].params[0] = (u32 *)CAPABILITY_ID_MONO_PASSTHOUGH;
	components[0].component_next = &components[1];

	components[2].component_id = CREATE_OPERATOR_REQ;
	components[2].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[2].params[0] = (u32 *)CAPABILITY_ID_MONO_PASSTHOUGH;
	components[1].component_next = &components[2];

	components[3].component_id = CREATE_OPERATOR_REQ;
	components[3].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[3].params[0] = (u32 *)CAPABILITY_ID_MONO_PASSTHOUGH;
	components[2].component_next = &components[3];

	components[4].component_id = GET_SINK_REQ;
	components[4].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[4].params[0] = (u32 *)ENDPOINT_TYPE_IACC;
	components[4].params[1] = (u32 *)ENDPOINT_PHY_DEV_IACC;
	get_external_param_addr(components_chain, "hw_ep_channles", &value);
	components[4].params[2] = (u32 *)value;
	get_external_param_addr(components_chain, "hw_ep_handle_addr", &value);
	components[4].params[3] = (u32 *)value;
	components[3].component_next = &components[4];

	components[5].component_id = GET_SOURCE_REQ;
	components[5].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[5].params[0] = (u32 *)ENDPOINT_TYPE_FILE;
	components[5].params[1] = (u32 *)0;
	get_external_param_addr(components_chain, "sw_ep_channles", &value);
	components[5].params[2] = (u32 *)value;
	get_external_param_addr(components_chain, "sw_ep_handle_addr", &value);
	components[5].params[3] = (u32 *)value;
	components[4].component_next = &components[5];

	i = 6;
	for (k = 0; k < 4; k++) {
		for (j = 0; j < 5; j++) {
			components[i].component_id = ENDPOINT_CONFIGURE_REQ;
			components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
			components[i].params[0] =
				(u32 *)(&components[4].ret[k]);
			components[i].params[1] = (u32 *)(ep_configure_key[j]);
			get_external_param_addr(components_chain,
				hw_ep_configure_params_key[j], &value);
			components[i].params[2] = (u32 *)value;
			components[i - 1].component_next = &components[i];
			i++;
		}
	}

	for (k = 0; k < 4; k++) {
		for (j = 0; j < 6; j++) {
			components[i].component_id = ENDPOINT_CONFIGURE_REQ;
			components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
			components[i].params[0] =
				(u32 *)(&components[5].ret[k]);
			components[i].params[1] = (u32 *)(ep_configure_key[j]);
			get_external_param_addr(components_chain,
				sw_ep_configure_params_key[j], &value);
			components[i].params[2] = (u32 *)value;
			components[i - 1].component_next = &components[i];
			i++;
		}
	}
	/* i == 50 */
	for (k = 0; k < 4; k++) {
		components[i].component_id = CONNECT_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32 *)(&components[k].ret[0]);
		components[i].params[1] = (u32 *)(&components[4].ret[k]);
		components[i].extend_info = CONNECT_SINK;
		components[i - 1].component_next = &components[i];
		i++;
	}
	/* i == 54 */
	for (k = 0; k < 4; k++) {
		components[i].component_id = CONNECT_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] = (u32 *)(&components[5].ret[k]);
		components[i].params[1] = (u32 *)(&components[k].ret[0]);
		components[i].extend_info = CONNECT_SOURCE;
		components[i - 1].component_next = &components[i];
		i++;
	}

	for (k = 0; k < 4; k++) {
		components[i].component_id = START_OPERATOR_REQ;
		components[i].execute_phase = EXEC_PHASE_TRIGGER_START;
		components[i].params[0] = (u32 *)(&components[k].ret[0]);
		components[i].params[1] = (u32 *)1;
		components[i - 1].component_next = &components[i];
		i++;
	}

	components[i].component_id = DATA_PRODUCED;
	components[i].execute_phase = EXEC_PHASE_TRIGGER_START;
	components[i].params[0] = (u32 *)(&components[5].ret[0]);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = DATA_PRODUCED;
	components[i].execute_phase = EXEC_PHASE_ACK;
	components[i].params[0] = (u32 *)(&components[5].ret[0]);
	components[i - 1].component_next = &components[i];
	i++;

	for (k = 0; k < 4; k++) {
		components[i].component_id = STOP_OPERATOR_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_FREE;
		components[i].params[0] = (u32 *)(&components[k].ret[0]);
		components[i].params[1] = (u32 *)1;
		components[i - 1].component_next = &components[i];
		i++;
	}
	for (k = 0; k < 4; k++) {
		components[i].component_id = DISCONNECT_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_FREE;
		components[i].params[0] = (u32 *)1;
		components[i].params[1] = (u32 *)(&components[k + 50].ret[0]);
		components[i - 1].component_next = &components[i];
		i++;
	}
	for (k = 0; k < 4; k++) {
		components[i].component_id = DISCONNECT_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_FREE;
		components[i].params[0] = (u32 *)1;
		components[i].params[1] = (u32 *)(&components[k + 54].ret[0]);
		components[i - 1].component_next = &components[i];
		i++;
	}
	components[i].component_id = CLOSE_SINK_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = (u32 *)4;
	components[i].params[1] = (u32 *)(components[4].ret);
	components[i - 1].component_next = &components[i];
	i++;
	components[i].component_id = CLOSE_SOURCE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = (u32 *)4;
	components[i].params[1] = (u32 *)(components[5].ret);
	components[i - 1].component_next = &components[i];
	i++;

	for (k = 0; k < 4; k++) {
		components[i].component_id = DESTROY_OPERATOR_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_FREE;
		components[i].params[0] = (u32 *)(&components[k].ret[0]);
		components[i].params[1] = (u32 *)1;
		components[i - 1].component_next = &components[i];
		i++;
	}
	components[i - 1].component_next = NULL;

	components_chain->notify_ep_id = &components[5].ret[0];
}

static void hard_cord_init_components_chain_capture(
	struct components_chain *components_chain)
{
	struct component *components = components_chain->components;
	int i, j;
	u32 *value;

	components[0].component_id = CREATE_OPERATOR_REQ;
	components[0].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[0].params[0] = (u32 *)CAPABILITY_ID_MONO_PASSTHOUGH;
	components_chain->component_first = &components[0];

	components[1].component_id = GET_SOURCE_REQ;
	components[1].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[1].params[0] = (u32 *)ENDPOINT_TYPE_IACC;
	components[1].params[1] = (u32 *)ENDPOINT_PHY_DEV_IACC;
	get_external_param_addr(components_chain, "hw_ep_channles", &value);
	components[1].params[2] = (u32 *)value;
	get_external_param_addr(components_chain, "hw_ep_handle_addr", &value);
	components[1].params[3] = (u32 *)value;
	components[0].component_next = &components[1];

	components[2].component_id = GET_SINK_REQ;
	components[2].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[2].params[0] = (u32 *)ENDPOINT_TYPE_FILE;
	components[2].params[1] = (u32 *)0;
	get_external_param_addr(components_chain, "sw_ep_channles", &value);
	components[2].params[2] = (u32 *)value;
	get_external_param_addr(components_chain, "sw_ep_handle_addr", &value);
	components[2].params[3] = (u32 *)value;
	components[1].component_next = &components[2];

	i = 3;
	for (j = 0; j < 5; j++) {
		components[i].component_id = ENDPOINT_CONFIGURE_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] =
			(u32 *)(&components[1].ret[0]);
		components[i].params[1] = (u32 *)(ep_configure_key[j]);
		get_external_param_addr(components_chain,
				hw_ep_configure_params_key[j], &value);
		components[i].params[2] = (u32 *)value;
		components[i - 1].component_next = &components[i];
		i++;
	}

	for (j = 0; j < 6; j++) {
		components[i].component_id = ENDPOINT_CONFIGURE_REQ;
		components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
		components[i].params[0] =
			(u32 *)(&components[2].ret[0]);
		components[i].params[1] = (u32 *)(ep_configure_key[j]);
		get_external_param_addr(components_chain,
				sw_ep_configure_params_key[j], &value);
		components[i].params[2] = (u32 *)value;
		components[i - 1].component_next = &components[i];
		i++;
	}

	/* i == 14 */
	components[i].component_id = CONNECT_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32 *)(&components[0].ret[0]);
	components[i].params[1] = (u32 *)(&components[2].ret[0]);
	components[i].extend_info = CONNECT_SINK;
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = CONNECT_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32 *)(&components[1].ret[0]);
	components[i].params[1] = (u32 *)(&components[0].ret[0]);
	components[i].extend_info = CONNECT_SOURCE;
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = START_OPERATOR_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_PARAMS;
	components[i].params[0] = (u32 *)(&components[0].ret[0]);
	components[i].params[1] = (u32 *)1;
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = STOP_OPERATOR_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = (u32 *)(&components[0].ret[0]);
	components[i].params[1] = (u32 *)1;
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = DISCONNECT_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = (u32 *)1;
	components[i].params[1] = (u32 *)(&components[14].ret[0]);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = DISCONNECT_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = (u32 *)1;
	components[i].params[1] = (u32 *)(&components[15].ret[0]);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = CLOSE_SINK_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = (u32 *)1;
	components[i].params[1] = (u32 *)(components[2].ret);
	components[i - 1].component_next = &components[i];
	i++;
	components[i].component_id = CLOSE_SOURCE_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = (u32 *)1;
	components[i].params[1] = (u32 *)(components[1].ret);
	components[i - 1].component_next = &components[i];
	i++;

	components[i].component_id = DESTROY_OPERATOR_REQ;
	components[i].execute_phase = EXEC_PHASE_HW_FREE;
	components[i].params[0] = (u32 *)(&components[0].ret[0]);
	components[i].params[1] = (u32 *)1;
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
	int ret;

	mutex_lock(&ipc_data->msg_send_mutex);
	switch (component->component_id) {
	case CREATE_OPERATOR_REQ:
		ret = ipc_create_operator(ipc_data,
			(u16)(component->params[0]),
			component->ret);
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
		if (component->extend_info == CONNECT_SINK)
			ret = ipc_connect_endpoints(ipc_data,
				*((u16 *)(component->params[0])) + 0x2000,
				*((u16 *)(component->params[1])),
				component->ret);
		else if (component->extend_info == CONNECT_SOURCE)
			ret = ipc_connect_endpoints(ipc_data,
				*((u16 *)(component->params[0])),
				*((u16 *)(component->params[1])) + 0xA000,
				component->ret);
		else
			ret = -EINVAL;
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

	mutex_unlock(&ipc_data->msg_send_mutex);
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

int execute_components_chain(struct components_chain *components_chain,
	u32 exec_phase)
{
	struct component *component;
	int ret;

	ipc_data = ipc_get_data();
	if (components_chain->component_first == NULL)
		return -ENODEV;
	for (component = components_chain->component_first;
		component != NULL; component = component->component_next) {
		if (component->execute_phase != exec_phase)
			continue;
		ret = execute_component(component);
		if (ret < 0)
			return ret;
	}
	return 0;
}

void kcm_init(void)
{
	struct components_chain *components_chain;

	INIT_LIST_HEAD(&components_chain_list);
	components_chain = create_components_chain("Music Playback");

	init_external_param(components_chain, "hw_ep_channles", 0);
	init_external_param(components_chain, "hw_ep_handle_addr", 0);
	init_external_param(components_chain, "hw_ep_conf_audio_sample_rate",
			0);
	init_external_param(components_chain, "hw_ep_conf_audio_data_format",
			0);
	init_external_param(components_chain,
			"hw_ep_conf_dram_packing_format", 0);
	init_external_param(components_chain, "hw_ep_conf_interleaving_mode",
			0);
	init_external_param(components_chain, "hw_ep_conf_clock_master", 0);
	init_external_param(components_chain, "sw_ep_channles", 0);
	init_external_param(components_chain, "sw_ep_handle_addr", 0);
	init_external_param(components_chain, "sw_ep_conf_audio_sample_rate",
			0);
	init_external_param(components_chain, "sw_ep_conf_audio_data_format",
			0);
	init_external_param(components_chain,
			"sw_ep_conf_dram_packing_format", 0);
	init_external_param(components_chain, "sw_ep_conf_interleaving_mode",
			0);
	init_external_param(components_chain, "sw_ep_conf_clock_master", 0);
	init_external_param(components_chain, "sw_ep_period_size", 0);
	hard_cord_init_components_chain_playback(components_chain);

	components_chain = create_components_chain("Analog Capture");
	init_external_param(components_chain, "hw_ep_channles", 0);
	init_external_param(components_chain, "hw_ep_handle_addr", 0);
	init_external_param(components_chain, "hw_ep_conf_audio_sample_rate",
			0);
	init_external_param(components_chain, "hw_ep_conf_audio_data_format",
			0);
	init_external_param(components_chain,
			"hw_ep_conf_dram_packing_format", 0);
	init_external_param(components_chain, "hw_ep_conf_interleaving_mode",
			0);
	init_external_param(components_chain, "hw_ep_conf_clock_master", 0);
	init_external_param(components_chain, "sw_ep_channles", 0);
	init_external_param(components_chain, "sw_ep_handle_addr", 0);
	init_external_param(components_chain, "sw_ep_conf_audio_sample_rate",
			0);
	init_external_param(components_chain, "sw_ep_conf_audio_data_format",
			0);
	init_external_param(components_chain,
			"sw_ep_conf_dram_packing_format", 0);
	init_external_param(components_chain, "sw_ep_conf_interleaving_mode",
			0);
	init_external_param(components_chain, "sw_ep_conf_clock_master", 0);
	init_external_param(components_chain, "sw_ep_period_size", 0);
	hard_cord_init_components_chain_capture(components_chain);
}
