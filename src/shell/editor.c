/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "editor.h"
#include "vga.h"
#include "ps2_kbd.h"
#include "usb_hid.h"
#include "xhci.h"
#include "fat.h"
#include "blockdev.h"
#include "heap.h"
#include "pit.h"
#include "time.h"
#include "string.h"
#include "sched.h"
#include "io.h"

#define MAX_EDITOR_LINES 1024
#define MAX_LINE_LEN     512
#define VIEW_ROWS        22
#define GUTTER_WIDTH     6

/* MS-DOS Edit Color Palette */
#define COLOR_HEADER      0x70  /* Black text on Light Grey background */
#define COLOR_TEXT        0x1F  /* Bright White on Blue background */
#define COLOR_GUTTER      0x1B  /* Cyan text on Blue background */
#define COLOR_STATUS      0x70  /* Black text on Light Grey background */
#define COLOR_STATUS_MOD  0x74  /* Red text on Light Grey background */
#define COLOR_MSG_OK      0x2F  /* Bright White on Green background */
#define COLOR_MSG_ERR     0x4F  /* Bright White on Red background */

typedef struct {
    char lines[MAX_EDITOR_LINES][MAX_LINE_LEN];
    size_t num_lines;

    int cur_row;       /* Current line (0-indexed) */
    int cur_col;       /* Current byte offset in current line */

    int top_row;       /* Top visible row index */
    int left_col;      /* Leftmost visible column */

    char dev_name[16];
    char filename[64];

    bool modified;
    bool running;

    char msg[80];
    uint8_t msg_color;
    uint32_t msg_time;
} editor_state_t;

static editor_state_t ed;

/* ========================================================================= */
/* UTF-8 and CP437 Support Routines                                          */
/* ========================================================================= */

static int utf8_char_length(uint8_t first_byte) {
    if ((first_byte & 0x80) == 0x00) return 1;
    if ((first_byte & 0xE0) == 0xC0) return 2;
    if ((first_byte & 0xF0) == 0xE0) return 3;
    if ((first_byte & 0xF8) == 0xF0) return 4;
    return 1; // Fallback
}

