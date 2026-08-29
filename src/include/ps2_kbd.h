/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_PS2_KBD_H
#define GEMIOS_PS2_KBD_H

#include "types.h"

/* Keyboard Device Commands */
#define PS2_KBD_CMD_SET_LEDS        0xED
#define PS2_KBD_CMD_ECHO            0xEE
#define PS2_KBD_CMD_SCAN_CODE_SET   0xF0
#define PS2_KBD_CMD_GET_DEVICE_ID   0xF2
#define PS2_KBD_CMD_SET_TYPEMATIC   0xF3
#define PS2_KBD_CMD_ENABLE_SCAN     0xF4
#define PS2_KBD_CMD_DISABLE_SCAN    0xF5
#define PS2_KBD_CMD_SET_DEFAULTS    0xF6
#define PS2_KBD_CMD_RESEND          0xFE
#define PS2_KBD_CMD_RESET           0xFF

/* Keyboard Responses */
#define PS2_KBD_RESP_ACK            0xFA
#define PS2_KBD_RESP_BAT_PASSED     0xAA

void ps2_kbd_init(void);
void ps2_keyboard_handle_byte(uint8_t scancode);
void kbd_push_char(uint16_t key);
bool ps2_kbd_is_present(void);
void ps2_kbd_set_leds(bool scroll, bool num, bool caps);

#endif /* GEMIOS_PS2_KBD_H */
