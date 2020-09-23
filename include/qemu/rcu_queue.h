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
#define QLIST_EMPTY_RCU(head) (qatomic_read(&(head)->lh_first) == NULL)
#define QLIST_FIRST_RCU(head) (qatomic_rcu_read(&(head)->lh_first))
#define QLIST_NEXT_RCU(elm, field) (qatomic_rcu_read(&(elm)->field.le_next))

/*
 * List functions.
 */


/*
 *  The difference between qatomic_read/set and qatomic_rcu_read/set
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
    qatomic_rcu_set(&(listelm)->field.le_next, (elm));      \
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
    qatomic_rcu_set((listelm)->field.le_prev, (elm));       \
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
    qatomic_rcu_set((&(head)->lh_first), (elm));        \
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
    qatomic_set((elm)->field.le_prev, (elm)->field.le_next); \
} while (/*CONSTCOND*/0)

/* List traversal must occur within an RCU critical section.  */
#define QLIST_FOREACH_RCU(var, head, field)                 \
        for ((var) = qatomic_rcu_read(&(head)->lh_first);   \
                (var);                                      \
                (var) = qatomic_rcu_read(&(var)->field.le_next))

/* List traversal must occur within an RCU critical section.  */
#define QLIST_FOREACH_SAFE_RCU(var, head, field, next_var)           \
    for ((var) = (qatomic_rcu_read(&(head)->lh_first));              \
      (var) &&                                                       \
          ((next_var) = qatomic_rcu_read(&(var)->field.le_next), 1); \
           (var) = (next_var))

/*
 * RCU simple queue
 */

/* Simple queue access methods */
#define QSIMPLEQ_EMPTY_RCU(head) \
    (qatomic_read(&(head)->sqh_first) == NULL)
#define QSIMPLEQ_FIRST_RCU(head)       qatomic_rcu_read(&(head)->sqh_first)
#define QSIMPLEQ_NEXT_RCU(elm, field)  qatomic_rcu_read(&(elm)->field.sqe_next)

/* Simple queue functions */
#define QSIMPLEQ_INSERT_HEAD_RCU(head, elm, field) do {         \
    (elm)->field.sqe_next = (head)->sqh_first;                  \
    if ((elm)->field.sqe_next == NULL) {                        \
        (head)->sqh_last = &(elm)->field.sqe_next;              \
    }                                                           \
    qatomic_rcu_set(&(head)->sqh_first, (elm));                 \
} while (/*CONSTCOND*/0)

#define QSIMPLEQ_INSERT_TAIL_RCU(head, elm, field) do {    \
    (elm)->field.sqe_next = NULL;                          \
    qatomic_rcu_set((head)->sqh_last, (elm));              \
    (head)->sqh_last = &(elm)->field.sqe_next;             \
} while (/*CONSTCOND*/0)

#define QSIMPLEQ_INSERT_AFTER_RCU(head, listelm, elm, field) do {       \
    (elm)->field.sqe_next = (listelm)->field.sqe_next;                  \
    if ((elm)->field.sqe_next == NULL) {                                \
        (head)->sqh_last = &(elm)->field.sqe_next;                      \
    }                                                                   \
    qatomic_rcu_set(&(listelm)->field.sqe_next, (elm));                 \
} while (/*CONSTCOND*/0)

#define QSIMPLEQ_REMOVE_HEAD_RCU(head, field) do {                     \
    qatomic_set(&(head)->sqh_first, (head)->sqh_first->field.sqe_next);\
    if ((head)->sqh_first == NULL) {                                   \
        (head)->sqh_last = &(head)->sqh_first;                         \
    }                                                                  \
} while (/*CONSTCOND*/0)

