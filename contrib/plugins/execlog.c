/*
 * Copyright (C) 2021, Alexandre Iooss <erdnaxe@crans.org>
 *
 * Log instruction execution with memory access and register changes
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#include <glib.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <qemu-plugin.h>

typedef struct {
    struct qemu_plugin_register *handle;
    GByteArray *last;
    GByteArray *new;
    const char *name;
} Register;

typedef struct CPU {
    /* Store last executed instruction on each vCPU as a GString */
    GString *last_exec;
    /* Ptr array of Register */
    GPtrArray *registers;
} CPU;

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static GArray *cpus;
static GRWLock expand_array_lock;

static GPtrArray *imatches;
static GArray *amatches;
static GPtrArray *rmatches;
static bool disas_assist;
static GMutex add_reg_name_lock;
static GPtrArray *all_reg_names;

static CPU *get_cpu(int vcpu_index)
{
    CPU *c;
    g_rw_lock_reader_lock(&expand_array_lock);
    c = &g_array_index(cpus, CPU, vcpu_index);
    g_rw_lock_reader_unlock(&expand_array_lock);

    return c;
}

/**
 * Add memory read or write information to current instruction log
 */
static void vcpu_mem(unsigned int cpu_index, qemu_plugin_meminfo_t info,
                     uint64_t vaddr, void *udata)
{
    CPU *c = get_cpu(cpu_index);
    GString *s = c->last_exec;

    /* Find vCPU in array */

    /* Indicate type of memory access */
    if (qemu_plugin_mem_is_store(info)) {
        g_string_append(s, ", store");
    } else {
        g_string_append(s, ", load");
    }

    /* If full system emulation log physical address and device name */
    struct qemu_plugin_hwaddr *hwaddr = qemu_plugin_get_hwaddr(info, vaddr);
    if (hwaddr) {
        uint64_t addr = qemu_plugin_hwaddr_phys_addr(hwaddr);
        const char *name = qemu_plugin_hwaddr_device_name(hwaddr);
        g_string_append_printf(s, ", 0x%08"PRIx64", %s", addr, name);
    } else {
        g_string_append_printf(s, ", 0x%08"PRIx64, vaddr);
    }
}

/**
 * Log instruction execution, outputting the last one.
 *
 * vcpu_insn_exec() is a copy and paste of vcpu_insn_exec_with_regs()
 * without the checking of register values when we've attempted to
 * optimise with disas_assist.
 */
static void insn_check_regs(CPU *cpu)
{
    for (int n = 0; n < cpu->registers->len; n++) {
        Register *reg = cpu->registers->pdata[n];
        int sz;

        g_byte_array_set_size(reg->new, 0);
        sz = qemu_plugin_read_register(reg->handle, reg->new);
        g_assert(sz == reg->last->len);

        if (memcmp(reg->last->data, reg->new->data, sz)) {
            GByteArray *temp = reg->last;
            g_string_append_printf(cpu->last_exec, ", %s -> 0x", reg->name);
            /* TODO: handle BE properly */
            for (int i = sz - 1; i >= 0; i--) {
                g_string_append_printf(cpu->last_exec, "%02x",
                                       reg->new->data[i]);
            }
            reg->last = reg->new;
            reg->new = temp;
        }
    }
}

/* Log last instruction while checking registers */
static void vcpu_insn_exec_with_regs(unsigned int cpu_index, void *udata)
{
    CPU *cpu = get_cpu(cpu_index);

    /* Print previous instruction in cache */
    if (cpu->last_exec->len) {
        if (cpu->registers) {
            insn_check_regs(cpu);
        }

        qemu_plugin_outs(cpu->last_exec->str);
        qemu_plugin_outs("\n");
    }

    /* Store new instruction in cache */
    /* vcpu_mem will add memory access information to last_exec */
    g_string_printf(cpu->last_exec, "%u, ", cpu_index);
    g_string_append(cpu->last_exec, (char *)udata);
}

/* Log last instruction while checking registers, ignore next */
static void vcpu_insn_exec_only_regs(unsigned int cpu_index, void *udata)
{
    CPU *cpu = get_cpu(cpu_index);

    /* Print previous instruction in cache */
    if (cpu->last_exec->len) {
        if (cpu->registers) {
            insn_check_regs(cpu);
        }

        qemu_plugin_outs(cpu->last_exec->str);
        qemu_plugin_outs("\n");
    }

    /* reset */
    cpu->last_exec->len = 0;
}

/* Log last instruction without checking regs, setup next */
static void vcpu_insn_exec(unsigned int cpu_index, void *udata)
{
    CPU *cpu = get_cpu(cpu_index);

    /* Print previous instruction in cache */
    if (cpu->last_exec->len) {
        qemu_plugin_outs(cpu->last_exec->str);
        qemu_plugin_outs("\n");
    }

    /* Store new instruction in cache */
    /* vcpu_mem will add memory access information to last_exec */
    g_string_printf(cpu->last_exec, "%u, ", cpu_index);
    g_string_append(cpu->last_exec, (char *)udata);
}

