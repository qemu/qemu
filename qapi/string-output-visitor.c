/*
 * String printing Visitor
 *
 * Copyright Red Hat, Inc. 2012-2016
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qapi/string-output-visitor.h"
#include "qapi/visitor-impl.h"
#include "qemu/host-utils.h"
#include <math.h>
#include "qemu/range.h"

enum ListMode {
    LM_NONE,             /* not traversing a list of repeated options */
    LM_STARTED,          /* next_list() ready to be called */

    LM_IN_PROGRESS,      /* next_list() has been called.
                          *
                          * Generating the next list link will consume the most
                          * recently parsed QemuOpt instance of the repeated
                          * option.
                          *
                          * Parsing a value into the list link will examine the
                          * next QemuOpt instance of the repeated option, and
                          * possibly enter LM_SIGNED_INTERVAL or
                          * LM_UNSIGNED_INTERVAL.
                          */

    LM_SIGNED_INTERVAL,  /* next_list() has been called.
                          *
                          * Generating the next list link will consume the most
                          * recently stored element from the signed interval,
                          * parsed from the most recent QemuOpt instance of the
                          * repeated option. This may consume QemuOpt itself
                          * and return to LM_IN_PROGRESS.
                          *
                          * Parsing a value into the list link will store the
                          * next element of the signed interval.
                          */

    LM_UNSIGNED_INTERVAL,/* Same as above, only for an unsigned interval. */

    LM_END,              /* next_list() called, about to see last element. */
};

typedef enum ListMode ListMode;

struct StringOutputVisitor
{
    Visitor visitor;
    bool human;
    GString *string;
    char **result;
    ListMode list_mode;
    union {
        int64_t s;
        uint64_t u;
    } range_start, range_end;
    GList *ranges;
    void *list; /* Only needed for sanity checking the caller */
};

static StringOutputVisitor *to_sov(Visitor *v)
{
    return container_of(v, StringOutputVisitor, visitor);
}

static void string_output_set(StringOutputVisitor *sov, char *string)
{
    if (sov->string) {
        g_string_free(sov->string, true);
    }
    sov->string = g_string_new(string);
    g_free(string);
}

static void string_output_append(StringOutputVisitor *sov, int64_t a)
{
    Range *r = g_malloc0(sizeof(*r));

    range_set_bounds(r, a, a);
    sov->ranges = range_list_insert(sov->ranges, r);
}

static void string_output_append_range(StringOutputVisitor *sov,
                                       int64_t s, int64_t e)
{
    Range *r = g_malloc0(sizeof(*r));

    range_set_bounds(r, s, e);
    sov->ranges = range_list_insert(sov->ranges, r);
}

static void format_string(StringOutputVisitor *sov, Range *r, bool next,
                          bool human)
{
    if (range_lob(r) != range_upb(r)) {
        if (human) {
            g_string_append_printf(sov->string, "0x%" PRIx64 "-0x%" PRIx64,
                                   range_lob(r), range_upb(r));

        } else {
            g_string_append_printf(sov->string, "%" PRId64 "-%" PRId64,
                                   range_lob(r), range_upb(r));
        }
    } else {
        if (human) {
            g_string_append_printf(sov->string, "0x%" PRIx64, range_lob(r));
        } else {
            g_string_append_printf(sov->string, "%" PRId64, range_lob(r));
        }
    }
    if (next) {
        g_string_append(sov->string, ",");
    }
}

static bool print_type_int64(Visitor *v, const char *name, int64_t *obj,
                             Error **errp)
{
    StringOutputVisitor *sov = to_sov(v);
    GList *l;

    switch (sov->list_mode) {
    case LM_NONE:
        string_output_append(sov, *obj);
        break;

    case LM_STARTED:
        sov->range_start.s = *obj;
        sov->range_end.s = *obj;
        sov->list_mode = LM_IN_PROGRESS;
        return true;

    case LM_IN_PROGRESS:
        if (sov->range_end.s + 1 == *obj) {
            sov->range_end.s++;
        } else {
            if (sov->range_start.s == sov->range_end.s) {
                string_output_append(sov, sov->range_end.s);
            } else {
                assert(sov->range_start.s < sov->range_end.s);
                string_output_append_range(sov, sov->range_start.s,
                                           sov->range_end.s);
            }

            sov->range_start.s = *obj;
            sov->range_end.s = *obj;
        }
        return true;

    case LM_END:
        if (sov->range_end.s + 1 == *obj) {
            sov->range_end.s++;
            assert(sov->range_start.s < sov->range_end.s);
            string_output_append_range(sov, sov->range_start.s,
                                       sov->range_end.s);
        } else {
            if (sov->range_start.s == sov->range_end.s) {
                string_output_append(sov, sov->range_end.s);
            } else {
                assert(sov->range_start.s < sov->range_end.s);

                string_output_append_range(sov, sov->range_start.s,
                                           sov->range_end.s);
            }
            string_output_append(sov, *obj);
        }
        break;

    default:
        abort();
    }

    l = sov->ranges;
    while (l) {
        Range *r = l->data;
        format_string(sov, r, l->next != NULL, false);
        l = l->next;
    }

    if (sov->human) {
        l = sov->ranges;
        g_string_append(sov->string, " (");
        while (l) {
            Range *r = l->data;
            format_string(sov, r, l->next != NULL, true);
            l = l->next;
        }
        g_string_append(sov->string, ")");
    }

    return true;
}

