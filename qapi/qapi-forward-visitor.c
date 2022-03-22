/*
 * Forward Visitor
 *
 * Copyright (C) 2021 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/compat-policy.h"
#include "qapi/error.h"
#include "qapi/forward-visitor.h"
#include "qapi/visitor-impl.h"
#include "qemu/queue.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qnull.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qstring.h"
#include "qemu/cutils.h"

struct ForwardFieldVisitor {
    Visitor visitor;

    Visitor *target;
    char *from;
    char *to;

    int depth;
};

static ForwardFieldVisitor *to_ffv(Visitor *v)
{
    return container_of(v, ForwardFieldVisitor, visitor);
}

static bool forward_field_translate_name(ForwardFieldVisitor *v, const char **name,
                                         Error **errp)
{
    if (v->depth) {
        return true;
    }
    if (g_str_equal(*name, v->from)) {
        *name = v->to;
        return true;
    }
    error_setg(errp, QERR_MISSING_PARAMETER, *name);
    return false;
}

static bool forward_field_check_struct(Visitor *v, Error **errp)
{
    ForwardFieldVisitor *ffv = to_ffv(v);

    return visit_check_struct(ffv->target, errp);
}

static bool forward_field_start_struct(Visitor *v, const char *name, void **obj,
                                       size_t size, Error **errp)
{
    ForwardFieldVisitor *ffv = to_ffv(v);

    if (!forward_field_translate_name(ffv, &name, errp)) {
        return false;
    }
    if (!visit_start_struct(ffv->target, name, obj, size, errp)) {
        return false;
    }
    ffv->depth++;
    return true;
}

static void forward_field_end_struct(Visitor *v, void **obj)
{
    ForwardFieldVisitor *ffv = to_ffv(v);

    assert(ffv->depth);
    ffv->depth--;
    visit_end_struct(ffv->target, obj);
}

static bool forward_field_start_list(Visitor *v, const char *name,
                                     GenericList **list, size_t size,
                                     Error **errp)
{
    ForwardFieldVisitor *ffv = to_ffv(v);

    if (!forward_field_translate_name(ffv, &name, errp)) {
        return false;
    }
    ffv->depth++;
    return visit_start_list(ffv->target, name, list, size, errp);
}

static GenericList *forward_field_next_list(Visitor *v, GenericList *tail,
                                            size_t size)
{
    ForwardFieldVisitor *ffv = to_ffv(v);

    assert(ffv->depth);
    return visit_next_list(ffv->target, tail, size);
}

static bool forward_field_check_list(Visitor *v, Error **errp)
{
    ForwardFieldVisitor *ffv = to_ffv(v);

    assert(ffv->depth);
    return visit_check_list(ffv->target, errp);
}

static void forward_field_end_list(Visitor *v, void **obj)
{
    ForwardFieldVisitor *ffv = to_ffv(v);

    assert(ffv->depth);
    ffv->depth--;
    visit_end_list(ffv->target, obj);
}

static bool forward_field_start_alternate(Visitor *v, const char *name,
                                          GenericAlternate **obj, size_t size,
                                          Error **errp)
{
    ForwardFieldVisitor *ffv = to_ffv(v);

    if (!forward_field_translate_name(ffv, &name, errp)) {
        return false;
    }
    /*
     * The name passed to start_alternate is used also in the visit_type_* calls
     * that retrieve the alternate's content; so, do not increase depth here.
     */
    return visit_start_alternate(ffv->target, name, obj, size, errp);
}

static void forward_field_end_alternate(Visitor *v, void **obj)
{
    ForwardFieldVisitor *ffv = to_ffv(v);

    visit_end_alternate(ffv->target, obj);
}

static bool forward_field_type_int64(Visitor *v, const char *name, int64_t *obj,
                                     Error **errp)
{
    ForwardFieldVisitor *ffv = to_ffv(v);

    if (!forward_field_translate_name(ffv, &name, errp)) {
        return false;
    }
    return visit_type_int64(ffv->target, name, obj, errp);
}

static bool forward_field_type_uint64(Visitor *v, const char *name,
                                      uint64_t *obj, Error **errp)
{
    ForwardFieldVisitor *ffv = to_ffv(v);

    if (!forward_field_translate_name(ffv, &name, errp)) {
        return false;
    }
    return visit_type_uint64(ffv->target, name, obj, errp);
}

static bool forward_field_type_bool(Visitor *v, const char *name, bool *obj,
                                    Error **errp)
{
    ForwardFieldVisitor *ffv = to_ffv(v);

    if (!forward_field_translate_name(ffv, &name, errp)) {
        return false;
    }
    return visit_type_bool(ffv->target, name, obj, errp);
}