int utf8_decode(const char *str, uint32_t *codepoint) {
    uint8_t b0 = (uint8_t)str[0];
    if (b0 == 0) {
        if (codepoint) *codepoint = 0;
        return 0;
    }

    if ((b0 & 0x80) == 0) {
        if (codepoint) *codepoint = b0;
        return 1;
    } else if ((b0 & 0xE0) == 0xC0) {
        uint8_t b1 = (uint8_t)str[1];
        if ((b1 & 0xC0) != 0x80) { if (codepoint) *codepoint = b0; return 1; }
        if (codepoint) *codepoint = ((b0 & 0x1F) << 6) | (b1 & 0x3F);
        return 2;
    } else if ((b0 & 0xF0) == 0xE0) {
        uint8_t b1 = (uint8_t)str[1];
        uint8_t b2 = (uint8_t)str[2];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) { if (codepoint) *codepoint = b0; return 1; }
        if (codepoint) *codepoint = ((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
        return 3;
    } else if ((b0 & 0xF8) == 0xF0) {
        uint8_t b1 = (uint8_t)str[1];
        uint8_t b2 = (uint8_t)str[2];
        uint8_t b3 = (uint8_t)str[3];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) { if (codepoint) *codepoint = b0; return 1; }
        if (codepoint) *codepoint = ((b0 & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
        return 4;
    }

    if (codepoint) *codepoint = b0;
    return 1;
}

/* Map Unicode Codepoint to IBM PC CP437 character */
uint8_t utf8_to_cp437(uint32_t cp) {
    if (cp < 128) return (uint8_t)cp;

    switch (cp) {
        /* Latin characters / accents */
        case 0x00C7: return 0x80; /* Ç */
        case 0x00FC: return 0x81; /* ü */
        case 0x00E9: return 0x82; /* é */
        case 0x00E2: return 0x83; /* â */
        case 0x00E4: return 0x84; /* ä */
        case 0x00E0: return 0x85; /* à */
        case 0x00E5: return 0x86; /* å */
        case 0x00E7: return 0x87; /* ç */
        case 0x00EA: return 0x88; /* ê */
        case 0x00EB: return 0x89; /* ë */
        case 0x00E8: return 0x8A; /* è */
        case 0x00EF: return 0x8B; /* ï */
        case 0x00EE: return 0x8C; /* î */
        case 0x00EC: return 0x8D; /* ì */
        case 0x00C4: return 0x8E; /* Ä */
        case 0x00C5: return 0x8F; /* Å */
        case 0x00C9: return 0x90; /* É */
        case 0x00E6: return 0x91; /* æ */
        case 0x00C6: return 0x92; /* Æ */
        case 0x00F4: return 0x93; /* ô */
        case 0x00F6: return 0x94; /* ö */
        case 0x00F2: return 0x95; /* ò */
        case 0x00FB: return 0x96; /* û */
        case 0x00F9: return 0x97; /* ù */
        case 0x00FF: return 0x98; /* ÿ */
        case 0x00D6: return 0x99; /* Ö */
        case 0x00DC: return 0x9A; /* Ü */
        case 0x00A2: return 0x9B; /* ¢ */
        case 0x00A3: return 0x9C; /* £ */
        case 0x00A5: return 0x9D; /* ¥ */
        case 0x20A7: return 0x9E; /* ₧ */
        case 0x0192: return 0x9F; /* ƒ */
        case 0x00E1: return 0xA0; /* á */
        case 0x00ED: return 0xA1; /* í */
        case 0x00F3: return 0xA2; /* ó */
        case 0x00FA: return 0xA3; /* ú */
        case 0x00F1: return 0xA4; /* ñ */
        case 0x00D1: return 0xA5; /* Ñ */
        case 0x00AA: return 0xA6; /* ª */
        case 0x00BA: return 0xA7; /* º */
        case 0x00BF: return 0xA8; /* ¿ */
        case 0x00AC: return 0xAA; /* ¬ */
        case 0x00BD: return 0xAB; /* ½ */
        case 0x00BC: return 0xAC; /* ¼ */
        case 0x00A1: return 0xAD; /* ¡ */
        case 0x00AB: return 0xAE; /* « */
        case 0x00BB: return 0xAF; /* » */
        case 0x00DF: return 0xE1; /* ß */

        /* Box drawing / Graphics */
        case 0x2591: return 0xB0; /* ░ */
        case 0x2592: return 0xB1; /* ▒ */
        case 0x2593: return 0xB2; /* ▓ */
        case 0x2502: return 0xB3; /* │ */
        case 0x2524: return 0xB4; /* ┤ */
        case 0x2561: return 0xB5; /* ╡ */
        case 0x2562: return 0xB6; /* ╢ */
        case 0x2556: return 0xB7; /* ╖ */
        case 0x2555: return 0xB8; /* ╕ */
        case 0x2563: return 0xB9; /* ╣ */
        case 0x2551: return 0xBA; /* ║ */
        case 0x2557: return 0xBB; /* ╗ */
        case 0x255D: return 0xBC; /* ╝ */
        case 0x255C: return 0xBD; /* ╜ */
        case 0x255B: return 0xBE; /* ╛ */
        case 0x2510: return 0xBF; /* ┐ */
        case 0x2514: return 0xC0; /* └ */
        case 0x2534: return 0xC1; /* ┴ */
        case 0x252C: return 0xC2; /* ┬ */
        case 0x251C: return 0xC3; /* ├ */
        case 0x2500: return 0xC4; /* ─ */
        case 0x253C: return 0xC5; /* ┼ */
        case 0x255E: return 0xC6; /* ╞ */
        case 0x255F: return 0xC7; /* ╟ */
        case 0x255A: return 0xC8; /* ╚ */
        case 0x2554: return 0xC9; /* ╔ */
        case 0x2569: return 0xCA; /* ╩ */
        case 0x2566: return 0xCB; /* ╦ */
        case 0x2560: return 0xCC; /* ╠ */
        case 0x2550: return 0xCD; /* ═ */
        case 0x256C: return 0xCE; /* ╬ */
        case 0x2567: return 0xCF; /* ╧ */
        case 0x2568: return 0xD0; /* ╨ */
        case 0x2564: return 0xD1; /* ╤ */
        case 0x2565: return 0xD2; /* ╥ */
        case 0x2559: return 0xD3; /* ╙ */
        case 0x2558: return 0xD4; /* ╘ */
        case 0x2552: return 0xD5; /* ╒ */
        case 0x2553: return 0xD6; /* ╓ */
        case 0x256B: return 0xD7; /* ╫ */
        case 0x256A: return 0xD8; /* ╪ */
        case 0x2518: return 0xD9; /* ┘ */
        case 0x250C: return 0xDA; /* ┌ */
        case 0x2588: return 0xDB; /* █ */
        case 0x2584: return 0xDC; /* ▄ */
        case 0x258C: return 0xDD; /* ▌ */
        case 0x2590: return 0xDE; /* ▐ */
        case 0x2580: return 0xDF; /* ▀ */
        case 0x25A0: return 0xFE; /* ■ */

        /* Symbols & Math */
        case 0x03B1: return 0xE0; /* α */
        case 0x0393: return 0xE2; /* Γ */
        case 0x03C0: return 0xE3; /* π */
        case 0x03A3: return 0xE4; /* Σ */
        case 0x03C3: return 0xE5; /* σ */
        case 0x00B5: return 0xE6; /* µ */
        case 0x03C4: return 0xE7; /* τ */
        case 0x03A6: return 0xE8; /* Φ */
        case 0x0398: return 0xE9; /* Θ */
        case 0x03A9: return 0xEA; /* Ω */
        case 0x03B4: return 0xEB; /* δ */
        case 0x221E: return 0xEC; /* ∞ */
        case 0x03C6: return 0xED; /* φ */
        case 0x03B5: return 0xEE; /* ε */
        case 0x2229: return 0xEF; /* ∩ */
        case 0x2261: return 0xF0; /* ≡ */
        case 0x00B1: return 0xF1; /* ± */
        case 0x2265: return 0xF2; /* ≥ */
        case 0x2264: return 0xF3; /* ≤ */
        case 0x2320: return 0xF4; /* ⌠ */
        case 0x2321: return 0xF5; /* ⌡ */
        case 0x00F7: return 0xF6; /* ÷ */
        case 0x2248: return 0xF7; /* ≈ */
        case 0x00B0: return 0xF8; /* ° */
        case 0x2219: return 0xF9; /* ∙ */
        case 0x00B7: return 0xFA; /* · */
        case 0x221A: return 0xFB; /* √ */
        case 0x207F: return 0xFC; /* ⁿ */
        case 0x00B2: return 0xFD; /* ² */

        /* Arrows & UI icons */
        case 0x2191: return 0x18; /* ↑ */
        case 0x2193: return 0x19; /* ↓ */
        case 0x2192: return 0x1A; /* → */
        case 0x2190: return 0x1B; /* ← */
        case 0x25B2: return 0x1E; /* ▲ */
        case 0x25BC: return 0x1F; /* ▼ */
        case 0x25BA: return 0x10; /* ► */
        case 0x25C4: return 0x11; /* ◄ */
        case 0x263A: return 0x01; /* ☺ */
        case 0x263B: return 0x02; /* ☻ */
        case 0x2665: return 0x03; /* ♥ */
        case 0x2666: return 0x04; /* ♦ */
        case 0x2663: return 0x05; /* ♣ */
        case 0x2660: return 0x06; /* ♠ */
        case 0x2022: return 0x07; /* • */
        case 0x266A: return 0x0D; /* ♪ */
        case 0x266B: return 0x0E; /* ♫ */
        case 0x263C: return 0x0F; /* ☼ */

        default: return '?';
    }
}

static int utf8_prev_char(const char *line, int idx) {
    if (idx <= 0) return 0;
    idx--;
    while (idx > 0 && ((uint8_t)line[idx] & 0xC0) == 0x80) {
        idx--;
    }
    return idx;
}

static int utf8_next_char(const char *line, int idx) {
    size_t len = strlen(line);
    if ((size_t)idx >= len) return (int)len;
    int c_len = utf8_char_length((uint8_t)line[idx]);
    if (idx + c_len > (int)len) return (int)len;
    return idx + c_len;
}

static int utf8_byte_to_column(const char *line, int byte_idx) {
    int col = 0;
    int idx = 0;
    while (idx < byte_idx && line[idx] != '\0') {
        if (line[idx] == '\t') {
            col += 4 - (col % 4);
            idx++;
        } else {
            int c_len = utf8_char_length((uint8_t)line[idx]);
            col++;
            idx += c_len;
        }
    }
    return col;
}

/* ========================================================================= */
/* File Loading and Saving Routines                                          */
/* ========================================================================= */

static void set_status_msg(const char *msg, uint8_t color) {
    strncpy(ed.msg, msg, sizeof(ed.msg) - 1);
    ed.msg[sizeof(ed.msg) - 1] = '\0';
    ed.msg_color = color;
    ed.msg_time = pit_get_ticks();
}

static void editor_load_file(void) {
    ed.num_lines = 0;
    ed.cur_row = 0;
    ed.cur_col = 0;
    ed.top_row = 0;
    ed.left_col = 0;
    ed.modified = false;

    block_dev_t *bdev = blockdev_get(ed.dev_name);
    if (!bdev) {
        set_status_msg("[ Device not found, starting new file ]", COLOR_MSG_ERR);
        strncpy(ed.lines[0], "", MAX_LINE_LEN);
        ed.num_lines = 1;
        return;
    }

    fat_fs_t fs;
    if (fat_mount(bdev, &fs) != 0) {
        set_status_msg("[ Filesystem error, starting new file ]", COLOR_MSG_ERR);
        strncpy(ed.lines[0], "", MAX_LINE_LEN);
        ed.num_lines = 1;
        return;
    }

    size_t file_buf_size = 65536;
    char *buf = (char*)kmalloc(file_buf_size);
    if (!buf) {
        strncpy(ed.lines[0], "", MAX_LINE_LEN);
        ed.num_lines = 1;
        return;
    }

    size_t file_len = 0;
    if (fat_read_file(&fs, ed.filename, buf, file_buf_size - 1, &file_len) == 0 && file_len > 0) {
        buf[file_len] = '\0';

        char *p = buf;
        while (*p && ed.num_lines < MAX_EDITOR_LINES) {
            char *line_start = p;
            while (*p && *p != '\n' && *p != '\r') p++;

            size_t len = p - line_start;
            if (len >= MAX_LINE_LEN) len = MAX_LINE_LEN - 1;

            memcpy(ed.lines[ed.num_lines], line_start, len);
            ed.lines[ed.num_lines][len] = '\0';
            ed.num_lines++;

            if (*p == '\r' && *(p + 1) == '\n') p += 2;
            else if (*p == '\n' || *p == '\r') p++;
        }

        if (ed.num_lines == 0) {
            strncpy(ed.lines[0], "", MAX_LINE_LEN);
            ed.num_lines = 1;
        }

        char msg_buf[64];
        snprintf(msg_buf, sizeof(msg_buf), "[ Loaded %u bytes from %s ]", (uint32_t)file_len, ed.filename);
        set_status_msg(msg_buf, COLOR_MSG_OK);
    } else {
        strncpy(ed.lines[0], "", MAX_LINE_LEN);
        ed.num_lines = 1;
        char msg_buf[64];
        snprintf(msg_buf, sizeof(msg_buf), "[ New file '%s' ]", ed.filename);
        set_status_msg(msg_buf, COLOR_MSG_OK);
    }

    kfree(buf);
}

static bool editor_save_file(void) {
    block_dev_t *bdev = blockdev_get(ed.dev_name);
    if (!bdev) {
        set_status_msg("[ Save failed: device not found ]", COLOR_MSG_ERR);
        return false;
    }

    fat_fs_t fs;
    if (fat_mount(bdev, &fs) != 0) {
        set_status_msg("[ Save failed: mount error ]", COLOR_MSG_ERR);
        return false;
    }

    size_t total_alloc = 0;
    for (size_t i = 0; i < ed.num_lines; i++) {
        total_alloc += strlen(ed.lines[i]) + 2;
    }
    if (total_alloc < 512) total_alloc = 512;

    char *buf = (char*)kmalloc(total_alloc);
    if (!buf) {
        set_status_msg("[ Save failed: out of memory ]", COLOR_MSG_ERR);
        return false;
    }

    size_t out_pos = 0;
    for (size_t i = 0; i < ed.num_lines; i++) {
        size_t len = strlen(ed.lines[i]);
        memcpy(buf + out_pos, ed.lines[i], len);
        out_pos += len;
        if (i < ed.num_lines - 1) {
            buf[out_pos++] = '\n';
        }
    }
    buf[out_pos] = '\0';

    serial_puts("[EDITOR] Saving file...\n");
    if (fat_write_file(&fs, ed.filename, buf, out_pos) == 0) {
        serial_puts("[EDITOR] Saved successfully!\n");
        ed.modified = false;
        char msg_buf[64];
        snprintf(msg_buf, sizeof(msg_buf), "[ Saved %u bytes to %s ]", (uint32_t)out_pos, ed.filename);
        set_status_msg(msg_buf, COLOR_MSG_OK);
        kfree(buf);
        return true;
    } else {
        serial_puts("[EDITOR] Save FAILED!\n");
        set_status_msg("[ Save failed: write error ]", COLOR_MSG_ERR);
        kfree(buf);
        return false;
    }
}

/* ========================================================================= */
/* Screen Rendering Routines                                                 */
/* ========================================================================= */

static void editor_render(void) {
    /* 1. Header Bar (Row 0) */
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        vga_putc_at(' ', COLOR_HEADER, x, 0);
    }
    vga_puts_at(" GEMIOS EDIT ", COLOR_HEADER, 1, 0);
    vga_putc_at(0xB3, COLOR_HEADER, 13, 0); /* │ */

    char title[64];
    snprintf(title, sizeof(title), " File: %s:%s  (UTF-8) ", ed.dev_name, ed.filename);
    vga_puts_at(title, COLOR_HEADER, 15, 0);

    /* 2. Text Editing Area (Rows 1 to 22) */
    for (int r = 0; r < VIEW_ROWS; r++) {
        int line_idx = ed.top_row + r;
        int screen_y = 1 + r;

        // Clear row to DOS blue
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            vga_putc_at(' ', COLOR_TEXT, x, screen_y);
        }

        if (line_idx < (int)ed.num_lines) {
            // Draw Line Number in gutter
            char num_str[8];
            snprintf(num_str, sizeof(num_str), "%4d ", line_idx + 1);
            vga_puts_at(num_str, COLOR_GUTTER, 0, screen_y);
            vga_putc_at(0xB3, COLOR_GUTTER, 5, screen_y); /* Gutter divider │ */

            // Render UTF-8 line content mapped to CP437
            const char *line = ed.lines[line_idx];
            int text_col = 0;
            size_t b_idx = 0;
            size_t line_bytes = strlen(line);

            while (b_idx < line_bytes && text_col < VGA_WIDTH - GUTTER_WIDTH + ed.left_col) {
                if (line[b_idx] == '\t') {
                    int next_tab = text_col + (4 - (text_col % 4));
                    while (text_col < next_tab && text_col < VGA_WIDTH - GUTTER_WIDTH + ed.left_col) {
                        if (text_col >= ed.left_col) {
                            vga_putc_at(' ', COLOR_TEXT, GUTTER_WIDTH + (text_col - ed.left_col), screen_y);
                        }
                        text_col++;
                    }
                    b_idx++;
                } else {
                    uint32_t cp = 0;
                    int c_len = utf8_decode(&line[b_idx], &cp);
                    if (c_len <= 0) break;

                    if (text_col >= ed.left_col) {
                        uint8_t glyph = utf8_to_cp437(cp);
                        vga_putc_at((char)glyph, COLOR_TEXT, GUTTER_WIDTH + (text_col - ed.left_col), screen_y);
                    }
                    text_col++;
                    b_idx += c_len;
                }
            }
        } else {
            // Empty line below file EOF
            vga_puts_at("   ~ ", COLOR_GUTTER, 0, screen_y);
            vga_putc_at(0xB3, COLOR_GUTTER, 5, screen_y);
        }
    }

    /* 3. Shortcut Bar (Row 23) */
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        vga_putc_at(' ', COLOR_STATUS, x, 23);
    }
    vga_puts_at(" ^S Save   ^Q Exit   ^N New   ^K DelLine   ESC Exit", COLOR_STATUS, 2, 23);

    /* 4. Status Bar (Row 24) */
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        vga_putc_at(' ', COLOR_STATUS, x, 24);
    }

    // Message or Line Status
    if (ed.msg[0] != '\0' && (pit_get_ticks() - ed.msg_time < 3000)) {
        vga_puts_at(ed.msg, ed.msg_color, 2, 24);
    } else {
        char stat[80];
        int vis_col = utf8_byte_to_column(ed.lines[ed.cur_row], ed.cur_col);
        snprintf(stat, sizeof(stat), " Line: %d/%d  Col: %d   UTF-8",
                 ed.cur_row + 1, (int)ed.num_lines, vis_col + 1);
        vga_puts_at(stat, COLOR_STATUS, 2, 24);
    }

    if (ed.modified) {
        vga_puts_at(" [Modified] ", COLOR_STATUS_MOD, 66, 24);
    } else {
        vga_puts_at("   [Saved]  ", COLOR_STATUS, 66, 24);
    }

    /* 5. Hardware Cursor Placement */
    int cursor_vis_col = utf8_byte_to_column(ed.lines[ed.cur_row], ed.cur_col);
    int scr_x = GUTTER_WIDTH + (cursor_vis_col - ed.left_col);
    int scr_y = 1 + (ed.cur_row - ed.top_row);

    if (scr_x < GUTTER_WIDTH) scr_x = GUTTER_WIDTH;
    if (scr_x >= VGA_WIDTH) scr_x = VGA_WIDTH - 1;
    if (scr_y < 1) scr_y = 1;
    if (scr_y > VIEW_ROWS) scr_y = VIEW_ROWS;

    vga_set_cursor(scr_x, scr_y);
}

