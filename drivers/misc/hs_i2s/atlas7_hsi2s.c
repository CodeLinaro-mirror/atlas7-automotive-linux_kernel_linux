/*
 *SiRF ATLAS7 HS-I2S controller driver
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

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dma-direction.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include "atlas7_hsi2s.h"

#define DRV_NAME "hsi2s"
/* there are 2 HS-I2S port, save and check current device number by dev_num
 * the HS-I2S0 will use /dev/hsi2s1 and HS-I2S1 will use /dev/hsi2s2
 */
static int dev_num;

/* all sys attribute just used for debug */
static ssize_t bclk_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct atlas7_hsi2s *i2s = platform_get_drvdata(pdev);
	static const char *mode[BCLK_MODE_MAX] = {
		[BCLK_MODE_CONTINUOUS] = "Continuous",
		[BCLK_MODE_BURST] = "Burst",
	};
	const char *str;

	if (i2s->i2s_ctrl.bclk_mode > BCLK_MODE_LAST)
		i2s->i2s_ctrl.bclk_mode = BCLK_MODE_CONTINUOUS;

	str = mode[i2s->i2s_ctrl.bclk_mode];
	return snprintf(buf, PAGE_SIZE, "%d: %s\n",
			i2s->i2s_ctrl.bclk_mode, str);
}

static ssize_t bclk_mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct atlas7_hsi2s *i2s = platform_get_drvdata(pdev);
	int ret;
	unsigned long value;

	ret = kstrtoul(buf, 0, &value);
	if (value > BCLK_MODE_LAST)
		value = BCLK_MODE_CONTINUOUS;
	i2s->i2s_ctrl.bclk_mode = value;

	return count;
}
static DEVICE_ATTR(bclk_mode, S_IRUSR | S_IWUSR,
		bclk_mode_show, bclk_mode_store);

static ssize_t word_size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct atlas7_hsi2s *i2s = platform_get_drvdata(pdev);

	return snprintf(buf, PAGE_SIZE, "%d\n", i2s->i2s_ctrl.word_size);
}

static ssize_t word_size_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct atlas7_hsi2s *i2s = platform_get_drvdata(pdev);
	int ret;
	unsigned long value;

	ret = kstrtoul(buf, 0, &value);
	switch (value) {
	case 16:
	case 24:
	case 32:
		i2s->i2s_ctrl.word_size = value;
		break;
	default:
		i2s->i2s_ctrl.word_size = 16;
		break;
	}

	return count;
}
static DEVICE_ATTR(word_size, S_IRUSR | S_IWUSR,
		word_size_show, word_size_store);

static ssize_t sample_size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct atlas7_hsi2s *i2s = platform_get_drvdata(pdev);

	return snprintf(buf, PAGE_SIZE, "%d\n", i2s->i2s_ctrl.sample_size);
}

static ssize_t sample_size_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct atlas7_hsi2s *i2s = platform_get_drvdata(pdev);
	int ret;
	unsigned long value;

	ret = kstrtoul(buf, 0, &value);

	if (value < 16)
		value = 16;
	i2s->i2s_ctrl.sample_size = value;
	return count;
}
static DEVICE_ATTR(sample_size, S_IRUSR | S_IWUSR,
		sample_size_show, sample_size_store);

static ssize_t data_align_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct atlas7_hsi2s *i2s = platform_get_drvdata(pdev);
	static const char *mode[DATA_ALIGN_MAX] = {
		[DATA_ALIGN_LEFT] = "Left-Justified",
		[DATA_ALIGN_I2S] = "I2S",
	};
	const char *str;

	if (i2s->i2s_ctrl.data_align > DATA_ALIGN_I2S)
		i2s->i2s_ctrl.data_align = DATA_ALIGN_LEFT;
	str = mode[i2s->i2s_ctrl.data_align];

	return snprintf(buf, PAGE_SIZE, "%d: %s\n",
			i2s->i2s_ctrl.data_align, str);
}

static ssize_t data_align_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct atlas7_hsi2s *i2s = platform_get_drvdata(pdev);
	int ret;
	unsigned long value;

	ret = kstrtoul(buf, 0, &value);

	if (value > DATA_ALIGN_LAST)
		value = DATA_ALIGN_LEFT;
	i2s->i2s_ctrl.data_align = value;

	return count;
}
static DEVICE_ATTR(data_align, S_IRUSR | S_IWUSR,
		data_align_show, data_align_store);

