/*
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

#include <linux/uaccess.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/cpufreq.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/of_gpio.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/time.h>
#include <linux/irq.h>
#include "sirfsoc_gpsdrv.h"

#define GPS_RTC_CLK_SWITCH_OFFSET		0x1C
#define GPS_RTC_CLK_DIV_OFFSET			0x0C

static DEFINE_MUTEX(gps_mutex);

struct gps_dev {
	struct DSP_MEM_INFO		mem_info;
	struct sirfsoc_gps_pdata	*pdata;

	struct clk			*dspclk;
	struct clk			*gpsclk;
	struct clk			*mfclk;
	struct clk			*cpuclk;
	struct clk			*cphclk;

	void __iomem			*iface_base;
	void __iomem			*idma_base;
	void __iomem			*intrctrl_base;
	u32				gps_rtc_base;
	u32				cphifbg_pa_base;
	u32				gps_pa_base;
	unsigned int			sys_rtc_cn;
	unsigned int			irq;

	struct completion		gps_com; /*replace struct semaphore*/

	unsigned long			rtc_counter;
	struct timeval			timevalofday;
	unsigned short			return_code;
	int				return_value;
	int				int_count;
	char				str_status[256];
	char				*p_read;

	struct pm_qos_request qos_request;
	struct cdev			cdev; /*Char device structure */
	struct device			dev;
};

struct sirfsoc_gps_pdata {
	int grst_gpio;
	int clk_out_gpio;
	int shutdown_gpio;
	int lan_en_gpio;

	char *dsp_clk;
	char *gps_clk;
	char *mf_clk;
	char *cpu_clk;
	char *cph_clk;
};

enum {
	RF_TRIG = 0,
	RF_TRIGLITE,
	RF_3IPLUS,
	RF_CD4150
} RF_TYPE;
static int rf_type = RF_TRIG;
struct gps_dev *gpsdev;

#define PORT_ADDR(base, reg)		((base) + (reg))

#define BH2X0BD_DSP_MAJOR		120

#define DSP_IDMA_SET_PM(dev)		writel(0, PORT_ADDR(dev->idma_base,\
		DSPIDMA_SET_TYPE))
#define DSP_IDMA_SET_DM(dev)		writel(1, PORT_ADDR(dev->idma_base,\
		DSPIDMA_SET_TYPE))
#define DSP_IDMA_SET_CM(dev)		writel(2, PORT_ADDR(dev->idma_base,\
		DSPIDMA_SET_TYPE))
#define DSP_IDMA_SET_ADDR(dev, addr)	writel(addr, PORT_ADDR(dev->idma_base,\
		DSPIDMA_SET_ADDR))
#define DSP_IDMA_PUT_DATA(dev, data)	writew(data, PORT_ADDR(dev->idma_base,\
		DSPIDMA_DATA))
#define DSP_IDMA_GET_DATA(dev)	readw(PORT_ADDR(dev->idma_base, DSPIDMA_DATA))
#define DSP_IDMA_REBOOT(dev)	writel(1, PORT_ADDR(dev->idma_base,\
		DSPIDMA_REBOOT))

#define CHECK_DMX_ADDRESS(address, length)	(((address) >= 0) &&\
	((address) + (length) <= (DMX_IN_SIZE + DMX_SWAP_SIZE * 2)))
#define CHECK_DMY_ADDRESS(address, length)	(((address) >= 0) &&\
	((address) + (length) <= DMY_SIZE))
#define CHECK_PM_ADDRESS(address, length)		(((address) >= 0) &&\
	((address) + (length) <= (PM_IN_SIZE + PM_SWAP_SIZE * 2)))

static phys_addr_t sirf_gps_phy_base;
static phys_addr_t sirf_gps_phy_size;
static struct pm_qos_request qos_request;

static void sirfsoc_gps_reset(struct gps_dev *gdev);

void __init sirfsoc_gps_reserve_memblock(void)
{
	sirf_gps_phy_size = 4 * SZ_1M;
	sirf_gps_phy_base = memblock_alloc(sirf_gps_phy_size, SZ_1M);
	memblock_remove(sirf_gps_phy_base, sirf_gps_phy_size);
}
EXPORT_SYMBOL(sirfsoc_gps_reserve_memblock);

void __init sirfsoc_gps_nosave_memblock(void)
{
	register_nosave_region_late(
		__phys_to_pfn(sirf_gps_phy_base),
		__phys_to_pfn(sirf_gps_phy_base +
			sirf_gps_phy_size));
}
EXPORT_SYMBOL(sirfsoc_gps_nosave_memblock);

static struct sirfsoc_gps_pdata gps_pdata = {
	/*3iplus use only*/
	.grst_gpio = -1,
	/*TCXO_ONLY*/
	.clk_out_gpio = -1,
	/*SHUTDOWN*/
	.shutdown_gpio = -1,
	/*LAN_EN*/
	.lan_en_gpio = -1,

	.dsp_clk = "dsp",
	.gps_clk = "gps",
	.mf_clk = "mf",
	.cpu_clk = "cpu",
	.cph_clk = "cphif",
};

static const struct of_device_id sirfsoc_gps_of_match[] = {
	{ .compatible = "sirf,prima2-gps", .data = &gps_pdata, },
	{},
};
MODULE_DEVICE_TABLE(of, sirfsoc_gps_of_match);

