/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "string.h"
#include "vga.h"

/* 64-bit arithmetic helper routines for bare-metal x86 */
uint64_t __udivmoddi4(uint64_t num, uint64_t den, uint64_t *rem_p) {
    uint64_t quot;
    uint64_t qbit;
    quot = 0;
    qbit = 1;
    if (den == 0) return 0;
    while ((int64_t)den >= 0 && den < num) {
        den <<= 1;
        qbit <<= 1;
    }
    while (qbit) {
        if (num >= den) {
            num -= den;
            quot |= qbit;
        }
        den >>= 1;
        qbit >>= 1;
    }
    if (rem_p) *rem_p = num;
    return quot;
}

uint64_t __udivdi3(uint64_t a, uint64_t b) {
    return __udivmoddi4(a, b, NULL);
}

uint64_t __umoddi3(uint64_t a, uint64_t b) {
    uint64_t rem;
    rem = 0;
    __udivmoddi4(a, b, &rem);
    return rem;
}

int64_t __divdi3(int64_t a, int64_t b) {
    bool neg;
    uint64_t res;
    neg = false;
    if (a < 0) { a = -a; neg = !neg; }
    if (b < 0) { b = -b; neg = !neg; }
    res = __udivmoddi4((uint64_t)a, (uint64_t)b, NULL);
    return neg ? -(int64_t)res : (int64_t)res;
}

int64_t __moddi3(int64_t a, int64_t b) {
    bool neg;
    uint64_t rem;
    neg = false;
    rem = 0;
    if (a < 0) { a = -a; neg = true; }
    if (b < 0) { b = -b; }
    __udivmoddi4((uint64_t)a, (uint64_t)b, &rem);
    return neg ? -(int64_t)rem : (int64_t)rem;
}

static void format_number(char **buf, size_t *rem, uint64_t num, int base, int width, char pad_char, bool uppercase, bool is_signed, bool left_align) {
    char temp[64];
    int pos;
    const char *digits;
    bool negative;
    int len;

    pos = 0;
    digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    negative = false;

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

    len = pos;

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
    char *buf;
    size_t rem;

    if (!str || size == 0) return 0;

    buf = str;
    rem = size;

    while (*format && rem > 1) {
        bool left_align;
        char pad_char;
        int width;
        bool is_long;

        if (*format != '%') {
            *buf++ = *format++;
            rem--;
            continue;
        }

        format++; /* Skip '%' */

        left_align = false;
        pad_char = ' ';
        width = 0;

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

        is_long = false;
        (void)is_long;
        if (*format == 'l') {
            is_long = true;
            format++;
            if (*format == 'l') {
                format++;
            }
        }

        switch (*format) {
            case 'c': {
                char c;
                c = (char)va_arg(ap, int);
                if (rem > 1) {
                    *buf++ = c;
                    rem--;
                }
                break;
            }
            case 's': {
                const char *s;
                int slen;
                s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                slen = strlen(s);

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
                int32_t val;
                val = va_arg(ap, int32_t);
                format_number(&buf, &rem, (uint64_t)val, 10, width, pad_char, false, true, left_align);
                break;
            }
            case 'u': {
                uint32_t val;
                val = va_arg(ap, uint32_t);
                format_number(&buf, &rem, val, 10, width, pad_char, false, false, left_align);
                break;
            }
            case 'x': {
                uint32_t val;
                val = va_arg(ap, uint32_t);
                format_number(&buf, &rem, val, 16, width, pad_char, false, false, left_align);
                break;
            }
            case 'X': {
                uint32_t val;
                val = va_arg(ap, uint32_t);
                format_number(&buf, &rem, val, 16, width, pad_char, true, false, left_align);
                break;
            }
            case 'p': {
                uintptr_t val;
                val = (uintptr_t)va_arg(ap, void*);
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
    int ret;
    va_start(ap, format);
    ret = vsnprintf(str, size, format, ap);
    va_end(ap);
    return ret;
}

int sprintf(char *str, const char *format, ...) {
    va_list ap;
    int ret;
    va_start(ap, format);
    ret = vsnprintf(str, 1024, format, ap);
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
    uint8_t old_color;
    va_start(ap, format);
    vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    old_color = vga_get_color();
    vga_set_color(color);
    vga_puts(buf);
    vga_set_color(old_color);

    serial_puts(buf);
}

void hexdump(const void *data, size_t size) {
    const uint8_t *p;
    size_t i;
    size_t j;

    p = (const uint8_t*)data;
    for (i = 0; i < size; i += 16) {
        kprintf("%04x: ", (uint32_t)i);
        for (j = 0; j < 16; j++) {
            if (i + j < size) {
                kprintf("%02x ", p[i + j]);
            } else {
                kprintf("   ");
            }
        }
        kprintf(" |");
        for (j = 0; j < 16; j++) {
            if (i + j < size) {
                char c;
                c = (char)p[i + j];
                kprintf("%c", (c >= 32 && c <= 126) ? c : '.');
            }
        }
        kprintf("|\n");
    }
}
