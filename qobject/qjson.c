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
#include "qapi/qmp/json-parser.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qstring.h"
#include "qemu/unicode.h"

typedef struct JSONParsingState
{
    JSONMessageParser parser;
    QObject *result;
    Error *err;
} JSONParsingState;

static void consume_json(void *opaque, QObject *json, Error *err)
{
    JSONParsingState *s = opaque;

    assert(!json != !err);
    assert(!s->result || !s->err);

    if (s->result) {
        qobject_unref(s->result);
        s->result = NULL;
        error_setg(&s->err, "Expecting at most one JSON value");
    }
    if (s->err) {
        qobject_unref(json);
        error_free(err);
        return;
    }
    s->result = json;
    s->err = err;
}

/*
 * Parse @string as JSON value.
 * If @ap is non-null, interpolate %-escapes.
 * Takes ownership of %p arguments.
 * On success, return the JSON value.
 * On failure, store an error through @errp and return NULL.
 * Ownership of %p arguments becomes indeterminate then.  To avoid
 * leaks, callers passing %p must terminate on error, e.g. by passing
 * &error_abort.
 */
static QObject *qobject_from_jsonv(const char *string, va_list *ap,
                                   Error **errp)
{
    JSONParsingState state = {};

    json_message_parser_init(&state.parser, consume_json, &state, ap);
    json_message_parser_feed(&state.parser, string, strlen(string));
    json_message_parser_flush(&state.parser);
    json_message_parser_destroy(&state.parser);

    if (!state.result && !state.err) {
        error_setg(&state.err, "Expecting a JSON value");
    }

    error_propagate(errp, state.err);
    return state.result;
}

QObject *qobject_from_json(const char *string, Error **errp)
{
    return qobject_from_jsonv(string, NULL, errp);
}

/*
 * Parse @string as JSON value with %-escapes interpolated.
 * Abort on error.  Do not use with untrusted @string.
 * Return the resulting QObject.  It is never null.
 */
QObject *qobject_from_vjsonf_nofail(const char *string, va_list ap)
{
    va_list ap_copy;
    QObject *obj;

    /* va_copy() is needed when va_list is an array type */
    va_copy(ap_copy, ap);
    obj = qobject_from_jsonv(string, &ap_copy, &error_abort);
    va_end(ap_copy);

    assert(obj);
    return obj;
}

/*
 * Parse @string as JSON value with %-escapes interpolated.
 * Abort on error.  Do not use with untrusted @string.
 * Return the resulting QObject.  It is never null.
 */
QObject *qobject_from_jsonf_nofail(const char *string, ...)
{
    QObject *obj;
    va_list ap;

    va_start(ap, string);
    obj = qobject_from_vjsonf_nofail(string, ap);
    va_end(ap);

    return obj;
}

/*
 * Parse @string as JSON object with %-escapes interpolated.
 * Abort on error.  Do not use with untrusted @string.
 * Return the resulting QDict.  It is never null.
 */
QDict *qdict_from_vjsonf_nofail(const char *string, va_list ap)
{
    QDict *qdict;

    qdict = qobject_to(QDict, qobject_from_vjsonf_nofail(string, ap));
    assert(qdict);
    return qdict;
}

/*
 * Parse @string as JSON object with %-escapes interpolated.
 * Abort on error.  Do not use with untrusted @string.
 * Return the resulting QDict.  It is never null.
 */
QDict *qdict_from_jsonf_nofail(const char *string, ...)
{
    QDict *qdict;
    va_list ap;

    va_start(ap, string);
    qdict = qdict_from_vjsonf_nofail(string, ap);
    va_end(ap);
    return qdict;
}

static void to_json(const QObject *obj, QString *str, int pretty, int indent);

static void json_pretty_newline(QString *str, bool pretty, int indent)
{
    int i;

    if (pretty) {
        qstring_append(str, "\n");
        for (i = 0; i < indent; i++) {
            qstring_append(str, "    ");
        }
    }
}

static void to_json(const QObject *obj, QString *str, int pretty, int indent)
{
    switch (qobject_type(obj)) {
    case QTYPE_QNULL:
        qstring_append(str, "null");
        break;
    case QTYPE_QNUM: {
        QNum *val = qobject_to(QNum, obj);
        char *buffer = qnum_to_string(val);
        qstring_append(str, buffer);
        g_free(buffer);
        break;
    }
    case QTYPE_QSTRING: {
        QString *val = qobject_to(QString, obj);
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
        QDict *val = qobject_to(QDict, obj);
        const char *comma = pretty ? "," : ", ";
        const char *sep = "";
        const QDictEntry *entry;
        QString *qkey;

        qstring_append(str, "{");

        for (entry = qdict_first(val);
             entry;
             entry = qdict_next(val, entry)) {
            qstring_append(str, sep);
            json_pretty_newline(str, pretty, indent + 1);

            qkey = qstring_from_str(qdict_entry_key(entry));
            to_json(QOBJECT(qkey), str, pretty, indent + 1);
            qobject_unref(qkey);

            qstring_append(str, ": ");
            to_json(qdict_entry_value(entry), str, pretty, indent + 1);
            sep = comma;
        }

        json_pretty_newline(str, pretty, indent);
        qstring_append(str, "}");
        break;
    }
    case QTYPE_QLIST: {
        QList *val = qobject_to(QList, obj);
        const char *comma = pretty ? "," : ", ";
        const char *sep = "";
        QListEntry *entry;

        qstring_append(str, "[");

        QLIST_FOREACH_ENTRY(val, entry) {
            qstring_append(str, sep);
            json_pretty_newline(str, pretty, indent + 1);
            to_json(qlist_entry_obj(entry), str, pretty, indent + 1);
            sep = comma;
        }

        json_pretty_newline(str, pretty, indent);
        qstring_append(str, "]");
        break;
    }
    case QTYPE_QBOOL: {
        QBool *val = qobject_to(QBool, obj);

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
