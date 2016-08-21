/*
 * kalimba IPC driver
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

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/wait.h>

#include <asm/processor.h>

#include "dsp.h"
#include "firmware.h"
#include "ipc.h"
#include "kerror.h"
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
#define IPC_MSG_RING_SIZE		64

struct ipc_msg {
	u16 payload[64];
};

static DEFINE_KFIFO(ipc_msg_kfifo, struct ipc_msg, IPC_MSG_RING_SIZE);

struct ipc_data {
	u32 irq;
	void __iomem *ipc_base;
	struct regmap *kalimba_regs_regmap;
	struct mutex ipc_comm_mutex;
	struct mutex ipc_send_mutex;
	wait_queue_head_t waitq_dsp_ack;
	wait_queue_head_t waitq_dsp_rsp;
	bool msg_dsp_rsp;
	u16 payload[64];
	struct ipc_msg message;
	struct ipc_msg respone;
	u16 *cur_offs;	/*Current the payload fill offset */

	struct work_struct msg_process_work;
};

static struct ipc_data *ipc_data;

#ifdef CONFIG_TRACING
static struct {
	char *text;
	u16 msg_id;
} msg_text[] = {
	{"Create op", CREATE_OPERATOR_REQ},
	{"Create Extended Operator", CREATE_OPERATOR_EXTENDED_REQ},
	{"Start op", START_OPERATOR_REQ},
	{"Stop op", STOP_OPERATOR_REQ},
	{"Reset op", RESET_OPERATOR_REQ},
	{"Destroy op", DESTROY_OPERATOR_REQ},
	{"Op msg", OPERATOR_MESSAGE_REQ},
	{"Get source", GET_SOURCE_REQ},
	{"Get sink", GET_SINK_REQ},
	{"Close source", CLOSE_SOURCE_REQ},
	{"Close sink", CLOSE_SINK_REQ},
	{"Config ep", ENDPOINT_CONFIGURE_REQ},
	{"Connect", CONNECT_REQ},
	{"Disconnect", DISCONNECT_REQ},
	{"Data produced", DATA_PRODUCED},
	{"Data consumed", DATA_CONSUMED},
	{"Capability addr set", CAPABILITY_CODE_DRAM_ADDR_SET_REQ},
	{"Capability addr clear", CAPABILITY_CODE_DRAM_ADDR_CLEAR_REQ},
	{"Sync endpoint", SYNC_ENDPOINTS_REQ},
	{"System get version id", GET_VERSION_ID_REQ},
	{"System get capid list", GET_CAPID_LIST_REQ},
	{"System get opid list", GET_OPID_LIST_REQ},
	{"System get connection list", GET_CONNECTION_LIST_REQ}
};

static void dump_req_or_rsp(u16 id)
{
	bool rsp = 0;
	int i;

	if (id & 0x1000) {
		rsp = 1;
		id &= 0xfff;
	}
	for (i = 0; i < ARRAY_SIZE(msg_text); i++) {
		if (msg_text[i].msg_id == id) {
			trace_printk("%s %s id=0x%04x\n", msg_text[i].text,
				rsp ? "rsp" : "req", id);
			return;
		}
	}
}
#else
#define dump_req_or_rsp(id)
#endif

u32 read_kalimba_reg(u32 reg_addr)
{
	u32 val;

	regmap_read(ipc_data->kalimba_regs_regmap, reg_addr, &val);
	return val;
}

void write_kalimba_reg(u32 reg_addr, u32 val)
{
	regmap_write(ipc_data->kalimba_regs_regmap, reg_addr, val);
}

void update_bits_kalimba_reg(u32 reg_addr, u32 mask, u32 val)
{
	regmap_update_bits(ipc_data->kalimba_regs_regmap, reg_addr, mask, val);
}

static u32 read_sram(u32 address)
{
	write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR, (address << 2) | (0x2 << 30));
	return read_kalimba_reg(KAS_CPU_KEYHOLE_DATA);
}

