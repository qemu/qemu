/*
 * QList data type header.
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */
#ifndef QLIST_H
#define QLIST_H

#include "qobject.h"
#include "qemu-queue.h"
#include "qemu-common.h"

typedef struct QListEntry {
    QObject *value;
    QTAILQ_ENTRY(QListEntry) next;
} QListEntry;

typedef struct QList {
    QObject_HEAD;
    QTAILQ_HEAD(,QListEntry) head;
} QList;

#define qlist_append(qlist, obj) \
        qlist_append_obj(qlist, QOBJECT(obj))

QList *qlist_new(void);
void qlist_append_obj(QList *qlist, QObject *obj);
void qlist_iter(const QList *qlist,
                void (*iter)(QObject *obj, void *opaque), void *opaque);
QList *qobject_to_qlist(const QObject *obj);

#endif /* QLIST_H */
