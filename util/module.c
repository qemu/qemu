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
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#ifdef CONFIG_MODULES
#include <gmodule.h>
#endif
#include "qemu/queue.h"
#include "qemu/module.h"

typedef struct ModuleEntry
{
    void (*init)(void);
    QTAILQ_ENTRY(ModuleEntry) node;
    module_init_type type;
} ModuleEntry;

typedef QTAILQ_HEAD(, ModuleEntry) ModuleTypeList;

static ModuleTypeList init_type_list[MODULE_INIT_MAX];

static ModuleTypeList dso_init_list;

static void init_lists(void)
{
    static int inited;
    int i;

    if (inited) {
        return;
    }

    for (i = 0; i < MODULE_INIT_MAX; i++) {
        QTAILQ_INIT(&init_type_list[i]);
    }

    QTAILQ_INIT(&dso_init_list);

    inited = 1;
}


static ModuleTypeList *find_type(module_init_type type)
{
    ModuleTypeList *l;

    init_lists();

    l = &init_type_list[type];

    return l;
}

void register_module_init(void (*fn)(void), module_init_type type)
{
    ModuleEntry *e;
    ModuleTypeList *l;

    e = g_malloc0(sizeof(*e));
    e->init = fn;
    e->type = type;

    l = find_type(type);

    QTAILQ_INSERT_TAIL(l, e, node);
}

void register_dso_module_init(void (*fn)(void), module_init_type type)
{
    ModuleEntry *e;

    init_lists();

    e = g_malloc0(sizeof(*e));
    e->init = fn;
    e->type = type;

    QTAILQ_INSERT_TAIL(&dso_init_list, e, node);
}

static void module_load(module_init_type type);

void module_call_init(module_init_type type)
{
    ModuleTypeList *l;
    ModuleEntry *e;

    module_load(type);
    l = find_type(type);

    QTAILQ_FOREACH(e, l, node) {
        e->init();
    }
}

#ifdef CONFIG_MODULES
static int module_load_file(const char *fname)
{
    GModule *g_module;
    void (*sym)(void);
    const char *dsosuf = HOST_DSOSUF;
    int len = strlen(fname);
    int suf_len = strlen(dsosuf);
    ModuleEntry *e, *next;
    int ret;

    if (len <= suf_len || strcmp(&fname[len - suf_len], dsosuf)) {
        /* wrong suffix */
        ret = -EINVAL;
        goto out;
    }
    if (access(fname, F_OK)) {
        ret = -ENOENT;
        goto out;
    }

    assert(QTAILQ_EMPTY(&dso_init_list));

    g_module = g_module_open(fname, G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
    if (!g_module) {
        fprintf(stderr, "Failed to open module: %s\n",
                g_module_error());
        ret = -EINVAL;
        goto out;
    }
    if (!g_module_symbol(g_module, DSO_STAMP_FUN_STR, (gpointer *)&sym)) {
        fprintf(stderr, "Failed to initialize module: %s\n",
                fname);
        /* Print some info if this is a QEMU module (but from different build),
         * this will make debugging user problems easier. */
        if (g_module_symbol(g_module, "qemu_module_dummy", (gpointer *)&sym)) {
            fprintf(stderr,
                    "Note: only modules from the same build can be loaded.\n");
        }
        g_module_close(g_module);
        ret = -EINVAL;
    } else {
        QTAILQ_FOREACH(e, &dso_init_list, node) {
            register_module_init(e->init, e->type);
        }
        ret = 0;
    }

    QTAILQ_FOREACH_SAFE(e, &dso_init_list, node, next) {
        QTAILQ_REMOVE(&dso_init_list, e, node);
        g_free(e);
    }
out:
    return ret;
}
#endif

static void module_load(module_init_type type)
{
#ifdef CONFIG_MODULES
    char *fname = NULL;
    const char **mp;
    static const char *block_modules[] = {
        CONFIG_BLOCK_MODULES
    };
    char *exec_dir;
    char *dirs[3];
    int i = 0;
    int ret;

    if (!g_module_supported()) {
        fprintf(stderr, "Module is not supported by system.\n");
        return;
    }

    switch (type) {
    case MODULE_INIT_BLOCK:
        mp = block_modules;
        break;
    default:
        /* no other types have dynamic modules for now*/
        return;
    }

    exec_dir = qemu_get_exec_dir();
    dirs[i++] = g_strdup_printf("%s", CONFIG_QEMU_MODDIR);
    dirs[i++] = g_strdup_printf("%s/..", exec_dir ? : "");
    dirs[i++] = g_strdup_printf("%s", exec_dir ? : "");
    assert(i == ARRAY_SIZE(dirs));
    g_free(exec_dir);
    exec_dir = NULL;

    for ( ; *mp; mp++) {
        for (i = 0; i < ARRAY_SIZE(dirs); i++) {
            fname = g_strdup_printf("%s/%s%s", dirs[i], *mp, HOST_DSOSUF);
            ret = module_load_file(fname);
            g_free(fname);
            fname = NULL;
            /* Try loading until loaded a module file */
            if (!ret) {
                break;
            }
        }
    }

    for (i = 0; i < ARRAY_SIZE(dirs); i++) {
        g_free(dirs[i]);
    }

#endif
}
