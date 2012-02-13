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
#include <linux/pinctrl/pinmux.h>

#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/regs-gpio.h>

#define SIRFSOC_IRQ_SIRFSOC_GPIO_GROUP0         43
#define SIRFSOC_IRQ_SIRFSOC_GPIO_GROUP1         44
#define SIRFSOC_IRQ_SIRFSOC_GPIO_GROUP2         45
#define SIRFSOC_IRQ_SIRFSOC_GPIO_GROUP3         46
#define SIRFSOC_IRQ_SIRFSOC_GPIO_GROUP4         47

struct gpio_bank {
	u8 group;
	u16 irq;
	u8 wake_mask;

	spinlock_t lock;
	struct gpio_chip chip;
};

static struct gpio_bank sirfsoc_gpio_bank[SIRFSOC_GPIO_NO_OF_BANKS] = {
	{.group = 0, .irq = SIRFSOC_IRQ_SIRFSOC_GPIO_GROUP0,},
	{.group = 1, .irq = SIRFSOC_IRQ_SIRFSOC_GPIO_GROUP1,},
	{.group = 2, .irq = SIRFSOC_IRQ_SIRFSOC_GPIO_GROUP2,},
	{.group = 3, .irq = SIRFSOC_IRQ_SIRFSOC_GPIO_GROUP3,},
	{.group = 4, .irq = SIRFSOC_IRQ_SIRFSOC_GPIO_GROUP4,}
};

static void __iomem *sirfsoc_gpio_pinmux_base;
static DEFINE_SPINLOCK(gpio_lock);

static inline struct gpio_bank *sirfsoc_irq_to_bank(unsigned int irq)
{
	return &sirfsoc_gpio_bank[(irq - SIRFSOC_GPIO_IRQ_START) / SIRFSOC_GPIO_BANK_SIZE];
}

static inline int sirfsoc_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	return SIRFSOC_GPIO_IRQ_START + (chip->base + offset);
}

static inline int sirfsoc_irq_to_indx(unsigned int irq)
{
	return (irq - SIRFSOC_GPIO_IRQ_START) % SIRFSOC_GPIO_BANK_SIZE;
}

static inline struct gpio_bank *sirfsoc_gpio_to_bank(unsigned int gpio)
{
	return &sirfsoc_gpio_bank[gpio / SIRFSOC_GPIO_BANK_SIZE];
}

static inline int sirfsoc_gpio_to_offset(unsigned int gpio)
{
	return gpio % SIRFSOC_GPIO_BANK_SIZE;
}

static void sirfsoc_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_bank *bank = sirfsoc_irq_to_bank(d->irq);
	int index = sirfsoc_irq_to_indx(d->irq);
	u32 status, offset;
	unsigned long flags;

	offset = SIRFSOC_GPIO_CTRL(bank->group, index);
	spin_lock_irqsave(&gpio_lock, flags);

	status = readl(sirfsoc_gpio_pinmux_base + offset);

	writel(status, sirfsoc_gpio_pinmux_base + offset);
	pr_debug("%s: ack gpio group %d index %d, status %#x\n",
		__func__, bank->group, index,
		readl(sirfsoc_gpio_pinmux_base + offset));
	spin_unlock_irqrestore(&gpio_lock, flags);
}

static void __sirfsoc_gpio_irq_mask(unsigned int irq)
{
	struct gpio_bank *bank = sirfsoc_irq_to_bank(irq);
	int index = sirfsoc_irq_to_indx(irq);
	u32 status, offset;
	unsigned long flags;

	pr_debug("%s: unmask gpio group %d index %d\n", __func__,
		bank->group, index);
	offset = SIRFSOC_GPIO_CTRL(bank->group, index);
	spin_lock_irqsave(&gpio_lock, flags);
	status = readl(sirfsoc_gpio_pinmux_base + offset);

	status &= ~SIRFSOC_GPIO_CTL_INTR_EN_MASK;
	status &= ~SIRFSOC_GPIO_CTL_INTR_STS_MASK;

	writel(status, sirfsoc_gpio_pinmux_base + offset);

	spin_unlock_irqrestore(&gpio_lock, flags);
}

static void sirfsoc_gpio_irq_mask(struct irq_data *d)
{
	__sirfsoc_gpio_irq_mask(d->irq);
}

