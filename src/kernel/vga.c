/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "vga.h"
#include "multiboot.h"
#include "io.h"
#include "pci.h"
#include "string.h"
#include "font.h"

#define VGA_BUFFER_ADDR 0xB8000
#define COM1_PORT       0x3F8

#define FONT_WIDTH      8
#define FONT_HEIGHT     16
#define FONT_FIRST_CHAR 32
#define FONT_LAST_CHAR  126

#define SCROLL_TOP 1
#define SCROLL_BOTTOM 23

/* Bochs / QEMU VBE Dispi Interface */
#define VBE_DISPI_IOPORT_INDEX      0x01CE
#define VBE_DISPI_IOPORT_DATA       0x01CF

#define VBE_DISPI_INDEX_ID          0x0
#define VBE_DISPI_INDEX_XRES        0x1
#define VBE_DISPI_INDEX_YRES        0x2
#define VBE_DISPI_INDEX_BPP         0x3
#define VBE_DISPI_INDEX_ENABLE      0x4
#define VBE_DISPI_INDEX_BANK        0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH  0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 0x7
#define VBE_DISPI_INDEX_X_OFFSET    0x8
#define VBE_DISPI_INDEX_Y_OFFSET    0x9

#define VBE_DISPI_DISABLED          0x00
#define VBE_DISPI_ENABLED           0x01
#define VBE_DISPI_LFB_ENABLED       0x40
#define VBE_DISPI_NOCLEARMEM        0x80

struct framebuffer_driver_state {
    bool enabled;
    uintptr_t paddr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t  bpp;
    uint8_t  type;
    uint8_t  red_pos;
    uint8_t  red_mask_size;
    uint8_t  green_pos;
    uint8_t  green_mask_size;
    uint8_t  blue_pos;
    uint8_t  blue_mask_size;
    size_t   cols;
    size_t   rows;
};

static struct framebuffer_driver_state fb;

#define MAX_TERM_COLS 256
#define MAX_TERM_ROWS 128

static volatile uint16_t *vga_buffer = (volatile uint16_t*)VGA_BUFFER_ADDR;
static uint16_t shadow_buffer[MAX_TERM_ROWS * MAX_TERM_COLS];
static size_t term_cols = VGA_WIDTH;
static size_t term_rows = VGA_HEIGHT;
static size_t scroll_top = 1;
static size_t scroll_bottom = VGA_HEIGHT - 2;
static size_t cursor_x = 0;
static size_t cursor_y = 1;
static uint8_t current_color = 0x07;

/* Standard VGA 16-color RGB table */
static const uint32_t vga_rgb_palette[16] = {
    0x000000, /* 0: Black */
    0x0000AA, /* 1: Blue */
    0x00AA00, /* 2: Green */
    0x00AAAA, /* 3: Cyan */
    0xAA0000, /* 4: Red */
    0xAA00AA, /* 5: Magenta */
    0xAA5500, /* 6: Brown */
    0xAAAAAA, /* 7: Light Grey */
    0x555555, /* 8: Dark Grey */
    0x5555FF, /* 9: Light Blue */
    0x55FF55, /* 10: Light Green */
    0x55FFFF, /* 11: Light Cyan */
    0xFF5555, /* 12: Light Red */
    0xFF55FF, /* 13: Light Magenta */
    0xFFFF55, /* 14: Light Brown / Yellow */
    0xFFFFFF  /* 15: White */
};

