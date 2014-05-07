/*
 * String parsing visitor
 *
 * Copyright Red Hat, Inc. 2012
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "qapi/string-input-visitor.h"
#include "qapi/visitor-impl.h"
#include "qapi/qmp/qerror.h"
#include "qemu/option.h"

struct StringInputVisitor
{
    Visitor visitor;
    const char *string;
};

static void parse_type_int(Visitor *v, int64_t *obj, const char *name,
                           Error **errp)
{
    StringInputVisitor *siv = DO_UPCAST(StringInputVisitor, visitor, v);
    char *endp = (char *) siv->string;
    long long val;

    errno = 0;
    if (siv->string) {
        val = strtoll(siv->string, &endp, 0);
    }
    if (!siv->string || errno || endp == siv->string || *endp) {
        error_set(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                  "integer");
        return;
    }

    *obj = val;
}

static void parse_type_size(Visitor *v, uint64_t *obj, const char *name,
                            Error **errp)
{
    StringInputVisitor *siv = DO_UPCAST(StringInputVisitor, visitor, v);
    Error *err = NULL;
    uint64_t val;

    if (siv->string) {
        parse_option_size(name, siv->string, &val, &err);
    } else {
        error_set(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                  "size");
        return;
    }
    if (err) {
        error_propagate(errp, err);
        return;
    }

    *obj = val;
}

static void parse_type_bool(Visitor *v, bool *obj, const char *name,
                            Error **errp)
{
    StringInputVisitor *siv = DO_UPCAST(StringInputVisitor, visitor, v);

    if (siv->string) {
        if (!strcasecmp(siv->string, "on") ||
            !strcasecmp(siv->string, "yes") ||
            !strcasecmp(siv->string, "true")) {
            *obj = true;
            return;
        }
        if (!strcasecmp(siv->string, "off") ||
            !strcasecmp(siv->string, "no") ||
            !strcasecmp(siv->string, "false")) {
            *obj = false;
            return;
        }
    }

    error_set(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
              "boolean");
}

static void parse_type_str(Visitor *v, char **obj, const char *name,
                           Error **errp)
{
    StringInputVisitor *siv = DO_UPCAST(StringInputVisitor, visitor, v);
    if (siv->string) {
        *obj = g_strdup(siv->string);
    } else {
        error_set(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                  "string");
    }
}

static void parse_type_number(Visitor *v, double *obj, const char *name,
                              Error **errp)
{
    StringInputVisitor *siv = DO_UPCAST(StringInputVisitor, visitor, v);
    char *endp = (char *) siv->string;
    double val;

    errno = 0;
    if (siv->string) {
        val = strtod(siv->string, &endp);
    }
    if (!siv->string || errno || endp == siv->string || *endp) {
        error_set(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                  "number");
        return;
    }

    *obj = val;
}

static void parse_optional(Visitor *v, bool *present, const char *name,
                           Error **errp)
{
    StringInputVisitor *siv = DO_UPCAST(StringInputVisitor, visitor, v);

    if (!siv->string) {
        *present = false;
        return;
    }

    *present = true;
}

Visitor *string_input_get_visitor(StringInputVisitor *v)
{
    return &v->visitor;
}

void string_input_visitor_cleanup(StringInputVisitor *v)
{
    g_free(v);
}

StringInputVisitor *string_input_visitor_new(const char *str)
{
    StringInputVisitor *v;

    v = g_malloc0(sizeof(*v));

    v->visitor.type_enum = input_type_enum;
    v->visitor.type_int = parse_type_int;
    v->visitor.type_size = parse_type_size;
    v->visitor.type_bool = parse_type_bool;
    v->visitor.type_str = parse_type_str;
    v->visitor.type_number = parse_type_number;
    v->visitor.optional = parse_optional;

    v->string = str;
    return v;
}
