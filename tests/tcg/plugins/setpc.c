/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2026, Florian Hofhammer <florian.hofhammer@epfl.ch>
 */
#include <assert.h>
#include <glib.h>
#include <inttypes.h>
#include <unistd.h>

#include <qemu-plugin.h>

/* If we detect this magic syscall, ... */
#define MAGIC_SYSCALL 4096
/* ... the plugin either jumps directly to the target address ... */
#define SETPC 0
/* ... or just updates the target address for future use in callbacks. */
#define SETTARGET 1

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static uint64_t source_pc;
static uint64_t target_pc;
static uint64_t target_vaddr;

static bool vcpu_syscall_filter(qemu_plugin_id_t id, unsigned int vcpu_index,
                                int64_t num, uint64_t a1, uint64_t a2,
                                uint64_t a3, uint64_t a4, uint64_t a5,
                                uint64_t a6, uint64_t a7, uint64_t a8,
                                uint64_t *sysret)
{
    if (num == MAGIC_SYSCALL) {
        if (a1 == SETPC) {
            qemu_plugin_outs("Magic syscall detected, jump to clean exit\n");
            qemu_plugin_set_pc(a2);
        } else if (a1 == SETTARGET) {
            qemu_plugin_outs("Magic syscall detected, set target_pc / "
                             "target_vaddr\n");
            source_pc = a2;
            target_pc = a3;
            target_vaddr = a4;
            *sysret = 0;
            return true;
        } else {
            qemu_plugin_outs("Unknown magic syscall argument, ignoring\n");
        }
    }
    return false;
}

static void vcpu_insn_exec(unsigned int vcpu_index, void *userdata)
{
    uint64_t vaddr = (uint64_t)userdata;
    if (vaddr == source_pc) {
        g_assert(target_pc != 0);
        g_assert(target_vaddr == 0);

        qemu_plugin_outs("Marker insn detected, jump to clean return\n");
        qemu_plugin_set_pc(target_pc);
    }
}

static void vcpu_mem_access(unsigned int vcpu_index,
                            qemu_plugin_meminfo_t info,
                            uint64_t vaddr, void *userdata)
{
    if (vaddr != 0 && vaddr == target_vaddr) {
        g_assert(source_pc == 0);
        g_assert(target_pc != 0);

        qemu_plugin_outs("Marker mem access detected, jump to clean return\n");
        qemu_plugin_set_pc(target_pc);
    }
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t insns = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < insns; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t insn_vaddr = qemu_plugin_insn_vaddr(insn);
        /*
         * Note: we cannot only register the callbacks if the instruction is
         * in one of the functions of interest, because symbol lookup for
         * filtering does not work for all architectures (e.g., ppc64).
         */
        qemu_plugin_register_vcpu_insn_exec_cb(insn, vcpu_insn_exec,
                                               QEMU_PLUGIN_CB_RW_REGS_PC,
                                               (void *)insn_vaddr);
        qemu_plugin_register_vcpu_mem_cb(insn, vcpu_mem_access,
                                         QEMU_PLUGIN_CB_RW_REGS_PC,
                                         QEMU_PLUGIN_MEM_R, NULL);
    }
}


QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{

    qemu_plugin_register_vcpu_syscall_filter_cb(id, vcpu_syscall_filter);
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    return 0;
}