/* Convert standard VGA color index (0-15) to native framebuffer pixel value */
static uint32_t vga_color_to_pixel(uint8_t color_idx) {
    uint32_t rgb;
    uint8_t r, g, b;

    if (fb.type == MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED || fb.bpp == 8) {
        return (uint32_t)(color_idx & 0x0F);
    }

    rgb = vga_rgb_palette[color_idx & 0x0F];
    r = (uint8_t)((rgb >> 16) & 0xFF);
    g = (uint8_t)((rgb >> 8) & 0xFF);
    b = (uint8_t)(rgb & 0xFF);

    if (fb.type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB) {
        if (fb.bpp == 16 && fb.red_mask_size == 0) {
            uint16_t r5 = (uint16_t)((r >> 3) & 0x1F);
            uint16_t g6 = (uint16_t)((g >> 2) & 0x3F);
            uint16_t b5 = (uint16_t)((b >> 3) & 0x1F);
            return (uint32_t)((r5 << 11) | (g6 << 5) | b5);
        }
        uint32_t r_val = (uint32_t)(fb.red_mask_size ? (r >> (8 - fb.red_mask_size)) : r);
        uint32_t g_val = (uint32_t)(fb.green_mask_size ? (g >> (8 - fb.green_mask_size)) : g);
        uint32_t b_val = (uint32_t)(fb.blue_mask_size ? (b >> (8 - fb.blue_mask_size)) : b);
        return (r_val << fb.red_pos) | (g_val << fb.green_pos) | (b_val << fb.blue_pos);
    }

    return rgb;
}

/* Bochs VBE Helper routines */
static uint16_t bochs_vbe_read(uint16_t index) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

static void bochs_vbe_write(uint16_t index, uint16_t value) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA, value);
}

static bool bochs_vbe_init(uint32_t width, uint32_t height, uint32_t bpp, uintptr_t *fb_addr) {
    uint16_t id;
    uint32_t bar0;
    uint8_t bus, slot;

    id = bochs_vbe_read(VBE_DISPI_INDEX_ID);
    if (id < 0xB0C0 || id > 0xB0C6) {
        return false;
    }

    bar0 = 0;
    for (bus = 0; bus < 4 && bar0 == 0; bus++) {
        for (slot = 0; slot < 32; slot++) {
            uint32_t id_reg = pci_read_config32(bus, slot, 0, 0);
            if (id_reg != 0xFFFFFFFF && id_reg != 0) {
                uint8_t class_code = pci_read_config8(bus, slot, 0, 11);
                if (class_code == PCI_CLASS_DISPLAY || (id_reg == 0x11111234)) {
                    uint16_t cmd;
                    bar0 = pci_read_config32(bus, slot, 0, 0x10) & 0xFFFFFFF0;
                    cmd = pci_read_config16(bus, slot, 0, 0x04);
                    pci_write_config16(bus, slot, 0, 0x04, (uint16_t)(cmd | 0x07));
                    break;
                }
            }
        }
    }

    if (bar0 == 0) {
        bar0 = 0xFD000000; /* Standard QEMU PCI VGA LFB */
    }

    bochs_vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    bochs_vbe_write(VBE_DISPI_INDEX_XRES, (uint16_t)width);
    bochs_vbe_write(VBE_DISPI_INDEX_YRES, (uint16_t)height);
    bochs_vbe_write(VBE_DISPI_INDEX_BPP, (uint16_t)bpp);
    bochs_vbe_write(VBE_DISPI_INDEX_ENABLE, (uint16_t)(VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED));

    *fb_addr = bar0;
    return true;
}

