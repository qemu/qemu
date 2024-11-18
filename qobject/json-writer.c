/*
 * JSON Writer
 *
 * Copyright IBM, Corp. 2009
 * Copyright (c) 2010-2020 Red Hat Inc.
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Markus Armbruster <armbru@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qobject/json-writer.h"
#include "qemu/unicode.h"

struct JSONWriter {
    bool pretty;
    bool need_comma;
    GString *contents;
    GByteArray *container_is_array;
};

JSONWriter *json_writer_new(bool pretty)
{
    JSONWriter *writer = g_new(JSONWriter, 1);

    writer->pretty = pretty;
    writer->need_comma = false;
    writer->contents = g_string_new(NULL);
    writer->container_is_array = g_byte_array_new();
    return writer;
}

const char *json_writer_get(JSONWriter *writer)
{
    g_assert(!writer->container_is_array->len);
    return writer->contents->str;
}

GString *json_writer_get_and_free(JSONWriter *writer)
{
    GString *contents = writer->contents;

    writer->contents = NULL;
    g_byte_array_free(writer->container_is_array, true);
    g_free(writer);
    return contents;
}

void json_writer_free(JSONWriter *writer)
{
    if (writer) {
        g_string_free(json_writer_get_and_free(writer), true);
    }
}

static void enter_container(JSONWriter *writer, bool is_array)
{
    unsigned depth = writer->container_is_array->len;

    g_byte_array_set_size(writer->container_is_array, depth + 1);
    writer->container_is_array->data[depth] = is_array;
    writer->need_comma = false;
}

static void leave_container(JSONWriter *writer, bool is_array)
{
    unsigned depth = writer->container_is_array->len;

    assert(depth);
    assert(writer->container_is_array->data[depth - 1] == is_array);
    g_byte_array_set_size(writer->container_is_array, depth - 1);
    writer->need_comma = true;
}

static bool in_object(JSONWriter *writer)
{
    unsigned depth = writer->container_is_array->len;

    return depth && !writer->container_is_array->data[depth - 1];
}

static void pretty_newline(JSONWriter *writer)
{
    if (writer->pretty) {
        g_string_append_printf(writer->contents, "\n%*s",
                               writer->container_is_array->len * 4, "");
    }
}

static void pretty_newline_or_space(JSONWriter *writer)
{
    if (writer->pretty) {
        g_string_append_printf(writer->contents, "\n%*s",
                               writer->container_is_array->len * 4, "");
    } else {
        g_string_append_c(writer->contents, ' ');
    }
}

static void quoted_str(JSONWriter *writer, const char *str)
{
    const char *ptr;
    char *end;
    int cp;

    g_string_append_c(writer->contents, '"');

    for (ptr = str; *ptr; ptr = end) {
        cp = mod_utf8_codepoint(ptr, 6, &end);
        switch (cp) {
        case '\"':
            g_string_append(writer->contents, "\\\"");
            break;
        case '\\':
            g_string_append(writer->contents, "\\\\");
            break;
        case '\b':
            g_string_append(writer->contents, "\\b");
            break;
        case '\f':
            g_string_append(writer->contents, "\\f");
            break;
        case '\n':
            g_string_append(writer->contents, "\\n");
            break;
        case '\r':
            g_string_append(writer->contents, "\\r");
            break;
        case '\t':
            g_string_append(writer->contents, "\\t");
            break;
        default:
            if (cp < 0) {
                cp = 0xFFFD; /* replacement character */
            }
            if (cp > 0xFFFF) {
                /* beyond BMP; need a surrogate pair */
                g_string_append_printf(writer->contents, "\\u%04X\\u%04X",
                                       0xD800 + ((cp - 0x10000) >> 10),
                                       0xDC00 + ((cp - 0x10000) & 0x3FF));
            } else if (cp < 0x20 || cp >= 0x7F) {
                g_string_append_printf(writer->contents, "\\u%04X", cp);
            } else {
                g_string_append_c(writer->contents, cp);
            }
        }
    };

    g_string_append_c(writer->contents, '"');
}

static void maybe_comma_name(JSONWriter *writer, const char *name)
{
    if (writer->need_comma) {
        g_string_append_c(writer->contents, ',');
        pretty_newline_or_space(writer);
    } else {
        if (writer->contents->len) {
            pretty_newline(writer);
        }
        writer->need_comma = true;
    }

    if (in_object(writer)) {
        quoted_str(writer, name);
        g_string_append(writer->contents, ": ");
    }
}

void json_writer_start_object(JSONWriter *writer, const char *name)
{
    maybe_comma_name(writer, name);
    g_string_append_c(writer->contents, '{');
    enter_container(writer, false);
}

void json_writer_end_object(JSONWriter *writer)
{
    leave_container(writer, false);
    pretty_newline(writer);
    g_string_append_c(writer->contents, '}');
}

void json_writer_start_array(JSONWriter *writer, const char *name)
{
    maybe_comma_name(writer, name);
    g_string_append_c(writer->contents, '[');
    enter_container(writer, true);
}

void json_writer_end_array(JSONWriter *writer)
{
    leave_container(writer, true);
    pretty_newline(writer);
    g_string_append_c(writer->contents, ']');
}

void json_writer_bool(JSONWriter *writer, const char *name, bool val)
{
    maybe_comma_name(writer, name);
    g_string_append(writer->contents, val ? "true" : "false");
}

void json_writer_null(JSONWriter *writer, const char *name)
{
    maybe_comma_name(writer, name);
    g_string_append(writer->contents, "null");
}

void json_writer_int64(JSONWriter *writer, const char *name, int64_t val)
{
    maybe_comma_name(writer, name);
    g_string_append_printf(writer->contents, "%" PRId64, val);
}

void json_writer_uint64(JSONWriter *writer, const char *name, uint64_t val)
{
    maybe_comma_name(writer, name);
    g_string_append_printf(writer->contents, "%" PRIu64, val);
}

void json_writer_double(JSONWriter *writer, const char *name, double val)
{
    maybe_comma_name(writer, name);

    /*
     * FIXME: g_string_append_printf() is locale dependent; but JSON
     * requires numbers to be formatted as if in the C locale.
     * Dependence on C locale is a pervasive issue in QEMU.
     */
    /*
     * FIXME: This risks printing Inf or NaN, which are not valid
     * JSON values.
     */
    g_string_append_printf(writer->contents, "%.17g", val);
}

void json_writer_str(JSONWriter *writer, const char *name, const char *str)
{
    maybe_comma_name(writer, name);
    quoted_str(writer, str);
}
