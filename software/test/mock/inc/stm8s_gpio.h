/* Host-test mock of software/inc/stm8s_gpio.h (see test/Makefile).
 *
 * Only the pieces the code under test (ui.c) touches are provided: the
 * GPIO register blocks become plain RAM objects the harness can drive,
 * so the button/encoder interrupt routines can be simulated.
 */
#ifndef MOCK_STM8S_GPIO_H
#define MOCK_STM8S_GPIO_H

#include <stdint.h>

typedef struct {
    uint8_t ODR;
    uint8_t IDR;
    uint8_t DDR;
    uint8_t CR1;
    uint8_t CR2;
} GPIO_TypeDef;

/* Defined in the test harness; GPIOB/GPIOC IDR are driven by the harness. */
extern GPIO_TypeDef mock_gpio_b;
extern GPIO_TypeDef mock_gpio_c;

#define GPIOB (&mock_gpio_b)
#define GPIOC (&mock_gpio_c)

#endif
