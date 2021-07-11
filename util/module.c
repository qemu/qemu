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
#ifdef CONFIG_MODULES
#include <gmodule.h>
#endif
#include "qemu/queue.h"
#include "qemu/module.h"
#include "qemu/cutils.h"
#include "qemu/config-file.h"
#ifdef CONFIG_MODULE_UPGRADES
#include "qemu-version.h"
#endif
#include "trace.h"

typedef struct ModuleEntry
{
    void (*init)(void);
    QTAILQ_ENTRY(ModuleEntry) node;
    module_init_type type;
} ModuleEntry;

typedef QTAILQ_HEAD(, ModuleEntry) ModuleTypeList;

static ModuleTypeList init_type_list[MODULE_INIT_MAX];
static bool modules_init_done[MODULE_INIT_MAX];

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
    init_lists();

    return &init_type_list[type];
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

void module_call_init(module_init_type type)
{
    ModuleTypeList *l;
    ModuleEntry *e;

    if (modules_init_done[type]) {
        return;
    }

    l = find_type(type);

    QTAILQ_FOREACH(e, l, node) {
        e->init();
    }

    modules_init_done[type] = true;
}

#ifdef CONFIG_MODULES

static const QemuModinfo module_info_stub[] = { {
    /* end of list */
} };
static const QemuModinfo *module_info = module_info_stub;
static const char *module_arch;

void module_init_info(const QemuModinfo *info)
{
    module_info = info;
}

void module_allow_arch(const char *arch)
{
    module_arch = arch;
}

static bool module_check_arch(const QemuModinfo *modinfo)
{
    if (modinfo->arch) {
        if (!module_arch) {
            /* no arch set -> ignore all */
            return false;
        }
        if (strcmp(module_arch, modinfo->arch) != 0) {
            /* mismatch */
            return false;
        }
    }
    return true;
}

static int module_load_file(const char *fname, bool mayfail, bool export_symbols)
{
    GModule *g_module;
    void (*sym)(void);
    const char *dsosuf = CONFIG_HOST_DSOSUF;
    int len = strlen(fname);
    int suf_len = strlen(dsosuf);
    ModuleEntry *e, *next;
    int ret, flags;

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

    flags = 0;
    if (!export_symbols) {
        flags |= G_MODULE_BIND_LOCAL;
    }
    g_module = g_module_open(fname, flags);
    if (!g_module) {
        if (!mayfail) {
            fprintf(stderr, "Failed to open module: %s\n",
                    g_module_error());
        }
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
            e->init();
            register_module_init(e->init, e->type);
        }
        ret = 0;
    }

    trace_module_load_module(fname);
    QTAILQ_FOREACH_SAFE(e, &dso_init_list, node, next) {
        QTAILQ_REMOVE(&dso_init_list, e, node);
        g_free(e);
    }
out:
    return ret;
}
#endif

