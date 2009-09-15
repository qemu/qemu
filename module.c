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
#include "qemu-queue.h"
#include "module.h"

typedef struct ModuleEntry
{
    module_init_type type;
    void (*init)(void);
    QTAILQ_ENTRY(ModuleEntry) node;
} ModuleEntry;

typedef QTAILQ_HEAD(, ModuleEntry) ModuleTypeList;

static ModuleTypeList init_type_list[MODULE_INIT_MAX];

static void init_types(void)
{
    static int inited;
    int i;

    if (inited) {
        return;
    }

    for (i = 0; i < MODULE_INIT_MAX; i++) {
        QTAILQ_INIT(&init_type_list[i]);
    }

    inited = 1;
}


static ModuleTypeList *find_type(module_init_type type)
{
    ModuleTypeList *l;

    init_types();

    l = &init_type_list[type];

    return l;
}

void register_module_init(void (*fn)(void), module_init_type type)
{
    ModuleEntry *e;
    ModuleTypeList *l;

    e = qemu_mallocz(sizeof(*e));
    e->init = fn;

    l = find_type(type);

    QTAILQ_INSERT_TAIL(l, e, node);
}

void module_call_init(module_init_type type)
{
    ModuleTypeList *l;
    ModuleEntry *e;

    l = find_type(type);

    QTAILQ_FOREACH(e, l, node) {
        e->init();
    }
}
