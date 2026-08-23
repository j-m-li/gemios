/*
 * This is free and unencumbered software released into the public domain.
 * GEMOS Preemptive Real-Time Operating System
 */

#include "string.h"
#include "vga.h"

static void format_number(char **buf, size_t *rem, uint64_t num, int base, int width, char pad_char, bool uppercase, bool is_signed, bool left_align) {
    char temp[64];
    int pos = 0;
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    bool negative = false;

    if (is_signed && (int64_t)num < 0) {
        negative = true;
        num = -(int64_t)num;
    }

    if (num == 0) {
        temp[pos++] = '0';
    } else {
        while (num > 0) {
            temp[pos++] = digits[num % base];
            num /= base;
        }
    }

    if (negative && pad_char == '0' && !left_align) {
        if (*rem > 1) {
            *(*buf)++ = '-';
            (*rem)--;
        }
        negative = false;
        if (width > 0) width--;
    }

    if (negative) {
        temp[pos++] = '-';
    }

    int len = pos;

    if (!left_align) {
        while (len < width && *rem > 1) {
            *(*buf)++ = pad_char;
            (*rem)--;
            width--;
        }
    }

    while (pos > 0) {
        if (*rem > 1) {
            *(*buf)++ = temp[--pos];
            (*rem)--;
        } else {
            pos--;
        }
    }

    if (left_align) {
        while (len < width && *rem > 1) {
            *(*buf)++ = ' ';
            (*rem)--;
            len++;
        }
    }
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    if (!str || size == 0) return 0;

    char *buf = str;
    size_t rem = size;

    while (*format && rem > 1) {
        if (*format != '%') {
            *buf++ = *format++;
            rem--;
            continue;
        }

        format++; // Skip '%'

        bool left_align = false;
        char pad_char = ' ';
        int width = 0;

        if (*format == '-') {
            left_align = true;
            format++;
        }

        if (*format == '0' && !left_align) {
            pad_char = '0';
            format++;
        }

        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }

        bool is_long = false; (void)is_long;
        if (*format == 'l') {
            is_long = true;
            format++;
            if (*format == 'l') {
                format++;
            }
        }

        switch (*format) {
            case 'c': {
                char c = (char)va_arg(ap, int);
                if (rem > 1) {
                    *buf++ = c;
                    rem--;
                }
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                int slen = strlen(s);

                if (!left_align) {
                    while (slen < width && rem > 1) {
                        *buf++ = ' ';
                        rem--;
                        width--;
                    }
                }

                while (*s && rem > 1) {
                    *buf++ = *s++;
                    rem--;
                }

                if (left_align) {
                    while (slen < width && rem > 1) {
                        *buf++ = ' ';
                        rem--;
                        slen++;
                    }
                }
                break;
            }
            case 'd':
            case 'i': {
                int32_t val = va_arg(ap, int32_t);
                format_number(&buf, &rem, (uint64_t)val, 10, width, pad_char, false, true, left_align);
                break;
            }
            case 'u': {
                uint32_t val = va_arg(ap, uint32_t);
                format_number(&buf, &rem, val, 10, width, pad_char, false, false, left_align);
                break;
            }
            case 'x': {
                uint32_t val = va_arg(ap, uint32_t);
                format_number(&buf, &rem, val, 16, width, pad_char, false, false, left_align);
                break;
            }
            case 'X': {
                uint32_t val = va_arg(ap, uint32_t);
                format_number(&buf, &rem, val, 16, width, pad_char, true, false, left_align);
                break;
            }
            case 'p': {
                uintptr_t val = (uintptr_t)va_arg(ap, void*);
                if (rem > 3) {
                    *buf++ = '0'; rem--;
                    *buf++ = 'x'; rem--;
                }
                format_number(&buf, &rem, val, 16, 8, '0', false, false, false);
                break;
            }
            case '%': {
                if (rem > 1) {
                    *buf++ = '%';
                    rem--;
                }
                break;
            }
            default:
                if (rem > 1) {
                    *buf++ = *format;
                    rem--;
                }
                break;
        }
        format++;
    }

    *buf = '\0';
    return (int)(buf - str);
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, size, format, ap);
    va_end(ap);
    return ret;
}

int sprintf(char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, 1024, format, ap);
    va_end(ap);
    return ret;
}

void kprintf(const char *format, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, format);
    vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    vga_puts(buf);
    serial_puts(buf);
}

void kprint_color(uint8_t color, const char *format, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, format);
    vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    uint8_t old_color = vga_get_color();
    vga_set_color(color);
    vga_puts(buf);
    vga_set_color(old_color);

    serial_puts(buf);
}

void hexdump(const void *data, size_t size) {
    const uint8_t *p = (const uint8_t*)data;
    for (size_t i = 0; i < size; i += 16) {
        kprintf("%04x: ", (uint32_t)i);
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                kprintf("%02x ", p[i + j]);
            } else {
                kprintf("   ");
            }
        }
        kprintf(" |");
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                char c = (char)p[i + j];
                kprintf("%c", (c >= 32 && c <= 126) ? c : '.');
            }
        }
        kprintf("|\n");
    }
}
