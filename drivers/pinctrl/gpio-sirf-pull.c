/*
 * gpio_set_pull is not upstreamed yet, seperate it for the moment
 */

static inline struct sirfsoc_gpio_bank *sirfsoc_gpio_to_bank(unsigned int gpio)
{
	int i;

	for (i = 0; i < SIRFSOC_GPIO_NO_OF_BANKS; i++)
		if (sgpio_bank[i].chip.gc.base == (gpio &
				(SIRFSOC_GPIO_BANK_SIZE - 1)))
			return &sgpio_bank[i];

	return NULL;
}

void gpio_set_pull(unsigned gpio, unsigned mode)
{
	struct sirfsoc_gpio_bank *bank = sirfsoc_gpio_to_bank(gpio);
	int idx = sirfsoc_gpio_to_offset(gpio);
	u32 status, offset;
	unsigned long flags;

	offset = SIRFSOC_GPIO_CTRL(bank->id, idx);

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
