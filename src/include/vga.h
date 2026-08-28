/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_VGA_H
#define GEMIOS_VGA_H

#include "types.h"
#include "multiboot.h"

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
    VGA_COLOR_WHITE = 15
};

#define vga_entry_color(fg, bg) ((uint8_t)((fg) | ((bg) << 4)))
#define vga_entry(uc, color)    ((uint16_t)((uint8_t)(uc) | ((uint16_t)(uint8_t)(color) << 8)))

void vga_init(const struct multiboot_info *mbi);
void vga_clear(void);
void vga_set_color(uint8_t color);
uint8_t vga_get_color(void);
void vga_putc(char c);
void vga_puts(const char *str);
void vga_putc_at(char c, uint8_t color, size_t x, size_t y);
void vga_puts_at(const char *str, uint8_t color, size_t x, size_t y);
void vga_set_cursor(size_t x, size_t y);
void vga_get_cursor(size_t *x, size_t *y);
size_t vga_get_cols(void);
size_t vga_get_rows(void);
void vga_draw_status_bar(void);
void vga_update_mouse_status(int32_t x, int32_t y, uint8_t buttons);

/* Framebuffer Functions */
bool vga_is_framebuffer(void);
void vga_get_resolution(uint32_t *width, uint32_t *height, uint32_t *bpp);
uintptr_t vga_get_framebuffer_addr(void);
void fb_draw_pixel(uint32_t x, uint32_t y, uint32_t color);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);

/* Serial Port (COM1) */
void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *str);
bool serial_has_char(void);
char serial_getchar(void);

#endif /* GEMIOS_VGA_H */
