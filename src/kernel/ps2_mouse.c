/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "ps2.h"
#include "ps2_mouse.h"
#include "ps2_kbd.h"
#include "idt.h"
#include "pic.h"
#include "io.h"
#include "vga.h"
#include "string.h"

static bool g_mouse_present = false;
static bool g_has_scroll_wheel = false;
static uint8_t g_mouse_packet_size = 3;
static uint8_t g_mouse_packet[4];
static uint8_t g_mouse_cycle = 0;
static int32_t g_mouse_x = 40;
static int32_t g_mouse_y = 12;
static int32_t g_mouse_z = 0;
static uint8_t g_mouse_buttons = 0;

static bool ps2_mouse_write(uint8_t val) {
    if (!ps2_wait_write()) return false;
    outb(PS2_COMMAND_PORT, PS2_CMD_WRITE_PORT2);
    if (!ps2_wait_write()) return false;
    outb(PS2_DATA_PORT, val);
    return true;
}

static uint8_t ps2_mouse_read(void) {
    if (!ps2_wait_read()) return 0;
    return inb(PS2_DATA_PORT);
}

bool ps2_mouse_is_present(void) {
    return g_mouse_present;
}

bool ps2_mouse_has_scroll_wheel(void) {
    return g_has_scroll_wheel;
}

void ps2_mouse_get_state(int32_t *x, int32_t *y, uint8_t *buttons) {
    if (x) *x = g_mouse_x;
    if (y) *y = g_mouse_y;
    if (buttons) *buttons = g_mouse_buttons;
}

void ps2_mouse_set_resolution(uint8_t resolution) {
    ps2_mouse_write(PS2_MOUSE_CMD_SET_RESOLUTION);
    ps2_mouse_read(); /* ACK */
    ps2_mouse_write(resolution);
    ps2_mouse_read(); /* ACK */
}

void ps2_mouse_set_sample_rate(uint8_t sample_rate) {
    ps2_mouse_write(PS2_MOUSE_CMD_SET_SAMPLE_RATE);
    ps2_mouse_read(); /* ACK */
    ps2_mouse_write(sample_rate);
    ps2_mouse_read(); /* ACK */
}

static void process_mouse_packet(uint8_t *packet) {
    uint8_t flags;
    int32_t dx;
    int32_t dy;
    size_t cols;
    size_t rows;

    flags = packet[0];

    /* Discard packet if overflow flags are set */
    if (flags & 0xC0) {
        return;
    }

    dx = (int32_t)packet[1];
    dy = (int32_t)packet[2];

    /* Sign-extend 9-bit delta X and Y */
    if (flags & 0x10) {
        dx |= (int32_t)0xFFFFFF00;
    }
    if (flags & 0x20) {
        dy |= (int32_t)0xFFFFFF00;
    }

    g_mouse_buttons = flags & 0x07; /* Bit 0: Left, Bit 1: Right, Bit 2: Middle */

    cols = vga_get_cols();
    rows = vga_get_rows();

    g_mouse_x += dx;
    /* PS/2 mouse reports positive dy when moved upwards; invert for screen rows */
    g_mouse_y -= dy;

    if (g_mouse_x < 0) g_mouse_x = 0;
    if (g_mouse_x >= (int32_t)cols) g_mouse_x = (int32_t)cols - 1;
    if (g_mouse_y < 1) g_mouse_y = 1;
    if (g_mouse_y >= (int32_t)rows - 1) g_mouse_y = (int32_t)rows - 2;

    if (g_mouse_packet_size == 4) {
        int8_t dz = (int8_t)packet[3];
        g_mouse_z += dz;
    }

    vga_update_mouse_status(g_mouse_x, g_mouse_y, g_mouse_buttons);
}

void ps2_mouse_handle_byte(uint8_t data) {
    if (g_mouse_cycle == 0) {
        /* In PS/2 mouse packets, bit 3 of the header byte is always 1 */
        if ((data & 0x08) == 0) {
            return; /* Out of synchronization; drop byte */
        }
        g_mouse_packet[0] = data;
        g_mouse_cycle = 1;
        return;
    }

    if (g_mouse_cycle == 1) {
        g_mouse_packet[1] = data;
        g_mouse_cycle = 2;
        return;
    }

    if (g_mouse_cycle == 2) {
        g_mouse_packet[2] = data;
        if (g_mouse_packet_size == 4) {
            g_mouse_cycle = 3;
            return;
        }
        g_mouse_cycle = 0;
        process_mouse_packet(g_mouse_packet);
        return;
    }

    if (g_mouse_cycle == 3) {
        g_mouse_packet[3] = data;
        g_mouse_cycle = 0;
        process_mouse_packet(g_mouse_packet);
        return;
    }
}