#define QSIMPLEQ_REMOVE_RCU(head, elm, type, field) do {            \
    if ((head)->sqh_first == (elm)) {                               \
        QSIMPLEQ_REMOVE_HEAD_RCU((head), field);                    \
    } else {                                                        \
        struct type *curr = (head)->sqh_first;                      \
        while (curr->field.sqe_next != (elm)) {                     \
            curr = curr->field.sqe_next;                            \
        }                                                           \
        qatomic_set(&curr->field.sqe_next,                          \
                   curr->field.sqe_next->field.sqe_next);           \
        if (curr->field.sqe_next == NULL) {                         \
            (head)->sqh_last = &(curr)->field.sqe_next;             \
        }                                                           \
    }                                                               \
} while (/*CONSTCOND*/0)

#define QSIMPLEQ_FOREACH_RCU(var, head, field)                          \
    for ((var) = qatomic_rcu_read(&(head)->sqh_first);                  \
         (var);                                                         \
         (var) = qatomic_rcu_read(&(var)->field.sqe_next))

#define QSIMPLEQ_FOREACH_SAFE_RCU(var, head, field, next)                \
    for ((var) = qatomic_rcu_read(&(head)->sqh_first);                   \
         (var) && ((next) = qatomic_rcu_read(&(var)->field.sqe_next), 1);\
         (var) = (next))

/*
 * RCU tail queue
 */

/* Tail queue access methods */
#define QTAILQ_EMPTY_RCU(head)      (qatomic_read(&(head)->tqh_first) == NULL)
#define QTAILQ_FIRST_RCU(head)       qatomic_rcu_read(&(head)->tqh_first)
#define QTAILQ_NEXT_RCU(elm, field)  qatomic_rcu_read(&(elm)->field.tqe_next)

/* Tail queue functions */
#define QTAILQ_INSERT_HEAD_RCU(head, elm, field) do {                   \
    (elm)->field.tqe_next = (head)->tqh_first;                          \
    if ((elm)->field.tqe_next != NULL) {                                \
        (head)->tqh_first->field.tqe_circ.tql_prev =                    \
            &(elm)->field.tqe_circ;                                     \
    } else {                                                            \
        (head)->tqh_circ.tql_prev = &(elm)->field.tqe_circ;             \
    }                                                                   \
    qatomic_rcu_set(&(head)->tqh_first, (elm));                         \
    (elm)->field.tqe_circ.tql_prev = &(head)->tqh_circ;                 \
} while (/*CONSTCOND*/0)

#define QTAILQ_INSERT_TAIL_RCU(head, elm, field) do {                   \
    (elm)->field.tqe_next = NULL;                                       \
    (elm)->field.tqe_circ.tql_prev = (head)->tqh_circ.tql_prev;         \
    qatomic_rcu_set(&(head)->tqh_circ.tql_prev->tql_next, (elm));       \
    (head)->tqh_circ.tql_prev = &(elm)->field.tqe_circ;                 \
} while (/*CONSTCOND*/0)

#define QTAILQ_INSERT_AFTER_RCU(head, listelm, elm, field) do {         \
    (elm)->field.tqe_next = (listelm)->field.tqe_next;                  \
    if ((elm)->field.tqe_next != NULL) {                                \
        (elm)->field.tqe_next->field.tqe_circ.tql_prev =                \
            &(elm)->field.tqe_circ;                                     \
    } else {                                                            \
        (head)->tqh_circ.tql_prev = &(elm)->field.tqe_circ;             \
    }                                                                   \
    qatomic_rcu_set(&(listelm)->field.tqe_next, (elm));                 \
    (elm)->field.tqe_circ.tql_prev = &(listelm)->field.tqe_circ;        \
} while (/*CONSTCOND*/0)

#define QTAILQ_INSERT_BEFORE_RCU(listelm, elm, field) do {                \
    (elm)->field.tqe_circ.tql_prev = (listelm)->field.tqe_circ.tql_prev;  \
    (elm)->field.tqe_next = (listelm);                                    \
    qatomic_rcu_set(&(listelm)->field.tqe_circ.tql_prev->tql_next, (elm));\
    (listelm)->field.tqe_circ.tql_prev = &(elm)->field.tqe_circ;          \
} while (/*CONSTCOND*/0)

