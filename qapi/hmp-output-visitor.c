/*
 * HMP string output Visitor
 *
 * Copyright Yandex N.V., 2021
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qapi/hmp-output-visitor.h"
#include "qapi/visitor-impl.h"

struct HMPOutputVisitor {
    Visitor visitor;
    char **result;
    GString *buffer;
    bool is_continue;
};

static HMPOutputVisitor *to_hov(Visitor *v)
{
    return container_of(v, HMPOutputVisitor, visitor);
}

static void hmp_output_append_formatted(Visitor *v, const char *fmt, ...)
{
    HMPOutputVisitor *ov = to_hov(v);
    va_list args;

    if (ov->is_continue) {
        g_string_append(ov->buffer, ",");
    } else {
        ov->is_continue = true;
    }

    va_start(args, fmt);
    g_string_append_vprintf(ov->buffer, fmt, args);
    va_end(args);
}

static void hmp_output_skip_comma(Visitor *v)
{
    HMPOutputVisitor *ov = to_hov(v);

    ov->is_continue = false;
}

static bool hmp_output_start_struct(Visitor *v, const char *name,
                                    void **obj, size_t unused, Error **errp)
{
    return true;
}

static void hmp_output_end_struct(Visitor *v, void **obj) {}

static bool hmp_output_start_list(Visitor *v, const char *name,
                                  GenericList **listp, size_t size,
                                  Error **errp)
{
    hmp_output_append_formatted(v, "%s=[", name);
    /* First element in array without comma before it */
    hmp_output_skip_comma(v);

    return true;
}

static GenericList *hmp_output_next_list(Visitor *v, GenericList *tail,
                                         size_t size)
{
    return tail->next;
}

static void hmp_output_end_list(Visitor *v, void **obj)
{
    /* Don't need comma after last array element */
    hmp_output_skip_comma(v);
    hmp_output_append_formatted(v, "]");
}

static bool hmp_output_type_int64(Visitor *v, const char *name,
                                  int64_t *obj, Error **errp)
{
    hmp_output_append_formatted(v, "%s=%" PRId64, name, *obj);

    return true;
}

static bool hmp_output_type_uint64(Visitor *v, const char *name,
                                   uint64_t *obj, Error **errp)
{
    hmp_output_append_formatted(v, "%s=%" PRIu64, name, *obj);

    return true;
}

static bool hmp_output_type_bool(Visitor *v, const char *name, bool *obj,
                                 Error **errp)
{
    hmp_output_append_formatted(v, "%s=%s", name, *obj ? "true" : "false");

    return true;
}

static bool hmp_output_type_str(Visitor *v, const char *name, char **obj,
                                Error **errp)
{
    /* Skip already printed or unused fields */
    if (!*obj || g_str_equal(name, "id") || g_str_equal(name, "type")) {
        return true;
    }

    /* Do not print stub name for StringList elements */
    if (g_str_equal(name, "str")) {
        hmp_output_append_formatted(v, "%s", *obj);
    } else {
        hmp_output_append_formatted(v, "%s=%s", name, *obj);
    }

    return true;
}

static bool hmp_output_type_number(Visitor *v, const char *name,
                                   double *obj, Error **errp)
{
    hmp_output_append_formatted(v, "%s=%.17g", name, *obj);

    return true;
}

/* TODO: remove this function? */
static bool hmp_output_type_any(Visitor *v, const char *name,
                                QObject **obj, Error **errp)
{
    return true;
}

static bool hmp_output_type_null(Visitor *v, const char *name,
                                 QNull **obj, Error **errp)
{
    hmp_output_append_formatted(v, "%s=NULL", name);

    return true;
}

static void hmp_output_complete(Visitor *v, void *opaque)
{
    HMPOutputVisitor *ov = to_hov(v);

    *ov->result = g_string_free(ov->buffer, false);
    ov->buffer = NULL;
}

static void hmp_output_free(Visitor *v)
{
    HMPOutputVisitor *ov = to_hov(v);

    if (ov->buffer) {
        g_string_free(ov->buffer, true);
    }
    g_free(v);
}

Visitor *hmp_output_visitor_new(char **result)
{
    HMPOutputVisitor *v;

    v = g_malloc0(sizeof(*v));

    v->visitor.type = VISITOR_OUTPUT;
    v->visitor.start_struct = hmp_output_start_struct;
    v->visitor.end_struct = hmp_output_end_struct;
    v->visitor.start_list = hmp_output_start_list;
    v->visitor.next_list = hmp_output_next_list;
    v->visitor.end_list = hmp_output_end_list;
    v->visitor.type_int64 = hmp_output_type_int64;
    v->visitor.type_uint64 = hmp_output_type_uint64;
    v->visitor.type_bool = hmp_output_type_bool;
    v->visitor.type_str = hmp_output_type_str;
    v->visitor.type_number = hmp_output_type_number;
    v->visitor.type_any = hmp_output_type_any;
    v->visitor.type_null = hmp_output_type_null;
    v->visitor.complete = hmp_output_complete;
    v->visitor.free = hmp_output_free;

    v->result = result;
    v->buffer = g_string_new("");
    v->is_continue = false;

    return &v->visitor;
}