static void editor_adjust_view(void) {
    if (ed.cur_row < 0) ed.cur_row = 0;
    if (ed.cur_row >= (int)ed.num_lines) ed.cur_row = (int)ed.num_lines - 1;

    size_t line_len = strlen(ed.lines[ed.cur_row]);
    if (ed.cur_col < 0) ed.cur_col = 0;
    if (ed.cur_col > (int)line_len) ed.cur_col = (int)line_len;

    // Adjust vertical scroll
    if (ed.cur_row < ed.top_row) {
        ed.top_row = ed.cur_row;
    }
    if (ed.cur_row >= ed.top_row + VIEW_ROWS) {
        ed.top_row = ed.cur_row - VIEW_ROWS + 1;
    }

    // Adjust horizontal scroll
    int vis_col = utf8_byte_to_column(ed.lines[ed.cur_row], ed.cur_col);
    int view_width = VGA_WIDTH - GUTTER_WIDTH;

    if (vis_col < ed.left_col) {
        ed.left_col = vis_col;
    }
    if (vis_col >= ed.left_col + view_width) {
        ed.left_col = vis_col - view_width + 1;
    }
}

/* ========================================================================= */
/* Editing Logic & Commands                                                  */
/* ========================================================================= */

static void editor_insert_char(char c) {
    char *line = ed.lines[ed.cur_row];
    size_t len = strlen(line);
    if (len >= MAX_LINE_LEN - 2) return;

    memmove(line + ed.cur_col + 1, line + ed.cur_col, len - ed.cur_col + 1);
    line[ed.cur_col] = c;
    ed.cur_col++;
    ed.modified = true;
}

