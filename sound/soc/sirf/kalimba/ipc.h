#ifndef _KAS_IPC_H
#define _KAS_IPC_H

#include <linux/platform_device.h>

enum ipc_operator_state {
	OPERATOR_STARTING,
	OPERATOR_STARTED,
	OPERATOR_STOPPING,
	OPERATOR_STOPPED,
};

struct endpoint_handle {
	u32 buff_addr;
	u32 buff_length;
	u32 read_pointer;
	u32 write_pointer;
};

struct ipc_action {
	struct list_head node;	/* Link to other ipc_action */
	u32 message;
	void *priv_data;
	void (*handler)(u32, void *, u32 *);
};

struct ipc_data {
	u32 irq;
	bool debug;
	void __iomem *base;
	struct regmap *regmap;
	struct device *dev;
	struct mutex ipc_comm_mutex;
	struct mutex msg_send_mutex;
	wait_queue_head_t waitq_dsp_ack;
	bool msg_send_ack;
	wait_queue_head_t waitq_dsp_rsp;
	bool msg_dsp_rsp;
	enum ipc_operator_state op_state;
	u32 payload[64];
	u32 *cur_offs;	/*Current the payload fill offset */
	struct list_head actions;	/* ipc_action list */
};

void *request_ipc(struct ipc_data *ipc_data, u32 message,
		void (*handler)(u32, void *, u32 *), void *priv_data);
void free_ipc(struct ipc_data *ipc_data, void *action_id);

struct ipc_data *ipc_get_data(void);
int ipc_init(struct platform_device *pdev);
int ipc_create_operator(struct ipc_data *ipc_data,
		u16 capability_id, u16 *operator_id);
int ipc_start_operator(struct ipc_data *ipc_data,
		u16 *operators_id, u16 operator_count);
int ipc_stop_operator(struct ipc_data *ipc_data,
		u16 *operators_id, u16 operator_count);
int ipc_reset_operator(struct ipc_data *ipc_data,
		u16 *operators_id, u16 operator_count);
int ipc_destroy_operator(struct ipc_data *ipc_data,
		u16 *operators_id, u16 operator_count);
int ipc_operator_message(struct ipc_data *ipc_data,
		u16 operator_id, u16 msg_id,
		int message_data_len, u16 *msg_data, u16 **res_msg_data,
		u16 *rsp_msg_len);
int ipc_get_source(struct ipc_data *ipc_data,
		u16 endpoint_type, u16 instance_id,
		u16 channels, u32 handle_addr,
		u16 *endpoint_id);
int ipc_get_sink(struct ipc_data *ipc_data,
		u16 endpoint_type, u16 instance_id,
		u16 channels, u32 handle_addr,
		u16 *endpoint_id);
int ipc_config_endpoint(struct ipc_data *ipc_data,
		u16 endpoint_id, u16 config_key, u32 config_value);
int ipc_close_source(struct ipc_data *ipc_data,
		u16 endpoint_count, u16 *endpoint_id);
int ipc_close_sink(struct ipc_data *ipc_data,
		u16 endpoint_count, u16 *endpoint_id);
int ipc_connect_endpoints(struct ipc_data *ipc_data,
		u16 source_endpoint_id, u16 sink_endpoint_id,
		u16 *connect_id);
int ipc_disconnect_endpoints(struct ipc_data *ipc_data,
		u16 connect_count, u16 *connect_id);
void ipc_data_produced(struct ipc_data *ipc_data,
		u16 endpoint_id);
void ipc_data_consumed(struct ipc_data *ipc_data,
		u16 endpoint_id);
int ipc_get_version_id(struct ipc_data *ipc_data,
		u32 *version_id);
int ipc_get_capid_list(struct ipc_data *ipc_data,
		u16 *capids);
int ipc_get_opid_list(struct ipc_data *ipc_data,
		u16 filter, u16 *opids, u16 *capids);
int ipc_get_connection_list(struct ipc_data *ipc_data,
		u16 source_filter, u16 sink_filter,
		u16 *connection_ids, u16 *source_ids, u16 *sink_ids);
int ipc_sync_endpoint(struct ipc_data *ipc_data,
		u16 endpoint1, u16 endpoint2);
int ipc_get_endpoint_info(struct ipc_data *ipc_data,
		u16 endpoint_id, u16 configure_key);

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

#define CAPABILITY_ID_MONO_PASSTHOUGH		0x0001

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

#define ENDPOINT_CONF_AUDIO_SAMPLE_RATE		0x0A00
#define ENDPOINT_CONF_AUDIO_DATA_FORMAT		0x0A01
#define ENDPOINT_CONF_DRAM_PACKING_FORMAT	0x0A02
#define ENDPOINT_CONF_INTERLEAVING_MODE		0x0A03
#define ENDPOINT_CONF_CLOCK_MASTER		0x0A04
#define ENDPOINT_CONF_PERIOD_SIZE		0x0A05

#define MESSAGING_SHORT_BASE			0x0000
#define MESSAGING_SHORT_COMPLETE		(MESSAGING_SHORT_BASE + 0x3)
#define MESSAGING_SHORT_START			(MESSAGING_SHORT_BASE + 0x2)
#define MESSAGING_SHORT_CONTINUE		(MESSAGING_SHORT_BASE + 0x0)
#define MESSAGING_SHORT_END			(MESSAGING_SHORT_BASE + 0x1)

#define ARM_SEND_COUNT_ADDR			0xFFAF9A
#define ARM_ACK_COUNT_ADDR			0xFFAF9B
#define ARM_MESSAGE_SEND_ADDR			0xFFAF9C
#define DSP_START_OPERATOR_REPS_ADDR		0xFFAFA6

#define DSP_SEND_COUNT_ADDR			0x007F9A
#define DSP_ACK_COUNT_ADDR			0x007F9B
#define DSP_MESSAGE_SEND_ADDR			0x007F9C
#define DSP_INTR_RAISED_ADDR			0x007FA6

#define FRAME_MAX_SIZE					10
#define FRAME_MAX_START_COMPLETE_DATA_SIZE		(FRAME_MAX_SIZE - 2)
#define FRAME_MAX_CONTINUE_END_DATA_SIZE		(FRAME_MAX_SIZE - 1)

#define START_OPERATOR_REPS_INIT_STATUS		0
#define START_OPERATOR_REPS_SUCCESS		1
#define START_OPERATOR_REPS_FAILED		0xff

#define ARM_IPC_INTR_TO_KALIMBA			1

#define IPC_TRGT3_INIT0_1			0x10
#define IPC_TRGT0_INIT3_1			0x300

#endif /* _KAS_IPC_H */
