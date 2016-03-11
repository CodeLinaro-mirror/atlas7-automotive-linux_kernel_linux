/*
 * @brief
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

#ifndef TX_ITF_DATA_H_
#define TX_ITF_DATA_H_

#include "tx_message.h"

/** DATA Interface Protocol */
#define TX_MPROTO_DATA		(TX_MPROTO_INTERNAL + 2)

/** set or get DMA state 0=off otherwise=on*/
#define TX_MPID_DATA_DMAACTIVE		1
#define TX_MPTY_DATA_DMAACTIVE		TX_MPTYPE_I32

/** set or get DMA number of 512 byte frames per DMA operation*/
#define TX_MPID_DATA_DMAFRAMECNT	2
#define TX_MPTY_DATA_DMAFRAMECNT	TX_MPTYPE_U32

/** set or get DMA timeout in us */
#define TX_MPID_DATA_DMATIMEOUT		3
#define TX_MPTY_DATA_DMATIMEOUT		TX_MPTYPE_U32

/** set or get interface clock frequency */
#define TX_MPID_DATA_CLOCKRATE		4
#define TX_MPTY_DATA_CLOCKRATE		TX_MPTYPE_U32

/* set or get CMD53 addr*/
#define TX_MPID_DATA_CMD53_ADDR		6
#define TX_MPTY_DATA_CMD53_ADDR		TX_MPTYPE_U32

/* set the timer callback interval in ns */
#define TX_MPID_DATA_TIMER_INTVL        7
#define TX_MPTY_DATA_TIMER_INTVL        TX_MPTYPE_U32

/** server signal frame data buffer*/
#define TX_MOID_DATA_SIG_BUFFER		1

/** server signal time stamp of type tx_TimeSpec */
#define TX_MOID_DATA_SIG_TIMESTAMP	2

#endif /* TX_ITF_DATA_H_ */
