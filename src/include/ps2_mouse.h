/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_PS2_MOUSE_H
#define GEMIOS_PS2_MOUSE_H

#include "types.h"

/* Mouse Device Commands */
#define PS2_MOUSE_CMD_SET_SCALING_1_1   0xE6
#define PS2_MOUSE_CMD_SET_SCALING_2_1   0xE7
#define PS2_MOUSE_CMD_SET_RESOLUTION    0xE8
#define PS2_MOUSE_CMD_STATUS_REQUEST    0xE9
#define PS2_MOUSE_CMD_SET_STREAM_MODE   0xEA
#define PS2_MOUSE_CMD_READ_DATA         0xEB
#define PS2_MOUSE_CMD_RESET_WRAP_MODE   0xEC
#define PS2_MOUSE_CMD_SET_REMOTE_MODE   0xF0
#define PS2_MOUSE_CMD_GET_DEVICE_ID     0xF2
#define PS2_MOUSE_CMD_SET_SAMPLE_RATE   0xF3
#define PS2_MOUSE_CMD_ENABLE_REPORTING  0xF4
#define PS2_MOUSE_CMD_DISABLE_REPORTING 0xF5
#define PS2_MOUSE_CMD_SET_DEFAULTS      0xF6
#define PS2_MOUSE_CMD_RESEND            0xFE
#define PS2_MOUSE_CMD_RESET             0xFF

/* Mouse Device Responses */
#define PS2_MOUSE_RESP_ACK              0xFA
#define PS2_MOUSE_RESP_BAT_PASSED       0xAA

void ps2_mouse_init(void);
void ps2_mouse_handle_byte(uint8_t data);
bool ps2_mouse_is_present(void);
void ps2_mouse_get_state(int32_t *x, int32_t *y, uint8_t *buttons);
bool ps2_mouse_has_scroll_wheel(void);
void ps2_mouse_set_resolution(uint8_t resolution);
void ps2_mouse_set_sample_rate(uint8_t sample_rate);

#endif /* GEMIOS_PS2_MOUSE_H */
