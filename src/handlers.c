#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#include "worker.h"

// accept a new client on the main listen socket
// and add it to epoll interests lists to listen for
// read write events
void handle_new_client(worker_t *worker) {
    DEBUG_PRINTF("NEW CLIENT\n");

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
        
        // add to epoll
        struct epoll_event event = {
            .data.ptr = conn,
            .events = EPOLLIN | EPOLLRDHUP | EPOLLERR
        };

        epoll_ctl(worker->epfd, EPOLL_CTL_ADD, client_fd, &event);
    }
}


void handle_hangup(worker_t *worker, conn_t *conn) {
    close(conn->client_fd);
    conn_reset(conn);
    queue_enqueue(worker->free_pool, conn);
}

void handle_input(worker_t *worker, conn_t *conn) {
    DEBUG_PRINTF("INCOMING DATA ON CLIENT %d\n", conn->client_fd);
    
    int n = recv(conn->client_fd, conn->rbuf + conn->rbuf_len, sizeof(conn->rbuf) -  conn->rbuf_len, 0);
    
    if (n == -1)
        return;
    
    conn->rbuf_len += n;
    int result = http_parse(&conn->req, conn->rbuf, conn->rbuf_len);
    
    conn->keep_alive = false;

    if (conn->req.http_minor == 1) 
        conn->keep_alive = true;
    
    http_header_t *cn_hdr = http_request_header_get(&conn->req, "Connection");
    if (cn_hdr) {
        // if close matches, 0 returns, means false. No match means non zero -> true
        conn->keep_alive = (bool)strncasecmp(cn_hdr->value, "close", cn_hdr->value_len);
    }

    if (result == COMPLETE) {
        http_response_t *resp = http_response_hello_world();
        conn->wbuf_len = http_response_to_str(resp, conn->wbuf, 4096);
        free(resp);

        int n = send(conn->client_fd, conn->wbuf, conn->wbuf_len, 0);
        
        if (n == -1) {
            // write did not succeed, need to write again when socket is ready
            // so modify the epoll to add EPOLLOUT
            struct epoll_event ev;
            ev.events = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLRDHUP;
            epoll_ctl(worker->epfd, EPOLL_CTL_MOD, conn->client_fd, &ev);
        }
        else if(n == conn->wbuf_len) {
            conn->wbuf_sent += n;
            if (conn->keep_alive) {
                // reset connection for next request
                conn_reset_buf(conn);
            } else {
                handle_hangup(worker, conn);
            }
        }
    }
    else if(result == INCOMPLETE) {
        DEBUG_PRINTF("INCOMPLETE REQUEST, WAITING FOR MORE\n");
    }
    else if(result == INVALID) {
        http_response_t *bad_request = http_response_400();
        conn->wbuf_len = http_response_to_str(bad_request, conn->wbuf, 4096);
        send(conn->client_fd, conn->wbuf, conn->wbuf_len, 0);
        free(bad_request);
    }
}

void handle_write(worker_t *worker, conn_t *conn) {
    int n = send(conn->client_fd, conn->wbuf + conn->wbuf_sent, conn->wbuf_len - conn->wbuf_sent, 0);

    if (n != -1) {
        conn->wbuf_sent += n;
        
        if (conn->wbuf_sent == conn->wbuf_len) {
            if (conn->keep_alive) {
                // reset connection for next request
                conn_reset_buf(conn);
            } else {
                handle_hangup(worker, conn);
            }
        }
    } else {
        return;
    }
}