static void sirfsoc_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_bank *bank = sirfsoc_irq_to_bank(d->irq);
	int index = sirfsoc_irq_to_indx(d->irq);
	u32 status, offset;
	unsigned long flags;

	pr_debug("%s: unmask gpio group %d index %d\n", __func__,
		bank->group, index);
	offset = SIRFSOC_GPIO_CTRL(bank->group, index);

	spin_lock_irqsave(&gpio_lock, flags);
	status = readl(sirfsoc_gpio_pinmux_base + offset);

	status &= ~SIRFSOC_GPIO_CTL_INTR_STS_MASK;
	status |= SIRFSOC_GPIO_CTL_INTR_EN_MASK;

	writel(status, sirfsoc_gpio_pinmux_base + offset);
	spin_unlock_irqrestore(&gpio_lock, flags);
}

static int sirfsoc_gpio_irq_type(struct irq_data *d, unsigned type)
{
	struct gpio_bank *bank = sirfsoc_irq_to_bank(d->irq);
	int index = sirfsoc_irq_to_indx(d->irq);
	u32 status, offset;
	unsigned long flags;

	offset = SIRFSOC_GPIO_CTRL(bank->group, index);
	spin_lock_irqsave(&gpio_lock, flags);
	status = readl(sirfsoc_gpio_pinmux_base + offset);
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

	writel(status, sirfsoc_gpio_pinmux_base + offset);

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
	struct gpio_bank *bank = NULL;
	u32 status, ctrl;
	int i, index = 0;

	for (i = 0; i < SIRFSOC_GPIO_NO_OF_BANKS; i++) {
		if (sirfsoc_gpio_bank[i].irq == irq) {
			bank = &sirfsoc_gpio_bank[i];
			break;
		}
	}

	status = readl(sirfsoc_gpio_pinmux_base + SIRFSOC_GPIO_INT_STATUS(bank->group));
	if (!status) {
		printk(KERN_WARNING
			"%s: gpio group %d status %#x no interrupt is flaged\n",
			__func__, bank->group, status);
		handle_bad_irq(irq, desc);
		return;
	}

	while (status) {
		ctrl = readl(sirfsoc_gpio_pinmux_base + SIRFSOC_GPIO_CTRL(bank->group, index));

		/*
		 * Here we must check whether the corresponding GPIO's interrupt
		 * has been enabled, otherwise just skip it
		 */
		if ((status & 0x1) && (ctrl & SIRFSOC_GPIO_CTL_INTR_EN_MASK)) {
			pr_debug("%s: gpio group %d index %d happens\n",
				__func__, bank->group, index);
			irq =
				(SIRFSOC_GPIO_IRQ_START +
				 (bank->group * SIRFSOC_GPIO_BANK_SIZE)) + index;
			generic_handle_irq(irq);
		}

		index++;
		status = status >> 1;
	}
}

static inline void sirfsoc_gpio_set_input(struct gpio_bank *bank, unsigned ctrl_offset)
{
	u32 status;
	status = readl(sirfsoc_gpio_pinmux_base + ctrl_offset);
	status &= ~SIRFSOC_GPIO_CTL_OUT_EN_MASK;
	writel(status, sirfsoc_gpio_pinmux_base + ctrl_offset);
}

static int sirfsoc_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct gpio_bank *bank = container_of(chip, struct gpio_bank, chip);
	unsigned long flags;

	if (pinmux_request_gpio(chip->base + offset))
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
	struct gpio_bank *bank = container_of(chip, struct gpio_bank, chip);
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);

	/*disable irq */
	__sirfsoc_gpio_irq_mask(sirfsoc_gpio_to_irq(chip, offset));

	/*set gpio to input */
	sirfsoc_gpio_set_input(bank, SIRFSOC_GPIO_CTRL(bank->group, offset));

	pinmux_free_gpio(chip->base + offset);

	spin_unlock_irqrestore(&bank->lock, flags);
}

static int sirfsoc_gpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	struct gpio_bank *bank = container_of(chip, struct gpio_bank, chip);
	int index = sirfsoc_gpio_to_offset(gpio);
	unsigned long flags;
	unsigned offset;

	offset = SIRFSOC_GPIO_CTRL(bank->group, index);
	spin_lock_irqsave(&bank->lock, flags);
	sirfsoc_gpio_set_input(bank, offset);
	spin_unlock_irqrestore(&bank->lock, flags);
	return 0;
}

static inline void sirfsoc_gpio_set_output(struct gpio_bank *bank, unsigned offset,
	int value)
{
	u32 status;

	status = readl(sirfsoc_gpio_pinmux_base + offset);
	if (value)
		status |= SIRFSOC_GPIO_CTL_DATAOUT_MASK;
	else
		status &= ~SIRFSOC_GPIO_CTL_DATAOUT_MASK;

	status &= ~SIRFSOC_GPIO_CTL_INTR_EN_MASK;
	status |= SIRFSOC_GPIO_CTL_OUT_EN_MASK;

	writel(status, sirfsoc_gpio_pinmux_base + offset);
}

