/*
 * Battery and Power Management code for the Prima II.
 *
 * Copyright 2011 CSR plc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/timer.h>
#include <linux/kthread.h>
#include <linux/sched/rt.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>
#include <linux/input/sirfsoc_adc.h>

#define DRIVER_NAME "sirfsoc-battery"

#define EXT_ON_EN  BIT(1)
#define VOLT_LOW   BIT(4)
#define VOLT_HIGH  BIT(5)

#define SIRFSOC_PWRC_BASE 0x3000
#define PWRC_PIN_STATUS	0x14

#define SIRFSOC_BATT_AC_CHG     0x00000001
#define SIRFSOC_BATT_USB_CHG    0x00000002
#define SIRFSOC_BATT_CHARGE_SOURCE \
		(SIRFSOC_BATT_AC_CHG | SIRFSOC_BATT_USB_CHG)

#define SIRFSOC_BATT_MAX 4050
#define SIRFSOC_BATT_MIN 3350

#define SIRFSOC_BATT_FIFOLEN 15

struct sirfsoc_batt_info {
	u32 voltage_max_design;
	u32 voltage_min_design;
	u32 batt_technology;
	u32 batt_status;
	u32 batt_health;
	u32 batt_valid;
	u32 batt_temp;
	u32 batt_capacity;
	u32 battery_voltage;
	u32 avail_chg_sources;
	u32 current_chg_source;
	struct power_supply *psy_ac;
	struct power_supply *psy_usb;
	struct power_supply *psy_batt;
};

struct sirfsoc_batt_cali_data {
	u32 digital_offset;
	u32 digital_again;
	u32 digital_ideal;
	bool is_calibration;
};

struct sirfsoc_batt {
	struct sirfsoc_batt_info batt_info;
	struct sirfsoc_adc_request *req;
	struct task_struct *battery_task;
	int charge_full;
	int charge_full_gpio;
	int status_batt;
};

static struct sirfsoc_batt *sirfsoc_batt;

static enum power_supply_property sirfsoc_power_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};
static char *sirfsoc_batt_power_supplied_to[] = {
	"battery",
};

static enum power_supply_property sirfsoc_batt_power_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
};

static int sirfsoc_batt_power_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val);
static int sirfsoc_batt_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val);

static struct power_supply sirfsoc_batt_psy_ac = {
	.name = "ac",
	.type = POWER_SUPPLY_TYPE_MAINS,
	.supplied_to = sirfsoc_batt_power_supplied_to,
	.num_supplicants = ARRAY_SIZE(sirfsoc_batt_power_supplied_to),
	.properties = sirfsoc_power_props,
	.num_properties = ARRAY_SIZE(sirfsoc_power_props),
	.get_property = sirfsoc_batt_power_get_property,
};

static struct power_supply sirfsoc_batt_psy_usb = {
	.name = "usb",
	.type = POWER_SUPPLY_TYPE_USB,
	.supplied_to = sirfsoc_batt_power_supplied_to,
	.num_supplicants = ARRAY_SIZE(sirfsoc_batt_power_supplied_to),
	.properties = sirfsoc_power_props,
	.num_properties = ARRAY_SIZE(sirfsoc_power_props),
	.get_property = sirfsoc_batt_power_get_property,
};

static struct power_supply sirfsoc_batt_psy_batt = {
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = sirfsoc_batt_power_props,
	.num_properties = ARRAY_SIZE(sirfsoc_batt_power_props),
	.get_property = sirfsoc_batt_get_property,
};

struct sirfsoc_batt_rcv_fifo {
	u32 fifodata[SIRFSOC_BATT_FIFOLEN];
	u32 head;
	u32 tag;
	u8 empty;
	u8 full;
	char strname[64];
};

static void sirfsoc_batt_fifoinit(struct sirfsoc_batt_rcv_fifo *pfifo,
							char *strname)
{
	pfifo = pfifo;
	memset(pfifo, 0, sizeof(struct sirfsoc_batt_rcv_fifo));
	pfifo->empty = 1;
	pfifo->full = 0;
	if (strname)
		strncpy(pfifo->strname, strname, 63);
}
static u8 sirfsoc_batt_fifoput(struct sirfsoc_batt_rcv_fifo *pfifo, u32 data)
{
	u8 rdyfull;
	pfifo = pfifo;
	data = data;
	rdyfull = 0;
	if (pfifo->full) {
		pfifo->head++;
		if (pfifo->head == SIRFSOC_BATT_FIFOLEN)
			pfifo->head = 0;
	}
	if (((pfifo->tag - pfifo->head) == (SIRFSOC_BATT_FIFOLEN - 1))
		|| ((pfifo->tag - pfifo->head) == -1))
		rdyfull = 1;
	pfifo->fifodata[pfifo->tag++] = data;
	if (pfifo->tag == SIRFSOC_BATT_FIFOLEN)
		pfifo->tag = 0;
	if (pfifo->empty)
		pfifo->empty = 0;
	if (rdyfull)
		pfifo->full = 1;
	return 1;
}

/*Functions to get the calibrated voltage*/
/*static int sirfsoc_get_low_volt(void)
{
	u32 pmu_reg, value, low_voltage;

	if (of_machine_is_compatible("sirf,atlas6"))
		return 3500;

	pmu_reg = sirfsoc_rtc_iobrg_readl(SYS_PWR_BASE + PWRC_PMU_CTRL);
	value = ((pmu_reg & VOLT_HIGH) | (pmu_reg & VOLT_LOW)) >> 4;
	switch (value) {
	case 0:
		low_voltage = 3200;
		break;
	case 1:
		low_voltage = 3300;
		break;
	case 2:
		low_voltage = 3400;
		break;
	case 3:
		low_voltage = 3500;
		break;
	}
	return low_voltage;
}
*/
static int sirfsoc_batt_get_batt_status(void)
{
	u32 pwr_pin_status, value = 0;
/*If the board can use the usb charge, check if the usb pluged*/
/*	if (of_machine_is_compatible("sirf,atlas6-lc")) {
		u32 usb_id_status = __raw_readl(usbcd_usb1_vaddr() + OTGSC);
		if ((usb_id_status & OTGSC_ID_MASK)
				&& (usb_id_status & OTGSC_AVV_MASK))
			value |= SIRFSOC_BATT_USB_CHG;
	}
*/
	pwr_pin_status = sirfsoc_rtc_iobrg_readl(SIRFSOC_PWRC_BASE
				+ PWRC_PIN_STATUS);
	if (pwr_pin_status & EXT_ON_EN)
		value |= SIRFSOC_BATT_AC_CHG;
	return value;
}


