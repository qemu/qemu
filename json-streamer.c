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

#define MAX_TOKEN_SIZE (64ULL << 20)
#define MAX_NESTING (1ULL << 10)

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

    parser->token_size += token->length;

    qlist_append(parser->tokens, dict);

    if (type == JSON_ERROR) {
        goto out_emit_bad;
    } else if (parser->brace_count < 0 ||
        parser->bracket_count < 0 ||
        (parser->brace_count == 0 &&
         parser->bracket_count == 0)) {
        goto out_emit;
    } else if (parser->token_size > MAX_TOKEN_SIZE ||
               parser->bracket_count > MAX_NESTING ||
               parser->brace_count > MAX_NESTING) {
        /* Security consideration, we limit total memory allocated per object
         * and the maximum recursion depth that a message can force.
         */
        goto out_emit;
    }

    return;

out_emit_bad:
    /* clear out token list and tell the parser to emit and error
     * indication by passing it a NULL list
     */
    QDECREF(parser->tokens);
    parser->tokens = NULL;
out_emit:
    /* send current list of tokens to parser and reset tokenizer */
    parser->brace_count = 0;
    parser->bracket_count = 0;
    parser->emit(parser, parser->tokens);
    if (parser->tokens) {
        QDECREF(parser->tokens);
    }
    parser->tokens = qlist_new();
    parser->token_size = 0;
}

void json_message_parser_init(JSONMessageParser *parser,
                              void (*func)(JSONMessageParser *, QList *))
{
    parser->emit = func;
    parser->brace_count = 0;
    parser->bracket_count = 0;
    parser->tokens = qlist_new();
    parser->token_size = 0;

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