static int sirfsoc_gpio_direction_output(struct gpio_chip *chip, unsigned gpio, int value)
{
	struct gpio_bank *bank = container_of(chip, struct gpio_bank, chip);
	int index = sirfsoc_gpio_to_offset(gpio);
	u32 offset;
	unsigned long flags;

	offset = SIRFSOC_GPIO_CTRL(bank->group, index);
	spin_lock_irqsave(&gpio_lock, flags);
	sirfsoc_gpio_set_output(bank, offset, value);
	spin_unlock_irqrestore(&gpio_lock, flags);

	return 0;
}

void gpio_set_pull(unsigned gpio, unsigned mode)
{
	struct gpio_bank *bank = sirfsoc_gpio_to_bank(gpio);
	int index = sirfsoc_gpio_to_offset(gpio);
	u32 status, offset;
	unsigned long flags;

	offset = SIRFSOC_GPIO_CTRL(bank->group, index);

	spin_lock_irqsave(&gpio_lock, flags);

	status = readl(sirfsoc_gpio_pinmux_base + offset);

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

	writel(status, sirfsoc_gpio_pinmux_base + offset);

	spin_unlock_irqrestore(&gpio_lock, flags);
}
EXPORT_SYMBOL(gpio_set_pull);

static int sirfsoc_gpio_get_value(struct gpio_chip *chip, unsigned offset)
{
	struct gpio_bank *bank = container_of(chip, struct gpio_bank, chip);
	u32 status;

	status = readl(sirfsoc_gpio_pinmux_base + SIRFSOC_GPIO_CTRL(bank->group, offset));

	return !!(status & SIRFSOC_GPIO_CTL_DATAIN_MASK);
}

static void sirfsoc_gpio_set_value(struct gpio_chip *chip, unsigned offset,
	int value)
{
	struct gpio_bank *bank = container_of(chip, struct gpio_bank, chip);
	u32 status;

	status = readl(sirfsoc_gpio_pinmux_base + SIRFSOC_GPIO_CTRL(bank->group, offset));
	if (value)
		status |= SIRFSOC_GPIO_CTL_DATAOUT_MASK;
	else
		status &= ~SIRFSOC_GPIO_CTL_DATAOUT_MASK;
	writel(status, sirfsoc_gpio_pinmux_base + SIRFSOC_GPIO_CTRL(bank->group, offset));
}

static int __devinit sirfsoc_gpio_probe(struct device_node *np)
{
	int i;

	struct gpio_bank *bank;
	int gpio = 0;

	for (i = 0; i < SIRFSOC_GPIO_NO_OF_BANKS; i++) {
		bank = &sirfsoc_gpio_bank[i];
		spin_lock_init(&bank->lock);
		irq_set_chained_handler(bank->irq, sirfsoc_gpio_handle_irq);

		bank->chip.request = sirfsoc_gpio_request;
		bank->chip.free = sirfsoc_gpio_free;
		bank->chip.direction_input = sirfsoc_gpio_direction_input;
		bank->chip.get = sirfsoc_gpio_get_value;
		bank->chip.direction_output = sirfsoc_gpio_direction_output;
		bank->chip.set = sirfsoc_gpio_set_value;
		bank->chip.to_irq = sirfsoc_gpio_to_irq;
		bank->chip.of_node = np;
		bank->chip.label = "gpio";
		bank->chip.base = gpio;

		gpio += SIRFSOC_GPIO_BANK_SIZE;
		bank->chip.ngpio = SIRFSOC_GPIO_BANK_SIZE;
		gpiochip_add(&bank->chip);
	}

	for (i = SIRFSOC_GPIO_IRQ_START; i < SIRFSOC_GPIO_IRQ_END; i++) {
		irq_set_chip(i, &sirfsoc_irq_chip);
		irq_set_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
	}

	return 0;
}

static const struct of_device_id gpio_ids[] = {
	{.compatible = "sirf,prima2-gpio-pinmux", },
	{},
};

static int __init sirfsoc_gpio_init(void)
{
	struct device_node *np;
	np = of_find_matching_node(NULL, gpio_ids);
	if (!np)
		panic("unable to find compatible gpio node in dtb\n");

	sirfsoc_gpio_pinmux_base = of_iomap(np, 0);
	if (!sirfsoc_gpio_pinmux_base)
		panic("unable to map gpio registers\n");

	return sirfsoc_gpio_probe(np);
}
subsys_initcall(sirfsoc_gpio_init);

MODULE_DESCRIPTION("SiRFSoC gpio driver");
MODULE_AUTHOR("Yuping Luo <yuping.luo@csr.com>, Barry Song <baohua.song@csr.com>");
MODULE_LICENSE("GPL");