static u32 sirfsoc_batt_offset_cali(struct sirfsoc_adc_request *req)
{
	u32 i, digital_offset = 0, count = 0, sum = 0;
	/* To set the reigsters in order to get the ADC offset */
	req->mode = ADC_SEL(11);
	req->aux = 0x2C;
	req->s_gain_bits = ADC_SGAIN(7);
	req->delay_bits = ADC_DEL_SET(4);
	req->req_status = SIRFSOC_ADC_REQ_NONE;

	for (i = 0; i < 10; i++) {
		if (unlikely(sirfsoc_adc_sync_request(req)))
			break;
		digital_offset = req->adc_data.aux;
		/* Maybe the value is wrong, so remove it use experience */
		if (digital_offset < 230 && digital_offset > 130) {
			sum += digital_offset;
			count++;
		}
	}
	if (!sum || !count)
		digital_offset = 170;
	else
		digital_offset = sum / count;

	return digital_offset;
}


/* For ADC gain calibration */
static u32 sirfsoc_batt_gain_cali(struct sirfsoc_adc_request *req)
{
	u32 i, digital_gain = 0, count = 0, sum = 0;
	/* To set the reigsters in order to get the ADC gain */
	req->mode = ADC_SEL(12);
	req->aux = 0x2C;
	req->s_gain_bits = ADC_SGAIN(0);
	req->delay_bits = ADC_DEL_SET(4);
	req->req_status = SIRFSOC_ADC_REQ_NONE;

	for (i = 0; i < 10; i++) {
		if (unlikely(sirfsoc_adc_sync_request(req)))
			break;
		digital_gain = req->adc_data.aux;
		/* Maybe the value is wrong, so remove it use experience */
		if (digital_gain < 6500 && digital_gain > 5500) {
			sum += digital_gain;
			count++;
		}
	}
	if (!sum || !count)
		digital_gain = 5555;
	else
		digital_gain = sum / count;

	return digital_gain;
}

