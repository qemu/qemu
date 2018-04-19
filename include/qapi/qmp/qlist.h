/*
 * QList Module
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#ifndef QLIST_H
#define QLIST_H

#include "qapi/qmp/qobject.h"
#include "qemu/queue.h"

typedef struct QListEntry {
    QObject *value;
    QTAILQ_ENTRY(QListEntry) next;
} QListEntry;

struct QList {
    struct QObjectBase_ base;
    QTAILQ_HEAD(,QListEntry) head;
};

#define qlist_append(qlist, obj) \
        qlist_append_obj(qlist, QOBJECT(obj))

void qlist_append_bool(QList *qlist, bool value);
void qlist_append_int(QList *qlist, int64_t value);
void qlist_append_null(QList *qlist);
void qlist_append_str(QList *qlist, const char *value);

#define QLIST_FOREACH_ENTRY(qlist, var)             \
        for ((var) = ((qlist)->head.tqh_first);     \
            (var);                                  \
            (var) = ((var)->next.tqe_next))

static inline QObject *qlist_entry_obj(const QListEntry *entry)
{
    return entry->value;
}

QList *qlist_new(void);
QList *qlist_copy(QList *src);
void qlist_append_obj(QList *qlist, QObject *obj);
void qlist_iter(const QList *qlist,
                void (*iter)(QObject *obj, void *opaque), void *opaque);
QObject *qlist_pop(QList *qlist);
QObject *qlist_peek(QList *qlist);
int qlist_empty(const QList *qlist);
size_t qlist_size(const QList *qlist);
bool qlist_is_equal(const QObject *x, const QObject *y);
void qlist_destroy_obj(QObject *obj);

static inline const QListEntry *qlist_first(const QList *qlist)
{
    return QTAILQ_FIRST(&qlist->head);
}

static inline const QListEntry *qlist_next(const QListEntry *entry)
{
    return QTAILQ_NEXT(entry, next);
}

#endif /* QLIST_H */
