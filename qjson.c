/*
 * QObject JSON integration
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

#include "json-lexer.h"
#include "json-parser.h"
#include "json-streamer.h"
#include "qjson.h"

typedef struct JSONParsingState
{
    JSONMessageParser parser;
    va_list *ap;
    QObject *result;
} JSONParsingState;

static void parse_json(JSONMessageParser *parser, QList *tokens)
{
    JSONParsingState *s = container_of(parser, JSONParsingState, parser);
    s->result = json_parser_parse(tokens, s->ap);
}

QObject *qobject_from_json(const char *string)
{
    JSONParsingState state = {};

    json_message_parser_init(&state.parser, parse_json);
    json_message_parser_feed(&state.parser, string, strlen(string));
    json_message_parser_flush(&state.parser);
    json_message_parser_destroy(&state.parser);

    return state.result;
}

QObject *qobject_from_jsonf(const char *string, ...)
{
    JSONParsingState state = {};
    va_list ap;

    va_start(ap, string);
    state.ap = &ap;

    json_message_parser_init(&state.parser, parse_json);
    json_message_parser_feed(&state.parser, string, strlen(string));
    json_message_parser_flush(&state.parser);
    json_message_parser_destroy(&state.parser);

    va_end(ap);

    return state.result;
}
