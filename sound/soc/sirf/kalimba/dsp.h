#ifndef _KAS_DSP_H
#define _KAS_DSP_H

struct ipc_data;

struct kalimba_msg_action {
	struct list_head node;	/* Link to other ipc_action */
	u32 message;
	void *priv_data;
	void (*handler)(u16, void *, u16 *);
};

struct kalimba {
	struct clk *clk_kas;
	struct clk *clk_audmscm;
	struct clk *clk_gpum;
	struct ipc_data *ipc_data;
	/* kalimba_msg_action list */
	struct list_head kalimba_msg_action_list;
	struct mutex msg_send_mutex;
};

struct endpoint_handle {
	u32 buff_addr;
	u32 buff_length;
	u32 read_pointer;
	u32 write_pointer;
};
u32 kalimba_read_reg(u32 reg_addr);
void kalimba_write_reg(u32 reg_addr, u32 val);
void kalimba_update_bits_reg(u32 reg_addr, u32 mask, u32 val);

void kalimba_msg_send_lock(void);
void kalimba_msg_send_unlock(void);

void kalimba_create_operator(u16 capability_id, u16 *operator_id, u16 *resp);
int kalimba_destroy_operator(u16 *operators_id, u16 operator_count, u16 *resp);
int kalimba_operator_message(u16 operator_id, u16 msg_id, int message_data_len,
	u16 *msg_data, u16 **res_msg_data, u16 *rsp_msg_len, u16 *resp);
int kalimba_start_operator(u16 *operators_id, u16 operator_count, u16 *resp);
int kalimba_stop_operator(u16 *operators_id, u16 operator_count, u16 *resp);
int kalimba_reset_operator(u16 *operators_id, u16 operator_count, u16 *resp);
void kalimba_get_source(u16 endpoint_type, u16 instance_id, u16 channels,
	u32 handle_addr, u16 *endpoint_id, u16 *resp);
void kalimba_get_sink(u16 endpoint_type, u16 instance_id, u16 channels,
	u32 handle_addr, u16 *endpoint_id, u16 *resp);
void kalimba_config_endpoint(u16 endpoint_id, u16 config_key,
	u32 config_value, u16 *resp);
void kalimba_connect_endpoints(u16 source_endpoint_id, u16 sink_endpoint_id,
	u16 *connect_id, u16 *resp);
int kalimba_close_source(u16 endpoint_count, u16 *endpoint_id, u16 *resp);
int kalimba_close_sink(u16 endpoint_count, u16 *endpoint_id, u16 *resp);
int kalimba_disconnect_endpoints(u16 connect_count, u16 *connect_id, u16 *resp);
void kalimba_data_produced(u16 endpoint_id);
void kalimba_data_consumed(u16 endpoint_id);
void kalimba_get_version_id(u32 *version_id, u16 *resp);
void kalimba_get_capid_list(u16 *capids, u16 *resp);
void kalimba_get_opid_list(u16 filter, u16 *opids, u16 *capids, u16 *resp);
void kalimba_get_connection_list(u16 source_filter, u16 sink_filter,
	u16 *connection_ids, u16 *source_ids, u16 *sink_ids, u16 *resp);
void kalimba_sync_endpoint(u16 endpoint1, u16 endpoint2, u16 *resp);
void kalimba_get_endpoint_info(u16 endpoint_id, u16 configure_key, u16 *resp);
void kalimba_capability_code_dram_addr_set(u16 addr_low, u16 addr_high,
	u16 *capids, u16 *resp);
void kalimba_capability_code_dram_addr_clear(u16 addr_low, u16 addr_high,
	u16 *resp);
void *register_kalimba_msg_action(u16 message,
		void (*handler)(u16, void *, u16 *), void *priv_data);
void unregister_kalimba_msg_action(void *action_id);

void kalimba_do_actions(u16 message, u16 *data);
void kalimba_set_channel_volume(int channel, int vol);

#define ENDPOINT_TYPE_I2S			2
#define ENDPOINT_TYPE_IACC			3
#define ENDPOINT_TYPE_SPDIF			5
#define ENDPOINT_TYPE_FILE			10
#define ENDPOINT_TYPE_APPDATA			11

#define ENDPOINT_PHY_DEV_PCM0			0
#define ENDPOINT_PHY_DEV_PCM1			1
#define ENDPOINT_PHY_DEV_PCM2			2
#define ENDPOINT_PHY_DEV_I2S0			3
#define ENDPOINT_PHY_DEV_I2S1			4
#define ENDPOINT_PHY_DEV_AC97			5
#define ENDPOINT_PHY_DEV_A7CA			6
#define ENDPOINT_PHY_DEV_IACC			7
#define ENDPOINT_PHY_DEV_SPDIF			8

#define MESSAGE_SEND_ADDR			0xFFAF9C
#define MESSAGE_SEND_ACK_ADDR			0x007FA0

#define CAPABILITY_ID_BASIC_PASSTHOUGH		0x0001
#define CAPABILITY_ID_RESAMPLER			0x0009
#define CAPABILITY_ID_MIXER			0x000A
#define CAPABILITY_ID_VOLUME_CONTROL		0x0048
#define CAPABILITY_ID_PEQ			0x0049

