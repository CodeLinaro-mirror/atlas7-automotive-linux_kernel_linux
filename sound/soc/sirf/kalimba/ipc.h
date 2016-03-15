/*
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

#ifndef _KAS_IPC_H
#define _KAS_IPC_H

int ipc_send_msg(u16 *msg, int size, u32 need_ack_rsp, u16 *resp);
void ipc_clear_raised_and_send_ack(void);
u32 read_kalimba_reg(u32 reg_addr);
void write_kalimba_reg(u32 reg_addr, u32 val);
void update_bits_kalimba_reg(u32 reg_addr, u32 mask, u32 val);

#define IPC_TRGT3_INIT1_1			0x110
#define IPC_TRGT1_INIT3_1			0x308

#define MSG_NEED_ACK				0x1
#define MSG_NEED_RSP				0x2

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
#define DSP_PS_FILE_BASE_ADDR			0x007FAB

#define FRAME_MAX_SIZE					10
#define FRAME_MAX_START_COMPLETE_DATA_SIZE		(FRAME_MAX_SIZE - 2)
#define FRAME_MAX_CONTINUE_END_DATA_SIZE		(FRAME_MAX_SIZE - 1)

#define ARM_IPC_INTR_TO_KALIMBA			1

#define IPC_SEND_MSG_TO_ARM			0x1
#define IPC_SEND_RSP_TO_ARM			0x2
#define IPC_SEND_NO_COMPLETE			0x3
#define IPC_SEND_ACK_TO_ARM			0x4
#endif /* _KAS_IPC_H */
