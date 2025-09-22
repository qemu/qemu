/*
 * Copyright (C) 2025, Pierrick Bouvier <pierrick.bouvier@linaro.org>
 *
 * Generates a trace compatible with uftrace (similar to uftrace record).
 * https://github.com/namhyung/uftrace
 *
 * See docs/about/emulation.rst|Uftrace for details and examples.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <qemu-plugin.h>
#include <glib.h>
#include <stdio.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

typedef struct {
    GArray *s;
} Callstack;

typedef struct {
    uint64_t pc;
    uint64_t frame_pointer;
} CallstackEntry;

typedef struct Cpu Cpu;

typedef struct {
    void (*init)(Cpu *cpu);
    void (*end)(Cpu *cpu);
    uint64_t (*get_frame_pointer)(Cpu *cpu);
    bool (*does_insn_modify_frame_pointer)(const char *disas);
} CpuOps;

typedef struct Cpu {
    Callstack *cs;
    GByteArray *buf;
    CpuOps ops;
    void *arch;
} Cpu;

typedef struct {
    struct qemu_plugin_register *reg_fp;
} Aarch64Cpu;

static struct qemu_plugin_scoreboard *score;
static CpuOps arch_ops;

static Callstack *callstack_new(void)
{
    Callstack *cs = g_new0(Callstack, 1);
    cs->s = g_array_new(false, false, sizeof(CallstackEntry));
    return cs;
}

static void callstack_free(Callstack *cs)
{
    g_array_free(cs->s, true);
    cs->s = NULL;
    g_free(cs);
}

static size_t callstack_depth(const Callstack *cs)
{
    return cs->s->len;
}

static size_t callstack_empty(const Callstack *cs)
{
    return callstack_depth(cs) == 0;
}

static void callstack_clear(Callstack *cs)
{
    g_array_set_size(cs->s, 0);
}

static const CallstackEntry *callstack_at(const Callstack *cs, size_t depth)
{
    g_assert(depth > 0);
    g_assert(depth <= callstack_depth(cs));
    return &g_array_index(cs->s, CallstackEntry, depth - 1);
}

static CallstackEntry callstack_top(const Callstack *cs)
{
    if (callstack_depth(cs) >= 1) {
        return *callstack_at(cs, callstack_depth(cs));
    }
    return (CallstackEntry){};
}

static CallstackEntry callstack_caller(const Callstack *cs)
{
    if (callstack_depth(cs) >= 2) {
        return *callstack_at(cs, callstack_depth(cs) - 1);
    }
    return (CallstackEntry){};
}

static void callstack_push(Callstack *cs, CallstackEntry e)
{
    g_array_append_val(cs->s, e);
}

static CallstackEntry callstack_pop(Callstack *cs)
{
    g_assert(!callstack_empty(cs));
    CallstackEntry e = callstack_top(cs);
    g_array_set_size(cs->s, callstack_depth(cs) - 1);
    return e;
}

static uint64_t cpu_read_register64(Cpu *cpu, struct qemu_plugin_register *reg)
{
    GByteArray *buf = cpu->buf;
    g_byte_array_set_size(buf, 0);
    size_t sz = qemu_plugin_read_register(reg, buf);
    g_assert(sz == 8);
    g_assert(buf->len == 8);
    return *((uint64_t *) buf->data);
}

static uint64_t cpu_read_memory64(Cpu *cpu, uint64_t addr)
{
    g_assert(addr);
    GByteArray *buf = cpu->buf;
    g_byte_array_set_size(buf, 0);
    bool read = qemu_plugin_read_memory_vaddr(addr, buf, 8);
    if (!read) {
        return 0;
    }
    g_assert(buf->len == 8);
    return *((uint64_t *) buf->data);
}

static void cpu_unwind_stack(Cpu *cpu, uint64_t frame_pointer, uint64_t pc)
{
    g_assert(callstack_empty(cpu->cs));

    #define UNWIND_STACK_MAX_DEPTH 1024
    CallstackEntry unwind[UNWIND_STACK_MAX_DEPTH];
    size_t depth = 0;
    do {
        /* check we don't have an infinite stack */
        for (size_t i = 0; i < depth; ++i) {
            if (frame_pointer == unwind[i].frame_pointer) {
                break;
            }
        }
        CallstackEntry e = {.frame_pointer = frame_pointer, .pc = pc};
        unwind[depth] = e;
        depth++;
        if (frame_pointer) {
            frame_pointer = cpu_read_memory64(cpu, frame_pointer);
        }
        pc = cpu_read_memory64(cpu, frame_pointer + 8); /* read previous lr */
    } while (frame_pointer && pc && depth < UNWIND_STACK_MAX_DEPTH);
    #undef UNWIND_STACK_MAX_DEPTH

    /* push it from bottom to top */
    while (depth) {
        callstack_push(cpu->cs, unwind[depth - 1]);
        --depth;
    }
}

static struct qemu_plugin_register *plugin_find_register(const char *name)
{
    g_autoptr(GArray) regs = qemu_plugin_get_registers();
    for (int i = 0; i < regs->len; ++i) {
        qemu_plugin_reg_descriptor *reg;
        reg = &g_array_index(regs, qemu_plugin_reg_descriptor, i);
        if (!strcmp(reg->name, name)) {
            return reg->handle;
        }
    }
    return NULL;
}

