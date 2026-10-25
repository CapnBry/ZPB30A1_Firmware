/* Host-test mock of software/adc.h (see test/Makefile).
 *
 * The real header pulls in the STM8 ADC register definitions, which have
 * no meaning on the host. ui.c only calls adc_get_voltage().
 */
#ifndef MOCK_ADC_H
#define MOCK_ADC_H
#include <stdint.h>

uint16_t adc_get_voltage(void);

#endif
