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

#include "qemu/osdep.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qobject.h"
#include "qemu/queue.h"
#include "qemu-common.h"

/**
 * qlist_new(): Create a new QList
 *
 * Return strong reference.
 */
QList *qlist_new(void)
{
    QList *qlist;

    qlist = g_malloc(sizeof(*qlist));
    qobject_init(QOBJECT(qlist), QTYPE_QLIST);
    QTAILQ_INIT(&qlist->head);

    return qlist;
}

static void qlist_copy_elem(QObject *obj, void *opaque)
{
    QList *dst = opaque;

    qobject_incref(obj);
    qlist_append_obj(dst, obj);
}

QList *qlist_copy(QList *src)
{
    QList *dst = qlist_new();

    qlist_iter(src, qlist_copy_elem, dst);

    return dst;
}

/**
 * qlist_append_obj(): Append an QObject into QList
 *
 * NOTE: ownership of 'value' is transferred to the QList
 */
void qlist_append_obj(QList *qlist, QObject *value)
{
    QListEntry *entry;

    entry = g_malloc(sizeof(*entry));
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

QObject *qlist_pop(QList *qlist)
{
    QListEntry *entry;
    QObject *ret;

    if (qlist == NULL || QTAILQ_EMPTY(&qlist->head)) {
        return NULL;
    }

    entry = QTAILQ_FIRST(&qlist->head);
    QTAILQ_REMOVE(&qlist->head, entry, next);

    ret = entry->value;
    g_free(entry);

    return ret;
}

QObject *qlist_peek(QList *qlist)
{
    QListEntry *entry;

    if (qlist == NULL || QTAILQ_EMPTY(&qlist->head)) {
        return NULL;
    }

    entry = QTAILQ_FIRST(&qlist->head);

    return entry->value;
}

int qlist_empty(const QList *qlist)
{
    return QTAILQ_EMPTY(&qlist->head);
}

static void qlist_size_iter(QObject *obj, void *opaque)
{
    size_t *count = opaque;
    (*count)++;
}

size_t qlist_size(const QList *qlist)
{
    size_t count = 0;
    qlist_iter(qlist, qlist_size_iter, &count);
    return count;
}

/**
 * qobject_to_qlist(): Convert a QObject into a QList
 */
QList *qobject_to_qlist(const QObject *obj)
{
    if (!obj || qobject_type(obj) != QTYPE_QLIST) {
        return NULL;
    }
    return container_of(obj, QList, base);
}

/**
 * qlist_destroy_obj(): Free all the memory allocated by a QList
 */
void qlist_destroy_obj(QObject *obj)
{
    QList *qlist;
    QListEntry *entry, *next_entry;

    assert(obj != NULL);
    qlist = qobject_to_qlist(obj);

    QTAILQ_FOREACH_SAFE(entry, &qlist->head, next, next_entry) {
        QTAILQ_REMOVE(&qlist->head, entry, next);
        qobject_decref(entry->value);
        g_free(entry);
    }

    g_free(qlist);
}
