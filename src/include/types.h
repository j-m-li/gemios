/*
 * This is free and unencumbered software released into the public domain.
 * GEMOS Preemptive Real-Time Operating System
 */

#ifndef GEMOS_TYPES_H
#define GEMOS_TYPES_H

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

typedef uint32_t           size_t;
typedef int32_t            ssize_t;
typedef uint32_t           uintptr_t;
typedef int32_t            intptr_t;
typedef uint32_t           phys_addr_t;
typedef uint32_t           virt_addr_t;

#define NULL ((void*)0)

#ifndef __cplusplus
typedef _Bool bool;
#define true  1
#define false 0
#endif

#define PACKED __attribute__((packed))
#define ALIGNED(n) __attribute__((aligned(n)))
#define NORETURN __attribute__((noreturn))
#define UNUSED(x) ((void)(x))

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))
#define ALIGN_DOWN(x, align) ((x) & ~((align) - 1))

/* Special Keyboard Key Codes (values > 255 to avoid colliding with UTF-8 bytes 0x00..0xFF) */
#define KEY_UP       0x101
#define KEY_DOWN     0x102
#define KEY_LEFT     0x103
#define KEY_RIGHT    0x104
#define KEY_HOME     0x105
#define KEY_END      0x106
#define KEY_DELETE   0x107
#define KEY_PGUP     0x108
#define KEY_PGDN     0x109
#define KEY_F1       0x110
#define KEY_F2       0x111
#define KEY_F3       0x112
#define KEY_ESC      0x1B

/* Common Ctrl Keys */
#define KEY_CTRL_A   ((char)0x01)
#define KEY_CTRL_C   ((char)0x03)
#define KEY_CTRL_K   ((char)0x0B)
#define KEY_CTRL_N   ((char)0x0E)
#define KEY_CTRL_O   ((char)0x0F)
#define KEY_CTRL_Q   ((char)0x11)
#define KEY_CTRL_S   ((char)0x13)
#define KEY_CTRL_X   ((char)0x18)
#define KEY_CTRL_Z   ((char)0x1A)

#endif /* GEMOS_TYPES_H */
