/*
 * QList data type.
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */
#include "qlist.h"
#include "qobject.h"
#include "qemu-queue.h"
#include "qemu-common.h"

static void qlist_destroy_obj(QObject *obj);

static const QType qlist_type = {
    .code = QTYPE_QLIST,
    .destroy = qlist_destroy_obj,
};
 
/**
 * qlist_new(): Create a new QList
 *
 * Return strong reference.
 */
QList *qlist_new(void)
{
    QList *qlist;

    qlist = qemu_malloc(sizeof(*qlist));
    QTAILQ_INIT(&qlist->head);
    QOBJECT_INIT(qlist, &qlist_type);

    return qlist;
}

/**
 * qlist_append_obj(): Append an QObject into QList
 *
 * NOTE: ownership of 'value' is transferred to the QList
 */
void qlist_append_obj(QList *qlist, QObject *value)
{
    QListEntry *entry;

    entry = qemu_malloc(sizeof(*entry));
    entry->value = value;

    QTAILQ_INSERT_TAIL(&qlist->head, entry, next);
}

/**
 * qlist_iter(): Iterate over all the list's stored values.
 *
 * This function allows the user to provide an iterator, which will be
 * called for each stored value in the list.
 */
void qlist_iter(const QList *qlist,
                void (*iter)(QObject *obj, void *opaque), void *opaque)
{
    QListEntry *entry;

    QTAILQ_FOREACH(entry, &qlist->head, next)
        iter(entry->value, opaque);
}

/**
 * qobject_to_qlist(): Convert a QObject into a QList
 */
QList *qobject_to_qlist(const QObject *obj)
{
    if (qobject_type(obj) != QTYPE_QLIST) {
        return NULL;
    }

    return container_of(obj, QList, base);
}

/**
 * qlist_destroy_obj(): Free all the memory allocated by a QList
 */
static void qlist_destroy_obj(QObject *obj)
{
    QList *qlist;
    QListEntry *entry, *next_entry;

    assert(obj != NULL);
    qlist = qobject_to_qlist(obj);

    QTAILQ_FOREACH_SAFE(entry, &qlist->head, next, next_entry) {
        QTAILQ_REMOVE(&qlist->head, entry, next);
        qobject_decref(entry->value);
        qemu_free(entry);
    }

    qemu_free(qlist);
}
