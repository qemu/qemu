/*
 * Copy one QAPI object to another
 *
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/clone-visitor.h"
#include "qapi/visitor-impl.h"
#include "qapi/error.h"

struct QapiCloneVisitor {
    Visitor visitor;
    size_t depth;
};

static QapiCloneVisitor *to_qcv(Visitor *v)
{
    return container_of(v, QapiCloneVisitor, visitor);
}

static void qapi_clone_start_struct(Visitor *v, const char *name, void **obj,
                                    size_t size, Error **errp)
{
    QapiCloneVisitor *qcv = to_qcv(v);

    if (!obj) {
        assert(qcv->depth);
        /* Only possible when visiting an alternate's object
         * branch. Nothing further to do here, since the earlier
         * visit_start_alternate() already copied memory. */
        return;
    }

    *obj = g_memdup(*obj, size);
    qcv->depth++;
}

static void qapi_clone_end(Visitor *v, void **obj)
{
    QapiCloneVisitor *qcv = to_qcv(v);

    assert(qcv->depth);
    if (obj) {
        qcv->depth--;
    }
}

static void qapi_clone_start_list(Visitor *v, const char *name,
                                  GenericList **listp, size_t size,
                                  Error **errp)
{
    qapi_clone_start_struct(v, name, (void **)listp, size, errp);
}

static GenericList *qapi_clone_next_list(Visitor *v, GenericList *tail,
                                         size_t size)
{
    QapiCloneVisitor *qcv = to_qcv(v);

    assert(qcv->depth);
    /* Unshare the tail of the list cloned by g_memdup() */
    tail->next = g_memdup(tail->next, size);
    return tail->next;
}

static void qapi_clone_start_alternate(Visitor *v, const char *name,
                                       GenericAlternate **obj, size_t size,
                                       bool promote_int, Error **errp)
{
    qapi_clone_start_struct(v, name, (void **)obj, size, errp);
}

static void qapi_clone_type_int64(Visitor *v, const char *name, int64_t *obj,
                                   Error **errp)
{
    QapiCloneVisitor *qcv = to_qcv(v);

    assert(qcv->depth);
    /* Value was already cloned by g_memdup() */
}

static void qapi_clone_type_uint64(Visitor *v, const char *name,
                                    uint64_t *obj, Error **errp)
{
    QapiCloneVisitor *qcv = to_qcv(v);

    assert(qcv->depth);
    /* Value was already cloned by g_memdup() */
}

static void qapi_clone_type_bool(Visitor *v, const char *name, bool *obj,
                                  Error **errp)
{
    QapiCloneVisitor *qcv = to_qcv(v);

    assert(qcv->depth);
    /* Value was already cloned by g_memdup() */
}

static void qapi_clone_type_str(Visitor *v, const char *name, char **obj,
                                 Error **errp)
{
    QapiCloneVisitor *qcv = to_qcv(v);

    assert(qcv->depth);
    /*
     * Pointer was already cloned by g_memdup; create fresh copy.
     * Note that as long as qobject-output-visitor accepts NULL instead of
     * "", then we must do likewise. However, we want to obey the
     * input visitor semantics of never producing NULL when the empty
     * string is intended.
     */
    *obj = g_strdup(*obj ?: "");
}

static void qapi_clone_type_number(Visitor *v, const char *name, double *obj,
                                    Error **errp)
{
    QapiCloneVisitor *qcv = to_qcv(v);

    assert(qcv->depth);
    /* Value was already cloned by g_memdup() */
}

static void qapi_clone_type_null(Visitor *v, const char *name, Error **errp)
{
    QapiCloneVisitor *qcv = to_qcv(v);

    assert(qcv->depth);
    /* Nothing to do */
}

static void qapi_clone_free(Visitor *v)
{
    g_free(v);
}

static Visitor *qapi_clone_visitor_new(void)
{
    QapiCloneVisitor *v;

    v = g_malloc0(sizeof(*v));

    v->visitor.type = VISITOR_CLONE;
    v->visitor.start_struct = qapi_clone_start_struct;
    v->visitor.end_struct = qapi_clone_end;
    v->visitor.start_list = qapi_clone_start_list;
    v->visitor.next_list = qapi_clone_next_list;
    v->visitor.end_list = qapi_clone_end;
    v->visitor.start_alternate = qapi_clone_start_alternate;
    v->visitor.end_alternate = qapi_clone_end;
    v->visitor.type_int64 = qapi_clone_type_int64;
    v->visitor.type_uint64 = qapi_clone_type_uint64;
    v->visitor.type_bool = qapi_clone_type_bool;
    v->visitor.type_str = qapi_clone_type_str;
    v->visitor.type_number = qapi_clone_type_number;
    v->visitor.type_null = qapi_clone_type_null;
    v->visitor.free = qapi_clone_free;

    return &v->visitor;
}

void *qapi_clone(const void *src, void (*visit_type)(Visitor *, const char *,
                                                     void **, Error **))
{
    Visitor *v;
    void *dst = (void *) src; /* Cast away const */

    if (!src) {
        return NULL;
    }

    v = qapi_clone_visitor_new();
    visit_type(v, NULL, &dst, &error_abort);
    visit_free(v);
    return dst;
}
