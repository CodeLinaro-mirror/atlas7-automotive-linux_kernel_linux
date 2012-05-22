/*
 * GPIO controller driver for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/consumer.h>

#define SIRFSOC_GPIO_CTRL(g, i)			((g)*0x100 + (i)*4)
#define SIRFSOC_GPIO_DSP_EN0			(0x80)
#define SIRFSOC_GPIO_PAD_EN(g)			((g)*0x100 + 0x84)
#define SIRFSOC_GPIO_INT_STATUS(g)		((g)*0x100 + 0x8C)

#define SIRFSOC_GPIO_CTL_INTR_LOW_MASK		0x1
#define SIRFSOC_GPIO_CTL_INTR_HIGH_MASK		0x2
#define SIRFSOC_GPIO_CTL_INTR_TYPE_MASK		0x4
#define SIRFSOC_GPIO_CTL_INTR_EN_MASK		0x8
#define SIRFSOC_GPIO_CTL_INTR_STS_MASK		0x10
#define SIRFSOC_GPIO_CTL_OUT_EN_MASK		0x20
#define SIRFSOC_GPIO_CTL_DATAOUT_MASK		0x40
#define SIRFSOC_GPIO_CTL_DATAIN_MASK		0x80
#define SIRFSOC_GPIO_CTL_PULL_MASK		0x100
#define SIRFSOC_GPIO_CTL_PULL_HIGH		0x200
#define SIRFSOC_GPIO_CTL_DSP_INT		0x400

#define SIRFSOC_GPIO_NUM(bank, index)	(((bank)*(32)) + (index))

struct sirfsoc_gpio_bank {
	struct of_mm_gpio_chip chip;
	u8 group;
	u16 irq;
	u8 wake_mask;
	spinlock_t lock;
};

static struct sirfsoc_gpio_bank sgpio_bank[SIRFSOC_GPIO_NO_OF_BANKS];

static DEFINE_SPINLOCK(gpio_lock);

static inline struct sirfsoc_gpio_bank *sirfsoc_irq_to_bank(unsigned int irq)
{
	int i;

	for (i = 0; i < SIRFSOC_GPIO_NO_OF_BANKS; i++) {
		if (sgpio_bank[i].irq == irq)
			return &sgpio_bank[i];
	}

	return NULL;
}

static inline int sirfsoc_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	return SIRFSOC_GPIO_IRQ_START + (chip->base + offset);
}

static inline int sirfsoc_irq_to_indx(unsigned int irq)
{
	return (irq - SIRFSOC_GPIO_IRQ_START) % SIRFSOC_GPIO_BANK_SIZE;
}

static inline struct sirfsoc_gpio_bank *sirfsoc_gpio_to_bank(unsigned int gpio)
{
	return &sgpio_bank[gpio / SIRFSOC_GPIO_BANK_SIZE];
}

static inline int sirfsoc_gpio_to_offset(unsigned int gpio)
{
	return gpio % SIRFSOC_GPIO_BANK_SIZE;
}

static void sirfsoc_gpio_irq_ack(struct irq_data *d)
{
	struct sirfsoc_gpio_bank *bank = sirfsoc_irq_to_bank(d->irq);
	int idx = sirfsoc_irq_to_indx(d->irq);
	u32 status, offset;
	unsigned long flags;

	offset = SIRFSOC_GPIO_CTRL(bank->group, idx);
	spin_lock_irqsave(&gpio_lock, flags);

	status = readl(bank->chip.regs + offset);

	writel(status, bank->chip.regs + offset);
	pr_debug("%s: ack gpio group %d idx %d, status %#x\n",
		__func__, bank->group, idx,
		readl(bank->chip.regs + offset));
	spin_unlock_irqrestore(&gpio_lock, flags);
}

static void __sirfsoc_gpio_irq_mask(unsigned int irq)
{
	struct sirfsoc_gpio_bank *bank = sirfsoc_irq_to_bank(irq);
	int idx = sirfsoc_irq_to_indx(irq);
	u32 status, offset;
	unsigned long flags;

	pr_debug("%s: unmask gpio group %d idx %d\n", __func__,
		bank->group, idx);
	offset = SIRFSOC_GPIO_CTRL(bank->group, idx);
	spin_lock_irqsave(&gpio_lock, flags);
	status = readl(bank->chip.regs + offset);

	status &= ~SIRFSOC_GPIO_CTL_INTR_EN_MASK;
	status &= ~SIRFSOC_GPIO_CTL_INTR_STS_MASK;

	writel(status, bank->chip.regs + offset);

	spin_unlock_irqrestore(&gpio_lock, flags);
}

static void sirfsoc_gpio_irq_mask(struct irq_data *d)
{
	__sirfsoc_gpio_irq_mask(d->irq);
}

static void sirfsoc_gpio_irq_unmask(struct irq_data *d)
{
	struct sirfsoc_gpio_bank *bank = sirfsoc_irq_to_bank(d->irq);
	int idx = sirfsoc_irq_to_indx(d->irq);
	u32 status, offset;
	unsigned long flags;

	pr_debug("%s: unmask gpio group %d idx %d\n", __func__,
		bank->group, idx);
	offset = SIRFSOC_GPIO_CTRL(bank->group, idx);

	spin_lock_irqsave(&gpio_lock, flags);
	status = readl(bank->chip.regs + offset);

	status &= ~SIRFSOC_GPIO_CTL_INTR_STS_MASK;
	status |= SIRFSOC_GPIO_CTL_INTR_EN_MASK;

	writel(status, bank->chip.regs + offset);
	spin_unlock_irqrestore(&gpio_lock, flags);
}

static int sirfsoc_gpio_irq_type(struct irq_data *d, unsigned type)
{
	struct sirfsoc_gpio_bank *bank = sirfsoc_irq_to_bank(d->irq);
	int idx = sirfsoc_irq_to_indx(d->irq);
	u32 status, offset;
	unsigned long flags;

	offset = SIRFSOC_GPIO_CTRL(bank->group, idx);
	spin_lock_irqsave(&gpio_lock, flags);
	status = readl(bank->chip.regs + offset);
	status &= ~SIRFSOC_GPIO_CTL_INTR_STS_MASK;

	switch (type) {
	case IRQ_TYPE_NONE:
		break;
	case IRQ_TYPE_EDGE_RISING:
		status |= (SIRFSOC_GPIO_CTL_INTR_HIGH_MASK | SIRFSOC_GPIO_CTL_INTR_TYPE_MASK);
		status &= ~SIRFSOC_GPIO_CTL_INTR_LOW_MASK;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		status &= ~SIRFSOC_GPIO_CTL_INTR_HIGH_MASK;
		status |= (SIRFSOC_GPIO_CTL_INTR_LOW_MASK | SIRFSOC_GPIO_CTL_INTR_TYPE_MASK);
		break;
	case IRQ_TYPE_EDGE_BOTH:
		status |=
			(SIRFSOC_GPIO_CTL_INTR_HIGH_MASK | SIRFSOC_GPIO_CTL_INTR_LOW_MASK |
			 SIRFSOC_GPIO_CTL_INTR_TYPE_MASK);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		status &= ~(SIRFSOC_GPIO_CTL_INTR_HIGH_MASK | SIRFSOC_GPIO_CTL_INTR_TYPE_MASK);
		status |= SIRFSOC_GPIO_CTL_INTR_LOW_MASK;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		status |= SIRFSOC_GPIO_CTL_INTR_HIGH_MASK;
		status &= ~(SIRFSOC_GPIO_CTL_INTR_LOW_MASK | SIRFSOC_GPIO_CTL_INTR_TYPE_MASK);
		break;
	}

	writel(status, bank->chip.regs + offset);

	spin_unlock_irqrestore(&gpio_lock, flags);

	return 0;
}

static struct irq_chip sirfsoc_irq_chip = {
	.name = "SiRF SoC GPIO IRQ",
	.irq_ack = sirfsoc_gpio_irq_ack,
	.irq_mask = sirfsoc_gpio_irq_mask,
	.irq_unmask = sirfsoc_gpio_irq_unmask,
	.irq_set_type = sirfsoc_gpio_irq_type,
};

static void sirfsoc_gpio_handle_irq(unsigned int irq, struct irq_desc *desc)
{
	struct sirfsoc_gpio_bank *bank = NULL;
	u32 status, ctrl;
	int i, idx = 0;

	bank = sirfsoc_irq_to_bank(irq);
	status = readl(bank->chip.regs + SIRFSOC_GPIO_INT_STATUS(bank->group));
	if (!status) {
		printk(KERN_WARNING
			"%s: gpio group %d status %#x no interrupt is flaged\n",
			__func__, bank->group, status);
		handle_bad_irq(irq, desc);
		return;
	}

	while (status) {
		ctrl = readl(bank->chip.regs + SIRFSOC_GPIO_CTRL(bank->group, idx));

		/*
		 * Here we must check whether the corresponding GPIO's interrupt
		 * has been enabled, otherwise just skip it
		 */
		if ((status & 0x1) && (ctrl & SIRFSOC_GPIO_CTL_INTR_EN_MASK)) {
			pr_debug("%s: gpio group %d idx %d happens\n",
				__func__, bank->group, idx);
			irq =
				(SIRFSOC_GPIO_IRQ_START +
				 (bank->group * SIRFSOC_GPIO_BANK_SIZE)) + idx;
			generic_handle_irq(irq);
		}

		idx++;
		status = status >> 1;
	}
}

