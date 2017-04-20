/*
 * A simple JSON writer
 *
 * Copyright Alexander Graf
 *
 * Authors:
 *  Alexander Graf <agraf@suse.de>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

/*
 * Type QJSON lets you build JSON text.  Its interface mirrors (a
 * subset of) abstract JSON syntax.
 *
 * It does *not* detect incorrect use.  It happily produces invalid
 * JSON then.  This is what migration wants.
 *
 * QAPI output visitors also produce JSON text.  However, they do
 * assert their preconditions and invariants, and therefore abort on
 * incorrect use.
 */

#include "qemu/osdep.h"
#include "qapi/qmp/qstring.h"
#include "qjson.h"

struct QJSON {
    QString *str;
    bool omit_comma;
};

static void json_emit_element(QJSON *json, const char *name)
{
    /* Check whether we need to print a , before an element */
    if (json->omit_comma) {
        json->omit_comma = false;
    } else {
        qstring_append(json->str, ", ");
    }

    if (name) {
        qstring_append(json->str, "\"");
        qstring_append(json->str, name);
        qstring_append(json->str, "\" : ");
    }
}

void json_start_object(QJSON *json, const char *name)
{
    json_emit_element(json, name);
    qstring_append(json->str, "{ ");
    json->omit_comma = true;
}

void json_end_object(QJSON *json)
{
    qstring_append(json->str, " }");
    json->omit_comma = false;
}

void json_start_array(QJSON *json, const char *name)
{
    json_emit_element(json, name);
    qstring_append(json->str, "[ ");
    json->omit_comma = true;
}

void json_end_array(QJSON *json)
{
    qstring_append(json->str, " ]");
    json->omit_comma = false;
}

void json_prop_int(QJSON *json, const char *name, int64_t val)
{
    json_emit_element(json, name);
    qstring_append_int(json->str, val);
}

void json_prop_str(QJSON *json, const char *name, const char *str)
{
    json_emit_element(json, name);
    qstring_append_chr(json->str, '"');
    qstring_append(json->str, str);
    qstring_append_chr(json->str, '"');
}

const char *qjson_get_str(QJSON *json)
{
    return qstring_get_str(json->str);
}

QJSON *qjson_new(void)
{
    QJSON *json = g_new0(QJSON, 1);

    json->str = qstring_from_str("{ ");
    json->omit_comma = true;
    return json;
}

void qjson_finish(QJSON *json)
{
    json_end_object(json);
}

void qjson_destroy(QJSON *json)
{
    QDECREF(json->str);
    g_free(json);
}
