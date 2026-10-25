/* Host-test mock of software/inc/stm8s_itc.h (see test/Makefile).
 *
 * The interrupt line numbers only appear as __interrupt() arguments in
 * ui.c, and the harness compiles with -D'__interrupt(x)=' so these are
 * never evaluated; they are kept for completeness.
 */
#ifndef MOCK_STM8S_ITC_H
#define MOCK_STM8S_ITC_H

#define ITC_IRQ_PORTB 4
#define ITC_IRQ_PORTC 5

#endif