static bool forward_field_type_str(Visitor *v, const char *name, char **obj,
                                   Error **errp)
{
    ForwardFieldVisitor *ffv = to_ffv(v);

    if (!forward_field_translate_name(ffv, &name, errp)) {
        return false;
    }
    return visit_type_str(ffv->target, name, obj, errp);
}

static bool forward_field_type_size(Visitor *v, const char *name, uint64_t *obj,
                                    Error **errp)
{
    ForwardFieldVisitor *ffv = to_ffv(v);

    if (!forward_field_translate_name(ffv, &name, errp)) {
        return false;
    }
    return visit_type_size(ffv->target, name, obj, errp);
}

static bool forward_field_type_number(Visitor *v, const char *name, double *obj,
                                      Error **errp)
{
    ForwardFieldVisitor *ffv = to_ffv(v);

    if (!forward_field_translate_name(ffv, &name, errp)) {
        return false;
    }
    return visit_type_number(ffv->target, name, obj, errp);
}

static bool forward_field_type_any(Visitor *v, const char *name, QObject **obj,
                                   Error **errp)
{
    ForwardFieldVisitor *ffv = to_ffv(v);

    if (!forward_field_translate_name(ffv, &name, errp)) {
        return false;
    }
    return visit_type_any(ffv->target, name, obj, errp);
}

static bool forward_field_type_null(Visitor *v, const char *name,
                                    QNull **obj, Error **errp)
{
    ForwardFieldVisitor *ffv = to_ffv(v);

    if (!forward_field_translate_name(ffv, &name, errp)) {
        return false;
    }
    return visit_type_null(ffv->target, name, obj, errp);
}

static void forward_field_optional(Visitor *v, const char *name, bool *present)
{
    ForwardFieldVisitor *ffv = to_ffv(v);

    if (!forward_field_translate_name(ffv, &name, NULL)) {
        *present = false;
        return;
    }
    visit_optional(ffv->target, name, present);
}

static bool forward_field_policy_reject(Visitor *v, const char *name,
                                        unsigned special_features,
                                        Error **errp)
{
    ForwardFieldVisitor *ffv = to_ffv(v);

    if (!forward_field_translate_name(ffv, &name, errp)) {
        return true;
    }
    return visit_policy_reject(ffv->target, name, special_features, errp);
}

static bool forward_field_policy_skip(Visitor *v, const char *name,
                                      unsigned special_features)
{
    ForwardFieldVisitor *ffv = to_ffv(v);

    if (!forward_field_translate_name(ffv, &name, NULL)) {
        return true;
    }
    return visit_policy_skip(ffv->target, name, special_features);
}

static void forward_field_complete(Visitor *v, void *opaque)
{
    /*
     * Do nothing, the complete method will be called in due time
     * on the target visitor.
     */
}

static void forward_field_free(Visitor *v)
{
    ForwardFieldVisitor *ffv = to_ffv(v);

    g_free(ffv->from);
    g_free(ffv->to);
    g_free(ffv);
}

Visitor *visitor_forward_field(Visitor *target, const char *from, const char *to)
{
    ForwardFieldVisitor *v = g_new0(ForwardFieldVisitor, 1);

    /*
     * Clone and dealloc visitors don't use a name for the toplevel
     * visit, so they make no sense here.
     */
    assert(target->type == VISITOR_OUTPUT || target->type == VISITOR_INPUT);

    v->visitor.type = target->type;
    v->visitor.start_struct = forward_field_start_struct;
    v->visitor.check_struct = forward_field_check_struct;
    v->visitor.end_struct = forward_field_end_struct;
    v->visitor.start_list = forward_field_start_list;
    v->visitor.next_list = forward_field_next_list;
    v->visitor.check_list = forward_field_check_list;
    v->visitor.end_list = forward_field_end_list;
    v->visitor.start_alternate = forward_field_start_alternate;
    v->visitor.end_alternate = forward_field_end_alternate;
    v->visitor.type_int64 = forward_field_type_int64;
    v->visitor.type_uint64 = forward_field_type_uint64;
    v->visitor.type_size = forward_field_type_size;
    v->visitor.type_bool = forward_field_type_bool;
    v->visitor.type_str = forward_field_type_str;
    v->visitor.type_number = forward_field_type_number;
    v->visitor.type_any = forward_field_type_any;
    v->visitor.type_null = forward_field_type_null;
    v->visitor.optional = forward_field_optional;
    v->visitor.policy_reject = forward_field_policy_reject;
    v->visitor.policy_skip = forward_field_policy_skip;
    v->visitor.complete = forward_field_complete;
    v->visitor.free = forward_field_free;

    v->target = target;
    v->from = g_strdup(from);
    v->to = g_strdup(to);

    return &v->visitor;
}
