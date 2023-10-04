/*
 * QEMU Plugin Core Loader Code
 *
 * This is the code responsible for loading and unloading the plugins.
 * Aside from the basic housekeeping tasks we also need to ensure any
 * generated code is flushed when we remove a plugin so we cannot end
 * up calling and unloaded helper function.
 *
 * Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
 * Copyright (C) 2019, Linaro
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/config-file.h"
#include "qapi/error.h"
#include "qemu/lockable.h"
#include "qemu/option.h"
#include "qemu/rcu_queue.h"
#include "qemu/qht.h"
#include "qemu/bitmap.h"
#include "qemu/cacheinfo.h"
#include "qemu/xxhash.h"
#include "qemu/plugin.h"
#include "qemu/memalign.h"
#include "hw/core/cpu.h"
#include "exec/tb-flush.h"
#ifndef CONFIG_USER_ONLY
#include "hw/boards.h"
#endif
#include "qemu/compiler.h"

#include "plugin.h"

/*
 * For convenience we use a bitmap for plugin.mask, but really all we need is a
 * u32, which is what we store in TranslationBlock.
 */
QEMU_BUILD_BUG_ON(QEMU_PLUGIN_EV_MAX > 32);

struct qemu_plugin_desc {
    char *path;
    char **argv;
    QTAILQ_ENTRY(qemu_plugin_desc) entry;
    int argc;
};

struct qemu_plugin_parse_arg {
    QemuPluginList *head;
    struct qemu_plugin_desc *curr;
};

QemuOptsList qemu_plugin_opts = {
    .name = "plugin",
    .implied_opt_name = "file",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_plugin_opts.head),
    .desc = {
        /* do our own parsing to support multiple plugins */
        { /* end of list */ }
    },
};

typedef int (*qemu_plugin_install_func_t)(qemu_plugin_id_t, const qemu_info_t *, int, char **);

extern struct qemu_plugin_state plugin;

void qemu_plugin_add_dyn_cb_arr(GArray *arr)
{
    uint32_t hash = qemu_xxhash2((uint64_t)(uintptr_t)arr);
    bool inserted;

    inserted = qht_insert(&plugin.dyn_cb_arr_ht, arr, hash, NULL);
    g_assert(inserted);
}

static struct qemu_plugin_desc *plugin_find_desc(QemuPluginList *head,
                                                 const char *path)
{
    struct qemu_plugin_desc *desc;

    QTAILQ_FOREACH(desc, head, entry) {
        if (strcmp(desc->path, path) == 0) {
            return desc;
        }
    }
    return NULL;
}

static int plugin_add(void *opaque, const char *name, const char *value,
                      Error **errp)
{
    struct qemu_plugin_parse_arg *arg = opaque;
    struct qemu_plugin_desc *p;
    bool is_on;
    char *fullarg;

    if (strcmp(name, "file") == 0) {
        if (strcmp(value, "") == 0) {
            error_setg(errp, "requires a non-empty argument");
            return 1;
        }
        p = plugin_find_desc(arg->head, value);
        if (p == NULL) {
            p = g_new0(struct qemu_plugin_desc, 1);
            p->path = g_strdup(value);
            QTAILQ_INSERT_TAIL(arg->head, p, entry);
        }
        arg->curr = p;
    } else {
        if (arg->curr == NULL) {
            error_setg(errp, "missing earlier '-plugin file=' option");
            return 1;
        }

        if (g_strcmp0(name, "arg") == 0 &&
                !qapi_bool_parse(name, value, &is_on, NULL)) {
            if (strchr(value, '=') == NULL) {
                /* Will treat arg="argname" as "argname=on" */
                fullarg = g_strdup_printf("%s=%s", value, "on");
            } else {
                fullarg = g_strdup_printf("%s", value);
            }
            warn_report("using 'arg=%s' is deprecated", value);
            error_printf("Please use '%s' directly\n", fullarg);
        } else {
            fullarg = g_strdup_printf("%s=%s", name, value);
        }

        p = arg->curr;
        p->argc++;
        p->argv = g_realloc_n(p->argv, p->argc, sizeof(char *));
        p->argv[p->argc - 1] = fullarg;
    }

    return 0;
}

void qemu_plugin_opt_parse(const char *optstr, QemuPluginList *head)
{
    struct qemu_plugin_parse_arg arg;
    QemuOpts *opts;

    opts = qemu_opts_parse_noisily(qemu_find_opts("plugin"), optstr, true);
    if (opts == NULL) {
        exit(1);
    }
    arg.head = head;
    arg.curr = NULL;
    qemu_opt_foreach(opts, plugin_add, &arg, &error_fatal);
    qemu_opts_del(opts);
}