static void write_sram(u32 address, u32 value)
{
	write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR,	(address << 2) | (0x2 << 30));
	write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, value);
}

static void increment_counter(u32 address)
{
	u32 counter;

	counter = read_sram(address);
	counter++;
	write_sram(address, counter);
}

/* Clear the flag of DSP interrupt raised, then send a ACK to kalimba */
void ipc_clear_raised_and_send_ack(void)
{
	trace_printk("arm send ack: dsp send: %d, arm ack: %d\n",
		read_sram(DSP_SEND_COUNT_ADDR), read_sram(ARM_ACK_COUNT_ADDR));
	/*
	 * Clear the interrupt raised flag of kalimba,
	 * let the kalimba know next IPC interrupt can be sent.
	 */
	write_sram(DSP_INTR_RAISED_ADDR, 0);
	/* Increnment the ACK counter, then send a ACK single to kalimba */
	increment_counter(ARM_ACK_COUNT_ADDR);
	/* Send IPC intr to kalimba */
	writel(ARM_IPC_INTR_TO_KALIMBA, ipc_data->ipc_base + IPC_TRGT3_INIT1_1);
}

static u32 read_msg_payload(void)
{
	u32 msg_type = 0;
	u32 len;
	u32 i;
	u32 val;

	write_kalimba_reg(KAS_CPU_KEYHOLE_MODE, 4);
	write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR,
			(DSP_MESSAGE_SEND_ADDR << 2) | (0x2 << 30));
	msg_type = read_kalimba_reg(KAS_CPU_KEYHOLE_DATA);
	switch (msg_type) {
	case MESSAGING_SHORT_COMPLETE:
	case MESSAGING_SHORT_START:
		len = read_kalimba_reg(KAS_CPU_KEYHOLE_DATA);
		for (i = 0; i < FRAME_MAX_START_COMPLETE_DATA_SIZE; i++) {
			val = read_kalimba_reg(KAS_CPU_KEYHOLE_DATA);
			ipc_data->payload[i] = (u16)val;
		}
		ipc_data->cur_offs = ipc_data->payload +
				FRAME_MAX_START_COMPLETE_DATA_SIZE;
		trace_printk("rsp_id = 0x%04x\n", ipc_data->payload[0]);
		break;
	case MESSAGING_SHORT_CONTINUE:
	case MESSAGING_SHORT_END:
		for (i = 0; i < FRAME_MAX_CONTINUE_END_DATA_SIZE; i++) {
			val = read_kalimba_reg(KAS_CPU_KEYHOLE_DATA);
			ipc_data->cur_offs[i] = (u16)val;
		}
		ipc_data->cur_offs += FRAME_MAX_CONTINUE_END_DATA_SIZE;
		break;
	default:
		WARN_ON(1);
		msg_type = -EKASMSGTYPE;
		break;
	}
	return msg_type;
}

static int process_ipc_payload(u32 msg_type)
{
	int ret;

	switch (msg_type) {
	case MESSAGING_SHORT_COMPLETE:
	case MESSAGING_SHORT_END:
		if (ipc_data->payload[0] & 0x1000) {
			memcpy(ipc_data->respone.payload, ipc_data->payload,
				64 * sizeof(u16));
			ipc_data->msg_dsp_rsp = true;
			ipc_clear_raised_and_send_ack();
			wake_up(&ipc_data->waitq_dsp_rsp);
			ret = IPC_SEND_RSP_TO_ARM;
		} else {
			memcpy(ipc_data->message.payload, ipc_data->payload,
				64 * sizeof(u16));
			ret = IPC_SEND_MSG_TO_ARM;
		}
		break;
	case MESSAGING_SHORT_CONTINUE:
	case MESSAGING_SHORT_START:
		ipc_clear_raised_and_send_ack();
		ret = IPC_SEND_NO_COMPLETE;
		break;
	default:
		WARN_ON(1);
		ret = -EKASPLD;
		break;
	}
	return ret;
}

