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
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qint.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qfloat.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/json-parser.h"
#include "qapi/qmp/json-lexer.h"
#include "qapi/qmp/qerror.h"

typedef struct JSONParserContext
{
    Error *err;
    struct {
        QObject **buf;
        size_t pos;
        size_t count;
    } tokens;
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
    char message[1024];
    va_start(ap, msg);
    vsnprintf(message, sizeof(message), msg, ap);
    va_end(ap);
    if (ctxt->err) {
        error_free(ctxt->err);
        ctxt->err = NULL;
    }
    error_set(&ctxt->err, QERR_JSON_PARSE_ERROR, message);
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

static QObject *parser_context_pop_token(JSONParserContext *ctxt)
{
    QObject *token;
    g_assert(ctxt->tokens.pos < ctxt->tokens.count);
    token = ctxt->tokens.buf[ctxt->tokens.pos];
    ctxt->tokens.pos++;
    return token;
}

/* Note: parser_context_{peek|pop}_token do not increment the
 * token object's refcount. In both cases the references will continue
 * to be tracked and cleaned up in parser_context_free(), so do not
 * attempt to free the token object.
 */
static QObject *parser_context_peek_token(JSONParserContext *ctxt)
{
    QObject *token;
    g_assert(ctxt->tokens.pos < ctxt->tokens.count);
    token = ctxt->tokens.buf[ctxt->tokens.pos];
    return token;
}

static JSONParserContext parser_context_save(JSONParserContext *ctxt)
{
    JSONParserContext saved_ctxt = {0};
    saved_ctxt.tokens.pos = ctxt->tokens.pos;
    saved_ctxt.tokens.count = ctxt->tokens.count;
    saved_ctxt.tokens.buf = ctxt->tokens.buf;
    return saved_ctxt;
}

static void parser_context_restore(JSONParserContext *ctxt,
                                   JSONParserContext saved_ctxt)
{
    ctxt->tokens.pos = saved_ctxt.tokens.pos;
    ctxt->tokens.count = saved_ctxt.tokens.count;
    ctxt->tokens.buf = saved_ctxt.tokens.buf;
}

static void tokens_append_from_iter(QObject *obj, void *opaque)
{
    JSONParserContext *ctxt = opaque;
    g_assert(ctxt->tokens.pos < ctxt->tokens.count);
    ctxt->tokens.buf[ctxt->tokens.pos++] = obj;
    qobject_incref(obj);
}

static JSONParserContext *parser_context_new(QList *tokens)
{
    JSONParserContext *ctxt;
    size_t count;

    if (!tokens) {
        return NULL;
    }

    count = qlist_size(tokens);
    if (count == 0) {
        return NULL;
    }

    ctxt = g_malloc0(sizeof(JSONParserContext));
    ctxt->tokens.pos = 0;
    ctxt->tokens.count = count;
    ctxt->tokens.buf = g_malloc(count * sizeof(QObject *));
    qlist_iter(tokens, tokens_append_from_iter, ctxt);
    ctxt->tokens.pos = 0;

    return ctxt;
}

/* to support error propagation, ctxt->err must be freed separately */
static void parser_context_free(JSONParserContext *ctxt)
{
    int i;
    if (ctxt) {
        for (i = 0; i < ctxt->tokens.count; i++) {
            qobject_decref(ctxt->tokens.buf[i]);
        }
        g_free(ctxt->tokens.buf);
        g_free(ctxt);
    }
}

/**
 * Parsing rules
 */
static int parse_pair(JSONParserContext *ctxt, QDict *dict, va_list *ap)
{
    QObject *key = NULL, *token = NULL, *value, *peek;
    JSONParserContext saved_ctxt = parser_context_save(ctxt);

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

    if (!token_is_operator(token, ':')) {
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
    parser_context_restore(ctxt, saved_ctxt);
    qobject_decref(key);

    return -1;
}

static QObject *parse_object(JSONParserContext *ctxt, va_list *ap)
{
    QDict *dict = NULL;
    QObject *token, *peek;
    JSONParserContext saved_ctxt = parser_context_save(ctxt);

    token = parser_context_pop_token(ctxt);
    if (token == NULL) {
        goto out;
    }

    if (!token_is_operator(token, '{')) {
        goto out;
    }
    token = NULL;

    dict = qdict_new();

    peek = parser_context_peek_token(ctxt);
    if (peek == NULL) {
        parse_error(ctxt, NULL, "premature EOI");
        goto out;
    }

    if (!token_is_operator(peek, '}')) {
        if (parse_pair(ctxt, dict, ap) == -1) {
            goto out;
        }

        token = parser_context_pop_token(ctxt);
        if (token == NULL) {
            parse_error(ctxt, NULL, "premature EOI");
            goto out;
        }

        while (!token_is_operator(token, '}')) {
            if (!token_is_operator(token, ',')) {
                parse_error(ctxt, token, "expected separator in dict");
                goto out;
            }
            token = NULL;

            if (parse_pair(ctxt, dict, ap) == -1) {
                goto out;
            }

            token = parser_context_pop_token(ctxt);
            if (token == NULL) {
                parse_error(ctxt, NULL, "premature EOI");
                goto out;
            }
        }
        token = NULL;
    } else {
        token = parser_context_pop_token(ctxt);
        token = NULL;
    }

    return QOBJECT(dict);

out:
    parser_context_restore(ctxt, saved_ctxt);
    QDECREF(dict);
    return NULL;
}

static QObject *parse_array(JSONParserContext *ctxt, va_list *ap)
{
    QList *list = NULL;
    QObject *token, *peek;
    JSONParserContext saved_ctxt = parser_context_save(ctxt);

    token = parser_context_pop_token(ctxt);
    if (token == NULL) {
        goto out;
    }

    if (!token_is_operator(token, '[')) {
        token = NULL;
        goto out;
    }
    token = NULL;

    list = qlist_new();

    peek = parser_context_peek_token(ctxt);
    if (peek == NULL) {
        parse_error(ctxt, NULL, "premature EOI");
        goto out;
    }

    if (!token_is_operator(peek, ']')) {
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

        while (!token_is_operator(token, ']')) {
            if (!token_is_operator(token, ',')) {
                parse_error(ctxt, token, "expected separator in list");
                goto out;
            }

            token = NULL;

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

        token = NULL;
    } else {
        token = parser_context_pop_token(ctxt);
        token = NULL;
    }

    return QOBJECT(list);

out:
    parser_context_restore(ctxt, saved_ctxt);
    QDECREF(list);
    return NULL;
}

static QObject *parse_keyword(JSONParserContext *ctxt)
{
    QObject *token, *ret;
    JSONParserContext saved_ctxt = parser_context_save(ctxt);

    token = parser_context_pop_token(ctxt);
    if (token == NULL) {
        goto out;
    }

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

    return ret;

out: 
    parser_context_restore(ctxt, saved_ctxt);

    return NULL;
}

static QObject *parse_escape(JSONParserContext *ctxt, va_list *ap)
{
    QObject *token = NULL, *obj;
    JSONParserContext saved_ctxt = parser_context_save(ctxt);

    if (ap == NULL) {
        goto out;
    }

    token = parser_context_pop_token(ctxt);
    if (token == NULL) {
        goto out;
    }

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

    return obj;

out:
    parser_context_restore(ctxt, saved_ctxt);

    return NULL;
}

static QObject *parse_literal(JSONParserContext *ctxt)
{
    QObject *token, *obj;
    JSONParserContext saved_ctxt = parser_context_save(ctxt);

    token = parser_context_pop_token(ctxt);
    if (token == NULL) {
        goto out;
    }

    switch (token_get_type(token)) {
    case JSON_STRING:
        obj = QOBJECT(qstring_from_escaped_str(ctxt, token));
        break;
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
        value = strtoll(token_get_value(token), NULL, 10);
        if (errno != ERANGE) {
            obj = QOBJECT(qint_from_int(value));
            break;
        }
        /* fall through to JSON_FLOAT */
    }
    case JSON_FLOAT:
        /* FIXME dependent on locale */
        obj = QOBJECT(qfloat_from_double(strtod(token_get_value(token), NULL)));
        break;
    default:
        goto out;
    }

    return obj;

out:
    parser_context_restore(ctxt, saved_ctxt);

    return NULL;
}

static QObject *parse_value(JSONParserContext *ctxt, va_list *ap)
{
    QObject *obj;

    obj = parse_object(ctxt, ap);
    if (obj == NULL) {
        obj = parse_array(ctxt, ap);
    }
    if (obj == NULL) {
        obj = parse_escape(ctxt, ap);
    }
    if (obj == NULL) {
        obj = parse_keyword(ctxt);
    } 
    if (obj == NULL) {
        obj = parse_literal(ctxt);
    }

    return obj;
}

QObject *json_parser_parse(QList *tokens, va_list *ap)
{
    return json_parser_parse_err(tokens, ap, NULL);
}

QObject *json_parser_parse_err(QList *tokens, va_list *ap, Error **errp)
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
