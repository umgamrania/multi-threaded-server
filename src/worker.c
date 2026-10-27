#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <sys/sendfile.h>

#include "worker.h"

#define CONN_POOL   256
#define PORT        8000

/* Various epoll related event handlers */

void handle_new_client(worker_t *worker);
void handle_input(worker_t *worker, conn_t *conn);
void handle_request(worker_t *worker, conn_t *conn);
void handle_invalid(http_request_t *req, http_response_t *res, int status_code);
void handle_write(worker_t *worker, conn_t *conn);
void handle_complete(worker_t *worker, conn_t *conn);
void handle_sendfile(worker_t *worker, conn_t *conn);
void handle_hangup(worker_t *worker, conn_t *conn);
int handle_static_file(http_request_t *req, http_response_t *resp);
static void handle_epoll_event(worker_t *worker, struct epoll_event event);

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
    setsockopt(fd, SOL_SOCKET, 15, &reuse, sizeof(reuse)); // SO_REUSEPORT

    bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(fd, 1024);

    return fd;
}

// Create new worker
worker_t *worker_new() {
    return (worker_t*)malloc(sizeof(worker_t));
}

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
    worker->sockfd = create_listen_socket(PORT);

    // register the listen socket on epoll to handle
    // incoming connection events
    
    struct epoll_event event = {
        .data.ptr = NULL,               // NULL represents the main listen socket
                                        // any other value would represent an existing socket connection
        .events = EPOLLIN | EPOLLRDHUP | EPOLLERR
    };

    epoll_ctl(worker->epfd, EPOLL_CTL_ADD, worker->sockfd, &event);
}


