/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2026, Florian Hofhammer <florian.hofhammer@epfl.ch>
 */
#include "glib.h"
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

/*
 * This plugin tests whether we can read and write registers via the plugin
 * API. We try to just read/write a single register, as some architectures have
 * registers that cannot be written to, which would fail the test.
 * See: https://lists.gnu.org/archive/html/qemu-devel/2026-02/msg07025.html
 */
static void vcpu_init_cb(qemu_plugin_id_t id, unsigned int vcpu_index)
{
    g_autoptr(GArray) regs = qemu_plugin_get_registers();
    g_assert(regs != NULL);
    g_autoptr(GByteArray) buf = g_byte_array_sized_new(0);
    qemu_plugin_reg_descriptor *reg_desc = NULL;
    bool success = false;

    /* Make sure we can read and write a register not marked as readonly */
    for (size_t i = 0; i < regs->len; i++) {
        reg_desc = &g_array_index(regs, qemu_plugin_reg_descriptor, i);
        if (!reg_desc->is_readonly) {
            g_byte_array_set_size(buf, 0);
            success = qemu_plugin_read_register(reg_desc->handle, buf);
            g_assert(success);
            g_assert(buf->len > 0);
            success = qemu_plugin_write_register(reg_desc->handle, buf);
            g_assert(success);
            break;
        } else {
            reg_desc = NULL;
        }
    }
    g_assert(regs->len == 0 || reg_desc != NULL);

    /*
     * Check whether we can still read a read-only register. On each
     * architecture, at least the PC should be read-only because it's only
     * supposed to be modified via the qemu_plugin_set_pc() function.
     */
    for (size_t i = 0; i < regs->len; i++) {
        reg_desc = &g_array_index(regs, qemu_plugin_reg_descriptor, i);
        if (reg_desc->is_readonly) {
            g_byte_array_set_size(buf, 0);
            success = qemu_plugin_read_register(reg_desc->handle, buf);
            g_assert(success);
            g_assert(buf->len > 0);
            break;
        } else {
            reg_desc = NULL;
        }
    }
    g_assert(regs->len == 0 || reg_desc != NULL);
    /*
     * Note: we currently do not test whether the read-only register can be
     * written to, because doing so would throw an assert in the plugin API.
     */
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    qemu_plugin_register_vcpu_init_cb(id, vcpu_init_cb);
    return 0;
}