/*
 * From: https://en.wikipedia.org/wiki/Xorshift
 * This is faster than rand_r(), and gives us a wider range (RAND_MAX is only
 * guaranteed to be >= INT_MAX).
 */
static uint64_t xorshift64star(uint64_t x)
{
    x ^= x >> 12; /* a */
    x ^= x << 25; /* b */
    x ^= x >> 27; /* c */
    return x * UINT64_C(2685821657736338717);
}

/*
 * Disable CFI checks.
 * The install and version functions have been loaded from an external library
 * so we do not have type information
 */
QEMU_DISABLE_CFI
static int plugin_load(struct qemu_plugin_desc *desc, const qemu_info_t *info, Error **errp)
{
    qemu_plugin_install_func_t install;
    struct qemu_plugin_ctx *ctx;
    gpointer sym;
    int rc;

    ctx = qemu_memalign(qemu_dcache_linesize, sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));
    ctx->desc = desc;

    ctx->handle = g_module_open(desc->path, G_MODULE_BIND_LOCAL);
    if (ctx->handle == NULL) {
        error_setg(errp, "Could not load plugin %s: %s", desc->path, g_module_error());
        goto err_dlopen;
    }

    if (!g_module_symbol(ctx->handle, "qemu_plugin_install", &sym)) {
        error_setg(errp, "Could not load plugin %s: %s", desc->path, g_module_error());
        goto err_symbol;
    }
    install = (qemu_plugin_install_func_t) sym;
    /* symbol was found; it could be NULL though */
    if (install == NULL) {
        error_setg(errp, "Could not load plugin %s: qemu_plugin_install is NULL",
                   desc->path);
        goto err_symbol;
    }

    if (!g_module_symbol(ctx->handle, "qemu_plugin_version", &sym)) {
        error_setg(errp, "Could not load plugin %s: plugin does not declare API version %s",
                   desc->path, g_module_error());
        goto err_symbol;
    } else {
        int version = *(int *)sym;
        if (version < QEMU_PLUGIN_MIN_VERSION) {
            error_setg(errp, "Could not load plugin %s: plugin requires API version %d, but "
                       "this QEMU supports only a minimum version of %d",
                       desc->path, version, QEMU_PLUGIN_MIN_VERSION);
            goto err_symbol;
        } else if (version > QEMU_PLUGIN_VERSION) {
            error_setg(errp, "Could not load plugin %s: plugin requires API version %d, but "
                       "this QEMU supports only up to version %d",
                       desc->path, version, QEMU_PLUGIN_VERSION);
            goto err_symbol;
        }
    }

    qemu_rec_mutex_lock(&plugin.lock);

    /* find an unused random id with &ctx as the seed */
    ctx->id = (uint64_t)(uintptr_t)ctx;
    for (;;) {
        void *existing;

        ctx->id = xorshift64star(ctx->id);
        existing = g_hash_table_lookup(plugin.id_ht, &ctx->id);
        if (likely(existing == NULL)) {
            bool success;

            success = g_hash_table_insert(plugin.id_ht, &ctx->id, &ctx->id);
            g_assert(success);
            break;
        }
    }
    QTAILQ_INSERT_TAIL(&plugin.ctxs, ctx, entry);
    ctx->installing = true;
    rc = install(ctx->id, info, desc->argc, desc->argv);
    ctx->installing = false;
    if (rc) {
        error_setg(errp, "Could not load plugin %s: qemu_plugin_install returned error code %d",
                   desc->path, rc);
        /*
         * we cannot rely on the plugin doing its own cleanup, so
         * call a full uninstall if the plugin did not yet call it.
         */
        if (!ctx->uninstalling) {
            plugin_reset_uninstall(ctx->id, NULL, false);
        }
    }

    qemu_rec_mutex_unlock(&plugin.lock);
    return rc;

 err_symbol:
    g_module_close(ctx->handle);
 err_dlopen:
    qemu_vfree(ctx);
    return 1;
}

/* call after having removed @desc from the list */
static void plugin_desc_free(struct qemu_plugin_desc *desc)
{
    int i;

    for (i = 0; i < desc->argc; i++) {
        g_free(desc->argv[i]);
    }
    g_free(desc->argv);
    g_free(desc->path);
    g_free(desc);
}

/**
 * qemu_plugin_load_list - load a list of plugins
 * @head: head of the list of descriptors of the plugins to be loaded
 *
 * Returns 0 if all plugins in the list are installed, !0 otherwise.
 *
 * Note: the descriptor of each successfully installed plugin is removed
 * from the list given by @head.
 */