static irqreturn_t gps_interrupt(int irq, void *dev_id)
{
	struct gps_dev *dev = (struct gps_dev *)dev_id;

	dev->return_value = readl(PORT_ADDR(dev->iface_base, DSP_INT_RISC));
	writel(1, PORT_ADDR(dev->iface_base, DSP_INT_RISC));

	complete(&(dev->gps_com));
	dev->int_count++;
	dev->rtc_counter = sirfsoc_rtc_iobrg_readl(dev->gps_rtc_base);

	do_gettimeofday(&dev->timevalofday);

	return IRQ_HANDLED;
}

static void sirfsoc_gps_reset(struct gps_dev *gdev)
{
	/*should be software reset for gps related devices*/
	struct platform_device *pdev;
	struct device_node *pdn;
	int ret;

	pdn = of_find_node_by_path(DSP_NODEPATH_DTS);
	if (!pdn)
		dev_err(&gdev->dev, "reset can't find prima2-dsp node\n");
	pdev = of_find_device_by_node(pdn);
	ret = device_reset(&pdev->dev);
	if (ret)
		dev_err(&pdev->dev,
			"Failed to reset prima2-dsp,err= %d\n", ret);

	pdn = of_find_node_by_path(DSPIF_NODEPATH_DTS);
	if (!pdn)
		dev_err(&gdev->dev, "reset can't find prima2-dspif node\n");
	pdev = of_find_device_by_node(pdn);
	ret = device_reset(&pdev->dev);
	if (ret)
		dev_err(&pdev->dev,
			"Failed to reset prima2-dspif,err= %d\n", ret);

	pdn = of_find_node_by_path(GPS_NODEPATH_DTS);
	if (!pdn)
		dev_err(&gdev->dev, "reset can't find prima2-gps node\n");
	pdev = of_find_device_by_node(pdn);
	ret = device_reset(&pdev->dev);
	if (ret)
		dev_err(&pdev->dev,
			"Failed to reset prima2-gps,err= %d\n", ret);
}

static void gps_init_interfaces(struct gps_dev *gdev)
{
	unsigned long reg_value;

	/*enable clock and reset */
	if (clk_prepare_enable(gdev->dspclk))
		dev_err(&gdev->dev, "DSP CLK Enable Failed\n");

	/*software reset */
	sirfsoc_gps_reset(gdev);

	/*Enable RISC interrupt from DSP */
	reg_value = readl(PORT_ADDR(gdev->intrctrl_base, 0x0018));
	reg_value |= 0x80;
	writel(reg_value, PORT_ADDR(gdev->intrctrl_base, 0x0018));

	/*Allow DSP access Interrupt Controller */
	writel(1, PORT_ADDR(gdev->intrctrl_base, 0x0030));

	/*Let risc take control at opening */
	writel(1, PORT_ADDR(gdev->iface_base, DSP_DIV_CLK));

	writel(1, PORT_ADDR(gdev->iface_base, DSPREG_MODE));
	writel(0x30000, PORT_ADDR(gdev->iface_base, DSPDMA_MODE));
	writel(0, PORT_ADDR(gdev->iface_base, DSPREG_MODE));

	/*Enable GPS and Matchfilter clocks */
	if (clk_prepare_enable(gdev->gpsclk))
		dev_err(&gdev->dev, "GPS CLK Enable Failed\n");
	if (clk_prepare_enable(gdev->mfclk))
		dev_err(&gdev->dev, "MF CLK Enable Failed\n");
	/*Enable CPH IF Bridge clock */
	if (clk_prepare_enable(gdev->cphclk))
		dev_err(&gdev->dev, "CPH CLK Enable Failed\n");
}

static void gps_uninit_interfaces(struct gps_dev *dev)
{
	clk_disable_unprepare(dev->dspclk);
	clk_disable_unprepare(dev->mfclk);
	clk_disable_unprepare(dev->gpsclk);
	clk_disable_unprepare(dev->cphclk);
}

/*gps_open success should return 0*/
static int gps_open(struct inode *inode, struct file *filp)
{
	int ret = 0;
	struct gps_dev *gdev;
	/*success return 1*/
	ret = mutex_trylock(&gps_mutex);
	if (!ret) {
		ret = -EBUSY;
		goto out_error;
	}
	ret = 0;
	gdev = container_of(inode->i_cdev, struct gps_dev, cdev);
	dev_info(&gpsdev->dev, "Enter gps_open fileflag 0x%x\n", filp->f_flags);
	gdev->p_read = gdev->str_status;

out_error:
	return ret;
}

static int gps_release(struct inode *inode, struct file *filp)
{
	struct gps_dev *dev; /*device information */
	mutex_unlock(&gps_mutex);
	dev = container_of(inode->i_cdev, struct gps_dev, cdev);
	gps_uninit_interfaces(dev);

	return 0;
}


static void compose_status(struct gps_dev *dev)
{
	int len;

	len = sprintf(dev->str_status, "DSP status:\n\tReturn Code %04x\n",
		(dev->return_value >> 16) & 0xffff);
	len = sprintf(dev->str_status + len, "\tInt Count %d\n",
		dev->int_count);
	dev->p_read = dev->str_status;
}

static ssize_t gps_read(struct file *filp, char __user *buf,
	size_t count, loff_t *f_pos)
{
	struct gps_dev *dev = gpsdev;
	int copy_count = 0;

	while (copy_count < count && *dev->p_read != 0) {
		put_user(*dev->p_read++, buf);
		buf++;
		copy_count++;
	}
	return copy_count;
}

