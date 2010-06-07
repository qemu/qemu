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

#include "json-lexer.h"
#include "json-parser.h"
#include "json-streamer.h"
#include "qjson.h"
#include "qint.h"
#include "qlist.h"
#include "qbool.h"
#include "qfloat.h"
#include "qdict.h"

typedef struct JSONParsingState
{
    JSONMessageParser parser;
    va_list *ap;
    QObject *result;
} JSONParsingState;

static void parse_json(JSONMessageParser *parser, QList *tokens)
{
    JSONParsingState *s = container_of(parser, JSONParsingState, parser);
    s->result = json_parser_parse(tokens, s->ap);
}

QObject *qobject_from_jsonv(const char *string, va_list *ap)
{
    JSONParsingState state = {};

    state.ap = ap;

    json_message_parser_init(&state.parser, parse_json);
    json_message_parser_feed(&state.parser, string, strlen(string));
    json_message_parser_flush(&state.parser);
    json_message_parser_destroy(&state.parser);

    return state.result;
}

QObject *qobject_from_json(const char *string)
{
    return qobject_from_jsonv(string, NULL);
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
    obj = qobject_from_jsonv(string, &ap);
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

    if (s->count)
        qstring_append(s->str, ", ");

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

    if (s->count)
        qstring_append(s->str, ", ");

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

        ptr = qstring_get_str(val);
        qstring_append(str, "\"");
        while (*ptr) {
            if ((ptr[0] & 0xE0) == 0xE0 &&
                (ptr[1] & 0x80) && (ptr[2] & 0x80)) {
                uint16_t wchar;
                char escape[7];

                wchar  = (ptr[0] & 0x0F) << 12;
                wchar |= (ptr[1] & 0x3F) << 6;
                wchar |= (ptr[2] & 0x3F);
                ptr += 2;

                snprintf(escape, sizeof(escape), "\\u%04X", wchar);
                qstring_append(str, escape);
            } else if ((ptr[0] & 0xE0) == 0xC0 && (ptr[1] & 0x80)) {
                uint16_t wchar;
                char escape[7];

                wchar  = (ptr[0] & 0x1F) << 6;
                wchar |= (ptr[1] & 0x3F);
                ptr++;

                snprintf(escape, sizeof(escape), "\\u%04X", wchar);
                qstring_append(str, escape);
            } else switch (ptr[0]) {
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
                default: {
                    if (ptr[0] <= 0x1F) {
                        char escape[7];
                        snprintf(escape, sizeof(escape), "\\u%04X", ptr[0]);
                        qstring_append(str, escape);
                    } else {
                        char buf[2] = { ptr[0], 0 };
                        qstring_append(str, buf);
                    }
                    break;
                }
                }
            ptr++;
        }
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

        if (qbool_get_int(val)) {
            qstring_append(str, "true");
        } else {
            qstring_append(str, "false");
        }
        break;
    }
    case QTYPE_QERROR:
        /* XXX: should QError be emitted? */
    case QTYPE_NONE:
        break;
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
