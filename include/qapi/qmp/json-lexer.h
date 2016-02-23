/*
 * JSON lexer
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

#ifndef QEMU_JSON_LEXER_H
#define QEMU_JSON_LEXER_H


typedef enum json_token_type {
    JSON_MIN = 100,
    JSON_LCURLY = JSON_MIN,
    JSON_RCURLY,
    JSON_LSQUARE,
    JSON_RSQUARE,
    JSON_COLON,
    JSON_COMMA,
    JSON_INTEGER,
    JSON_FLOAT,
    JSON_KEYWORD,
    JSON_STRING,
    JSON_ESCAPE,
    JSON_SKIP,
    JSON_ERROR,
} JSONTokenType;

typedef struct JSONLexer JSONLexer;

typedef void (JSONLexerEmitter)(JSONLexer *, GString *,
                                JSONTokenType, int x, int y);

struct JSONLexer
{
    JSONLexerEmitter *emit;
    int state;
    GString *token;
    int x, y;
};

void json_lexer_init(JSONLexer *lexer, JSONLexerEmitter func);

int json_lexer_feed(JSONLexer *lexer, const char *buffer, size_t size);

int json_lexer_flush(JSONLexer *lexer);

void json_lexer_destroy(JSONLexer *lexer);

#endif