/**
 * On translation block new translation
 *
 * QEMU convert code by translation block (TB). By hooking here we can then hook
 * a callback on each instruction and memory access.
 */
static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    struct qemu_plugin_insn *insn;
    bool skip = (imatches || amatches);
    bool check_regs_this = rmatches;
    bool check_regs_next = false;

    size_t n_insns = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n_insns; i++) {
        char *insn_disas;
        uint64_t insn_vaddr;

        /*
         * `insn` is shared between translations in QEMU, copy needed data here.
         * `output` is never freed as it might be used multiple times during
         * the emulation lifetime.
         * We only consider the first 32 bits of the instruction, this may be
         * a limitation for CISC architectures.
         */
        insn = qemu_plugin_tb_get_insn(tb, i);
        insn_disas = qemu_plugin_insn_disas(insn);
        insn_vaddr = qemu_plugin_insn_vaddr(insn);

        /*
         * If we are filtering we better check out if we have any
         * hits. The skip "latches" so we can track memory accesses
         * after the instruction we care about. Also enable register
         * checking on the next instruction.
         */
        if (skip && imatches) {
            int j;
            for (j = 0; j < imatches->len && skip; j++) {
                char *m = g_ptr_array_index(imatches, j);
                if (g_str_has_prefix(insn_disas, m)) {
                    skip = false;
                    check_regs_next = rmatches;
                }
            }
        }

        if (skip && amatches) {
            int j;
            for (j = 0; j < amatches->len && skip; j++) {
                uint64_t v = g_array_index(amatches, uint64_t, j);
                if (v == insn_vaddr) {
                    skip = false;
                }
            }
        }

        /*
         * Check the disassembly to see if a register we care about
         * will be affected by this instruction. This relies on the
         * dissembler doing something sensible for the registers we
         * care about.
         */
        if (disas_assist && rmatches) {
            check_regs_next = false;
            gchar *args = g_strstr_len(insn_disas, -1, " ");
            for (int n = 0; n < all_reg_names->len; n++) {
                gchar *reg = g_ptr_array_index(all_reg_names, n);
                if (g_strrstr(args, reg)) {
                    check_regs_next = true;
                    skip = false;
                }
            }
        }

        /*
         * We now have 3 choices:
         *
         * - Log insn
         * - Log insn while checking registers
         * - Don't log this insn but check if last insn changed registers
         */

        if (skip) {
            if (check_regs_this) {
                qemu_plugin_register_vcpu_insn_exec_cb(insn,
                                                       vcpu_insn_exec_only_regs,
                                                       QEMU_PLUGIN_CB_R_REGS,
                                                       NULL);
            }
        } else {
            uint32_t insn_opcode = 0;
            qemu_plugin_insn_data(insn, &insn_opcode, sizeof(insn_opcode));

            char *output = g_strdup_printf("0x%"PRIx64", 0x%"PRIx32", \"%s\"",
                                           insn_vaddr, insn_opcode, insn_disas);

            /* Register callback on memory read or write */
            qemu_plugin_register_vcpu_mem_cb(insn, vcpu_mem,
                                             QEMU_PLUGIN_CB_NO_REGS,
                                             QEMU_PLUGIN_MEM_RW, NULL);

            /* Register callback on instruction */
            if (check_regs_this) {
                qemu_plugin_register_vcpu_insn_exec_cb(
                    insn, vcpu_insn_exec_with_regs,
                    QEMU_PLUGIN_CB_R_REGS,
                    output);
            } else {
                qemu_plugin_register_vcpu_insn_exec_cb(
                    insn, vcpu_insn_exec,
                    QEMU_PLUGIN_CB_NO_REGS,
                    output);
            }

            /* reset skip */
            skip = (imatches || amatches);
        }

        /* set regs for next */
        if (disas_assist && rmatches) {
            check_regs_this = check_regs_next;
        }

        g_free(insn_disas);
    }
}

static Register *init_vcpu_register(qemu_plugin_reg_descriptor *desc)
{
    Register *reg = g_new0(Register, 1);
    g_autofree gchar *lower = g_utf8_strdown(desc->name, -1);
    int r;

    reg->handle = desc->handle;
    reg->name = g_intern_string(lower);
    reg->last = g_byte_array_new();
    reg->new = g_byte_array_new();

    /* read the initial value */
    r = qemu_plugin_read_register(reg->handle, reg->last);
    g_assert(r > 0);
    return reg;
}

/*
 * g_pattern_match_string has been deprecated in Glib since 2.70 and
 * will complain about it if you try to use it. Fortunately the
 * signature of both functions is the same making it easy to work
 * around.
 */
