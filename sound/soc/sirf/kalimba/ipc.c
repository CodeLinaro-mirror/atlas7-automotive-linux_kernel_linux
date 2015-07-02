/*
 * kalimba IPC driver
 *
 * Copyright (c) 2015 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/wait.h>

#include <asm/processor.h>

#include "dsp.h"
#include "ipc.h"
#include "regs.h"

/*
 * Messaging between ARM and Kalimba.
 * - Two shared buffers are used to achieve bi-directional communication.
 *   One for each direction.
 * - Produce/consume are notified through dedicated IPC interrupt. No data
 *   payload can be attached to the interrupt at hardware level.
 * - Synchronization is kept by maintaining two counters for each shared buffer.
 *   Take "Shared Buf #1" and counter "C1", "C2" for example.
 *   o ARM can send message only if (C1 == C2). Otherwise, it has to wait.
 *   o ARM copy message to shared buffer, increment C1, interrupt Kalimba to
 *     indicate a new message.
 *   o Kalimba consumes the message, increment C2, interrupt ARM to indicate
 *     an ACK.
 *
 * +---------------------+---------------------+
 * |      ARM Core       |     Kalimba Core    |
 * |                     |                     |
 * |             +-------+-------+             |
 * |        TX==>| Shared Buf #1 |==>RX        |
 * |             |  (ARM -> KAS) |             |
 * |             +---------------+             |
 * |     write-->|TX Counter(C1) |-->read      |
 * |             +---------------+             |
 * |      read<--|ACK Counter(C2)|<--write     |
 * |             +-------+-------+             |
 * |                     |                     |
 * |                     |                     |
 * |             +-------+-------+             |
 * |        RX<==| Shared Buf #2 |<==TX        |
 * |             |  (ARM <- KAS) |             |
 * |             +---------------+             |
 * |     write-->|  TX Counter   |-->read      |
 * |             +---------------+             |
 * |      read<--|  ACK Counter  |<--write     |
 * |             +-------+-------+             |
 * |                     |                     |
 * |                     |                     |
 * +---------------------+---------------------+
 */

#define IPC_COMM_TIMEOUT		1000
static struct ipc_data *ipc_data;

static struct {
	char *text;
	u16 msg_id;
} msg_text[] = {
	{"Create op", CREATE_OPERATOR_REQ},
	{"Start op", START_OPERATOR_REQ},
	{"Stop op", STOP_OPERATOR_REQ},
	{"Reset op", RESET_OPERATOR_REQ},
	{"Destroy op", DESTROY_OPERATOR_REQ},
	{"op msg", OPERATOR_MESSAGE_REQ},
	{"Get source", GET_SOURCE_REQ},
	{"Get sink", GET_SINK_REQ},
	{"Close source", CLOSE_SOURCE_REQ},
	{"Close sink", CLOSE_SINK_REQ},
	{"Config ep", ENDPOINT_CONFIGURE_REQ},
	{"Connect", CONNECT_REQ},
	{"Disconnect", DISCONNECT_REQ},
	{"Data produced", DATA_PRODUCED},
	{"Data consumed", DATA_CONSUMED},
	{"Sync endpoint", SYNC_ENDPOINTS_REQ},
	{"System get version id", GET_VERSION_ID_REQ},
	{"System get capid list", GET_CAPID_LIST_REQ},
	{"System get opid list", GET_OPID_LIST_REQ},
	{"System get connection list", GET_CONNECTION_LIST_REQ}
};


static void print_req_or_rsp(u16 id)
{
	bool rsp = 0;
	int i;

	if (!ipc_data->debug)
		return;

	if (id & 0x1000) {
		rsp = 1;
		id &= 0xfff;
	}
	for (i = 0; i < ARRAY_SIZE(msg_text); i++) {
		if (msg_text[i].msg_id == id) {
			pr_info("%s %s id=0x%04x\n", msg_text[i].text, rsp ?
					"rsp" : "req", id);
			return;
		}
	}
}
#define DEBUG_MSG_OUTPUT(x) print_req_or_rsp(x)

static u32 read_sram(struct ipc_data *ipc_data, u32 address)
{
	u32 value;

	regmap_write(ipc_data->regmap, KAS_CPU_KEYHOLE_ADDR,
			(address << 2) | (0x2 << 30));
	regmap_read(ipc_data->regmap, KAS_CPU_KEYHOLE_DATA, &value);

	return value;
}

static void write_sram(struct ipc_data *ipc_data, u32 address, u32 value)
{
	regmap_write(ipc_data->regmap, KAS_CPU_KEYHOLE_ADDR,
			(address << 2) | (0x2 << 30));

	regmap_write(ipc_data->regmap, KAS_CPU_KEYHOLE_DATA, value);
}

