#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "worker.h"


int main() {
    worker_t *worker = worker_new();
    worker_init(worker);

    worker_run(worker);    
    return 0;
}