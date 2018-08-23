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
#include "qemu-common.h"
#include "qapi/error.h"
#include "qapi/qmp/json-lexer.h"
#include "qapi/qmp/json-parser.h"
#include "qapi/qmp/json-streamer.h"

#define MAX_TOKEN_SIZE (64ULL << 20)
#define MAX_TOKEN_COUNT (2ULL << 20)
#define MAX_NESTING (1ULL << 10)

static void json_message_free_token(void *token, void *opaque)
{
    g_free(token);
}

static void json_message_free_tokens(JSONMessageParser *parser)
{
    if (parser->tokens) {
        g_queue_foreach(parser->tokens, json_message_free_token, NULL);
        g_queue_free(parser->tokens);
        parser->tokens = NULL;
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
    default:
        break;
    }

    token = g_malloc(sizeof(JSONToken) + input->len + 1);
    token->type = type;
    memcpy(token->str, input->str, input->len);
    token->str[input->len] = 0;
    token->x = x;
    token->y = y;

    parser->token_size += input->len;

    g_queue_push_tail(parser->tokens, token);

    if (parser->brace_count < 0 ||
        parser->bracket_count < 0 ||
        (parser->brace_count == 0 &&
         parser->bracket_count == 0)) {
        json = json_parser_parse(parser->tokens, parser->ap, &err);
        parser->tokens = NULL;
        goto out_emit;
    }

    /*
     * Security consideration, we limit total memory allocated per object
     * and the maximum recursion depth that a message can force.
     */
    if (parser->token_size > MAX_TOKEN_SIZE) {
        error_setg(&err, "JSON token size limit exceeded");
        goto out_emit;
    }
    if (g_queue_get_length(parser->tokens) > MAX_TOKEN_COUNT) {
        error_setg(&err, "JSON token count limit exceeded");
        goto out_emit;
    }
    if (parser->bracket_count + parser->brace_count > MAX_NESTING) {
        error_setg(&err, "JSON nesting depth limit exceeded");
        goto out_emit;
    }

    return;

out_emit:
    parser->brace_count = 0;
    parser->bracket_count = 0;
    json_message_free_tokens(parser);
    parser->tokens = g_queue_new();
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
    parser->tokens = g_queue_new();
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
}

void json_message_parser_destroy(JSONMessageParser *parser)
{
    json_lexer_destroy(&parser->lexer);
    json_message_free_tokens(parser);
}
