/*
 * QEMU Object Model.
 *
 * Based on ideas by Avi Kivity <avi@redhat.com>
 *
 * Copyright (C) 2009 Red Hat Inc.
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
 *  return it as either a weak or a strong reference.  If the reference
 *  is strong, you are responsible for calling QDECREF() on the reference
 *  when you are done.
 *
 *  If the reference is weak, the owner of the reference may free it at
 *  any time in the future.  Before storing the reference anywhere, you
 *  should call QINCREF() to make the reference strong.
 *
 *  - Transferring ownership: when you transfer ownership of a reference
 *  by calling a function, you are no longer responsible for calling
 *  QDECREF() when the reference is no longer needed.  In other words,
 *  when the function returns you must behave as if the reference to the
 *  passed object was weak.
 */
#ifndef QOBJECT_H
#define QOBJECT_H

#include <stddef.h>
#include <assert.h>

typedef enum {
    QTYPE_NONE,
    QTYPE_QINT,
    QTYPE_QSTRING,
    QTYPE_QDICT,
    QTYPE_QLIST,
    QTYPE_QFLOAT,
    QTYPE_QBOOL,
    QTYPE_QERROR,
} qtype_code;

struct QObject;

typedef struct QType {
    qtype_code code;
    void (*destroy)(struct QObject *);
} QType;

typedef struct QObject {
    const QType *type;
    size_t refcnt;
} QObject;

/* Objects definitions must include this */
#define QObject_HEAD  \
    QObject base

/* Get the 'base' part of an object */
#define QOBJECT(obj) (&(obj)->base)

/* High-level interface for qobject_incref() */
#define QINCREF(obj)      \
    qobject_incref(QOBJECT(obj))

/* High-level interface for qobject_decref() */
#define QDECREF(obj)              \
    qobject_decref(QOBJECT(obj))

/* Initialize an object to default values */
#define QOBJECT_INIT(obj, qtype_type)   \
    obj->base.refcnt = 1;               \
    obj->base.type   = qtype_type

/**
 * qobject_incref(): Increment QObject's reference count
 */
static inline void qobject_incref(QObject *obj)
{
    if (obj)
        obj->refcnt++;
}

/**
 * qobject_decref(): Decrement QObject's reference count, deallocate
 * when it reaches zero
 */
static inline void qobject_decref(QObject *obj)
{
    if (obj && --obj->refcnt == 0) {
        assert(obj->type != NULL);
        assert(obj->type->destroy != NULL);
        obj->type->destroy(obj);
    }
}

/**
 * qobject_type(): Return the QObject's type
 */
static inline qtype_code qobject_type(const QObject *obj)
{
    assert(obj->type != NULL);
    return obj->type->code;
}

#endif /* QOBJECT_H */