int qemu_plugin_load_list(QemuPluginList *head, Error **errp)
{
    struct qemu_plugin_desc *desc, *next;
    g_autofree qemu_info_t *info = g_new0(qemu_info_t, 1);

    info->target_name = TARGET_NAME;
    info->version.min = QEMU_PLUGIN_MIN_VERSION;
    info->version.cur = QEMU_PLUGIN_VERSION;
#ifndef CONFIG_USER_ONLY
    MachineState *ms = MACHINE(qdev_get_machine());
    info->system_emulation = true;
    info->system.smp_vcpus = ms->smp.cpus;
    info->system.max_vcpus = ms->smp.max_cpus;
#else
    info->system_emulation = false;
#endif

    QTAILQ_FOREACH_SAFE(desc, head, entry, next) {
        int err;

        err = plugin_load(desc, info, errp);
        if (err) {
            return err;
        }
        QTAILQ_REMOVE(head, desc, entry);
    }
    return 0;
}

struct qemu_plugin_reset_data {
    struct qemu_plugin_ctx *ctx;
    qemu_plugin_simple_cb_t cb;
    bool reset;
};

static void plugin_reset_destroy__locked(struct qemu_plugin_reset_data *data)
{
    struct qemu_plugin_ctx *ctx = data->ctx;
    enum qemu_plugin_event ev;
    bool success;

    /*
     * After updating the subscription lists there is no need to wait for an RCU
     * grace period to elapse, because right now we either are in a "safe async"
     * work environment (i.e. all vCPUs are asleep), or no vCPUs have yet been
     * created.
     */
    for (ev = 0; ev < QEMU_PLUGIN_EV_MAX; ev++) {
        plugin_unregister_cb__locked(ctx, ev);
    }

    if (data->reset) {
        g_assert(ctx->resetting);
        if (data->cb) {
            data->cb(ctx->id);
        }
        ctx->resetting = false;
        g_free(data);
        return;
    }

    g_assert(ctx->uninstalling);
    /* we cannot dlclose if we are going to return to plugin code */
    if (ctx->installing) {
        error_report("Calling qemu_plugin_uninstall from the install function "
                     "is a bug. Instead, return !0 from the install function.");
        abort();
    }

    success = g_hash_table_remove(plugin.id_ht, &ctx->id);
    g_assert(success);
    QTAILQ_REMOVE(&plugin.ctxs, ctx, entry);
    if (data->cb) {
        data->cb(ctx->id);
    }
    if (!g_module_close(ctx->handle)) {
        warn_report("%s: %s", __func__, g_module_error());
    }
    plugin_desc_free(ctx->desc);
    qemu_vfree(ctx);
    g_free(data);
}

static void plugin_reset_destroy(struct qemu_plugin_reset_data *data)
{
    qemu_rec_mutex_lock(&plugin.lock);
    plugin_reset_destroy__locked(data);
    qemu_rec_mutex_lock(&plugin.lock);
}

static void plugin_flush_destroy(CPUState *cpu, run_on_cpu_data arg)
{
    struct qemu_plugin_reset_data *data = arg.host_ptr;

    g_assert(cpu_in_exclusive_context(cpu));
    tb_flush(cpu);
    plugin_reset_destroy(data);
}

void plugin_reset_uninstall(qemu_plugin_id_t id,
                            qemu_plugin_simple_cb_t cb,
                            bool reset)
{
    struct qemu_plugin_reset_data *data;
    struct qemu_plugin_ctx *ctx;

    WITH_QEMU_LOCK_GUARD(&plugin.lock) {
        ctx = plugin_id_to_ctx_locked(id);
        if (ctx->uninstalling || (reset && ctx->resetting)) {
            return;
        }
        ctx->resetting = reset;
        ctx->uninstalling = !reset;
    }

    data = g_new(struct qemu_plugin_reset_data, 1);
    data->ctx = ctx;
    data->cb = cb;
    data->reset = reset;
    /*
     * Only flush the code cache if the vCPUs have been created. If so,
     * current_cpu must be non-NULL.
     */
    if (current_cpu) {
        async_safe_run_on_cpu(current_cpu, plugin_flush_destroy,
                              RUN_ON_CPU_HOST_PTR(data));
    } else {
        /*
         * If current_cpu isn't set, then we don't have yet any vCPU threads
         * and we therefore can remove the callbacks synchronously.
         */
        plugin_reset_destroy(data);
    }
}
