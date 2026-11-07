#include "uart.h"
#include "config.h"
#include "inc/stm8s_uart2.h"
#include "inc/stm8s_itc.h"
#include <stdio.h>
#include "adc.h"
#include "load.h"
#include "ui.h"

void uart_init(void)
{
    uint16_t uart_div =  (F_CPU + BAUDR/2) / BAUDR;
    UART2->BRR2 = (uart_div >> 12) | (uart_div & 0x0f);
    UART2->BRR1 = (uart_div >> 4);
    UART2->CR2 = UART2_CR2_TEN | UART2_CR2_REN | UART2_CR2_RIEN;
}

int putchar(int c)
{
    UART2->DR = (char) c;
    while (!(UART2->SR & (uint8_t)UART2_FLAG_TXE));
    return c;
}

static uint8_t cnt = 0;
/* SET: line state. set_pending requests one, set_cnt is the step of the
   line being printed (0 = none in progress). */
static bool set_pending = 1;
static uint8_t set_cnt = 0;
static uint8_t val_lines = 0;

void uart_request_settings(void)
{
    set_pending = 1;
}

/* Print one piece of the SET: line per call, like the VAL: line. */
static void uart_print_settings(void)
{
    if (set_cnt == 1) {
        printf("SET: M %u ", (unsigned int)settings.mode);
    } else if (set_cnt == 2) {
        printf("c %u w %u ", (unsigned int)settings.setpoints[MODE_CC],
                             (unsigned int)settings.setpoints[MODE_CW]);
    } else if (set_cnt == 3) {
        printf("r %u v %u ", (unsigned int)settings.setpoints[MODE_CR],
                             (unsigned int)settings.setpoints[MODE_CV]);
    } else if (set_cnt == 4) {
        printf("U %u u %u ", (unsigned int)settings.cutoff_enabled,
                             (unsigned int)settings.cutoff_voltage);
    } else {
        printf("X %u\r\n", (unsigned int)load_disable_reason);
        set_cnt = 0;
        return;
    }
    set_cnt++;
}

void uart_timer(void)
{
    static uint16_t timer = 0;
    timer++;
    //Output one item as fast as possible, but only start output new sequences with F_LOG frequency
    if (timer == F_SYSTICK/F_LOG) {
        timer = 0;
        cnt = 1;
    }
}

typedef enum {
    STATE_IDLE,
    STATE_COMMAND,
    STATE_DIGITS,
    STATE_WAITING_FOR_EXECUTION,
    STATE_UNINITIALIZED,
} uart_state_t;

static uart_state_t state = STATE_UNINITIALIZED;
static uint8_t cmd;
static uint16_t param;
static uint8_t error_code = 0;

static inline void set_error(uint8_t code)
{
    error_code = code;
}

