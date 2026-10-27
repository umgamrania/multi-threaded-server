/**
 * This header contains definitions for workers, 
 * which run on seperate threads to process requests
 */

#ifndef _WORKER_H_
#define _WORKER_H_

#include <pthread.h>

#include "connection.h"
#include "queue.h"

typedef struct _worker_t {
    int     epfd;
    int     sockfd;

    conn_t      *conn_pool;
    queue_t     *free_pool;

    pthread_t   thread;
} worker_t;

worker_t    *worker_new();
void        worker_init(worker_t *worker);
void        worker_run(worker_t *worker);
void        worker_destroy(worker_t *worker);

void        http_response_send(worker_t *worker, http_response_t *res, conn_t *conn);

#endif