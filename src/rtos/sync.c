/*
 * This is free and unencumbered software released into the public domain.
 * GEMOS Preemptive Real-Time Operating System
 */

#include "sync.h"
#include "sched.h"
#include "time.h"
#include "heap.h"
#include "string.h"
#include "io.h"

/* Semaphore Implementation */
void rtos_sem_init(rtos_sem_t *sem, int32_t initial_count, int32_t max_count) {
    sem->count = initial_count;
    sem->max_count = max_count;
    sem->wait_head = NULL;
    sem->wait_tail = NULL;
}

static void add_waiter(task_t **head, task_t **tail, task_t *task) {
    task->next_wait = NULL;
    if (!*head) {
        *head = task;
        *tail = task;
    } else {
        (*tail)->next_wait = task;
        *tail = task;
    }
}

static task_t *pop_waiter(task_t **head, task_t **tail) {
    if (!*head) return NULL;
    task_t *task = *head;
    *head = task->next_wait;
    if (!*head) {
        *tail = NULL;
    }
    task->next_wait = NULL;
    return task;
}

bool rtos_sem_wait(rtos_sem_t *sem, uint32_t timeout_ms) {
    uint32_t flags = irq_save();

    if (sem->count > 0) {
        sem->count--;
        irq_restore(flags);
        return true;
    }

    if (timeout_ms == 0) {
        irq_restore(flags);
        return false;
    }

    task_t *cur = rtos_current_task();
    if (!cur) {
        irq_restore(flags);
        return false;
    }

    add_waiter(&sem->wait_head, &sem->wait_tail, cur);
    rtos_task_block(cur, sem);
    irq_restore(flags);

    return true;
}

void rtos_sem_signal(rtos_sem_t *sem) {
    uint32_t flags = irq_save();

    task_t *waiter = pop_waiter(&sem->wait_head, &sem->wait_tail);
    if (waiter) {
        rtos_task_unblock(waiter);
    } else {
        if (sem->count < sem->max_count) {
            sem->count++;
        }
    }

    irq_restore(flags);
}

/* Mutex Implementation with Priority Inheritance */
void rtos_mutex_init(rtos_mutex_t *mutex) {
    mutex->locked = false;
    mutex->owner = NULL;
    mutex->wait_head = NULL;
    mutex->wait_tail = NULL;
}

bool rtos_mutex_lock(rtos_mutex_t *mutex, uint32_t timeout_ms) {
    uint32_t flags = irq_save();
    task_t *cur = rtos_current_task();

    if (!mutex->locked) {
        mutex->locked = true;
        mutex->owner = cur;
        irq_restore(flags);
        return true;
    }

    if (mutex->owner == cur) {
        // Recursive lock (allow)
        irq_restore(flags);
        return true;
    }

    if (timeout_ms == 0) {
        irq_restore(flags);
        return false;
    }

    // Priority inheritance: boost owner priority if waiter has higher priority
    if (cur && mutex->owner && cur->priority > mutex->owner->priority) {
        mutex->owner->priority = cur->priority;
    }

    if (cur) {
        add_waiter(&mutex->wait_head, &mutex->wait_tail, cur);
        rtos_task_block(cur, mutex);
    }

    irq_restore(flags);
    return true;
}

void rtos_mutex_unlock(rtos_mutex_t *mutex) {
    uint32_t flags = irq_save();
    task_t *cur = rtos_current_task();

    if (!mutex->locked || mutex->owner != cur) {
        irq_restore(flags);
        return;
    }

    // Restore owner base priority
    if (mutex->owner) {
        mutex->owner->priority = mutex->owner->base_priority;
    }

    task_t *waiter = pop_waiter(&mutex->wait_head, &mutex->wait_tail);
    if (waiter) {
        mutex->owner = waiter;
        rtos_task_unblock(waiter);
    } else {
        mutex->locked = false;
        mutex->owner = NULL;
    }

    irq_restore(flags);
}

/* Message Queue Implementation */
rtos_queue_t *rtos_queue_create(size_t item_size, size_t capacity) {
    rtos_queue_t *q = (rtos_queue_t*)kmalloc(sizeof(rtos_queue_t));
    if (!q) return NULL;

    q->buffer = (uint8_t*)kmalloc(item_size * capacity);
    if (!q->buffer) {
        kfree(q);
        return NULL;
    }

    q->item_size = item_size;
    q->capacity = capacity;
    q->count = 0;
    q->head = 0;
    q->tail = 0;

    rtos_sem_init(&q->sem_items, 0, capacity);
    rtos_sem_init(&q->sem_spaces, capacity, capacity);
    rtos_mutex_init(&q->lock);

    return q;
}

void rtos_queue_destroy(rtos_queue_t *q) {
    if (q) {
        if (q->buffer) kfree(q->buffer);
        kfree(q);
    }
}

bool rtos_queue_send(rtos_queue_t *q, const void *item, uint32_t timeout_ms) {
    if (!q || !item) return false;

    if (!rtos_sem_wait(&q->sem_spaces, timeout_ms)) {
        return false;
    }

    rtos_mutex_lock(&q->lock, 0xFFFFFFFF);

    memcpy(q->buffer + (q->tail * q->item_size), item, q->item_size);
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;

    rtos_mutex_unlock(&q->lock);
    rtos_sem_signal(&q->sem_items);

    return true;
}

bool rtos_queue_receive(rtos_queue_t *q, void *item, uint32_t timeout_ms) {
    if (!q || !item) return false;

    if (!rtos_sem_wait(&q->sem_items, timeout_ms)) {
        return false;
    }

    rtos_mutex_lock(&q->lock, 0xFFFFFFFF);

    memcpy(item, q->buffer + (q->head * q->item_size), q->item_size);
    q->head = (q->head + 1) % q->capacity;
    q->count--;

    rtos_mutex_unlock(&q->lock);
    rtos_sem_signal(&q->sem_spaces);

    return true;
}

size_t rtos_queue_count(rtos_queue_t *q) {
    return q ? q->count : 0;
}

/* Event Flags */
void rtos_event_init(rtos_event_t *ev) {
    ev->flags = 0;
    ev->wait_head = NULL;
}

uint32_t rtos_event_wait(rtos_event_t *ev, uint32_t mask, bool clear_on_exit, uint32_t timeout_ms) {
    UNUSED(timeout_ms);
    uint32_t flags = irq_save();

    while ((ev->flags & mask) == 0) {
        task_t *cur = rtos_current_task();
        if (!cur) break;
        cur->wait_object = ev;
        cur->state = TASK_STATE_BLOCKED;
        irq_restore(flags);
        rtos_reschedule();
        flags = irq_save();
    }

    uint32_t res = ev->flags & mask;
    if (clear_on_exit) {
        ev->flags &= ~mask;
    }

    irq_restore(flags);
    return res;
}

void rtos_event_set(rtos_event_t *ev, uint32_t mask) {
    uint32_t flags = irq_save();
    ev->flags |= mask;

    size_t count = rtos_get_task_count();
    for (size_t i = 0; i < count; i++) {
        task_t *t = rtos_get_task_by_index(i);
        if (t && t->state == TASK_STATE_BLOCKED && t->wait_object == ev) {
            t->state = TASK_STATE_READY;
            t->wait_object = NULL;
        }
    }

    irq_restore(flags);
    rtos_reschedule();
}

void rtos_event_clear(rtos_event_t *ev, uint32_t mask) {
    uint32_t flags = irq_save();
    ev->flags &= ~mask;
    irq_restore(flags);
}