void uart_handler(void)
{
    if (set_cnt) {
        // Finish a SET: line before anything else starts printing
        uart_print_settings();
    } else if (cnt) {
        if (cnt == 1) {
            char status = 'D';
            if (load_active) {
                status = load_regulated?'A':'U';
            }
            printf("VAL: %c %d ", status, error);
        } else if (cnt == 2) {
            printf("T %3u ", temperature);
        } else if (cnt == 3) {
            printf("Vi %5u ", v_12V);
        } else if (cnt == 4) {
            printf("Vl %5u ", v_load);
        } else if (cnt == 5) {
            printf("Vs %5u ", v_sense);
        } else if (cnt == 6) {
            printf("I %5u ", current_setpoint);
        } else if (cnt == 7) {
            printf("mWs %10lu ", mWatt_seconds);
        } else if (cnt == 8) {
            printf("mAs %10lu ", mAmpere_seconds);
        } else {
            printf("\r\n");
            cnt = 0; // Disable output till new trigger by uart_timer()
            // Report the settings about once a second
            if (++val_lines >= F_LOG) {
                val_lines = 0;
                set_pending = 1;
            }
        }
        if (cnt) cnt++;
    } else if (error_code) {
        printf("ERR:%d %d %d\r\n", cmd, param, error_code);
        error_code = 0;
    } else if (set_pending) {
        set_pending = 0;
        set_cnt = 1;
        uart_print_settings();
    } else if (state == STATE_WAITING_FOR_EXECUTION) {
        printf("CMD:%c%d\r\n", cmd, param);

        switch (cmd) {
            case 'R': // Run
                if (!load_active) {
                    ui_activate_load(); //This is handled in the UI code because we want to show the run mode on the display as well.
                }
                break;
            case 'S': // Stop
                if (load_active) {
                    ui_disable_load();
                } else {
                    /* Already stopped: acknowledge an automatic stop, like
                       the encoder button on the run screen. Silences the
                       cutoff beeper and the blinking RUN LED. */
                    load_disable_reason = DISABLE_USER;
                }
                break;
            case 'M': // Mode
                if (param < NUM_MODES) {
                    settings.mode = param;
                } else {
                    set_error(ERR_MODE_INVALID);
                }
                break;
            case 'c': // Setpoint CC
                if (param >= CUR_MIN && param <= CUR_MAX) {
                    settings.setpoints[MODE_CC] = param;
                } else {
                    set_error(ERR_OUT_OF_RANGE);
                }
                break;
            case 'w': // Setpoint CW
                if (param >= POW_MIN && param <= POW_MAX) {
                    settings.setpoints[MODE_CW] = param;
                } else {
                    set_error(ERR_OUT_OF_RANGE);
                }
                break;
            case 'r': // Setpoint CR
                if (param >= R_MIN && param <= R_MAX) {
                    settings.setpoints[MODE_CR] = param;
                } else {
                    set_error(ERR_OUT_OF_RANGE);
                }
                break;
            case 'v': // Setpoint CV
                if (param >= VOLT_MIN && param <= VOLT_MAX) {
                    settings.setpoints[MODE_CV] = param;
                } else {
                    set_error(ERR_OUT_OF_RANGE);
                }
                break;
            case 'U': // Cutoff enable
                if (param <= 1) {
                    settings.cutoff_enabled = param;
                } else {
                    set_error(ERR_OUT_OF_RANGE);
                }
                break;
            case 'u': // Cutoff voltage
                if (param >= VOLT_MIN && param <= VOLT_MAX) {
                    settings.cutoff_voltage = param;
                } else {
                    set_error(ERR_OUT_OF_RANGE);
                }
                break;
            case 'C': // Clear counters
                mWatt_seconds = 0;
                mAmpere_seconds = 0;
                break;
            case '?': // Request a SET: line (sent below)
                break;
            case 'E': // Store settings
                settings_update();
                break;
            case 'e': // Load settings
                settings_init();
                break;
            default:
                set_error(ERR_INVALID_COMMAND);
        }
        if (!error_code) {
            set_pending = 1; // Report the result of the command
        }
        cmd = 0;
        param = 0;
        state = STATE_IDLE;
    }
}

void uart_rx_irq(void) __interrupt(ITC_IRQ_UART2_RX)
{
    char c = UART2->DR;
    if (c == CMD_RESET) {
        state = STATE_IDLE;
        cmd = 0;
        param = 0;
        if (error == ERROR_COMMAND) {
            error_code = ERR_NONE;
        }
    } else if (state == STATE_UNINITIALIZED) {
        // Ignore everything till the interface is initialized
    } else if (c == '\n' || c == '\r') {
        if (state != STATE_IDLE) {
            state = STATE_WAITING_FOR_EXECUTION;
        }
    } else if (state == STATE_IDLE) {
        cmd = c;
        param = 0;
        state = STATE_DIGITS;
    } else if (state == STATE_DIGITS) {
        if (c >= '0' && c <= '9') {
            param *= 10;
            param += c - '0';
        } else {
            set_error(ERR_NOT_A_DIGIT);
        }
    } else {
        set_error(ERR_SHOULD_NOT_HAPPEN);
    }
    //TODO: Calibration mode
}
