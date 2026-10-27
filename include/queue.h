/**
 * This header contains declarations for a standard 
 * circular queue implementation
 */

#ifndef _QUEUE_H_
#define _QUEUE_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct _queue_t {
    void    **arr;
    int64_t head;
    int64_t tail;
    int64_t capacity;
} queue_t;

/* Queue lifecycle */
queue_t *queue_new(int capacity);
void    queue_destroy(queue_t *queue);

/* Queue manipulation */
int     queue_enqueue(queue_t *queue, void *data);
void    *queue_dequeue(queue_t *queue);
bool    queue_empty(queue_t *queue);

#endif