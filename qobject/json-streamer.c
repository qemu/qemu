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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "json-parser-int.h"

#define MAX_TOKEN_SIZE (64ULL << 20)
#define MAX_TOKEN_COUNT (2ULL << 20)
#define MAX_NESTING (1 << 10)

static void json_message_free_tokens(JSONMessageParser *parser)
{
    JSONToken *token;

    while ((token = g_queue_pop_head(&parser->tokens))) {
        g_free(token);
    }
}

void json_message_process_token(JSONLexer *lexer, GString *input,
                                JSONTokenType type, int x, int y)
{
    JSONMessageParser *parser = container_of(lexer, JSONMessageParser, lexer);
    QObject *json = NULL;
    Error *err = NULL;
    JSONToken *token;

    switch (type) {
    case JSON_LCURLY:
        parser->brace_count++;
        break;
    case JSON_RCURLY:
        parser->brace_count--;
        break;
    case JSON_LSQUARE:
        parser->bracket_count++;
        break;
    case JSON_RSQUARE:
        parser->bracket_count--;
        break;
    case JSON_ERROR:
        error_setg(&err, "JSON parse error, stray '%s'", input->str);
        goto out_emit;
    case JSON_END_OF_INPUT:
        if (g_queue_is_empty(&parser->tokens)) {
            return;
        }
        json = json_parser_parse(&parser->tokens, parser->ap, &err);
        goto out_emit;
    default:
        break;
    }

    /*
     * Security consideration, we limit total memory allocated per object
     * and the maximum recursion depth that a message can force.
     */
    if (parser->token_size + input->len + 1 > MAX_TOKEN_SIZE) {
        error_setg(&err, "JSON token size limit exceeded");
        goto out_emit;
    }
    if (g_queue_get_length(&parser->tokens) + 1 > MAX_TOKEN_COUNT) {
        error_setg(&err, "JSON token count limit exceeded");
        goto out_emit;
    }
    if (parser->bracket_count + parser->brace_count > MAX_NESTING) {
        error_setg(&err, "JSON nesting depth limit exceeded");
        goto out_emit;
    }

    token = json_token(type, x, y, input);
    parser->token_size += input->len;

    g_queue_push_tail(&parser->tokens, token);

    if ((parser->brace_count > 0 || parser->bracket_count > 0)
        && parser->bracket_count >= 0 && parser->bracket_count >= 0) {
        return;
    }

    json = json_parser_parse(&parser->tokens, parser->ap, &err);

out_emit:
    parser->brace_count = 0;
    parser->bracket_count = 0;
    json_message_free_tokens(parser);
    parser->token_size = 0;
    parser->emit(parser->opaque, json, err);
}

void json_message_parser_init(JSONMessageParser *parser,
                              void (*emit)(void *opaque, QObject *json,
                                           Error *err),
                              void *opaque, va_list *ap)
{
    parser->emit = emit;
    parser->opaque = opaque;
    parser->ap = ap;
    parser->brace_count = 0;
    parser->bracket_count = 0;
    g_queue_init(&parser->tokens);
    parser->token_size = 0;

    json_lexer_init(&parser->lexer, !!ap);
}

void json_message_parser_feed(JSONMessageParser *parser,
                             const char *buffer, size_t size)
{
    json_lexer_feed(&parser->lexer, buffer, size);
}

void json_message_parser_flush(JSONMessageParser *parser)
{
    json_lexer_flush(&parser->lexer);
    assert(g_queue_is_empty(&parser->tokens));
}

void json_message_parser_destroy(JSONMessageParser *parser)
{
    json_lexer_destroy(&parser->lexer);
    json_message_free_tokens(parser);
}
