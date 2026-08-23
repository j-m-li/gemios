/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_VGA_H
#define GEMIOS_VGA_H

#include "types.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

enum vga_color {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN = 14,
    VGA_COLOR_WHITE = 15,
};

static inline uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) {
    return fg | (bg << 4);
}

static inline uint16_t vga_entry(unsigned char uc, uint8_t color) {
    return (uint16_t)uc | ((uint16_t)color << 8);
}

void vga_init(void);
void vga_clear(void);
void vga_set_color(uint8_t color);
uint8_t vga_get_color(void);
void vga_putc(char c);
void vga_puts(const char *str);
void vga_putc_at(char c, uint8_t color, size_t x, size_t y);
void vga_puts_at(const char *str, uint8_t color, size_t x, size_t y);
void vga_set_cursor(size_t x, size_t y);
void vga_get_cursor(size_t *x, size_t *y);
void vga_draw_status_bar(void);
void vga_update_mouse_status(int32_t x, int32_t y, uint8_t buttons);

/* Serial Port (COM1) */
void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *str);
bool serial_has_char(void);
char serial_getchar(void);

#endif /* GEMIOS_VGA_H */