static void editor_insert_newline(void) {
    if (ed.num_lines >= MAX_EDITOR_LINES - 1) return;

    char *cur = ed.lines[ed.cur_row];

    // Shift lines down
    for (size_t i = ed.num_lines; i > (size_t)ed.cur_row + 1; i--) {
        strncpy(ed.lines[i], ed.lines[i - 1], MAX_LINE_LEN);
    }

    // Split line
    strncpy(ed.lines[ed.cur_row + 1], cur + ed.cur_col, MAX_LINE_LEN);
    cur[ed.cur_col] = '\0';

    ed.num_lines++;
    ed.cur_row++;
    ed.cur_col = 0;
    ed.modified = true;
}

static void editor_backspace(void) {
    char *line = ed.lines[ed.cur_row];

    if (ed.cur_col > 0) {
        int prev = utf8_prev_char(line, ed.cur_col);
        size_t len = strlen(line);

        memmove(line + prev, line + ed.cur_col, len - ed.cur_col + 1);
        ed.cur_col = prev;
        ed.modified = true;
    } else if (ed.cur_row > 0) {
        // Merge with previous line
        char *prev_line = ed.lines[ed.cur_row - 1];
        size_t prev_len = strlen(prev_line);
        size_t cur_len = strlen(line);

        if (prev_len + cur_len < MAX_LINE_LEN) {
            strcat(prev_line, line);

            for (size_t i = ed.cur_row; i < ed.num_lines - 1; i++) {
                strncpy(ed.lines[i], ed.lines[i + 1], MAX_LINE_LEN);
            }
            ed.num_lines--;
            ed.cur_row--;
            ed.cur_col = (int)prev_len;
            ed.modified = true;
        }
    }
}