/* Render character glyph using font.h */
static void fb_draw_char_at(char c, uint8_t color, size_t col, size_t row) {
    size_t x, y;
    uint32_t fg_pix, bg_pix;
    uint32_t px_start, py_start;
    const uint8_t *glyph_data;
    uint8_t fallback_glyph[16];

    if (!fb.enabled || col >= fb.cols || row >= fb.rows) return;

    fg_pix = vga_color_to_pixel((uint8_t)(color & 0x0F));
    bg_pix = vga_color_to_pixel((uint8_t)((color >> 4) & 0x0F));

    px_start = (uint32_t)(col * FONT_WIDTH);
    py_start = (uint32_t)(row * FONT_HEIGHT);

    if ((uint8_t)c >= FONT_FIRST_CHAR && (uint8_t)c <= FONT_LAST_CHAR) {
        glyph_data = (const uint8_t*)&font[((uint8_t)c - FONT_FIRST_CHAR) * FONT_HEIGHT];
    } else if ((uint8_t)c == 0xB3) { /* Box vertical line │ */
        for (y = 0; y < FONT_HEIGHT; y++) fallback_glyph[y] = 0x18;
        glyph_data = fallback_glyph;
    } else if ((uint8_t)c == 0xC4) { /* Box horizontal line ─ */
        for (y = 0; y < FONT_HEIGHT; y++) fallback_glyph[y] = (y == 7 || y == 8) ? 0xFF : 0x00;
        glyph_data = fallback_glyph;
    } else {
        for (y = 0; y < FONT_HEIGHT; y++) fallback_glyph[y] = 0x00;
        glyph_data = fallback_glyph;
    }

    if (fb.bpp == 32) {
        for (y = 0; y < FONT_HEIGHT; y++) {
            uint8_t row_bits = glyph_data[y];
            uint32_t *line_ptr = (uint32_t*)(fb.paddr + (py_start + y) * fb.pitch + px_start * 4);
            for (x = 0; x < FONT_WIDTH; x++) {
                line_ptr[x] = (row_bits & (0x80 >> x)) ? fg_pix : bg_pix;
            }
        }
    } else if (fb.bpp == 24) {
        for (y = 0; y < FONT_HEIGHT; y++) {
            uint8_t row_bits = glyph_data[y];
            uint8_t *line_ptr = (uint8_t*)(fb.paddr + (py_start + y) * fb.pitch + px_start * 3);
            for (x = 0; x < FONT_WIDTH; x++) {
                uint32_t pix = (row_bits & (0x80 >> x)) ? fg_pix : bg_pix;
                line_ptr[x * 3 + 0] = (uint8_t)(pix & 0xFF);
                line_ptr[x * 3 + 1] = (uint8_t)((pix >> 8) & 0xFF);
                line_ptr[x * 3 + 2] = (uint8_t)((pix >> 16) & 0xFF);
            }
        }
    } else if (fb.bpp == 16) {
        for (y = 0; y < FONT_HEIGHT; y++) {
            uint8_t row_bits = glyph_data[y];
            uint16_t *line_ptr = (uint16_t*)(fb.paddr + (py_start + y) * fb.pitch + px_start * 2);
            for (x = 0; x < FONT_WIDTH; x++) {
                line_ptr[x] = (uint16_t)((row_bits & (0x80 >> x)) ? fg_pix : bg_pix);
            }
        }
    } else if (fb.bpp == 8) {
        for (y = 0; y < FONT_HEIGHT; y++) {
            uint8_t row_bits = glyph_data[y];
            uint8_t *line_ptr = (uint8_t*)(fb.paddr + (py_start + y) * fb.pitch + px_start);
            for (x = 0; x < FONT_WIDTH; x++) {
                line_ptr[x] = (uint8_t)((row_bits & (0x80 >> x)) ? fg_pix : bg_pix);
            }
        }
    }
}

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

/* Public Framebuffer inspection helpers */
bool vga_is_framebuffer(void) {
    return fb.enabled;
}

void vga_get_resolution(uint32_t *width, uint32_t *height, uint32_t *bpp) {
    if (width) *width = fb.enabled ? fb.width : VGA_WIDTH;
    if (height) *height = fb.enabled ? fb.height : VGA_HEIGHT;
    if (bpp) *bpp = fb.enabled ? fb.bpp : 4;
}

uintptr_t vga_get_framebuffer_addr(void) {
    return fb.enabled ? fb.paddr : VGA_BUFFER_ADDR;
}

void fb_draw_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!fb.enabled || x >= fb.width || y >= fb.height) return;

    if (fb.bpp == 32) {
        uint32_t *p = (uint32_t*)(fb.paddr + y * fb.pitch + x * 4);
        *p = color;
    } else if (fb.bpp == 24) {
        uint8_t *p = (uint8_t*)(fb.paddr + y * fb.pitch + x * 3);
        p[0] = (uint8_t)(color & 0xFF);
        p[1] = (uint8_t)((color >> 8) & 0xFF);
        p[2] = (uint8_t)((color >> 16) & 0xFF);
    } else if (fb.bpp == 16) {
        uint16_t *p = (uint16_t*)(fb.paddr + y * fb.pitch + x * 2);
        *p = (uint16_t)color;
    } else if (fb.bpp == 8) {
        uint8_t *p = (uint8_t*)(fb.paddr + y * fb.pitch + x);
        *p = (uint8_t)(color & 0xFF);
    }
}

