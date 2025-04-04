/*
 * Copyright (c) 2025 Linaro Ltd
 *
 * Test the reset/uninstall cycle of a plugin.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <glib.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;
static qemu_plugin_id_t plugin_id;
static bool was_reset;
static bool was_uninstalled;

static void after_uninstall(qemu_plugin_id_t id)
{
    g_assert(was_reset && !was_uninstalled);
    qemu_plugin_outs("uninstall done\n");
    was_uninstalled = true;
}

static void tb_exec_after_reset(unsigned int vcpu_index, void *userdata)
{
    g_assert(was_reset && !was_uninstalled);
    qemu_plugin_uninstall(plugin_id, after_uninstall);
}

static void tb_trans_after_reset(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    g_assert(was_reset && !was_uninstalled);
    qemu_plugin_register_vcpu_tb_exec_cb(tb, tb_exec_after_reset,
                                         QEMU_PLUGIN_CB_NO_REGS, NULL);
}

static void after_reset(qemu_plugin_id_t id)
{
    g_assert(!was_reset && !was_uninstalled);
    qemu_plugin_outs("reset done\n");
    was_reset = true;
    qemu_plugin_register_vcpu_tb_trans_cb(id, tb_trans_after_reset);
}

static void tb_exec_before_reset(unsigned int vcpu_index, void *userdata)
{
    g_assert(!was_reset && !was_uninstalled);
    qemu_plugin_reset(plugin_id, after_reset);
}

static void tb_trans_before_reset(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    g_assert(!was_reset && !was_uninstalled);
    qemu_plugin_register_vcpu_tb_exec_cb(tb, tb_exec_before_reset,
                                         QEMU_PLUGIN_CB_NO_REGS, NULL);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    plugin_id = id;
    qemu_plugin_register_vcpu_tb_trans_cb(id, tb_trans_before_reset);
    return 0;
}

/* Since we uninstall the plugin, we can't use qemu_plugin_register_atexit_cb,
 * so we use destructor attribute instead. */
static void __attribute__((destructor)) on_plugin_exit(void)
{
    g_assert(was_reset && was_uninstalled);
    qemu_plugin_outs("plugin exit\n");
}
