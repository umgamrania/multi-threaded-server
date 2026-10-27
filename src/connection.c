#include <unistd.h>
#include <string.h>

#include "connection.h"
#include "http.h"

void conn_reset(conn_t *conn) {
    if (!conn) return;
    
    memset(conn, 0, sizeof(conn_t));
    conn->resp.file_fd = -1;  // 0 is a valid fd (stdin); use -1 as sentinel
}

void conn_reset_buf(conn_t *conn) {
    if (!conn) return;

    conn->rbuf_len  = 0;
    conn->wbuf_len  = 0;
    conn->wbuf_sent = 0;
    conn->state     = CONN_READING;

    // close any open file fd from the previous response
    if (conn->resp.file_fd > 0)
        close(conn->resp.file_fd);

    // reset response (but preserve client_fd which lives outside resp)
    memset(&conn->resp, 0, sizeof(conn->resp));
    conn->resp.file_fd = -1;

    http_request_reset(&conn->req);   // reset parser state for next request
}