static void ps2_mouse_irq_handler(registers_t *regs) {
    uint8_t status;
    uint8_t data;

    UNUSED(regs);

    while ((status = inb(PS2_STATUS_PORT)) & PS2_STATUS_OUTPUT_FULL) {
        data = inb(PS2_DATA_PORT);

        if (status & PS2_STATUS_MOUSE_BUFFER_FULL) {
            /* Byte is from PS/2 mouse */
            ps2_mouse_handle_byte(data);
        } else {
            /* Byte is from PS/2 keyboard */
            ps2_keyboard_handle_byte(data);
        }
    }
}

void ps2_mouse_init(void) {
    uint8_t ack;
    uint8_t bat;
    uint8_t id;
    uint8_t mouse_id;

    g_mouse_present = false;
    g_has_scroll_wheel = false;
    g_mouse_packet_size = 3;
    g_mouse_cycle = 0;

    /* 1. Enable Second PS/2 Port (Auxiliary / Mouse) */
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, PS2_CMD_ENABLE_PORT2);

    /* 2. Reset Mouse (0xFF) */
    if (!ps2_mouse_write(PS2_MOUSE_CMD_RESET)) {
        kprintf("[PS/2] Failed to write reset command to mouse.\n");
        return;
    }

    ack = ps2_mouse_read();
    if (ack != PS2_MOUSE_RESP_ACK) {
        kprintf("[PS/2] No PS/2 mouse response (ack=0x%02x)\n", ack);
        return;
    }

    bat = ps2_mouse_read();
    if (bat != PS2_MOUSE_RESP_BAT_PASSED) {
        kprintf("[PS/2] PS/2 mouse self-test failed (bat=0x%02x)\n", bat);
    }
    id = ps2_mouse_read();
    UNUSED(id);

    /* 3. Set Default Parameters (0xF6) */
    ps2_mouse_write(PS2_MOUSE_CMD_SET_DEFAULTS);
    ps2_mouse_read(); /* ACK */

    /* 4. Attempt IntelliMouse Magic Sequence to detect scroll wheel */
    ps2_mouse_write(PS2_MOUSE_CMD_SET_SAMPLE_RATE);
    ps2_mouse_read();
    ps2_mouse_write(200);
    ps2_mouse_read();

    ps2_mouse_write(PS2_MOUSE_CMD_SET_SAMPLE_RATE);
    ps2_mouse_read();
    ps2_mouse_write(100);
    ps2_mouse_read();

    ps2_mouse_write(PS2_MOUSE_CMD_SET_SAMPLE_RATE);
    ps2_mouse_read();
    ps2_mouse_write(80);
    ps2_mouse_read();

    ps2_mouse_write(PS2_MOUSE_CMD_GET_DEVICE_ID);
    ps2_mouse_read();
    mouse_id = ps2_mouse_read();

    if (mouse_id == 3 || mouse_id == 4) {
        g_has_scroll_wheel = true;
        g_mouse_packet_size = 4;
    } else {
        g_has_scroll_wheel = false;
        g_mouse_packet_size = 3;
    }

    /* 5. Set Sample Rate to 100 packets/sec */
    ps2_mouse_set_sample_rate(100);

    /* 6. Set Resolution to 8 counts/mm (Resolution 3) */
    ps2_mouse_set_resolution(3);

    /* 7. Enable Data Reporting (0xF4) */
    ps2_mouse_write(PS2_MOUSE_CMD_ENABLE_REPORTING);
    ps2_mouse_read(); /* ACK */

    /* 8. Center initial coordinates */
    g_mouse_x = (int32_t)(vga_get_cols() / 2);
    g_mouse_y = (int32_t)(vga_get_rows() / 2);
    g_mouse_buttons = 0;
    vga_update_mouse_status(g_mouse_x, g_mouse_y, 0);

    /* 9. Register IRQ12 handler (Vector 44) and unmask IRQ2 & IRQ12 */
    register_interrupt_handler(44, ps2_mouse_irq_handler);
    pic_unmask_irq(2);  /* Cascade */
    pic_unmask_irq(12); /* Mouse IRQ on Slave PIC */

    g_mouse_present = true;
    kprintf("[PS/2] Initialized PS/2 Mouse Driver (IRQ12 Enabled, %s, ID 0x%02x)\n",
            g_has_scroll_wheel ? "Scroll Wheel Mouse" : "Standard Mouse", mouse_id);
}
