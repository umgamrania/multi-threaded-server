#ifndef _HTTP_PARSER_H_
#define _HTTP_PARSER_H_

#include <stddef.h>

typedef struct _http_request_t http_request_t;

typedef enum {
	STATE_METHOD,
	STATE_PATH,
	STATE_HTTP_H,
	STATE_HTTP_T1,
	STATE_HTTP_T2,
	STATE_HTTP_P,
	STATE_HTTP_SLASH,
	STATE_HTTP_MINOR,
	STATE_HTTP_DOT,
	STATE_HTTP_MAJOR,
	STATE_REQ_LINE_CR,
	STATE_REQ_LINE_LF,
	STATE_HEADER_START,
	STATE_HEADER_NAME,
	STATE_HEADER_VALUE,
	STATE_HEADER_VALUE_SP,
	STATE_HEADER_VALUE_CR,
	STATE_HEADER_CR,
	STATE_BODY,
	STATE_DONE
} http_parser_state;

typedef enum {
	INCOMPLETE,
	COMPLETE,
	INVALID,
	INVALID_METHOD,
	INVALID_PATH,
	INVALID_VERSION,
	INVALID_HEADER,
	INVALID_LENGTH_REQ
} http_parse_result_t;

typedef struct {
	http_parser_state 	state;
	size_t				pos;
	size_t				body_offset;  /* offset into rbuf where body begins */
} http_parser_t;

http_parse_result_t	http_parse(http_request_t *req, const char *buf, size_t buf_len);

#endif