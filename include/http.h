/** 
 * This header contains definitions of core HTTP data structures
 * and function declarations
 */

#ifndef _HTTP_H_
#define _HTTP_H_

#define PATH_MAX_LEN    2048
#define MAX_HEADERS     16

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>   /* ssize_t */

#include "common.h"
#include "http_parser.h"

typedef struct {
    const char  *key;
    int         key_len;
    const char  *value;
    int         value_len;
} http_header_t;

typedef struct _http_request_t {
	char 	method[8];
	size_t 	method_len;

	char	path[PATH_MAX_LEN];
	size_t 	path_len;

	uint8_t	http_major;
	uint8_t http_minor;
	
    http_header_t   headers[MAX_HEADERS];
    size_t          headers_count;

    /* Body — zero-copy pointer into the connection's read buffer.
     * content_length == -1  →  no Content-Length header was sent.
     * content_length >= 0   →  declared body size in bytes.
     * body is set once the blank line has been seen; it may point to
     * data that has not fully arrived yet (check body_received). */
    const char     *body;
    ssize_t         content_length;   /* -1 = absent */
    size_t          body_received;    /* bytes received so far        */

	http_parser_t parser;
} http_request_t;

// request lifecycle functions
http_request_t* http_request_new();
void            http_request_reset(http_request_t *req);
void            http_request_destroy(http_request_t *req);

// header functions
void            http_request_header_add(http_request_t *req, const char *key, const char *value);
http_header_t*  http_request_header_get(http_request_t *req, char *key);
void 			http_request_header_remove(http_request_t *req, char *key);
void    		http_request_print(http_request_t *req);


typedef struct _http_response_t {
	int		status_code;

	bool	has_file;
	int		fd;

	// common headers
	char	*content_type;
	size_t	content_length;

	char	*body;
	size_t	body_len;

	http_header_t	headers[MAX_HEADERS];
	size_t			headers_count;
} http_response_t;

// response lifecycle
http_response_t	*http_response_new();
void			http_response_destroy(http_response_t *res);

// response field manipulation
void	http_response_set_status_code(http_response_t *res, int status_code);
void	http_response_set_file(http_response_t *res, int fd);
void	http_response_set_content_type(http_response_t *res, char *content_type);
void	http_response_set_body(http_response_t *res, char *body, int len);
void	http_response_header_add(http_response_t *res, const char *key, const char *value);
void	http_response_header_remove(http_response_t *res, char *key);
void	http_response_print(http_response_t *res);
size_t	http_response_to_str(http_response_t *res, char *buf, size_t len);

/* Common HTTP responses */
http_response_t *http_response_hello_world();
http_response_t *http_response_200();
http_response_t *http_response_400();
http_response_t *http_response_401();
http_response_t *http_response_403();
http_response_t *http_response_404();
http_response_t *http_response_500();

#endif