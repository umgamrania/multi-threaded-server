#include <stdlib.h>

#include "queue.h"

queue_t *queue_new(int capacity) {
    queue_t *q = (queue_t*)malloc(sizeof(queue_t));
    
    if (!q) return NULL;

    q->arr = (void**)calloc(capacity, sizeof(void*));

    if (!(q->arr)) {
        free(q);
        return NULL;
    }

    q->head = q->tail = -1;
    q->capacity = capacity;

    return q;
}

void queue_destroy(queue_t *queue) {
    free(queue->arr);
    free(queue);
}

int queue_enqueue(queue_t *queue, void *data) {
    if (!queue) return 0;

    int64_t new_tail = (queue->tail + 1) % queue->capacity;

    if (new_tail == queue->head) {
        // overflow
        return 0;
    }

    queue->tail = new_tail;
    queue->arr[queue->tail] = data;

    // first enqueue opr
    if (queue->head == -1) 
        queue->head++;

    return 1;
}

void *queue_dequeue(queue_t *queue) {
    if (!queue) return NULL;

    if (queue->head == -1) {
        // queue empty
        return NULL;
    }

    void *data = queue->arr[queue->head];


    if (queue->head == queue->tail) {
        // queue empty
        queue->head = queue->tail = -1;
    } else {
        queue->head = (queue->head + 1) % queue->capacity;
    }

    return data;
}

bool queue_empty(queue_t *queue) {
    return (queue->head == -1);
}