static inline void sirfsoc_gpio_set_input(struct sirfsoc_gpio_bank *bank, unsigned ctrl_offset)
{
	u32 status;
	status = readl(bank->chip.regs + ctrl_offset);
	status &= ~SIRFSOC_GPIO_CTL_OUT_EN_MASK;
	writel(status, bank->chip.regs + ctrl_offset);
}

static int sirfsoc_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct sirfsoc_gpio_bank *bank = container_of(to_of_mm_gpio_chip(chip), struct sirfsoc_gpio_bank, chip);
	unsigned long flags;

	if (pinctrl_request_gpio(chip->base + offset))
		return -ENODEV;

	spin_lock_irqsave(&bank->lock, flags);

	/*set direction as input and disable/mask irq */
	sirfsoc_gpio_set_input(bank, SIRFSOC_GPIO_CTRL(bank->group, offset));
	__sirfsoc_gpio_irq_mask(sirfsoc_gpio_to_irq(chip, offset));

	spin_unlock_irqrestore(&bank->lock, flags);
	return 0;
}

static void sirfsoc_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	struct sirfsoc_gpio_bank *bank = container_of(to_of_mm_gpio_chip(chip),
		struct sirfsoc_gpio_bank, chip);
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);

	/*disable irq */
	__sirfsoc_gpio_irq_mask(sirfsoc_gpio_to_irq(chip, offset));

	/*set gpio to input */
	sirfsoc_gpio_set_input(bank, SIRFSOC_GPIO_CTRL(bank->group, offset));

	pinctrl_free_gpio(chip->base + offset);

	spin_unlock_irqrestore(&bank->lock, flags);
}