static uint64_t aarch64_get_frame_pointer(Cpu *cpu_)
{
    Aarch64Cpu *cpu = cpu_->arch;
    return cpu_read_register64(cpu_, cpu->reg_fp);
}

static void aarch64_init(Cpu *cpu_)
{
    Aarch64Cpu *cpu = g_new0(Aarch64Cpu, 1);
    cpu_->arch = cpu;
    cpu->reg_fp = plugin_find_register("x29");
    if (!cpu->reg_fp) {
        fprintf(stderr, "uftrace plugin: frame pointer register (x29) is not "
                        "available. Please use an AArch64 cpu (or -cpu max).\n");
        g_abort();
    }
}

static void aarch64_end(Cpu *cpu)
{
    g_free(cpu->arch);
}

static bool aarch64_does_insn_modify_frame_pointer(const char *disas)
{
    /*
     * Check if current instruction concerns fp register "x29".
     * We add a prefix space to make sure we don't match addresses dump
     * in disassembly.
     */
    return strstr(disas, " x29");
}

static CpuOps aarch64_ops = {
    .init = aarch64_init,
    .end = aarch64_end,
    .get_frame_pointer = aarch64_get_frame_pointer,
    .does_insn_modify_frame_pointer = aarch64_does_insn_modify_frame_pointer,
};

static void track_callstack(unsigned int cpu_index, void *udata)
{
    uint64_t pc = (uintptr_t) udata;
    Cpu *cpu = qemu_plugin_scoreboard_find(score, cpu_index);
    Callstack *cs = cpu->cs;

    uint64_t fp = cpu->ops.get_frame_pointer(cpu);
    if (!fp && callstack_empty(cs)) {
        /*
         * We simply push current pc. Note that we won't detect symbol change as
         * long as a proper call does not happen.
         */
        callstack_push(cs, (CallstackEntry){.frame_pointer = fp, .pc = pc});
        return;
    }

    CallstackEntry top = callstack_top(cs);
    if (fp == top.frame_pointer) {
        /* same function */
        return;
    }

    CallstackEntry caller = callstack_caller(cs);
    if (fp == caller.frame_pointer) {
        /* return */
        callstack_pop(cs);
        return;
    }

    uint64_t caller_fp = fp ? cpu_read_memory64(cpu, fp) : 0;
    if (caller_fp == top.frame_pointer) {
        /* call */
        callstack_push(cs, (CallstackEntry){.frame_pointer = fp, .pc = pc});
        return;
    }

    /* discontinuity, exit current stack and unwind new one */
    callstack_clear(cs);
    cpu_unwind_stack(cpu, fp, pc);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n_insns = qemu_plugin_tb_n_insns(tb);

    /*
     * Callbacks and inline instrumentation are inserted before an instruction.
     * Thus, to see instruction effect, we need to wait for next one.
     * Potentially, the last instruction of a block could modify the frame
     * pointer. Thus, we need to always instrument first instruction in a tb.
     */
    bool instrument_insn = true;
    for (size_t i = 0; i < n_insns; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);

        if (instrument_insn) {
            uintptr_t pc = qemu_plugin_insn_vaddr(insn);
            qemu_plugin_register_vcpu_insn_exec_cb(insn, track_callstack,
                                                   QEMU_PLUGIN_CB_R_REGS,
                                                   (void *) pc);
            instrument_insn = false;
        }

        char *disas = qemu_plugin_insn_disas(insn);
        if (arch_ops.does_insn_modify_frame_pointer(disas)) {
            instrument_insn = true;
        }
    }
}

static void vcpu_init(qemu_plugin_id_t id, unsigned int vcpu_index)
{
    Cpu *cpu = qemu_plugin_scoreboard_find(score, vcpu_index);
    cpu->ops = arch_ops;

    cpu->ops.init(cpu);
    cpu->buf = g_byte_array_new();

    cpu->cs = callstack_new();
}

static void vcpu_end(unsigned int vcpu_index)
{
    Cpu *cpu = qemu_plugin_scoreboard_find(score, vcpu_index);
    g_byte_array_free(cpu->buf, true);

    callstack_free(cpu->cs);
    memset(cpu, 0, sizeof(Cpu));
}

static void at_exit(qemu_plugin_id_t id, void *data)
{
    for (size_t i = 0; i < qemu_plugin_num_vcpus(); ++i) {
        vcpu_end(i);
    }

    qemu_plugin_scoreboard_free(score);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    if (!strcmp(info->target_name, "aarch64")) {
        arch_ops = aarch64_ops;
    } else {
        fprintf(stderr, "plugin uftrace: %s target is not supported\n",
                info->target_name);
        return 1;
    }

    score = qemu_plugin_scoreboard_new(sizeof(Cpu));
    qemu_plugin_register_vcpu_init_cb(id, vcpu_init);
    qemu_plugin_register_atexit_cb(id, at_exit, NULL);
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);

    return 0;
}
