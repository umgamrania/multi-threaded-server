#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "util.h"
#include "http.h"

static const char *http_status_text(int status_code) {
    switch (status_code) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 419: return "Length Required";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default:  return "Unknown";
    }
}

http_response_t *http_response_new() {
    http_response_t *res = (http_response_t*)malloc(sizeof(http_response_t));
    res->has_file = false;
    res->headers_count = 0;
    res->content_length = 0;
    res->body_len = 0;
    return res;
}

void http_response_destroy(http_response_t *res) {
    if (!res) return;

    free(res);
}

void http_response_set_status_code(http_response_t *res, int status_code) {
    if (!res) return;

    res->status_code = status_code;
}

void http_response_set_file(http_response_t *res, int fd) {
    if (!res) return;

    res->has_file = true;
    res->file_fd = fd;
    res->file_size = get_file_size(fd);
}

void http_response_set_content_type(http_response_t *res, char *content_type) {
    if (!res) return;

    res->content_type = content_type;
}

void http_response_set_body(http_response_t *res, char *body, int len) {
    if (!res) return;

    if (len == -1) len = strlen(body);

    res->has_file = false;
    res->body = body;
    res->body_len = len;
}

void http_response_header_add(http_response_t *res, const char *key, const char *value) {
    if (!res || !key || !value) return;
    if (res->headers_count >= MAX_HEADERS) return;

    http_header_t *h = &res->headers[res->headers_count++];
    h->key      = key;
    h->key_len  = strlen(key);
    h->value    = value;
    h->value_len = strlen(value);
}

void http_response_header_remove(http_response_t *res, char *key) {
    if (!res || !key) return;

    size_t key_len = strlen(key);
    for (size_t i = 0; i < res->headers_count; i++) {
        if (res->headers[i].key_len == (int)key_len &&
            strncasecmp(res->headers[i].key, key, key_len) == 0) {
            /* Shift remaining headers left */
            for (size_t j = i; j < res->headers_count - 1; j++)
                res->headers[j] = res->headers[j + 1];
            res->headers_count--;
            return;
        }
    }
}

void http_response_print(http_response_t *res) {
    if (!res) return;

    printf("HTTP/1.1 %d %s\r\n", res->status_code, http_status_text(res->status_code));

    if (res->content_type)
        printf("Content-Type: %s\r\n", res->content_type);

    if (!res->has_file && res->body)
        printf("Content-Length: %zu\r\n", res->body_len);

    for (size_t i = 0; i < res->headers_count; i++)
        printf("%s: %s\r\n", res->headers[i].key, res->headers[i].value);

    printf("\r\n");

    if (!res->has_file && res->body)
        printf("%.*s", (int)res->body_len, res->body);
}

/*
 * Write helpers — all inline, zero format-string overhead.
 *
 * WR_LIT : copy a string literal (length known at compile time)
 * WR_MEM : copy a runtime buffer of known byte length
 * WR_UINT: write a size_t as decimal digits
 *
 * On truncation every macro jumps to `done` via UNLIKELY guard.
 */
#define WR_LIT(s) \
    do { \
        const size_t _n = sizeof(s) - 1; \
        if (UNLIKELY(p + _n > end)) goto done; \
        memcpy(p, s, _n); p += _n; \
    } while (0)

#define WR_MEM(ptr, n) \
    do { \
        if (UNLIKELY(p + (n) > end)) goto done; \
        memcpy(p, ptr, n); p += (n); \
    } while (0)

#define WR_UINT(v) \
    do { \
        char _tmp[20]; int _i = 0; size_t _v = (v); \
        if (_v == 0) { _tmp[_i++] = '0'; } \
        else { while (_v) { _tmp[_i++] = (char)('0' + _v % 10); _v /= 10; } } \
        if (UNLIKELY(p + _i > end)) goto done; \
        for (int _j = _i - 1; _j >= 0; _j--) *p++ = _tmp[_j]; \
    } while (0)