static int sirfsoc_gpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	struct sirfsoc_gpio_bank *bank = container_of(to_of_mm_gpio_chip(chip),
		struct sirfsoc_gpio_bank, chip);
	int idx = sirfsoc_gpio_to_offset(gpio);
	unsigned long flags;
	unsigned offset;

	offset = SIRFSOC_GPIO_CTRL(bank->group, idx);
	spin_lock_irqsave(&bank->lock, flags);
	sirfsoc_gpio_set_input(bank, offset);
	spin_unlock_irqrestore(&bank->lock, flags);
	return 0;
}

static inline void sirfsoc_gpio_set_output(struct sirfsoc_gpio_bank *bank, unsigned offset,
	int value)
{
	u32 status;

	status = readl(bank->chip.regs + offset);
	if (value)
		status |= SIRFSOC_GPIO_CTL_DATAOUT_MASK;
	else
		status &= ~SIRFSOC_GPIO_CTL_DATAOUT_MASK;

	status &= ~SIRFSOC_GPIO_CTL_INTR_EN_MASK;
	status |= SIRFSOC_GPIO_CTL_OUT_EN_MASK;

	writel(status, bank->chip.regs + offset);
}

static int sirfsoc_gpio_direction_output(struct gpio_chip *chip, unsigned gpio, int value)
{
	struct sirfsoc_gpio_bank *bank = container_of(to_of_mm_gpio_chip(chip),
		struct sirfsoc_gpio_bank, chip);
	int idx = sirfsoc_gpio_to_offset(gpio);
	u32 offset;
	unsigned long flags;

	offset = SIRFSOC_GPIO_CTRL(bank->group, idx);
	spin_lock_irqsave(&gpio_lock, flags);
	sirfsoc_gpio_set_output(bank, offset, value);
	spin_unlock_irqrestore(&gpio_lock, flags);

	return 0;
}