static bool is_ipc_ack(void)
{
	u32 arm_send_count;
	u32 dsp_ack_count;

	arm_send_count = read_sram(ARM_SEND_COUNT_ADDR);
	dsp_ack_count = read_sram(DSP_ACK_COUNT_ADDR);
	/*
	 * If the counter of ARM send equal the counter of the DSP ack,
	 * that means the kalimba sends ACKs signal for the messages by
	 * the ARM.
	 */
	if (arm_send_count == dsp_ack_count)
		return true;
	return false;
}

static int ipc_recv_msg_payload_handler(void)
{
	u32 msg_type;
	u32 arm_ack_count;
	u32 dsp_send_count;
	int ret;

	mutex_lock(&ipc_data->ipc_comm_mutex);
	arm_ack_count = read_sram(ARM_ACK_COUNT_ADDR);
	dsp_send_count = read_sram(DSP_SEND_COUNT_ADDR);
	/*
	 * If the counter of ARM ack unequal to the counter of the DSP send,
	 * that means the kalimba sends a message or a response to the ARM.
	 */
	if (arm_ack_count != dsp_send_count) {
		msg_type = read_msg_payload();
		if (msg_type < 0) {
			ret = msg_type;
			pr_info("Error msg_type\n");
			goto out;
		}

		ret = process_ipc_payload(msg_type);
	}
out:
	mutex_unlock(&ipc_data->ipc_comm_mutex);
	return ret;
}

static int check_response(u16 msg_id, u16 resp_id, u16 status)
{
	/*
	 * If the response id is not correct or the status is error code,
	 * then cause the system oops.
	 */
	if ((resp_id != (msg_id | 0x1000)) || status != 0) {
		pr_err("kas response: %s\n", kerror_str(status));
		pr_err("msg id: 0x%04x, resp id: 0x%04x, status: 0x%04x\n",
			msg_id, resp_id, status);
		WARN_ON(1);
		dump_stack();
		return -EKASMSGRSP;
	}

	return 0;
}

static int ipc_send_msg_package(u16 *msg, int size, u16 msg_short_type,
	int total_len, u32 need_ack_rsp)
{
	int i;
	int ret = 0;

	if ((need_ack_rsp & MSG_NEED_ACK) && !is_ipc_ack()) {
		pr_err("The conuts of ARM request and DSP ack are not equal\n");
		return -EKASCRASH;
	}

	write_kalimba_reg(KAS_CPU_KEYHOLE_MODE, 4);
	write_kalimba_reg(KAS_CPU_KEYHOLE_ADDR,
			(ARM_MESSAGE_SEND_ADDR << 2) | (0x2 << 30));

	write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, msg_short_type);

	if (msg_short_type == MESSAGING_SHORT_COMPLETE
			|| msg_short_type == MESSAGING_SHORT_START)
		write_kalimba_reg(KAS_CPU_KEYHOLE_DATA,	(u32)total_len);

	for (i = 0; i < size; i++)
		write_kalimba_reg(KAS_CPU_KEYHOLE_DATA, msg[i]);
	increment_counter(ARM_SEND_COUNT_ADDR);
	writel(ARM_IPC_INTR_TO_KALIMBA, ipc_data->ipc_base + IPC_TRGT3_INIT1_1);
	if (need_ack_rsp & MSG_NEED_ACK) {
		/* Try to check the ACK for 1000 times */
		for (i = 0; i < 1000; i++) {
			if (is_ipc_ack()) {
				break;
			}
			mutex_unlock(&ipc_data->ipc_comm_mutex);
			usleep_range(50, 60);
			mutex_lock(&ipc_data->ipc_comm_mutex);
		}
		if (i == 1000) {
			kcoredump();
			pr_err("Ack from DSP timeout: Maybe Kalimba is down\n");
			WARN_ON(1);
			dump_stack();
			ret = -EKASCRASH;
			goto out;
		}
	}
