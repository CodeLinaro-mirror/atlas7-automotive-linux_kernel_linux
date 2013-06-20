/*
 *  Touchscreen Linear Scale Adaptor
 *
 *
 * This library is licensed under GPL.
 *
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
#include <asm/system.h>
#include <linux/input/sirfsoc_ts.h>

/*
 * sysctl-tuning infrastructure.
 * a[7] use to identify calibrate mode
 */
static struct ts_calibration {
/* Linear scaling and offset parameters for x,y (can include rotation) */
	int a[8];
} cal;

static ctl_table ts_proc_calibration_table[] = {
	{
	.procname = "a0",
	.data = &cal.a[0],
	.maxlen = sizeof(int),
	.mode = 0666,
	.proc_handler = &proc_dointvec,
	},
	{
	.procname = "a1",
	.data = &cal.a[1],
	.maxlen = sizeof(int),
	.mode = 0666,
	.proc_handler = &proc_dointvec,
	},
	{
	.procname = "a2",
	.data = &cal.a[2],
	.maxlen = sizeof(int),
	.mode = 0666,
	.proc_handler = &proc_dointvec,
	},
	{
	.procname = "a3",
	.data = &cal.a[3],
	.maxlen = sizeof(int),
	.mode = 0666,
	.proc_handler = &proc_dointvec,
	},
	{
	.procname = "a4",
	.data = &cal.a[4],
	.maxlen = sizeof(int),
	.mode = 0666,
	.proc_handler = &proc_dointvec,
	},
	{
	.procname = "a5",
	.data = &cal.a[5],
	.maxlen = sizeof(int),
	.mode = 0666,
	.proc_handler = &proc_dointvec,
	},
	{
	.procname = "a6",
	.data = &cal.a[6],
	.maxlen = sizeof(int),
	.mode = 0666,
	.proc_handler = &proc_dointvec,
	},
	{
	.procname = "a7",
	.data = &cal.a[7],
	.maxlen = sizeof(int),
	.mode = 0666,
	.proc_handler = &proc_dointvec,
	},
	{}
};

static ctl_table ts_proc_root[] = {
	{
	.procname = "ts_device",
	.mode = 0555,
	.child = ts_proc_calibration_table,
	},
	{}
};

static ctl_table ts_dev_root[] = {
	{
	.procname = "dev",
	.mode = 0555,
	.child = ts_proc_root,
	},
	{}
};

static struct ctl_table_header *ts_sysctl_header;

int ts_linear_scale(int *x, int *y, int swap_xy)
{
	int xtemp, ytemp;

	/* return in calibration mode */
	if (cal.a[7] == 1) {
		if (swap_xy) {
			int tmp = *x;
			*x = *y;
			*y = tmp;
		}
		return 0;
	}

	xtemp = *x;
	ytemp = *y;

	if (cal.a[6] == 0)
		return -EINVAL;

	*x = (cal.a[2] + cal.a[0] * xtemp + cal.a[1] * ytemp) / cal.a[6];
	*y = (cal.a[5] + cal.a[3] * xtemp + cal.a[4] * ytemp) / cal.a[6];

	/*if (cpu_is_prima2_BX()) {*/
	if (of_machine_is_compatible("sirf,prima2-cb")) {
		/* screen size and touch mapping */
		*x = *x * 0xFFF / 0x320;
		*y = *y * 0xFFF / 0x1E0;
	/*} else if (cpu_is_prima2_A1()) {*/
		/* 800*480 */
		/**x = *x * 0x3FFF / 0x320;
		*y = *y * 0x3FFF / 0x1E0;*/
	} else if (of_machine_is_compatible("sirf,atlas6-cb")) {
		/* 800*480 */
		*x = *x * 0x3FFF / 0x320;
		*y = *y * 0x3FFF / 0x1E0;
	} else if (of_machine_is_compatible("sirf,atlas6-lc")) {
		/* 800*600 */
		*x = *x * 0x3FFF / 0x320;
		*y = *y * 0x3FFF / 0x258;
	}

	if (swap_xy) {
		int tmp = *x;
		*x = *y;
		*y = tmp;
	}
	return 0;
}
EXPORT_SYMBOL(ts_linear_scale);

static int __init ts_linear_init(void)
{
	ts_sysctl_header = register_sysctl_table(ts_dev_root);
	/* Use default values that leave ts numbers
	  unchanged after transform */
	/*if (cpu_is_prima2_BX()) {*/
	if (of_machine_is_compatible("sirf,prima2-cb")) {
		cal.a[0] = -3966;
		cal.a[1] = -8;
		cal.a[2] = 54124960;
		cal.a[3] = -19;
		cal.a[4] = 2601;
		cal.a[5] = -2492944;
		cal.a[6] = 65536;
	/*} else if (cpu_is_prima2_A1()) {
		cal.a[0] = -3601;
		cal.a[1] = 15;
		cal.a[2] = 54808424;
		cal.a[3] = 0;
		cal.a[4] = 2379;
		cal.a[5] = -2875912;
		cal.a[6] = 65536;*/
	} else if (of_machine_is_compatible("sirf,atlas6-cb")) {
		cal.a[0] = -5170;
		cal.a[1] = 22;
		cal.a[2] = 53686744;
		cal.a[3] = 56;
		cal.a[4] = -3477;
		cal.a[5] = 33720496;
		cal.a[6] = 65536;
	} else if (of_machine_is_compatible("sirf,atlas6-lc")) {
		/*if (cpu_is_atlas6_A0()) {*/
			cal.a[0] = 37;
			cal.a[1] = 5794;
			cal.a[2] = -2149112;
			cal.a[3] = -4371;
			cal.a[4] = 9;
			cal.a[5] = 41155968;
			cal.a[6] = 65536;
		/*} else if (cpu_is_atlas6_A1()) {
			cal.a[0] = -7;
			cal.a[1] = 7941;
			cal.a[2] = -13871584;
			cal.a[3] = -6922;
			cal.a[4] = 75;
			cal.a[5] = 55433552;
			cal.a[6] = 65536;
		}*/
	}

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