static long gps_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct gps_dev *gdev; /*device information */
	struct gps_dev *gps_device;
	int i, ret, retval = 0;
	int core, rate;
	unsigned int timeout, msdly = 0;

	struct clk *clk = NULL;
	struct GPS_RTC_CLK_INFO clk_info;
	struct cpufreq_policy policy;

	struct DSP_BUF_INFO gps_buffer_info;
	struct DSP_RW_GENERAL_REG gps_gen_reg;
	unsigned short dataw, *p_word;
	unsigned long datal, *p_dword;
	struct inode *inode;
	unsigned long gpsrtc = 0, sysrtc = 0;

	inode = filp->f_dentry->d_inode;
	gdev = container_of(inode->i_cdev, struct gps_dev, cdev);
	gps_device = gdev;
	if (_IOC_TYPE(cmd) != DSP_IOC_MAGIC)
		return -EINVAL;
	if (_IOC_NR(cmd) > DSP_IOC_MAXNR)
		return -EINVAL;

	switch (cmd) {
	case IOCTL_SET_CPU_FREQ_TO_MAX:
		if (!cpufreq_get_policy(&policy, 0))
			/*keep cpuferq max to make sure caculation
			 *perfermance in GNSS acquisition channels*/
			pm_qos_update_request(&qos_request,
					policy.cpuinfo.max_freq);
		break;

	case IOCTL_RESET_CPU_FREQ_TO_DEFAULT:
		if (!cpufreq_get_policy(&policy, 0))
			/*set to default after GNSS got fix*/
			pm_qos_update_request(&qos_request,
					policy.cpuinfo.min_freq);
		break;

	case IOCTL_DSP_READ_DMX_BUFFER:
		if (copy_from_user(&gps_buffer_info, (void __user *)arg,
				sizeof(struct DSP_BUF_INFO)))
			return -EINVAL;
#if 0
		if (!CHECK_DMX_ADDRESS(gps_buffer_info.addr,
				gps_buffer_info.length))
			return -EINVAL;
#endif
		DSP_IDMA_SET_ADDR(gdev, gps_buffer_info.addr);
		DSP_IDMA_SET_DM(gdev);
		p_word = (unsigned short __user *)gps_buffer_info.p_buf;

		for (i = 0; i < gps_buffer_info.length; i++) {
			dataw = DSP_IDMA_GET_DATA(gdev);
			put_user(dataw, p_word);
			p_word++;
		}
		break;

	case IOCTL_DSP_WRITE_DMX_BUFFER:
		if (copy_from_user(&gps_buffer_info, (void __user *)arg,
				sizeof(struct DSP_BUF_INFO)))
			return -EINVAL;

#if 0
		if (!CHECK_DMX_ADDRESS(gps_buffer_info.addr,
				gps_buffer_info.length))
			return -EINVAL;
#endif
		DSP_IDMA_SET_ADDR(gdev, gps_buffer_info.addr);
		DSP_IDMA_SET_DM(gdev);
		p_word = (unsigned short __user *)gps_buffer_info.p_buf;

		for (i = 0; i < gps_buffer_info.length; i++) {
			get_user(dataw, p_word);
			DSP_IDMA_PUT_DATA(gdev, dataw);
			p_word++;
		}
		break;

	case IOCTL_DSP_READ_DMY_BUFFER:
		if (copy_from_user(&gps_buffer_info, (void __user *)arg,
				sizeof(struct DSP_BUF_INFO)))
			return -EINVAL;

		if (!CHECK_DMY_ADDRESS(gps_buffer_info.addr,
				gps_buffer_info.length))
			return -EINVAL;
		DSP_IDMA_SET_ADDR(gdev, gps_buffer_info.addr);
		DSP_IDMA_SET_CM(gdev);
		p_word = (unsigned short __user *)gps_buffer_info.p_buf;

		for (i = 0; i < gps_buffer_info.length; i++) {
			dataw = DSP_IDMA_GET_DATA(gdev);
			put_user(dataw, p_word);
			p_word++;
		}
		break;

	case IOCTL_DSP_WRITE_DMY_BUFFER:
		if (copy_from_user(&gps_buffer_info, (void __user *)arg,
				sizeof(struct DSP_BUF_INFO)))
			return -EINVAL;

		if (!CHECK_DMY_ADDRESS(gps_buffer_info.addr,
				gps_buffer_info.length))
			return -EINVAL;
		DSP_IDMA_SET_ADDR(gdev, gps_buffer_info.addr);
		DSP_IDMA_SET_CM(gdev);
		p_word = (unsigned short __user *)gps_buffer_info.p_buf;

		for (i = 0; i < gps_buffer_info.length; i++) {
			get_user(dataw, p_word);
			DSP_IDMA_PUT_DATA(gdev, dataw);
			p_word++;
		}
		break;

	case IOCTL_DSP_READ_PM_BUFFER:
		if (copy_from_user(&gps_buffer_info, (void __user *)arg,
				sizeof(struct DSP_BUF_INFO)))
			return -EINVAL;

		if (!CHECK_PM_ADDRESS(gps_buffer_info.addr,
				gps_buffer_info.length))
			return -EINVAL;
		DSP_IDMA_SET_ADDR(gdev, gps_buffer_info.addr);
		DSP_IDMA_SET_PM(gdev);
		p_dword = (unsigned long __user *)gps_buffer_info.p_buf;

		for (i = 0; i < gps_buffer_info.length; i++) {
			datal = DSP_IDMA_GET_DATA(gdev);
			datal |= ((unsigned long)DSP_IDMA_GET_DATA(gdev)) << 16;
			put_user(datal, p_dword);
			p_dword++;
		}
		break;

	case IOCTL_DSP_WRITE_PM_BUFFER:
		if (copy_from_user(&gps_buffer_info, (void __user *)arg,
				sizeof(struct DSP_BUF_INFO)))
			return -EINVAL;

		if (!CHECK_PM_ADDRESS(gps_buffer_info.addr,
				gps_buffer_info.length))
			return -EINVAL;
		DSP_IDMA_SET_ADDR(gdev, gps_buffer_info.addr);
		DSP_IDMA_SET_PM(gdev);
		p_dword = (unsigned long __user *)gps_buffer_info.p_buf;

		for (i = 0; i < gps_buffer_info.length; i++) {
			get_user(datal, p_dword);
			DSP_IDMA_PUT_DATA(gdev,
					(unsigned short)(datal & 0xffff));
			DSP_IDMA_PUT_DATA(gdev, (unsigned short)(datal >> 16));
			p_dword++;
		}
		break;

	case IOCTL_DSP_EXECUTE:
		/*for simplify, skip DSP running check and wait for DSP
		 *complete step just trigger the DSP interrupt with the address
		 */
		writel(arg, PORT_ADDR(gdev->iface_base, RISC_INT_DSP));
		break;

	case IOCTL_DSP_START_DSP:
		DSP_IDMA_REBOOT(gdev);
		break;

	case IOCTL_DSP_WAIT_COMPLETE:
		/*wait complete function will be added later */
		break;

	case IOCTL_DSP_GET_STATUS:
		/*get status function will be added later */
		break;

	case IOCTL_DSP_READ_GEN_REG:
		if (copy_from_user(&gps_gen_reg, (void __user *)arg,
				sizeof(struct DSP_RW_GENERAL_REG)))
			return -EINVAL;
		if (gps_gen_reg.index < 0 || gps_gen_reg.index >= 4)
			return -EINVAL;
		gps_gen_reg.value = readl(PORT_ADDR(gdev->iface_base,
				DSP_GEN_REG0 + ((gps_gen_reg.index) << 2)));
		if (copy_to_user((void __user *)arg, &gps_gen_reg,
				sizeof(struct DSP_RW_GENERAL_REG)))
			return -EINVAL;
		break;

	case IOCTL_DSP_WRITE_GEN_REG:
		if (copy_from_user(&gps_gen_reg, (void __user *)arg,
				sizeof(struct DSP_RW_GENERAL_REG)))
			return -EINVAL;
		if (gps_gen_reg.index < 0 || gps_gen_reg.index >= 4)
			return -EINVAL;
		writel(gps_gen_reg.value, PORT_ADDR(gdev->iface_base,
				DSP_GEN_REG0 + ((gps_gen_reg.index) << 2)));
		break;

	case IOCTL_DSP_GET_RETUEN_CODE:
		put_user((unsigned short)(gdev->return_value >> 16),
			(unsigned short __user *)arg);
		put_user((unsigned long)(gdev->rtc_counter),
			(unsigned long __user *)(arg + 4));
		put_user(*(unsigned long *)((char *)&gdev->timevalofday),
			(unsigned long __user *)(arg + 8));
		put_user(*(unsigned long *)((char *)&gdev->timevalofday + 4),
			(unsigned long __user *)(arg + 12));
		break;

	case IOCTL_DSP_WAIT_INTERRUPT:
		if (get_user(timeout, (unsigned int __user *)arg))
			return -EFAULT;
		ret = wait_for_completion_interruptible_timeout(&gdev->gps_com,
			msecs_to_jiffies(timeout));
		if (ret > 0)
			retval = 0;
		else
			retval = ret ? ret : -ETIMEDOUT;
		break;

	case IOCTL_DSP_GET_CLOCK_INFO:
		if (copy_from_user(&core, (int __user *) arg, sizeof(int)))
			return -EINVAL;
		clk = ((core == CPU_CLOCK) ?
				gps_device->cpuclk : gps_device->gpsclk);
		rate = clk_get_rate(clk);
		if (copy_to_user((int __user *) arg, &rate, sizeof(int)))
			return -EINVAL;
		break;

	case IOCTL_DSP_GET_RES_MEM_INFO:
		dev_dbg(&gdev->dev, "DSP: MemResBase 0x%x Size 0x%x\n",
			(unsigned int)(gdev->mem_info.addr),
			(unsigned int) (gdev->mem_info.size));
		if (copy_to_user((void __user *) arg, &(gdev->mem_info),
				sizeof(struct DSP_MEM_INFO)))
			return -EINVAL;
		break;

	case IOCTL_DSP_GPS_RF_POWER_CTRL:
		/*It's not used for prima2 */
		break;

	case IOCTL_DSP_GPS_RF_RESET:
		if (get_user(msdly, (unsigned int __user *)arg))
			return -EFAULT;
		if (gpio_is_valid(gdev->pdata->grst_gpio)) {
			dev_info(&gdev->dev,
				"Doing GPS RF Reset: Delay: %d ms\n", msdly);
			gpio_set_value(gdev->pdata->grst_gpio, 0);
			msleep(msdly);
			gpio_set_value(gdev->pdata->grst_gpio, 1);
		} else {
			dev_info(&gdev->dev,
				"gps: RF reset gpio is not specified!\n");
			return -EINVAL;
		}
		break;

	case IOCTL_RF_CLK_OUT:
		/*
		 *SHUTDOWN(shutdown_gpio)+TCXO_ONLY(clk_out_gpio)pin setting:
		 *High+High, full working: GPS RF works, and output TCXO 26MHz;
		 *High+Low,  GPS RF stop working, but output TCXO 26MHz;
		 *Low +Low,  full stop;
		 *Low +High, full stop;
		 *Default: High + High.
		 *LAN_EN_pin(lan_en_gpio) should set HIGH to enable
		 *signal gain when using internal gps atena.
		 */
		if (gpio_is_valid(gdev->pdata->clk_out_gpio)) {
			gpio_set_value(gdev->pdata->clk_out_gpio, 0);
			msleep(100);
			gpio_set_value(gdev->pdata->clk_out_gpio, 1);
		} else {
			printk_once(KERN_ERR "gps: clock output gpio is not specified!\n");
			return -EINVAL;
		}
		/*SHUTDOWN is not allow to set 0 on TaiShan or LungChing
		 *which will never set according GPIO valid here */
		if (gpio_is_valid(gdev->pdata->shutdown_gpio)) {
			gpio_set_value(gdev->pdata->shutdown_gpio, 0);
			msleep(100);
			gpio_set_value(gdev->pdata->shutdown_gpio, 1);
		} else {
			printk_once(KERN_ERR "gps: shutdown gpio is not specified!\n");
			return -EINVAL;
		}
		if (gpio_is_valid(gdev->pdata->lan_en_gpio)) {
			gpio_set_value(gdev->pdata->lan_en_gpio, 0);
			msleep(100);
			gpio_set_value(gdev->pdata->lan_en_gpio, 1);
		} else {
			printk_once(KERN_ERR "gps: lan_en gpio is not specified!\n");
			return -EINVAL;
		}
		break;

	case IOCTL_DSP_SET_CLK_SRC:
		if (copy_from_user(&clk_info, (void __user *)arg,
				sizeof(struct GPS_RTC_CLK_INFO)))
			return -EINVAL;

		sirfsoc_rtc_iobrg_writel(clk_info.clock_id,
			gdev->gps_rtc_base + GPS_RTC_CLK_SWITCH_OFFSET);

		sirfsoc_rtc_iobrg_writel(sirfsoc_rtc_iobrg_readl(
				gdev->gps_rtc_base +
					GPS_RTC_CLK_SWITCH_OFFSET) | 0x4,
				gdev->gps_rtc_base +
					GPS_RTC_CLK_SWITCH_OFFSET);

		/*read sysrtc counter, then sync gpsrtc with 64*sysrtc */
		sysrtc = sirfsoc_rtc_iobrg_readl(gdev->sys_rtc_cn);
		sirfsoc_rtc_iobrg_writel(64 * sysrtc, gdev->gps_rtc_base);

		if (clk_info.divider != -1) {
			sirfsoc_rtc_iobrg_writel(clk_info.divider,
				gdev->gps_rtc_base + GPS_RTC_CLK_DIV_OFFSET);
		} else {
			dev_dbg(&gdev->dev,
				"IOCTL_DSP_SET_CLK_SRC: Using default divider: x%x\n",
				sirfsoc_rtc_iobrg_readl(gdev->gps_rtc_base +
					GPS_RTC_CLK_DIV_OFFSET));
		}

		msleep(20);
		break;

	case IOCTL_DSP_INIT_GPS:
		gps_init_interfaces(gdev);
		break;

	case IOCTL_DSP_UNINIT_GPS:
		gps_uninit_interfaces(gdev);
		break;

	case IOCTL_DSP_READ_RTC:
		gpsrtc = sirfsoc_rtc_iobrg_readl(gdev->gps_rtc_base);
		put_user(gpsrtc, (unsigned long __user *)arg);
		break;

	case IOCTL_GPIO_REQUEST:
		if (rf_type == RF_3IPLUS) {
			if (!gpio_is_valid(gps_device->pdata->grst_gpio))
				dev_err(&gdev->dev, "GRST not valid\n");
			if (!gpio_request(
				gps_device->pdata->grst_gpio, "grst"))
				dev_err(&gdev->dev,
					"GRST GPIO-%d Request Failed\n",
					gps_device->pdata->grst_gpio);
			if (!gpio_direction_output(
				gps_device->pdata->grst_gpio, 1) < 0)
				dev_err(&gdev->dev,
					"Can not set GPIO-%d as output GPIO\n",
					gps_device->pdata->grst_gpio);
		} else if (rf_type == RF_TRIGLITE) {
			if (!gpio_is_valid(gps_device->pdata->clk_out_gpio))
				dev_err(&gdev->dev, "CLK_OUT not valid\n");
			if (!gpio_request(gps_device->pdata->clk_out_gpio,
						"clk-out-gpios"))
				dev_err(&gdev->dev,
					"GRST GPIO-%d Request Failed\n",
					gps_device->pdata->clk_out_gpio);
			if (!gpio_direction_output(
					gps_device->pdata->clk_out_gpio, 1))
				dev_err(&gdev->dev,
					"Can not set GPIO-%d as output GPIO\n",
					gps_device->pdata->clk_out_gpio);
		} else {
			dev_info(&gdev->dev,
				"gps: IOCTL_GPIO_REQUEST not need!\n");
		}
		break;

	case IOCTL_GPIO_FREE:
		if (rf_type == RF_TRIGLITE) {
			if (gpio_is_valid(gps_device->pdata->clk_out_gpio))
				gpio_free(gps_device->pdata->clk_out_gpio);
		} else if (rf_type == RF_3IPLUS) {
			if (gpio_is_valid(gps_device->pdata->grst_gpio))
				gpio_free(gps_device->pdata->grst_gpio);
		}
		break;
	}
	compose_status(gdev);
	return retval;
}

