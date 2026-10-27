#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <sys/epoll.h>

#include "worker.h"
#include "connection.h"

#define CONN_POOL   256
#define PORT        8000

// event loop event handlers defined in handlers.c
void handle_new_client(worker_t *worker);
void handle_input(worker_t *worker, conn_t *conn);
void handle_hangup(worker_t *worker, conn_t *conn);
void handle_write(worker_t *worker, conn_t *conn);

// Create pool_size connections and put them in the free_pool queue
static void init_worker_pool(worker_t *worker, int pool_size) {
    worker->free_pool = queue_new(pool_size);

    worker->conn_pool = calloc(pool_size, sizeof(conn_t));

    conn_t **tmp = (conn_t**)worker->free_pool->arr;
    conn_t *pool_start = worker->conn_pool;

    for (int i = 0; i < pool_size; i++) 
        tmp[i] = pool_start + i;
    
    worker->free_pool->head = 0;
    worker->free_pool->tail = pool_size - 1;
}

// Create a socket and bind it to INADDR_ANY on given port
// to listen for incoming connections
static int create_listen_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };

    // Set SO_REUSEADDR since multiple threads will be listening on 
    // the same port number
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(fd, 1024);

    return fd;
}

// Create new worker
worker_t *worker_new() {
    return (worker_t*)malloc(sizeof(worker_t));
}

// Initialize a worker
void worker_init(worker_t *worker) {
    init_worker_pool(worker, CONN_POOL);

    // create new epoll instance
    worker->epfd = epoll_create1(0);

    if (worker->epfd == -1) {
        DEBUG_PRINTF("CANNOT CREATE EPOLL\n");
        return;
    }

    // create and bind the listen socket
    // which will accept incoming client connections
    worker->sockfd = create_listen_socket(8000);

    // register the listen socket on epoll to handle
    // incoming connection events
    
    struct epoll_event event = {
        .data.ptr = NULL,               // NULL represents the main listen socket
                                        // any other value would represent an existing socket connection
        .events = EPOLLIN | EPOLLRDHUP | EPOLLERR
    };

    epoll_ctl(worker->epfd, EPOLL_CTL_ADD, worker->sockfd, &event);
}

void dispatch_epoll_event(worker_t *worker, struct epoll_event event) {
    if (event.events & EPOLLRDHUP) {
        // hang up
        
        if (UNLIKELY(event.data.ptr == NULL)) {
            // error in listen socket
            worker_destroy(worker);
        }
        else {
            handle_hangup(worker, (conn_t*)event.data.ptr);
        }
    }

    else if (event.events & EPOLLIN) {
        if (event.data.ptr == NULL) {
            // new client
            handle_new_client(worker);
        }

        else {
            // input on client
            handle_input(worker, (conn_t*)event.data.ptr);
        }
    }

    else if (event.events & EPOLLOUT) {
        // client ready for write
        handle_write(worker, (conn_t*)event.data.ptr);
    }
}

void worker_run(worker_t *worker) {
    struct epoll_event events[1024];
    int n;

    while ((n = epoll_wait(worker->epfd, events, 1024, -1)) != -1) {
        for (int i = 0; i < n; i++)
            dispatch_epoll_event(worker, events[i]);
    }
}

// Free all the memory occupied by a worker
void worker_destroy(worker_t *worker) {
    close(worker->epfd);
    close(worker->sockfd);
    free(worker->conn_pool);
    queue_destroy(worker->free_pool);
    free(worker);
}