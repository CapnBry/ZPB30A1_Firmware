#ifndef _UI_H_
#define _UI_H_
#include <stdbool.h>
#include <stdint.h>
#include "menu_items.h"

void ui_init(void);
void ui_timer(void);

/* Result of a numeric edit (ui_edit_value_internal).
 * COMMIT: value confirmed and written to *edit->var.
 * CANCEL: editing aborted, *edit->var unchanged. */
typedef enum {
    UI_EDIT_ACTIVE,
    UI_EDIT_COMMIT,
    UI_EDIT_CANCEL,
} ui_edit_result_t;

ui_edit_result_t ui_edit_value_internal(uint8_t event, const NumericEdit *edit, uint8_t leds);

void ui_mainmenu(uint8_t event, const MenuItem *item);
void ui_submenu(uint8_t event, const MenuItem *item);
void ui_select_item(uint8_t event, const MenuItem *item);
void ui_edit_value(uint8_t event, const MenuItem *item);
void ui_edit_setpoint(uint8_t event, const MenuItem *item);
void ui_run_mode(uint8_t event, const MenuItem *item);
void ui_info_mode(uint8_t event, const MenuItem *item);
void ui_error_handler(uint8_t event, const MenuItem *item);
void ui_clear_counters(uint8_t event, const MenuItem *item);
void ui_cal_wizard(uint8_t event, const MenuItem *item);
void ui_cal_reset(uint8_t event, const MenuItem *item);

void ui_activate_load(void);
void ui_disable_load(void);
#endif
