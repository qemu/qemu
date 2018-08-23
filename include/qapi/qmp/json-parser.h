/*
 * JSON Parser
 *
 * Copyright IBM, Corp. 2009
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef QAPI_QMP_JSON_PARSER_H
#define QAPI_QMP_JSON_PARSER_H

typedef struct JSONLexer {
    int start_state, state;
    GString *token;
    int x, y;
} JSONLexer;

typedef struct JSONMessageParser {
    void (*emit)(void *opaque, QObject *json, Error *err);
    void *opaque;
    va_list *ap;
    JSONLexer lexer;
    int brace_count;
    int bracket_count;
    GQueue tokens;
    uint64_t token_size;
} JSONMessageParser;

void json_message_parser_init(JSONMessageParser *parser,
                              void (*emit)(void *opaque, QObject *json,
                                           Error *err),
                              void *opaque, va_list *ap);

void json_message_parser_feed(JSONMessageParser *parser,
                             const char *buffer, size_t size);

void json_message_parser_flush(JSONMessageParser *parser);

void json_message_parser_destroy(JSONMessageParser *parser);

#endif
