/*
 * QEMU Module Infrastructure
 *
 * Copyright IBM, Corp. 2009
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "sys-queue.h"
#include "module.h"

typedef struct ModuleEntry
{
    module_init_type type;
    void (*init)(void);
    TAILQ_ENTRY(ModuleEntry) node;
} ModuleEntry;

typedef struct ModuleTypeList
{
    module_init_type type;
    TAILQ_HEAD(, ModuleEntry) entry_list;
    TAILQ_ENTRY(ModuleTypeList) node;
} ModuleTypeList;

static TAILQ_HEAD(, ModuleTypeList) init_type_list;

static ModuleTypeList *find_type_or_alloc(module_init_type type, int alloc)
{
    ModuleTypeList *n;

    TAILQ_FOREACH(n, &init_type_list, node) {
        if (type >= n->type)
            break;
    }

    if (!n || n->type != type) {
        ModuleTypeList *o;

        if (!alloc)
            return NULL;

        o = qemu_mallocz(sizeof(*o));
        o->type = type;
        TAILQ_INIT(&o->entry_list);

        if (n) {
            TAILQ_INSERT_AFTER(&init_type_list, n, o, node);
        } else {
            TAILQ_INSERT_HEAD(&init_type_list, o, node);
        }

        n = o;
    }

    return n;
}

void register_module_init(void (*fn)(void), module_init_type type)
{
    ModuleEntry *e;
    ModuleTypeList *l;

    e = qemu_mallocz(sizeof(*e));
    e->init = fn;

    l = find_type_or_alloc(type, 1);

    TAILQ_INSERT_TAIL(&l->entry_list, e, node);
}

void module_call_init(module_init_type type)
{
    ModuleTypeList *l;
    ModuleEntry *e;

    l = find_type_or_alloc(type, 0);
    if (!l) {
        return;
    }

    TAILQ_FOREACH(e, &l->entry_list, node) {
        e->init();
    }
}
