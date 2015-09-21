/*
 * Touchscreen Linear Scale Adaptor
 *
 * Copyright (c) 2013, 2015, The Linux Foundation. All rights reserved.
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
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/sysctl.h>

#include "ts_linear.h"

/*
 * sysctl-tuning infrastructure.
 * param[7],param[8] use for the screen size
 * cali_mode use to identify calibrate mode
 */
static struct ts_calibration {
/* Linear scaling and offset parameters for x,y (can include rotation) */
	int param[9];
	int cali_mode;
} cal;

static struct ctl_table ts_proc_calibration_table[] = {
	{
	.procname = "cali_param",
	.data = cal.param,
	.maxlen = sizeof(cal.param),
	.mode = 0666,
	.proc_handler = &proc_dointvec,
	}, {
	.procname = "cali_mode",
	.data = &cal.cali_mode,
	.maxlen = sizeof(int),
	.mode = 0666,
	.proc_handler = &proc_dointvec,
	},
	{}
};

static struct ctl_table ts_proc_root[] = {
	{
	.procname = "ts_device",
	.mode = 0555,
	.child = ts_proc_calibration_table,
	},
	{}
};

static struct ctl_table ts_dev_root[] = {
	{
	.procname = "dev",
	.mode = 0555,
	.child = ts_proc_root,
	},
	{}
};

static struct ctl_table_header *ts_sysctl_header;

int ts_linear_scale(int *x, int *y)
{
	int xtemp, ytemp;

	/* return in calibration mode */
	if (cal.cali_mode == 1)
		return 0;

	xtemp = *x;
	ytemp = *y;

	if (cal.param[6] == 0)
		return -EINVAL;

	*x = (cal.param[2] + cal.param[0] * xtemp +
			cal.param[1] * ytemp) / cal.param[6];
	*y = (cal.param[5] + cal.param[3] * xtemp +
			cal.param[4] * ytemp) / cal.param[6];

	if (cal.param[7] && cal.param[8]) {
		/* screen size and touch mapping */
		*x = *x * 0x3FFF / cal.param[7];
		*y = *y * 0x3FFF / cal.param[8];
	}

	return 0;
}
EXPORT_SYMBOL(ts_linear_scale);

static int __init ts_linear_init(void)
{
	ts_sysctl_header = register_sysctl_table(ts_dev_root);
	/* Use default values for calibrate*/
	cal.param[0] = -3966;
	cal.param[1] = -8;
	cal.param[2] = 54124960;
	cal.param[3] = -19;
	cal.param[4] = 2601;
	cal.param[5] = -2492944;
	cal.param[6] = 65536;
	cal.param[7] = 800;
	cal.param[8] = 480;
	cal.cali_mode = 0;

	return 0;
}

static void __exit ts_linear_cleanup(void)
{
	unregister_sysctl_table(ts_sysctl_header);
}

module_init(ts_linear_init);
module_exit(ts_linear_cleanup);

MODULE_DESCRIPTION("touch screen linear scaling driver");
MODULE_LICENSE("GPL");
