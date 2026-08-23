/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_STRING_H
#define GEMIOS_STRING_H

#include "types.h"
#include <stdarg.h>

void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
char *strdup(const char *s);

int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int snprintf(char *str, size_t size, const char *format, ...);
int sprintf(char *str, const char *format, ...);

void kprintf(const char *format, ...);
void kprint_color(uint8_t color, const char *format, ...);
void hexdump(const void *data, size_t size);

#endif /* GEMIOS_STRING_H */