size_t http_response_to_str(http_response_t *res, char *buf, size_t len) {
    if (!res || !buf || len == 0) return 0;

    char       *p   = buf;
    const char *end = buf + len;

    /* Status line: "HTTP/1.1 XXX Reason Phrase\r\n"
     * Status code is always 3 digits — write each digit directly. */
    WR_LIT("HTTP/1.1 ");
    if (UNLIKELY(p + 3 > end)) goto done;
    int sc = res->status_code;
    *p++ = (char)('0' + sc / 100);
    *p++ = (char)('0' + sc / 10 % 10);
    *p++ = (char)('0' + sc % 10);
    *p++ = ' ';
    const char *reason = http_status_text(sc);
    WR_MEM(reason, strlen(reason));
    WR_LIT("\r\n");

    /* Content-Type */
    if (res->content_type) {
        WR_LIT("Content-Type: ");
        WR_MEM(res->content_type, strlen(res->content_type));
        WR_LIT("\r\n");
    }

    WR_LIT("Content-Length: ");
    if (res->has_file) {
        WR_UINT(res->file_size);
    } else {
        WR_UINT(res->content_length);
    }
    WR_LIT("\r\n");

    /* Extra headers — use pre-stored key_len / value_len, no strlen */
    for (size_t i = 0; i < res->headers_count; i++) {
        const http_header_t *h = &res->headers[i];
        WR_MEM(h->key,   (size_t)h->key_len);
        WR_LIT(": ");
        WR_MEM(h->value, (size_t)h->value_len);
        WR_LIT("\r\n");
    }

    /* Header / body separator */
    WR_LIT("\r\n");

    /* Body — single memcpy, no snprintf */
    if (!res->has_file && res->body) {
        size_t to_copy = MIN(res->body_len, (size_t)(end - p));
        memcpy(p, res->body, to_copy);
        p += to_copy;
    }

done:
    return (size_t)(p - buf);
}

#undef WR_LIT
#undef WR_MEM
#undef WR_UINT

/* Common HTTP responses */

http_response_t *http_response_hello_world() {
    http_response_t *res = http_response_new();
    http_response_set_status_code(res, 200);
    http_response_header_add(res, "Server", "Simple-C-Server");
    http_response_header_add(res, "Connection", "Keep-Alive");
    http_response_set_body(res, "Hello world", -1);
    return res;
}

http_response_t *http_response_200() {
    http_response_t *res = http_response_new();
    http_response_set_status_code(res, 200);
    http_response_header_add(res, "Server", "Simple-C-Server");
    return res;
}

http_response_t *http_response_400() {
    http_response_t *res = http_response_new();
    http_response_set_status_code(res, 400);
    http_response_header_add(res, "Server", "Simple-C-Server");
    http_response_set_body(res, "Bad request!", -1);
    return res;
}

http_response_t *http_response_401() {
    http_response_t *res = http_response_new();
    http_response_set_status_code(res, 401);
    http_response_header_add(res, "Server", "Simple-C-Server");
    http_response_set_body(res, "Unauthorized!", -1);
    return res;
}

http_response_t *http_response_403() {
    http_response_t *res = http_response_new();
    http_response_set_status_code(res, 403);
    http_response_header_add(res, "Server", "Simple-C-Server");
    http_response_set_body(res, "Forbidden!", -1);
    return res;
}

http_response_t *http_response_404() {
    http_response_t *res = http_response_new();
    http_response_set_status_code(res, 404);
    http_response_header_add(res, "Server", "Simple-C-Server");
    http_response_set_body(res, "Not found!", -1);
    return res;
}

http_response_t *http_response_500() {
    http_response_t *res = http_response_new();
    http_response_set_status_code(res, 500);
    http_response_header_add(res, "Server", "Simple-C-Server");
    http_response_set_body(res, "Internal server error!", -1);
    return res;
}
