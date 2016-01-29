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
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/notify.h"

void notifier_list_init(NotifierList *list)
{
    QLIST_INIT(&list->notifiers);
}

void notifier_list_add(NotifierList *list, Notifier *notifier)
{
    QLIST_INSERT_HEAD(&list->notifiers, notifier, node);
}

void notifier_remove(Notifier *notifier)
{
    QLIST_REMOVE(notifier, node);
}

void notifier_list_notify(NotifierList *list, void *data)
{
    Notifier *notifier, *next;

    QLIST_FOREACH_SAFE(notifier, &list->notifiers, node, next) {
        notifier->notify(notifier, data);
    }
}

void notifier_with_return_list_init(NotifierWithReturnList *list)
{
    QLIST_INIT(&list->notifiers);
}

void notifier_with_return_list_add(NotifierWithReturnList *list,
                                   NotifierWithReturn *notifier)
{
    QLIST_INSERT_HEAD(&list->notifiers, notifier, node);
}

void notifier_with_return_remove(NotifierWithReturn *notifier)
{
    QLIST_REMOVE(notifier, node);
}

int notifier_with_return_list_notify(NotifierWithReturnList *list, void *data)
{
    NotifierWithReturn *notifier, *next;
    int ret = 0;

    QLIST_FOREACH_SAFE(notifier, &list->notifiers, node, next) {
        ret = notifier->notify(notifier, data);
        if (ret != 0) {
            break;
        }
    }
    return ret;
}
