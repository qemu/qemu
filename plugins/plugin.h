/*
 * Plugin Shared Internal Functions
 *
 * Copyright (C) 2019, Linaro
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _PLUGIN_INTERNAL_H_
#define _PLUGIN_INTERNAL_H_

#include <gmodule.h>

#define QEMU_PLUGIN_MIN_VERSION 0

/* global state */
struct qemu_plugin_state {
    QTAILQ_HEAD(, qemu_plugin_ctx) ctxs;
    QLIST_HEAD(, qemu_plugin_cb) cb_lists[QEMU_PLUGIN_EV_MAX];
    /*
     * Use the HT as a hash map by inserting k == v, which saves memory as
     * documented by GLib. The parent struct is obtained with container_of().
     */
    GHashTable *id_ht;
    /*
     * Use the HT as a hash map. Note that we could use a list here,
     * but with the HT we avoid adding a field to CPUState.
     */
    GHashTable *cpu_ht;
    DECLARE_BITMAP(mask, QEMU_PLUGIN_EV_MAX);
    /*
     * @lock protects the struct as well as ctx->uninstalling.
     * The lock must be acquired by all API ops.
     * The lock is recursive, which greatly simplifies things, e.g.
     * callback registration from qemu_plugin_vcpu_for_each().
     */
    QemuRecMutex lock;
    /*
     * HT of callbacks invoked from helpers. All entries are freed when
     * the code cache is flushed.
     */
    struct qht dyn_cb_arr_ht;
};


struct qemu_plugin_ctx {
    GModule *handle;
    qemu_plugin_id_t id;
    struct qemu_plugin_cb *callbacks[QEMU_PLUGIN_EV_MAX];
    QTAILQ_ENTRY(qemu_plugin_ctx) entry;
    /*
     * keep a reference to @desc until uninstall, so that plugins do not have
     * to strdup plugin args.
     */
    struct qemu_plugin_desc *desc;
    bool installing;
    bool uninstalling;
    bool resetting;
};

struct qemu_plugin_ctx *plugin_id_to_ctx_locked(qemu_plugin_id_t id);

void plugin_register_inline_op(GArray **arr,
                               enum qemu_plugin_mem_rw rw,
                               enum qemu_plugin_op op, void *ptr,
                               uint64_t imm);

void plugin_reset_uninstall(qemu_plugin_id_t id,
                            qemu_plugin_simple_cb_t cb,
                            bool reset);

void plugin_register_cb(qemu_plugin_id_t id, enum qemu_plugin_event ev,
                        void *func);

void plugin_unregister_cb__locked(struct qemu_plugin_ctx *ctx,
                                  enum qemu_plugin_event ev);

void
plugin_register_cb_udata(qemu_plugin_id_t id, enum qemu_plugin_event ev,
                         void *func, void *udata);

void
plugin_register_dyn_cb__udata(GArray **arr,
                              qemu_plugin_vcpu_udata_cb_t cb,
                              enum qemu_plugin_cb_flags flags, void *udata);


void plugin_register_vcpu_mem_cb(GArray **arr,
                                 void *cb,
                                 enum qemu_plugin_cb_flags flags,
                                 enum qemu_plugin_mem_rw rw,
                                 void *udata);

void exec_inline_op(struct qemu_plugin_dyn_cb *cb);

#endif /* _PLUGIN_INTERNAL_H_ */
