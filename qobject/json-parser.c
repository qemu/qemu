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

#include "qemu/osdep.h"

#include "qemu-common.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qint.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qfloat.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/json-parser.h"
#include "qapi/qmp/json-lexer.h"
#include "qapi/qmp/json-streamer.h"

typedef struct JSONParserContext
{
    Error *err;
    JSONToken *current;
    GQueue *buf;
} JSONParserContext;

#define BUG_ON(cond) assert(!(cond))

/**
 * TODO
 *
 * 0) make errors meaningful again
 * 1) add geometry information to tokens
 * 3) should we return a parsed size?
 * 4) deal with premature EOI
 */

static QObject *parse_value(JSONParserContext *ctxt, va_list *ap);

/**
 * Error handler
 */
static void GCC_FMT_ATTR(3, 4) parse_error(JSONParserContext *ctxt,
                                           JSONToken *token, const char *msg, ...)
{
    va_list ap;
    char message[1024];
    va_start(ap, msg);
    vsnprintf(message, sizeof(message), msg, ap);
    va_end(ap);
    if (ctxt->err) {
        error_free(ctxt->err);
        ctxt->err = NULL;
    }
    error_setg(&ctxt->err, "JSON parse error, %s", message);
}

/**
 * String helpers
 *
 * These helpers are used to unescape strings.
 */
static void wchar_to_utf8(uint16_t wchar, char *buffer, size_t buffer_length)
{
    if (wchar <= 0x007F) {
        BUG_ON(buffer_length < 2);

        buffer[0] = wchar & 0x7F;
        buffer[1] = 0;
    } else if (wchar <= 0x07FF) {
        BUG_ON(buffer_length < 3);

        buffer[0] = 0xC0 | ((wchar >> 6) & 0x1F);
        buffer[1] = 0x80 | (wchar & 0x3F);
        buffer[2] = 0;
    } else {
        BUG_ON(buffer_length < 4);

        buffer[0] = 0xE0 | ((wchar >> 12) & 0x0F);
        buffer[1] = 0x80 | ((wchar >> 6) & 0x3F);
        buffer[2] = 0x80 | (wchar & 0x3F);
        buffer[3] = 0;
    }
}

static int hex2decimal(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return (ch - '0');
    } else if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    } else if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }

    return -1;
}

/**
 * parse_string(): Parse a json string and return a QObject
 *
 *  string
 *      ""
 *      " chars "
 *  chars
 *      char
 *      char chars
 *  char
 *      any-Unicode-character-
 *          except-"-or-\-or-
 *          control-character
 *      \"
 *      \\
 *      \/
 *      \b
 *      \f
 *      \n
 *      \r
 *      \t
 *      \u four-hex-digits 
 */
static QString *qstring_from_escaped_str(JSONParserContext *ctxt,
                                         JSONToken *token)
{
    const char *ptr = token->str;
    QString *str;
    int double_quote = 1;

    if (*ptr == '"') {
        double_quote = 1;
    } else {
        double_quote = 0;
    }
    ptr++;

    str = qstring_new();
    while (*ptr && 
           ((double_quote && *ptr != '"') || (!double_quote && *ptr != '\''))) {
        if (*ptr == '\\') {
            ptr++;

            switch (*ptr) {
            case '"':
                qstring_append(str, "\"");
                ptr++;
                break;
            case '\'':
                qstring_append(str, "'");
                ptr++;
                break;
            case '\\':
                qstring_append(str, "\\");
                ptr++;
                break;
            case '/':
                qstring_append(str, "/");
                ptr++;
                break;
            case 'b':
                qstring_append(str, "\b");
                ptr++;
                break;
            case 'f':
                qstring_append(str, "\f");
                ptr++;
                break;
            case 'n':
                qstring_append(str, "\n");
                ptr++;
                break;
            case 'r':
                qstring_append(str, "\r");
                ptr++;
                break;
            case 't':
                qstring_append(str, "\t");
                ptr++;
                break;
            case 'u': {
                uint16_t unicode_char = 0;
                char utf8_char[4];
                int i = 0;

                ptr++;

                for (i = 0; i < 4; i++) {
                    if (qemu_isxdigit(*ptr)) {
                        unicode_char |= hex2decimal(*ptr) << ((3 - i) * 4);
                    } else {
                        parse_error(ctxt, token,
                                    "invalid hex escape sequence in string");
                        goto out;
                    }
                    ptr++;
                }

                wchar_to_utf8(unicode_char, utf8_char, sizeof(utf8_char));
                qstring_append(str, utf8_char);
            }   break;
            default:
                parse_error(ctxt, token, "invalid escape sequence in string");
                goto out;
            }
        } else {
            char dummy[2];

            dummy[0] = *ptr++;
            dummy[1] = 0;

            qstring_append(str, dummy);
        }
    }

    return str;

out:
    QDECREF(str);
    return NULL;
}

