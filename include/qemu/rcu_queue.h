#ifndef QEMU_RCU_QUEUE_H
#define QEMU_RCU_QUEUE_H

/*
 * rcu_queue.h
 *
 * RCU-friendly versions of the queue.h primitives.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Copyright (c) 2013 Mike D. Day, IBM Corporation.
 *
 * IBM's contributions to this file may be relicensed under LGPLv2 or later.
 */

#include "qemu/queue.h"
#include "qemu/atomic.h"

#ifdef __cplusplus
extern "C" {
#endif


/*
 * List access methods.
 */
#define QLIST_EMPTY_RCU(head) (atomic_rcu_read(&(head)->lh_first) == NULL)
#define QLIST_FIRST_RCU(head) (atomic_rcu_read(&(head)->lh_first))
#define QLIST_NEXT_RCU(elm, field) (atomic_rcu_read(&(elm)->field.le_next))

/*
 * List functions.
 */


/*
 *  The difference between atomic_read/set and atomic_rcu_read/set
 *  is in the including of a read/write memory barrier to the volatile
 *  access. atomic_rcu_* macros include the memory barrier, the
 *  plain atomic macros do not. Therefore, it should be correct to
 *  issue a series of reads or writes to the same element using only
 *  the atomic_* macro, until the last read or write, which should be
 *  atomic_rcu_* to introduce a read or write memory barrier as
 *  appropriate.
 */

/* Upon publication of the listelm->next value, list readers
 * will see the new node when following next pointers from
 * antecedent nodes, but may not see the new node when following
 * prev pointers from subsequent nodes until after the RCU grace
 * period expires.
 * see linux/include/rculist.h __list_add_rcu(new, prev, next)
 */
#define QLIST_INSERT_AFTER_RCU(listelm, elm, field) do {    \
    (elm)->field.le_next = (listelm)->field.le_next;        \
    (elm)->field.le_prev = &(listelm)->field.le_next;       \
    atomic_rcu_set(&(listelm)->field.le_next, (elm));       \
    if ((elm)->field.le_next != NULL) {                     \
       (elm)->field.le_next->field.le_prev =                \
        &(elm)->field.le_next;                              \
    }                                                       \
} while (/*CONSTCOND*/0)

/* Upon publication of the listelm->prev->next value, list
 * readers will see the new element when following prev pointers
 * from subsequent elements, but may not see the new element
 * when following next pointers from antecedent elements
 * until after the RCU grace period expires.
 */
#define QLIST_INSERT_BEFORE_RCU(listelm, elm, field) do {   \
    (elm)->field.le_prev = (listelm)->field.le_prev;        \
    (elm)->field.le_next = (listelm);                       \
    atomic_rcu_set((listelm)->field.le_prev, (elm));        \
    (listelm)->field.le_prev = &(elm)->field.le_next;       \
} while (/*CONSTCOND*/0)

/* Upon publication of the head->first value, list readers
 * will see the new element when following the head, but may
 * not see the new element when following prev pointers from
 * subsequent elements until after the RCU grace period has
 * expired.
 */
#define QLIST_INSERT_HEAD_RCU(head, elm, field) do {    \
    (elm)->field.le_prev = &(head)->lh_first;           \
    (elm)->field.le_next = (head)->lh_first;            \
    atomic_rcu_set((&(head)->lh_first), (elm));         \
    if ((elm)->field.le_next != NULL) {                 \
       (elm)->field.le_next->field.le_prev =            \
        &(elm)->field.le_next;                          \
    }                                                   \
} while (/*CONSTCOND*/0)


/* prior to publication of the elm->prev->next value, some list
 * readers may still see the removed element when following
 * the antecedent's next pointer.
 */
#define QLIST_REMOVE_RCU(elm, field) do {           \
    if ((elm)->field.le_next != NULL) {             \
       (elm)->field.le_next->field.le_prev =        \
        (elm)->field.le_prev;                       \
    }                                               \
    *(elm)->field.le_prev =  (elm)->field.le_next;  \
} while (/*CONSTCOND*/0)

/* List traversal must occur within an RCU critical section.  */
#define QLIST_FOREACH_RCU(var, head, field)                 \
        for ((var) = atomic_rcu_read(&(head)->lh_first);    \
                (var);                                      \
                (var) = atomic_rcu_read(&(var)->field.le_next))

/* List traversal must occur within an RCU critical section.  */
#define QLIST_FOREACH_SAFE_RCU(var, head, field, next_var)           \
    for ((var) = (atomic_rcu_read(&(head)->lh_first));               \
      (var) &&                                                       \
          ((next_var) = atomic_rcu_read(&(var)->field.le_next), 1);  \
           (var) = (next_var))

#ifdef __cplusplus
}
#endif
#endif /* QEMU_RCU_QUEUE_H */