static void editor_delete(void) {
    char *line = ed.lines[ed.cur_row];
    size_t len = strlen(line);

    if (ed.cur_col < (int)len) {
        int next = utf8_next_char(line, ed.cur_col);
        memmove(line + ed.cur_col, line + next, len - next + 1);
        ed.modified = true;
    } else if (ed.cur_row < (int)ed.num_lines - 1) {
        // Merge next line into current line
        char *next_line = ed.lines[ed.cur_row + 1];
        size_t next_len = strlen(next_line);

        if (len + next_len < MAX_LINE_LEN) {
            strcat(line, next_line);

            for (size_t i = ed.cur_row + 1; i < ed.num_lines - 1; i++) {
                strncpy(ed.lines[i], ed.lines[i + 1], MAX_LINE_LEN);
            }
            ed.num_lines--;
            ed.modified = true;
        }
    }
}

static void editor_delete_line(void) {
    if (ed.num_lines <= 1) {
        ed.lines[0][0] = '\0';
        ed.cur_row = 0;
        ed.cur_col = 0;
        ed.modified = true;
        return;
    }

    for (size_t i = ed.cur_row; i < ed.num_lines - 1; i++) {
        strncpy(ed.lines[i], ed.lines[i + 1], MAX_LINE_LEN);
    }
    ed.num_lines--;
    if (ed.cur_row >= (int)ed.num_lines) ed.cur_row = (int)ed.num_lines - 1;
    ed.cur_col = 0;
    ed.modified = true;
    set_status_msg("[ Line deleted ]", COLOR_MSG_OK);
}

