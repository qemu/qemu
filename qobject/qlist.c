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
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qnull.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qstring.h"
#include "qemu/queue.h"
#include "qobject-internal.h"

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

QList *qlist_copy(QList *src)
{
    QList *dst = qlist_new();
    QListEntry *entry;
    QObject *elt;

    QLIST_FOREACH_ENTRY(src, entry) {
        elt = qlist_entry_obj(entry);
        qobject_ref(elt);
        qlist_append_obj(dst, elt);
    }
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

void qlist_append_int(QList *qlist, int64_t value)
{
    qlist_append(qlist, qnum_from_int(value));
}

void qlist_append_bool(QList *qlist, bool value)
{
    qlist_append(qlist, qbool_from_bool(value));
}

void qlist_append_str(QList *qlist, const char *value)
{
    qlist_append(qlist, qstring_from_str(value));
}

void qlist_append_null(QList *qlist)
{
    qlist_append(qlist, qnull());
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

size_t qlist_size(const QList *qlist)
{
    size_t count = 0;
    QListEntry *entry;

    QLIST_FOREACH_ENTRY(qlist, entry) {
        count++;
    }
    return count;
}

/**
 * qlist_is_equal(): Test whether the two QLists are equal
 *
 * In order to be considered equal, the respective two objects at each
 * index of the two lists have to compare equal (regarding
 * qobject_is_equal()), and both lists have to have the same number of
 * elements.
 * That means both lists have to contain equal objects in equal order.
 */
bool qlist_is_equal(const QObject *x, const QObject *y)
{
    const QList *list_x = qobject_to(QList, x);
    const QList *list_y = qobject_to(QList, y);
    const QListEntry *entry_x, *entry_y;

    entry_x = qlist_first(list_x);
    entry_y = qlist_first(list_y);

    while (entry_x && entry_y) {
        if (!qobject_is_equal(qlist_entry_obj(entry_x),
                              qlist_entry_obj(entry_y)))
        {
            return false;
        }

        entry_x = qlist_next(entry_x);
        entry_y = qlist_next(entry_y);
    }

    return !entry_x && !entry_y;
}

/**
 * qlist_destroy_obj(): Free all the memory allocated by a QList
 */
void qlist_destroy_obj(QObject *obj)
{
    QList *qlist;
    QListEntry *entry, *next_entry;

    assert(obj != NULL);
    qlist = qobject_to(QList, obj);

    QTAILQ_FOREACH_SAFE(entry, &qlist->head, next, next_entry) {
        QTAILQ_REMOVE(&qlist->head, entry, next);
        qobject_unref(entry->value);
        g_free(entry);
    }

    g_free(qlist);
}
