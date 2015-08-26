/*
 * kailimba dsp driver for CSR SiRFAtlas7
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

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include "buffer.h"
#ifdef CONFIG_SND_SOC_SIRF_KALIMBA_DEBUG
#include "debug.h"
#endif
#include "dsp.h"
#include "ipc.h"
#include "regs.h"

struct kalimba *kalimba;

void kalimba_create_operator(u16 capability_id, u16 *operator_id, u16 *resp)
{
	u16 msg[3] = {CREATE_OPERATOR_REQ, 1, capability_id};

	ipc_send_msg(msg, 3, MSG_NEED_ACK | MSG_NEED_RSP, resp);
	*operator_id = resp[3];
}

int kalimba_destroy_operator(u16 *operators_id, u16 operator_count, u16 *resp)
{
	int msg_size = 2 + operator_count;
	u16 *msg;
	int i;

	if (operator_count < 1) {
		pr_err("%s: The operator numbers must great than zero: %d\n",
			__func__, operator_count);
		BUG();
	}

	msg = kmalloc_array(msg_size, sizeof(u16), GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = DESTROY_OPERATOR_REQ;
	msg[1] = operator_count;
	for (i = 0; i < operator_count; i++)
		msg[2 + i] = operators_id[i];

	ipc_send_msg(msg, msg_size, MSG_NEED_ACK | MSG_NEED_RSP, resp);
	kfree(msg);

	if (resp[3] != operator_count) {
		pr_err("Operator destroy failed: %d %d\n", operator_count,
			resp[3]);
		pr_err("First failure reason: %x\n", resp[4]);
		BUG();
	}
	return 0;
}

int kalimba_operator_message(u16 operator_id, u16 msg_id, int message_data_len,
	u16 *msg_data, u16 **res_msg_data, u16 *rsp_msg_len, u16 *resp)
{
	int msg_size = 2 + 2 + message_data_len;
	u16 *msg;
	int i;

	msg = kmalloc_array(msg_size, sizeof(u16), GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = OPERATOR_MESSAGE_REQ;
	msg[1] = 2 + message_data_len;
	msg[2] = operator_id;
	msg[3] = msg_id;

	for (i = 0; i < message_data_len; i++)
		msg[4 + i] = msg_data[i];

	ipc_send_msg(msg, msg_size, MSG_NEED_ACK | MSG_NEED_RSP, resp);
	kfree(msg);

	if (res_msg_data != NULL) {
		*rsp_msg_len = resp[1] - 3;
		*res_msg_data = kmalloc_array(*rsp_msg_len,
			sizeof(u16), GFP_KERNEL);
		for (i = 0; i < *rsp_msg_len; i++)
			*res_msg_data[i] = resp[5 + i];
	}
	return 0;
}

int kalimba_start_operator(u16 *operators_id, u16 operator_count, u16 *resp)
{
	int msg_size = 2 + operator_count;
	u16 *msg;
	int i;

	if (operator_count < 1) {
		pr_err("%s: The operator numbers must great than zero: %d\n",
			__func__, operator_count);
		BUG();
	}

	msg = kmalloc_array(msg_size, sizeof(u16), GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = START_OPERATOR_REQ;
	msg[1] = operator_count;
	for (i = 0; i < msg[1]; i++)
		msg[2 + i] = operators_id[i];

	ipc_send_msg(msg, msg_size, 0, resp);
	kfree(msg);

	return 0;
}

int kalimba_stop_operator(u16 *operators_id, u16 operator_count, u16 *resp)
{
	int msg_size = 2 + operator_count;
	u16 *msg;
	int i;

	if (operator_count < 1) {
		pr_err("%s: The operator numbers must great than zero: %d\n",
			__func__, operator_count);
		BUG();
	}

	msg = kmalloc_array(msg_size, sizeof(u16), GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = STOP_OPERATOR_REQ;
	msg[1] = operator_count;
	for (i = 0; i < msg[1]; i++)
		msg[2 + i] = operators_id[i];

	ipc_send_msg(msg, msg_size, MSG_NEED_ACK | MSG_NEED_RSP, resp);
	kfree(msg);

	if (resp[3] != operator_count) {
		pr_err("Operator stop failed: %d %d\n",
				operator_count, resp[3]);
		pr_err("First failure reason: %x\n", resp[4]);
		BUG();
	}
	return 0;
}

int kalimba_reset_operator(u16 *operators_id, u16 operator_count, u16 *resp)
{
	int msg_size = 2 + operator_count;
	u16 *msg;
	int i;

	if (operator_count < 1) {
		pr_err("%s: The operator numbers must great than zero: %d\n",
			__func__, operator_count);
		BUG();
	}

	msg = kmalloc_array(msg_size, sizeof(u16), GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = RESET_OPERATOR_REQ;
	msg[1] = operator_count;
	for (i = 0; i < msg[1]; i++)
		msg[2 + i] = operators_id[i];

	ipc_send_msg(msg, msg_size, MSG_NEED_ACK | MSG_NEED_RSP, resp);
	kfree(msg);

	if (resp[3] != operator_count) {
		pr_err("Operator reset failed: %d %d\n",
				operator_count, resp[3]);
		pr_err("First failure reason: %x\n", resp[4]);
		BUG();
	}
	return 0;
}

void kalimba_get_source(u16 endpoint_type, u16 instance_id, u16 channels,
	u32 handle_addr, u16 *endpoint_id, u16 *resp)
{
	int i;
	u16 msg[7] = {GET_SOURCE_REQ, 5, endpoint_type,	instance_id, channels,
		handle_addr & 0xffff, handle_addr >> 16};

	ipc_send_msg(msg, 7, MSG_NEED_ACK | MSG_NEED_RSP, resp);

	if (endpoint_id) {
		for (i = 0; i < channels; i++)
			endpoint_id[i] = resp[3 + i];
	}
}

void kalimba_get_sink(u16 endpoint_type, u16 instance_id, u16 channels,
		u32 handle_addr, u16 *endpoint_id, u16 *resp)
{
	int i;
	u16 msg[7] = {GET_SINK_REQ, 5, endpoint_type,
		instance_id, channels, handle_addr & 0xffff, handle_addr >> 16};

	ipc_send_msg(msg, 7, MSG_NEED_ACK | MSG_NEED_RSP, resp);

	if (endpoint_id) {
		for (i = 0; i < channels; i++)
			endpoint_id[i] = resp[3 + i];
	}
}

void kalimba_config_endpoint(u16 endpoint_id, u16 config_key,
		u32 config_value, u16 *resp)
{
	u16 msg[6] = {ENDPOINT_CONFIGURE_REQ, 4, endpoint_id,
		config_key, config_value & 0xffff, config_value >> 16};

	ipc_send_msg(msg, 6, MSG_NEED_ACK | MSG_NEED_RSP, resp);

}

void kalimba_connect_endpoints(u16 source_endpoint_id, u16 sink_endpoint_id,
		u16 *connect_id, u16 *resp)
{
	u16 msg[4] = {CONNECT_REQ, 2, source_endpoint_id, sink_endpoint_id};

	ipc_send_msg(msg, 4, MSG_NEED_ACK | MSG_NEED_RSP, resp);

	if (connect_id)
		*connect_id = resp[3];
}

int kalimba_close_source(u16 endpoint_count, u16 *endpoint_id, u16 *resp)
{
	u16 *msg;

	msg = kmalloc(4 + endpoint_count * 2, GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = CLOSE_SOURCE_REQ;
	msg[1] = endpoint_count;
	memcpy(&msg[2], endpoint_id, endpoint_count * 2);

	ipc_send_msg(msg, 2 + endpoint_count, MSG_NEED_ACK | MSG_NEED_RSP,
		resp);
	kfree(msg);

	return 0;
}

int kalimba_close_sink(u16 endpoint_count, u16 *endpoint_id, u16 *resp)
{
	u16 *msg;

	msg = kmalloc(4 + endpoint_count * 2, GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = CLOSE_SINK_REQ;
	msg[1] = endpoint_count;
	memcpy(&msg[2], endpoint_id, endpoint_count * 2);

	ipc_send_msg(msg, 2 + endpoint_count, MSG_NEED_ACK | MSG_NEED_RSP,
		resp);
	kfree(msg);

	return 0;
}

int kalimba_disconnect_endpoints(u16 connect_count, u16 *connect_id, u16 *resp)
{
	u16 *msg;

	msg = kmalloc(4 + connect_count * 2, GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	msg[0] = DISCONNECT_REQ;
	msg[1] = connect_count;
	memcpy(&msg[2], connect_id, connect_count * 2);

	ipc_send_msg(msg, 2 + connect_count, MSG_NEED_ACK | MSG_NEED_RSP, resp);
	kfree(msg);

	return 0;
}

void kalimba_data_produced(u16 endpoint_id)
{
	u16 msg[3] = {DATA_PRODUCED, 1, endpoint_id};

	ipc_send_msg(msg, 3, 0, NULL);
}

void kalimba_data_consumed(u16 endpoint_id)
{
	u16 msg[3] = {DATA_CONSUMED, 1, endpoint_id};

	ipc_send_msg(msg, 3, 0, NULL);
}

void kalimba_get_version_id(u32 *version_id, u16 *resp)
{
	u16 msg[2] = {GET_VERSION_ID_REQ, 0};

	ipc_send_msg(msg, 2, MSG_NEED_ACK | MSG_NEED_RSP, resp);

	if (version_id)
		*version_id = resp[3] | (resp[4] << 16);
}

void kalimba_get_capid_list(u16 *capids, u16 *resp)
{
	int capid_num = 0;
	int i;
	u16 msg[2] = {GET_CAPID_LIST_REQ, 0};

	ipc_send_msg(msg, 2, MSG_NEED_ACK | MSG_NEED_RSP, resp);

	capid_num = resp[1] - 1;
	if (capids) {
		for (i = 0; i < capid_num; i++)
			capids[i] = resp[3 + i];
	}
}

void kalimba_get_opid_list(u16 filter, u16 *opids, u16 *capids, u16 *resp)
{
	int num = 0;
	int i;
	u16 msg[3] = {GET_OPID_LIST_REQ, 1, filter};

	ipc_send_msg(msg, 3, MSG_NEED_ACK | MSG_NEED_RSP, resp);

	num = resp[1] - 1;
	if (opids && capids) {
		for (i = 0; i < num / 2; i++) {
			opids[i] = resp[3 + i * 2];
			capids[i] = resp[3 + i * 2 + 1];
		}
	}
}

void kalimba_get_connection_list(u16 source_filter, u16 sink_filter,
		u16 *connection_ids, u16 *source_ids, u16 *sink_ids, u16 *resp)
{
	int num = 0;
	int i;
	u16 msg[4] = {GET_CONNECTION_LIST_REQ, 2, source_filter, sink_filter};

	ipc_send_msg(msg, 4, MSG_NEED_ACK | MSG_NEED_RSP, resp);

	num = resp[1] - 1;
	if (connection_ids && source_ids && sink_ids) {
		for (i = 0; i < num / 3; i++) {
			connection_ids[i] = resp[3 + i * 3];
			source_ids[i] = resp[3 + i * 3 + 1];
			sink_ids[i] = resp[3 + i * 3 + 2];
		}
	}
}

void kalimba_sync_endpoint(u16 endpoint1, u16 endpoint2, u16 *resp)
{
	u16 msg[4] = {SYNC_ENDPOINTS_REQ, 2, endpoint1, endpoint2};

	ipc_send_msg(msg, 4, MSG_NEED_ACK | MSG_NEED_RSP, resp);
}

void kalimba_get_endpoint_info(u16 endpoint_id, u16 configure_key, u16 *resp)
{
	u16 msg[4] = {ENDPOINT_GET_INFO_REQ, 2, endpoint_id, configure_key};

	ipc_send_msg(msg, 4, MSG_NEED_ACK | MSG_NEED_RSP, resp);
}

void *register_kalimba_msg_action(u16 message,
		void (*handler)(u16, void *, u16 *), void *priv_data)
{
	struct kalimba_msg_action *action;

	action = kmalloc(sizeof(struct kalimba_msg_action), GFP_KERNEL);
	action->message = message;
	action->handler = handler;
	action->priv_data = priv_data;

	list_add(&action->node, &kalimba->kalimba_msg_action_list);
	return action;
}

void unregister_kalimba_msg_action(void *action_id)
{
	struct kalimba_msg_action *action;

	list_for_each_entry(action, &kalimba->kalimba_msg_action_list, node) {
		if (action == action_id) {
			list_del(&action->node);
			kfree(action);
			return;
		}
	}
}

static void unregister_kalimba_msg_all_actions(void)
{
	struct kalimba_msg_action *action;

	list_for_each_entry(action, &kalimba->kalimba_msg_action_list, node) {
		list_del(&action->node);
		kfree(action);
	}
}

void kalimba_do_actions(u16 message, u16 *data)
{
	struct kalimba_msg_action *action;

	list_for_each_entry(action, &kalimba->kalimba_msg_action_list, node) {
		if (action->message == message)
			action->handler(message, action->priv_data, data);
	}
}

void kalimba_msg_send_lock(void)
{
	mutex_lock(&kalimba->msg_send_mutex);
}

void kalimba_msg_send_unlock(void)
{
	mutex_unlock(&kalimba->msg_send_mutex);
}

static int kalimba_probe(struct platform_device *pdev)
{
	int ret;

	kalimba = devm_kzalloc(&pdev->dev, sizeof(struct kalimba),
			GFP_KERNEL);
	if (kalimba == NULL)
		return -ENOMEM;

	kalimba->clk_kas = devm_clk_get(&pdev->dev, "kas_kas");
	if (IS_ERR(kalimba->clk_kas)) {
		dev_err(&pdev->dev, "Get clock(kas) failed.\n");
		return PTR_ERR(kalimba->clk_kas);
	}
	ret = clk_prepare_enable(kalimba->clk_kas);
	if (ret) {
		dev_err(&pdev->dev, "Enable clock(kas) failed.\n");
		return ret;
	}

	kalimba->clk_audmscm = devm_clk_get(&pdev->dev, "audmscm_nocd");
	if (IS_ERR(kalimba->clk_audmscm)) {
		dev_err(&pdev->dev, "Get clock(audmscm) failed.\n");
		ret = PTR_ERR(kalimba->clk_audmscm);
		goto clk_get_audmscm_failed;
	}
	ret = clk_prepare_enable(kalimba->clk_audmscm);
	if (ret) {
		dev_err(&pdev->dev, "Enable clock(audmscm failed.\n");
		goto clk_get_audmscm_failed;
	}

	kalimba->clk_gpum = devm_clk_get(&pdev->dev, "gpum_nocd");
	if (IS_ERR(kalimba->clk_gpum)) {
		dev_err(&pdev->dev, "Get clock(gpum) failed.\n");
		ret = PTR_ERR(kalimba->clk_gpum);
		goto clk_get_gpum_failed;
	}
	ret = clk_prepare_enable(kalimba->clk_gpum);
	if (ret) {
		dev_err(&pdev->dev, "Enable clock(gpum failed.\n");
		goto clk_get_gpum_failed;
	}

	ret = device_reset(&pdev->dev);
	if (ret != 0) {
		dev_err(&pdev->dev, "Reset kalimba failed: %d\n", ret);
		goto kalimba_reset_failed;
	}

	INIT_LIST_HEAD(&kalimba->kalimba_msg_action_list);
	mutex_init(&kalimba->msg_send_mutex);
	platform_set_drvdata(pdev, kalimba);

#ifdef CONFIG_SND_SOC_SIRF_KALIMBA_DEBUG
	ret = debug_init();
	if (ret != 0) {
		dev_err(&pdev->dev, "Initialize debug interface failed.\n");
		goto kalimba_reset_failed;
	}
#endif
	return 0;

kalimba_reset_failed:
	clk_disable_unprepare(kalimba->clk_gpum);
clk_get_gpum_failed:
	clk_disable_unprepare(kalimba->clk_audmscm);
clk_get_audmscm_failed:
	clk_disable_unprepare(kalimba->clk_kas);
	return ret;
}

static int kalimba_remove(struct platform_device *pdev)
{
	struct kalimba *kalimba = platform_get_drvdata(pdev);

	unregister_kalimba_msg_all_actions();
	clk_disable_unprepare(kalimba->clk_gpum);
	clk_disable_unprepare(kalimba->clk_audmscm);
	clk_disable_unprepare(kalimba->clk_kas);

#ifdef CONFIG_SND_SOC_SIRF_KALIMBA_DEBUG
	debug_deinit();
#endif
	return 0;
}

static const struct of_device_id kalimba_of_match[] = {
	{ .compatible = "csr,kalimba", },
	{}
};
MODULE_DEVICE_TABLE(of, kalimba_of_match);

static struct platform_driver kalimba_driver = {
	.driver = {
		.name = "kalimba",
		.of_match_table = kalimba_of_match,
	},
	.probe = kalimba_probe,
	.remove = kalimba_remove,
};

module_platform_driver(kalimba_driver);

MODULE_DESCRIPTION("SiRF SoC Kalimba DSP driver");
MODULE_LICENSE("GPL v2");