static int get_serial_byte_timed(int loops) {
    while (!serial_has_char() && --loops > 0) {
        io_wait();
    }
    if (serial_has_char()) {
        return (uint8_t)serial_getchar();
    }
    return -1;
}

static int editor_get_char(void) {
    if (usb_kbd_has_char()) {
        return (int)usb_kbd_getchar();
    }
    if (serial_has_char()) {
        uint8_t c = (uint8_t)serial_getchar();
        if (c == 27) { // ESC or ANSI escape sequence
            int c2 = get_serial_byte_timed(500000);
            if (c2 == '[') {
                int c3 = get_serial_byte_timed(500000);
                if (c3 == 'A') return KEY_UP;
                if (c3 == 'B') return KEY_DOWN;
                if (c3 == 'C') return KEY_RIGHT;
                if (c3 == 'D') return KEY_LEFT;
                if (c3 == 'H') return KEY_HOME;
                if (c3 == 'F') return KEY_END;
                if (c3 == '1') {
                    int c4 = get_serial_byte_timed(500000);
                    if (c4 == '~') return KEY_HOME;
                    if (c4 == '1') { get_serial_byte_timed(500000); return KEY_F1; }
                    if (c4 == '2') { get_serial_byte_timed(500000); return KEY_F2; }
                    return KEY_HOME;
                }
                if (c3 == '3') { get_serial_byte_timed(500000); return KEY_DELETE; }
                if (c3 == '4') { get_serial_byte_timed(500000); return KEY_END; }
                if (c3 == '5') { get_serial_byte_timed(500000); return KEY_PGUP; }
                if (c3 == '6') { get_serial_byte_timed(500000); return KEY_PGDN; }
            } else if (c2 == 'O') {
                int c3 = get_serial_byte_timed(500000);
                if (c3 == 'P') return KEY_F1;
                if (c3 == 'Q') return KEY_F2;
                if (c3 == 'R') return KEY_F3;
            }
            if (c2 == -1) return KEY_ESC;
            return c2;
        }
        return (int)c;
    }
    return 0;
}

