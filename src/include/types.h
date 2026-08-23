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

/* Special Keyboard Key Codes */
#define KEY_UP       ((char)0x80)
#define KEY_DOWN     ((char)0x81)
#define KEY_LEFT     ((char)0x82)
#define KEY_RIGHT    ((char)0x83)
#define KEY_HOME     ((char)0x84)
#define KEY_END      ((char)0x85)
#define KEY_DELETE   ((char)0x86)

#endif /* GEMOS_TYPES_H */
