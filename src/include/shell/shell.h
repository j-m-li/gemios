/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_SHELL_H
#define GEMIOS_SHELL_H

#include "types.h"

void shell_init(void);
void shell_task(void *arg);
void shell_execute_command(char *cmd_line);

#endif /* GEMIOS_SHELL_H */
