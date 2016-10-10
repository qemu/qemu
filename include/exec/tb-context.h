/*
 * Internal structs that QEMU exports to TCG
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_TB_CONTEXT_H
#define QEMU_TB_CONTEXT_H

#include "qemu/thread.h"
#include "qemu/qht.h"

#define CODE_GEN_HTABLE_BITS     15
#define CODE_GEN_HTABLE_SIZE     (1 << CODE_GEN_HTABLE_BITS)

typedef struct TranslationBlock TranslationBlock;
typedef struct TBContext TBContext;

struct TBContext {

    TranslationBlock *tbs;
    struct qht htable;
    int nb_tbs;
    /* any access to the tbs or the page table must use this lock */
    QemuMutex tb_lock;

    /* statistics */
    unsigned tb_flush_count;
    int tb_phys_invalidate_count;
};

#endif
