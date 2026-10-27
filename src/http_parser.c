#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "http.h"

static const unsigned char token_char_table[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //   0- 15  ................
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //  16- 31  ................
    0, 1, 0, 1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, //  32- 47   !"#$%&'()*+,-./
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, //  48- 63  0123456789:;<=>?
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //  64- 79  @ABCDEFGHIJKLMNO
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, //  80- 95  PQRSTUVWXYZ[\]^_
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //  96-111  `abcdefghijklmno
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, // 112-127  pqrstuvwxyz{|}~.
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 128-143  ................
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 144-159  ................
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 160-175  ................
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 176-191  ................
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 192-207  ................
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 208-223  ................
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 224-239  ................
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 240-255  ................
};

static inline int is_token_char(char c) {
    /* RFC 7230 'token' chars used for header field-names. */
    return token_char_table[(unsigned char)c];
}

http_parse_result_t http_parse(http_request_t *req, const char *buf, size_t buf_len) {
	if (req->parser.state == STATE_DONE)
		return COMPLETE;

	size_t i = req->parser.pos;

	size_t mlen = req->method_len;
	size_t plen = req->path_len;
	http_header_t *tmp_hdr;

	for(;;) {
		if (i >= buf_len || buf[i] == '\0') {
			req->parser.pos = i;
			
			req->method_len = mlen;
			req->path_len = plen;
			return INCOMPLETE;
		}

		char c = buf[i];
		DEBUG_PRINTF("TOKEN: %c (%d)\n", c, c);

		switch(req->parser.state) {

			case STATE_METHOD:
				DEBUG_PRINTF("STATE_METHOD\n");
				if (c == ' ') {
					if (UNLIKELY(mlen == 0)) return INVALID_METHOD;
					req->parser.state = STATE_PATH;
				} 
				else if (isupper(c) && mlen < sizeof(req->method) - 1) {
					req->method[mlen++] = c;
					req->method[mlen] = '\0';
				} 
				else {
					return INVALID_METHOD;
				}
				break;
			
			case STATE_PATH: {
				DEBUG_PRINTF("STATE_PATH\n");

				const char *sp = memchr(buf + i, ' ', buf_len - i);
				size_t chunk_len = sp ? (size_t)(sp - (buf + i)) : (buf_len - i);

				// reject \r or \n anywhere in this chunk
				const char *bad = memchr(buf + i, '\r', chunk_len);
				if (!bad) bad = memchr(buf + i, '\n', chunk_len);

				if (UNLIKELY(bad)) return INVALID_PATH;

				if (UNLIKELY(plen + chunk_len >= sizeof(req->path))) return INVALID_PATH;

				memcpy(req->path + plen, buf + i, chunk_len);
				plen += chunk_len;
				req->path[plen] = '\0';

				if (sp == NULL) {
					// no space yet, whole buffer consumed, normal, not hinted
					i = buf_len;
					req->parser.pos = i;
					req->method_len = mlen;
					req->path_len = plen;
					return INCOMPLETE;
				}

				// found the space
				if (UNLIKELY(plen == 0 || req->path[0] != '/')) return INVALID_PATH;
				i = sp - buf; // position of the space itself
				req->parser.state = STATE_HTTP_H;
				req->path_len = plen;
				i++; // consume the space
				continue;
			}

			case STATE_HTTP_H:
				DEBUG_PRINTF("STATE_HTTP_H\n");
				if (UNLIKELY(c != 'H')) return INVALID_VERSION;
				req->parser.state = STATE_HTTP_T1;
				break;


			case STATE_HTTP_T1:
				DEBUG_PRINTF("STATE_HTTP_T1\n");
				if (UNLIKELY(c != 'T')) return INVALID_VERSION;
				req->parser.state = STATE_HTTP_T2;
				break;

			case STATE_HTTP_T2:
				DEBUG_PRINTF("STATE_HTTP_T2\n");
				if (UNLIKELY(c != 'T')) return INVALID_VERSION;
				req->parser.state = STATE_HTTP_P;
				break;

			case STATE_HTTP_P:
				DEBUG_PRINTF("STATE_HTTP_P\n");
				if (UNLIKELY(c != 'P')) return INVALID_VERSION;
				req->parser.state = STATE_HTTP_SLASH;
				break;

			case STATE_HTTP_SLASH:
				DEBUG_PRINTF("STATE_HTTP_SLASH\n");
				if (UNLIKELY(c != '/')) return INVALID_VERSION;
				req->parser.state = STATE_HTTP_MAJOR;
				break;
			
			case STATE_HTTP_MAJOR:
				DEBUG_PRINTF("STATE_HTTP_MAJOR\n");
				if (UNLIKELY(!isdigit(c))) return INVALID_VERSION;
				req->http_major = c - '0';
				req->parser.state = STATE_HTTP_DOT;
				break;

			case STATE_HTTP_DOT:
				DEBUG_PRINTF("STATE_HTTP_DOT\n");
				if (UNLIKELY(c != '.')) return INVALID_VERSION;
				req->parser.state = STATE_HTTP_MINOR;
				break;

			case STATE_HTTP_MINOR:
				DEBUG_PRINTF("STATE_HTTP_MINOR\n");
				if (UNLIKELY(!isdigit(c))) return INVALID_VERSION;
				req->http_minor = c - '0';
				req->parser.state = STATE_REQ_LINE_CR;
				break;

			case STATE_REQ_LINE_CR:
				DEBUG_PRINTF("STATE_REQ_LINE_CR\n");
				if (UNLIKELY(c != '\r')) return INVALID_VERSION;
				req->parser.state = STATE_REQ_LINE_LF;
				break;

			case STATE_REQ_LINE_LF:
				DEBUG_PRINTF("STATE_REQ_LINE_LF\n");
				if (UNLIKELY(c != '\n')) return INVALID_VERSION;
				req->parser.state = STATE_HEADER_START;
				break;

			case STATE_HEADER_START:
				DEBUG_PRINTF("STATE_HEADER_START\n");
				if (c == '\r') {
					req->parser.state = STATE_HEADER_CR;
				}
				else {
					// Add new header
					if (UNLIKELY(req->headers_count >= MAX_HEADERS)) return INVALID_HEADER;
					tmp_hdr = &(req->headers[req->headers_count]);

					tmp_hdr->key = buf + i;
					tmp_hdr->key_len = 1;

					req->parser.state = STATE_HEADER_NAME;
					continue;
				}
				break;

			case STATE_HEADER_NAME:
				DEBUG_PRINTF("STATE_HEADER_NAME\n");
				if (c == ':') {
					tmp_hdr = &(req->headers[req->headers_count]);
					tmp_hdr->key_len = (buf + i) - (tmp_hdr->key);
					DEBUG_PRINTF("HEADER KEY LENGTH %d\n", tmp_hdr->key_len);
					req->parser.state = STATE_HEADER_VALUE_SP;
				}
				else if(UNLIKELY(!is_token_char(c))) {
					return INVALID_HEADER;
				}

				break;

			case STATE_HEADER_VALUE_SP:
				DEBUG_PRINTF("STATE_HEADER_VALUE_SP\n");
				if (c == ' ' || c == '\t') {
					// ignore space
				}
				else if (UNLIKELY(c == '\r' || c == '\n')) {
					return INVALID_HEADER;
				}
				else {
					tmp_hdr = &(req->headers[req->headers_count]);
					tmp_hdr->value = buf + i;
					tmp_hdr->value_len = 1;
					req->parser.state = STATE_HEADER_VALUE;
				}
				break;

			case STATE_HEADER_VALUE: {
				DEBUG_PRINTF("STATE_HEADER_VALUE\n");

				size_t remaining = buf_len - i;
				const char *cr = memchr(buf + i, '\r', remaining);
				size_t chunk_len = cr ? (size_t)(cr - (buf + i)) : remaining;

				if (UNLIKELY(memchr(buf + i, '\n', chunk_len) != NULL)) return INVALID_HEADER;

				if (cr == NULL) {
					// no CR yet — normal for streamed reads, not hinted
					i = buf_len;
					req->parser.pos = i;
					req->method_len = mlen;
					req->path_len = plen;
					return INCOMPLETE;
				}

				i = (cr - buf) + 1; // move past the '\r'
				req->parser.state = STATE_HEADER_VALUE_CR;
				continue;
			}
		
			case STATE_HEADER_VALUE_CR:
				DEBUG_PRINTF("STATE_HEADER_VALUE_CR\n");
				if (UNLIKELY(c != '\n')) {
					return INVALID_HEADER;
				}
				tmp_hdr = &(req->headers[req->headers_count++]);
				tmp_hdr->value_len = (buf + i) - (tmp_hdr->value) - 1;
				DEBUG_PRINTF("HEADER VALUE LENGTH %d\n", tmp_hdr->value_len);
				req->parser.state = STATE_HEADER_START;
				break;

			case STATE_HEADER_CR:
				DEBUG_PRINTF("STATE_HEADER_CR\n");
				if (UNLIKELY(c != '\n'))
					return INVALID_HEADER;

				/* All headers parsed.  Determine whether a body is expected.
				 * We look up Content-Length now so the parser owns the decision
				 * and the caller doesn't have to repeat the header scan. */
				{
					http_header_t *cl_hdr = http_request_header_get(req, "Content-Length");
					if (cl_hdr) {
						/* Parse the value (must be a non-negative decimal integer). */
						char tmp[32];
						size_t vlen = (cl_hdr->value_len < 31) ? (size_t)cl_hdr->value_len : 31;
						memcpy(tmp, cl_hdr->value, vlen);
						tmp[vlen] = '\0';

						char *end;
						long long cl = strtoll(tmp, &end, 10);
						if (end == tmp || *end != '\0' || cl < 0)
							return INVALID_HEADER;

						req->content_length = (ssize_t)cl;
					}
					/* else: content_length stays -1 (no header present) */

					i++;   /* consume the '\n' */

					if (req->content_length > 0) {
						/* Point body at the first byte after the blank line. */
						req->body = buf + i;
						req->body_received = (size_t)(buf_len - i);
						req->parser.state = STATE_BODY;
						req->parser.pos = buf_len;   /* consumed everything we have */
						req->method_len = mlen;
						req->path_len   = plen;

						if (req->body_received >= (size_t)req->content_length) {
							req->parser.state = STATE_DONE;
							return COMPLETE;
						}
						return INCOMPLETE;
					}


                    if (i != buf_len) {
                        // parser is not at the end of buffer yet, and no content length
                        // header is present. Since body is present, content length IS required
                        return INVALID_LENGTH_REQ;
                    }

					/* content_length == 0 or == -1: no body to read. */
					req->parser.state = STATE_DONE;
					req->parser.pos   = i;
					req->method_len   = mlen;
					req->path_len     = plen;
					return COMPLETE;
				}

			case STATE_BODY:
				DEBUG_PRINTF("STATE_BODY\n");
				/* The body pointer was already set when we entered this state.
				 * Each call just accounts for newly arrived bytes. */
				req->body_received = (size_t)(buf_len - (size_t)(req->body - buf));
				req->parser.pos    = buf_len;
				req->method_len    = mlen;
				req->path_len      = plen;

				if (req->body_received >= (size_t)req->content_length) {
					req->parser.state = STATE_DONE;
					return COMPLETE;
				}
				return INCOMPLETE;

			case STATE_DONE:
				DEBUG_PRINTF("STATE_DONE\n");
				return COMPLETE;
		}

		i++;
	}
}

