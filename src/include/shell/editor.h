/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_EDITOR_H
#define GEMIOS_EDITOR_H

#include "types.h"

int utf8_decode(const char *str, uint32_t *codepoint);
uint8_t utf8_to_cp437(uint32_t cp);
void editor_open(const char *dev_name, const char *filename);

#endif /* GEMIOS_EDITOR_H */
