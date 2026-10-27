#include <string.h>

#include "connection.h"
#include "http.h"

void conn_reset(conn_t *conn) {
    if (!conn) return;
    
    memset(conn, 0, sizeof(conn_t));
}

void conn_reset_buf(conn_t *conn) {
    if (!conn) return;

    conn->rbuf_len  = 0;
    conn->wbuf_len  = 0;
    conn->wbuf_sent = 0;
    http_request_reset(&conn->req);   // reset parser state for next request
}