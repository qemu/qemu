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

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/qmp/json-lexer.h"

#define MAX_TOKEN_SIZE (64ULL << 20)

/*
 * \"([^\\\"]|(\\\"\\'\\\\\\/\\b\\f\\n\\r\\t\\u[0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]))*\"
 * '([^\\']|(\\\"\\'\\\\\\/\\b\\f\\n\\r\\t\\u[0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]))*'
 * 0|([1-9][0-9]*(.[0-9]+)?([eE]([-+])?[0-9]+))
 * [{}\[\],:]
 * [a-z]+
 *
 */

enum json_lexer_state {
    IN_ERROR = 0,               /* must really be 0, see json_lexer[] */
    IN_DQ_UCODE3,
    IN_DQ_UCODE2,
    IN_DQ_UCODE1,
    IN_DQ_UCODE0,
    IN_DQ_STRING_ESCAPE,
    IN_DQ_STRING,
    IN_SQ_UCODE3,
    IN_SQ_UCODE2,
    IN_SQ_UCODE1,
    IN_SQ_UCODE0,
    IN_SQ_STRING_ESCAPE,
    IN_SQ_STRING,
    IN_ZERO,
    IN_DIGITS,
    IN_DIGIT,
    IN_EXP_E,
    IN_MANTISSA,
    IN_MANTISSA_DIGITS,
    IN_NONZERO_NUMBER,
    IN_NEG_NONZERO_NUMBER,
    IN_KEYWORD,
    IN_ESCAPE,
    IN_ESCAPE_L,
    IN_ESCAPE_LL,
    IN_ESCAPE_I,
    IN_ESCAPE_I6,
    IN_ESCAPE_I64,
    IN_WHITESPACE,
    IN_START,
};

QEMU_BUILD_BUG_ON((int)JSON_MIN <= (int)IN_START);

#define TERMINAL(state) [0 ... 0x7F] = (state)

/* Return whether TERMINAL is a terminal state and the transition to it
   from OLD_STATE required lookahead.  This happens whenever the table
   below uses the TERMINAL macro.  */
#define TERMINAL_NEEDED_LOOKAHEAD(old_state, terminal) \
            (json_lexer[(old_state)][0] == (terminal))

