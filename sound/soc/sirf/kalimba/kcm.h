#ifndef _KAS_KCM_H
#define _KAS_KCM_H

#define EXEC_PHASE_HW_PARAMS		1
#define EXEC_PHASE_TRIGGER_START	2
#define EXEC_PHASE_TRIGGER_STOP		3
#define EXEC_PHASE_HW_FREE		4
#define EXEC_PHASE_HW_FREE_1		5
#define EXEC_PHASE_ACK			6

#define CONNECT_SINK			1
#define CONNECT_SOURCE			2

#include "ipc.h"

struct hw_ep_handle_buff_t {
	struct endpoint_handle *handle;
	u32 handle_phy_addr;
	void *buff;
	u32 sample_rate;
	u32 channels;
	u32 audio_data_format;
	u32 packing_format;
	u32 interleaving_format;
	u32 clock_master;
	int buff_bytes;
};

struct kcm_t {
	struct hw_ep_handle_buff_t playback_iacc_ep;
	struct hw_ep_handle_buff_t capture_iacc_ep;
	struct hw_ep_handle_buff_t playback_iacc_sco_ep;
	struct hw_ep_handle_buff_t capture_iacc_sco_ep;
	struct hw_ep_handle_buff_t playback_usp_sco_ep;
	struct hw_ep_handle_buff_t capture_usp_sco_ep;
	struct hw_ep_handle_buff_t capture_usp_a2dp_ep;
};

struct component {
	u32 component_id;
	u32 execute_phase;
	u32 extend_info;
	u32 params[16];
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
	int stream_id;
};

int set_external_param(struct components_chain *components_chain,
	char *key, u32 value);
struct components_chain *get_components_chain(const char *stream_name);
u16 get_notify_ep_id(struct components_chain *components_chain);
u16 get_volume_control_op_id(void);
int execute_global_shared_components(u32 exec_phase);
int execute_cvc_shared_components(u32 exec_phase);
u16 get_mixer_op_id(void);
u16 get_peq_op_id(u16 index);
int execute_components_chain(struct components_chain *components_chain,
	u32 exec_phase);
struct component *get_data_produced_ack_component(
	struct components_chain *components_chain);
struct component *get_control_component(int ctype);
int execute_component(struct component *component);
struct kcm_t *kcm_init(struct device *dev);
void kcm_deinit(struct device *dev);
#endif