static int gps_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct gps_dev *gdev = gpsdev;

	if (((vma->vm_pgoff<<PAGE_SHIFT) != gdev->cphifbg_pa_base) &&
		((vma->vm_pgoff<<PAGE_SHIFT) != (gdev->gps_pa_base + 0x4000)) &&
		((vma->vm_pgoff<<PAGE_SHIFT) != gdev->mem_info.addr)) {
		if (pgprot_val(vma->vm_page_prot) !=
				pgprot_val(PAGE_READONLY)) {
			dev_err(&gdev->dev,
				"GPS/CPHB: ReadOnly mapping is not allowed\n");
			return -EINVAL;
		} else {
			if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
					vma->vm_end - vma->vm_start,
					pgprot_noncached(PAGE_READONLY))) {
				return -EAGAIN;
			}
			return 0;
		}
	} else if (((vma->vm_pgoff<<PAGE_SHIFT) != gdev->mem_info.addr) &&
		((vma->vm_end - vma->vm_start) > 0x1000)) {
		dev_err(&gdev->dev, "MMap for more than 4K is not allowed\n");
		return -EINVAL;
	} else {
		if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
				vma->vm_end - vma->vm_start,
				pgprot_noncached(vma->vm_page_prot))) {
			return -EAGAIN;
		}
		return 0;
	}
}