out:
	return ret;

}

int ipc_send_msg(u16 *msg, int size, u32 need_ack_rsp, u16 *resp)
{
	u16 msg_id;
	int ret = 0;
#ifdef CONFIG_TRACING
	int i;
	char trace_info[512];
#endif
	mutex_lock(&ipc_data->ipc_send_mutex);
	mutex_lock(&ipc_data->ipc_comm_mutex);
	msg_id = msg[0];
	ipc_data->msg_dsp_rsp = false;

#ifdef CONFIG_TRACING
	memset(trace_info, 0, 512);
	for (i = 0; i < size; i++)
		sprintf(trace_info, "%s %04x", trace_info, msg[i]);
	trace_printk("%s\n", trace_info);
#endif

	dump_req_or_rsp(msg_id);

	if (size <= FRAME_MAX_START_COMPLETE_DATA_SIZE) {
		ret = ipc_send_msg_package(msg, size,
				MESSAGING_SHORT_COMPLETE,
				size, need_ack_rsp);
		if (ret < 0)
			goto error;
	} else {
		ret = ipc_send_msg_package(msg,
				FRAME_MAX_START_COMPLETE_DATA_SIZE,
				MESSAGING_SHORT_START,
				size, need_ack_rsp);
		if (ret < 0)
			goto error;
		msg += FRAME_MAX_START_COMPLETE_DATA_SIZE;
		size -= FRAME_MAX_START_COMPLETE_DATA_SIZE;
		while (size > FRAME_MAX_CONTINUE_END_DATA_SIZE) {
			ret = ipc_send_msg_package(
					msg, FRAME_MAX_CONTINUE_END_DATA_SIZE,
					MESSAGING_SHORT_CONTINUE,
					0, need_ack_rsp);
			if (ret < 0)
				goto error;
			msg += FRAME_MAX_CONTINUE_END_DATA_SIZE;
			size -= FRAME_MAX_CONTINUE_END_DATA_SIZE;
		}
		ret = ipc_send_msg_package(msg, size,
				MESSAGING_SHORT_END,
				0, need_ack_rsp);
		if (ret < 0)
			goto error;
	}

	if (need_ack_rsp & MSG_NEED_RSP) {
		mutex_unlock(&ipc_data->ipc_comm_mutex);
		if (!wait_event_timeout(ipc_data->waitq_dsp_rsp,
			ipc_data->msg_dsp_rsp,
			msecs_to_jiffies(IPC_COMM_TIMEOUT))) {
			kcoredump();
			pr_err("RSP from DSP timeout: Maybe Kalimba is down\n");
			ret = -EKASCRASH;
			goto out;
		}
		check_response(msg_id, ipc_data->respone.payload[0],
			ipc_data->respone.payload[2]);
		if (resp)
			memcpy(resp, ipc_data->respone.payload,
				64 * sizeof(u16));
		mutex_lock(&ipc_data->ipc_comm_mutex);
		dump_req_or_rsp(ipc_data->respone.payload[0]);
	}

error:
	mutex_unlock(&ipc_data->ipc_comm_mutex);
out:
	mutex_unlock(&ipc_data->ipc_send_mutex);
	return ret;
}