/* For ADC digital IDEAL */
static int sirfsoc_batt_adc_cali(struct sirfsoc_adc_request *req,
				struct sirfsoc_batt_cali_data *cali_data)
{
	cali_data->digital_offset = sirfsoc_batt_offset_cali(req);
	if (!(cali_data->digital_offset))
		return -EINVAL;
	cali_data->digital_again = sirfsoc_batt_gain_cali(req);
	if (!(cali_data->digital_again))
		return -EINVAL;
	/* the ideal ADC conversion result for
		PrimaII A1 which show in the ADC spec */
	cali_data->digital_ideal = (16384 * 1200) / (14 * 333);
	return 0;
}


/* For Get battery voltage after ADC conversion */
static u32 sirfsoc_batt_get_adc_volt(struct sirfsoc_adc_request *req,
				struct sirfsoc_batt_cali_data *cali_data)
{
	u32 digital_out, digital_convert, batt_volt;
	if (!(cali_data->is_calibration)) {
		if (sirfsoc_batt_adc_cali(req, cali_data))
			return 0;
		cali_data->is_calibration = true;
	}
	/* To set the reigsters in order to get the ADC output */
	req->mode = ADC_SEL(4);
	req->aux = ADC_AUX0;
	req->s_gain_bits = ADC_SGAIN(0);
	req->delay_bits = ADC_DEL_SET(4);

	sirfsoc_adc_sync_request(req);
	if (req->adc_data.aux) {
		digital_out = req->adc_data.aux;
		/*
		 * The equation is to calibration the digital value out form
		 * ADC using the offset and absolute gain for PrmaII A1
		 * which can find in the adc spec
		 */
		digital_convert = ((digital_out - 2 * cali_data->digital_offset
			* 11645 / 140000) * cali_data->digital_ideal)
			/ (cali_data->digital_again -
			2 * cali_data->digital_offset
			* 11645 / 140000);
		batt_volt = (1200 * digital_convert) / cali_data->digital_ideal;
		batt_volt = batt_volt * 2;

		pr_debug("battery_voltage = %d\n", batt_volt);
	} else {
		pr_debug("ERROR\n");
		return 0;
	}

	return batt_volt;
}


/* Adjust the voltage value between charging and discharging*/
static u32 sirfsoc_batt_get_custom_volt(u32 battery)
{
	if (sirfsoc_batt_get_batt_status()) {
		if ((battery > 3500) && (battery <= 4020))
			battery -= 200;
		if (battery > 4020)
			battery -= 100;
		if (battery <= SIRFSOC_BATT_MIN)
			return 0xAA;
	} else
		battery = battery + 100;
	if (battery >= SIRFSOC_BATT_MAX)
		return SIRFSOC_BATT_MAX;
	if (battery <= SIRFSOC_BATT_MIN)
		return SIRFSOC_BATT_MIN;
	pr_debug("A1 custom batt  = %d\n", battery);
	return battery;
}

static u32 sirfsoc_batt_get_charged_battery(struct sirfsoc_adc_request *req,
				struct sirfsoc_batt_cali_data *cali_data)
{
	int battery;

	battery = sirfsoc_batt_get_adc_volt(req, cali_data);

	return sirfsoc_batt_get_custom_volt(battery);
}

static u32 sirfsoc_batt_get_average_voltage(
			struct sirfsoc_batt_rcv_fifo *battery_info,
			u32 *battery_voltage)
{
	int i;
	u32 sum = 0;
	u32 count = 0;
	for (i = 0; i < SIRFSOC_BATT_FIFOLEN; i++) {
		if (0 != battery_info->fifodata[i]) {
			sum += battery_info->fifodata[i];
			count++;
		}
	}
	if (0 != count)
		*battery_voltage = sum / count;
	else
		*battery_voltage = 0;
	return count;
}

static irqreturn_t sirfsoc_batt_charge_full_handler(int irq, void *dev_id)
{
	struct sirfsoc_batt *batt =
				(struct sirfsoc_batt *)dev_id;
	batt->charge_full =
			gpio_get_value(batt->charge_full_gpio);

	return IRQ_HANDLED;
}