static ssize_t word_align_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct atlas7_hsi2s *i2s = platform_get_drvdata(pdev);
	static const char *mode[WORD_ALIGN_MAX] = {
		[WORD_ALIGN_LEFT] = "Left-Justified",
		[WORD_ALIGN_I2S0] = "I2S0",
		[WORD_ALIGN_I2S1] = "I2S1",
	};
	const char *str;

	if (i2s->i2s_ctrl.word_align > WORD_ALIGN_LAST)
		i2s->i2s_ctrl.word_align = WORD_ALIGN_I2S0;
	str = mode[i2s->i2s_ctrl.word_align];

	return snprintf(buf, PAGE_SIZE, "%d: %s\n",
			i2s->i2s_ctrl.word_align, str);
}

static ssize_t word_align_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct atlas7_hsi2s *i2s = platform_get_drvdata(pdev);
	int ret;
	unsigned long value;

	ret = kstrtoul(buf, 0, &value);
	if (value > WORD_ALIGN_LAST)
		value = WORD_ALIGN_I2S0;
	i2s->i2s_ctrl.word_align = value;

	return count;
}
static DEVICE_ATTR(word_align, S_IRUSR | S_IWUSR,
		word_align_show, word_align_store);

static ssize_t frame_polarity_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct atlas7_hsi2s *i2s = platform_get_drvdata(pdev);
	static const char *mode[FRAME_POLARITY_MAX] = {
		[FRAME_POLARITY_RAISING] = "Raising",
		[FRAME_POLARITY_FAILING] = "Failing",
	};
	const char *str;

	if (i2s->i2s_ctrl.frame_polarity >= FRAME_POLARITY_MAX)
		i2s->i2s_ctrl.frame_polarity = FRAME_POLARITY_RAISING;
	str = mode[i2s->i2s_ctrl.frame_polarity];

	return snprintf(buf, PAGE_SIZE, "%d: %s\n",
			i2s->i2s_ctrl.frame_polarity, str);
}

static ssize_t frame_polarity_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct atlas7_hsi2s *i2s = platform_get_drvdata(pdev);
	int ret;
	unsigned long value;

	ret = kstrtoul(buf, 0, &value);
	if (value > FRAME_POLARITY_LAST)
		i2s->i2s_ctrl.frame_polarity = FRAME_POLARITY_FAILING;
	i2s->i2s_ctrl.frame_polarity = value;

	return count;
}
static DEVICE_ATTR(frame_polarity, S_IRUSR | S_IWUSR,
		frame_polarity_show, frame_polarity_store);

static ssize_t frame_sync_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct atlas7_hsi2s *i2s = platform_get_drvdata(pdev);
	static const char *mode[FRAME_SYNC_MAX] = {
		[FRAME_SYNC_I2S] = "Left-Right",
		[FRAME_SYNC_DSP0] = "DSP0",
		[FRAME_SYNC_DSP1] = "DSP1",
	};
	const char *str;

	if (i2s->i2s_ctrl.frame_sync > FRAME_SYNC_LAST)
		i2s->i2s_ctrl.frame_sync = FRAME_SYNC_I2S;
	str = mode[i2s->i2s_ctrl.frame_sync];

	return snprintf(buf, PAGE_SIZE, "%d: %s\n",
			i2s->i2s_ctrl.frame_sync, str);
}

static ssize_t frame_sync_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct atlas7_hsi2s *i2s = platform_get_drvdata(pdev);
	int ret;
	unsigned long value;

	ret = kstrtoul(buf, 0, &value);
	if (value > FRAME_SYNC_LAST)
		i2s->i2s_ctrl.frame_sync = FRAME_SYNC_I2S;
	i2s->i2s_ctrl.frame_sync = value;

	return count;
}
static DEVICE_ATTR(frame_sync, S_IRUSR | S_IWUSR,
		frame_sync_show, frame_sync_store);

static ssize_t rxmode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct atlas7_hsi2s *i2s = platform_get_drvdata(pdev);
	static const char *mode[RXMODE_MAX] = {
		[RXMODE_MUX] = "MUX",
		[RXMODE_SPLIT] = "SPLIT",
	};
	const char *str;

	if (i2s->i2s_ctrl.rx_mode > RXMODE_LAST)
		i2s->i2s_ctrl.rx_mode = RXMODE_SPLIT;
	str = mode[i2s->i2s_ctrl.rx_mode];

	return snprintf(buf, PAGE_SIZE, "%d: %s\n", i2s->i2s_ctrl.rx_mode, str);
}

