/*
 * Simple transactions API
 *
 * Copyright (c) 2021 Virtuozzo International GmbH.
 *
 * Author:
 *  Vladimir Sementsov-Ogievskiy <vsementsov@virtuozzo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * = Generic transaction API =
 *
 * The intended usage is the following: you create "prepare" functions, which
 * represents the actions. They will usually have Transaction* argument, and
 * call tran_add() to register finalization callbacks. For finalization
 * callbacks, prepare corresponding TransactionActionDrv structures.
 *
 * Then, when you need to make a transaction, create an empty Transaction by
 * tran_create(), call your "prepare" functions on it, and finally call
 * tran_abort() or tran_commit() to finalize the transaction by corresponding
 * finalization actions in reverse order.
 */

#ifndef QEMU_TRANSACTIONS_H
#define QEMU_TRANSACTIONS_H

#include <gmodule.h>

typedef struct TransactionActionDrv {
    void (*abort)(void *opaque);
    void (*commit)(void *opaque);
    void (*clean)(void *opaque);
} TransactionActionDrv;

typedef struct Transaction Transaction;

Transaction *tran_new(void);
void tran_add(Transaction *tran, TransactionActionDrv *drv, void *opaque);
void tran_abort(Transaction *tran);
void tran_commit(Transaction *tran);

static inline void tran_finalize(Transaction *tran, int ret)
{
    if (ret < 0) {
        tran_abort(tran);
    } else {
        tran_commit(tran);
    }
}

#endif /* QEMU_TRANSACTIONS_H */
