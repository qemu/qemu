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
#include "qapi/qmp/json-writer.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qstring.h"

typedef struct JSONParsingState {
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

static void to_json(JSONWriter *writer, const char *name,
                    const QObject *obj)
{
    switch (qobject_type(obj)) {
    case QTYPE_QNULL:
        json_writer_null(writer, name);
        break;
    case QTYPE_QNUM: {
        QNum *val = qobject_to(QNum, obj);

        switch (val->kind) {
        case QNUM_I64:
            json_writer_int64(writer, name, val->u.i64);
            break;
        case QNUM_U64:
            json_writer_uint64(writer, name, val->u.u64);
            break;
        case QNUM_DOUBLE:
            json_writer_double(writer, name, val->u.dbl);
            break;
        default:
            abort();
        }
        break;
    }
    case QTYPE_QSTRING: {
        QString *val = qobject_to(QString, obj);

        json_writer_str(writer, name, qstring_get_str(val));
        break;
    }
    case QTYPE_QDICT: {
        QDict *val = qobject_to(QDict, obj);
        const QDictEntry *entry;

        json_writer_start_object(writer, name);

        for (entry = qdict_first(val);
             entry;
             entry = qdict_next(val, entry)) {
            to_json(writer, qdict_entry_key(entry), qdict_entry_value(entry));
        }

        json_writer_end_object(writer);
        break;
    }
    case QTYPE_QLIST: {
        QList *val = qobject_to(QList, obj);
        QListEntry *entry;

        json_writer_start_array(writer, name);

        QLIST_FOREACH_ENTRY(val, entry) {
            to_json(writer, NULL, qlist_entry_obj(entry));
        }

        json_writer_end_array(writer);
        break;
    }
    case QTYPE_QBOOL: {
        QBool *val = qobject_to(QBool, obj);

        json_writer_bool(writer, name, qbool_get_bool(val));
        break;
    }
    default:
        abort();
    }
}

GString *qobject_to_json_pretty(const QObject *obj, bool pretty)
{
    JSONWriter *writer = json_writer_new(pretty);

    to_json(writer, NULL, obj);
    return json_writer_get_and_free(writer);
}

GString *qobject_to_json(const QObject *obj)
{
    return qobject_to_json_pretty(obj, false);
}
