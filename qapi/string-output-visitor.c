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
#include "qemu/host-utils.h"

struct StringOutputVisitor
{
    Visitor visitor;
    bool human;
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
    char *out;

    if (sov->human) {
        out = g_strdup_printf("%lld (%#llx)", (long long) *obj, (long long) *obj);
    } else {
        out = g_strdup_printf("%lld", (long long) *obj);
    }
    string_output_set(sov, out);
}

static void print_type_size(Visitor *v, uint64_t *obj, const char *name,
                           Error **errp)
{
    StringOutputVisitor *sov = DO_UPCAST(StringOutputVisitor, visitor, v);
    static const char suffixes[] = { 'B', 'K', 'M', 'G', 'T' };
    uint64_t div, val;
    char *out;
    int i;

    if (!sov->human) {
        out = g_strdup_printf("%llu", (long long) *obj);
        string_output_set(sov, out);
        return;
    }

    val = *obj;

    /* Compute floor(log2(val)).  */
    i = 64 - clz64(val);

    /* Find the power of 1024 that we'll display as the units.  */
    i /= 10;
    if (i >= ARRAY_SIZE(suffixes)) {
        i = ARRAY_SIZE(suffixes) - 1;
    }
    div = 1ULL << (i * 10);

    out = g_strdup_printf("%0.03f%c", (double)val/div, suffixes[i]);
    string_output_set(sov, out);
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
    char *out;

    if (sov->human) {
        out = *obj ? g_strdup_printf("\"%s\"", *obj) : g_strdup("<null>");
    } else {
        out = g_strdup(*obj ? *obj : "");
    }
    string_output_set(sov, out);
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

StringOutputVisitor *string_output_visitor_new(bool human)
{
    StringOutputVisitor *v;

    v = g_malloc0(sizeof(*v));

    v->human = human;
    v->visitor.type_enum = output_type_enum;
    v->visitor.type_int = print_type_int;
    v->visitor.type_size = print_type_size;
    v->visitor.type_bool = print_type_bool;
    v->visitor.type_str = print_type_str;
    v->visitor.type_number = print_type_number;

    return v;
}
