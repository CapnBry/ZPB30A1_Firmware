#ifndef _UART_H_
#define _UART_H_

void uart_init(void);
void uart_timer(void);
void uart_handler(void);
/* Send a SET: line with the current settings as soon as possible. */
void uart_request_settings(void);

#define CMD_RESET '!'

typedef enum {
    ERR_NONE,
    ERR_MODE_INVALID,
    ERR_OUT_OF_RANGE,
    ERR_NOT_A_DIGIT,
    ERR_SHOULD_NOT_HAPPEN, // = Internal logic error
    ERR_INVALID_COMMAND,
    ERR_RX_ERROR, // a byte was lost or garbled (UART overrun, noise, framing)
} error_codes_t;

#endif