static irqreturn_t ipc_irq_handler(int irq, void *pdata)
{
	while (1) {
		/* Read from IPC interrupt register will clear the interrupt */
		readl(ipc_data->ipc_base + IPC_TRGT1_INIT3_1);

		/*
		 * If the DSP send counter unequal to  ARM ack counter,
		 * that means the DSP send a message or respone to the ARM
		 * After a message or respone processed, check again.
		 */
		mutex_lock(&ipc_data->ipc_comm_mutex);
		if (read_sram(DSP_SEND_COUNT_ADDR) ==
				read_sram(ARM_ACK_COUNT_ADDR)) {
			mutex_unlock(&ipc_data->ipc_comm_mutex);
			return IRQ_HANDLED;
		}
		mutex_unlock(&ipc_data->ipc_comm_mutex);

		if (ipc_recv_msg_payload_handler() == IPC_SEND_MSG_TO_ARM) {
			if (kfifo_put(&ipc_msg_kfifo, ipc_data->message))
				schedule_work(&ipc_data->msg_process_work);
			else
				pr_err("Kalimba IPC msg buff overflow.\n");
			mutex_lock(&ipc_data->ipc_comm_mutex);
			ipc_clear_raised_and_send_ack();
			mutex_unlock(&ipc_data->ipc_comm_mutex);
		}
	}
}

static const struct regmap_config kalimba_regs_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = KAS_CPU_KEYHOLE_MODE,
	.cache_type = REGCACHE_NONE,
};

static void ipc_msg_process_work(struct work_struct *work)
{
	struct ipc_msg message;

	while (kfifo_get(&ipc_msg_kfifo, &message))
		kalimba_do_actions(message.payload[0], &message.payload[2]);
}

static int ipc_probe(struct platform_device *pdev)
{
	void __iomem *base;
	struct resource *mem_res;
	const struct firmware *fw;
	int ret;

	ipc_data = devm_kzalloc(&pdev->dev, sizeof(*ipc_data), GFP_KERNEL);
	if (ipc_data == NULL)
		return -ENOMEM;

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap(&pdev->dev, mem_res->start,
			resource_size(mem_res));
	if (base == NULL)
		return -ENOMEM;

	ipc_data->kalimba_regs_regmap = devm_regmap_init_mmio(&pdev->dev, base,
			&kalimba_regs_regmap_config);

	if (IS_ERR(ipc_data->kalimba_regs_regmap))
		return PTR_ERR(ipc_data->kalimba_regs_regmap);

	/* Map IPC register space */
	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	ipc_data->ipc_base = devm_ioremap(&pdev->dev, mem_res->start,
			resource_size(mem_res));
	if (ipc_data->ipc_base == NULL)
		return -ENOMEM;

	ipc_data->irq = platform_get_irq(pdev, 0);
	if (ipc_data->irq < 0) {
		dev_err(&pdev->dev, "no IRQ found\n");
		return ipc_data->irq;
	}

	ret = devm_request_threaded_irq(&pdev->dev, ipc_data->irq, NULL,
			ipc_irq_handler,
			IRQF_ONESHOT, pdev->name, NULL);
	if (ret < 0) {
		dev_err(&pdev->dev, "Request the IPC IRQ failed.\n");
		return ret;
	}

	ret = request_firmware(&fw, "kalimba/kalimba.fw", &pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"could not upgrade firmware: unable to load\n");
		return ret;
	}

	firmware_download((u32 *)(fw->data));
	release_firmware(fw);

	init_waitqueue_head(&ipc_data->waitq_dsp_ack);
	init_waitqueue_head(&ipc_data->waitq_dsp_rsp);
	mutex_init(&ipc_data->ipc_comm_mutex);
	mutex_init(&ipc_data->ipc_send_mutex);

	INIT_WORK(&ipc_data->msg_process_work, ipc_msg_process_work);
	return 0;
}

static const struct of_device_id kalimba_ipc_of_match[] = {
	{ .compatible = "csr,kalimba-ipc", },
	{}
};
MODULE_DEVICE_TABLE(of, kalimba_ipc_of_match);

static struct platform_driver kalimba_ipc_driver = {
	.driver = {
		.name = "kalimba-ipc",
		.of_match_table = kalimba_ipc_of_match,
	},
	.probe = ipc_probe,
};

module_platform_driver(kalimba_ipc_driver);

MODULE_DESCRIPTION("SiRF SoC Kalimba DSP IPC driver");
MODULE_LICENSE("GPL v2");