void fb_fill_rect(uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh, uint32_t color) {
    uint32_t x, y;
    if (!fb.enabled) return;
    if (rx >= fb.width || ry >= fb.height) return;
    if (rx + rw > fb.width) rw = fb.width - rx;
    if (ry + rh > fb.height) rh = fb.height - ry;

    for (y = 0; y < rh; y++) {
        uint32_t py = ry + y;
        if (fb.bpp == 32) {
            uint32_t *line_ptr = (uint32_t*)(fb.paddr + py * fb.pitch + rx * 4);
            for (x = 0; x < rw; x++) {
                line_ptr[x] = color;
            }
        } else if (fb.bpp == 24) {
            uint8_t *line_ptr = (uint8_t*)(fb.paddr + py * fb.pitch + rx * 3);
            for (x = 0; x < rw; x++) {
                line_ptr[x * 3 + 0] = (uint8_t)(color & 0xFF);
                line_ptr[x * 3 + 1] = (uint8_t)((color >> 8) & 0xFF);
                line_ptr[x * 3 + 2] = (uint8_t)((color >> 16) & 0xFF);
            }
        } else if (fb.bpp == 16) {
            uint16_t *line_ptr = (uint16_t*)(fb.paddr + py * fb.pitch + rx * 2);
            for (x = 0; x < rw; x++) {
                line_ptr[x] = (uint16_t)color;
            }
        } else if (fb.bpp == 8) {
            uint8_t *line_ptr = (uint8_t*)(fb.paddr + py * fb.pitch + rx);
            for (x = 0; x < rw; x++) {
                line_ptr[x] = (uint8_t)(color & 0xFF);
            }
        }
    }
}

/* VGA and Framebuffer Text Console driver */
size_t vga_get_cols(void) {
    return term_cols;
}

size_t vga_get_rows(void) {
    return term_rows;
}

void vga_set_cursor(size_t x, size_t y) {
    uint16_t pos;

    if (x >= term_cols) x = term_cols - 1;
    if (y >= term_rows) y = term_rows - 1;
    cursor_x = x;
    cursor_y = y;

    if (!fb.enabled && y < 25 && x < 80) {
        pos = (uint16_t)(y * 80 + x);
        outb(0x3D4, 0x0F);
        outb(0x3D5, (uint8_t)(pos & 0xFF));
        outb(0x3D4, 0x0E);
        outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
    }
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
    if (x < term_cols && y < term_rows) {
        if (x < MAX_TERM_COLS && y < MAX_TERM_ROWS) {
            shadow_buffer[y * MAX_TERM_COLS + x] = vga_entry(c, color);
        }
        if (!fb.enabled && y < 25 && x < 80) {
            vga_buffer[y * 80 + x] = vga_entry(c, color);
        }
    }

    if (fb.enabled) {
        fb_draw_char_at(c, color, x, y);
    }
}

void vga_puts_at(const char *str, uint8_t color, size_t x, size_t y) {
    while (*str && x < term_cols) {
        vga_putc_at(*str++, color, x++, y);
    }
}

void vga_draw_status_bar(void) {
    uint8_t header_color;
    uint8_t footer_color;
    size_t x;
    size_t footer_row;

    header_color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    for (x = 0; x < term_cols; x++) {
        vga_putc_at(' ', header_color, x, 0);
    }
    vga_puts_at(" GEMIOS x86-32 Preemptive RTOS | USB xHCI / HID / MSC / Hub ", header_color, 2, 0);

    footer_row = (term_rows > 0) ? (term_rows - 1) : 0;
    footer_color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_DARK_GREY);
    for (x = 0; x < term_cols; x++) {
        vga_putc_at(' ', footer_color, x, footer_row);
    }
    vga_puts_at(" Shell Ready | Type 'help' for commands", footer_color, 2, footer_row);
}