static void increment_counter(struct ipc_data *ipc_data, u32 address)
{
	u32 counter;

	counter = read_sram(ipc_data, address);
	counter++;
	write_sram(ipc_data, address, counter);
}

/* Clear the flag of DSP interrupt raised, then send a ACK to kalimba */
static void ipc_clear_raised_and_send_ack(struct ipc_data *ipc_data)
{
	if (ipc_data->debug)
		pr_info("arm send ack: dsp send: %d, arm ack: %d\n",
			read_sram(ipc_data, DSP_SEND_COUNT_ADDR),
			read_sram(ipc_data, ARM_ACK_COUNT_ADDR));
	/*
	 * Clear the interrupt raised flag of kalimba,
	 * let the kalimba know next IPC interrupt can be sent.
	 */
	write_sram(ipc_data, DSP_INTR_RAISED_ADDR, 0);
	/* Increnment the ACK counter, then send a ACK single to kalimba */
	increment_counter(ipc_data, ARM_ACK_COUNT_ADDR);
	/* Send IPC intr to kalimba */
	writel(ARM_IPC_INTR_TO_KALIMBA, ipc_data->base + IPC_TRGT3_INIT0_1);
}

static void do_actions(struct ipc_data *ipc_data, u32 message, u32 *data)
{
	struct ipc_action *action;

	list_for_each_entry(action, &ipc_data->actions, node) {
		if (action->message == message)
			action->handler(message, action->priv_data, data);
	}
}

static u32 read_msg_payload(struct ipc_data *ipc_data)
{
	u32 msg_type = 0;
	u32 len;
	u32 i;

	regmap_write(ipc_data->regmap, KAS_CPU_KEYHOLE_MODE, 4);
	regmap_write(ipc_data->regmap, KAS_CPU_KEYHOLE_ADDR,
			(DSP_MESSAGE_SEND_ADDR << 2) | (0x2 << 30));
	regmap_read(ipc_data->regmap, KAS_CPU_KEYHOLE_DATA, &msg_type);
	switch (msg_type) {
	case MESSAGING_SHORT_COMPLETE:
	case MESSAGING_SHORT_START:
		regmap_read(ipc_data->regmap, KAS_CPU_KEYHOLE_DATA, &len);
		for (i = 0; i < FRAME_MAX_START_COMPLETE_DATA_SIZE; i++)
			regmap_read(ipc_data->regmap, KAS_CPU_KEYHOLE_DATA,
					&ipc_data->payload[i]);
		ipc_data->cur_offs = ipc_data->payload +
				FRAME_MAX_START_COMPLETE_DATA_SIZE;
		if (ipc_data->debug)
			pr_info("rsp_id = 0x%04x\n", ipc_data->cur_offs[0]);
		break;
	case MESSAGING_SHORT_CONTINUE:
	case MESSAGING_SHORT_END:
		for (i = 0; i < FRAME_MAX_CONTINUE_END_DATA_SIZE; i++)
			regmap_read(ipc_data->regmap, KAS_CPU_KEYHOLE_DATA,
					&ipc_data->cur_offs[i]);
		ipc_data->cur_offs += FRAME_MAX_CONTINUE_END_DATA_SIZE;
		break;
	default:
		BUG();
	}
	return msg_type;
}

static void process_ipc_payload(struct ipc_data *ipc_data, u32 msg_type)
{
	switch (msg_type) {
	case MESSAGING_SHORT_COMPLETE:
	case MESSAGING_SHORT_END:
		if (ipc_data->payload[0] & 0x1000) {
			ipc_data->msg_dsp_rsp = true;
			wake_up_interruptible(&ipc_data->waitq_dsp_rsp);
		} else {
			do_actions(ipc_data,
				ipc_data->payload[0],
				&ipc_data->payload[2]);
			ipc_clear_raised_and_send_ack(ipc_data);
		}
		break;
	case MESSAGING_SHORT_CONTINUE:
	case MESSAGING_SHORT_START:
		ipc_clear_raised_and_send_ack(ipc_data);
		break;
	default:
		BUG();
	}
}