/* Note: the token object returned by parser_context_peek_token or
 * parser_context_pop_token is deleted as soon as parser_context_pop_token
 * is called again.
 */
static JSONToken *parser_context_pop_token(JSONParserContext *ctxt)
{
    g_free(ctxt->current);
    assert(!g_queue_is_empty(ctxt->buf));
    ctxt->current = g_queue_pop_head(ctxt->buf);
    return ctxt->current;
}

static JSONToken *parser_context_peek_token(JSONParserContext *ctxt)
{
    assert(!g_queue_is_empty(ctxt->buf));
    return g_queue_peek_head(ctxt->buf);
}

static JSONParserContext *parser_context_new(GQueue *tokens)
{
    JSONParserContext *ctxt;

    if (!tokens) {
        return NULL;
    }

    ctxt = g_malloc0(sizeof(JSONParserContext));
    ctxt->buf = tokens;

    return ctxt;
}

/* to support error propagation, ctxt->err must be freed separately */
static void parser_context_free(JSONParserContext *ctxt)
{
    if (ctxt) {
        while (!g_queue_is_empty(ctxt->buf)) {
            parser_context_pop_token(ctxt);
        }
        g_free(ctxt->current);
        g_queue_free(ctxt->buf);
        g_free(ctxt);
    }
}

/**
 * Parsing rules
 */
static int parse_pair(JSONParserContext *ctxt, QDict *dict, va_list *ap)
{
    QObject *key = NULL, *value;
    JSONToken *peek, *token;

    peek = parser_context_peek_token(ctxt);
    if (peek == NULL) {
        parse_error(ctxt, NULL, "premature EOI");
        goto out;
    }

    key = parse_value(ctxt, ap);
    if (!key || qobject_type(key) != QTYPE_QSTRING) {
        parse_error(ctxt, peek, "key is not a string in object");
        goto out;
    }

    token = parser_context_pop_token(ctxt);
    if (token == NULL) {
        parse_error(ctxt, NULL, "premature EOI");
        goto out;
    }

    if (token->type != JSON_COLON) {
        parse_error(ctxt, token, "missing : in object pair");
        goto out;
    }

    value = parse_value(ctxt, ap);
    if (value == NULL) {
        parse_error(ctxt, token, "Missing value in dict");
        goto out;
    }

    qdict_put_obj(dict, qstring_get_str(qobject_to_qstring(key)), value);

    qobject_decref(key);

    return 0;

out:
    qobject_decref(key);

    return -1;
}

static QObject *parse_object(JSONParserContext *ctxt, va_list *ap)
{
    QDict *dict = NULL;
    JSONToken *token, *peek;

    token = parser_context_pop_token(ctxt);
    assert(token && token->type == JSON_LCURLY);

    dict = qdict_new();

    peek = parser_context_peek_token(ctxt);
    if (peek == NULL) {
        parse_error(ctxt, NULL, "premature EOI");
        goto out;
    }

    if (peek->type != JSON_RCURLY) {
        if (parse_pair(ctxt, dict, ap) == -1) {
            goto out;
        }

        token = parser_context_pop_token(ctxt);
        if (token == NULL) {
            parse_error(ctxt, NULL, "premature EOI");
            goto out;
        }

        while (token->type != JSON_RCURLY) {
            if (token->type != JSON_COMMA) {
                parse_error(ctxt, token, "expected separator in dict");
                goto out;
            }

            if (parse_pair(ctxt, dict, ap) == -1) {
                goto out;
            }

            token = parser_context_pop_token(ctxt);
            if (token == NULL) {
                parse_error(ctxt, NULL, "premature EOI");
                goto out;
            }
        }
    } else {
        (void)parser_context_pop_token(ctxt);
    }

    return QOBJECT(dict);

out:
    QDECREF(dict);
    return NULL;
}

static QObject *parse_array(JSONParserContext *ctxt, va_list *ap)
{
    QList *list = NULL;
    JSONToken *token, *peek;

    token = parser_context_pop_token(ctxt);
    assert(token && token->type == JSON_LSQUARE);

    list = qlist_new();

    peek = parser_context_peek_token(ctxt);
    if (peek == NULL) {
        parse_error(ctxt, NULL, "premature EOI");
        goto out;
    }

    if (peek->type != JSON_RSQUARE) {
        QObject *obj;

        obj = parse_value(ctxt, ap);
        if (obj == NULL) {
            parse_error(ctxt, token, "expecting value");
            goto out;
        }

        qlist_append_obj(list, obj);

        token = parser_context_pop_token(ctxt);
        if (token == NULL) {
            parse_error(ctxt, NULL, "premature EOI");
            goto out;
        }

        while (token->type != JSON_RSQUARE) {
            if (token->type != JSON_COMMA) {
                parse_error(ctxt, token, "expected separator in list");
                goto out;
            }

            obj = parse_value(ctxt, ap);
            if (obj == NULL) {
                parse_error(ctxt, token, "expecting value");
                goto out;
            }

            qlist_append_obj(list, obj);

            token = parser_context_pop_token(ctxt);
            if (token == NULL) {
                parse_error(ctxt, NULL, "premature EOI");
                goto out;
            }
        }
    } else {
        (void)parser_context_pop_token(ctxt);
    }

    return QOBJECT(list);

out:
    QDECREF(list);
    return NULL;
}

