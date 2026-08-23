/*
 * This is free and unencumbered software released into the public domain.
 * GEMOS Preemptive Real-Time Operating System
 */

#ifndef GEMOS_SHELL_H
#define GEMOS_SHELL_H

#include "types.h"

void shell_init(void);
void shell_task(void *arg);
void shell_execute_command(char *cmd_line);

#endif /* GEMOS_SHELL_H */