static const uint8_t json_lexer[][256] =  {
    /* Relies on default initialization to IN_ERROR! */

    /* double quote string */
    [IN_DQ_UCODE3] = {
        ['0' ... '9'] = IN_DQ_STRING,
        ['a' ... 'f'] = IN_DQ_STRING,
        ['A' ... 'F'] = IN_DQ_STRING,
    },
    [IN_DQ_UCODE2] = {
        ['0' ... '9'] = IN_DQ_UCODE3,
        ['a' ... 'f'] = IN_DQ_UCODE3,
        ['A' ... 'F'] = IN_DQ_UCODE3,
    },
    [IN_DQ_UCODE1] = {
        ['0' ... '9'] = IN_DQ_UCODE2,
        ['a' ... 'f'] = IN_DQ_UCODE2,
        ['A' ... 'F'] = IN_DQ_UCODE2,
    },
    [IN_DQ_UCODE0] = {
        ['0' ... '9'] = IN_DQ_UCODE1,
        ['a' ... 'f'] = IN_DQ_UCODE1,
        ['A' ... 'F'] = IN_DQ_UCODE1,
    },
    [IN_DQ_STRING_ESCAPE] = {
        ['b'] = IN_DQ_STRING,
        ['f'] =  IN_DQ_STRING,
        ['n'] =  IN_DQ_STRING,
        ['r'] =  IN_DQ_STRING,
        ['t'] =  IN_DQ_STRING,
        ['/'] = IN_DQ_STRING,
        ['\\'] = IN_DQ_STRING,
        ['\''] = IN_DQ_STRING,
        ['\"'] = IN_DQ_STRING,
        ['u'] = IN_DQ_UCODE0,
    },
    [IN_DQ_STRING] = {
        [1 ... 0xBF] = IN_DQ_STRING,
        [0xC2 ... 0xF4] = IN_DQ_STRING,
        ['\\'] = IN_DQ_STRING_ESCAPE,
        ['"'] = JSON_STRING,
    },

    /* single quote string */
    [IN_SQ_UCODE3] = {
        ['0' ... '9'] = IN_SQ_STRING,
        ['a' ... 'f'] = IN_SQ_STRING,
        ['A' ... 'F'] = IN_SQ_STRING,
    },
    [IN_SQ_UCODE2] = {
        ['0' ... '9'] = IN_SQ_UCODE3,
        ['a' ... 'f'] = IN_SQ_UCODE3,
        ['A' ... 'F'] = IN_SQ_UCODE3,
    },
    [IN_SQ_UCODE1] = {
        ['0' ... '9'] = IN_SQ_UCODE2,
        ['a' ... 'f'] = IN_SQ_UCODE2,
        ['A' ... 'F'] = IN_SQ_UCODE2,
    },
    [IN_SQ_UCODE0] = {
        ['0' ... '9'] = IN_SQ_UCODE1,
        ['a' ... 'f'] = IN_SQ_UCODE1,
        ['A' ... 'F'] = IN_SQ_UCODE1,
    },
    [IN_SQ_STRING_ESCAPE] = {
        ['b'] = IN_SQ_STRING,
        ['f'] =  IN_SQ_STRING,
        ['n'] =  IN_SQ_STRING,
        ['r'] =  IN_SQ_STRING,
        ['t'] =  IN_SQ_STRING,
        ['/'] = IN_SQ_STRING,
        ['\\'] = IN_SQ_STRING,
        ['\''] = IN_SQ_STRING,
        ['\"'] = IN_SQ_STRING,
        ['u'] = IN_SQ_UCODE0,
    },
    [IN_SQ_STRING] = {
        [1 ... 0xBF] = IN_SQ_STRING,
        [0xC2 ... 0xF4] = IN_SQ_STRING,
        ['\\'] = IN_SQ_STRING_ESCAPE,
        ['\''] = JSON_STRING,
    },

    /* Zero */
    [IN_ZERO] = {
        TERMINAL(JSON_INTEGER),
        ['0' ... '9'] = IN_ERROR,
        ['.'] = IN_MANTISSA,
    },

    /* Float */
    [IN_DIGITS] = {
        TERMINAL(JSON_FLOAT),
        ['0' ... '9'] = IN_DIGITS,
    },

    [IN_DIGIT] = {
        ['0' ... '9'] = IN_DIGITS,
    },

    [IN_EXP_E] = {
        ['-'] = IN_DIGIT,
        ['+'] = IN_DIGIT,
        ['0' ... '9'] = IN_DIGITS,
    },

    [IN_MANTISSA_DIGITS] = {
        TERMINAL(JSON_FLOAT),
        ['0' ... '9'] = IN_MANTISSA_DIGITS,
        ['e'] = IN_EXP_E,
        ['E'] = IN_EXP_E,
    },

    [IN_MANTISSA] = {
        ['0' ... '9'] = IN_MANTISSA_DIGITS,
    },

    /* Number */
    [IN_NONZERO_NUMBER] = {
        TERMINAL(JSON_INTEGER),
        ['0' ... '9'] = IN_NONZERO_NUMBER,
        ['e'] = IN_EXP_E,
        ['E'] = IN_EXP_E,
        ['.'] = IN_MANTISSA,
    },

    [IN_NEG_NONZERO_NUMBER] = {
        ['0'] = IN_ZERO,
        ['1' ... '9'] = IN_NONZERO_NUMBER,
    },

    /* keywords */
    [IN_KEYWORD] = {
        TERMINAL(JSON_KEYWORD),
        ['a' ... 'z'] = IN_KEYWORD,
    },

    /* whitespace */
    [IN_WHITESPACE] = {
        TERMINAL(JSON_SKIP),
        [' '] = IN_WHITESPACE,
        ['\t'] = IN_WHITESPACE,
        ['\r'] = IN_WHITESPACE,
        ['\n'] = IN_WHITESPACE,
    },        

    /* escape */
    [IN_ESCAPE_LL] = {
        ['d'] = JSON_ESCAPE,
    },

    [IN_ESCAPE_L] = {
        ['d'] = JSON_ESCAPE,
        ['l'] = IN_ESCAPE_LL,
    },

    [IN_ESCAPE_I64] = {
        ['d'] = JSON_ESCAPE,
    },

    [IN_ESCAPE_I6] = {
        ['4'] = IN_ESCAPE_I64,
    },

    [IN_ESCAPE_I] = {
        ['6'] = IN_ESCAPE_I6,
    },

    [IN_ESCAPE] = {
        ['d'] = JSON_ESCAPE,
        ['i'] = JSON_ESCAPE,
        ['p'] = JSON_ESCAPE,
        ['s'] = JSON_ESCAPE,
        ['f'] = JSON_ESCAPE,
        ['l'] = IN_ESCAPE_L,
        ['I'] = IN_ESCAPE_I,
    },

    /* top level rule */
    [IN_START] = {
        ['"'] = IN_DQ_STRING,
        ['\''] = IN_SQ_STRING,
        ['0'] = IN_ZERO,
        ['1' ... '9'] = IN_NONZERO_NUMBER,
        ['-'] = IN_NEG_NONZERO_NUMBER,
        ['{'] = JSON_LCURLY,
        ['}'] = JSON_RCURLY,
        ['['] = JSON_LSQUARE,
        [']'] = JSON_RSQUARE,
        [','] = JSON_COMMA,
        [':'] = JSON_COLON,
        ['a' ... 'z'] = IN_KEYWORD,
        ['%'] = IN_ESCAPE,
        [' '] = IN_WHITESPACE,
        ['\t'] = IN_WHITESPACE,
        ['\r'] = IN_WHITESPACE,
        ['\n'] = IN_WHITESPACE,
    },
};

