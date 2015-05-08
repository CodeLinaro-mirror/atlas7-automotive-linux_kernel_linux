#ifndef _KAS_KCM_H
#define _KAS_KCM_H

#define EXEC_PHASE_HW_PARAMS		1
#define EXEC_PHASE_TRIGGER_START	2
#define EXEC_PHASE_TRIGGER_STOP		3
#define EXEC_PHASE_HW_FREE		4
#define EXEC_PHASE_ACK			5

#define CONNECT_SINK			1
#define CONNECT_SOURCE			2

struct component {
	u32 component_id;
	u32 execute_phase;
	u32 extend_info;
	u32 *params[16];
	u16 ret[16];
	struct component *component_next;
};

struct external_params {
	char *key;
	u32 value;
};

struct components_chain {
	struct list_head node;
	struct external_params external_params_map[64];
	struct component components[256];
	u16 *notify_ep_id;
	char *stream_name;
	struct component *component_first;
};

int set_external_param(struct components_chain *components_chain,
	char *key, u32 value);
struct components_chain *get_components_chain(char *stream_name);
u16 get_notify_ep_id(struct components_chain *components_chain);
int execute_components_chain(struct components_chain *components_chain,
	u32 exec_phase);
struct component *get_data_produced_ack_component(
	struct components_chain *components_chain);
int execute_component(struct component *component);
void kcm_init(void);
#endif
