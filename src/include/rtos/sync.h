/*
 * This is free and unencumbered software released into the public domain.
 * GEMOS Preemptive Real-Time Operating System
 */

#ifndef GEMOS_RTOS_SYNC_H
#define GEMOS_RTOS_SYNC_H

#include "task.h"

/* Semaphore */
typedef struct {
    int32_t count;
    int32_t max_count;
    task_t *wait_head;
    task_t *wait_tail;
} rtos_sem_t;

void rtos_sem_init(rtos_sem_t *sem, int32_t initial_count, int32_t max_count);
bool rtos_sem_wait(rtos_sem_t *sem, uint32_t timeout_ms);
void rtos_sem_signal(rtos_sem_t *sem);

/* Mutex with priority inheritance support */
typedef struct {
    bool locked;
    task_t *owner;
    task_t *wait_head;
    task_t *wait_tail;
} rtos_mutex_t;

void rtos_mutex_init(rtos_mutex_t *mutex);
bool rtos_mutex_lock(rtos_mutex_t *mutex, uint32_t timeout_ms);
void rtos_mutex_unlock(rtos_mutex_t *mutex);

/* Message Queue */
typedef struct {
    uint8_t *buffer;
    size_t item_size;
    size_t capacity;
    size_t count;
    size_t head;
    size_t tail;
    rtos_sem_t sem_items;
    rtos_sem_t sem_spaces;
    rtos_mutex_t lock;
} rtos_queue_t;

rtos_queue_t *rtos_queue_create(size_t item_size, size_t capacity);
void rtos_queue_destroy(rtos_queue_t *q);
bool rtos_queue_send(rtos_queue_t *q, const void *item, uint32_t timeout_ms);
bool rtos_queue_receive(rtos_queue_t *q, void *item, uint32_t timeout_ms);
size_t rtos_queue_count(rtos_queue_t *q);

/* Event Flags */
typedef struct {
    uint32_t flags;
    task_t *wait_head;
} rtos_event_t;

void rtos_event_init(rtos_event_t *ev);
uint32_t rtos_event_wait(rtos_event_t *ev, uint32_t mask, bool clear_on_exit, uint32_t timeout_ms);
void rtos_event_set(rtos_event_t *ev, uint32_t mask);
void rtos_event_clear(rtos_event_t *ev, uint32_t mask);

#endif /* GEMOS_RTOS_SYNC_H */