static bool print_type_uint64(Visitor *v, const char *name, uint64_t *obj,
                             Error **errp)
{
    /* FIXME: print_type_int64 mishandles values over INT64_MAX */
    int64_t i = *obj;
    return print_type_int64(v, name, &i, errp);
}

static bool print_type_size(Visitor *v, const char *name, uint64_t *obj,
                            Error **errp)
{
    StringOutputVisitor *sov = to_sov(v);
    uint64_t val;
    char *out, *psize;

    if (!sov->human) {
        out = g_strdup_printf("%"PRIu64, *obj);
        string_output_set(sov, out);
        return true;
    }

    val = *obj;
    psize = size_to_str(val);
    out = g_strdup_printf("%"PRIu64" (%s)", val, psize);
    string_output_set(sov, out);

    g_free(psize);
    return true;
}

static bool print_type_bool(Visitor *v, const char *name, bool *obj,
                            Error **errp)
{
    StringOutputVisitor *sov = to_sov(v);
    string_output_set(sov, g_strdup(*obj ? "true" : "false"));
    return true;
}

static bool print_type_str(Visitor *v, const char *name, char **obj,
                           Error **errp)
{
    StringOutputVisitor *sov = to_sov(v);
    char *out;

    if (sov->human) {
        out = *obj ? g_strdup_printf("\"%s\"", *obj) : g_strdup("<null>");
    } else {
        out = g_strdup(*obj ? *obj : "");
    }
    string_output_set(sov, out);
    return true;
}

static bool print_type_number(Visitor *v, const char *name, double *obj,
                              Error **errp)
{
    StringOutputVisitor *sov = to_sov(v);
    string_output_set(sov, g_strdup_printf("%.17g", *obj));
    return true;
}

static bool print_type_null(Visitor *v, const char *name, QNull **obj,
                            Error **errp)
{
    StringOutputVisitor *sov = to_sov(v);
    char *out;

    if (sov->human) {
        out = g_strdup("<null>");
    } else {
        out = g_strdup("");
    }
    string_output_set(sov, out);
    return true;
}

static bool
start_list(Visitor *v, const char *name, GenericList **list, size_t size,
           Error **errp)
{
    StringOutputVisitor *sov = to_sov(v);

    /* we can't traverse a list in a list */
    assert(sov->list_mode == LM_NONE);
    /* We don't support visits without a list */
    assert(list);
    sov->list = list;
    /* List handling is only needed if there are at least two elements */
    if (*list && (*list)->next) {
        sov->list_mode = LM_STARTED;
    }
    return true;
}

static GenericList *next_list(Visitor *v, GenericList *tail, size_t size)
{
    StringOutputVisitor *sov = to_sov(v);
    GenericList *ret = tail->next;

    if (ret && !ret->next) {
        sov->list_mode = LM_END;
    }
    return ret;
}

static void end_list(Visitor *v, void **obj)
{
    StringOutputVisitor *sov = to_sov(v);

    assert(sov->list == obj);
    assert(sov->list_mode == LM_STARTED ||
           sov->list_mode == LM_END ||
           sov->list_mode == LM_NONE ||
           sov->list_mode == LM_IN_PROGRESS);
    sov->list_mode = LM_NONE;
}

static void string_output_complete(Visitor *v, void *opaque)
{
    StringOutputVisitor *sov = to_sov(v);

    assert(opaque == sov->result);
    *sov->result = g_string_free(sov->string, false);
    sov->string = NULL;
}

static void free_range(void *range, void *dummy)
{
    g_free(range);
}

static void string_output_free(Visitor *v)
{
    StringOutputVisitor *sov = to_sov(v);

    if (sov->string) {
        g_string_free(sov->string, true);
    }

    g_list_foreach(sov->ranges, free_range, NULL);
    g_list_free(sov->ranges);
    g_free(sov);
}

Visitor *string_output_visitor_new(bool human, char **result)
{
    StringOutputVisitor *v;

    v = g_malloc0(sizeof(*v));

    v->string = g_string_new(NULL);
    v->human = human;
    v->result = result;
    *result = NULL;

    v->visitor.type = VISITOR_OUTPUT;
    v->visitor.type_int64 = print_type_int64;
    v->visitor.type_uint64 = print_type_uint64;
    v->visitor.type_size = print_type_size;
    v->visitor.type_bool = print_type_bool;
    v->visitor.type_str = print_type_str;
    v->visitor.type_number = print_type_number;
    v->visitor.type_null = print_type_null;
    v->visitor.start_list = start_list;
    v->visitor.next_list = next_list;
    v->visitor.end_list = end_list;
    v->visitor.complete = string_output_complete;
    v->visitor.free = string_output_free;

    return &v->visitor;
}