void vga_update_mouse_status(int32_t x, int32_t y, uint8_t buttons) {
    char buf[40];
    uint8_t footer_color;
    size_t footer_row;
    size_t start_col;

    footer_row = (term_rows > 0) ? (term_rows - 1) : 0;
    snprintf(buf, sizeof(buf), "Mouse: (%3d, %3d) [B:%c%c%c] ",
             x, y,
             (buttons & 1) ? 'L' : '-',
             (buttons & 4) ? 'M' : '-',
             (buttons & 2) ? 'R' : '-');
    footer_color = vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_DARK_GREY);
    start_col = (term_cols > 35) ? (term_cols - 35) : 0;
    vga_puts_at(buf, footer_color, start_col, footer_row);
}

static void vga_scroll(void) {
    size_t y;
    size_t x;
    uint16_t blank;

    for (y = scroll_top; y < scroll_bottom; y++) {
        for (x = 0; x < term_cols; x++) {
            if (x < MAX_TERM_COLS && (y + 1) < MAX_TERM_ROWS) {
                shadow_buffer[y * MAX_TERM_COLS + x] = shadow_buffer[(y + 1) * MAX_TERM_COLS + x];
            }
            if (!fb.enabled && y < 25 && x < 80) {
                vga_buffer[y * 80 + x] = shadow_buffer[y * MAX_TERM_COLS + x];
            }
        }
    }
    blank = vga_entry(' ', current_color);
    for (x = 0; x < term_cols; x++) {
        if (x < MAX_TERM_COLS && scroll_bottom < MAX_TERM_ROWS) {
            shadow_buffer[scroll_bottom * MAX_TERM_COLS + x] = blank;
        }
        if (!fb.enabled && scroll_bottom < 25 && x < 80) {
            vga_buffer[scroll_bottom * 80 + x] = blank;
        }
    }

    if (fb.enabled) {
        size_t top_py = scroll_top * FONT_HEIGHT;
        size_t scroll_h = (scroll_bottom - scroll_top) * FONT_HEIGHT;
        uint32_t bg_pix = vga_color_to_pixel((uint8_t)((current_color >> 4) & 0x0F));

        memmove((void*)(fb.paddr + top_py * fb.pitch),
                (const void*)(fb.paddr + (top_py + FONT_HEIGHT) * fb.pitch),
                scroll_h * fb.pitch);

        fb_fill_rect(0, (uint32_t)(scroll_bottom * FONT_HEIGHT), fb.width, FONT_HEIGHT, bg_pix);
    }

    cursor_y = scroll_bottom;
}

void vga_clear(void) {
    uint16_t blank;
    size_t y;
    size_t x;

    blank = vga_entry(' ', current_color);
    for (y = scroll_top; y <= scroll_bottom; y++) {
        for (x = 0; x < term_cols; x++) {
            if (x < MAX_TERM_COLS && y < MAX_TERM_ROWS) {
                shadow_buffer[y * MAX_TERM_COLS + x] = blank;
            }
            if (!fb.enabled && y < 25 && x < 80) {
                vga_buffer[y * 80 + x] = blank;
            }
        }
    }

    if (fb.enabled) {
        uint32_t bg_pix = vga_color_to_pixel((uint8_t)((current_color >> 4) & 0x0F));
        fb_fill_rect(0, (uint32_t)(scroll_top * FONT_HEIGHT),
                     fb.width, (uint32_t)((scroll_bottom - scroll_top + 1) * FONT_HEIGHT),
                     bg_pix);
    }

    cursor_x = 0;
    cursor_y = scroll_top;
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

    if (cursor_x >= term_cols) {
        cursor_x = 0;
        cursor_y++;
    }

    if (cursor_y > scroll_bottom) {
        vga_scroll();
    }

    vga_set_cursor(cursor_x, cursor_y);
}

void vga_puts(const char *str) {
    while (*str) {
        vga_putc(*str++);
    }
}

