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
    STATE_EXECUTING,  // uart_handler() has taken the command and is running it
    STATE_DISCARD,    // a bad byte cancelled this line; skip to its end
} uart_state_t;

/* cmd, param, error_code and state are shared with uart_rx_irq(). The main
   loop takes a consistent copy with interrupts off, never piecemeal. */
static volatile uart_state_t state = STATE_UNINITIALIZED;
static volatile uint8_t cmd;
static volatile uint16_t param;
static volatile uint8_t error_code = 0;

/* Every failure goes through here: the line is reported and abandoned, never
   executed. Before, an error only queued an ERR: report while the command
   itself still ran once the line ended -- so a burst of junk (a host's boot
   log, line noise) could run whatever its first byte happened to name. */
static void reject(uint8_t code)
{
    error_code = code;
    state = STATE_DISCARD;
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
        // A line rejected while it was being received
        uint8_t c, code;
        uint16_t p;
        disableInterrupts();
        c = cmd;
        p = param;
        code = error_code;
        error_code = 0;
        enableInterrupts();
        printf("ERR:%d %u %d\r\n", c, p, code);
    } else if (set_pending) {
        set_pending = 0;
        set_cnt = 1;
        uart_print_settings();
    } else if (state == STATE_WAITING_FOR_EXECUTION) {
        uint8_t c, code = ERR_NONE;
        uint16_t p;
        disableInterrupts();
        c = cmd;
        p = param;
        state = STATE_EXECUTING;
        enableInterrupts();

        switch (c) {
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
                if (p < NUM_MODES) {
                    settings.mode = p;
                } else {
                    code = ERR_MODE_INVALID;
                }
                break;
            case 'c': // Setpoint CC
                if (p >= CUR_MIN && p <= CUR_MAX) {
                    settings.setpoints[MODE_CC] = p;
                } else {
                    code = ERR_OUT_OF_RANGE;
                }
                break;
            case 'w': // Setpoint CW
                if (p >= POW_MIN && p <= POW_MAX) {
                    settings.setpoints[MODE_CW] = p;
                } else {
                    code = ERR_OUT_OF_RANGE;
                }
                break;
            case 'r': // Setpoint CR
                if (p >= R_MIN && p <= R_MAX) {
                    settings.setpoints[MODE_CR] = p;
                } else {
                    code = ERR_OUT_OF_RANGE;
                }
                break;
            case 'v': // Setpoint CV
                if (p >= VOLT_MIN && p <= VOLT_MAX) {
                    settings.setpoints[MODE_CV] = p;
                } else {
                    code = ERR_OUT_OF_RANGE;
                }
                break;
            case 'U': // Cutoff enable
                if (p <= 1) {
                    settings.cutoff_enabled = p;
                } else {
                    code = ERR_OUT_OF_RANGE;
                }
                break;
            case 'u': // Cutoff voltage
                if (p >= VOLT_MIN && p <= VOLT_MAX) {
                    settings.cutoff_voltage = p;
                } else {
                    code = ERR_OUT_OF_RANGE;
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
                code = ERR_INVALID_COMMAND;
        }

        /* Exactly one reply per command, sent once it has run: CMD: if it
           took effect, ERR: if not. (CMD: used to go out first, followed by a
           second ERR:0 0 <code> line if the command then failed.) Replying
           after the fact also means the host sends nothing more while an E
           is still writing the EEPROM, when this CPU can't take a byte. */
        if (code) {
            printf("ERR:%d %u %d\r\n", c, p, code);
        } else {
            printf("CMD:%c%u\r\n", c, p);
            set_pending = 1; // Report the result of the command
        }

        disableInterrupts();
        // A byte that arrived meanwhile has already moved state on
        if (state == STATE_EXECUTING) {
            state = STATE_IDLE;
        }
        enableInterrupts();
    }
}

void uart_rx_irq(void) __interrupt(ITC_IRQ_UART2_RX)
{
    /* SR first, then DR: that sequence is what clears the error flags. Reading
       DR alone leaves an overrun flagged, and with RIEN set an overrun raises
       this interrupt again the moment it returns -- forever, starving the main
       loop (display, encoder, buttons, UART output) until a power cycle. An
       overrun happens whenever a byte arrives while interrupts are held off,
       e.g. through the tens of ms of an EEPROM write. */
    uint8_t sr = UART2->SR;
    char c = UART2->DR;

    if (sr & (UART2_SR_OR | UART2_SR_NF | UART2_SR_FE | UART2_SR_PE)) {
        // A byte of this line was lost or garbled: none of it can be trusted
        if (state != STATE_UNINITIALIZED) {
            reject(ERR_RX_ERROR);
        }
        return;
    }
    if (state == STATE_UNINITIALIZED && c != CMD_RESET) {
        // Ignore everything till the interface is initialized
        return;
    }

    if (c == CMD_RESET) {
        state = STATE_IDLE;
        cmd = 0;
        param = 0;
        if (error == ERROR_COMMAND) {
            error_code = ERR_NONE;
        }
        return;
    }

    bool eol = (c == '\n' || c == '\r');
    switch (state) {
        case STATE_IDLE:
            if (!eol) {
                cmd = c;
                param = 0;
                state = STATE_DIGITS;
            }
            break;
        case STATE_DIGITS:
            if (eol) {
                state = STATE_WAITING_FOR_EXECUTION;
            } else if (c < '0' || c > '9') {
                reject(ERR_NOT_A_DIGIT);
            } else if (param > 6553 || (param == 6553 && c > '5')) {
                // Would wrap: 70000 must not arrive as 4464
                reject(ERR_OUT_OF_RANGE);
            } else {
                param = param * 10 + (c - '0');
            }
            break;
        case STATE_WAITING_FOR_EXECUTION:
        case STATE_EXECUTING:
            // The second half of a \r\n is fine; anything else means the
            // sender didn't wait for the reply. A command still waiting is
            // cancelled rather than run on a line that may be junk.
            if (!eol) {
                reject(ERR_SHOULD_NOT_HAPPEN);
            }
            break;
        case STATE_DISCARD:
            if (eol) {
                state = STATE_IDLE;
            }
            break;
        default:
            reject(ERR_SHOULD_NOT_HAPPEN);
    }
    //TODO: Calibration mode
}
