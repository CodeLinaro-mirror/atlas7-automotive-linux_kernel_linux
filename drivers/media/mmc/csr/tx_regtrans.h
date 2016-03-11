/*
 * @brief Messaging Definitions for Register Transactions
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

#ifndef TX_ITF_REGTRANS_H_
#define TX_ITF_REGTRANS_H_

#include "tx_message.h"

/** Register Transaction protocol */
#define TX_MPROTO_REGT	(TX_MPROTO_INTERNAL + 1)
/** R/W busid:0 and register_address:1 for next block of register values
 * busid (first 16bit value selects which device we want to talk to.
 * For SDIO this is the 3 MSBits of the 17 bit register used to distinguish
 * the chained RGG chips.
 * address is the register offset inside one chip
 * For SDIO the MSBit indicates that function 0 registers shall be adressed
 * instead of Fn1. This is used to initialize the relay chain */
#define TX_MPID_REGT_ADDRESS 1
#define TX_MPTY_REGT_ADDRESS TX_MPTYPE_U16
#define TX_MVAL_REGT_ADDRESS_F0    (1<<15)

/**
 * Function 0 register: r/w  8-Bit values
 * Function 1 register: r/w 16-Bit values
 * R/W block of 8 or 16bit register values
 * partial write to 8bit is supported (TBD)
 */
#define TX_MOID_REGT_DATA 1

/** r/w register RAM-COEFFICIENT */
#define TX_MOID_REGT_COEF 2

/** enables a SDIO function for usage */
#define TX_MPID_REGT_ENABLEFUNC 2
#define TX_MPTY_REGT_ENABLEFUNC TX_MPTYPE_I32

/** */
#define TX_MPID_REGT_REINIT 3
#define TX_MPTY_REGT_REINIT TX_MPTYPE_I32

#endif /* TX_ITF_REGTRANS_H_ */
