#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "http.h"

http_request_t *http_request_new() {
    http_request_t *req = (http_request_t*)malloc(sizeof(http_request_t));
    
    if (!req) return NULL;

    memset(req, 0, sizeof(http_request_t));
    req->content_length = -1;

    return req;
}

void http_request_reset(http_request_t *req) {
    if (!req) return;

    memset(req, 0, sizeof(http_request_t));
    req->content_length = -1;   /* -1 means "no Content-Length header seen" */
}

void http_request_destroy(http_request_t *req) {
    if (!req) return;

    free(req);
}


void http_request_header_add(http_request_t *req, const char *key, const char *value) {
    if (!req || !key || !value) return;
    
    if (req->headers_count == MAX_HEADERS) return;
    
    http_header_t *header = &req->headers[req->headers_count++];
    header->key = key;
    header->key_len = strlen(key);

    header->value = value;
    header->value_len = strlen(value);
}

http_header_t *http_request_header_get(http_request_t *req, char *key) {
    if (!req || !key) return NULL;
    
    http_header_t *headers = req->headers;

    for (size_t i = 0; i < req->headers_count; i++) {
        if (strncasecmp(key, headers[i].key, headers[i].key_len) == 0)
            return &headers[i];
    }
    return NULL;
}

void http_request_header_remove(http_request_t *req, char *key) {
    if (!req) return;

    http_header_t *headers = req->headers;

    size_t i;
    for (i = 0; i < req->headers_count; i++) 
        if (strcmp(key, headers[i].key) == 0)
            break; 

    if (i == req->headers_count) return;

    while (i < req->headers_count - 1) {
        headers[i] = headers[i+1];
        i++;
    }

    req->headers_count--;
}

void http_request_print(http_request_t *req) {
    printf("=== HTTP REQUEST ===\n");
	printf("METHOD: %s\n", req->method);
	printf("PATH: %s\n", req->path);
	printf("VERSION: HTTP/%d.%d\n", req->http_major, req->http_minor);
	printf("HEADERS: %ld\n", req->headers_count);

	http_header_t *header;
	for(size_t i = 0; i < req->headers_count; i++) {
		header = &(req->headers[i]);
		printf("{[%.*s]: [%.*s]}\n", (int)header->key_len, header->key, (int)header->value_len, header->value);
	}

	printf("---\n");
}