static QObject *parse_keyword(JSONParserContext *ctxt)
{
    JSONToken *token;

    token = parser_context_pop_token(ctxt);
    assert(token && token->type == JSON_KEYWORD);

    if (!strcmp(token->str, "true")) {
        return QOBJECT(qbool_from_bool(true));
    } else if (!strcmp(token->str, "false")) {
        return QOBJECT(qbool_from_bool(false));
    } else if (!strcmp(token->str, "null")) {
        return qnull();
    }
    parse_error(ctxt, token, "invalid keyword '%s'", token->str);
    return NULL;
}

static QObject *parse_escape(JSONParserContext *ctxt, va_list *ap)
{
    JSONToken *token;

    if (ap == NULL) {
        return NULL;
    }

    token = parser_context_pop_token(ctxt);
    assert(token && token->type == JSON_ESCAPE);

    if (!strcmp(token->str, "%p")) {
        return va_arg(*ap, QObject *);
    } else if (!strcmp(token->str, "%i")) {
        return QOBJECT(qbool_from_bool(va_arg(*ap, int)));
    } else if (!strcmp(token->str, "%d")) {
        return QOBJECT(qint_from_int(va_arg(*ap, int)));
    } else if (!strcmp(token->str, "%ld")) {
        return QOBJECT(qint_from_int(va_arg(*ap, long)));
    } else if (!strcmp(token->str, "%lld") ||
               !strcmp(token->str, "%I64d")) {
        return QOBJECT(qint_from_int(va_arg(*ap, long long)));
    } else if (!strcmp(token->str, "%s")) {
        return QOBJECT(qstring_from_str(va_arg(*ap, const char *)));
    } else if (!strcmp(token->str, "%f")) {
        return QOBJECT(qfloat_from_double(va_arg(*ap, double)));
    }
    return NULL;
}

static QObject *parse_literal(JSONParserContext *ctxt)
{
    JSONToken *token;

    token = parser_context_pop_token(ctxt);
    assert(token);

    switch (token->type) {
    case JSON_STRING:
        return QOBJECT(qstring_from_escaped_str(ctxt, token));
    case JSON_INTEGER: {
        /* A possibility exists that this is a whole-valued float where the
         * fractional part was left out due to being 0 (.0). It's not a big
         * deal to treat these as ints in the parser, so long as users of the
         * resulting QObject know to expect a QInt in place of a QFloat in
         * cases like these.
         *
         * However, in some cases these values will overflow/underflow a
         * QInt/int64 container, thus we should assume these are to be handled
         * as QFloats/doubles rather than silently changing their values.
         *
         * strtoll() indicates these instances by setting errno to ERANGE
         */
        int64_t value;

        errno = 0; /* strtoll doesn't set errno on success */
        value = strtoll(token->str, NULL, 10);
        if (errno != ERANGE) {
            return QOBJECT(qint_from_int(value));
        }
        /* fall through to JSON_FLOAT */
    }
    case JSON_FLOAT:
        /* FIXME dependent on locale; a pervasive issue in QEMU */
        /* FIXME our lexer matches RFC 7159 in forbidding Inf or NaN,
         * but those might be useful extensions beyond JSON */
        return QOBJECT(qfloat_from_double(strtod(token->str, NULL)));
    default:
        abort();
    }
}

static QObject *parse_value(JSONParserContext *ctxt, va_list *ap)
{
    JSONToken *token;

    token = parser_context_peek_token(ctxt);
    if (token == NULL) {
        parse_error(ctxt, NULL, "premature EOI");
        return NULL;
    }

    switch (token->type) {
    case JSON_LCURLY:
        return parse_object(ctxt, ap);
    case JSON_LSQUARE:
        return parse_array(ctxt, ap);
    case JSON_ESCAPE:
        return parse_escape(ctxt, ap);
    case JSON_INTEGER:
    case JSON_FLOAT:
    case JSON_STRING:
        return parse_literal(ctxt);
    case JSON_KEYWORD:
        return parse_keyword(ctxt);
    default:
        parse_error(ctxt, token, "expecting value");
        return NULL;
    }
}

QObject *json_parser_parse(GQueue *tokens, va_list *ap)
{
    return json_parser_parse_err(tokens, ap, NULL);
}

QObject *json_parser_parse_err(GQueue *tokens, va_list *ap, Error **errp)
{
    JSONParserContext *ctxt = parser_context_new(tokens);
    QObject *result;

    if (!ctxt) {
        return NULL;
    }

    result = parse_value(ctxt, ap);

    error_propagate(errp, ctxt->err);

    parser_context_free(ctxt);

    return result;
}