static const struct file_operations gps_fops = {
	.owner =    THIS_MODULE,
	.read =     gps_read,
	.unlocked_ioctl =    gps_ioctl,
	.open =     gps_open,
	.release =  gps_release,
	.mmap =	    gps_mmap,
};

static ssize_t gps_sys_dump_status(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct gps_dev *gps_device = dev_get_drvdata(dev);
	if (!gps_device) {
		return sprintf(buf, "Error in extracting GPS device handle.\n");
	} else {
		ssize_t cnt = 0;
		while (gps_device->p_read[cnt] != 0) {
			buf[cnt] = gps_device->p_read[cnt];
			cnt++;
		}
		return cnt;
	}
}
static DEVICE_ATTR(status, S_IRUGO, gps_sys_dump_status, NULL);

static int sirf_gps_probe(struct platform_device *pdev)
{
	struct gps_dev *gps_device = NULL;
	struct resource *plat_res = NULL;
	const struct of_device_id *match;
	struct device_node *pdn;
	struct platform_device *platdev = NULL;
	int ret = 0;

	match = of_match_node(sirfsoc_gps_of_match, pdev->dev.of_node);
	gps_device = kzalloc(sizeof(struct gps_dev), GFP_KERNEL);
	if (gps_device == NULL) {
		dev_err(&pdev->dev, "GPS: MemAlloc Failed\n");
		ret = -ENOMEM;
		goto end;
	}

	gps_device->dev = pdev->dev;

	/*original dsp-mem belong reserve space alloced by sirfsoc_reserve*/
	gps_device->mem_info.addr = sirf_gps_phy_base;
	gps_device->mem_info.size = sirf_gps_phy_size;

	gps_device->pdata = (struct sirfsoc_gps_pdata *)match->data;
	if (gps_device->pdata == NULL) {
		dev_err(&pdev->dev, "GPS: Platform data is NULL\n");
		ret = -EINVAL;
		goto rel_gpsdev;
	}

	plat_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (plat_res == NULL) {
		dev_err(&pdev->dev, "GPS:can't get prima2-gps resource\n");
		ret = -EINVAL;
		goto rel_gpsdev;
	}
	gps_device->gps_pa_base = plat_res->start;

	/*get gps clock*/
	gps_device->gpsclk = devm_clk_get(&pdev->dev, "gps");
	if (IS_ERR(gps_device->gpsclk)) {
		dev_err(&pdev->dev,
			"Failed to get gpsclk,err= %ld\n",
					PTR_ERR(gps_device->gpsclk));
		ret = -EINVAL;
		goto rel_gpsdev;
	}
	/*get mf clock*/
	gps_device->mfclk = devm_clk_get(&pdev->dev, "mf");
	if (IS_ERR(gps_device->mfclk)) {
		dev_err(&pdev->dev,
			"Failed to get mfclk,err= %ld\n",
					PTR_ERR(gps_device->mfclk));
		ret = -EINVAL;
		goto rel_gpsdev;
	}
	/*get cpu clock*/
	gps_device->cpuclk = devm_clk_get(&pdev->dev, "cpu");
	if (IS_ERR(gps_device->cpuclk)) {
		dev_err(&pdev->dev,
			"Failed to get cpuclk,err= %ld\n",
					PTR_ERR(gps_device->cpuclk));
		ret = -EINVAL;
		goto rel_gpsdev;
	}

	/*get gps gpios for none TriG RF*/
	if (RF_TRIG != rf_type) {
		gps_device->pdata->grst_gpio = of_get_named_gpio(
			pdev->dev.of_node, "grst-gpios", 0);
		gps_device->pdata->clk_out_gpio = of_get_named_gpio(
			pdev->dev.of_node, "clk-out-gpios", 0);
		gps_device->pdata->shutdown_gpio = of_get_named_gpio(
			pdev->dev.of_node, "shutdown-gpios", 0);
		gps_device->pdata->lan_en_gpio = of_get_named_gpio(
			pdev->dev.of_node, "lan-en-gpios", 0);
	}

	if (rf_type == RF_3IPLUS) {
		if (gpio_is_valid(gps_device->pdata->grst_gpio)) {
			if (gpio_request(
				gps_device->pdata->grst_gpio, "grst")) {
				dev_err(&pdev->dev,
					"GRST GPIO-%d Request Failed\n",
					gps_device->pdata->grst_gpio);
				ret = -EINVAL;
				goto rel_gpsdev;
			}
			if (gpio_direction_output(
				gps_device->pdata->grst_gpio, 1) < 0) {
				dev_err(&pdev->dev,
					"Can not set GPIO-%d as output GPIO\n",
					gps_device->pdata->grst_gpio);
				goto free_grst;
			}
		}
	} else if (rf_type == RF_TRIGLITE) {
		if (gpio_is_valid(gps_device->pdata->clk_out_gpio)) {
			if (gpio_request(
				gps_device->pdata->clk_out_gpio, "clk-out")) {
				dev_err(&pdev->dev,
					"GRST GPIO-%d Request Failed\n",
					gps_device->pdata->clk_out_gpio);
				ret = -EINVAL;
				goto free_grst;
			}
			if (gpio_direction_output(
				gps_device->pdata->clk_out_gpio, 1) < 0) {
				dev_err(&pdev->dev,
					"Can not set GPIO-%d as output GPIO\n",
					gps_device->pdata->clk_out_gpio);
				goto free_clk_out;
			}
		}

		if (gpio_is_valid(gps_device->pdata->shutdown_gpio)) {
			if (gpio_request(
				gps_device->pdata->shutdown_gpio, "shutdown")) {
				dev_err(&pdev->dev,
					"GRST GPIO-%d Request Failed\n",
					gps_device->pdata->shutdown_gpio);
				ret = -EINVAL;
				goto free_clk_out;
			}
			if (gpio_direction_output(
				gps_device->pdata->shutdown_gpio, 1) < 0) {
				dev_err(&pdev->dev,
					"Can not set GPIO-%d as input GPIO\n",
					gps_device->pdata->shutdown_gpio);
				goto free_shutdown;
			}
		}

		if (gpio_is_valid(gps_device->pdata->lan_en_gpio)) {
			if (gpio_request(
				gps_device->pdata->lan_en_gpio, "lan_en")) {
				dev_err(&pdev->dev,
					"GRST GPIO-%d Request Failed\n",
					gps_device->pdata->lan_en_gpio);
				ret = -EINVAL;
				goto free_shutdown;
			}
			if (gpio_direction_output(
				gps_device->pdata->lan_en_gpio, 1) < 0) {
				dev_err(&pdev->dev,
					"Can not set GPIO-%d as output GPIO\n",
					gps_device->pdata->lan_en_gpio);
				goto free_lan_en;
			}
		}
	}

	pdn = of_find_node_by_path(CPHIF_NODEPATH_DTS);
	if (!pdn) {
		dev_err(&pdev->dev, "GPS: can't find node name cphifbg!\n");
		goto free_lan_en;
	}
	platdev = of_find_device_by_node(pdn);
	/*get cphif clock*/
	gps_device->cphclk = devm_clk_get(&platdev->dev, NULL);
	if (IS_ERR(gps_device->cphclk)) {
		dev_err(&platdev->dev,
			"Failed to get cphclk,err= %ld\n",
					PTR_ERR(gps_device->cphclk));
		ret = -EINVAL;
		goto free_lan_en;
	}
	/*get cphif pa base*/
	of_address_to_resource(pdn, 0, plat_res);
	if (plat_res == NULL) {
		dev_err(&pdev->dev, "GPS:can't get cphifbg resource\n");
		ret = -EINVAL;
		goto free_lan_en;
	}
	gps_device->cphifbg_pa_base = plat_res->start;

	pdn = of_find_node_by_path(DSPIF_NODEPATH_DTS);
	if (!pdn) {
		dev_err(&pdev->dev, "GPS: can't find node name dspif!\n");
		ret = -EINVAL;
		goto free_lan_en;
	}
	gps_device->iface_base = of_iomap(pdn, 0);
	if (!gps_device->iface_base) {
		dev_err(&pdev->dev, "GPS: of_iomap failed for dsp-ifreg\n");
		ret = -EINVAL;
		goto free_lan_en;
	}

	pdn = of_find_node_by_path(DSP_NODEPATH_DTS);
	if (!pdn) {
		dev_err(&pdev->dev, "GPS: can't find node name dsp\n");
		goto unmap_iface;
	}
	platdev = of_find_device_by_node(pdn);
	/*get dsp interrupt num*/
	gps_device->irq = platform_get_irq(platdev, 0);
	/*clear any pending interrupt before enable interrupt */
	writel(1, PORT_ADDR(gps_device->iface_base, DSP_INT_RISC));

	if (devm_request_irq(&platdev->dev, gps_device->irq,
				gps_interrupt, 0, "prima2-dsp", gps_device))
		dev_err(&pdev->dev, "GPS:DSP failed to request_irq\n");

	/*get dsp clock*/
	gps_device->dspclk = devm_clk_get(&platdev->dev, NULL);
	if (IS_ERR(gps_device->dspclk)) {
		dev_err(&platdev->dev,
			"Failed to get dspclk,err= %ld\n",
					PTR_ERR(gps_device->dspclk));
		ret = -EINVAL;
		goto unmap_iface;
	}
	gps_device->idma_base = of_iomap(pdn, 0);
	if (!gps_device->idma_base) {
		dev_err(&pdev->dev, "GPS: of_iomap failed for prima2-dsp\n");
		ret = -EINVAL;
		goto unmap_iface;
	}

	/*get gps_rtc_base */
	pdn = of_find_node_by_path(GPSRTC_NODEPATH_DTS);
	if (!pdn) {
		dev_err(&pdev->dev, "GPS: can't find node name gpsrtc\n");
		ret = -EINVAL;
		goto unmap_idma;
	}
	ret = of_property_read_u32(pdn, "reg", &gps_device->gps_rtc_base);
	if (ret) {
		dev_err(&pdev->dev, "can't find gpsrtc base in dtb\n");
		ret = -EINVAL;
		goto unmap_idma;
	}

	/*get sys_rtc_base */
	pdn = of_find_node_by_path(SYSRTC_NODEPATH_DTS);
	if (!pdn) {
		dev_err(&pdev->dev, "GPS: can't find node name sysrtc\n");
		ret = -EINVAL;
		goto unmap_idma;
	}
	ret = of_property_read_u32(pdn, "reg", &gps_device->sys_rtc_cn);
	if (ret) {
		dev_err(&pdev->dev, "can't find sysrtc base in dtb\n");
		ret = -EINVAL;
		goto unmap_idma;
	}

	pdn = of_find_node_by_path(INTC_NODEPATH_DTS);
	if (!pdn) {
		dev_err(&pdev->dev, "GPS: can't find node name intc\n");
		ret = -EINVAL;
		goto unmap_idma;
	}
	gps_device->intrctrl_base = of_iomap(pdn, 0);
	if (!gps_device->intrctrl_base) {
		dev_err(&pdev->dev, "GPS: of_iomap failed for prima2-intc\n");
		ret = -EINVAL;
		goto unmap_idma;
	}
	pm_qos_add_request(&qos_request, PM_QOS_CPU_FREQ_MIN,
			PM_QOS_DEFAULT_VALUE);
	platform_set_drvdata(pdev, gps_device);
	gpsdev = gps_device;

	if (device_create_file(&(pdev->dev), &dev_attr_status) < 0) {
		dev_err(&pdev->dev,
			"GPS:Error in Creating 'status' sys attribute\n");
		goto unmap_intrctrl;
	}

	ret = register_chrdev_region(MKDEV(BH2X0BD_DSP_MAJOR, 0), 1, "gps");
	if (ret < 0) {
		dev_err(&pdev->dev, "GPS: can't register device\n");
		goto remove_sys_entry;
	}

	cdev_init(&(gps_device->cdev), &gps_fops);
	gps_device->cdev.owner = THIS_MODULE;
	gps_device->cdev.ops = &gps_fops;
	ret = cdev_add(&(gps_device->cdev), MKDEV(BH2X0BD_DSP_MAJOR, 0), 1);
	if (ret) {
		dev_err(&pdev->dev, "GPS: Error adding dsp\n");
		goto unreg_char;
	} else {
		init_completion(&(gps_device->gps_com));
		compose_status(gps_device);
		dev_info(&pdev->dev, "Ready to operate!");
		return 0;
	}

unreg_char:
	unregister_chrdev_region(MKDEV(BH2X0BD_DSP_MAJOR, 0), 1);
remove_sys_entry:
	device_remove_file(&(pdev->dev), &dev_attr_status);
unmap_intrctrl:
	platform_set_drvdata(pdev, NULL);
	iounmap((void *)(gps_device->intrctrl_base));
unmap_idma:
	iounmap((void *)(gps_device->idma_base));
unmap_iface:
	iounmap((void *)(gps_device->iface_base));
free_lan_en:
	if (rf_type == RF_TRIGLITE) {
		if (gpio_is_valid(gps_device->pdata->lan_en_gpio))
			gpio_free(gps_device->pdata->lan_en_gpio);
	}
free_shutdown:
	if (rf_type == RF_TRIGLITE) {
		if (gpio_is_valid(gps_device->pdata->shutdown_gpio))
			gpio_free(gps_device->pdata->shutdown_gpio);
	}
free_clk_out:
	if (rf_type == RF_TRIGLITE) {
		if (gpio_is_valid(gps_device->pdata->clk_out_gpio))
			gpio_free(gps_device->pdata->clk_out_gpio);
	}
free_grst:
	if (rf_type == RF_3IPLUS) {
		if (gpio_is_valid(gps_device->pdata->grst_gpio))
			gpio_free(gps_device->pdata->grst_gpio);
	}
rel_gpsdev:
	kzfree(gps_device);
	gpsdev = NULL;
end:
	return ret;
}

