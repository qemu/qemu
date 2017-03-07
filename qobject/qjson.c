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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qmp/json-lexer.h"
#include "qapi/qmp/json-parser.h"
#include "qapi/qmp/json-streamer.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/types.h"
#include "qemu/unicode.h"

typedef struct JSONParsingState
{
    JSONMessageParser parser;
    va_list *ap;
    QObject *result;
    Error *err;
} JSONParsingState;

static void parse_json(JSONMessageParser *parser, GQueue *tokens)
{
    JSONParsingState *s = container_of(parser, JSONParsingState, parser);

    s->result = json_parser_parse_err(tokens, s->ap, &s->err);
}

QObject *qobject_from_jsonv(const char *string, va_list *ap, Error **errp)
{
    JSONParsingState state = {};

    state.ap = ap;

    json_message_parser_init(&state.parser, parse_json);
    json_message_parser_feed(&state.parser, string, strlen(string));
    json_message_parser_flush(&state.parser);
    json_message_parser_destroy(&state.parser);

    error_propagate(errp, state.err);
    return state.result;
}

QObject *qobject_from_json(const char *string, Error **errp)
{
    return qobject_from_jsonv(string, NULL, errp);
}

/*
 * IMPORTANT: This function aborts on error, thus it must not
 * be used with untrusted arguments.
 */
QObject *qobject_from_jsonf(const char *string, ...)
{
    QObject *obj;
    va_list ap;

    va_start(ap, string);
    obj = qobject_from_jsonv(string, &ap, &error_abort);
    va_end(ap);

    assert(obj != NULL);
    return obj;
}

typedef struct ToJsonIterState
{
    int indent;
    int pretty;
    int count;
    QString *str;
} ToJsonIterState;

static void to_json(const QObject *obj, QString *str, int pretty, int indent);

static void to_json_dict_iter(const char *key, QObject *obj, void *opaque)
{
    ToJsonIterState *s = opaque;
    QString *qkey;
    int j;

    if (s->count) {
        qstring_append(s->str, s->pretty ? "," : ", ");
    }

    if (s->pretty) {
        qstring_append(s->str, "\n");
        for (j = 0 ; j < s->indent ; j++)
            qstring_append(s->str, "    ");
    }

    qkey = qstring_from_str(key);
    to_json(QOBJECT(qkey), s->str, s->pretty, s->indent);
    QDECREF(qkey);

    qstring_append(s->str, ": ");
    to_json(obj, s->str, s->pretty, s->indent);
    s->count++;
}

static void to_json_list_iter(QObject *obj, void *opaque)
{
    ToJsonIterState *s = opaque;
    int j;

    if (s->count) {
        qstring_append(s->str, s->pretty ? "," : ", ");
    }

    if (s->pretty) {
        qstring_append(s->str, "\n");
        for (j = 0 ; j < s->indent ; j++)
            qstring_append(s->str, "    ");
    }

    to_json(obj, s->str, s->pretty, s->indent);
    s->count++;
}

static void to_json(const QObject *obj, QString *str, int pretty, int indent)
{
    switch (qobject_type(obj)) {
    case QTYPE_QNULL:
        qstring_append(str, "null");
        break;
    case QTYPE_QINT: {
        QInt *val = qobject_to_qint(obj);
        char buffer[1024];

        snprintf(buffer, sizeof(buffer), "%" PRId64, qint_get_int(val));
        qstring_append(str, buffer);
        break;
    }
    case QTYPE_QSTRING: {
        QString *val = qobject_to_qstring(obj);
        const char *ptr;
        int cp;
        char buf[16];
        char *end;

        ptr = qstring_get_str(val);
        qstring_append(str, "\"");

        for (; *ptr; ptr = end) {
            cp = mod_utf8_codepoint(ptr, 6, &end);
            switch (cp) {
            case '\"':
                qstring_append(str, "\\\"");
                break;
            case '\\':
                qstring_append(str, "\\\\");
                break;
            case '\b':
                qstring_append(str, "\\b");
                break;
            case '\f':
                qstring_append(str, "\\f");
                break;
            case '\n':
                qstring_append(str, "\\n");
                break;
            case '\r':
                qstring_append(str, "\\r");
                break;
            case '\t':
                qstring_append(str, "\\t");
                break;
            default:
                if (cp < 0) {
                    cp = 0xFFFD; /* replacement character */
                }
                if (cp > 0xFFFF) {
                    /* beyond BMP; need a surrogate pair */
                    snprintf(buf, sizeof(buf), "\\u%04X\\u%04X",
                             0xD800 + ((cp - 0x10000) >> 10),
                             0xDC00 + ((cp - 0x10000) & 0x3FF));
                } else if (cp < 0x20 || cp >= 0x7F) {
                    snprintf(buf, sizeof(buf), "\\u%04X", cp);
                } else {
                    buf[0] = cp;
                    buf[1] = 0;
                }
                qstring_append(str, buf);
            }
        };

        qstring_append(str, "\"");
        break;
    }
    case QTYPE_QDICT: {
        ToJsonIterState s;
        QDict *val = qobject_to_qdict(obj);

        s.count = 0;
        s.str = str;
        s.indent = indent + 1;
        s.pretty = pretty;
        qstring_append(str, "{");
        qdict_iter(val, to_json_dict_iter, &s);
        if (pretty) {
            int j;
            qstring_append(str, "\n");
            for (j = 0 ; j < indent ; j++)
                qstring_append(str, "    ");
        }
        qstring_append(str, "}");
        break;
    }
    case QTYPE_QLIST: {
        ToJsonIterState s;
        QList *val = qobject_to_qlist(obj);

        s.count = 0;
        s.str = str;
        s.indent = indent + 1;
        s.pretty = pretty;
        qstring_append(str, "[");
        qlist_iter(val, (void *)to_json_list_iter, &s);
        if (pretty) {
            int j;
            qstring_append(str, "\n");
            for (j = 0 ; j < indent ; j++)
                qstring_append(str, "    ");
        }
        qstring_append(str, "]");
        break;
    }
    case QTYPE_QFLOAT: {
        QFloat *val = qobject_to_qfloat(obj);
        char buffer[1024];
        int len;

        /* FIXME: snprintf() is locale dependent; but JSON requires
         * numbers to be formatted as if in the C locale. Dependence
         * on C locale is a pervasive issue in QEMU. */
        /* FIXME: This risks printing Inf or NaN, which are not valid
         * JSON values. */
        /* FIXME: the default precision of 6 for %f often causes
         * rounding errors; we should be using DBL_DECIMAL_DIG (17),
         * and only rounding to a shorter number if the result would
         * still produce the same floating point value.  */
        len = snprintf(buffer, sizeof(buffer), "%f", qfloat_get_double(val));
        while (len > 0 && buffer[len - 1] == '0') {
            len--;
        }

        if (len && buffer[len - 1] == '.') {
            buffer[len - 1] = 0;
        } else {
            buffer[len] = 0;
        }

        qstring_append(str, buffer);
        break;
    }
    case QTYPE_QBOOL: {
        QBool *val = qobject_to_qbool(obj);

        if (qbool_get_bool(val)) {
            qstring_append(str, "true");
        } else {
            qstring_append(str, "false");
        }
        break;
    }
    default:
        abort();
    }
}

QString *qobject_to_json(const QObject *obj)
{
    QString *str = qstring_new();

    to_json(obj, str, 0, 0);

    return str;
}

QString *qobject_to_json_pretty(const QObject *obj)
{
    QString *str = qstring_new();

    to_json(obj, str, 1, 0);

    return str;
}