static inline
gboolean g_pattern_spec_match_string_qemu(GPatternSpec *pspec,
                                          const gchar *string)
{
#if GLIB_CHECK_VERSION(2, 70, 0)
    return g_pattern_spec_match_string(pspec, string);
#else
    return g_pattern_match_string(pspec, string);
#endif
};
#define g_pattern_spec_match_string(p, s) g_pattern_spec_match_string_qemu(p, s)

static GPtrArray *registers_init(int vcpu_index)
{
    g_autoptr(GPtrArray) registers = g_ptr_array_new();
    g_autoptr(GArray) reg_list = qemu_plugin_get_registers();

    if (rmatches && reg_list->len) {
        /*
         * Go through each register in the complete list and
         * see if we want to track it.
         */
        for (int r = 0; r < reg_list->len; r++) {
            qemu_plugin_reg_descriptor *rd = &g_array_index(
                reg_list, qemu_plugin_reg_descriptor, r);
            for (int p = 0; p < rmatches->len; p++) {
                g_autoptr(GPatternSpec) pat = g_pattern_spec_new(rmatches->pdata[p]);
                g_autofree gchar *rd_lower = g_utf8_strdown(rd->name, -1);
                if (g_pattern_spec_match_string(pat, rd->name) ||
                    g_pattern_spec_match_string(pat, rd_lower)) {
                    Register *reg = init_vcpu_register(rd);
                    g_ptr_array_add(registers, reg);

                    /* we need a list of regnames at TB translation time */
                    if (disas_assist) {
                        g_mutex_lock(&add_reg_name_lock);
                        if (!g_ptr_array_find(all_reg_names, reg->name, NULL)) {
                            g_ptr_array_add(all_reg_names, (gpointer)reg->name);
                        }
                        g_mutex_unlock(&add_reg_name_lock);
                    }
                }
            }
        }
    }

    return registers->len ? g_steal_pointer(&registers) : NULL;
}

/*
 * Initialise a new vcpu/thread with:
 *   - last_exec tracking data
 *   - list of tracked registers
 *   - initial value of registers
 *
 * As we could have multiple threads trying to do this we need to
 * serialise the expansion under a lock.
 */
static void vcpu_init(qemu_plugin_id_t id, unsigned int vcpu_index)
{
    CPU *c;

    g_rw_lock_writer_lock(&expand_array_lock);
    if (vcpu_index >= cpus->len) {
        g_array_set_size(cpus, vcpu_index + 1);
    }
    g_rw_lock_writer_unlock(&expand_array_lock);

    c = get_cpu(vcpu_index);
    c->last_exec = g_string_new(NULL);
    c->registers = registers_init(vcpu_index);
}

/**
 * On plugin exit, print last instruction in cache
 */
static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    guint i;
    g_rw_lock_reader_lock(&expand_array_lock);
    for (i = 0; i < cpus->len; i++) {
        CPU *c = get_cpu(i);
        if (c->last_exec && c->last_exec->str) {
            qemu_plugin_outs(c->last_exec->str);
            qemu_plugin_outs("\n");
        }
    }
    g_rw_lock_reader_unlock(&expand_array_lock);
}

/* Add a match to the array of matches */
static void parse_insn_match(char *match)
{
    if (!imatches) {
        imatches = g_ptr_array_new();
    }
    g_ptr_array_add(imatches, g_strdup(match));
}

static void parse_vaddr_match(char *match)
{
    uint64_t v = g_ascii_strtoull(match, NULL, 16);

    if (!amatches) {
        amatches = g_array_new(false, true, sizeof(uint64_t));
    }
    g_array_append_val(amatches, v);
}

/*
 * We have to wait until vCPUs are started before we can check the
 * patterns find anything.
 */
static void add_regpat(char *regpat)
{
    if (!rmatches) {
        rmatches = g_ptr_array_new();
    }
    g_ptr_array_add(rmatches, g_strdup(regpat));
}

/**
 * Install the plugin
 */
QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info, int argc,
                                           char **argv)
{
    /*
     * Initialize dynamic array to cache vCPU instruction. In user mode
     * we don't know the size before emulation.
     */
    cpus = g_array_sized_new(true, true, sizeof(CPU),
                             info->system_emulation ? info->system.max_vcpus : 1);

    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_auto(GStrv) tokens = g_strsplit(opt, "=", 2);
        if (g_strcmp0(tokens[0], "ifilter") == 0) {
            parse_insn_match(tokens[1]);
        } else if (g_strcmp0(tokens[0], "afilter") == 0) {
            parse_vaddr_match(tokens[1]);
        } else if (g_strcmp0(tokens[0], "reg") == 0) {
            add_regpat(tokens[1]);
        } else if (g_strcmp0(tokens[0], "rdisas") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &disas_assist)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", opt);
                return -1;
            }
            all_reg_names = g_ptr_array_new();
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }

    /* Register init, translation block and exit callbacks */
    qemu_plugin_register_vcpu_init_cb(id, vcpu_init);
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);

    return 0;
}