static int sirf_gps_remove(struct platform_device *pdev)
{
	struct gps_dev *gps_device = platform_get_drvdata(pdev);

	pm_qos_remove_request(&qos_request);
	cdev_del(&(gps_device->cdev));
	unregister_chrdev_region(MKDEV(BH2X0BD_DSP_MAJOR, 0), 1);

	device_remove_file(&(pdev->dev), &dev_attr_status);
	platform_set_drvdata(pdev, NULL);
	iounmap((void *)(gps_device->intrctrl_base));
	iounmap((void *)(gps_device->idma_base));
	iounmap((void *)(gps_device->iface_base));
	if (rf_type == RF_TRIGLITE) {
		if (gpio_is_valid(gps_device->pdata->clk_out_gpio))
			gpio_free(gps_device->pdata->clk_out_gpio);
	} else if (rf_type == RF_3IPLUS) {
		if (gpio_is_valid(gps_device->pdata->grst_gpio))
			gpio_free(gps_device->pdata->grst_gpio);
	}

	kfree(gps_device);
	gpsdev = NULL;

	return 0;
}

static struct platform_driver gps_sirf_driver = {
	.probe =	sirf_gps_probe,
	.remove =	sirf_gps_remove,
	.driver	=	{
		.name = "sirfsoc-gps",
		.owner = THIS_MODULE,
		.of_match_table = sirfsoc_gps_of_match,
	},
};

module_platform_driver(gps_sirf_driver);
MODULE_DESCRIPTION("SIRFSOC GPS DSP driver");
MODULE_LICENSE("GPL");