static ssize_t rxmode_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct atlas7_hsi2s *i2s = platform_get_drvdata(pdev);
	int ret;
	unsigned long value;

	ret = kstrtoul(buf, 0, &value);
	if (value > RXMODE_LAST)
		value = RXMODE_SPLIT;
	i2s->i2s_ctrl.rx_mode = value;

	return count;
}
static DEVICE_ATTR(rx_mode, S_IRUSR | S_IWUSR, rxmode_show, rxmode_store);

/* these attr for debug, will remove */
static struct attribute *i2s_ctrl_attrs[] = {
	&dev_attr_rx_mode.attr,
	&dev_attr_frame_sync.attr,
	&dev_attr_frame_polarity.attr,
	&dev_attr_word_align.attr,
	&dev_attr_data_align.attr,
	&dev_attr_sample_size.attr,
	&dev_attr_word_size.attr,
	&dev_attr_bclk_mode.attr,
	NULL,
};

static struct attribute_group i2s_ctrl_attr_group = {
	.name = "i2s_ctrl",
	.attrs = i2s_ctrl_attrs,
};

static const struct attribute_group *atlas7_i2s_attr_groups[] = {
	&i2s_ctrl_attr_group,
	NULL,
};

static int atlas7_hsi2s_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int atlas7_hsi2s_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static int atlas7_hsi2s_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct atlas7_hsi2s *i2s =
		container_of(filp->private_data, struct atlas7_hsi2s, misc_i2s);

	vma->vm_pgoff = i2s->rx_dma_addr >> PAGE_SHIFT;
	return remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
				vma->vm_end - vma->vm_start,
				vma->vm_page_prot);
}

static enum hrtimer_restart i2s_hrtimer_callback(struct hrtimer *hrt)
{
	struct atlas7_hsi2s *i2s = container_of(hrt, struct atlas7_hsi2s, hrt);
	struct device *dev = &i2s->pdev->dev;
	struct dma_tx_state tx_state;
	u32 pos;
	int size;

	dmaengine_tx_status(i2s->rx_chan, i2s->dma_cookie, &tx_state);
	pos = I2S_DMA_BUF_SIZE - tx_state.residue;
	if (pos < i2s->last_pos) {
		size = I2S_DMA_BUF_SIZE - i2s->last_pos;
		dma_sync_single_for_cpu(dev, i2s->rx_dma_addr + i2s->last_pos,
				size, DMA_FROM_DEVICE);
		i2s->last_pos = 0;
		i2s->in = 0;
	}

	if (pos > i2s->last_pos) {
		size = pos - i2s->last_pos;
		dma_sync_single_for_cpu(dev, i2s->rx_dma_addr + i2s->last_pos,
				size, DMA_FROM_DEVICE);
		i2s->last_pos = pos;
		i2s->in = pos;
	}

	hrtimer_forward_now(hrt, ns_to_ktime(i2s->timer_interval));

	return HRTIMER_RESTART;
}

static unsigned long atlas7_prep_and_start_dma(struct atlas7_hsi2s *i2s)
{
	struct device *dev = &i2s->pdev->dev;
	unsigned long reg_ctrl;

	reg_ctrl = (i2s->i2s_ctrl.frame_sync << FRAME_SYNC_SFT) |
		(i2s->i2s_ctrl.frame_polarity << FRAME_POLARITY_SFT) |
		(i2s->i2s_ctrl.word_align << WORD_ALIGN_SFT) |
		(i2s->i2s_ctrl.data_align << DATA_ALIGN_SFT) |
		((i2s->i2s_ctrl.sample_size - 1) << SAMPLE_SIZE_SFT) |
		((i2s->i2s_ctrl.word_size - 1) << WORD_SIZE_SFT) |
		(i2s->i2s_ctrl.bclk_mode << BURST_MODE_SFT);

	writel((1 << 31), i2s->regbase + I2S_CTRL);
	writel(reg_ctrl, i2s->regbase + I2S_CTRL);
	writel((0x18 << 20) | (0x10 << 10) | 0x8,
			i2s->regbase + I2S_RXFIFO_LEV_CHK);
	dev_err(dev, "hs-i2s: CTRL %x\n", readl(i2s->regbase + I2S_CTRL));
	i2s->desc = dmaengine_prep_dma_cyclic(i2s->rx_chan, i2s->rx_dma_addr,
			I2S_DMA_BUF_SIZE, I2S_DMA_BUF_SIZE / 2,
			DMA_DEV_TO_MEM, DMA_PREP_CONTINUE);
	if (IS_ERR_OR_NULL(i2s->desc)) {
		dev_err(dev, "hs-i2s: prep rx dma failed\n");
		return -ENODEV;
	}
	dmaengine_submit(i2s->desc);
	dma_async_issue_pending(i2s->rx_chan);
	writel(FIFO_RESET, i2s->regbase + I2S_RXFIFO_OP);
	writel(FIFO_START, i2s->regbase + I2S_RXFIFO_OP);
	switch (i2s->i2s_ctrl.rx_mode) {
	case RXMODE_MUX:
		reg_ctrl |= RX0_EN;
		break;
	case RXMODE_SPLIT:
	default:
		reg_ctrl |= (RX0_EN | RX1_EN);
		break;
	}
	writel(reg_ctrl, i2s->regbase + I2S_CTRL);

	return 0;
}