static irqreturn_t ipc_recv_msg_payload_handler(int irq, void *pdata)
{
	struct ipc_data *ipc_data = (struct ipc_data *)pdata;
	u32 msg_type;
	u32 arm_ack_count;
	u32 dsp_send_count;

	/* Read from IPC interrupt register will clear the interrupt */
	readl(ipc_data->base + IPC_TRGT0_INIT3_1);

	mutex_lock(&ipc_data->ipc_comm_mutex);
	arm_ack_count = read_sram(ipc_data, ARM_ACK_COUNT_ADDR);
	dsp_send_count = read_sram(ipc_data, DSP_SEND_COUNT_ADDR);
	/*
	 * If the counter of ARM ack equal the counter of the DSP send,
	 * that means the kalimba sends ACKs signal for the messages by
	 * the ARM.
	 */
	if (arm_ack_count == dsp_send_count) {
		write_sram(ipc_data, DSP_INTR_RAISED_ADDR, 0);
		ipc_data->msg_send_ack = true;
		wake_up_interruptible(&ipc_data->waitq_dsp_ack);
		mutex_unlock(&ipc_data->ipc_comm_mutex);
		return IRQ_HANDLED;
	}

	msg_type = read_msg_payload(ipc_data);
	process_ipc_payload(ipc_data, msg_type);
	mutex_unlock(&ipc_data->ipc_comm_mutex);
	return IRQ_HANDLED;
}

struct ipc_data *ipc_get_data(void)
{
	return ipc_data;
}
EXPORT_SYMBOL(ipc_get_data);

static ssize_t enable_debug_info_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	if (buf[0] == '1')
		ipc_data->debug = true;
	else if (buf[0] == '0')
		ipc_data->debug = false;

	return len;
}
static DEVICE_ATTR_WO(enable_debug_info);

int ipc_init(struct platform_device *pdev)
{
	int ret;
	struct resource *mem_res;
	struct kalimba *kalimba = platform_get_drvdata(pdev);

	ipc_data = devm_kzalloc(&pdev->dev, sizeof(*ipc_data), GFP_KERNEL);
	if (ipc_data == NULL)
		return -ENOMEM;

	ipc_data->irq = platform_get_irq(pdev, 0);
	if (ipc_data->irq < 0) {
		dev_err(&pdev->dev, "no IRQ found\n");
		return ipc_data->irq;
	}

	/* Map IPC register space */
	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	ipc_data->base = devm_ioremap(&pdev->dev, mem_res->start,
			resource_size(mem_res));
	if (ipc_data->base == NULL)
		return -ENOMEM;

	ret = devm_request_threaded_irq(&pdev->dev, ipc_data->irq, NULL,
			ipc_recv_msg_payload_handler,
			IRQF_ONESHOT, pdev->name, ipc_data);
	if (ret) {
		dev_err(&pdev->dev, "Request the IPC IRQ failed.\n");
		return ret;
	}
	ipc_data->regmap = kalimba->regmap;
	ipc_data->dev = &pdev->dev;
	kalimba->ipc_data = ipc_data;

	mutex_init(&ipc_data->ipc_comm_mutex);
	mutex_init(&ipc_data->msg_send_mutex);
	INIT_LIST_HEAD(&ipc_data->actions);
	init_waitqueue_head(&ipc_data->waitq_dsp_ack);
	init_waitqueue_head(&ipc_data->waitq_dsp_rsp);

	device_create_file(&pdev->dev, &dev_attr_enable_debug_info);
	return 0;
}

static void ipc_send_msg_package(struct ipc_data *ipc_data,
		u16 *msg, int size, u16 msg_short_type, int total_len)
{
	int i;

	ipc_data->msg_send_ack = false;
	regmap_write(ipc_data->regmap, KAS_CPU_KEYHOLE_MODE, 4);
	regmap_write(ipc_data->regmap, KAS_CPU_KEYHOLE_ADDR,
			(ARM_MESSAGE_SEND_ADDR << 2) | (0x2 << 30));

	regmap_write(ipc_data->regmap, KAS_CPU_KEYHOLE_DATA,
			msg_short_type);

	if (msg_short_type == MESSAGING_SHORT_COMPLETE
			|| msg_short_type == MESSAGING_SHORT_START)
		regmap_write(ipc_data->regmap, KAS_CPU_KEYHOLE_DATA,
				(u32)total_len);

	for (i = 0; i < size; i++)
		regmap_write(ipc_data->regmap, KAS_CPU_KEYHOLE_DATA,
				msg[i]);
	increment_counter(ipc_data, ARM_SEND_COUNT_ADDR);
	writel(ARM_IPC_INTR_TO_KALIMBA, ipc_data->base + IPC_TRGT3_INIT0_1);
	if ((msg[0] != DATA_PRODUCED && msg[0] != DATA_CONSUMED
		&& msg[0] != START_OPERATOR_REQ)) {
		mutex_unlock(&ipc_data->ipc_comm_mutex);
		if (!wait_event_interruptible_timeout(ipc_data->waitq_dsp_ack,
			ipc_data->msg_send_ack,
			msecs_to_jiffies(IPC_COMM_TIMEOUT)))
			dev_err(ipc_data->dev, "Ack from DSP timeout - Maybe Kalimba is down\n");
		mutex_lock(&ipc_data->ipc_comm_mutex);
	}
}

