/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "vga.h"
#include "io.h"
#include "string.h"

#define VGA_BUFFER_ADDR 0xB8000
#define COM1_PORT       0x3F8

static volatile uint16_t *vga_buffer = (volatile uint16_t*)VGA_BUFFER_ADDR;
static size_t cursor_x = 0;
static size_t cursor_y = 1;
static uint8_t current_color = 0x07;

#define SCROLL_TOP 1
#define SCROLL_BOTTOM 23

/* Serial port driver */
void serial_init(void) {
    outb(COM1_PORT + 1, 0x00); /* Disable all interrupts */
    outb(COM1_PORT + 3, 0x80); /* Enable DLAB (set baud rate divisor) */
    outb(COM1_PORT + 0, 0x03); /* Set divisor to 3 (lo byte) 38400 baud */
    outb(COM1_PORT + 1, 0x00); /* (hi byte) */
    outb(COM1_PORT + 3, 0x03); /* 8 bits, no parity, one stop bit */
    outb(COM1_PORT + 2, 0xC7); /* Enable FIFO, clear them, with 14-byte threshold */
    outb(COM1_PORT + 4, 0x0B); /* IRQs enabled, RTS/DSR set */
}

static int serial_is_transmit_empty(void) {
    return inb(COM1_PORT + 5) & 0x20;
}

bool serial_has_char(void) {
    return (inb(COM1_PORT + 5) & 0x01) != 0;
}

char serial_getchar(void) {
    if (!serial_has_char()) return 0;
    return (char)inb(COM1_PORT);
}

void serial_putc(char c) {
    while (serial_is_transmit_empty() == 0);
    outb(COM1_PORT, c);
}

void serial_puts(const char *str) {
    while (*str) {
        if (*str == '\n') {
            serial_putc('\r');
        }
        serial_putc(*str++);
    }
}

/* VGA driver */
void vga_set_cursor(size_t x, size_t y) {
    uint16_t pos;

    if (x >= VGA_WIDTH) x = VGA_WIDTH - 1;
    if (y >= VGA_HEIGHT) y = VGA_HEIGHT - 1;
    cursor_x = x;
    cursor_y = y;

    pos = (uint16_t)(y * VGA_WIDTH + x);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_get_cursor(size_t *x, size_t *y) {
    if (x) *x = cursor_x;
    if (y) *y = cursor_y;
}

void vga_set_color(uint8_t color) {
    current_color = color;
}

uint8_t vga_get_color(void) {
    return current_color;
}

void vga_putc_at(char c, uint8_t color, size_t x, size_t y) {
    if (x < VGA_WIDTH && y < VGA_HEIGHT) {
        vga_buffer[y * VGA_WIDTH + x] = vga_entry(c, color);
    }
}

void vga_puts_at(const char *str, uint8_t color, size_t x, size_t y) {
    while (*str && x < VGA_WIDTH) {
        vga_putc_at(*str++, color, x++, y);
    }
}

void vga_draw_status_bar(void) {
    uint8_t header_color;
    uint8_t footer_color;
    size_t x;

    header_color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    for (x = 0; x < VGA_WIDTH; x++) {
        vga_putc_at(' ', header_color, x, 0);
    }
    vga_puts_at(" GEMIOS x86-32 Preemptive RTOS | USB xHCI / HID / MSC / Hub ", header_color, 2, 0);

    footer_color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_DARK_GREY);
    for (x = 0; x < VGA_WIDTH; x++) {
        vga_putc_at(' ', footer_color, x, 24);
    }
    vga_puts_at(" Shell Ready | Type 'help' for commands", footer_color, 2, 24);
}

void vga_update_mouse_status(int32_t x, int32_t y, uint8_t buttons) {
    char buf[40];
    uint8_t footer_color;

    snprintf(buf, sizeof(buf), "Mouse: (%3d, %3d) [B:%c%c%c] ",
             x, y,
             (buttons & 1) ? 'L' : '-',
             (buttons & 4) ? 'M' : '-',
             (buttons & 2) ? 'R' : '-');
    footer_color = vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_DARK_GREY);
    vga_puts_at(buf, footer_color, 48, 24);
}

static void vga_scroll(void) {
    size_t y;
    size_t x;
    uint16_t blank;

    for (y = SCROLL_TOP; y < SCROLL_BOTTOM; y++) {
        for (x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = vga_buffer[(y + 1) * VGA_WIDTH + x];
        }
    }
    blank = vga_entry(' ', current_color);
    for (x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[SCROLL_BOTTOM * VGA_WIDTH + x] = blank;
    }
    cursor_y = SCROLL_BOTTOM;
}

void vga_clear(void) {
    uint16_t blank;
    size_t y;
    size_t x;

    blank = vga_entry(' ', current_color);
    for (y = SCROLL_TOP; y <= SCROLL_BOTTOM; y++) {
        for (x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = blank;
        }
    }
    cursor_x = 0;
    cursor_y = SCROLL_TOP;
    vga_set_cursor(cursor_x, cursor_y);
    vga_draw_status_bar();
}

void vga_putc(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\t') {
        cursor_x = (cursor_x + 8) & ~7;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
        }
    } else {
        vga_putc_at(c, current_color, cursor_x, cursor_y);
        cursor_x++;
    }

    if (cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }

    if (cursor_y > SCROLL_BOTTOM) {
        vga_scroll();
    }

    vga_set_cursor(cursor_x, cursor_y);
}

void vga_puts(const char *str) {
    while (*str) {
        vga_putc(*str++);
    }
}

void vga_init(void) {
    serial_init();
    vga_clear();
}
