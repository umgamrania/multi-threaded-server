#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include "common.h"
#include "worker.h"

void *create_worker() {
    printf("Starting worker...\n");
    worker_t *worker = worker_new();
    worker_init(worker);
    worker_run(worker);

    return NULL;
}

int main() {
    pthread_t thread;
    pthread_create(&thread, NULL, &create_worker, NULL);
    
    pthread_t thread2;
    pthread_create(&thread2, NULL, &create_worker, NULL);

    pthread_join(thread, NULL);
    pthread_join(thread2, NULL);

    return 0;
}