static void ipc_send_msg(struct ipc_data *ipc_data, u16 *msg, int size)
{
	int i;

	ipc_data->msg_dsp_rsp = false;

	if (ipc_data->debug) {
		for (i = 0; i < size; i++)
			pr_info("%04x ", msg[i]);
		pr_info("\n");
	}

	DEBUG_MSG_OUTPUT(msg[0]);
	if (size <= FRAME_MAX_START_COMPLETE_DATA_SIZE)
		ipc_send_msg_package(ipc_data,
				msg, size, MESSAGING_SHORT_COMPLETE, size);
	else {
		ipc_send_msg_package(ipc_data, msg,
				FRAME_MAX_START_COMPLETE_DATA_SIZE,
				MESSAGING_SHORT_START, size);
		msg += FRAME_MAX_START_COMPLETE_DATA_SIZE;
		size -= FRAME_MAX_START_COMPLETE_DATA_SIZE;
		while (size > FRAME_MAX_CONTINUE_END_DATA_SIZE) {
			ipc_send_msg_package(ipc_data,
					msg, FRAME_MAX_CONTINUE_END_DATA_SIZE,
					MESSAGING_SHORT_CONTINUE, 0);
			msg += FRAME_MAX_CONTINUE_END_DATA_SIZE;
			size -= FRAME_MAX_CONTINUE_END_DATA_SIZE;
		}
		ipc_send_msg_package(ipc_data,
				msg, size, MESSAGING_SHORT_END, 0);
	}
	if (msg[0] != DATA_PRODUCED && msg[0] != DATA_CONSUMED
		&& msg[0] != START_OPERATOR_REQ) {
		mutex_unlock(&ipc_data->ipc_comm_mutex);
		if (!wait_event_interruptible_timeout(ipc_data->waitq_dsp_rsp,
			ipc_data->msg_dsp_rsp,
			msecs_to_jiffies(IPC_COMM_TIMEOUT)))
			dev_err(ipc_data->dev, "RSP from DSP timeout - Maybe Kalimba is down\n");
		mutex_lock(&ipc_data->ipc_comm_mutex);
		DEBUG_MSG_OUTPUT(ipc_data->payload[0]);
	}
}

int ipc_create_operator(struct ipc_data *ipc_data,
		u16 capability_id, u16 *operator_id)
{
	int ret = 0;
	u16 msg[3] = {CREATE_OPERATOR_REQ, 1, capability_id};

	mutex_lock(&ipc_data->ipc_comm_mutex);
	ipc_send_msg(ipc_data, msg, 3);
	if (ipc_data->payload[0] != CREATE_OPERATOR_RSP
			|| ipc_data->payload[2] != 0) {
		ret =  -EINVAL;
		goto out;
	}
	*operator_id = ipc_data->payload[3];
out:
	ipc_clear_raised_and_send_ack(ipc_data);
	ipc_data->op_state = OPERATOR_STOPPED;
	mutex_unlock(&ipc_data->ipc_comm_mutex);
	return ret;
}

