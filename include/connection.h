/**
 * This header contains definitions for the connection
 * mechanism which manages socket connections and related
 * fields for client-server communication
 */

#ifndef _HTTP_CONNECTION_H_
#define _HTTP_CONNECTION_H_

#include <stdbool.h>
#include <stdint.h>

#include "http.h"

typedef enum {
    CONN_READING,
    CONN_WRITING,
    CONN_COMPLETE
} conn_state_t;

typedef struct _conn_t {
    // Client socket file descriptor
    int client_fd;

    char    rbuf[4096];
    size_t  rbuf_len;

    char    wbuf[4096];
    int     wbuf_sent;
    int     wbuf_len;

    bool keep_alive;

    http_request_t  req;
    http_response_t resp;

    conn_state_t    state;
    int64_t         last_active;
} conn_t;

void conn_reset(conn_t *conn);
void conn_reset_buf(conn_t *conn);

#endif
