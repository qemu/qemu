/*
 * String parsing visitor
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
#include "qapi/error.h"
#include "qemu-common.h"
#include "qapi/string-input-visitor.h"
#include "qapi/visitor-impl.h"
#include "qapi/qmp/qerror.h"
#include "qemu/option.h"
#include "qemu/queue.h"
#include "qemu/range.h"


struct StringInputVisitor
{
    Visitor visitor;

    bool head;

    GList *ranges;
    GList *cur_range;
    int64_t cur;

    const char *string;
};

static StringInputVisitor *to_siv(Visitor *v)
{
    return container_of(v, StringInputVisitor, visitor);
}

static void free_range(void *range, void *dummy)
{
    g_free(range);
}

static void parse_str(StringInputVisitor *siv, Error **errp)
{
    char *str = (char *) siv->string;
    long long start, end;
    Range *cur;
    char *endptr;

    if (siv->ranges) {
        return;
    }

    do {
        errno = 0;
        start = strtoll(str, &endptr, 0);
        if (errno == 0 && endptr > str) {
            if (*endptr == '\0') {
                cur = g_malloc0(sizeof(*cur));
                cur->begin = start;
                cur->end = start + 1;
                siv->ranges = g_list_insert_sorted_merged(siv->ranges, cur,
                                                          range_compare);
                cur = NULL;
                str = NULL;
            } else if (*endptr == '-') {
                str = endptr + 1;
                errno = 0;
                end = strtoll(str, &endptr, 0);
                if (errno == 0 && endptr > str && start <= end &&
                    (start > INT64_MAX - 65536 ||
                     end < start + 65536)) {
                    if (*endptr == '\0') {
                        cur = g_malloc0(sizeof(*cur));
                        cur->begin = start;
                        cur->end = end + 1;
                        siv->ranges =
                            g_list_insert_sorted_merged(siv->ranges,
                                                        cur,
                                                        range_compare);
                        cur = NULL;
                        str = NULL;
                    } else if (*endptr == ',') {
                        str = endptr + 1;
                        cur = g_malloc0(sizeof(*cur));
                        cur->begin = start;
                        cur->end = end + 1;
                        siv->ranges =
                            g_list_insert_sorted_merged(siv->ranges,
                                                        cur,
                                                        range_compare);
                        cur = NULL;
                    } else {
                        goto error;
                    }
                } else {
                    goto error;
                }
            } else if (*endptr == ',') {
                str = endptr + 1;
                cur = g_malloc0(sizeof(*cur));
                cur->begin = start;
                cur->end = start + 1;
                siv->ranges = g_list_insert_sorted_merged(siv->ranges,
                                                          cur,
                                                          range_compare);
                cur = NULL;
            } else {
                goto error;
            }
        } else {
            goto error;
        }
    } while (str);

    return;
error:
    g_list_foreach(siv->ranges, free_range, NULL);
    g_list_free(siv->ranges);
    siv->ranges = NULL;
}

static void
start_list(Visitor *v, const char *name, Error **errp)
{
    StringInputVisitor *siv = to_siv(v);

    parse_str(siv, errp);

    siv->cur_range = g_list_first(siv->ranges);
    if (siv->cur_range) {
        Range *r = siv->cur_range->data;
        if (r) {
            siv->cur = r->begin;
        }
    }
}

static GenericList *next_list(Visitor *v, GenericList **list, size_t size)
{
    StringInputVisitor *siv = to_siv(v);
    GenericList **link;
    Range *r;

    if (!siv->ranges || !siv->cur_range) {
        return NULL;
    }

    r = siv->cur_range->data;
    if (!r) {
        return NULL;
    }

    if (siv->cur < r->begin || siv->cur >= r->end) {
        siv->cur_range = g_list_next(siv->cur_range);
        if (!siv->cur_range) {
            return NULL;
        }
        r = siv->cur_range->data;
        if (!r) {
            return NULL;
        }
        siv->cur = r->begin;
    }

    if (siv->head) {
        link = list;
        siv->head = false;
    } else {
        link = &(*list)->next;
    }

    *link = g_malloc0(size);
    return *link;
}

static void end_list(Visitor *v)
{
    StringInputVisitor *siv = to_siv(v);
    siv->head = true;
}

static void parse_type_int64(Visitor *v, const char *name, int64_t *obj,
                             Error **errp)
{
    StringInputVisitor *siv = to_siv(v);

    if (!siv->string) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                   "integer");
        return;
    }

    parse_str(siv, errp);

    if (!siv->ranges) {
        goto error;
    }

    if (!siv->cur_range) {
        Range *r;

        siv->cur_range = g_list_first(siv->ranges);
        if (!siv->cur_range) {
            goto error;
        }

        r = siv->cur_range->data;
        if (!r) {
            goto error;
        }

        siv->cur = r->begin;
    }

    *obj = siv->cur;
    siv->cur++;
    return;

error:
    error_setg(errp, QERR_INVALID_PARAMETER_VALUE, name ? name : "null",
               "an int64 value or range");
}

static void parse_type_uint64(Visitor *v, const char *name, uint64_t *obj,
                              Error **errp)
{
    /* FIXME: parse_type_int64 mishandles values over INT64_MAX */
    int64_t i;
    Error *err = NULL;
    parse_type_int64(v, name, &i, &err);
    if (err) {
        error_propagate(errp, err);
    } else {
        *obj = i;
    }
}

static void parse_type_size(Visitor *v, const char *name, uint64_t *obj,
                            Error **errp)
{
    StringInputVisitor *siv = to_siv(v);
    Error *err = NULL;
    uint64_t val;

    if (siv->string) {
        parse_option_size(name, siv->string, &val, &err);
    } else {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                   "size");
        return;
    }
    if (err) {
        error_propagate(errp, err);
        return;
    }

    *obj = val;
}

static void parse_type_bool(Visitor *v, const char *name, bool *obj,
                            Error **errp)
{
    StringInputVisitor *siv = to_siv(v);

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

    error_setg(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
               "boolean");
}

static void parse_type_str(Visitor *v, const char *name, char **obj,
                           Error **errp)
{
    StringInputVisitor *siv = to_siv(v);
    if (siv->string) {
        *obj = g_strdup(siv->string);
    } else {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                   "string");
    }
}

static void parse_type_number(Visitor *v, const char *name, double *obj,
                              Error **errp)
{
    StringInputVisitor *siv = to_siv(v);
    char *endp = (char *) siv->string;
    double val;

    errno = 0;
    if (siv->string) {
        val = strtod(siv->string, &endp);
    }
    if (!siv->string || errno || endp == siv->string || *endp) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                   "number");
        return;
    }

    *obj = val;
}

static void parse_optional(Visitor *v, const char *name, bool *present)
{
    StringInputVisitor *siv = to_siv(v);

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
    g_list_foreach(v->ranges, free_range, NULL);
    g_list_free(v->ranges);
    g_free(v);
}

StringInputVisitor *string_input_visitor_new(const char *str)
{
    StringInputVisitor *v;

    v = g_malloc0(sizeof(*v));

    v->visitor.type = VISITOR_INPUT;
    v->visitor.type_int64 = parse_type_int64;
    v->visitor.type_uint64 = parse_type_uint64;
    v->visitor.type_size = parse_type_size;
    v->visitor.type_bool = parse_type_bool;
    v->visitor.type_str = parse_type_str;
    v->visitor.type_number = parse_type_number;
    v->visitor.start_list = start_list;
    v->visitor.next_list = next_list;
    v->visitor.end_list = end_list;
    v->visitor.optional = parse_optional;

    v->string = str;
    v->head = true;
    return v;
}