int ipc_start_operator(struct ipc_data *ipc_data,
		u16 *operators_id, u16 operator_count)
{
	int msg_size = 2 + operator_count;
	u16 *msg;
	int i;
	u32 resp;
	int ret = 0;

	if (operator_count < 1)
		return -EINVAL;

	ipc_data->op_state = OPERATOR_STARTING;

	msg = kmalloc_array(msg_size, sizeof(u16), GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = START_OPERATOR_REQ;
	msg[1] = operator_count;
	for (i = 0; i < msg[1]; i++)
		msg[2 + i] = operators_id[i];

	mutex_lock(&ipc_data->ipc_comm_mutex);
	/* Clear the flag of start_operator_respond,
	 * - Value START_OPERATOR_REPS_SUCCESS is used to indicate
	 *   that the command was successfully executed.
	 * - Value START_OPERATOR_REPS_FAILED is used to indicate
	 *   that the command failed.
	 */
	write_sram(ipc_data, DSP_START_OPERATOR_REPS_ADDR,
		START_OPERATOR_REPS_INIT_STATUS);
	ipc_send_msg(ipc_data, msg, msg_size);
	kfree(msg);

	/* Wait the result of start operatore command */
	do {
		cpu_relax();
		resp = read_sram(ipc_data, DSP_START_OPERATOR_REPS_ADDR);
	} while (resp == START_OPERATOR_REPS_INIT_STATUS);

	if (resp == START_OPERATOR_REPS_FAILED) {
		ipc_data->op_state = OPERATOR_STOPPED;
		ret = -EINVAL;
	} else
		ipc_data->op_state = OPERATOR_STARTED;
	mutex_unlock(&ipc_data->ipc_comm_mutex);
	return ret;
}

int ipc_stop_operator(struct ipc_data *ipc_data,
		u16 *operators_id, u16 operator_count)
{
	int msg_size = 2 + operator_count;
	u16 *msg;
	int i;
	int ret = 0;

	if (operator_count < 1)
		return -EINVAL;

	ipc_data->op_state = OPERATOR_STOPPING;
	msg = kmalloc_array(msg_size, sizeof(u16), GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = STOP_OPERATOR_REQ;
	msg[1] = operator_count;
	for (i = 0; i < msg[1]; i++)
		msg[2 + i] = operators_id[i];

	mutex_lock(&ipc_data->ipc_comm_mutex);
	ipc_send_msg(ipc_data, msg, msg_size);
	kfree(msg);

	if (ipc_data->payload[0] != STOP_OPERATOR_RSP
			|| ipc_data->payload[2] != 0) {
		ret = -EINVAL;
		goto out;
	}

	if (ipc_data->payload[3] != operator_count) {
		dev_err(ipc_data->dev, "Operator stop failed: %d %d\n",
				operator_count, ipc_data->payload[3]);
		dev_err(ipc_data->dev, "First failure reason: %x\n",
				ipc_data->payload[4]);
		ret = -EINVAL;
	}
out:
	ipc_clear_raised_and_send_ack(ipc_data);
	if (ret < 0)
		ipc_data->op_state = OPERATOR_STARTED;
	else
		ipc_data->op_state = OPERATOR_STOPPED;
	mutex_unlock(&ipc_data->ipc_comm_mutex);
	return ret;
}

int ipc_reset_operator(struct ipc_data *ipc_data,
		u16 *operators_id, u16 operator_count)
{
	int msg_size = 2 + operator_count;
	u16 *msg;
	int i;
	int ret = 0;

	if (operator_count < 1)
		return -EINVAL;

	msg = kmalloc_array(msg_size, sizeof(u16), GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = RESET_OPERATOR_REQ;
	msg[1] = operator_count;
	for (i = 0; i < msg[1]; i++)
		msg[2 + i] = operators_id[i];

	mutex_lock(&ipc_data->ipc_comm_mutex);
	ipc_send_msg(ipc_data, msg, msg_size);
	kfree(msg);
	if (ipc_data->payload[0] != RESET_OPERATOR_RSP
			|| ipc_data->payload[2] != 0) {
		ret = -EINVAL;
		goto out;
	}

	if (ipc_data->payload[3] != operator_count) {
		dev_err(ipc_data->dev, "Operator reset failed: %d %d\n",
				operator_count, ipc_data->payload[3]);
		dev_err(ipc_data->dev, "First failure reason: %x\n",
				ipc_data->payload[4]);
		ret = -EINVAL;
	}
out:
	ipc_clear_raised_and_send_ack(ipc_data);
	mutex_unlock(&ipc_data->ipc_comm_mutex);
	return ret;
}

int ipc_destroy_operator(struct ipc_data *ipc_data,
		u16 *operators_id, u16 operator_count)
{
	int msg_size = 2 + operator_count;
	u16 *msg;
	int i;
	int ret = 0;

	if (operator_count < 1)
		return -EINVAL;

	msg = kmalloc_array(msg_size, sizeof(u16), GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = DESTROY_OPERATOR_REQ;
	msg[1] = operator_count;
	for (i = 0; i < operator_count; i++)
		msg[2 + i] = operators_id[i];

	mutex_lock(&ipc_data->ipc_comm_mutex);
	ipc_send_msg(ipc_data, msg, msg_size);
	kfree(msg);
	if (ipc_data->payload[0] != DESTROY_OPERATOR_RSP
			|| ipc_data->payload[2] != 0) {
		ret = -EINVAL;
		goto out;
	}

	if (ipc_data->payload[3] != operator_count) {
		dev_err(ipc_data->dev, "Operator destroy failed: %d %d\n",
				operator_count, ipc_data->payload[3]);
		dev_err(ipc_data->dev, "First failure reason: %x\n",
				ipc_data->payload[4]);
		ret = -EINVAL;
	}
out:
	ipc_clear_raised_and_send_ack(ipc_data);
	mutex_unlock(&ipc_data->ipc_comm_mutex);
	return ret;
}

int ipc_operator_message(struct ipc_data *ipc_data,
		u16 operator_id, u16 msg_id,
		int message_data_len, u16 *msg_data, u16 **res_msg_data,
		u16 *rsp_msg_len)
{
	int msg_size = 2 + 2 + message_data_len;
	u16 *msg;
	int i;
	int ret = 0;

	msg = kmalloc_array(msg_size, sizeof(u16), GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = OPERATOR_MESSAGE_REQ;
	msg[1] = 2 + message_data_len;
	msg[2] = operator_id;
	msg[3] = msg_id;

	for (i = 0; i < message_data_len; i++)
		msg[4 + i] = msg_data[i];

	mutex_lock(&ipc_data->ipc_comm_mutex);
	ipc_send_msg(ipc_data, msg, msg_size);
	kfree(msg);

	if (ipc_data->payload[0] != OPERATOR_MESSAGE_RSP
			|| ipc_data->payload[2] != 0) {
		ret = -EINVAL;
		goto out;
	}

	if (res_msg_data != NULL) {
		*rsp_msg_len = ipc_data->payload[1] - 3;
		*res_msg_data = kmalloc_array(*rsp_msg_len,
			sizeof(u16), GFP_KERNEL);
		for (i = 0; i < *rsp_msg_len; i++)
			*res_msg_data[i] = ipc_data->payload[5 + i];
	}
out:
	ipc_clear_raised_and_send_ack(ipc_data);
	mutex_unlock(&ipc_data->ipc_comm_mutex);
	return ret;
}

int ipc_get_source(struct ipc_data *ipc_data,
		u16 endpoint_type, u16 instance_id,
		u16 channels, u32 handle_addr,
		u16 *endpoint_id)
{
	int i;
	int ret = 0;
	u16 msg[7] = {GET_SOURCE_REQ, 5, endpoint_type,	instance_id, channels,
		handle_addr & 0xffff, handle_addr >> 16};

	mutex_lock(&ipc_data->ipc_comm_mutex);
	ipc_send_msg(ipc_data, msg, 7);
	if (ipc_data->payload[0] != GET_SOURCE_RSP
			|| ipc_data->payload[2] != 0) {
		pr_err("%s: error code: 0x%x\n", __func__,
				ipc_data->payload[2]);
		ret = -EINVAL;
		goto out;
	}

	if (endpoint_id) {
		for (i = 0; i < channels; i++)
			endpoint_id[i] = ipc_data->payload[3 + i];
	}
out:
	ipc_clear_raised_and_send_ack(ipc_data);
	mutex_unlock(&ipc_data->ipc_comm_mutex);
	return ret;
}

int ipc_get_sink(struct ipc_data *ipc_data,
		u16 endpoint_type, u16 instance_id,
		u16 channels, u32 handle_addr,
		u16 *endpoint_id)
{
	int i;
	int ret = 0;
	u32 *handle_virt;
	u16 msg[7] = {GET_SINK_REQ, 5, endpoint_type,
		instance_id, channels, handle_addr & 0xffff, handle_addr >> 16};

	handle_virt = phys_to_virt(handle_addr);
	mutex_lock(&ipc_data->ipc_comm_mutex);
	ipc_send_msg(ipc_data, msg, 7);
	if (ipc_data->payload[0] != GET_SINK_RSP
			|| ipc_data->payload[2] != 0) {
		ret = -EINVAL;
		goto out;
	}

	if (endpoint_id) {
		for (i = 0; i < channels; i++)
			endpoint_id[i] = ipc_data->payload[3 + i];
	}
out:
	ipc_clear_raised_and_send_ack(ipc_data);
	mutex_unlock(&ipc_data->ipc_comm_mutex);
	return ret;
}

int ipc_config_endpoint(struct ipc_data *ipc_data,
		u16 endpoint_id, u16 config_key, u32 config_value)
{
	int ret = 0;
	u16 msg[6] = {ENDPOINT_CONFIGURE_REQ, 4, endpoint_id,
		config_key, config_value & 0xffff, config_value >> 16};

	mutex_lock(&ipc_data->ipc_comm_mutex);
	ipc_send_msg(ipc_data, msg, 6);
	if (ipc_data->payload[0] != ENDPOINT_CONFIGURE_RSP
			|| ipc_data->payload[2] != 0)
		ret = -EINVAL;

	ipc_clear_raised_and_send_ack(ipc_data);
	mutex_unlock(&ipc_data->ipc_comm_mutex);
	return ret;
}

int ipc_close_source(struct ipc_data *ipc_data,
		u16 endpoint_count, u16 *endpoint_id)
{
	u16 *msg;
	int ret = 0;

	msg = kmalloc(4 + endpoint_count * 2, GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = CLOSE_SOURCE_REQ;
	msg[1] = endpoint_count;
	memcpy(&msg[2], endpoint_id, endpoint_count * 2);

	mutex_lock(&ipc_data->ipc_comm_mutex);
	ipc_send_msg(ipc_data, msg, 2 + endpoint_count);
	kfree(msg);
	if (ipc_data->payload[0] != CLOSE_SOURCE_RSP
			|| ipc_data->payload[2] != 0)
		ret = -EINVAL;

	ipc_clear_raised_and_send_ack(ipc_data);
	mutex_unlock(&ipc_data->ipc_comm_mutex);
	return ret;
}

int ipc_close_sink(struct ipc_data *ipc_data,
		u16 endpoint_count, u16 *endpoint_id)
{
	u16 *msg;
	int ret = 0;

	msg = kmalloc(4 + endpoint_count * 2, GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = CLOSE_SINK_REQ;
	msg[1] = endpoint_count;
	memcpy(&msg[2], endpoint_id, endpoint_count * 2);

	mutex_lock(&ipc_data->ipc_comm_mutex);
	ipc_send_msg(ipc_data, msg, 2 + endpoint_count);
	kfree(msg);
	if (ipc_data->payload[0] != CLOSE_SINK_RSP
			|| ipc_data->payload[2] != 0)
		ret = -EINVAL;

	ipc_clear_raised_and_send_ack(ipc_data);
	mutex_unlock(&ipc_data->ipc_comm_mutex);
	return ret;
}

int ipc_connect_endpoints(struct ipc_data *ipc_data,
		u16 source_endpoint_id, u16 sink_endpoint_id,
		u16 *connect_id)
{
	int ret = 0;
	u16 msg[4] = {CONNECT_REQ, 2, source_endpoint_id, sink_endpoint_id};

	mutex_lock(&ipc_data->ipc_comm_mutex);
	ipc_send_msg(ipc_data, msg, 4);
	if (ipc_data->payload[0] != CONNECT_RSP
			|| ipc_data->payload[2] != 0) {
		ret = -EINVAL;
		goto out;
	}

	if (connect_id)
		*connect_id = ipc_data->payload[3];
out:
	ipc_clear_raised_and_send_ack(ipc_data);
	mutex_unlock(&ipc_data->ipc_comm_mutex);
	return ret;
}

int ipc_disconnect_endpoints(struct ipc_data *ipc_data,
		u16 connect_count, u16 *connect_id)
{
	u16 *msg;
	int ret = 0;

	msg = kmalloc(4 + connect_count * 2, GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = DISCONNECT_REQ;
	msg[1] = connect_count;
	memcpy(&msg[2], connect_id, connect_count * 2);

	mutex_lock(&ipc_data->ipc_comm_mutex);
	ipc_send_msg(ipc_data, msg, 2 + connect_count);
	kfree(msg);
	if (ipc_data->payload[0] != DISCONNECT_RSP
			|| ipc_data->payload[2] != 0)
		ret = -EINVAL;

	ipc_clear_raised_and_send_ack(ipc_data);
	mutex_unlock(&ipc_data->ipc_comm_mutex);
	return ret;
}

void ipc_data_produced(struct ipc_data *ipc_data,
		u16 endpoint_id)
{
	u16 msg[3] = {DATA_PRODUCED, 1, endpoint_id};

	mutex_lock(&ipc_data->ipc_comm_mutex);
	ipc_send_msg(ipc_data, msg, 3);
	mutex_unlock(&ipc_data->ipc_comm_mutex);
}

void ipc_data_consumed(struct ipc_data *ipc_data,
		u16 endpoint_id)
{
	u16 msg[3] = {DATA_CONSUMED, 1, endpoint_id};

	mutex_lock(&ipc_data->ipc_comm_mutex);
	ipc_send_msg(ipc_data, msg, 3);
	mutex_unlock(&ipc_data->ipc_comm_mutex);
}

int ipc_get_version_id(struct ipc_data *ipc_data,
		u32 *version_id)
{
	int ret = 0;
	u16 msg[2] = {GET_VERSION_ID_REQ, 0};

	mutex_lock(&ipc_data->ipc_comm_mutex);
	ipc_send_msg(ipc_data, msg, 2);
	if (ipc_data->payload[0] != GET_VERSION_ID_RSP
			|| ipc_data->payload[2] != 0)
		ret = -EINVAL;

	if (version_id)
		*version_id = ipc_data->payload[3] |
			(ipc_data->payload[4] << 16);
	ipc_clear_raised_and_send_ack(ipc_data);
	mutex_unlock(&ipc_data->ipc_comm_mutex);
	return ret;
}

int ipc_get_capid_list(struct ipc_data *ipc_data,
		u16 *capids)
{
	int ret = 0;
	int capid_num;
	int i;
	u16 msg[2] = {GET_CAPID_LIST_REQ, 0};

	mutex_lock(&ipc_data->ipc_comm_mutex);
	ipc_send_msg(ipc_data, msg, 2);
	if (ipc_data->payload[0] != GET_CAPID_LIST_RSP
			|| ipc_data->payload[2] != 0)
		ret = -EINVAL;

	if (capids) {
		capid_num = ipc_data->payload[1] - 1;
		for (i = 0; i < capid_num; i++)
			capids[i] = ipc_data->payload[3 + i];
	}
	ipc_clear_raised_and_send_ack(ipc_data);
	mutex_unlock(&ipc_data->ipc_comm_mutex);
	return ret;
}

int ipc_get_opid_list(struct ipc_data *ipc_data,
		u16 filter, u16 *opids, u16 *capids)
{
	int ret = 0;
	int num;
	int i;
	u16 msg[3] = {GET_OPID_LIST_REQ, 1, filter};

	mutex_lock(&ipc_data->ipc_comm_mutex);
	ipc_send_msg(ipc_data, msg, 3);
	if (ipc_data->payload[0] != GET_OPID_LIST_RSP
			|| ipc_data->payload[2] != 0)
		ret = -EINVAL;

	if (opids && capids) {
		num = ipc_data->payload[1] - 1;
		for (i = 0; i < num / 2; i++) {
			opids[i] = ipc_data->payload[3 + i * 2];
			capids[i] = ipc_data->payload[3 + i * 2 + 1];
		}
	}
	ipc_clear_raised_and_send_ack(ipc_data);
	mutex_unlock(&ipc_data->ipc_comm_mutex);
	return ret;
}

int ipc_get_connection_list(struct ipc_data *ipc_data,
		u16 source_filter, u16 sink_filter,
		u16 *connection_ids, u16 *source_ids, u16 *sink_ids)
{
	int ret = 0;
	int num;
	int i;
	u16 msg[4] = {GET_CONNECTION_LIST_REQ, 2, source_filter, sink_filter};

	mutex_lock(&ipc_data->ipc_comm_mutex);
	ipc_send_msg(ipc_data, msg, 4);
	if (ipc_data->payload[0] != GET_CONNECTION_LIST_RSP
			|| ipc_data->payload[2] != 0)
		ret = -EINVAL;

	if (connection_ids && source_ids && sink_ids) {
		num = ipc_data->payload[1] - 1;
		for (i = 0; i < num / 3; i++) {
			connection_ids[i] = ipc_data->payload[3 +
				i * 3];
			source_ids[i] = ipc_data->payload[3 + i * 3
				+ 1];
			sink_ids[i] = ipc_data->payload[3 + i * 3 + 2];
		}
	}
	ipc_clear_raised_and_send_ack(ipc_data);
	mutex_unlock(&ipc_data->ipc_comm_mutex);
	return ret;
}

int ipc_sync_endpoint(struct ipc_data *ipc_data,
		u16 endpoint1, u16 endpoint2)
{
	int ret = 0;
	u16 msg[4] = {SYNC_ENDPOINTS_REQ, 2, endpoint1, endpoint2};

	mutex_lock(&ipc_data->ipc_comm_mutex);
	ipc_send_msg(ipc_data, msg, 4);
	if (ipc_data->payload[0] != SYNC_ENDPOINTS_RSP
			|| ipc_data->payload[2] != 0)
		ret = -EINVAL;

	ipc_clear_raised_and_send_ack(ipc_data);
	mutex_unlock(&ipc_data->ipc_comm_mutex);
	return ret;
}

int ipc_get_endpoint_info(struct ipc_data *ipc_data,
		u16 endpoint_id, u16 configure_key)
{
	int ret = 0;
	u16 msg[4] = {ENDPOINT_GET_INFO_REQ, 2, endpoint_id, configure_key};

	mutex_lock(&ipc_data->ipc_comm_mutex);
	ipc_send_msg(ipc_data, msg, 4);
	if (ipc_data->payload[0] != ENDPOINT_GET_INFO_REQ
			|| ipc_data->payload[2] != 0)
		ret = -EINVAL;

	ipc_clear_raised_and_send_ack(ipc_data);
	mutex_unlock(&ipc_data->ipc_comm_mutex);
	return ret;
}

void *request_ipc(struct ipc_data *ipc_data, u32 message,
		void (*handler)(u32, void *, u32 *), void *priv_data)
{
	struct ipc_action *action;

	action = kmalloc(sizeof(struct ipc_action), GFP_KERNEL);
	action->message = message;
	action->handler = handler;
	action->priv_data = priv_data;

	list_add(&action->node, &ipc_data->actions);
	return action;
}

void free_ipc(struct ipc_data *ipc_data, void *action_id)
{
	struct ipc_action *action;

	list_for_each_entry(action, &ipc_data->actions, node) {
		if (action == action_id) {
			list_del(&action->node);
			kfree(action);
			return;
		}
	}
}
