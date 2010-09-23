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

#include <stdarg.h>

#include "qemu-common.h"
#include "qstring.h"
#include "qint.h"
#include "qdict.h"
#include "qlist.h"
#include "qfloat.h"
#include "qbool.h"
#include "json-parser.h"
#include "json-lexer.h"

typedef struct JSONParserContext
{
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

static QObject *parse_value(JSONParserContext *ctxt, QList **tokens, va_list *ap);

/**
 * Token manipulators
 *
 * tokens are dictionaries that contain a type, a string value, and geometry information
 * about a token identified by the lexer.  These are routines that make working with
 * these objects a bit easier.
 */
static const char *token_get_value(QObject *obj)
{
    return qdict_get_str(qobject_to_qdict(obj), "token");
}

static JSONTokenType token_get_type(QObject *obj)
{
    return qdict_get_int(qobject_to_qdict(obj), "type");
}

static int token_is_operator(QObject *obj, char op)
{
    const char *val;

    if (token_get_type(obj) != JSON_OPERATOR) {
        return 0;
    }

    val = token_get_value(obj);

    return (val[0] == op) && (val[1] == 0);
}

static int token_is_keyword(QObject *obj, const char *value)
{
    if (token_get_type(obj) != JSON_KEYWORD) {
        return 0;
    }

    return strcmp(token_get_value(obj), value) == 0;
}

static int token_is_escape(QObject *obj, const char *value)
{
    if (token_get_type(obj) != JSON_ESCAPE) {
        return 0;
    }

    return (strcmp(token_get_value(obj), value) == 0);
}

/**
 * Error handler
 */
static void GCC_FMT_ATTR(3, 4) parse_error(JSONParserContext *ctxt,
                                           QObject *token, const char *msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    fprintf(stderr, "parse error: ");
    vfprintf(stderr, msg, ap);
    fprintf(stderr, "\n");
    va_end(ap);
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
static QString *qstring_from_escaped_str(JSONParserContext *ctxt, QObject *token)
{
    const char *ptr = token_get_value(token);
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

/**
 * Parsing rules
 */
static int parse_pair(JSONParserContext *ctxt, QDict *dict, QList **tokens, va_list *ap)
{
    QObject *key, *token = NULL, *value, *peek;
    QList *working = qlist_copy(*tokens);

    peek = qlist_peek(working);
    key = parse_value(ctxt, &working, ap);
    if (!key || qobject_type(key) != QTYPE_QSTRING) {
        parse_error(ctxt, peek, "key is not a string in object");
        goto out;
    }

    token = qlist_pop(working);
    if (!token_is_operator(token, ':')) {
        parse_error(ctxt, token, "missing : in object pair");
        goto out;
    }

    value = parse_value(ctxt, &working, ap);
    if (value == NULL) {
        parse_error(ctxt, token, "Missing value in dict");
        goto out;
    }

    qdict_put_obj(dict, qstring_get_str(qobject_to_qstring(key)), value);

    qobject_decref(token);
    qobject_decref(key);
    QDECREF(*tokens);
    *tokens = working;

    return 0;

out:
    qobject_decref(token);
    qobject_decref(key);
    QDECREF(working);

    return -1;
}

static QObject *parse_object(JSONParserContext *ctxt, QList **tokens, va_list *ap)
{
    QDict *dict = NULL;
    QObject *token, *peek;
    QList *working = qlist_copy(*tokens);

    token = qlist_pop(working);
    if (!token_is_operator(token, '{')) {
        goto out;
    }
    qobject_decref(token);
    token = NULL;

    dict = qdict_new();

    peek = qlist_peek(working);
    if (!token_is_operator(peek, '}')) {
        if (parse_pair(ctxt, dict, &working, ap) == -1) {
            goto out;
        }

        token = qlist_pop(working);
        while (!token_is_operator(token, '}')) {
            if (!token_is_operator(token, ',')) {
                parse_error(ctxt, token, "expected separator in dict");
                goto out;
            }
            qobject_decref(token);
            token = NULL;

            if (parse_pair(ctxt, dict, &working, ap) == -1) {
                goto out;
            }

            token = qlist_pop(working);
        }
        qobject_decref(token);
        token = NULL;
    } else {
        token = qlist_pop(working);
        qobject_decref(token);
        token = NULL;
    }

    QDECREF(*tokens);
    *tokens = working;

    return QOBJECT(dict);

out:
    qobject_decref(token);
    QDECREF(working);
    QDECREF(dict);
    return NULL;
}

static QObject *parse_array(JSONParserContext *ctxt, QList **tokens, va_list *ap)
{
    QList *list = NULL;
    QObject *token, *peek;
    QList *working = qlist_copy(*tokens);

    token = qlist_pop(working);
    if (!token_is_operator(token, '[')) {
        goto out;
    }
    qobject_decref(token);
    token = NULL;

    list = qlist_new();

    peek = qlist_peek(working);
    if (!token_is_operator(peek, ']')) {
        QObject *obj;

        obj = parse_value(ctxt, &working, ap);
        if (obj == NULL) {
            parse_error(ctxt, token, "expecting value");
            goto out;
        }

        qlist_append_obj(list, obj);

        token = qlist_pop(working);
        while (!token_is_operator(token, ']')) {
            if (!token_is_operator(token, ',')) {
                parse_error(ctxt, token, "expected separator in list");
                goto out;
            }

            qobject_decref(token);
            token = NULL;

            obj = parse_value(ctxt, &working, ap);
            if (obj == NULL) {
                parse_error(ctxt, token, "expecting value");
                goto out;
            }

            qlist_append_obj(list, obj);

            token = qlist_pop(working);
        }

        qobject_decref(token);
        token = NULL;
    } else {
        token = qlist_pop(working);
        qobject_decref(token);
        token = NULL;
    }

    QDECREF(*tokens);
    *tokens = working;

    return QOBJECT(list);

out:
    qobject_decref(token);
    QDECREF(working);
    QDECREF(list);
    return NULL;
}

static QObject *parse_keyword(JSONParserContext *ctxt, QList **tokens)
{
    QObject *token, *ret;
    QList *working = qlist_copy(*tokens);

    token = qlist_pop(working);

    if (token_get_type(token) != JSON_KEYWORD) {
        goto out;
    }

    if (token_is_keyword(token, "true")) {
        ret = QOBJECT(qbool_from_int(true));
    } else if (token_is_keyword(token, "false")) {
        ret = QOBJECT(qbool_from_int(false));
    } else {
        parse_error(ctxt, token, "invalid keyword `%s'", token_get_value(token));
        goto out;
    }

    qobject_decref(token);
    QDECREF(*tokens);
    *tokens = working;

    return ret;

out: 
    qobject_decref(token);
    QDECREF(working);

    return NULL;
}

static QObject *parse_escape(JSONParserContext *ctxt, QList **tokens, va_list *ap)
{
    QObject *token = NULL, *obj;
    QList *working = qlist_copy(*tokens);

    if (ap == NULL) {
        goto out;
    }

    token = qlist_pop(working);

    if (token_is_escape(token, "%p")) {
        obj = va_arg(*ap, QObject *);
    } else if (token_is_escape(token, "%i")) {
        obj = QOBJECT(qbool_from_int(va_arg(*ap, int)));
    } else if (token_is_escape(token, "%d")) {
        obj = QOBJECT(qint_from_int(va_arg(*ap, int)));
    } else if (token_is_escape(token, "%ld")) {
        obj = QOBJECT(qint_from_int(va_arg(*ap, long)));
    } else if (token_is_escape(token, "%lld") ||
               token_is_escape(token, "%I64d")) {
        obj = QOBJECT(qint_from_int(va_arg(*ap, long long)));
    } else if (token_is_escape(token, "%s")) {
        obj = QOBJECT(qstring_from_str(va_arg(*ap, const char *)));
    } else if (token_is_escape(token, "%f")) {
        obj = QOBJECT(qfloat_from_double(va_arg(*ap, double)));
    } else {
        goto out;
    }

    qobject_decref(token);
    QDECREF(*tokens);
    *tokens = working;

    return obj;

out:
    qobject_decref(token);
    QDECREF(working);

    return NULL;
}

static QObject *parse_literal(JSONParserContext *ctxt, QList **tokens)
{
    QObject *token, *obj;
    QList *working = qlist_copy(*tokens);

    token = qlist_pop(working);
    switch (token_get_type(token)) {
    case JSON_STRING:
        obj = QOBJECT(qstring_from_escaped_str(ctxt, token));
        break;
    case JSON_INTEGER:
        obj = QOBJECT(qint_from_int(strtoll(token_get_value(token), NULL, 10)));
        break;
    case JSON_FLOAT:
        /* FIXME dependent on locale */
        obj = QOBJECT(qfloat_from_double(strtod(token_get_value(token), NULL)));
        break;
    default:
        goto out;
    }

    qobject_decref(token);
    QDECREF(*tokens);
    *tokens = working;

    return obj;

out:
    qobject_decref(token);
    QDECREF(working);

    return NULL;
}

static QObject *parse_value(JSONParserContext *ctxt, QList **tokens, va_list *ap)
{
    QObject *obj;

    obj = parse_object(ctxt, tokens, ap);
    if (obj == NULL) {
        obj = parse_array(ctxt, tokens, ap);
    }
    if (obj == NULL) {
        obj = parse_escape(ctxt, tokens, ap);
    }
    if (obj == NULL) {
        obj = parse_keyword(ctxt, tokens);
    } 
    if (obj == NULL) {
        obj = parse_literal(ctxt, tokens);
    }

    return obj;
}

QObject *json_parser_parse(QList *tokens, va_list *ap)
{
    JSONParserContext ctxt = {};
    QList *working = qlist_copy(tokens);
    QObject *result;

    result = parse_value(&ctxt, &working, ap);

    QDECREF(working);

    return result;
}
