/*
 * Battery and Power Management code for the Prima II.
 *
 * Copyright (c) 2013-2014, 2016, The Linux Foundation. All rights reserved.
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
#include <linux/iio/consumer.h>

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

struct sirfsoc_batt {
	struct sirfsoc_batt_info batt_info;
	struct sirfsoc_adc_request *req;
	struct task_struct *battery_task;
	int charge_full;
	int charge_full_gpio;
	int status_batt;
	struct iio_channel *chan;
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
	return battery;
}

static u32 sirfsoc_batt_get_charged_battery(struct sirfsoc_batt *batt)
{
	int ret;
	int battery;

	ret = iio_read_channel_raw(batt->chan, &battery);
	if (ret < 0)
		return 0;

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
	struct sirfsoc_batt *batt = (struct sirfsoc_batt *)dev_id;

	batt->charge_full = gpio_get_value(batt->charge_full_gpio);

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
	struct sirfsoc_batt *batt = (struct sirfsoc_batt *)data;
	u32 new_capacity;
	bool clear = false;
	int old_status_batt;
	struct sirfsoc_batt_rcv_fifo battery_fifo;

	struct sched_param param = {
		.sched_priority = MAX_USER_RT_PRIO
	};

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
		battery = sirfsoc_batt_get_charged_battery(batt);

		schedule_timeout(2 * HZ);
		batt->status_batt = sirfsoc_batt_get_batt_status();
		if (batt->status_batt == 0) {
			battery = sirfsoc_batt_get_charged_battery(batt);
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
			battery = sirfsoc_batt_get_charged_battery(batt);
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
	dev_info(dev, "%s\n", __func__);
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

	dev_info(dev, "%s\n", __func__);
	enable_irq(gpio_to_irq(sirfsoc_batt->charge_full_gpio));
	sirfsoc_batt->charge_full = gpio_get_value(
						sirfsoc_batt->charge_full_gpio);

	sirfsoc_batt->battery_task = kthread_run(sirfsoc_batt_thread,
						sirfsoc_batt, "battery_task");
	if (IS_ERR(sirfsoc_batt->battery_task)) {
		dev_info(dev, "unable to start battery kthread!");
		ret = PTR_ERR(sirfsoc_batt->battery_task);
		sirfsoc_batt->battery_task = NULL;
		return ret;
	}
	return ret;
}

static const struct dev_pm_ops sirfsoc_batt_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sirfsoc_batt_suspend, sirfsoc_batt_resume)
};
#endif

static int sirfsoc_batt_probe(struct platform_device *pdev)
{
	int ret;
	int charge_full_irq;
	struct sirfsoc_batt *batt;
	if (pdev->id != -1) {
		dev_dbg(&pdev->dev,
			"%s:sirfsoc only support one battery\n", __func__);
		return -EINVAL;
	}

	batt = devm_kzalloc(&pdev->dev, sizeof(struct sirfsoc_batt),
				GFP_KERNEL);
	if (!batt) {
		dev_err(&pdev->dev,
			"sirfsoc_batt: Cant allocate request batt buffer\n");
		return -ENOMEM;
	}

	batt->chan = iio_channel_get(&pdev->dev, NULL);
	if (IS_ERR(batt->chan)) {
		dev_err(&pdev->dev, "sirfsoc batt: Unable get the adc channel\n");
		ret = PTR_ERR(batt->chan);
		return ret;
	}

	sirfsoc_batt_init(batt);
	sirfsoc_batt = batt;
	batt->status_batt = sirfsoc_batt_get_batt_status();
	batt->charge_full_gpio = of_get_named_gpio(
					pdev->dev.of_node, "cf-gpio", 0);
	if (batt->charge_full_gpio < 0) {
		dev_dbg(&pdev->dev, "%s:Charge full gpio get fail\n", __func__);
		return batt->charge_full_gpio;
	}
	ret = gpio_request(batt->charge_full_gpio, "charge_full");
	if (ret < 0) {
		dev_dbg(&pdev->dev,
			"%s:Charge full gpio request fail\n", __func__);
		return ret;
	}
	ret = gpio_direction_input(batt->charge_full_gpio);
	if (ret < 0) {
		dev_dbg(&pdev->dev,
			"%s:Charge full gpio set direction fail\n", __func__);
		return ret;
	}
	charge_full_irq = gpio_to_irq(batt->charge_full_gpio);
	if (charge_full_irq < 0) {
		ret = charge_full_irq;
		dev_dbg(&pdev->dev,
			"%s:Charge full gpio to irq fail\n", __func__);
		return ret;
	}
	ret = devm_request_irq(&pdev->dev, charge_full_irq,
			sirfsoc_batt_charge_full_handler,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
			| IRQF_SHARED, "charge_full", &batt);
	if (ret < 0) {
		dev_dbg(&pdev->dev,
			"%s:Request charge full irq fail\n", __func__);
		return ret;
	}

	if (SIRFSOC_BATT_CHARGE_SOURCE & SIRFSOC_BATT_AC_CHG) {
		batt->batt_info.avail_chg_sources |= SIRFSOC_BATT_AC_CHG;
		ret = power_supply_register(&pdev->dev, &sirfsoc_batt_psy_ac);
		if (ret) {
			dev_dbg(&pdev->dev, "sirfsoc_ac register failure!!!\n");
			goto err_power_supply_register_ac;
		}
	}
	batt->batt_info.psy_ac = &sirfsoc_batt_psy_ac;
	if (SIRFSOC_BATT_CHARGE_SOURCE & SIRFSOC_BATT_USB_CHG) {
		batt->batt_info.avail_chg_sources |= SIRFSOC_BATT_USB_CHG;
		ret = power_supply_register(&pdev->dev, &sirfsoc_batt_psy_usb);
		if (ret) {
			dev_dbg(&pdev->dev, "sirfsoc_usb register failure !!\n\n");
			goto err_power_supply_register_usb;
		}
		batt->batt_info.psy_usb = &sirfsoc_batt_psy_usb;
	}
	if (!batt->batt_info.psy_ac && !batt->batt_info.psy_usb) {
		dev_dbg(&pdev->dev, "%s:No external Power Supply(ACorUSB) is available\n",
			__func__);
		return -ENODEV;
	}
	batt->batt_info.batt_technology = POWER_SUPPLY_TECHNOLOGY_LION;
	batt->batt_info.batt_status = sirfsoc_batt_get_batt_status();
	batt->batt_info.voltage_max_design = SIRFSOC_BATT_MAX;
	batt->batt_info.voltage_min_design = SIRFSOC_BATT_MIN;
	ret = power_supply_register(&pdev->dev, &sirfsoc_batt_psy_batt);
	if (ret) {
		dev_dbg(&pdev->dev, "%s:power_supply_register failed ret = %d\n",
			__func__, ret);
		goto err_power_supply_register_batt;
	}

	batt->batt_info.psy_batt = &sirfsoc_batt_psy_batt;

	batt->battery_task = kthread_run(sirfsoc_batt_thread,
						batt, "battery_task");
	if (IS_ERR(batt->battery_task)) {
		dev_info(&pdev->dev, "unable to start battery kthread!");
		ret = PTR_ERR(batt->battery_task);
		batt->battery_task = NULL;
		return ret;
	}

	batt->charge_full = gpio_get_value(batt->charge_full_gpio);
	power_supply_changed(&sirfsoc_batt_psy_batt);

	platform_set_drvdata(pdev, batt);
	dev_info(&pdev->dev, "sirfsoc_batt_probe OK!!\n");

	return 0;

err_power_supply_register_batt:
	power_supply_unregister(batt->batt_info.psy_batt);
err_power_supply_register_usb:
	power_supply_unregister(batt->batt_info.psy_usb);
err_power_supply_register_ac:
	power_supply_unregister(batt->batt_info.psy_ac);
	iio_channel_release(batt->chan);
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

	iio_channel_release(batt->chan);
	power_supply_unregister(batt->batt_info.psy_batt);
	power_supply_unregister(batt->batt_info.psy_usb);
	power_supply_unregister(batt->batt_info.psy_ac);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static void sirfsoc_batt_shutdown(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "%s\n", __func__);
	sirfsoc_batt_remove(pdev);
}

static const struct of_device_id sirfsoc_batt_ids[] = {
	{ .compatible = "sirf,prima2-battery" },
	{ .compatible = "sirf,marco-battery" },
	{}
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

MODULE_DESCRIPTION("CSR Prima II battery driver");
MODULE_LICENSE("GPL");
