#ifndef _SIRFSOC_IPC_H_
#define _SIRFSOC_IPC_H_

#include <linux/platform_device.h>
#include <linux/mfd/core.h>

/* IPC hardware spinlock register offsets */
#define IPC_SPINLOCK_RD_DEBUG	0x400
#define IPC_SPINLOCK_BASE	0x404
#define IPC_SPINLOCK_OFFSET(x)	(IPC_SPINLOCK_BASE + 0x4 * (x))

/* IPC device interrupt register offeset */
#define REG_TRGT1_INIT0_1 0x0000
#define REG_TRGT1_INIT0_2 0x0004
#define REG_TRGT2_INIT0_1 0x0008
#define REG_TRGT2_INIT0_2 0x000C
#define REG_TRGT3_INIT0_1 0x0010
#define REG_TRGT3_INIT0_2 0x0014
#define REG_TRGT0_INIT1_1 0x0100
#define REG_TRGT0_INIT1_2 0x0104
#define REG_TRGT2_INIT1_1 0x0108
#define REG_TRGT2_INIT1_2 0x001C
#define REG_TRGT3_INIT1_1 0x0110
#define REG_TRGT3_INIT1_2 0x0114
#define REG_TRGT0_INIT2_1 0x0200
#define REG_TRGT0_INIT2_2 0x0204
#define REG_TRGT1_INIT2_1 0x0208
#define REG_TRGT1_INIT2_2 0x002C
#define REG_TRGT3_INIT2_1 0x0210
#define REG_TRGT3_INIT2_2 0x0214
#define REG_TRGT0_INIT3_1 0x0300
#define REG_TRGT0_INIT3_2 0x0304
#define REG_TRGT1_INIT3_1 0x0308
#define REG_TRGT1_INIT3_2 0x003C
#define REG_TRGT2_INIT3_1 0x0310
#define REG_TRGT2_INIT3_2 0x0314

/* Functions this IPC device can export */
enum sirfsoc_ipc_functions {
	IPC_HW_SPINLOCK = 0,
	IPC_S_NS_0,
	IPC_S_NS_1,
	IPC_S_M3_0,
	IPC_S_M3_1,
	IPC_S_KAL_0,
	IPC_S_KAL_1,
	IPC_NS_M3_0,
	IPC_NS_M3_1,
	IPC_NS_KAL_0,
	IPC_NS_KAL_1,
	IPC_MAX_FUNCTIONS,
};

struct sirf_ipc_device {
	/* Device data */
	struct device *dev;
	struct platform_device *pdev;

	/* Memory resources */
	void __iomem *base;

	/* MFD cells
	 * default cell is hardware spinlock, it ipc contain
	 * rproc devices, then cells can be increased as
	 * rproc_num
	 */
	const struct mfd_cell *cells;
	int num_cells;
};

#endif /* _SIRFSOC_IPC_H_ */