static u32 sirfsoc_batt_calculate_capacity(u32 current_voltage)
{
	u32 high_voltage = sirfsoc_batt->batt_info.voltage_max_design;
	u32 low_voltage = sirfsoc_batt->batt_info.voltage_min_design;
	if (current_voltage >= high_voltage)
		current_voltage = high_voltage;
	if (current_voltage <= low_voltage)
		current_voltage = low_voltage;
	return (current_voltage - low_voltage) *
			100 / (high_voltage - low_voltage);
}

static void sirfsoc_batt_init(struct sirfsoc_batt *batt)
{
	batt->batt_info.battery_voltage = 4050;
	batt->batt_info.batt_capacity = 100;
	batt->batt_info.batt_status = POWER_SUPPLY_STATUS_DISCHARGING;
	batt->batt_info.batt_health = POWER_SUPPLY_HEALTH_GOOD;
	batt->batt_info.batt_temp = 23;
	batt->batt_info.batt_valid = 1;
}

static int sirfsoc_batt_thread(void *data)
{
	u32 battery = 0, capacity = 0, fifo_count = 0;
	u32 old_battery, new_battery;
	struct sirfsoc_batt_cali_data cali_data;
	struct sirfsoc_batt *batt;
	u32 new_capacity;
	bool clear;
	int old_status_batt;
	struct sirfsoc_batt_rcv_fifo battery_fifo;

	struct sched_param param = {
		.sched_priority = MAX_USER_RT_PRIO
	};

	batt = (struct sirfsoc_batt *)data;
	sirfsoc_batt_fifoinit(&battery_fifo, "battery_voltage");
	sched_setscheduler(current, SCHED_FIFO, &param);
	old_status_batt = batt->status_batt;

	do {
		batt->status_batt = sirfsoc_batt_get_batt_status();
		if (old_status_batt != batt->status_batt) {
			power_supply_changed(&sirfsoc_batt_psy_batt);
			old_status_batt = batt->status_batt;
		}

		set_current_state(TASK_UNINTERRUPTIBLE);

		/* FIXME: whether 2seconds * 3 timeout needed? */
		schedule_timeout(2 * HZ);
		fifo_count = sirfsoc_batt_get_average_voltage(
						&battery_fifo, &old_battery);
		if (!clear) {
			if (fifo_count < 8)
				fifo_count = 0;
			if (fifo_count == 8) {
				memset(battery_fifo.fifodata, 0,
					SIRFSOC_BATT_FIFOLEN
					* sizeof(unsigned int));
				fifo_count = 0;
				clear = true;
			}
		}
		batt->status_batt = sirfsoc_batt_get_batt_status();
		if (old_status_batt != batt->status_batt) {
			power_supply_changed(&sirfsoc_batt_psy_batt);
			old_status_batt = batt->status_batt;
		}
		battery = sirfsoc_batt_get_charged_battery(batt->req,
								&cali_data);

		schedule_timeout(2 * HZ);
		batt->status_batt = sirfsoc_batt_get_batt_status();
		if (batt->status_batt == 0) {
			battery = sirfsoc_batt_get_charged_battery(
					batt->req, &cali_data);
			capacity = sirfsoc_batt_calculate_capacity(battery);
			sirfsoc_batt_fifoput(&battery_fifo, battery);
			if (fifo_count == 0) {
				batt->batt_info.battery_voltage = battery;
				batt->batt_info.batt_capacity = capacity;
			} else {
				new_battery = (battery * 1 + old_battery
					* (SIRFSOC_BATT_FIFOLEN - 1))
					/ SIRFSOC_BATT_FIFOLEN;
				new_capacity =
					sirfsoc_batt_calculate_capacity(
								new_battery);
				if (new_capacity <=
						batt->batt_info.batt_capacity) {
					batt->batt_info.battery_voltage =
								new_battery;
					batt->batt_info.batt_capacity =
								new_capacity;
				}
				if (new_battery > SIRFSOC_BATT_MAX)
					batt->batt_info.battery_voltage =
							SIRFSOC_BATT_MAX;
			}
			batt->batt_info.batt_status =
						POWER_SUPPLY_STATUS_DISCHARGING;
		} else {
			battery = sirfsoc_batt_get_charged_battery(batt->req,
								&cali_data);
			capacity = sirfsoc_batt_calculate_capacity(battery);
			if (battery == 0xAA) {
				batt->batt_info.batt_capacity = 1;
				batt->batt_info.battery_voltage =
							SIRFSOC_BATT_MIN;
				batt->batt_info.batt_status =
						POWER_SUPPLY_STATUS_CHARGING;
				sirfsoc_batt_fifoput(&battery_fifo, 3500);
			} else if (batt->charge_full) {
				batt->batt_info.batt_capacity = 100;
				batt->batt_info.battery_voltage =
							SIRFSOC_BATT_MAX;
				batt->batt_info.batt_status =
						POWER_SUPPLY_STATUS_FULL;
				sirfsoc_batt_fifoput(&battery_fifo,
							SIRFSOC_BATT_MAX);
			} else if ((capacity == 100) && (!batt->charge_full)) {
				batt->batt_info.batt_capacity = 99;
				batt->batt_info.battery_voltage = 4044;
				batt->batt_info.batt_status =
					POWER_SUPPLY_STATUS_CHARGING;
				sirfsoc_batt_fifoput(&battery_fifo, 4044);
			} else {
				if (fifo_count == 0) {
					sirfsoc_batt_fifoput(&battery_fifo,
								battery);
					batt->batt_info.battery_voltage =
								battery;
					batt->batt_info.batt_capacity =
								capacity;
					batt->batt_info.batt_status =
						POWER_SUPPLY_STATUS_CHARGING;
				} else {
					sirfsoc_batt_fifoput(&battery_fifo,
								battery);
					new_battery = (battery * 1 + old_battery
						* (SIRFSOC_BATT_FIFOLEN - 1))
						/ SIRFSOC_BATT_FIFOLEN;
					new_capacity =
						sirfsoc_batt_calculate_capacity(
								new_battery);
					batt->batt_info.battery_voltage =
								new_battery;
					batt->batt_info.batt_capacity =
								new_capacity;
					if (new_capacity >=
						batt->batt_info.batt_capacity)
						batt->batt_info.batt_status =
						POWER_SUPPLY_STATUS_CHARGING;
					else
						batt->batt_info.batt_status =
						POWER_SUPPLY_STATUS_DISCHARGING;

					if (new_battery > SIRFSOC_BATT_MAX)
						batt->batt_info.
							battery_voltage =
							SIRFSOC_BATT_MAX;
				}
			}
		}
		pr_debug("new battery = %d capacity = %d\n",
			batt->batt_info.battery_voltage,
			batt->batt_info.batt_capacity);
		old_status_batt = batt->status_batt;
		power_supply_changed(&sirfsoc_batt_psy_batt);

		schedule_timeout(2 * HZ);
	} while (!kthread_should_stop());

	return 0;
}

