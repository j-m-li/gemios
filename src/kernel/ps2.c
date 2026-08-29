/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "ps2.h"
#include "io.h"
#include "vga.h"
#include "idt.h"
#include "pic.h"

static bool g_ps2_present = false;
static bool g_dual_channel = false;

bool ps2_wait_write(void) {
    int timeout;
    timeout = 100000;
    while ((inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) && --timeout) {
        io_wait();
    }
    return timeout > 0;
}

bool ps2_wait_read(void) {
    int timeout;
    timeout = 100000;
    while (!(inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) && --timeout) {
        io_wait();
    }
    return timeout > 0;
}

bool ps2_is_dual_channel(void) {
    return g_dual_channel;
}

void ps2_init(void) {
    uint8_t config;
    uint8_t test_res;

    g_ps2_present = false;
    g_dual_channel = false;

    /* 1. Check if PS/2 controller exists (0xFF indicates floating bus) */
    if (inb(PS2_STATUS_PORT) == 0xFF) {
        kprintf("[PS/2] No PS/2 controller detected (floating bus).\n");
        return;
    }

    /* 2. Disable both PS/2 ports during initialization */
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, PS2_CMD_DISABLE_PORT1);
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, PS2_CMD_DISABLE_PORT2);

    /* 3. Flush any residual data in the controller output buffer */
    while (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
        inb(PS2_DATA_PORT);
        io_wait();
    }

    /* 4. Read Controller Configuration Byte */
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, PS2_CMD_READ_CONFIG);
    if (!ps2_wait_read()) {
        kprintf("[PS/2] Timed out reading controller configuration.\n");
        return;
    }
    config = inb(PS2_DATA_PORT);

    /* 5. Check if controller has a second channel (dual port) */
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, PS2_CMD_ENABLE_PORT2);
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, PS2_CMD_READ_CONFIG);
    if (ps2_wait_read()) {
        uint8_t cfg2;
        cfg2 = inb(PS2_DATA_PORT);
        if ((cfg2 & PS2_CFG_PORT2_CLOCK_DISABLE) == 0) {
            g_dual_channel = true;
        }
    }
    /* Disable port 2 until ready */
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, PS2_CMD_DISABLE_PORT2);

    /* 6. Perform Controller Self-Test (0xAA -> 0x55) */
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, PS2_CMD_SELF_TEST);
    if (ps2_wait_read()) {
        test_res = inb(PS2_DATA_PORT);
        if (test_res != 0x55) {
            kprintf("[PS/2] Controller self-test returned 0x%02x (expected 0x55)\n", test_res);
        }
    }

    /* 7. Test Ports */
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, PS2_CMD_TEST_PORT1);
    if (ps2_wait_read()) {
        test_res = inb(PS2_DATA_PORT);
        if (test_res != 0x00) {
            kprintf("[PS/2] Port 1 test failed (code 0x%02x)\n", test_res);
        }
    }

    if (g_dual_channel) {
        ps2_wait_write();
        outb(PS2_COMMAND_PORT, PS2_CMD_TEST_PORT2);
        if (ps2_wait_read()) {
            test_res = inb(PS2_DATA_PORT);
            if (test_res != 0x00) {
                kprintf("[PS/2] Port 2 test failed (code 0x%02x)\n", test_res);
            }
        }
    }

    /* 8. Configure Controller Configuration Byte:
          - Enable IRQ1 (bit 0)
          - Enable Translation (bit 6)
          - Clear Port 1 clock disable (bit 4)
          - If dual channel: Enable IRQ12 (bit 1) and clear Port 2 clock disable (bit 5)
    */
    config |= PS2_CFG_PORT1_INT;
    config &= ~PS2_CFG_PORT1_CLOCK_DISABLE;
    config |= PS2_CFG_PORT1_TRANSLATE;

    if (g_dual_channel) {
        config |= PS2_CFG_PORT2_INT;
        config &= ~PS2_CFG_PORT2_CLOCK_DISABLE;
    }

    ps2_wait_write();
    outb(PS2_COMMAND_PORT, PS2_CMD_WRITE_CONFIG);
    ps2_wait_write();
    outb(PS2_DATA_PORT, config);

    g_ps2_present = true;
    kprintf("[PS/2] i8042 Controller Initialized (%s channel)\n",
            g_dual_channel ? "Dual" : "Single");

    /* 9. Initialize Keyboard on Port 1 */
    ps2_kbd_init();

    /* 10. Initialize Mouse on Port 2 (if dual channel) */
    if (g_dual_channel) {
        ps2_mouse_init();
    }
}

void ps2_poll(void) {
    uint8_t status;
    uint8_t data;

    if (!g_ps2_present) return;

    while ((status = inb(PS2_STATUS_PORT)) & PS2_STATUS_OUTPUT_FULL) {
        data = inb(PS2_DATA_PORT);
        if (status & PS2_STATUS_MOUSE_BUFFER_FULL) {
            ps2_mouse_handle_byte(data);
        } else {
            ps2_keyboard_handle_byte(data);
        }
    }
}