void editor_open(const char *dev_name, const char *filename) {
    memset(&ed, 0, sizeof(ed));
    strncpy(ed.dev_name, (dev_name && dev_name[0]) ? dev_name : "usb0", sizeof(ed.dev_name) - 1);
    strncpy(ed.filename, (filename && filename[0]) ? filename : "UNTITLED.TXT", sizeof(ed.filename) - 1);
    ed.running = true;

    editor_load_file();
    serial_puts("[EDITOR] Ready\n");
    bool needs_redraw = true;

    while (ed.running) {
        xhci_poll();

        if (needs_redraw) {
            editor_adjust_view();
            editor_render();
            needs_redraw = false;
        }

        int c = editor_get_char();
        if (c == 0) {
            rtos_sleep_ms(10);
            continue;
        }

        needs_redraw = true;

        switch (c) {
            case KEY_CTRL_S:
                editor_save_file();
                break;

            case KEY_CTRL_Q:
            case KEY_CTRL_X:
            case KEY_ESC:
                if (ed.modified) {
                    set_status_msg("[ Save changes before exit? (Y/N/Esc) ]", COLOR_MSG_ERR);
                    editor_render();

                    bool deciding = true;
                    while (deciding) {
                        xhci_poll();
                        int opt = editor_get_char();
                        if (opt == 'y' || opt == 'Y' || opt == '\r' || opt == '\n') {
                            editor_save_file();
                            ed.running = false;
                            deciding = false;
                        } else if (opt == 'n' || opt == 'N' || opt == KEY_CTRL_Q || opt == KEY_CTRL_X) {
                            ed.running = false;
                            deciding = false;
                        } else if (opt == KEY_ESC || opt == 'c' || opt == 'C') {
                            set_status_msg("[ Cancelled ]", COLOR_MSG_OK);
                            deciding = false;
                        }
                        rtos_sleep_ms(10);
                    }
                } else {
                    ed.running = false;
                }
                break;

            case KEY_CTRL_N:
                if (ed.modified) {
                    editor_save_file();
                }
                ed.num_lines = 1;
                ed.lines[0][0] = '\0';
                ed.cur_row = 0;
                ed.cur_col = 0;
                ed.modified = false;
                set_status_msg("[ New document ]", COLOR_MSG_OK);
                break;

            case KEY_CTRL_K:
                editor_delete_line();
                break;

            case KEY_UP:
                if (ed.cur_row > 0) ed.cur_row--;
                break;

            case KEY_DOWN:
                if (ed.cur_row < (int)ed.num_lines - 1) ed.cur_row++;
                break;

            case KEY_LEFT:
                ed.cur_col = utf8_prev_char(ed.lines[ed.cur_row], ed.cur_col);
                break;

            case KEY_RIGHT:
                ed.cur_col = utf8_next_char(ed.lines[ed.cur_row], ed.cur_col);
                break;

            case KEY_HOME:
                ed.cur_col = 0;
                break;

            case KEY_END:
                ed.cur_col = (int)strlen(ed.lines[ed.cur_row]);
                break;

            case KEY_PGUP:
                ed.cur_row -= 20;
                if (ed.cur_row < 0) ed.cur_row = 0;
                break;

            case KEY_PGDN:
                ed.cur_row += 20;
                if (ed.cur_row >= (int)ed.num_lines) ed.cur_row = (int)ed.num_lines - 1;
                break;

            case '\b':
            case 127:
                editor_backspace();
                break;

            case KEY_DELETE:
                editor_delete();
                break;

            case '\n':
            case '\r':
                editor_insert_newline();
                break;

            case '\t':
                for (int t = 0; t < 4; t++) {
                    editor_insert_char(' ');
                }
                break;

            default:
                // Printable characters (ASCII & UTF-8 bytes 0x80..0xFF)
                if (c >= 32 && c <= 255) {
                    editor_insert_char((char)(uint8_t)c);
                }
                break;
        }
    }

    serial_puts("[EDITOR] Exited editor\n");

    // Clear screen and restore terminal on exit
    vga_clear();
    vga_set_color(0x07);
    vga_draw_status_bar();
}