// Worker entry point which also contains the main event loop
void worker_run(worker_t *worker) {
    int n;
    struct epoll_event events[1024];

    while ((n = epoll_wait(worker->epfd, events, 1024, -1)) != -1) {
        for (int i = 0; i < n; i++)
            handle_epoll_event(worker, events[i]);
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


static void handle_epoll_event(worker_t *worker, struct epoll_event event) {
    // Hang up
    if (event.events & EPOLLRDHUP) {
        if (event.data.ptr == NULL)     // Main listen socket
            worker_destroy(worker);
        else
            handle_hangup(worker, (conn_t*)event.data.ptr);
    }

    // Input
    else if (event.events & EPOLLIN) {
        if (event.data.ptr == NULL)     // New client
            handle_new_client(worker);
        else                            // New data on existing client
            handle_input(worker, (conn_t*)event.data.ptr);
    }

    // Ready to write again
    else if (event.events & EPOLLOUT)
        handle_write(worker, (conn_t*)event.data.ptr);
}


/* Various epoll related event handlers */

// Accept all pending connections on the main listen socket
// and add them to epoll interests list
void handle_new_client(worker_t *worker) {
    // accept all pending connections
    for(;;) {
        int client_fd = accept(worker->sockfd, NULL, NULL);  // dont need client's address here now
        
        if (client_fd == -1)
            break;

        // acquire an empty connection from pool
        conn_t *conn = queue_dequeue(worker->free_pool);

        if (!conn) {
            DEBUG_PRINTF("OUT OF CONNECTIONS!\n");
            close(client_fd);
            return;
        }

        conn->client_fd = client_fd;

        // Disable Nagle's algorithm: without this, the kernel buffers the
        // small headers send() waiting for an ACK before flushing the
        // subsequent sendfile(), adding a ~40 ms delay per request on
        // keep-alive connections.
        int nodelay = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        // add to epoll
        struct epoll_event event = {
            .data.ptr = conn,
            .events = EPOLLIN | EPOLLRDHUP | EPOLLERR
        };

        epoll_ctl(worker->epfd, EPOLL_CTL_ADD, client_fd, &event);
    }
}

// Handle input on client
void handle_input(worker_t *worker, conn_t *conn) {
    int bytes_received = recv(conn->client_fd,
        conn->rbuf + conn->rbuf_len,                // store after existing data in rbuf
        sizeof(conn->rbuf) - conn->rbuf_len,        // remaining space in rbuf
        0                                           // flags
    );

    if (bytes_received == 0) {                      // connection closed
        handle_hangup(worker, conn);
        return;
    }

    if (bytes_received == -1)                       // recv error
        return;

    // read success

    conn->rbuf_len += bytes_received;

    // parse to see if complete request received
    http_parse_result_t result = http_parse(&conn->req, conn->rbuf, conn->rbuf_len);

    if (result == COMPLETE) {
        conn->state = CONN_PROCESS;
        handle_request(worker, conn);
    }

    else if (result == INCOMPLETE) {
        // wait for more data
        return;
    }

    else {
        // error parsing, invalid request
        int status_code = 400;

        // only specific error being handled for now
        if (result == INVALID_LENGTH_REQ) status_code = 419;

        handle_invalid(NULL, &conn->resp, status_code);
        conn->wbuf_len = http_response_to_str(&conn->resp, conn->wbuf, sizeof(conn->wbuf));
        handle_write(worker, conn);
    }
}

// check if path is valid, only checks for syntatic errors in path
int path_valid(char *path) {
    // malicious path, directory traversal
    if (strstr(path, ".."))
        return 0;

    if (strstr(path, "//"))
        return 0;

    return 1;
}

// Process the request, prepare response and send response
void handle_request(worker_t *worker, conn_t *conn) {
    // validate request

    http_request_t *req = &conn->req;
    http_response_t *resp = &conn->resp;
    
    // path field in http_request_t is a zero-copy ptr
    // null termination is required manually
    req->path[req->path_len] = 0;

    // check path
    if (!path_valid(req->path)) {
        handle_invalid(NULL, &conn->resp, 400);
        return;
    }

    // set proper value for keep alive
    // default is false for http 1.0
    resp->keep_alive = false;

    // default to true if http 1.1
    if (req->http_minor == 1)
        resp->keep_alive = true;

    // final decision is the request
    http_header_t *cn_hdr = http_request_header_get(req, "Connection");
    if (cn_hdr) {
        if (strncasecmp(cn_hdr->value, "Close", cn_hdr->value_len) == 0)
            resp->keep_alive = false;

        if (strncasecmp(cn_hdr->value, "Keep-Alive", cn_hdr->value_len) == 0)
            resp->keep_alive = true;
    }

    // check for static files
    int result = handle_static_file(req, resp);

    if (!result)
        handle_invalid(req, resp, 404);

    conn->wbuf_len = http_response_to_str(resp, conn->wbuf, sizeof(conn->wbuf));
    handle_write(worker, conn);
    return;
}

// Send response to invalid request
void handle_invalid(http_request_t *req, http_response_t *res, int status_code) {
    if (res->keep_alive)
        http_response_header_add(res, "Connection", "Keep-Alive");

    http_response_set_status_code(res, status_code);
}

// send the contents of wbuf to client
void handle_write(worker_t *worker, conn_t *conn) {
    if (conn->state == CONN_SEND_FILE) {
        handle_sendfile(worker, conn);
        return;
    }

    int sent_bytes = send(conn->client_fd, 
        conn->wbuf + conn->wbuf_sent,                   // send from where left
        conn->wbuf_len - conn->wbuf_sent,               // no. bytes to send
        0                                               // flags
    );

    if (sent_bytes == -1) {
        // send failed, need to try again
        // register epoll out
        struct epoll_event ev = {
            .data.ptr = conn,
            .events = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLRDHUP
        };
        epoll_ctl(worker->epfd, EPOLL_CTL_MOD, conn->client_fd, &ev);
        return;
    }

    // send success
    conn->wbuf_sent += sent_bytes;

    if (conn->wbuf_sent == conn->wbuf_len) {
        // send complete

        // send file if any
        if (conn->resp.has_file) {
            conn->state = CONN_SEND_FILE;
            handle_sendfile(worker, conn);
            return;
        }

    }
    // otherwise we done here
    handle_complete(worker, conn);
}

void handle_hangup(worker_t *worker, conn_t *conn) {
    close(conn->client_fd);
    if (conn->resp.file_fd > 0)
        close(conn->resp.file_fd);
    conn_reset(conn);
    queue_enqueue(worker->free_pool, conn);
}

// wrap up after completing a request
// decides whether to keep connection alive or dispose it
void handle_complete(worker_t *worker, conn_t *conn) {
    if (conn->resp.keep_alive) {
        conn_reset_buf(conn);

        // Re-arm epoll to EPOLLIN only. If we went through a sendfile
        // retry, EPOLLOUT was added to the interest set and must be
        // removed; otherwise handle_write is called spuriously with
        // wbuf_len==0 which triggers handle_complete→handle_hangup,
        // silently closing the kept-alive connection.
        struct epoll_event ev = {
            .data.ptr = conn,
            .events = EPOLLIN | EPOLLRDHUP | EPOLLERR
        };
        epoll_ctl(worker->epfd, EPOLL_CTL_MOD, conn->client_fd, &ev);
        return;
    }

    handle_hangup(worker, conn);
}

// Send response file using sendfile syscall
void handle_sendfile(worker_t *worker, conn_t *conn) {
    http_response_t *resp = &conn->resp;

    long offset = resp->file_sent;
    
    int sent_bytes = sendfile(conn->client_fd,
        resp->file_fd,
        (off_t*)&offset,
        resp->file_size - resp->file_sent
    );

    resp->file_sent += sent_bytes;

    if (resp->file_sent == resp->file_size) {
        // done
        handle_complete(worker, conn);
        return;
    }

    // register epoll out
    struct epoll_event ev = {
        .events = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLRDHUP
    };
    epoll_ctl(worker->epfd, EPOLL_CTL_MOD, conn->client_fd, &ev);
}

int handle_static_file(http_request_t *req, http_response_t *resp) {
    int file_fd = open(req->path + 1, O_RDONLY);
    if (file_fd == -1) {
        return 0;
    }

    http_response_set_status_code(resp, 200);
    http_response_set_file(resp, file_fd);
    if (resp->keep_alive)
        http_response_header_add(resp, "Connection", "Keep-Alive");

    return 1;
}