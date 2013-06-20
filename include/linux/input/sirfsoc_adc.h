/*
 * inclue/linux/adc.h
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __SIRFSOC_ADC_H
#define __SIRFSOC_ADC_H

#define DATA_SHIFT_BITS		14

#define ADC_CONTROL1		(0x00)
#define ADC_CONTROL2		(0x04)
#define ADC_INTR		(0x08)
#define ADC_COORD		(0x0C)
#define ADC_PRESSURE		(0x10)
#define ADC_AUX0		(0x14)
#define ADC_AUX1		(0x18)
#define ADC_AUX2		(0x1C)
#define ADC_AUX3		(0x20)
#define ADC_AUX4		(0x24)
#define ADC_AUX5		(0x28)
#define ADC_CB			(0x2C)
#define ADC_COORD2		(0x30)
#define ADC_COORD3		(0x34)
#define ADC_COORD4		(0x38)
#define ADC_CONTROL3		(0x3C)

/* CTRL1 defines */
#define ADC_RESET_QUANT_EN	(1 << 24)
#define ADC_RST_B		(1 << 23)
#define ADC_RESOLUTION_12	(1 << 22)
#define ADC_RBAT_DISABLE	(0x0 << 21)
#define ADC_RBAT_ENABLE		(0x1 << 21)
#define ADC_EXTCM_MASK		(0x3 << 19)
#define ADC_EXTCM(x)		(((x) & 0x3) << 19)
#define ADC_SGAIN_MASK		(0x7 << 16)
#define ADC_SGAIN(x)		(((x) & 0x7) << 16)
#define ADC_POLL		(1 << 15)
#define ADC_SEL_MASK		(0xF << 11)
#define ADC_SEL(x)		(((x) & 0xF) << 11)
#define ADC_FREQ_6K		(0x0 << 8)
#define ADC_FREQ_13K		(0x1 << 8)
#define ADC_DEL_SET_MASK	(0xF << 4)
#define ADC_DEL_SET(x)		(((x) & 0xF) << 4)
#define ADC_TP_TIME_MASK	(0x7)
#define ADC_TP_TIME(x)		(((x) & 0x7) << 0)

/* CTRL2 defines */
#define ADC_PRP_MASK		(3 << 14)
/* Pen detector off, digitizer off */
#define ADC_PRP_MODE0		(0 << 14)
/* Pen detector on, digitizer off, digitizer wakes up on pen detect */
#define ADC_PRP_MODE1		(1 << 14)
/* Pen detector on, digitizer off, no wake up on pen detect */
#define ADC_PRP_MODE2		(2 << 14)
/* Pen detector on, digitizer on */
#define ADC_PRP_MODE3		(3 << 14)
#define ADC_RTOUCH_MASK		(0x3 << 12)
#define ADC_RTOUCH(x)		(((x) & 0x3) << 12)
#define ADC_DEL_AUTO_MASK	(0xF << 8)
#define ADC_DEL_AUTO(x)		(((x) & 0xF) << 8)
#define ADC_DEL_PRE(x)		(((x) & 0xF) << 4)
#define ADC_DEL_DIS(x)		(((x) & 0xF) << 0)

/* INTR register defines */
#define PEN_INTR_EN		(1 << 5)
#define DATA_INTR_EN		(1 << 4)
#define PEN_INTR		(1 << 1)
#define DATA_INTR		(1 << 0)

/* DATA register defines */
#define PEN_DOWN		(1 << 31)
#define DATA_YVALID		(1 << 30)
#define DATA_XVALID		(1 << 29)
#define DATA_Z2VALID		(1 << 30)
#define DATA_Z1VALID		(1 << 29)
#define DATA_AUXVALID		(1 << 30)
#define DATA_CB_VALID		(1 << 30)
#define DATA_Y2VALID		(1 << 30)
#define DATA_X2VALID		(1 << 29)
#define DATA_6VALID		(1 << 30)
#define DATA_5VALID		(1 << 29)
#define DATA_8VALID		(1 << 30)
#define DATA_7VALID		(1 << 29)

#define ADC_DATA_MASK(x)	(0x3FFF << (x))
#define DATA_XMASK		ADC_DATA_MASK(0)
#define DATA_YMASK		ADC_DATA_MASK(DATA_SHIFT_BITS)
#define DATA_Z1MASK		ADC_DATA_MASK(0)
#define DATA_Z2MASK		ADC_DATA_MASK(DATA_SHIFT_BITS)
#define DATA_AUXMASK		ADC_DATA_MASK(0)
#define DATA_CBMASK		ADC_DATA_MASK(0)
#define DATA_X2MASK		ADC_DATA_MASK(0)
#define DATA_Y2MASK		ADC_DATA_MASK(DATA_SHIFT_BITS)
#define DATA_5MASK		ADC_DATA_MASK(0)
#define DATA_6MASK		ADC_DATA_MASK(DATA_SHIFT_BITS)
#define DATA_7MASK		ADC_DATA_MASK(0)
#define DATA_8MASK		ADC_DATA_MASK(DATA_SHIFT_BITS)

#define ADC_IDEAL_RELA_RESULT	(11597)
#define ADC_IDEAL_ABSO_RESULT	(9446)

#define GETX(val)		((val) & DATA_XMASK)
#define GETY(val)		(((val) & DATA_YMASK) >> DATA_SHIFT_BITS)

#define ADC_MORE_CTL1		(of_machine_is_compatible("sirf,atlas6") ?\
					(ADC_RESET_QUANT_EN | ADC_RST_B) : (0))

/* high priority queue with the bigger value */
enum sirfsoc_adc_service_t {
	SIRFSOC_ADC_SERVICE_AUX = 0,
	SIRFSOC_ADC_SERVICE_TS_PARADOX,
	SIRFSOC_ADC_SERVICE_MAX,
};

enum sirfsoc_adc_req_status_t {
	SIRFSOC_ADC_REQ_NONE = 0,
	SIRFSOC_ADC_REQ_ACTIVE,
	SIRFSOC_ADC_REQ_BUSY,
	SIRFSOC_ADC_REQ_MAX,
};

struct sirfsoc_adc_data {
	u16 x;
	u16 y;
	u16 z1;
	u16 z2;
	u16 aux;
	u8 datavalid;
};

typedef void (*sirfsoc_adc_callback) (void *req);

struct sirfsoc_adc_request {
	enum sirfsoc_adc_service_t type;
	sirfsoc_adc_callback adc_cb;
	struct sirfsoc_adc_data adc_data;
	struct list_head adc_req;
	void *drv_handle;
	u16 mode;
	u16 aux;
	u16 reference;
	u8 delay_bits;
	u32 s_gain_bits;
	enum sirfsoc_adc_req_status_t req_status;
	struct completion adc_done;
};

/* export API from adc */
/*extern int sirfsoc_adc_async_request(struct sirfsoc_adc_request *req);*/

extern int sirfsoc_adc_sync_request(struct sirfsoc_adc_request *req);
void sirfsoc_adc_write_reg(u32 data, u32 offset);
u32 sirfsoc_adc_read_reg(u32 offset);
int sirfsoc_adc_sync_reg(void);

/*static inline void sirfsoc_adc_message_init(struct sirfsoc_adc_request *req)
{
	memset(req, 0, sizeof *req);
	INIT_LIST_HEAD(&req->adc_req);
}

int adc_service_register(sirfsoc_adc_service_t type,
			 struct sirfsoc_adc_request *req);

int adc_service_unregister(sirfsoc_adc_service_t type,
			   struct sirfsoc_adc_request *req);
*/
#endif				/* __SIRFSOC_ADC_H */