#define QTAILQ_REMOVE_RCU(head, elm, field) do {                        \
    if (((elm)->field.tqe_next) != NULL) {                              \
        (elm)->field.tqe_next->field.tqe_circ.tql_prev =                \
            (elm)->field.tqe_circ.tql_prev;                             \
    } else {                                                            \
        (head)->tqh_circ.tql_prev = (elm)->field.tqe_circ.tql_prev;     \
    }                                                                   \
    qatomic_set(&(elm)->field.tqe_circ.tql_prev->tql_next,              \
                (elm)->field.tqe_next);                                 \
    (elm)->field.tqe_circ.tql_prev = NULL;                              \
} while (/*CONSTCOND*/0)

#define QTAILQ_FOREACH_RCU(var, head, field)                            \
    for ((var) = qatomic_rcu_read(&(head)->tqh_first);                  \
         (var);                                                         \
         (var) = qatomic_rcu_read(&(var)->field.tqe_next))

#define QTAILQ_FOREACH_SAFE_RCU(var, head, field, next)                  \
    for ((var) = qatomic_rcu_read(&(head)->tqh_first);                   \
         (var) && ((next) = qatomic_rcu_read(&(var)->field.tqe_next), 1);\
         (var) = (next))

/*
 * RCU singly-linked list
 */

/* Singly-linked list access methods */
#define QSLIST_EMPTY_RCU(head)      (qatomic_read(&(head)->slh_first) == NULL)
#define QSLIST_FIRST_RCU(head)       qatomic_rcu_read(&(head)->slh_first)
#define QSLIST_NEXT_RCU(elm, field)  qatomic_rcu_read(&(elm)->field.sle_next)

/* Singly-linked list functions */
#define QSLIST_INSERT_HEAD_RCU(head, elm, field) do {           \
    (elm)->field.sle_next = (head)->slh_first;                  \
    qatomic_rcu_set(&(head)->slh_first, (elm));                 \
} while (/*CONSTCOND*/0)

#define QSLIST_INSERT_AFTER_RCU(head, listelm, elm, field) do {         \
    (elm)->field.sle_next = (listelm)->field.sle_next;                  \
    qatomic_rcu_set(&(listelm)->field.sle_next, (elm));                 \
} while (/*CONSTCOND*/0)

#define QSLIST_REMOVE_HEAD_RCU(head, field) do {                       \
    qatomic_set(&(head)->slh_first, (head)->slh_first->field.sle_next);\
} while (/*CONSTCOND*/0)

#define QSLIST_REMOVE_RCU(head, elm, type, field) do {              \
    if ((head)->slh_first == (elm)) {                               \
        QSLIST_REMOVE_HEAD_RCU((head), field);                      \
    } else {                                                        \
        struct type *curr = (head)->slh_first;                      \
        while (curr->field.sle_next != (elm)) {                     \
            curr = curr->field.sle_next;                            \
        }                                                           \
        qatomic_set(&curr->field.sle_next,                          \
                   curr->field.sle_next->field.sle_next);           \
    }                                                               \
} while (/*CONSTCOND*/0)

#define QSLIST_FOREACH_RCU(var, head, field)                          \
    for ((var) = qatomic_rcu_read(&(head)->slh_first);                \
         (var);                                                       \
         (var) = qatomic_rcu_read(&(var)->field.sle_next))

#define QSLIST_FOREACH_SAFE_RCU(var, head, field, next)                   \
    for ((var) = qatomic_rcu_read(&(head)->slh_first);                    \
         (var) && ((next) = qatomic_rcu_read(&(var)->field.sle_next), 1); \
         (var) = (next))

#ifdef __cplusplus
}
#endif
#endif /* QEMU_RCU_QUEUE_H */
