/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_PS2_H
#define GEMIOS_PS2_H

#include "types.h"
#include "ps2_kbd.h"
#include "ps2_mouse.h"

/* PS/2 Controller I/O Ports */
#define PS2_DATA_PORT       0x60
#define PS2_STATUS_PORT     0x64
#define PS2_COMMAND_PORT    0x64

/* PS/2 Controller Status Register Flags */
#define PS2_STATUS_OUTPUT_FULL      (1 << 0)
#define PS2_STATUS_INPUT_FULL       (1 << 1)
#define PS2_STATUS_SYSTEM_FLAG      (1 << 2)
#define PS2_STATUS_COMMAND_DATA     (1 << 3)
#define PS2_STATUS_KEYBOARD_LOCK    (1 << 4)
#define PS2_STATUS_MOUSE_BUFFER_FULL (1 << 5)
#define PS2_STATUS_TIMEOUT_ERR      (1 << 6)
#define PS2_STATUS_PARITY_ERR       (1 << 7)

/* PS/2 Controller Commands */
#define PS2_CMD_READ_CONFIG         0x20
#define PS2_CMD_WRITE_CONFIG        0x60
#define PS2_CMD_DISABLE_PORT2       0xA7
#define PS2_CMD_ENABLE_PORT2        0xA8
#define PS2_CMD_TEST_PORT2          0xA9
#define PS2_CMD_SELF_TEST           0xAA
#define PS2_CMD_TEST_PORT1          0xAB
#define PS2_CMD_DISABLE_PORT1       0xAD
#define PS2_CMD_ENABLE_PORT1        0xAE
#define PS2_CMD_WRITE_PORT2         0xD4

/* PS/2 Controller Configuration Byte Flags */
#define PS2_CFG_PORT1_INT           (1 << 0)
#define PS2_CFG_PORT2_INT           (1 << 1)
#define PS2_CFG_SYSTEM_FLAG         (1 << 2)
#define PS2_CFG_PORT1_CLOCK_DISABLE (1 << 4)
#define PS2_CFG_PORT2_CLOCK_DISABLE (1 << 5)
#define PS2_CFG_PORT1_TRANSLATE     (1 << 6)

/* Functions */
bool ps2_wait_write(void);
bool ps2_wait_read(void);
void ps2_init(void);
bool ps2_is_dual_channel(void);
void ps2_poll(void);

#endif /* GEMIOS_PS2_H */
