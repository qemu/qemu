/*
 * QEMU Object Model.
 *
 * Based on ideas by Avi Kivity <avi@redhat.com>
 *
 * Copyright (C) 2009, 2015 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 * QObject Reference Counts Terminology
 * ------------------------------------
 *
 *  - Returning references: A function that returns an object may
 *  return it as either a weak or a strong reference.  If the
 *  reference is strong, you are responsible for calling
 *  qobject_unref() on the reference when you are done.
 *
 *  If the reference is weak, the owner of the reference may free it at
 *  any time in the future.  Before storing the reference anywhere, you
 *  should call qobject_ref() to make the reference strong.
 *
 *  - Transferring ownership: when you transfer ownership of a reference
 *  by calling a function, you are no longer responsible for calling
 *  qobject_unref() when the reference is no longer needed.  In other words,
 *  when the function returns you must behave as if the reference to the
 *  passed object was weak.
 */
#ifndef QOBJECT_H
#define QOBJECT_H

#include "qapi/qapi-builtin-types.h"

/* Not for use outside include/qapi/qmp/ */
struct QObjectBase_ {
    QType type;
    size_t refcnt;
};

/* this struct must have no other members than base */
struct QObject {
    struct QObjectBase_ base;
};

/*
 * Preprocessor sorcery ahead: use a different identifier for the
 * local variable in each expansion, so we can nest macro calls
 * without shadowing variables.
 */
#define QOBJECT_INTERNAL(obj, _obj) ({                          \
    typeof(obj) _obj = (obj);                                   \
    _obj ? container_of(&_obj->base, QObject, base) : NULL;     \
})
#define QOBJECT(obj) QOBJECT_INTERNAL((obj), MAKE_IDENTFIER(_obj))

/* Required for qobject_to() */
#define QTYPE_CAST_TO_QNull     QTYPE_QNULL
#define QTYPE_CAST_TO_QNum      QTYPE_QNUM
#define QTYPE_CAST_TO_QString   QTYPE_QSTRING
#define QTYPE_CAST_TO_QDict     QTYPE_QDICT
#define QTYPE_CAST_TO_QList     QTYPE_QLIST
#define QTYPE_CAST_TO_QBool     QTYPE_QBOOL

QEMU_BUILD_BUG_MSG(QTYPE__MAX != 7,
                   "The QTYPE_CAST_TO_* list needs to be extended");

#define qobject_to(type, obj)                                       \
    ((type *)qobject_check_type(obj, glue(QTYPE_CAST_TO_, type)))

static inline void qobject_ref_impl(QObject *obj)
{
    if (obj) {
        obj->base.refcnt++;
    }
}

/**
 * qobject_is_equal(): Return whether the two objects are equal.
 *
 * Any of the pointers may be NULL; return true if both are.  Always
 * return false if only one is (therefore a QNull object is not
 * considered equal to a NULL pointer).
 */
bool qobject_is_equal(const QObject *x, const QObject *y);

/**
 * qobject_destroy(): Free resources used by the object
 * For use via qobject_unref() only!
 */
void qobject_destroy(QObject *obj);

static inline void qobject_unref_impl(QObject *obj)
{
    assert(!obj || obj->base.refcnt);
    if (obj && --obj->base.refcnt == 0) {
        qobject_destroy(obj);
    }
}

/**
 * qobject_ref(): Increment QObject's reference count
 *
 * Returns: the same @obj. The type of @obj will be propagated to the
 * return type.
 */
#define qobject_ref(obj) ({                     \
    typeof(obj) _o = (obj);                     \
    qobject_ref_impl(QOBJECT(_o));              \
    _o;                                         \
})

/**
 * qobject_unref(): Decrement QObject's reference count, deallocate
 * when it reaches zero
 */
#define qobject_unref(obj) qobject_unref_impl(QOBJECT(obj))

/**
 * qobject_type(): Return the QObject's type
 */
static inline QType qobject_type(const QObject *obj)
{
    assert(QTYPE_NONE < obj->base.type && obj->base.type < QTYPE__MAX);
    return obj->base.type;
}

/**
 * qobject_check_type(): Helper function for the qobject_to() macro.
 * Return @obj, but only if @obj is not NULL and @type is equal to
 * @obj's type.  Return NULL otherwise.
 */
static inline QObject *qobject_check_type(const QObject *obj, QType type)
{
    if (obj && qobject_type(obj) == type) {
        return (QObject *)obj;
    } else {
        return NULL;
    }
}

#endif /* QOBJECT_H */
