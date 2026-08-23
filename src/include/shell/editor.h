/*
 * This is free and unencumbered software released into the public domain.
 * GEMOS Preemptive Real-Time Operating System
 */

#ifndef GEMOS_EDITOR_H
#define GEMOS_EDITOR_H

#include "types.h"

int utf8_decode(const char *str, uint32_t *codepoint);
uint8_t utf8_to_cp437(uint32_t cp);
void editor_open(const char *dev_name, const char *filename);

#endif /* GEMOS_EDITOR_H */