static long atlas7_start_receive(struct atlas7_hsi2s *i2s)
{
	int ret;

	i2s->in = 0;
	ret = atlas7_prep_and_start_dma(i2s);
	hrtimer_start(&i2s->hrt, ns_to_ktime(i2s->timer_interval),
			HRTIMER_MODE_REL);

	return 0;
}

static long atlas7_stop_receive(struct atlas7_hsi2s *i2s)
{
	hrtimer_cancel(&i2s->hrt);
	writel(0x2 , i2s->regbase + I2S_RXFIFO_OP);
	dmaengine_terminate_all(i2s->rx_chan);

	return 0;
}

static long atlas_set_param(struct atlas7_hsi2s *i2s,
		struct i2s_ctrl_t *i2s_ctrl)
{
	if (!i2s_ctrl)
		return -EINVAL;
	if (i2s_ctrl->rx_mode != RXMODE_MUX &&
			i2s_ctrl->rx_mode != RXMODE_SPLIT)
		return -EINVAL;
	if (i2s_ctrl->frame_polarity != FRAME_POLARITY_FAILING &&
			i2s_ctrl->frame_polarity != FRAME_POLARITY_RAISING)
		return -EINVAL;
	if (i2s_ctrl->bclk_mode != BCLK_MODE_CONTINUOUS &&
			i2s_ctrl->bclk_mode != BCLK_MODE_BURST)
		return -EINVAL;
	if (i2s_ctrl->word_size != 16 && i2s_ctrl->word_size != 24
			&& i2s_ctrl->word_size != 32)
		return -EINVAL;
	if (i2s_ctrl->data_align > DATA_ALIGN_LAST)
		return -EINVAL;
	if (i2s_ctrl->word_align > WORD_ALIGN_LAST)
		return -EINVAL;
	if (i2s_ctrl->frame_sync > FRAME_SYNC_LAST)
		return -EINVAL;

	i2s->i2s_ctrl.val = i2s_ctrl->val;
	return 0;
}

static long
atlas7_hsi2s_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct atlas7_hsi2s *i2s =
		container_of(filp->private_data, struct atlas7_hsi2s, misc_i2s);

	switch (cmd) {
	case IOCTL_START_RX:
		ret = atlas7_start_receive(i2s);
		break;
	case IOCTL_GET_BUF_POINTER:
		put_user(i2s->in, (int __user *)arg);
		break;
	case IOCTL_STOP_RX:
		ret = atlas7_stop_receive(i2s);
		break;
	case IOCTL_SET_PARAM:
		ret = atlas_set_param(i2s, (struct i2s_ctrl_t *)arg);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct file_operations i2s_fops = {
	.unlocked_ioctl = atlas7_hsi2s_ioctl,
	.open = atlas7_hsi2s_open,
	.release = atlas7_hsi2s_release,
	.mmap = atlas7_hsi2s_mmap,
};

#ifdef CONFIG_PM_SLEEP
static int atlas7_hsi2s_pm_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct atlas7_hsi2s *i2s = platform_get_drvdata(pdev);

	clk_disable_unprepare(i2s->clk);

	return 0;
}

static int atlas7_hsi2s_pm_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct atlas7_hsi2s *i2s = platform_get_drvdata(pdev);

	return clk_prepare_enable(i2s->clk);
}
#endif

static SIMPLE_DEV_PM_OPS(atlas7_hsi2s_pm_ops, atlas7_hsi2s_pm_suspend,
		atlas7_hsi2s_pm_resume);


