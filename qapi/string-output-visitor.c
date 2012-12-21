/*
 * String printing Visitor
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
#include "qapi/string-output-visitor.h"
#include "qapi/visitor-impl.h"
#include "qapi/qmp/qerror.h"

struct StringOutputVisitor
{
    Visitor visitor;
    char *string;
};

static void string_output_set(StringOutputVisitor *sov, char *string)
{
    g_free(sov->string);
    sov->string = string;
}

static void print_type_int(Visitor *v, int64_t *obj, const char *name,
                           Error **errp)
{
    StringOutputVisitor *sov = DO_UPCAST(StringOutputVisitor, visitor, v);
    string_output_set(sov, g_strdup_printf("%lld", (long long) *obj));
}

static void print_type_bool(Visitor *v, bool *obj, const char *name,
                            Error **errp)
{
    StringOutputVisitor *sov = DO_UPCAST(StringOutputVisitor, visitor, v);
    string_output_set(sov, g_strdup(*obj ? "true" : "false"));
}

static void print_type_str(Visitor *v, char **obj, const char *name,
                           Error **errp)
{
    StringOutputVisitor *sov = DO_UPCAST(StringOutputVisitor, visitor, v);
    string_output_set(sov, g_strdup(*obj ? *obj : ""));
}

static void print_type_number(Visitor *v, double *obj, const char *name,
                              Error **errp)
{
    StringOutputVisitor *sov = DO_UPCAST(StringOutputVisitor, visitor, v);
    string_output_set(sov, g_strdup_printf("%f", *obj));
}

char *string_output_get_string(StringOutputVisitor *sov)
{
    char *string = sov->string;
    sov->string = NULL;
    return string;
}

Visitor *string_output_get_visitor(StringOutputVisitor *sov)
{
    return &sov->visitor;
}

void string_output_visitor_cleanup(StringOutputVisitor *sov)
{
    g_free(sov->string);
    g_free(sov);
}

StringOutputVisitor *string_output_visitor_new(void)
{
    StringOutputVisitor *v;

    v = g_malloc0(sizeof(*v));

    v->visitor.type_enum = output_type_enum;
    v->visitor.type_int = print_type_int;
    v->visitor.type_bool = print_type_bool;
    v->visitor.type_str = print_type_str;
    v->visitor.type_number = print_type_number;

    return v;
}