void gpio_set_pull(unsigned gpio, unsigned mode)
{
	struct sirfsoc_gpio_bank *bank = sirfsoc_gpio_to_bank(gpio);
	int idx = sirfsoc_gpio_to_offset(gpio);
	u32 status, offset;
	unsigned long flags;

	offset = SIRFSOC_GPIO_CTRL(bank->group, idx);

	spin_lock_irqsave(&gpio_lock, flags);

	status = readl(bank->chip.regs + offset);

	switch (mode) {
	case GPIO_PULL_NONE:
		status &= ~SIRFSOC_GPIO_CTL_PULL_MASK;
		break;
	case GPIO_PULL_UP:
		status |= SIRFSOC_GPIO_CTL_PULL_MASK;
		status |= SIRFSOC_GPIO_CTL_PULL_HIGH;
		break;
	case GPIO_PULL_DOWN:
		status |= SIRFSOC_GPIO_CTL_PULL_MASK;
		status &= ~SIRFSOC_GPIO_CTL_PULL_HIGH;
		break;
	default:
		break;
	}

	writel(status, bank->chip.regs + offset);

	spin_unlock_irqrestore(&gpio_lock, flags);
}
EXPORT_SYMBOL(gpio_set_pull);

static int sirfsoc_gpio_get_value(struct gpio_chip *chip, unsigned offset)
{
	struct sirfsoc_gpio_bank *bank = container_of(to_of_mm_gpio_chip(chip),
		struct sirfsoc_gpio_bank, chip);
	u32 status;

	status = readl(bank->chip.regs + SIRFSOC_GPIO_CTRL(bank->group, offset));

	return !!(status & SIRFSOC_GPIO_CTL_DATAIN_MASK);
}

static void sirfsoc_gpio_set_value(struct gpio_chip *chip, unsigned offset,
	int value)
{
	struct sirfsoc_gpio_bank *bank = container_of(to_of_mm_gpio_chip(chip),
		struct sirfsoc_gpio_bank, chip);
	u32 status;

	status = readl(bank->chip.regs + SIRFSOC_GPIO_CTRL(bank->group, offset));
	if (value)
		status |= SIRFSOC_GPIO_CTL_DATAOUT_MASK;
	else
		status &= ~SIRFSOC_GPIO_CTL_DATAOUT_MASK;
	writel(status, bank->chip.regs + SIRFSOC_GPIO_CTRL(bank->group, offset));
}

static int __devinit sirfsoc_gpio_probe(struct device_node *np)
{
	int i, err = 0;
	struct sirfsoc_gpio_bank *bank;
	struct platform_device *pdev = of_find_device_by_node(np);

	for (i = 0; i < SIRFSOC_GPIO_NO_OF_BANKS; i++) {
		bank = &sgpio_bank[i];
		spin_lock_init(&bank->lock);
		bank->chip.gc.request = sirfsoc_gpio_request;
		bank->chip.gc.free = sirfsoc_gpio_free;
		bank->chip.gc.direction_input = sirfsoc_gpio_direction_input;
		bank->chip.gc.get = sirfsoc_gpio_get_value;
		bank->chip.gc.direction_output = sirfsoc_gpio_direction_output;
		bank->chip.gc.set = sirfsoc_gpio_set_value;
		bank->chip.gc.to_irq = sirfsoc_gpio_to_irq;
		bank->chip.gc.ngpio = SIRFSOC_GPIO_BANK_SIZE;
		bank->group = i;
		bank->irq = platform_get_irq(pdev, i);
		if (bank->irq < 0) {
			err = bank->irq;
			goto out;
		}

		/* Call the OF gpio helper to setup and register the GPIO device */
		err = of_mm_gpiochip_add(np, &bank->chip);
		if (err) {
			pr_err("%s: error in probe function with status %d\n",
				np->full_name, err);
			goto out;
		}

		irq_set_chained_handler(bank->irq, sirfsoc_gpio_handle_irq);
		irq_set_chip(bank->irq, &sirfsoc_irq_chip);
		irq_set_handler(bank->irq, handle_level_irq);
		set_irq_flags(bank->irq, IRQF_VALID | IRQF_PROBE);
	}

out:
	return err;
}

static const struct of_device_id sgpio_of_match[] __devinitdata = {
	{.compatible = "sirf,prima2-gpio-pinmux", },
	{},
};

static int __init sirfsoc_gpio_init(void)
{

	struct device_node *np;

	for_each_matching_node(np, sgpio_of_match)
		sirfsoc_gpio_probe(np);

	return 0;
}
subsys_initcall(sirfsoc_gpio_init);

MODULE_DESCRIPTION("SiRFSoC gpio driver");
MODULE_AUTHOR("Yuping Luo <yuping.luo@csr.com>, Barry Song <baohua.song@csr.com>");
MODULE_LICENSE("GPL");
