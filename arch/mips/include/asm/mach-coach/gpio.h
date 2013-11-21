#ifndef __COACHGPIO_H_
#define __COACHGPIO_H_

#define gpio_get_value	__gpio_get_value
#define gpio_set_value	__gpio_set_value
#define gpio_cansleep	__gpio_cansleep
#define gpio_to_irq	__gpio_to_irq

#include <asm-generic/gpio.h>

#endif /* __COACHGPIO_H_ */
