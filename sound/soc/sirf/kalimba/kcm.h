#ifndef _KAS_KCM_H
#define _KAS_KCM_H

#define EXEC_PHASE_HW_PARAMS		1
#define EXEC_PHASE_TRIGGER_START	2
#define EXEC_PHASE_TRIGGER_STOP		3
#define EXEC_PHASE_HW_FREE		4
#define EXEC_PHASE_HW_FREE_1		5
#define EXEC_PHASE_ACK			6
#define EXEC_PHASE_CONTROL		7

#define CONNECT_SINK			1
#define CONNECT_SOURCE			2

#define CTYPE_MAX			64
#define CTYPE_SHIFT			12
#define CTYPE_MASK			0xF000
#define CTYPE_PEQ_BAND_MASK		0x000F
#define CTYPE_PEQ_FC_MASK		0x000F
#define CTYPE_PEQ_GAIN_MASK		0x00F0
#define CTYPE_USER_PEQ			0x0000
#define CTYPE_SPK1_PEQ			0x0001
#define CTYPE_SPK2_PEQ			0x0002
#define CTYPE_SPK3_PEQ			0x0003
#define CTYPE_SPK4_PEQ			0x0004
#define CTYPE_DELAY			0x0005

#define PEQ_NUM_MAX			5
#define PEQ_BAND_MIN			1
#define PEQ_BAND_MAX			10
#define PEQ_PARAM_SET_LEN		69
#define PEQ_PARAM_BAND_FILTER		0
#define PEQ_PARAM_BAND_FC		1
#define PEQ_PARAM_BAND_GAIN		2
#define PEQ_PARAM_BAND_Q		3
#define PEQ_PARAM_MAX			3
#define PEQ_PARAM_BLOCKS_LEN		3
#define PEQ_PARAM_MAIN_LEN_16B		6
#define PEQ_PARAM_MAIN_LEN_24B		4
#define PEQ_PARAM_BAND_LEN_16B		6
#define PEQ_PARAM_BAND_LEN_24B		4
#define PEQ_SAMPLE_RATE			0x0780	/* 48K/25 */

#include "ipc.h"

struct kcm_t {
	struct endpoint_handle *playback_hw_ep_handle;
	struct endpoint_handle *capture_hw_ep_handle;
	u32 playback_hw_ep_handle_phy_addr;
	u32 capture_hw_ep_handle_phy_addr;
	void *playback_hw_ep_buff;
	void *capture_hw_ep_buff;
	u32 hw_playback_sample_rate;
	u32 hw_capture_sample_rate;
	u32 hw_playback_channels;
	u32 hw_capture_channels;
	u32 hw_audio_data_format;
	u32 hw_packing_format;
	u32 hw_interleaving_format;
	u32 hw_clock_master;
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
int execute_shared_components(u32 exec_phase);
int execute_components_chain(struct components_chain *components_chain,
	u32 exec_phase);
struct component *get_data_produced_ack_component(
	struct components_chain *components_chain);
struct component *get_control_component(int ctype);
int get_peq_param(u32 peq, u32 band, u32 ptype);
int set_peq_param(u32 peq, u32 band, u32 ptype, int value);
int execute_component(struct component *component);
struct kcm_t *kcm_init(struct device *dev);
void kcm_deinit(struct device *dev);
#endif
