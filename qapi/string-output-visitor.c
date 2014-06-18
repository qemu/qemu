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
#include <math.h>
#include "qemu/range.h"

enum ListMode {
    LM_NONE,             /* not traversing a list of repeated options */
    LM_STARTED,          /* start_list() succeeded */

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

    LM_END
};

typedef enum ListMode ListMode;

struct StringOutputVisitor
{
    Visitor visitor;
    bool human;
    GString *string;
    bool head;
    ListMode list_mode;
    union {
        int64_t s;
        uint64_t u;
    } range_start, range_end;
    GList *ranges;
};

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
    r->begin = a;
    r->end = a + 1;
    sov->ranges = g_list_insert_sorted_merged(sov->ranges, r, range_compare);
}

static void string_output_append_range(StringOutputVisitor *sov,
                                       int64_t s, int64_t e)
{
    Range *r = g_malloc0(sizeof(*r));
    r->begin = s;
    r->end = e + 1;
    sov->ranges = g_list_insert_sorted_merged(sov->ranges, r, range_compare);
}

static void format_string(StringOutputVisitor *sov, Range *r, bool next,
                          bool human)
{
    if (r->end - r->begin > 1) {
        if (human) {
            g_string_append_printf(sov->string, "0x%" PRIx64 "-%" PRIx64,
                                   r->begin, r->end - 1);

        } else {
            g_string_append_printf(sov->string, "%" PRId64 "-%" PRId64,
                                   r->begin, r->end - 1);
        }
    } else {
        if (human) {
            g_string_append_printf(sov->string, "0x%" PRIx64, r->begin);
        } else {
            g_string_append_printf(sov->string, "%" PRId64, r->begin);
        }
    }
    if (next) {
        g_string_append(sov->string, ",");
    }
}

static void print_type_int(Visitor *v, int64_t *obj, const char *name,
                           Error **errp)
{
    StringOutputVisitor *sov = DO_UPCAST(StringOutputVisitor, visitor, v);
    GList *l;

    switch (sov->list_mode) {
    case LM_NONE:
        string_output_append(sov, *obj);
        break;

    case LM_STARTED:
        sov->range_start.s = *obj;
        sov->range_end.s = *obj;
        sov->list_mode = LM_IN_PROGRESS;
        return;

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
        return;

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
}

static void print_type_size(Visitor *v, uint64_t *obj, const char *name,
                           Error **errp)
{
    StringOutputVisitor *sov = DO_UPCAST(StringOutputVisitor, visitor, v);
    static const char suffixes[] = { 'B', 'K', 'M', 'G', 'T', 'P', 'E' };
    uint64_t div, val;
    char *out;
    int i;

    if (!sov->human) {
        out = g_strdup_printf("%"PRIu64, *obj);
        string_output_set(sov, out);
        return;
    }

    val = *obj;

    /* The exponent (returned in i) minus one gives us
     * floor(log2(val * 1024 / 1000).  The correction makes us
     * switch to the higher power when the integer part is >= 1000.
     */
    frexp(val / (1000.0 / 1024.0), &i);
    i = (i - 1) / 10;
    assert(i < ARRAY_SIZE(suffixes));
    div = 1ULL << (i * 10);

    out = g_strdup_printf("%"PRIu64" (%0.3g %c%s)", val,
                          (double)val/div, suffixes[i], i ? "iB" : "");
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

static void
start_list(Visitor *v, const char *name, Error **errp)
{
    StringOutputVisitor *sov = DO_UPCAST(StringOutputVisitor, visitor, v);

    /* we can't traverse a list in a list */
    assert(sov->list_mode == LM_NONE);
    sov->list_mode = LM_STARTED;
    sov->head = true;
}

static GenericList *
next_list(Visitor *v, GenericList **list, Error **errp)
{
    StringOutputVisitor *sov = DO_UPCAST(StringOutputVisitor, visitor, v);
    GenericList *ret = NULL;
    if (*list) {
        if (sov->head) {
            ret = *list;
        } else {
            ret = (*list)->next;
        }

        if (sov->head) {
            if (ret && ret->next == NULL) {
                sov->list_mode = LM_NONE;
            }
            sov->head = false;
        } else {
            if (ret && ret->next == NULL) {
                sov->list_mode = LM_END;
            }
        }
    }

    return ret;
}

static void
end_list(Visitor *v, Error **errp)
{
    StringOutputVisitor *sov = DO_UPCAST(StringOutputVisitor, visitor, v);

    assert(sov->list_mode == LM_STARTED ||
           sov->list_mode == LM_END ||
           sov->list_mode == LM_NONE ||
           sov->list_mode == LM_IN_PROGRESS);
    sov->list_mode = LM_NONE;
    sov->head = true;

}

char *string_output_get_string(StringOutputVisitor *sov)
{
    char *string = g_string_free(sov->string, false);
    sov->string = NULL;
    return string;
}

Visitor *string_output_get_visitor(StringOutputVisitor *sov)
{
    return &sov->visitor;
}

static void free_range(void *range, void *dummy)
{
    g_free(range);
}

void string_output_visitor_cleanup(StringOutputVisitor *sov)
{
    if (sov->string) {
        g_string_free(sov->string, true);
    }

    g_list_foreach(sov->ranges, free_range, NULL);
    g_list_free(sov->ranges);
    g_free(sov);
}

StringOutputVisitor *string_output_visitor_new(bool human)
{
    StringOutputVisitor *v;

    v = g_malloc0(sizeof(*v));

    v->string = g_string_new(NULL);
    v->human = human;
    v->visitor.type_enum = output_type_enum;
    v->visitor.type_int = print_type_int;
    v->visitor.type_size = print_type_size;
    v->visitor.type_bool = print_type_bool;
    v->visitor.type_str = print_type_str;
    v->visitor.type_number = print_type_number;
    v->visitor.start_list = start_list;
    v->visitor.next_list = next_list;
    v->visitor.end_list = end_list;

    return v;
}
