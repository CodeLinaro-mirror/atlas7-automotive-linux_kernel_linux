#ifndef __MACH_GPIO_H
#define __MACH_GPIO_H

#define GPIO_PULL_NONE 0
#define GPIO_PULL_UP   1
#define GPIO_PULL_DOWN 2
void sirfsoc_gpio_set_pull(unsigned gpio, unsigned mode);

#endif