void json_lexer_init(JSONLexer *lexer, JSONLexerEmitter func)
{
    lexer->emit = func;
    lexer->state = IN_START;
    lexer->token = g_string_sized_new(3);
    lexer->x = lexer->y = 0;
}

static int json_lexer_feed_char(JSONLexer *lexer, char ch, bool flush)
{
    int char_consumed, new_state;

    lexer->x++;
    if (ch == '\n') {
        lexer->x = 0;
        lexer->y++;
    }

    do {
        assert(lexer->state <= ARRAY_SIZE(json_lexer));
        new_state = json_lexer[lexer->state][(uint8_t)ch];
        char_consumed = !TERMINAL_NEEDED_LOOKAHEAD(lexer->state, new_state);
        if (char_consumed) {
            g_string_append_c(lexer->token, ch);
        }

        switch (new_state) {
        case JSON_LCURLY:
        case JSON_RCURLY:
        case JSON_LSQUARE:
        case JSON_RSQUARE:
        case JSON_COLON:
        case JSON_COMMA:
        case JSON_ESCAPE:
        case JSON_INTEGER:
        case JSON_FLOAT:
        case JSON_KEYWORD:
        case JSON_STRING:
            lexer->emit(lexer, lexer->token, new_state, lexer->x, lexer->y);
            /* fall through */
        case JSON_SKIP:
            g_string_truncate(lexer->token, 0);
            new_state = IN_START;
            break;
        case IN_ERROR:
            /* XXX: To avoid having previous bad input leaving the parser in an
             * unresponsive state where we consume unpredictable amounts of
             * subsequent "good" input, percolate this error state up to the
             * tokenizer/parser by forcing a NULL object to be emitted, then
             * reset state.
             *
             * Also note that this handling is required for reliable channel
             * negotiation between QMP and the guest agent, since chr(0xFF)
             * is placed at the beginning of certain events to ensure proper
             * delivery when the channel is in an unknown state. chr(0xFF) is
             * never a valid ASCII/UTF-8 sequence, so this should reliably
             * induce an error/flush state.
             */
            lexer->emit(lexer, lexer->token, JSON_ERROR, lexer->x, lexer->y);
            g_string_truncate(lexer->token, 0);
            new_state = IN_START;
            lexer->state = new_state;
            return 0;
        default:
            break;
        }
        lexer->state = new_state;
    } while (!char_consumed && !flush);

    /* Do not let a single token grow to an arbitrarily large size,
     * this is a security consideration.
     */
    if (lexer->token->len > MAX_TOKEN_SIZE) {
        lexer->emit(lexer, lexer->token, lexer->state, lexer->x, lexer->y);
        g_string_truncate(lexer->token, 0);
        lexer->state = IN_START;
    }

    return 0;
}

int json_lexer_feed(JSONLexer *lexer, const char *buffer, size_t size)
{
    size_t i;

    for (i = 0; i < size; i++) {
        int err;

        err = json_lexer_feed_char(lexer, buffer[i], false);
        if (err < 0) {
            return err;
        }
    }

    return 0;
}

int json_lexer_flush(JSONLexer *lexer)
{
    return lexer->state == IN_START ? 0 : json_lexer_feed_char(lexer, 0, true);
}

void json_lexer_destroy(JSONLexer *lexer)
{
    g_string_free(lexer->token, true);
}