#define RESAMPLER_SET_CONVERSION_RATE		0x0002
#define RESAMPLER_SET_CUSTOM_RATE		0x0003
#define RESAMPLER_FILTER_COEFFICIENTS		0x0004

#define OPERATOR_MSG_VOLUME_CTRL_SET_CONTROL	0x2002

#define CREATE_OPERATOR_REQ			0x0001
#define CREATE_OPERATOR_RSP			0x1001

#define START_OPERATOR_REQ			0x0002
#define START_OPERATOR_RSP			0x1002

#define STOP_OPERATOR_REQ			0x0003
#define STOP_OPERATOR_RSP			0x1003

#define RESET_OPERATOR_REQ			0x0004
#define RESET_OPERATOR_RSP			0x1004

#define DESTROY_OPERATOR_REQ			0x0005
#define DESTROY_OPERATOR_RSP			0x1005

#define OPERATOR_MESSAGE_REQ			0x0006
#define OPERATOR_MESSAGE_RSP			0x1006

#define OPERATOR_MSG_SET_CHANNELS		2
#define OPERATOR_MSG_SET_GAINS			1
#define OPERATOR_MSG_SET_PRIMARY_STREAM		4

#define GET_SOURCE_REQ				0x0008
#define GET_SOURCE_RSP				0x1008

#define GET_SINK_REQ				0x0009
#define GET_SINK_RSP				0x1009

#define CLOSE_SOURCE_REQ			0x000A
#define CLOSE_SOURCE_RSP			0x100A

#define CLOSE_SINK_REQ				0x000B
#define CLOSE_SINK_RSP				0x100B

#define SYNC_ENDPOINTS_REQ			0x000C
#define SYNC_ENDPOINTS_RSP			0x100C

#define ENDPOINT_CONFIGURE_REQ			0x000D
#define ENDPOINT_CONFIGURE_RSP			0x100D

#define ENDPOINT_GET_INFO_REQ			0x000E
#define ENDPOINT_GET_INFO_RSP			0x100E

#define CONNECT_REQ				0x000F
#define CONNECT_RSP				0x100F

#define DISCONNECT_REQ				0x0010
#define DISCONNECT_RSP				0x1010

#define CAPABILITY_CODE_DRAM_ADDR_SET_REQ	0x0011
#define CAPABILITY_CODE_DRAM_ADDR_SET_RSP	0x1011

#define CAPABILITY_CODE_DRAM_ADDR_CLEAR_REQ	0x0012
#define CAPABILITY_CODE_DRAM_ADDR_CLEAR_RSP	0x1012

#define DATA_PRODUCED				0x001A
#define DATA_CONSUMED				0x001B

#define GET_VERSION_ID_REQ			0x0013
#define GET_VERSION_ID_RSP			0x1013

#define GET_CAPID_LIST_REQ			0x0014
#define GET_CAPID_LIST_RSP			0x1014

#define GET_OPID_LIST_REQ			0x0015
#define GET_OPID_LIST_RSP			0x1015

#define GET_CONNECTION_LIST_REQ			0x0016
#define GET_CONNECTION_LIST_RSP			0x1016

#define DRAM_ALLOCATION_REQ			0x0017
#define DRAM_ALLOCATION_RSP			0x1017

#define DRAM_FREE_REQ				0x0018
#define DRAM_FREE_RSP				0x1018

/* license messages from design spec */
#define KASCMD_SIGNAL_ID_LICENCE_CHECK_REQ	0x0022
#define KASCMD_SIGNAL_ID_LICENCE_CHECK_RSP	0x1022

#define ENDPOINT_CONF_AUDIO_SAMPLE_RATE		0x0A00
#define ENDPOINT_CONF_AUDIO_DATA_FORMAT		0x0A01
#define ENDPOINT_CONF_DRAM_PACKING_FORMAT	0x0A02
#define ENDPOINT_CONF_INTERLEAVING_MODE		0x0A03
#define ENDPOINT_CONF_CLOCK_MASTER		0x0A04
#define ENDPOINT_CONF_PERIOD_SIZE		0x0A05

#define OPMSG_COMMON_GET_CAPABILITY_VERSION     0x1000
#define OPMSG_COMMON_ENABLE_FADE_OUT            0x2000
#define OPMSG_COMMON_DISABLE_FADE_OUT           0x2001
#define OPMSG_COMMON_SET_CONTROL                0x2002
#define OPMSG_COMMON_GET_PARAMS                 0x2003
#define OPMSG_COMMON_GET_DEFAULTS               0x2004
#define OPMSG_COMMON_SET_PARAMS                 0x2005
#define OPMSG_COMMON_GET_STATUS                 0x2006
#define OPMSG_COMMON_SET_UCID                   0x2007
#define OPMSG_COMMON_GET_LOGICAL_PS_ID          0x2008
#define OPMSG_COMMON_SET_BUFFER_SIZE            0x200C
#define OPMSG_COMMON_SET_TERMINAL_BUFFER_SIZE   0x200D
#define OPMSG_COMMON_SET_SAMPLE_RATE            0x200E
#define OPMSG_COMMON_SET_DATA_STREAM_BASED      0x200F

#define OPMSG_PEQ_SET_COEFFS                    0x0001

#endif /* _KAS_DSP_H */