bool module_load_one(const char *prefix, const char *lib_name, bool mayfail)
{
    bool success = false;

#ifdef CONFIG_MODULES
    char *fname = NULL;
#ifdef CONFIG_MODULE_UPGRADES
    char *version_dir;
#endif
    const char *search_dir;
    char *dirs[5];
    char *module_name;
    int i = 0, n_dirs = 0;
    int ret;
    bool export_symbols = false;
    static GHashTable *loaded_modules;
    const QemuModinfo *modinfo;
    const char **sl;

    if (!g_module_supported()) {
        fprintf(stderr, "Module is not supported by system.\n");
        return false;
    }

    if (!loaded_modules) {
        loaded_modules = g_hash_table_new(g_str_hash, g_str_equal);
    }

    module_name = g_strdup_printf("%s%s", prefix, lib_name);

    if (g_hash_table_contains(loaded_modules, module_name)) {
        g_free(module_name);
        return true;
    }
    g_hash_table_add(loaded_modules, module_name);

    for (modinfo = module_info; modinfo->name != NULL; modinfo++) {
        if (modinfo->arch) {
            if (strcmp(modinfo->name, module_name) == 0) {
                if (!module_check_arch(modinfo)) {
                    return false;
                }
            }
        }
        if (modinfo->deps) {
            if (strcmp(modinfo->name, module_name) == 0) {
                /* we depend on other module(s) */
                for (sl = modinfo->deps; *sl != NULL; sl++) {
                    module_load_one("", *sl, false);
                }
            } else {
                for (sl = modinfo->deps; *sl != NULL; sl++) {
                    if (strcmp(module_name, *sl) == 0) {
                        /* another module depends on us */
                        export_symbols = true;
                    }
                }
            }
        }
    }

    search_dir = getenv("QEMU_MODULE_DIR");
    if (search_dir != NULL) {
        dirs[n_dirs++] = g_strdup_printf("%s", search_dir);
    }
    dirs[n_dirs++] = get_relocated_path(CONFIG_QEMU_MODDIR);
    dirs[n_dirs++] = g_strdup(qemu_get_exec_dir());

#ifdef CONFIG_MODULE_UPGRADES
    version_dir = g_strcanon(g_strdup(QEMU_PKGVERSION),
                             G_CSET_A_2_Z G_CSET_a_2_z G_CSET_DIGITS "+-.~",
                             '_');
    dirs[n_dirs++] = g_strdup_printf("/var/run/qemu/%s", version_dir);
#endif

    assert(n_dirs <= ARRAY_SIZE(dirs));

    for (i = 0; i < n_dirs; i++) {
        fname = g_strdup_printf("%s/%s%s",
                dirs[i], module_name, CONFIG_HOST_DSOSUF);
        ret = module_load_file(fname, mayfail, export_symbols);
        g_free(fname);
        fname = NULL;
        /* Try loading until loaded a module file */
        if (!ret) {
            success = true;
            break;
        }
    }

    if (!success) {
        g_hash_table_remove(loaded_modules, module_name);
        g_free(module_name);
    }

    for (i = 0; i < n_dirs; i++) {
        g_free(dirs[i]);
    }

#endif
    return success;
}

#ifdef CONFIG_MODULES

static bool module_loaded_qom_all;

void module_load_qom_one(const char *type)
{
    const QemuModinfo *modinfo;
    const char **sl;

    if (!type) {
        return;
    }

    trace_module_lookup_object_type(type);
    for (modinfo = module_info; modinfo->name != NULL; modinfo++) {
        if (!modinfo->objs) {
            continue;
        }
        if (!module_check_arch(modinfo)) {
            continue;
        }
        for (sl = modinfo->objs; *sl != NULL; sl++) {
            if (strcmp(type, *sl) == 0) {
                module_load_one("", modinfo->name, false);
            }
        }
    }
}

void module_load_qom_all(void)
{
    const QemuModinfo *modinfo;

    if (module_loaded_qom_all) {
        return;
    }

    for (modinfo = module_info; modinfo->name != NULL; modinfo++) {
        if (!modinfo->objs) {
            continue;
        }
        if (!module_check_arch(modinfo)) {
            continue;
        }
        module_load_one("", modinfo->name, false);
    }
    module_loaded_qom_all = true;
}

void qemu_load_module_for_opts(const char *group)
{
    const QemuModinfo *modinfo;
    const char **sl;

    for (modinfo = module_info; modinfo->name != NULL; modinfo++) {
        if (!modinfo->opts) {
            continue;
        }
        for (sl = modinfo->opts; *sl != NULL; sl++) {
            if (strcmp(group, *sl) == 0) {
                module_load_one("", modinfo->name, false);
            }
        }
    }
}

#else

void module_allow_arch(const char *arch) {}
void qemu_load_module_for_opts(const char *group) {}
void module_load_qom_one(const char *type) {}
void module_load_qom_all(void) {}

#endif