static int sirfsoc_batt_power_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (psy->type == POWER_SUPPLY_TYPE_MAINS) {
			if (sirfsoc_batt->status_batt & SIRFSOC_BATT_AC_CHG)
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
			else
				val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		} else if (psy->type == POWER_SUPPLY_TYPE_USB) {
			if (sirfsoc_batt->status_batt & SIRFSOC_BATT_USB_CHG)
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
			else
				val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int sirfsoc_batt_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = sirfsoc_batt->batt_info.batt_status;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = sirfsoc_batt->batt_info.batt_health;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = sirfsoc_batt->batt_info.batt_valid;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = sirfsoc_batt->batt_info.batt_technology;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = sirfsoc_batt->batt_info.voltage_max_design;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = sirfsoc_batt->batt_info.voltage_min_design;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = sirfsoc_batt->batt_info.battery_voltage;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = sirfsoc_batt->batt_info.batt_capacity;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

#ifdef CONFIG_PM
static int sirfsoc_batt_suspend(struct device *dev)
{
	pr_info("%s\n", __func__);
	if (sirfsoc_batt->battery_task) {
		kthread_stop(sirfsoc_batt->battery_task);
		sirfsoc_batt->battery_task = NULL;
	}

	disable_irq(gpio_to_irq(sirfsoc_batt->charge_full_gpio));

	return 0;
}

static int sirfsoc_batt_resume(struct device *dev)
{
	int ret = 0;

	pr_info("%s\n", __func__);
	enable_irq(gpio_to_irq(sirfsoc_batt->charge_full_gpio));
	sirfsoc_batt->charge_full = gpio_get_value(
						sirfsoc_batt->charge_full_gpio);

	sirfsoc_batt->battery_task = kthread_run(sirfsoc_batt_thread,
						sirfsoc_batt, "battery_task");
	if (IS_ERR(sirfsoc_batt->battery_task)) {
		pr_info("unable to start battery kthread!");
		ret = PTR_ERR(sirfsoc_batt->battery_task);
		sirfsoc_batt->battery_task = NULL;
		return ret;
	}
	return ret;
}

static int sirfsoc_batt_freeze(struct device *dev)
{
	return sirfsoc_batt_suspend(dev);
}

static int sirfsoc_batt_thaw(struct device *dev)
{
	return 0;
}

static int sirfsoc_batt_restore(struct device *dev)
{
	return sirfsoc_batt_resume(dev);
}

#else
#define sirfsoc_batt_suspend NULL
#define sirfsoc_batt_resume NULL
#define sirfsoc_batt_freeze NULL
#define sirfsoc_batt_thaw NULL
#define sirfsoc_batt_restore NULL
#endif

static int sirfsoc_batt_probe(struct  platform_device *pdev)
{
	int ret;
	int charge_full_irq;
	struct sirfsoc_batt *batt;
	if (pdev->id != -1) {
		pr_debug("%s:sirfsoc only support one battery\n", __func__);
		return -EINVAL;
	}

	batt = devm_kzalloc(&pdev->dev, sizeof(struct sirfsoc_batt),
				GFP_KERNEL);
	if (!batt) {
		pr_err("sirfsoc_batt: Cant allocate request batt buffer\n");
		return -ENOMEM;
	}

	sirfsoc_batt_init(batt);
	sirfsoc_batt = batt;
	batt->status_batt = sirfsoc_batt_get_batt_status();
	batt->charge_full_gpio = of_get_named_gpio(
					pdev->dev.of_node, "cf-gpio", 0);
	if (batt->charge_full_gpio < 0) {
		pr_debug("%s:Charge full gpio get fail\n", __func__);
		return batt->charge_full_gpio;
	}
	ret = gpio_request(batt->charge_full_gpio, "charge_full");
	if (ret < 0) {
		pr_debug("%s:Charge full gpio request fail\n", __func__);
		return ret;
	}
	ret = gpio_direction_input(batt->charge_full_gpio);
	if (ret < 0) {
		pr_debug("%s:Charge full gpio set direction fail\n", __func__);
		return ret;
	}
	charge_full_irq = gpio_to_irq(batt->charge_full_gpio);
	if (charge_full_irq < 0) {
		ret = charge_full_irq;
		pr_debug("%s:Charge full gpio to irq fail\n", __func__);
		return ret;
	}
	ret = devm_request_irq(&pdev->dev, charge_full_irq,
			sirfsoc_batt_charge_full_handler,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
			| IRQF_SHARED, "charge_full", &batt);
	if (ret < 0) {
		pr_debug("%s:Request charge full irq fail\n", __func__);
		return ret;
	}

	if (SIRFSOC_BATT_CHARGE_SOURCE & SIRFSOC_BATT_AC_CHG) {
		batt->batt_info.avail_chg_sources |= SIRFSOC_BATT_AC_CHG;
		ret = power_supply_register(&pdev->dev, &sirfsoc_batt_psy_ac);
		if (ret) {
			pr_debug("sirfsoc_ac register failure!!!\n");
			goto err_power_supply_register_ac;
		}
	}
	batt->batt_info.psy_ac = &sirfsoc_batt_psy_ac;
	if (SIRFSOC_BATT_CHARGE_SOURCE & SIRFSOC_BATT_USB_CHG) {
		batt->batt_info.avail_chg_sources |= SIRFSOC_BATT_USB_CHG;
		ret = power_supply_register(&pdev->dev, &sirfsoc_batt_psy_usb);
		if (ret) {
			pr_debug("sirfsoc_usb register failure !!\n\n");
			goto err_power_supply_register_usb;
		}
		batt->batt_info.psy_usb = &sirfsoc_batt_psy_usb;
	}
	if (!batt->batt_info.psy_ac &&
		!batt->batt_info.psy_usb) {
		pr_debug("%s:No external Power Supply(ACorUSB) is available\n",
			__func__);
		return -ENODEV;
	}
	batt->batt_info.batt_technology = POWER_SUPPLY_TECHNOLOGY_LION;
	batt->batt_info.batt_status = sirfsoc_batt_get_batt_status();
	batt->batt_info.voltage_max_design = SIRFSOC_BATT_MAX;
	batt->batt_info.voltage_min_design = SIRFSOC_BATT_MIN;
	ret = power_supply_register(&pdev->dev, &sirfsoc_batt_psy_batt);
	if (ret) {
		pr_debug("%s:power_supply_register failed ret = %d\n",
			__func__, ret);
		goto err_power_supply_register_batt;
	}
	batt->req = devm_kzalloc(&pdev->dev, sizeof(struct sirfsoc_adc_request),
				GFP_KERNEL);
	if (!batt->req) {
		pr_err("sirfsoc_batt: Cant allocate request buffer\n");
		goto err_power_supply_register_batt;
	}

	batt->batt_info.psy_batt = &sirfsoc_batt_psy_batt;

	batt->battery_task = kthread_run(sirfsoc_batt_thread,
						batt, "battery_task");
	if (IS_ERR(batt->battery_task)) {
		pr_info("unable to start battery kthread!");
		ret = PTR_ERR(batt->battery_task);
		batt->battery_task = NULL;
		return ret;
	}

	batt->charge_full = gpio_get_value(batt->charge_full_gpio);
	power_supply_changed(&sirfsoc_batt_psy_batt);

	platform_set_drvdata(pdev, batt);
	pr_info("sirfsoc_batt_probe OK!!\n");

	return 0;

err_power_supply_register_batt:
	power_supply_unregister(batt->batt_info.psy_batt);
err_power_supply_register_usb:
	power_supply_unregister(batt->batt_info.psy_usb);
err_power_supply_register_ac:
	power_supply_unregister(batt->batt_info.psy_ac);
	return ret;
}

static int sirfsoc_batt_remove(struct platform_device *pdev)
{
	struct sirfsoc_batt *batt;
	batt = platform_get_drvdata(pdev);

	if (batt->battery_task) {
		kthread_stop(batt->battery_task);
		batt->battery_task = NULL;
	}

	power_supply_unregister(batt->batt_info.psy_batt);
	power_supply_unregister(batt->batt_info.psy_usb);
	power_supply_unregister(batt->batt_info.psy_ac);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static void sirfsoc_batt_shutdown(struct platform_device *pdev)
{
	pr_info("%s\n", __func__);
	sirfsoc_batt_remove(pdev);
}

static const struct of_device_id sirfsoc_batt_ids[] = {
	{ .compatible = "sirf,prima2-battery" },
	{ .compatible = "sirf,marco-battery" },
	{}
};

static const struct dev_pm_ops sirfsoc_batt_pm_ops = {
	.suspend = sirfsoc_batt_suspend,
	.resume = sirfsoc_batt_resume,
	.freeze = sirfsoc_batt_freeze,
	.thaw = sirfsoc_batt_thaw,
	.restore = sirfsoc_batt_restore,
};

static struct platform_driver sirfsoc_batt_driver = {
	.driver = {
		.name = DRIVER_NAME,
#ifdef CONFIG_PM
		.pm = &sirfsoc_batt_pm_ops,
#endif
		.of_match_table = sirfsoc_batt_ids,
	},
	.probe = sirfsoc_batt_probe,
	.remove = sirfsoc_batt_remove,
	.shutdown = sirfsoc_batt_shutdown,
};

module_platform_driver(sirfsoc_batt_driver);

MODULE_AUTHOR("Lisai Wang <Lisai.Wang@csr.com>");
MODULE_DESCRIPTION("CSR Prima II battery driver");
MODULE_LICENSE("GPL");