static int atlas7_hsi2s_probe(struct platform_device *pdev)
{
	int ret;
	struct atlas7_hsi2s *i2s;
	struct dma_slave_config rx_slv_cfg = {
		.src_maxburst = 2,
	};
	struct device *dev = &pdev->dev;
	struct resource *res_io;
	char dev_name[32];

	i2s = devm_kzalloc(&pdev->dev, sizeof(*i2s), GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;
	i2s->pdev = pdev;

	i2s->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(i2s->clk)) {
		dev_crit(dev, "failed to get a clock.\n");
		return PTR_ERR(i2s->clk);
	}

	platform_set_drvdata(pdev, i2s);
	atlas7_hsi2s_pm_resume(dev);
	res_io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i2s->regbase = devm_ioremap_resource(&pdev->dev, res_io);
	if (IS_ERR(i2s->regbase))
		return PTR_ERR(i2s->regbase);

	i2s->virt_dma_addr = dma_alloc_coherent(&pdev->dev,
			I2S_DMA_BUF_SIZE,
			&i2s->rx_dma_addr,
			GFP_KERNEL);
	if (!i2s->virt_dma_addr) {
		ret = -ENOMEM;
		goto out;
	}

	i2s->rx_chan = dma_request_slave_channel(dev, "rx");
	if (!i2s->rx_chan) {
		dev_err(dev, "hs-i2s: request rx dma failed\n");
		return -ENODEV;
	}
	dmaengine_slave_config(i2s->rx_chan, &rx_slv_cfg);
	i2s->timer_interval = 20000000;

	/* There are only 2 HS-I2S port */
	switch (dev_num & 0x3) {
	case 0:
	case 2:
		dev_num |= 0x1;
		i2s->dev_num = 1;
		break;
	case 1:
		dev_num |= 0x2;
		i2s->dev_num = 2;
		break;
	default:
		ret = -EBUSY;
		goto out;
	}
	i2s->i2s_ctrl.bclk_mode = BCLK_MODE_CONTINUOUS;
	i2s->i2s_ctrl.word_size = 16;
	i2s->i2s_ctrl.sample_size = 16;
	i2s->i2s_ctrl.data_align = DATA_ALIGN_LEFT;
	i2s->i2s_ctrl.word_align  = WORD_ALIGN_I2S0;
	i2s->i2s_ctrl.frame_polarity = FRAME_POLARITY_FAILING;
	i2s->i2s_ctrl.frame_sync = FRAME_SYNC_I2S;
	i2s->i2s_ctrl.rx_mode = RXMODE_SPLIT;

	hrtimer_init(&i2s->hrt, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	i2s->hrt.function = i2s_hrtimer_callback;

	sprintf(dev_name, "%s%d", DRV_NAME, i2s->dev_num);
	i2s->misc_i2s.name = dev_name;
	i2s->misc_i2s.fops = &i2s_fops;
	i2s->misc_i2s.minor = MISC_DYNAMIC_MINOR;
	ret = misc_register(&i2s->misc_i2s);
	if (unlikely(ret)) {
		dev_err(&pdev->dev, "misc register fail\n");
		goto out;
	}

	ret = sysfs_create_groups(&dev->kobj, atlas7_i2s_attr_groups);
	if (unlikely(ret)) {
		dev_err(&pdev->dev, "create sysfs fail\n");
		goto out;
	}

	return 0;
out:
	dma_free_coherent(&pdev->dev, I2S_DMA_BUF_SIZE, i2s->virt_dma_addr,
			i2s->rx_dma_addr);
	atlas7_hsi2s_pm_suspend(dev);

	return ret;
}

static int atlas7_hsi2s_remove(struct platform_device *pdev)
{
	struct atlas7_hsi2s *i2s = platform_get_drvdata(pdev);

	dev_num &= ~i2s->dev_num;
	sysfs_remove_groups(&pdev->dev.kobj, atlas7_i2s_attr_groups);
	misc_deregister(&i2s->misc_i2s);
	dma_free_coherent(&pdev->dev, I2S_DMA_BUF_SIZE, i2s->virt_dma_addr,
			i2s->rx_dma_addr);
	return 0;
}

static const struct of_device_id atlas7_hsi2s_of_match[] = {
	{ .compatible = "sirf,atlas7-hsi2s",},
	{}
};
MODULE_DEVICE_TABLE(of, atlas7_hsi2s_of_match);

static struct platform_driver atlas7_hsi2s_driver = {
	.driver		= {
		.name	= "hsi2s",
		.owner	= THIS_MODULE,
		.of_match_table = atlas7_hsi2s_of_match,
		.pm = &atlas7_hsi2s_pm_ops,
	},
	.probe		= atlas7_hsi2s_probe,
	.remove		= atlas7_hsi2s_remove,
};

module_platform_driver(atlas7_hsi2s_driver);

MODULE_DESCRIPTION("HS-I2S driver for SiRFAtlas7DA");
MODULE_LICENSE("GPL v2");
