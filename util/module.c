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
#include "qapi/error.h"
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

/*
 * module_load_dso: attempt to load an existing dso file
 *
 * fname:          full pathname of the file to load
 * export_symbols: if true, add the symbols to the global name space
 * errp:           error to set.
 *
 * Return value:   true on success, false on error, and errp will be set.
 */
static bool module_load_dso(const char *fname, bool export_symbols,
                            Error **errp)
{
    GModule *g_module;
    void (*sym)(void);
    ModuleEntry *e, *next;
    int flags;

    assert(QTAILQ_EMPTY(&dso_init_list));

    flags = 0;
    if (!export_symbols) {
        flags |= G_MODULE_BIND_LOCAL;
    }
    g_module = g_module_open(fname, flags);
    if (!g_module) {
        error_setg(errp, "failed to open module: %s", g_module_error());
        return false;
    }
    if (!g_module_symbol(g_module, DSO_STAMP_FUN_STR, (gpointer *)&sym)) {
        error_setg(errp, "failed to initialize module: %s", fname);
        /*
         * Print some info if this is a QEMU module (but from different build),
         * this will make debugging user problems easier.
         */
        if (g_module_symbol(g_module, "qemu_module_dummy", (gpointer *)&sym)) {
            error_append_hint(errp,
                "Only modules from the same build can be loaded.\n");
        }
        g_module_close(g_module);
        return false;
    }

    QTAILQ_FOREACH(e, &dso_init_list, node) {
        e->init();
        register_module_init(e->init, e->type);
    }
    trace_module_load_module(fname);
    QTAILQ_FOREACH_SAFE(e, &dso_init_list, node, next) {
        QTAILQ_REMOVE(&dso_init_list, e, node);
        g_free(e);
    }
    return true;
}

int module_load(const char *prefix, const char *name, Error **errp)
{
    int rv = -1;
#ifdef CONFIG_MODULE_UPGRADES
    char *version_dir;
#endif
    const char *search_dir;
    char *dirs[5];
    char *module_name;
    int i = 0, n_dirs = 0;
    bool export_symbols = false;
    static GHashTable *loaded_modules;
    const QemuModinfo *modinfo;
    const char **sl;

    if (!g_module_supported()) {
        error_setg(errp, "%s", "this platform does not support GLib modules");
        return -1;
    }

    if (!loaded_modules) {
        loaded_modules = g_hash_table_new(g_str_hash, g_str_equal);
    }

    /* allocate all resources managed by the out: label here */
    module_name = g_strdup_printf("%s%s", prefix, name);

    if (g_hash_table_contains(loaded_modules, module_name)) {
        g_free(module_name);
        return 2; /* module already loaded */
    }
    g_hash_table_add(loaded_modules, module_name);

    search_dir = getenv("QEMU_MODULE_DIR");
    if (search_dir != NULL) {
        dirs[n_dirs++] = g_strdup(search_dir);
    }
    dirs[n_dirs++] = get_relocated_path(CONFIG_QEMU_MODDIR);

#ifdef CONFIG_MODULE_UPGRADES
    version_dir = g_strcanon(g_strdup(QEMU_PKGVERSION),
                             G_CSET_A_2_Z G_CSET_a_2_z G_CSET_DIGITS "+-.~",
                             '_');
    dirs[n_dirs++] = g_strdup_printf("/var/run/qemu/%s", version_dir);
#endif
    assert(n_dirs <= ARRAY_SIZE(dirs));

    /* end of resources managed by the out: label */

    for (modinfo = module_info; modinfo->name != NULL; modinfo++) {
        if (modinfo->arch) {
            if (strcmp(modinfo->name, module_name) == 0) {
                if (!module_check_arch(modinfo)) {
                    error_setg(errp, "module arch does not match: "
                        "expected '%s', got '%s'", module_arch, modinfo->arch);
                    goto out;
                }
            }
        }
        if (modinfo->deps) {
            if (strcmp(modinfo->name, module_name) == 0) {
                /* we depend on other module(s) */
                for (sl = modinfo->deps; *sl != NULL; sl++) {
                    int subrv = module_load("", *sl, errp);
                    if (subrv <= 0) {
                        rv = subrv;
                        goto out;
                    }
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

    for (i = 0; i < n_dirs; i++) {
        char *fname = g_strdup_printf("%s/%s%s",
                                      dirs[i], module_name, CONFIG_HOST_DSOSUF);
        int ret = access(fname, F_OK);
        if (ret != 0 && (errno == ENOENT || errno == ENOTDIR)) {
            /*
             * if we don't find the module in this dir, try the next one.
             * If we don't find it in any dir, that can be fine too: user
             * did not install the module. We will return 0 in this case
             * with no error set.
             */
            g_free(fname);
            continue;
        } else if (ret != 0) {
            /* most common is EACCES here */
            error_setg_errno(errp, errno, "error trying to access %s", fname);
        } else if (module_load_dso(fname, export_symbols, errp)) {
            rv = 1; /* module successfully loaded */
        }
        g_free(fname);
        goto out;
    }
    rv = 0; /* module not found */

out:
    if (rv <= 0) {
        g_hash_table_remove(loaded_modules, module_name);
        g_free(module_name);
    }
    for (i = 0; i < n_dirs; i++) {
        g_free(dirs[i]);
    }
    return rv;
}

static bool module_loaded_qom_all;

int module_load_qom(const char *type, Error **errp)
{
    const QemuModinfo *modinfo;
    const char **sl;
    int rv = 0;

    if (!type) {
        error_setg(errp, "%s", "type is NULL");
        return -1;
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
                if (rv > 0) {
                    error_setg(errp, "multiple modules providing '%s'", type);
                    return -1;
                }
                rv = module_load("", modinfo->name, errp);
                if (rv < 0) {
                    return rv;
                }
            }
        }
    }
    return rv;
}

void module_load_qom_all(void)
{
    const QemuModinfo *modinfo;

    if (module_loaded_qom_all) {
        return;
    }

    for (modinfo = module_info; modinfo->name != NULL; modinfo++) {
        Error *local_err = NULL;
        if (!modinfo->objs) {
            continue;
        }
        if (!module_check_arch(modinfo)) {
            continue;
        }
        if (module_load("", modinfo->name, &local_err) < 0) {
            error_report_err(local_err);
        }
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
                Error *local_err = NULL;
                if (module_load("", modinfo->name, &local_err) < 0) {
                    error_report_err(local_err);
                }
            }
        }
    }
}

#else

void module_allow_arch(const char *arch) {}
void qemu_load_module_for_opts(const char *group) {}
int module_load(const char *prefix, const char *name, Error **errp) { return 2; }
int module_load_qom(const char *type, Error **errp) { return 2; }
void module_load_qom_all(void) {}

#endif
