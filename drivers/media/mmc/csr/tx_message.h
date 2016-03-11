/*
 * @brief Messaging Definitions
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

#ifndef TX_MESSAGE_H_
#define TX_MESSAGE_H_

#define TX_ERROR	-1 /* unspecified error */
/* message flag definitions */
/**< element caused error */
#define TX_MFLAG_ERROR		(1 << 31)
/**< element to be written */
#define TX_MFLAG_WRITE		(1 << 30)
/**< element to be read, read to be done after write, if both flag are set) */
#define TX_MFLAG_READ		(1 << 29)
/**< data object provided BY REFerence */
#define TX_MFLAG_BYREF		(1 << 28)
/** mask to select flags */
#define TX_MFLAG_MASK \
	(TX_MFLAG_ERROR | TX_MFLAG_WRITE | TX_MFLAG_READ | TX_MFLAG_BYREF)

/* parameter id and type definitions
 type information is optional ! */
#define TX_MPID_MASK	((1 << 24) - 1) /**< mask for parameter ID bits*/
#define TX_MPTYPE_SHIFT	24 /**< shift for parameter type bits*/
#define TX_MPTYPE_MASK	(15 << TX_MPTYPE_SHIFT) /**< mask for parameter type bits*/
#define TX_MPTYPE_I32	0 /**< default - element type tx_I32 */
#define TX_MPTYPE_U32	1 /**< element type tx_U32 */
#define TX_MPTYPE_I16	2 /**< element type 2 x tx_I16 */
#define TX_MPTYPE_U16	3 /**< element type 2 x tx_U16 */
#define TX_MPTYPE_I8	4 /**< element type 4 x tx_I8 */
#define TX_MPTYPE_U8	5 /**< element type 4 x tx_U8 */
#define TX_MPTYPE_F32	6 /**< element type float */
#define TX_MPTYPE_INVALID	7 /**< first invalid type ID maintain for tests!*/

/** calculate id for parameter form type and pid */
#define TX_MP_ID(id, type) \
	(((type << TX_MPTYPE_SHIFT) & TX_MPTYPE_MASK) | (id & TX_MPID_MASK))

/* object id and size definitions */
/**< 19 bit size -> max = 512k */
#define TX_MOID_SHIFT		19
/**< Mask for size of referenced memory, 18 bits*/
#define TX_MOSIZE_MASK		((1 << TX_MOID_SHIFT) - 1)
/**< Mask for Reference ID*/
#define TX_MOID_MASK (~TX_MFLAG_MASK & ~TX_MOSIZE_MASK)

#define TX_MO_ID(id, size)	\
	(TX_MFLAG_BYREF | ((id << TX_MOID_SHIFT) & TX_MOID_MASK)	\
	 | (size & TX_MOSIZE_MASK))

/** test for error flag */
#define TX_MISERR(id)		((id&TX_MFLAG_ERROR) != 0)
/** test for by write flag */
#define TX_MISWRITE(id)		((id&TX_MFLAG_WRITE) != 0)
/** test for by read flag */
#define TX_MISREAD(id)		((id&TX_MFLAG_READ) != 0)
/** test for by reference flag */
#define TX_MISBYREF(id)		((id&TX_MFLAG_BYREF) != 0)
/** get parameter ID from element id, if TX_MFLAG_BYREF is not set!*/
#define TX_MPID(id)		(id & TX_MPID_MASK)
/** get type from element id, if TX_MFLAG_BYREF is not set!*/
#define TX_MPTYPE(id)	((id & TX_MPTYPE_MASK) >> TX_MPTYPE_SHIFT)
/** get object ID from element id, if TX_MFLAG_BYREF is set!*/
#define TX_MOID(id)	((id & TX_MOID_MASK) >> TX_MOID_SHIFT)
/** get object ID from element id, if TX_MFLAG_BYREF is set!*/
#define TX_MOSIZE(id)	(id & TX_MOSIZE_MASK)

/** Allocation of id word
 * ######################################################################
 * #3|3|2|2|2|2|2|2|2|2|2|2|1|1|1|1|1|1|1|1|1|1|0|0|0|0|0|0|0|0|0|0#3..0#
 * #1|0|9|8|7|6|5|4|3|2|1|0|9|8|7|6|5|4|3|2|1|0|9|8|7|6|5|4|3|2|1|0#1..0#
 * #-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-######
 * #E|W|R|0|<type->|<------------------PID------------------------># val#
 * #-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-######
 * #E|W|R|1|<------OID------>|<------------object--size-----------># ptr#
 * ######################################################################
 */

/**
 * Message Parameter Union
 * Parameter Values are always 32bit and will be interpreted
 * as one of these types depending on the ptype bits in the element ID
 */
union tx_message_elementValue {
	int i32val; /**< default type signed 32 bit integer - TX_MPTYPE_I32 */
	u32 u32val; /**< unsigned 32 bit integer - TX_MPTYPE_U32 */
	short i16val[2]; /**array of 2 signed 16 bit integers - TX_MPTYPE_I16 */
	u16 u16val[2]; /**array of 2 unsigned 16 bit integers - TX_MPTYPE_U16 */
	char i8val[4]; /**array of 4 signed 8 bit integers - TX_MPTYPE_I8 */
	u8 u8val[4]; /**< array of 4 unigned 8 bit integers - TX_MPTYPE_U8 */
	float f32; /**< 32 bit float - TX_MPTYPE_F32 */
	void *ptr; /**< byref - NO PARAMETER TYPE */
};

/**
 * Message Element Structure
 * A Message is an array of these elements
 * */

struct tx_message_element {
	int id;
	union tx_message_elementValue val;
};

/**
 * Message Structure
 * Messages have count elements and are modified in place on execution
 * Memory referenced by a message (BYREF values) must be provided by the
 * calling context, similar to normal C calls.
 * Messages are processed IN PLACE, that means read values are written to
 * message elements (in case of parameters) or memory buffers referenced
 * by the elements.
 * */
struct tx_message {
	u32 count; /**< number of elements in message */
	/* count must be the first field as it overlaps
	   with the transport header*/
	int error; /**< return value of msg execution */
	struct tx_message_element elements[1];
};

#define TX_MSGSZ_WIRE 16 /* including 1 element */
#define TX_MSGELEMSZ_WIRE 8
/** macro to calculate total size of a message having nelem elements */
#define TX_MSGSIZE_MEM(nelem) (sizeof(struct tx_message)\
	+(nelem-1)*sizeof(struct tx_message_element))
#define TX_MSGSIZE_WIRE(nelem) (TX_MSGSZ_WIRE\
		+(nelem-1)*TX_MSGELEMSZ_WIRE)

#endif /* TX_MESSAGE_H_ */
