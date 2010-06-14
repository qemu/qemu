/*
 * JSON streaming support
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

#include "qlist.h"
#include "qint.h"
#include "qdict.h"
#include "qemu-common.h"
#include "json-lexer.h"
#include "json-streamer.h"

static void json_message_process_token(JSONLexer *lexer, QString *token, JSONTokenType type, int x, int y)
{
    JSONMessageParser *parser = container_of(lexer, JSONMessageParser, lexer);
    QDict *dict;

    if (type == JSON_OPERATOR) {
        switch (qstring_get_str(token)[0]) {
        case '{':
            parser->brace_count++;
            break;
        case '}':
            parser->brace_count--;
            break;
        case '[':
            parser->bracket_count++;
            break;
        case ']':
            parser->bracket_count--;
            break;
        default:
            break;
        }
    }

    dict = qdict_new();
    qdict_put(dict, "type", qint_from_int(type));
    QINCREF(token);
    qdict_put(dict, "token", token);
    qdict_put(dict, "x", qint_from_int(x));
    qdict_put(dict, "y", qint_from_int(y));

    qlist_append(parser->tokens, dict);

    if (parser->brace_count == 0 &&
        parser->bracket_count == 0) {
        parser->emit(parser, parser->tokens);
        QDECREF(parser->tokens);
        parser->tokens = qlist_new();
    }
}

void json_message_parser_init(JSONMessageParser *parser,
                              void (*func)(JSONMessageParser *, QList *))
{
    parser->emit = func;
    parser->brace_count = 0;
    parser->bracket_count = 0;
    parser->tokens = qlist_new();

    json_lexer_init(&parser->lexer, json_message_process_token);
}

int json_message_parser_feed(JSONMessageParser *parser,
                             const char *buffer, size_t size)
{
    return json_lexer_feed(&parser->lexer, buffer, size);
}

int json_message_parser_flush(JSONMessageParser *parser)
{
    return json_lexer_flush(&parser->lexer);
}

void json_message_parser_destroy(JSONMessageParser *parser)
{
    json_lexer_destroy(&parser->lexer);
    QDECREF(parser->tokens);
}
