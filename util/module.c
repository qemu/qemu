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
#ifdef CONFIG_MODULE_UPGRADES
#include "qemu-version.h"
#endif

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
static int module_load_file(const char *fname)
{
    GModule *g_module;
    void (*sym)(void);
    const char *dsosuf = CONFIG_HOST_DSOSUF;
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
            e->init();
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

bool module_load_one(const char *prefix, const char *lib_name)
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
    static GHashTable *loaded_modules;

    if (!g_module_supported()) {
        fprintf(stderr, "Module is not supported by system.\n");
        return false;
    }

    if (!loaded_modules) {
        loaded_modules = g_hash_table_new(g_str_hash, g_str_equal);
    }

    module_name = g_strdup_printf("%s%s", prefix, lib_name);

    if (!g_hash_table_add(loaded_modules, module_name)) {
        g_free(module_name);
        return true;
    }

    search_dir = getenv("QEMU_MODULE_DIR");
    if (search_dir != NULL) {
        dirs[n_dirs++] = g_strdup_printf("%s", search_dir);
    }
    dirs[n_dirs++] = g_strdup_printf("%s", CONFIG_QEMU_MODDIR);
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
        ret = module_load_file(fname);
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

/*
 * Building devices and other qom objects modular is mostly useful in
 * case they have dependencies to external shared libraries, so we can
 * cut down the core qemu library dependencies.  Which is the case for
 * only a very few devices & objects.
 *
 * So with the expectation that this will be rather the exception than
 * to rule and the list will not gain that many entries go with a
 * simple manually maintained list for now.
 */
static struct {
    const char *type;
    const char *prefix;
    const char *module;
} const qom_modules[] = {
    { "ccid-card-passthru",    "hw-", "usb-smartcard"         },
    { "ccid-card-emulated",    "hw-", "usb-smartcard"         },
    { "usb-redir",             "hw-", "usb-redirect"          },
    { "qxl-vga",               "hw-", "display-qxl"           },
    { "qxl",                   "hw-", "display-qxl"           },
    { "virtio-gpu-device",     "hw-", "display-virtio-gpu"    },
    { "vhost-user-gpu",        "hw-", "display-virtio-gpu"    },
    { "chardev-braille",       "chardev-", "baum"             },
};

static bool module_loaded_qom_all;

void module_load_qom_one(const char *type)
{
    int i;

    if (!type) {
        return;
    }
    if (module_loaded_qom_all) {
        return;
    }
    for (i = 0; i < ARRAY_SIZE(qom_modules); i++) {
        if (strcmp(qom_modules[i].type, type) == 0) {
            module_load_one(qom_modules[i].prefix,
                            qom_modules[i].module);
            return;
        }
    }
}

void module_load_qom_all(void)
{
    int i;

    if (module_loaded_qom_all) {
        return;
    }
    for (i = 0; i < ARRAY_SIZE(qom_modules); i++) {
        if (i > 0 && (strcmp(qom_modules[i - 1].module,
                             qom_modules[i].module) == 0 &&
                      strcmp(qom_modules[i - 1].prefix,
                             qom_modules[i].prefix) == 0)) {
            /* one module implementing multiple types -> load only once */
            continue;
        }
        module_load_one(qom_modules[i].prefix, qom_modules[i].module);
    }
    module_loaded_qom_all = true;
}