void vga_init(const struct multiboot_info *mbi) {
    serial_init();

    fb.enabled = false;
    fb.paddr = 0;
    fb.width = 0;
    fb.height = 0;
    fb.pitch = 0;
    fb.bpp = 0;
    fb.type = 0;
    fb.cols = VGA_WIDTH;
    fb.rows = VGA_HEIGHT;

    if (mbi && (mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) &&
        mbi->framebuffer_addr_low != 0 &&
        mbi->framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT) {
        fb.enabled = true;
        fb.paddr = (uintptr_t)mbi->framebuffer_addr_low;
        fb.width = mbi->framebuffer_width;
        fb.height = mbi->framebuffer_height;
        fb.pitch = mbi->framebuffer_pitch ? mbi->framebuffer_pitch : (mbi->framebuffer_width * ((mbi->framebuffer_bpp + 7) / 8));
        fb.bpp = mbi->framebuffer_bpp;
        fb.type = mbi->framebuffer_type;

        if (fb.type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB) {
            fb.red_pos = mbi->color_info.rgb.framebuffer_red_field_position;
            fb.red_mask_size = mbi->color_info.rgb.framebuffer_red_mask_size;
            fb.green_pos = mbi->color_info.rgb.framebuffer_green_field_position;
            fb.green_mask_size = mbi->color_info.rgb.framebuffer_green_mask_size;
            fb.blue_pos = mbi->color_info.rgb.framebuffer_blue_field_position;
            fb.blue_mask_size = mbi->color_info.rgb.framebuffer_blue_mask_size;
        }

        if ((fb.bpp == 32 || fb.bpp == 24) && fb.red_mask_size == 0) {
            fb.red_pos = 16;
            fb.red_mask_size = 8;
            fb.green_pos = 8;
            fb.green_mask_size = 8;
            fb.blue_pos = 0;
            fb.blue_mask_size = 8;
        }

        fb.cols = fb.width / FONT_WIDTH;
        fb.rows = fb.height / FONT_HEIGHT;
        if (fb.cols > MAX_TERM_COLS) fb.cols = MAX_TERM_COLS;
        if (fb.rows > MAX_TERM_ROWS) fb.rows = MAX_TERM_ROWS;

        term_cols = fb.cols;
        term_rows = fb.rows;
        scroll_top = 1;
        scroll_bottom = (term_rows > 2) ? (term_rows - 2) : (term_rows - 1);
    } else {
        /* Fallback: Bochs VBE (QEMU/Bochs/VirtualBox) */
        uintptr_t vbe_fb_addr = 0;
        if (bochs_vbe_init(640, 480, 32, &vbe_fb_addr)) {
            fb.enabled = true;
            fb.paddr = vbe_fb_addr;
            fb.width = 640;
            fb.height = 480;
            fb.pitch = 640 * 4;
            fb.bpp = 32;
            fb.type = MULTIBOOT_FRAMEBUFFER_TYPE_RGB;
            fb.red_pos = 16;
            fb.red_mask_size = 8;
            fb.green_pos = 8;
            fb.green_mask_size = 8;
            fb.blue_pos = 0;
            fb.blue_mask_size = 8;
            fb.cols = 640 / FONT_WIDTH;
            fb.rows = 480 / FONT_HEIGHT;
            if (fb.cols > MAX_TERM_COLS) fb.cols = MAX_TERM_COLS;
            if (fb.rows > MAX_TERM_ROWS) fb.rows = MAX_TERM_ROWS;

            term_cols = fb.cols;
            term_rows = fb.rows;
            scroll_top = 1;
            scroll_bottom = (term_rows > 2) ? (term_rows - 2) : (term_rows - 1);
        } else {
            term_cols = VGA_WIDTH;
            term_rows = VGA_HEIGHT;
            scroll_top = 1;
            scroll_bottom = VGA_HEIGHT - 2;
        }
    }

    if (fb.enabled) {
        fb_fill_rect(0, 0, fb.width, fb.height, 0x000000);
    }

    vga_clear();
}
