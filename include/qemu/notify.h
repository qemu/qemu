/*
 * Notifier lists
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_NOTIFY_H
#define QEMU_NOTIFY_H

#include "qemu/queue.h"

typedef struct Notifier Notifier;

struct Notifier
{
    void (*notify)(Notifier *notifier, void *data);
    QLIST_ENTRY(Notifier) node;
};

typedef struct NotifierList
{
    QLIST_HEAD(, Notifier) notifiers;
} NotifierList;

#define NOTIFIER_LIST_INITIALIZER(head) \
    { QLIST_HEAD_INITIALIZER((head).notifiers) }

void notifier_list_init(NotifierList *list);

void notifier_list_add(NotifierList *list, Notifier *notifier);

void notifier_remove(Notifier *notifier);

void notifier_list_notify(NotifierList *list, void *data);

bool notifier_list_empty(NotifierList *list);

/* Same as Notifier but allows .notify() to return errors */
typedef struct NotifierWithReturn NotifierWithReturn;

struct NotifierWithReturn {
    /**
     * Return 0 on success (next notifier will be invoked), otherwise
     * notifier_with_return_list_notify() will stop and return the value.
     */
    int (*notify)(NotifierWithReturn *notifier, void *data);
    QLIST_ENTRY(NotifierWithReturn) node;
};

typedef struct NotifierWithReturnList {
    QLIST_HEAD(, NotifierWithReturn) notifiers;
} NotifierWithReturnList;

#define NOTIFIER_WITH_RETURN_LIST_INITIALIZER(head) \
    { QLIST_HEAD_INITIALIZER((head).notifiers) }

void notifier_with_return_list_init(NotifierWithReturnList *list);

void notifier_with_return_list_add(NotifierWithReturnList *list,
                                   NotifierWithReturn *notifier);

void notifier_with_return_remove(NotifierWithReturn *notifier);

int notifier_with_return_list_notify(NotifierWithReturnList *list,
                                     void *data);

#endif
