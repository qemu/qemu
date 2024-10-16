/*
 * gdb server stub
 *
 * This implements a subset of the remote protocol as described in:
 *
 *   https://sourceware.org/gdb/onlinedocs/gdb/Remote-Protocol.html
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/ctype.h"
#include "qemu/cutils.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "trace.h"
#include "exec/gdbstub.h"
#include "gdbstub/commands.h"
#include "gdbstub/syscalls.h"
#ifdef CONFIG_USER_ONLY
#include "accel/tcg/vcpu-state.h"
#include "gdbstub/user.h"
#else
#include "hw/cpu/cluster.h"
#include "hw/boards.h"
#endif
#include "hw/core/cpu.h"

#include "sysemu/hw_accel.h"
#include "sysemu/runstate.h"
#include "exec/replay-core.h"
#include "exec/hwaddr.h"

#include "internals.h"

typedef struct GDBRegisterState {
    int base_reg;
    gdb_get_reg_cb get_reg;
    gdb_set_reg_cb set_reg;
    const GDBFeature *feature;
} GDBRegisterState;

GDBState gdbserver_state;

void gdb_init_gdbserver_state(void)
{
    g_assert(!gdbserver_state.init);
    memset(&gdbserver_state, 0, sizeof(GDBState));
    gdbserver_state.init = true;
    gdbserver_state.str_buf = g_string_new(NULL);
    gdbserver_state.mem_buf = g_byte_array_sized_new(MAX_PACKET_LENGTH);
    gdbserver_state.last_packet = g_byte_array_sized_new(MAX_PACKET_LENGTH + 4);

    /*
     * What single-step modes are supported is accelerator dependent.
     * By default try to use no IRQs and no timers while single
     * stepping so as to make single stepping like a typical ICE HW step.
     */
    gdbserver_state.supported_sstep_flags = accel_supported_gdbstub_sstep_flags();
    gdbserver_state.sstep_flags = SSTEP_ENABLE | SSTEP_NOIRQ | SSTEP_NOTIMER;
    gdbserver_state.sstep_flags &= gdbserver_state.supported_sstep_flags;
}

/* writes 2*len+1 bytes in buf */
void gdb_memtohex(GString *buf, const uint8_t *mem, int len)
{
    int i, c;
    for(i = 0; i < len; i++) {
        c = mem[i];
        g_string_append_c(buf, tohex(c >> 4));
        g_string_append_c(buf, tohex(c & 0xf));
    }
    g_string_append_c(buf, '\0');
}

void gdb_hextomem(GByteArray *mem, const char *buf, int len)
{
    int i;

    for(i = 0; i < len; i++) {
        guint8 byte = fromhex(buf[0]) << 4 | fromhex(buf[1]);
        g_byte_array_append(mem, &byte, 1);
        buf += 2;
    }
}

static void hexdump(const char *buf, int len,
                    void (*trace_fn)(size_t ofs, char const *text))
{
    char line_buffer[3 * 16 + 4 + 16 + 1];

    size_t i;
    for (i = 0; i < len || (i & 0xF); ++i) {
        size_t byte_ofs = i & 15;

        if (byte_ofs == 0) {
            memset(line_buffer, ' ', 3 * 16 + 4 + 16);
            line_buffer[3 * 16 + 4 + 16] = 0;
        }

        size_t col_group = (i >> 2) & 3;
        size_t hex_col = byte_ofs * 3 + col_group;
        size_t txt_col = 3 * 16 + 4 + byte_ofs;

        if (i < len) {
            char value = buf[i];

            line_buffer[hex_col + 0] = tohex((value >> 4) & 0xF);
            line_buffer[hex_col + 1] = tohex((value >> 0) & 0xF);
            line_buffer[txt_col + 0] = (value >= ' ' && value < 127)
                    ? value
                    : '.';
        }

        if (byte_ofs == 0xF)
            trace_fn(i & -16, line_buffer);
    }
}

/* return -1 if error, 0 if OK */
int gdb_put_packet_binary(const char *buf, int len, bool dump)
{
    int csum, i;
    uint8_t footer[3];

    if (dump && trace_event_get_state_backends(TRACE_GDBSTUB_IO_BINARYREPLY)) {
        hexdump(buf, len, trace_gdbstub_io_binaryreply);
    }

    for(;;) {
        g_byte_array_set_size(gdbserver_state.last_packet, 0);
        g_byte_array_append(gdbserver_state.last_packet,
                            (const uint8_t *) "$", 1);
        g_byte_array_append(gdbserver_state.last_packet,
                            (const uint8_t *) buf, len);
        csum = 0;
        for(i = 0; i < len; i++) {
            csum += buf[i];
        }
        footer[0] = '#';
        footer[1] = tohex((csum >> 4) & 0xf);
        footer[2] = tohex((csum) & 0xf);
        g_byte_array_append(gdbserver_state.last_packet, footer, 3);

        gdb_put_buffer(gdbserver_state.last_packet->data,
                   gdbserver_state.last_packet->len);

        if (gdb_got_immediate_ack()) {
            break;
        }
    }
    return 0;
}

/* return -1 if error, 0 if OK */
int gdb_put_packet(const char *buf)
{
    trace_gdbstub_io_reply(buf);

    return gdb_put_packet_binary(buf, strlen(buf), false);
}

void gdb_put_strbuf(void)
{
    gdb_put_packet(gdbserver_state.str_buf->str);
}

/* Encode data using the encoding for 'x' packets.  */
void gdb_memtox(GString *buf, const char *mem, int len)
{
    char c;

    while (len--) {
        c = *(mem++);
        switch (c) {
        case '#': case '$': case '*': case '}':
            g_string_append_c(buf, '}');
            g_string_append_c(buf, c ^ 0x20);
            break;
        default:
            g_string_append_c(buf, c);
            break;
        }
    }
}

static uint32_t gdb_get_cpu_pid(CPUState *cpu)
{
#ifdef CONFIG_USER_ONLY
    return getpid();
#else
    if (cpu->cluster_index == UNASSIGNED_CLUSTER_INDEX) {
        /* Return the default process' PID */
        int index = gdbserver_state.process_num - 1;
        return gdbserver_state.processes[index].pid;
    }
    return cpu->cluster_index + 1;
#endif
}

GDBProcess *gdb_get_process(uint32_t pid)
{
    int i;

    if (!pid) {
        /* 0 means any process, we take the first one */
        return &gdbserver_state.processes[0];
    }

    for (i = 0; i < gdbserver_state.process_num; i++) {
        if (gdbserver_state.processes[i].pid == pid) {
            return &gdbserver_state.processes[i];
        }
    }

    return NULL;
}

static GDBProcess *gdb_get_cpu_process(CPUState *cpu)
{
    return gdb_get_process(gdb_get_cpu_pid(cpu));
}

static CPUState *find_cpu(uint32_t thread_id)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        if (gdb_get_cpu_index(cpu) == thread_id) {
            return cpu;
        }
    }

    return NULL;
}

CPUState *gdb_get_first_cpu_in_process(GDBProcess *process)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        if (gdb_get_cpu_pid(cpu) == process->pid) {
            return cpu;
        }
    }

    return NULL;
}

static CPUState *gdb_next_cpu_in_process(CPUState *cpu)
{
    uint32_t pid = gdb_get_cpu_pid(cpu);
    cpu = CPU_NEXT(cpu);

    while (cpu) {
        if (gdb_get_cpu_pid(cpu) == pid) {
            break;
        }

        cpu = CPU_NEXT(cpu);
    }

    return cpu;
}

/* Return the cpu following @cpu, while ignoring unattached processes. */
static CPUState *gdb_next_attached_cpu(CPUState *cpu)
{
    cpu = CPU_NEXT(cpu);

    while (cpu) {
        if (gdb_get_cpu_process(cpu)->attached) {
            break;
        }

        cpu = CPU_NEXT(cpu);
    }

    return cpu;
}

/* Return the first attached cpu */
CPUState *gdb_first_attached_cpu(void)
{
    CPUState *cpu = first_cpu;
    GDBProcess *process = gdb_get_cpu_process(cpu);

    if (!process->attached) {
        return gdb_next_attached_cpu(cpu);
    }

    return cpu;
}

static CPUState *gdb_get_cpu(uint32_t pid, uint32_t tid)
{
    GDBProcess *process;
    CPUState *cpu;

    if (!pid && !tid) {
        /* 0 means any process/thread, we take the first attached one */
        return gdb_first_attached_cpu();
    } else if (pid && !tid) {
        /* any thread in a specific process */
        process = gdb_get_process(pid);

        if (process == NULL) {
            return NULL;
        }

        if (!process->attached) {
            return NULL;
        }

        return gdb_get_first_cpu_in_process(process);
    } else {
        /* a specific thread */
        cpu = find_cpu(tid);

        if (cpu == NULL) {
            return NULL;
        }

        process = gdb_get_cpu_process(cpu);

        if (pid && process->pid != pid) {
            return NULL;
        }

        if (!process->attached) {
            return NULL;
        }

        return cpu;
    }
}

static const char *get_feature_xml(const char *p, const char **newp,
                                   GDBProcess *process)
{
    CPUState *cpu = gdb_get_first_cpu_in_process(process);
    CPUClass *cc = CPU_GET_CLASS(cpu);
    GDBRegisterState *r;
    size_t len;

    /*
     * qXfer:features:read:ANNEX:OFFSET,LENGTH'
     *                     ^p    ^newp
     */
    char *term = strchr(p, ':');
    *newp = term + 1;
    len = term - p;

    /* Is it the main target xml? */
    if (strncmp(p, "target.xml", len) == 0) {
        if (!process->target_xml) {
            g_autoptr(GPtrArray) xml = g_ptr_array_new_with_free_func(g_free);

            g_ptr_array_add(
                xml,
                g_strdup("<?xml version=\"1.0\"?>"
                         "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
                         "<target>"));

            if (cc->gdb_arch_name) {
                g_ptr_array_add(
                    xml,
                    g_markup_printf_escaped("<architecture>%s</architecture>",
                                            cc->gdb_arch_name(cpu)));
            }
            for (guint i = 0; i < cpu->gdb_regs->len; i++) {
                r = &g_array_index(cpu->gdb_regs, GDBRegisterState, i);
                g_ptr_array_add(
                    xml,
                    g_markup_printf_escaped("<xi:include href=\"%s\"/>",
                                            r->feature->xmlname));
            }
            g_ptr_array_add(xml, g_strdup("</target>"));
            g_ptr_array_add(xml, NULL);

            process->target_xml = g_strjoinv(NULL, (void *)xml->pdata);
        }
        return process->target_xml;
    }
    /* Is it one of the features? */
    for (guint i = 0; i < cpu->gdb_regs->len; i++) {
        r = &g_array_index(cpu->gdb_regs, GDBRegisterState, i);
        if (strncmp(p, r->feature->xmlname, len) == 0) {
            return r->feature->xml;
        }
    }

    /* failed */
    return NULL;
}

void gdb_feature_builder_init(GDBFeatureBuilder *builder, GDBFeature *feature,
                              const char *name, const char *xmlname,
                              int base_reg)
{
    char *header = g_markup_printf_escaped(
        "<?xml version=\"1.0\"?>"
        "<!DOCTYPE feature SYSTEM \"gdb-target.dtd\">"
        "<feature name=\"%s\">",
        name);

    builder->feature = feature;
    builder->xml = g_ptr_array_new();
    g_ptr_array_add(builder->xml, header);
    builder->regs = g_ptr_array_new();
    builder->base_reg = base_reg;
    feature->xmlname = xmlname;
    feature->name = name;
}

void gdb_feature_builder_append_tag(const GDBFeatureBuilder *builder,
                                    const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    g_ptr_array_add(builder->xml, g_markup_vprintf_escaped(format, ap));
    va_end(ap);
}

void gdb_feature_builder_append_reg(const GDBFeatureBuilder *builder,
                                    const char *name,
                                    int bitsize,
                                    int regnum,
                                    const char *type,
                                    const char *group)
{
    if (builder->regs->len <= regnum) {
        g_ptr_array_set_size(builder->regs, regnum + 1);
    }

    builder->regs->pdata[regnum] = (gpointer *)name;

    if (group) {
        gdb_feature_builder_append_tag(
            builder,
            "<reg name=\"%s\" bitsize=\"%d\" regnum=\"%d\" type=\"%s\" group=\"%s\"/>",
            name, bitsize, builder->base_reg + regnum, type, group);
    } else {
        gdb_feature_builder_append_tag(
            builder,
            "<reg name=\"%s\" bitsize=\"%d\" regnum=\"%d\" type=\"%s\"/>",
            name, bitsize, builder->base_reg + regnum, type);
    }
}

void gdb_feature_builder_end(const GDBFeatureBuilder *builder)
{
    g_ptr_array_add(builder->xml, (void *)"</feature>");
    g_ptr_array_add(builder->xml, NULL);

    builder->feature->xml = g_strjoinv(NULL, (void *)builder->xml->pdata);

    for (guint i = 0; i < builder->xml->len - 2; i++) {
        g_free(g_ptr_array_index(builder->xml, i));
    }

    g_ptr_array_free(builder->xml, TRUE);

    builder->feature->num_regs = builder->regs->len;
    builder->feature->regs = (void *)g_ptr_array_free(builder->regs, FALSE);
}

const GDBFeature *gdb_find_static_feature(const char *xmlname)
{
    const GDBFeature *feature;

    for (feature = gdb_static_features; feature->xmlname; feature++) {
        if (!strcmp(feature->xmlname, xmlname)) {
            return feature;
        }
    }

    g_assert_not_reached();
}

GArray *gdb_get_register_list(CPUState *cpu)
{
    GArray *results = g_array_new(true, true, sizeof(GDBRegDesc));

    /* registers are only available once the CPU is initialised */
    if (!cpu->gdb_regs) {
        return results;
    }

    for (int f = 0; f < cpu->gdb_regs->len; f++) {
        GDBRegisterState *r = &g_array_index(cpu->gdb_regs, GDBRegisterState, f);
        for (int i = 0; i < r->feature->num_regs; i++) {
            const char *name = r->feature->regs[i];
            GDBRegDesc desc = {
                r->base_reg + i,
                name,
                r->feature->name
            };
            g_array_append_val(results, desc);
        }
    }

    return results;
}

int gdb_read_register(CPUState *cpu, GByteArray *buf, int reg)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);
    GDBRegisterState *r;

    if (reg < cc->gdb_num_core_regs) {
        return cc->gdb_read_register(cpu, buf, reg);
    }

    for (guint i = 0; i < cpu->gdb_regs->len; i++) {
        r = &g_array_index(cpu->gdb_regs, GDBRegisterState, i);
        if (r->base_reg <= reg && reg < r->base_reg + r->feature->num_regs) {
            return r->get_reg(cpu, buf, reg - r->base_reg);
        }
    }
    return 0;
}

static int gdb_write_register(CPUState *cpu, uint8_t *mem_buf, int reg)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);
    GDBRegisterState *r;

    if (reg < cc->gdb_num_core_regs) {
        return cc->gdb_write_register(cpu, mem_buf, reg);
    }

    for (guint i = 0; i < cpu->gdb_regs->len; i++) {
        r =  &g_array_index(cpu->gdb_regs, GDBRegisterState, i);
        if (r->base_reg <= reg && reg < r->base_reg + r->feature->num_regs) {
            return r->set_reg(cpu, mem_buf, reg - r->base_reg);
        }
    }
    return 0;
}

static void gdb_register_feature(CPUState *cpu, int base_reg,
                                 gdb_get_reg_cb get_reg, gdb_set_reg_cb set_reg,
                                 const GDBFeature *feature)
{
    GDBRegisterState s = {
        .base_reg = base_reg,
        .get_reg = get_reg,
        .set_reg = set_reg,
        .feature = feature
    };

    g_array_append_val(cpu->gdb_regs, s);
}

void gdb_init_cpu(CPUState *cpu)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);
    const GDBFeature *feature;

    cpu->gdb_regs = g_array_new(false, false, sizeof(GDBRegisterState));

    if (cc->gdb_core_xml_file) {
        feature = gdb_find_static_feature(cc->gdb_core_xml_file);
        gdb_register_feature(cpu, 0,
                             cc->gdb_read_register, cc->gdb_write_register,
                             feature);
        cpu->gdb_num_regs = cpu->gdb_num_g_regs = feature->num_regs;
    }

    if (cc->gdb_num_core_regs) {
        cpu->gdb_num_regs = cpu->gdb_num_g_regs = cc->gdb_num_core_regs;
    }
}

void gdb_register_coprocessor(CPUState *cpu,
                              gdb_get_reg_cb get_reg, gdb_set_reg_cb set_reg,
                              const GDBFeature *feature, int g_pos)
{
    GDBRegisterState *s;
    guint i;
    int base_reg = cpu->gdb_num_regs;

    for (i = 0; i < cpu->gdb_regs->len; i++) {
        /* Check for duplicates.  */
        s = &g_array_index(cpu->gdb_regs, GDBRegisterState, i);
        if (s->feature == feature) {
            return;
        }
    }

    gdb_register_feature(cpu, base_reg, get_reg, set_reg, feature);

    /* Add to end of list.  */
    cpu->gdb_num_regs += feature->num_regs;
    if (g_pos) {
        if (g_pos != base_reg) {
            error_report("Error: Bad gdb register numbering for '%s', "
                         "expected %d got %d", feature->xml, g_pos, base_reg);
        } else {
            cpu->gdb_num_g_regs = cpu->gdb_num_regs;
        }
    }
}

void gdb_unregister_coprocessor_all(CPUState *cpu)
{
    /*
     * Safe to nuke everything. GDBRegisterState::xml is static const char so
     * it won't be freed
     */
    g_array_free(cpu->gdb_regs, true);

    cpu->gdb_regs = NULL;
    cpu->gdb_num_regs = 0;
    cpu->gdb_num_g_regs = 0;
}

static void gdb_process_breakpoint_remove_all(GDBProcess *p)
{
    CPUState *cpu = gdb_get_first_cpu_in_process(p);

    while (cpu) {
        gdb_breakpoint_remove_all(cpu);
        cpu = gdb_next_cpu_in_process(cpu);
    }
}


static void gdb_set_cpu_pc(vaddr pc)
{
    CPUState *cpu = gdbserver_state.c_cpu;

    cpu_synchronize_state(cpu);
    cpu_set_pc(cpu, pc);
}

void gdb_append_thread_id(CPUState *cpu, GString *buf)
{
    if (gdbserver_state.multiprocess) {
        g_string_append_printf(buf, "p%02x.%02x",
                               gdb_get_cpu_pid(cpu), gdb_get_cpu_index(cpu));
    } else {
        g_string_append_printf(buf, "%02x", gdb_get_cpu_index(cpu));
    }
}

static GDBThreadIdKind read_thread_id(const char *buf, const char **end_buf,
                                      uint32_t *pid, uint32_t *tid)
{
    unsigned long p, t;
    int ret;

    if (*buf == 'p') {
        buf++;
        ret = qemu_strtoul(buf, &buf, 16, &p);

        if (ret) {
            return GDB_READ_THREAD_ERR;
        }

        /* Skip '.' */
        buf++;
    } else {
        p = 0;
    }

    ret = qemu_strtoul(buf, &buf, 16, &t);

    if (ret) {
        return GDB_READ_THREAD_ERR;
    }

    *end_buf = buf;

    if (p == -1) {
        return GDB_ALL_PROCESSES;
    }

    if (pid) {
        *pid = p;
    }

    if (t == -1) {
        return GDB_ALL_THREADS;
    }

    if (tid) {
        *tid = t;
    }

    return GDB_ONE_THREAD;
}

/**
 * gdb_handle_vcont - Parses and handles a vCont packet.
 * returns -ENOTSUP if a command is unsupported, -EINVAL or -ERANGE if there is
 *         a format error, 0 on success.
 */
static int gdb_handle_vcont(const char *p)
{
    int res, signal = 0;
    char cur_action;
    unsigned long tmp;
    uint32_t pid, tid;
    GDBProcess *process;
    CPUState *cpu;
    GDBThreadIdKind kind;
    unsigned int max_cpus = gdb_get_max_cpus();
    /* uninitialised CPUs stay 0 */
    g_autofree char *newstates = g_new0(char, max_cpus);

    /* mark valid CPUs with 1 */
    CPU_FOREACH(cpu) {
        newstates[cpu->cpu_index] = 1;
    }

    /*
     * res keeps track of what error we are returning, with -ENOTSUP meaning
     * that the command is unknown or unsupported, thus returning an empty
     * packet, while -EINVAL and -ERANGE cause an E22 packet, due to invalid,
     *  or incorrect parameters passed.
     */
    res = 0;

    /*
     * target_count and last_target keep track of how many CPUs we are going to
     * step or resume, and a pointer to the state structure of one of them,
     * respectively
     */
    int target_count = 0;
    CPUState *last_target = NULL;

    while (*p) {
        if (*p++ != ';') {
            return -ENOTSUP;
        }

        cur_action = *p++;
        if (cur_action == 'C' || cur_action == 'S') {
            cur_action = qemu_tolower(cur_action);
            res = qemu_strtoul(p, &p, 16, &tmp);
            if (res) {
                return res;
            }
            signal = gdb_signal_to_target(tmp);
        } else if (cur_action != 'c' && cur_action != 's') {
            /* unknown/invalid/unsupported command */
            return -ENOTSUP;
        }

        if (*p == '\0' || *p == ';') {
            /*
             * No thread specifier, action is on "all threads". The
             * specification is unclear regarding the process to act on. We
             * choose all processes.
             */
            kind = GDB_ALL_PROCESSES;
        } else if (*p++ == ':') {
            kind = read_thread_id(p, &p, &pid, &tid);
        } else {
            return -ENOTSUP;
        }

        switch (kind) {
        case GDB_READ_THREAD_ERR:
            return -EINVAL;

        case GDB_ALL_PROCESSES:
            cpu = gdb_first_attached_cpu();
            while (cpu) {
                if (newstates[cpu->cpu_index] == 1) {
                    newstates[cpu->cpu_index] = cur_action;

                    target_count++;
                    last_target = cpu;
                }

                cpu = gdb_next_attached_cpu(cpu);
            }
            break;

        case GDB_ALL_THREADS:
            process = gdb_get_process(pid);

            if (!process->attached) {
                return -EINVAL;
            }

            cpu = gdb_get_first_cpu_in_process(process);
            while (cpu) {
                if (newstates[cpu->cpu_index] == 1) {
                    newstates[cpu->cpu_index] = cur_action;

                    target_count++;
                    last_target = cpu;
                }

                cpu = gdb_next_cpu_in_process(cpu);
            }
            break;

        case GDB_ONE_THREAD:
            cpu = gdb_get_cpu(pid, tid);

            /* invalid CPU/thread specified */
            if (!cpu) {
                return -EINVAL;
            }

            /* only use if no previous match occourred */
            if (newstates[cpu->cpu_index] == 1) {
                newstates[cpu->cpu_index] = cur_action;

                target_count++;
                last_target = cpu;
            }
            break;
        }
    }

    /*
     * if we're about to resume a specific set of CPUs/threads, make it so that
     * in case execution gets interrupted, we can send GDB a stop reply with a
     * correct value. it doesn't really matter which CPU we tell GDB the signal
     * happened in (VM pauses stop all of them anyway), so long as it is one of
     * the ones we resumed/single stepped here.
     */
    if (target_count > 0) {
        gdbserver_state.c_cpu = last_target;
    }

    gdbserver_state.signal = signal;
    gdb_continue_partial(newstates);
    return res;
}

static const char *cmd_next_param(const char *param, const char delimiter)
{
    static const char all_delimiters[] = ",;:=";
    char curr_delimiters[2] = {0};
    const char *delimiters;

    if (delimiter == '?') {
        delimiters = all_delimiters;
    } else if (delimiter == '0') {
        return strchr(param, '\0');
    } else if (delimiter == '.' && *param) {
        return param + 1;
    } else {
        curr_delimiters[0] = delimiter;
        delimiters = curr_delimiters;
    }

    param += strcspn(param, delimiters);
    if (*param) {
        param++;
    }
    return param;
}

static int cmd_parse_params(const char *data, const char *schema,
                            GArray *params)
{
    const char *curr_schema, *curr_data;

    g_assert(schema);
    g_assert(params->len == 0);

    curr_schema = schema;
    curr_data = data;
    while (curr_schema[0] && curr_schema[1] && *curr_data) {
        GdbCmdVariant this_param;

        switch (curr_schema[0]) {
        case 'l':
            if (qemu_strtoul(curr_data, &curr_data, 16,
                             &this_param.val_ul)) {
                return -EINVAL;
            }
            curr_data = cmd_next_param(curr_data, curr_schema[1]);
            g_array_append_val(params, this_param);
            break;
        case 'L':
            if (qemu_strtou64(curr_data, &curr_data, 16,
                              (uint64_t *)&this_param.val_ull)) {
                return -EINVAL;
            }
            curr_data = cmd_next_param(curr_data, curr_schema[1]);
            g_array_append_val(params, this_param);
            break;
        case 's':
            this_param.data = curr_data;
            curr_data = cmd_next_param(curr_data, curr_schema[1]);
            g_array_append_val(params, this_param);
            break;
        case 'o':
            this_param.opcode = *(uint8_t *)curr_data;
            curr_data = cmd_next_param(curr_data, curr_schema[1]);
            g_array_append_val(params, this_param);
            break;
        case 't':
            this_param.thread_id.kind =
                read_thread_id(curr_data, &curr_data,
                               &this_param.thread_id.pid,
                               &this_param.thread_id.tid);
            curr_data = cmd_next_param(curr_data, curr_schema[1]);
            g_array_append_val(params, this_param);
            break;
        case '?':
            curr_data = cmd_next_param(curr_data, curr_schema[1]);
            break;
        default:
            return -EINVAL;
        }
        curr_schema += 2;
    }

    return 0;
}

static inline int startswith(const char *string, const char *pattern)
{
  return !strncmp(string, pattern, strlen(pattern));
}

static bool process_string_cmd(const char *data,
                               const GdbCmdParseEntry *cmds, int num_cmds)
{
    int i;
    g_autoptr(GArray) params = g_array_new(false, true, sizeof(GdbCmdVariant));

    if (!cmds) {
        return false;
    }

    for (i = 0; i < num_cmds; i++) {
        const GdbCmdParseEntry *cmd = &cmds[i];
        void *user_ctx = NULL;
        g_assert(cmd->handler && cmd->cmd);

        if ((cmd->cmd_startswith && !startswith(data, cmd->cmd)) ||
            (!cmd->cmd_startswith && strcmp(cmd->cmd, data))) {
            continue;
        }

        if (cmd->schema) {
            if (cmd_parse_params(&data[strlen(cmd->cmd)],
                                 cmd->schema, params)) {
                return false;
            }
        }

        if (cmd->need_cpu_context) {
            user_ctx = (void *)gdbserver_state.g_cpu;
        }

        gdbserver_state.allow_stop_reply = cmd->allow_stop_reply;
        cmd->handler(params, user_ctx);
        return true;
    }

    return false;
}

static void run_cmd_parser(const char *data, const GdbCmdParseEntry *cmd)
{
    if (!data) {
        return;
    }

    g_string_set_size(gdbserver_state.str_buf, 0);
    g_byte_array_set_size(gdbserver_state.mem_buf, 0);

    /* In case there was an error during the command parsing we must
    * send a NULL packet to indicate the command is not supported */
    if (!process_string_cmd(data, cmd, 1)) {
        gdb_put_packet("");
    }
}

static void handle_detach(GArray *params, void *user_ctx)
{
    GDBProcess *process;
    uint32_t pid = 1;

    if (gdbserver_state.multiprocess) {
        if (!params->len) {
            gdb_put_packet("E22");
            return;
        }

        pid = gdb_get_cmd_param(params, 0)->val_ul;
    }

#ifdef CONFIG_USER_ONLY
    if (gdb_handle_detach_user(pid)) {
        return;
    }
#endif

    process = gdb_get_process(pid);
    gdb_process_breakpoint_remove_all(process);
    process->attached = false;

    if (pid == gdb_get_cpu_pid(gdbserver_state.c_cpu)) {
        gdbserver_state.c_cpu = gdb_first_attached_cpu();
    }

    if (pid == gdb_get_cpu_pid(gdbserver_state.g_cpu)) {
        gdbserver_state.g_cpu = gdb_first_attached_cpu();
    }

    if (!gdbserver_state.c_cpu) {
        /* No more process attached */
        gdb_disable_syscalls();
        gdb_continue();
    }
    gdb_put_packet("OK");
}

static void handle_thread_alive(GArray *params, void *user_ctx)
{
    CPUState *cpu;

    if (!params->len) {
        gdb_put_packet("E22");
        return;
    }

    if (gdb_get_cmd_param(params, 0)->thread_id.kind == GDB_READ_THREAD_ERR) {
        gdb_put_packet("E22");
        return;
    }

    cpu = gdb_get_cpu(gdb_get_cmd_param(params, 0)->thread_id.pid,
                      gdb_get_cmd_param(params, 0)->thread_id.tid);
    if (!cpu) {
        gdb_put_packet("E22");
        return;
    }

    gdb_put_packet("OK");
}

static void handle_continue(GArray *params, void *user_ctx)
{
    if (params->len) {
        gdb_set_cpu_pc(gdb_get_cmd_param(params, 0)->val_ull);
    }

    gdbserver_state.signal = 0;
    gdb_continue();
}

static void handle_cont_with_sig(GArray *params, void *user_ctx)
{
    unsigned long signal = 0;

    /*
     * Note: C sig;[addr] is currently unsupported and we simply
     *       omit the addr parameter
     */
    if (params->len) {
        signal = gdb_get_cmd_param(params, 0)->val_ul;
    }

    gdbserver_state.signal = gdb_signal_to_target(signal);
    if (gdbserver_state.signal == -1) {
        gdbserver_state.signal = 0;
    }
    gdb_continue();
}

static void handle_set_thread(GArray *params, void *user_ctx)
{
    uint32_t pid, tid;
    CPUState *cpu;

    if (params->len != 2) {
        gdb_put_packet("E22");
        return;
    }

    if (gdb_get_cmd_param(params, 1)->thread_id.kind == GDB_READ_THREAD_ERR) {
        gdb_put_packet("E22");
        return;
    }

    if (gdb_get_cmd_param(params, 1)->thread_id.kind != GDB_ONE_THREAD) {
        gdb_put_packet("OK");
        return;
    }

    pid = gdb_get_cmd_param(params, 1)->thread_id.pid;
    tid = gdb_get_cmd_param(params, 1)->thread_id.tid;
#ifdef CONFIG_USER_ONLY
    if (gdb_handle_set_thread_user(pid, tid)) {
        return;
    }
#endif
    cpu = gdb_get_cpu(pid, tid);
    if (!cpu) {
        gdb_put_packet("E22");
        return;
    }

    /*
     * Note: This command is deprecated and modern gdb's will be using the
     *       vCont command instead.
     */
    switch (gdb_get_cmd_param(params, 0)->opcode) {
    case 'c':
        gdbserver_state.c_cpu = cpu;
        gdb_put_packet("OK");
        break;
    case 'g':
        gdbserver_state.g_cpu = cpu;
        gdb_put_packet("OK");
        break;
    default:
        gdb_put_packet("E22");
        break;
    }
}

static void handle_insert_bp(GArray *params, void *user_ctx)
{
    int res;

    if (params->len != 3) {
        gdb_put_packet("E22");
        return;
    }

    res = gdb_breakpoint_insert(gdbserver_state.c_cpu,
                                gdb_get_cmd_param(params, 0)->val_ul,
                                gdb_get_cmd_param(params, 1)->val_ull,
                                gdb_get_cmd_param(params, 2)->val_ull);
    if (res >= 0) {
        gdb_put_packet("OK");
        return;
    } else if (res == -ENOSYS) {
        gdb_put_packet("");
        return;
    }

    gdb_put_packet("E22");
}

static void handle_remove_bp(GArray *params, void *user_ctx)
{
    int res;

    if (params->len != 3) {
        gdb_put_packet("E22");
        return;
    }

    res = gdb_breakpoint_remove(gdbserver_state.c_cpu,
                                gdb_get_cmd_param(params, 0)->val_ul,
                                gdb_get_cmd_param(params, 1)->val_ull,
                                gdb_get_cmd_param(params, 2)->val_ull);
    if (res >= 0) {
        gdb_put_packet("OK");
        return;
    } else if (res == -ENOSYS) {
        gdb_put_packet("");
        return;
    }

    gdb_put_packet("E22");
}

/*
 * handle_set/get_reg
 *
 * Older gdb are really dumb, and don't use 'G/g' if 'P/p' is available.
 * This works, but can be very slow. Anything new enough to understand
 * XML also knows how to use this properly. However to use this we
 * need to define a local XML file as well as be talking to a
 * reasonably modern gdb. Responding with an empty packet will cause
 * the remote gdb to fallback to older methods.
 */

static void handle_set_reg(GArray *params, void *user_ctx)
{
    int reg_size;

    if (params->len != 2) {
        gdb_put_packet("E22");
        return;
    }

    reg_size = strlen(gdb_get_cmd_param(params, 1)->data) / 2;
    gdb_hextomem(gdbserver_state.mem_buf, gdb_get_cmd_param(params, 1)->data, reg_size);
    gdb_write_register(gdbserver_state.g_cpu, gdbserver_state.mem_buf->data,
                       gdb_get_cmd_param(params, 0)->val_ull);
    gdb_put_packet("OK");
}

static void handle_get_reg(GArray *params, void *user_ctx)
{
    int reg_size;

    if (!params->len) {
        gdb_put_packet("E14");
        return;
    }

    reg_size = gdb_read_register(gdbserver_state.g_cpu,
                                 gdbserver_state.mem_buf,
                                 gdb_get_cmd_param(params, 0)->val_ull);
    if (!reg_size) {
        gdb_put_packet("E14");
        return;
    } else {
        g_byte_array_set_size(gdbserver_state.mem_buf, reg_size);
    }

    gdb_memtohex(gdbserver_state.str_buf,
                 gdbserver_state.mem_buf->data, reg_size);
    gdb_put_strbuf();
}

static void handle_write_mem(GArray *params, void *user_ctx)
{
    if (params->len != 3) {
        gdb_put_packet("E22");
        return;
    }

    /* gdb_hextomem() reads 2*len bytes */
    if (gdb_get_cmd_param(params, 1)->val_ull >
        strlen(gdb_get_cmd_param(params, 2)->data) / 2) {
        gdb_put_packet("E22");
        return;
    }

    gdb_hextomem(gdbserver_state.mem_buf, gdb_get_cmd_param(params, 2)->data,
                 gdb_get_cmd_param(params, 1)->val_ull);
    if (gdb_target_memory_rw_debug(gdbserver_state.g_cpu,
                                   gdb_get_cmd_param(params, 0)->val_ull,
                                   gdbserver_state.mem_buf->data,
                                   gdbserver_state.mem_buf->len, true)) {
        gdb_put_packet("E14");
        return;
    }

    gdb_put_packet("OK");
}

static void handle_read_mem(GArray *params, void *user_ctx)
{
    if (params->len != 2) {
        gdb_put_packet("E22");
        return;
    }

    /* gdb_memtohex() doubles the required space */
    if (gdb_get_cmd_param(params, 1)->val_ull > MAX_PACKET_LENGTH / 2) {
        gdb_put_packet("E22");
        return;
    }

    g_byte_array_set_size(gdbserver_state.mem_buf,
                          gdb_get_cmd_param(params, 1)->val_ull);

    if (gdb_target_memory_rw_debug(gdbserver_state.g_cpu,
                                   gdb_get_cmd_param(params, 0)->val_ull,
                                   gdbserver_state.mem_buf->data,
                                   gdbserver_state.mem_buf->len, false)) {
        gdb_put_packet("E14");
        return;
    }

    gdb_memtohex(gdbserver_state.str_buf, gdbserver_state.mem_buf->data,
             gdbserver_state.mem_buf->len);
    gdb_put_strbuf();
}

static void handle_write_all_regs(GArray *params, void *user_ctx)
{
    int reg_id;
    size_t len;
    uint8_t *registers;
    int reg_size;

    if (!params->len) {
        return;
    }

    cpu_synchronize_state(gdbserver_state.g_cpu);
    len = strlen(gdb_get_cmd_param(params, 0)->data) / 2;
    gdb_hextomem(gdbserver_state.mem_buf, gdb_get_cmd_param(params, 0)->data, len);
    registers = gdbserver_state.mem_buf->data;
    for (reg_id = 0;
         reg_id < gdbserver_state.g_cpu->gdb_num_g_regs && len > 0;
         reg_id++) {
        reg_size = gdb_write_register(gdbserver_state.g_cpu, registers, reg_id);
        len -= reg_size;
        registers += reg_size;
    }
    gdb_put_packet("OK");
}

static void handle_read_all_regs(GArray *params, void *user_ctx)
{
    int reg_id;
    size_t len;

    cpu_synchronize_state(gdbserver_state.g_cpu);
    g_byte_array_set_size(gdbserver_state.mem_buf, 0);
    len = 0;
    for (reg_id = 0; reg_id < gdbserver_state.g_cpu->gdb_num_g_regs; reg_id++) {
        len += gdb_read_register(gdbserver_state.g_cpu,
                                 gdbserver_state.mem_buf,
                                 reg_id);
    }
    g_assert(len == gdbserver_state.mem_buf->len);

    gdb_memtohex(gdbserver_state.str_buf, gdbserver_state.mem_buf->data, len);
    gdb_put_strbuf();
}


static void handle_step(GArray *params, void *user_ctx)
{
    if (params->len) {
        gdb_set_cpu_pc(gdb_get_cmd_param(params, 0)->val_ull);
    }

    cpu_single_step(gdbserver_state.c_cpu, gdbserver_state.sstep_flags);
    gdb_continue();
}

static void handle_backward(GArray *params, void *user_ctx)
{
    if (!gdb_can_reverse()) {
        gdb_put_packet("E22");
    }
    if (params->len == 1) {
        switch (gdb_get_cmd_param(params, 0)->opcode) {
        case 's':
            if (replay_reverse_step()) {
                gdb_continue();
            } else {
                gdb_put_packet("E14");
            }
            return;
        case 'c':
            if (replay_reverse_continue()) {
                gdb_continue();
            } else {
                gdb_put_packet("E14");
            }
            return;
        }
    }

    /* Default invalid command */
    gdb_put_packet("");
}

static void handle_v_cont_query(GArray *params, void *user_ctx)
{
    gdb_put_packet("vCont;c;C;s;S");
}

static void handle_v_cont(GArray *params, void *user_ctx)
{
    int res;

    if (!params->len) {
        return;
    }

    res = gdb_handle_vcont(gdb_get_cmd_param(params, 0)->data);
    if ((res == -EINVAL) || (res == -ERANGE)) {
        gdb_put_packet("E22");
    } else if (res) {
        gdb_put_packet("");
    }
}

static void handle_v_attach(GArray *params, void *user_ctx)
{
    GDBProcess *process;
    CPUState *cpu;

    g_string_assign(gdbserver_state.str_buf, "E22");
    if (!params->len) {
        goto cleanup;
    }

    process = gdb_get_process(gdb_get_cmd_param(params, 0)->val_ul);
    if (!process) {
        goto cleanup;
    }

    cpu = gdb_get_first_cpu_in_process(process);
    if (!cpu) {
        goto cleanup;
    }

    process->attached = true;
    gdbserver_state.g_cpu = cpu;
    gdbserver_state.c_cpu = cpu;

    if (gdbserver_state.allow_stop_reply) {
        g_string_printf(gdbserver_state.str_buf, "T%02xthread:", GDB_SIGNAL_TRAP);
        gdb_append_thread_id(cpu, gdbserver_state.str_buf);
        g_string_append_c(gdbserver_state.str_buf, ';');
        gdbserver_state.allow_stop_reply = false;
cleanup:
        gdb_put_strbuf();
    }
}

static void handle_v_kill(GArray *params, void *user_ctx)
{
    /* Kill the target */
    gdb_put_packet("OK");
    error_report("QEMU: Terminated via GDBstub");
    gdb_exit(0);
    gdb_qemu_exit(0);
}

static const GdbCmdParseEntry gdb_v_commands_table[] = {
    /* Order is important if has same prefix */
    {
        .handler = handle_v_cont_query,
        .cmd = "Cont?",
        .cmd_startswith = true
    },
    {
        .handler = handle_v_cont,
        .cmd = "Cont",
        .cmd_startswith = true,
        .allow_stop_reply = true,
        .schema = "s0"
    },
    {
        .handler = handle_v_attach,
        .cmd = "Attach;",
        .cmd_startswith = true,
        .allow_stop_reply = true,
        .schema = "l0"
    },
    {
        .handler = handle_v_kill,
        .cmd = "Kill;",
        .cmd_startswith = true
    },
#ifdef CONFIG_USER_ONLY
    /*
     * Host I/O Packets. See [1] for details.
     * [1] https://sourceware.org/gdb/onlinedocs/gdb/Host-I_002fO-Packets.html
     */
    {
        .handler = gdb_handle_v_file_open,
        .cmd = "File:open:",
        .cmd_startswith = true,
        .schema = "s,L,L0"
    },
    {
        .handler = gdb_handle_v_file_close,
        .cmd = "File:close:",
        .cmd_startswith = true,
        .schema = "l0"
    },
    {
        .handler = gdb_handle_v_file_pread,
        .cmd = "File:pread:",
        .cmd_startswith = true,
        .schema = "l,L,L0"
    },
    {
        .handler = gdb_handle_v_file_readlink,
        .cmd = "File:readlink:",
        .cmd_startswith = true,
        .schema = "s0"
    },
#endif
};

static void handle_v_commands(GArray *params, void *user_ctx)
{
    if (!params->len) {
        return;
    }

    if (!process_string_cmd(gdb_get_cmd_param(params, 0)->data,
                            gdb_v_commands_table,
                            ARRAY_SIZE(gdb_v_commands_table))) {
        gdb_put_packet("");
    }
}

static void handle_query_qemu_sstepbits(GArray *params, void *user_ctx)
{
    g_string_printf(gdbserver_state.str_buf, "ENABLE=%x", SSTEP_ENABLE);

    if (gdbserver_state.supported_sstep_flags & SSTEP_NOIRQ) {
        g_string_append_printf(gdbserver_state.str_buf, ",NOIRQ=%x",
                               SSTEP_NOIRQ);
    }

    if (gdbserver_state.supported_sstep_flags & SSTEP_NOTIMER) {
        g_string_append_printf(gdbserver_state.str_buf, ",NOTIMER=%x",
                               SSTEP_NOTIMER);
    }

    gdb_put_strbuf();
}

static void handle_set_qemu_sstep(GArray *params, void *user_ctx)
{
    int new_sstep_flags;

    if (!params->len) {
        return;
    }

    new_sstep_flags = gdb_get_cmd_param(params, 0)->val_ul;

    if (new_sstep_flags  & ~gdbserver_state.supported_sstep_flags) {
        gdb_put_packet("E22");
        return;
    }

    gdbserver_state.sstep_flags = new_sstep_flags;
    gdb_put_packet("OK");
}

static void handle_query_qemu_sstep(GArray *params, void *user_ctx)
{
    g_string_printf(gdbserver_state.str_buf, "0x%x",
                    gdbserver_state.sstep_flags);
    gdb_put_strbuf();
}

static void handle_query_curr_tid(GArray *params, void *user_ctx)
{
    CPUState *cpu;
    GDBProcess *process;

    /*
     * "Current thread" remains vague in the spec, so always return
     * the first thread of the current process (gdb returns the
     * first thread).
     */
    process = gdb_get_cpu_process(gdbserver_state.g_cpu);
    cpu = gdb_get_first_cpu_in_process(process);
    g_string_assign(gdbserver_state.str_buf, "QC");
    gdb_append_thread_id(cpu, gdbserver_state.str_buf);
    gdb_put_strbuf();
}

static void handle_query_threads(GArray *params, void *user_ctx)
{
    if (!gdbserver_state.query_cpu) {
        gdb_put_packet("l");
        return;
    }

    g_string_assign(gdbserver_state.str_buf, "m");
    gdb_append_thread_id(gdbserver_state.query_cpu, gdbserver_state.str_buf);
    gdb_put_strbuf();
    gdbserver_state.query_cpu = gdb_next_attached_cpu(gdbserver_state.query_cpu);
}

static void handle_query_first_threads(GArray *params, void *user_ctx)
{
    gdbserver_state.query_cpu = gdb_first_attached_cpu();
    handle_query_threads(params, user_ctx);
}

static void handle_query_thread_extra(GArray *params, void *user_ctx)
{
    g_autoptr(GString) rs = g_string_new(NULL);
    CPUState *cpu;

    if (!params->len ||
        gdb_get_cmd_param(params, 0)->thread_id.kind == GDB_READ_THREAD_ERR) {
        gdb_put_packet("E22");
        return;
    }

    cpu = gdb_get_cpu(gdb_get_cmd_param(params, 0)->thread_id.pid,
                      gdb_get_cmd_param(params, 0)->thread_id.tid);
    if (!cpu) {
        return;
    }

    cpu_synchronize_state(cpu);

    if (gdbserver_state.multiprocess && (gdbserver_state.process_num > 1)) {
        /* Print the CPU model and name in multiprocess mode */
        ObjectClass *oc = object_get_class(OBJECT(cpu));
        const char *cpu_model = object_class_get_name(oc);
        const char *cpu_name =
            object_get_canonical_path_component(OBJECT(cpu));
        g_string_printf(rs, "%s %s [%s]", cpu_model, cpu_name,
                        cpu->halted ? "halted " : "running");
    } else {
        g_string_printf(rs, "CPU#%d [%s]", cpu->cpu_index,
                        cpu->halted ? "halted " : "running");
    }
    trace_gdbstub_op_extra_info(rs->str);
    gdb_memtohex(gdbserver_state.str_buf, (uint8_t *)rs->str, rs->len);
    gdb_put_strbuf();
}


static char **extra_query_flags;

void gdb_extend_qsupported_features(char *qflags)
{
    if (!extra_query_flags) {
        extra_query_flags = g_new0(char *, 2);
        extra_query_flags[0] = g_strdup(qflags);
    } else if (!g_strv_contains((const gchar * const *) extra_query_flags,
                                qflags)) {
        int len = g_strv_length(extra_query_flags);
        extra_query_flags = g_realloc_n(extra_query_flags, len + 2,
                                        sizeof(char *));
        extra_query_flags[len] = g_strdup(qflags);
    }
}

static void handle_query_supported(GArray *params, void *user_ctx)
{
    CPUClass *cc;

    g_string_printf(gdbserver_state.str_buf, "PacketSize=%x", MAX_PACKET_LENGTH);
    cc = CPU_GET_CLASS(first_cpu);
    if (cc->gdb_core_xml_file) {
        g_string_append(gdbserver_state.str_buf, ";qXfer:features:read+");
    }

    if (gdb_can_reverse()) {
        g_string_append(gdbserver_state.str_buf,
            ";ReverseStep+;ReverseContinue+");
    }

#if defined(CONFIG_USER_ONLY)
#if defined(CONFIG_LINUX)
    if (get_task_state(gdbserver_state.c_cpu)) {
        g_string_append(gdbserver_state.str_buf, ";qXfer:auxv:read+");
    }
    g_string_append(gdbserver_state.str_buf, ";QCatchSyscalls+");

    g_string_append(gdbserver_state.str_buf, ";qXfer:siginfo:read+");
#endif
    g_string_append(gdbserver_state.str_buf, ";qXfer:exec-file:read+");
#endif

    if (params->len) {
        const char *gdb_supported = gdb_get_cmd_param(params, 0)->data;

        if (strstr(gdb_supported, "multiprocess+")) {
            gdbserver_state.multiprocess = true;
        }
#if defined(CONFIG_USER_ONLY)
        gdb_handle_query_supported_user(gdb_supported);
#endif
    }

    g_string_append(gdbserver_state.str_buf, ";vContSupported+;multiprocess+");

    if (extra_query_flags) {
        int extras = g_strv_length(extra_query_flags);
        for (int i = 0; i < extras; i++) {
            g_string_append(gdbserver_state.str_buf, extra_query_flags[i]);
        }
    }

    gdb_put_strbuf();
}

static void handle_query_xfer_features(GArray *params, void *user_ctx)
{
    GDBProcess *process;
    CPUClass *cc;
    unsigned long len, total_len, addr;
    const char *xml;
    const char *p;

    if (params->len < 3) {
        gdb_put_packet("E22");
        return;
    }

    process = gdb_get_cpu_process(gdbserver_state.g_cpu);
    cc = CPU_GET_CLASS(gdbserver_state.g_cpu);
    if (!cc->gdb_core_xml_file) {
        gdb_put_packet("");
        return;
    }

    p = gdb_get_cmd_param(params, 0)->data;
    xml = get_feature_xml(p, &p, process);
    if (!xml) {
        gdb_put_packet("E00");
        return;
    }

    addr = gdb_get_cmd_param(params, 1)->val_ul;
    len = gdb_get_cmd_param(params, 2)->val_ul;
    total_len = strlen(xml);
    if (addr > total_len) {
        gdb_put_packet("E00");
        return;
    }

    if (len > (MAX_PACKET_LENGTH - 5) / 2) {
        len = (MAX_PACKET_LENGTH - 5) / 2;
    }

    if (len < total_len - addr) {
        g_string_assign(gdbserver_state.str_buf, "m");
        gdb_memtox(gdbserver_state.str_buf, xml + addr, len);
    } else {
        g_string_assign(gdbserver_state.str_buf, "l");
        gdb_memtox(gdbserver_state.str_buf, xml + addr, total_len - addr);
    }

    gdb_put_packet_binary(gdbserver_state.str_buf->str,
                      gdbserver_state.str_buf->len, true);
}

static void handle_query_qemu_supported(GArray *params, void *user_ctx)
{
    g_string_printf(gdbserver_state.str_buf, "sstepbits;sstep");
#ifndef CONFIG_USER_ONLY
    g_string_append(gdbserver_state.str_buf, ";PhyMemMode");
#endif
    gdb_put_strbuf();
}

static const GdbCmdParseEntry gdb_gen_query_set_common_table[] = {
    /* Order is important if has same prefix */
    {
        .handler = handle_query_qemu_sstepbits,
        .cmd = "qemu.sstepbits",
    },
    {
        .handler = handle_query_qemu_sstep,
        .cmd = "qemu.sstep",
    },
    {
        .handler = handle_set_qemu_sstep,
        .cmd = "qemu.sstep=",
        .cmd_startswith = true,
        .schema = "l0"
    },
};

/**
 * extend_table() - extend one of the command tables
 * @table: the command table to extend (or NULL)
 * @extensions: a list of GdbCmdParseEntry pointers
 *
 * The entries themselves should be pointers to static const
 * GdbCmdParseEntry entries. If the entry is already in the table we
 * skip adding it again.
 *
 * Returns (a potentially freshly allocated) GPtrArray of GdbCmdParseEntry
 */
static GPtrArray *extend_table(GPtrArray *table, GPtrArray *extensions)
{
    if (!table) {
        table = g_ptr_array_new();
    }

    for (int i = 0; i < extensions->len; i++) {
        gpointer entry = g_ptr_array_index(extensions, i);
        if (!g_ptr_array_find(table, entry, NULL)) {
            g_ptr_array_add(table, entry);
        }
    }

    return table;
}

/**
 * process_extended_table() - run through an extended command table
 * @table: the command table to check
 * @data: parameters
 *
 * returns true if the command was found and executed
 */
static bool process_extended_table(GPtrArray *table, const char *data)
{
    for (int i = 0; i < table->len; i++) {
        const GdbCmdParseEntry *entry = g_ptr_array_index(table, i);
        if (process_string_cmd(data, entry, 1)) {
            return true;
        }
    }
    return false;
}


/* Ptr to GdbCmdParseEntry */
static GPtrArray *extended_query_table;

void gdb_extend_query_table(GPtrArray *new_queries)
{
    extended_query_table = extend_table(extended_query_table, new_queries);
}

static const GdbCmdParseEntry gdb_gen_query_table[] = {
    {
        .handler = handle_query_curr_tid,
        .cmd = "C",
    },
    {
        .handler = handle_query_threads,
        .cmd = "sThreadInfo",
    },
    {
        .handler = handle_query_first_threads,
        .cmd = "fThreadInfo",
    },
    {
        .handler = handle_query_thread_extra,
        .cmd = "ThreadExtraInfo,",
        .cmd_startswith = true,
        .schema = "t0"
    },
#ifdef CONFIG_USER_ONLY
    {
        .handler = gdb_handle_query_offsets,
        .cmd = "Offsets",
    },
#else
    {
        .handler = gdb_handle_query_rcmd,
        .cmd = "Rcmd,",
        .cmd_startswith = true,
        .schema = "s0"
    },
#endif
    {
        .handler = handle_query_supported,
        .cmd = "Supported:",
        .cmd_startswith = true,
        .schema = "s0"
    },
    {
        .handler = handle_query_supported,
        .cmd = "Supported",
        .schema = "s0"
    },
    {
        .handler = handle_query_xfer_features,
        .cmd = "Xfer:features:read:",
        .cmd_startswith = true,
        .schema = "s:l,l0"
    },
#if defined(CONFIG_USER_ONLY)
#if defined(CONFIG_LINUX)
    {
        .handler = gdb_handle_query_xfer_auxv,
        .cmd = "Xfer:auxv:read::",
        .cmd_startswith = true,
        .schema = "l,l0"
    },
    {
        .handler = gdb_handle_query_xfer_siginfo,
        .cmd = "Xfer:siginfo:read::",
        .cmd_startswith = true,
        .schema = "l,l0"
     },
#endif
    {
        .handler = gdb_handle_query_xfer_exec_file,
        .cmd = "Xfer:exec-file:read:",
        .cmd_startswith = true,
        .schema = "l:l,l0"
    },
#endif
    {
        .handler = gdb_handle_query_attached,
        .cmd = "Attached:",
        .cmd_startswith = true
    },
    {
        .handler = gdb_handle_query_attached,
        .cmd = "Attached",
    },
    {
        .handler = handle_query_qemu_supported,
        .cmd = "qemu.Supported",
    },
#ifndef CONFIG_USER_ONLY
    {
        .handler = gdb_handle_query_qemu_phy_mem_mode,
        .cmd = "qemu.PhyMemMode",
    },
#endif
};

/* Ptr to GdbCmdParseEntry */
static GPtrArray *extended_set_table;

void gdb_extend_set_table(GPtrArray *new_set)
{
    extended_set_table = extend_table(extended_set_table, new_set);
}

static const GdbCmdParseEntry gdb_gen_set_table[] = {
    /* Order is important if has same prefix */
    {
        .handler = handle_set_qemu_sstep,
        .cmd = "qemu.sstep:",
        .cmd_startswith = true,
        .schema = "l0"
    },
#ifndef CONFIG_USER_ONLY
    {
        .handler = gdb_handle_set_qemu_phy_mem_mode,
        .cmd = "qemu.PhyMemMode:",
        .cmd_startswith = true,
        .schema = "l0"
    },
#endif
#if defined(CONFIG_USER_ONLY)
    {
        .handler = gdb_handle_set_catch_syscalls,
        .cmd = "CatchSyscalls:",
        .cmd_startswith = true,
        .schema = "s0",
    },
#endif
};

static void handle_gen_query(GArray *params, void *user_ctx)
{
    const char *data;

    if (!params->len) {
        return;
    }

    data = gdb_get_cmd_param(params, 0)->data;

    if (process_string_cmd(data,
                           gdb_gen_query_set_common_table,
                           ARRAY_SIZE(gdb_gen_query_set_common_table))) {
        return;
    }

    if (process_string_cmd(data,
                           gdb_gen_query_table,
                           ARRAY_SIZE(gdb_gen_query_table))) {
        return;
    }

    if (extended_query_table &&
        process_extended_table(extended_query_table, data)) {
        return;
    }

    /* Can't handle query, return Empty response. */
    gdb_put_packet("");
}

static void handle_gen_set(GArray *params, void *user_ctx)
{
    const char *data;

    if (!params->len) {
        return;
    }

    data = gdb_get_cmd_param(params, 0)->data;

    if (process_string_cmd(data,
                           gdb_gen_query_set_common_table,
                           ARRAY_SIZE(gdb_gen_query_set_common_table))) {
        return;
    }

    if (process_string_cmd(data,
                           gdb_gen_set_table,
                           ARRAY_SIZE(gdb_gen_set_table))) {
        return;
    }

    if (extended_set_table &&
        process_extended_table(extended_set_table, data)) {
        return;
    }

    /* Can't handle set, return Empty response. */
    gdb_put_packet("");
}

static void handle_target_halt(GArray *params, void *user_ctx)
{
    if (gdbserver_state.allow_stop_reply) {
        g_string_printf(gdbserver_state.str_buf, "T%02xthread:", GDB_SIGNAL_TRAP);
        gdb_append_thread_id(gdbserver_state.c_cpu, gdbserver_state.str_buf);
        g_string_append_c(gdbserver_state.str_buf, ';');
        gdb_put_strbuf();
        gdbserver_state.allow_stop_reply = false;
    }
    /*
     * Remove all the breakpoints when this query is issued,
     * because gdb is doing an initial connect and the state
     * should be cleaned up.
     */
    gdb_breakpoint_remove_all(gdbserver_state.c_cpu);
}

static int gdb_handle_packet(const char *line_buf)
{
    const GdbCmdParseEntry *cmd_parser = NULL;

    trace_gdbstub_io_command(line_buf);

    switch (line_buf[0]) {
    case '!':
        gdb_put_packet("OK");
        break;
    case '?':
        {
            static const GdbCmdParseEntry target_halted_cmd_desc = {
                .handler = handle_target_halt,
                .cmd = "?",
                .cmd_startswith = true,
                .allow_stop_reply = true,
            };
            cmd_parser = &target_halted_cmd_desc;
        }
        break;
    case 'c':
        {
            static const GdbCmdParseEntry continue_cmd_desc = {
                .handler = handle_continue,
                .cmd = "c",
                .cmd_startswith = true,
                .allow_stop_reply = true,
                .schema = "L0"
            };
            cmd_parser = &continue_cmd_desc;
        }
        break;
    case 'C':
        {
            static const GdbCmdParseEntry cont_with_sig_cmd_desc = {
                .handler = handle_cont_with_sig,
                .cmd = "C",
                .cmd_startswith = true,
                .allow_stop_reply = true,
                .schema = "l0"
            };
            cmd_parser = &cont_with_sig_cmd_desc;
        }
        break;
    case 'v':
        {
            static const GdbCmdParseEntry v_cmd_desc = {
                .handler = handle_v_commands,
                .cmd = "v",
                .cmd_startswith = true,
                .schema = "s0"
            };
            cmd_parser = &v_cmd_desc;
        }
        break;
    case 'k':
        /* Kill the target */
        error_report("QEMU: Terminated via GDBstub");
        gdb_exit(0);
        gdb_qemu_exit(0);
        break;
    case 'D':
        {
            static const GdbCmdParseEntry detach_cmd_desc = {
                .handler = handle_detach,
                .cmd = "D",
                .cmd_startswith = true,
                .schema = "?.l0"
            };
            cmd_parser = &detach_cmd_desc;
        }
        break;
    case 's':
        {
            static const GdbCmdParseEntry step_cmd_desc = {
                .handler = handle_step,
                .cmd = "s",
                .cmd_startswith = true,
                .allow_stop_reply = true,
                .schema = "L0"
            };
            cmd_parser = &step_cmd_desc;
        }
        break;
    case 'b':
        {
            static const GdbCmdParseEntry backward_cmd_desc = {
                .handler = handle_backward,
                .cmd = "b",
                .cmd_startswith = true,
                .allow_stop_reply = true,
                .schema = "o0"
            };
            cmd_parser = &backward_cmd_desc;
        }
        break;
    case 'F':
        {
            static const GdbCmdParseEntry file_io_cmd_desc = {
                .handler = gdb_handle_file_io,
                .cmd = "F",
                .cmd_startswith = true,
                .schema = "L,L,o0"
            };
            cmd_parser = &file_io_cmd_desc;
        }
        break;
    case 'g':
        {
            static const GdbCmdParseEntry read_all_regs_cmd_desc = {
                .handler = handle_read_all_regs,
                .cmd = "g",
                .cmd_startswith = true
            };
            cmd_parser = &read_all_regs_cmd_desc;
        }
        break;
    case 'G':
        {
            static const GdbCmdParseEntry write_all_regs_cmd_desc = {
                .handler = handle_write_all_regs,
                .cmd = "G",
                .cmd_startswith = true,
                .schema = "s0"
            };
            cmd_parser = &write_all_regs_cmd_desc;
        }
        break;
    case 'm':
        {
            static const GdbCmdParseEntry read_mem_cmd_desc = {
                .handler = handle_read_mem,
                .cmd = "m",
                .cmd_startswith = true,
                .schema = "L,L0"
            };
            cmd_parser = &read_mem_cmd_desc;
        }
        break;
    case 'M':
        {
            static const GdbCmdParseEntry write_mem_cmd_desc = {
                .handler = handle_write_mem,
                .cmd = "M",
                .cmd_startswith = true,
                .schema = "L,L:s0"
            };
            cmd_parser = &write_mem_cmd_desc;
        }
        break;
    case 'p':
        {
            static const GdbCmdParseEntry get_reg_cmd_desc = {
                .handler = handle_get_reg,
                .cmd = "p",
                .cmd_startswith = true,
                .schema = "L0"
            };
            cmd_parser = &get_reg_cmd_desc;
        }
        break;
    case 'P':
        {
            static const GdbCmdParseEntry set_reg_cmd_desc = {
                .handler = handle_set_reg,
                .cmd = "P",
                .cmd_startswith = true,
                .schema = "L?s0"
            };
            cmd_parser = &set_reg_cmd_desc;
        }
        break;
    case 'Z':
        {
            static const GdbCmdParseEntry insert_bp_cmd_desc = {
                .handler = handle_insert_bp,
                .cmd = "Z",
                .cmd_startswith = true,
                .schema = "l?L?L0"
            };
            cmd_parser = &insert_bp_cmd_desc;
        }
        break;
    case 'z':
        {
            static const GdbCmdParseEntry remove_bp_cmd_desc = {
                .handler = handle_remove_bp,
                .cmd = "z",
                .cmd_startswith = true,
                .schema = "l?L?L0"
            };
            cmd_parser = &remove_bp_cmd_desc;
        }
        break;
    case 'H':
        {
            static const GdbCmdParseEntry set_thread_cmd_desc = {
                .handler = handle_set_thread,
                .cmd = "H",
                .cmd_startswith = true,
                .schema = "o.t0"
            };
            cmd_parser = &set_thread_cmd_desc;
        }
        break;
    case 'T':
        {
            static const GdbCmdParseEntry thread_alive_cmd_desc = {
                .handler = handle_thread_alive,
                .cmd = "T",
                .cmd_startswith = true,
                .schema = "t0"
            };
            cmd_parser = &thread_alive_cmd_desc;
        }
        break;
    case 'q':
        {
            static const GdbCmdParseEntry gen_query_cmd_desc = {
                .handler = handle_gen_query,
                .cmd = "q",
                .cmd_startswith = true,
                .schema = "s0"
            };
            cmd_parser = &gen_query_cmd_desc;
        }
        break;
    case 'Q':
        {
            static const GdbCmdParseEntry gen_set_cmd_desc = {
                .handler = handle_gen_set,
                .cmd = "Q",
                .cmd_startswith = true,
                .schema = "s0"
            };
            cmd_parser = &gen_set_cmd_desc;
        }
        break;
    default:
        /* put empty packet */
        gdb_put_packet("");
        break;
    }

    if (cmd_parser) {
        run_cmd_parser(line_buf, cmd_parser);
    }

    return RS_IDLE;
}

void gdb_set_stop_cpu(CPUState *cpu)
{
    GDBProcess *p = gdb_get_cpu_process(cpu);

    if (!p->attached) {
        /*
         * Having a stop CPU corresponding to a process that is not attached
         * confuses GDB. So we ignore the request.
         */
        return;
    }

    gdbserver_state.c_cpu = cpu;
    gdbserver_state.g_cpu = cpu;
}

void gdb_read_byte(uint8_t ch)
{
    uint8_t reply;

    gdbserver_state.allow_stop_reply = false;
#ifndef CONFIG_USER_ONLY
    if (gdbserver_state.last_packet->len) {
        /* Waiting for a response to the last packet.  If we see the start
           of a new command then abandon the previous response.  */
        if (ch == '-') {
            trace_gdbstub_err_got_nack();
            gdb_put_buffer(gdbserver_state.last_packet->data,
                       gdbserver_state.last_packet->len);
        } else if (ch == '+') {
            trace_gdbstub_io_got_ack();
        } else {
            trace_gdbstub_io_got_unexpected(ch);
        }

        if (ch == '+' || ch == '$') {
            g_byte_array_set_size(gdbserver_state.last_packet, 0);
        }
        if (ch != '$')
            return;
    }
    if (runstate_is_running()) {
        /*
         * When the CPU is running, we cannot do anything except stop
         * it when receiving a char. This is expected on a Ctrl-C in the
         * gdb client. Because we are in all-stop mode, gdb sends a
         * 0x03 byte which is not a usual packet, so we handle it specially
         * here, but it does expect a stop reply.
         */
        if (ch != 0x03) {
            trace_gdbstub_err_unexpected_runpkt(ch);
        } else {
            gdbserver_state.allow_stop_reply = true;
        }
        vm_stop(RUN_STATE_PAUSED);
    } else
#endif
    {
        switch(gdbserver_state.state) {
        case RS_IDLE:
            if (ch == '$') {
                /* start of command packet */
                gdbserver_state.line_buf_index = 0;
                gdbserver_state.line_sum = 0;
                gdbserver_state.state = RS_GETLINE;
            } else if (ch == '+') {
                /*
                 * do nothing, gdb may preemptively send out ACKs on
                 * initial connection
                 */
            } else {
                trace_gdbstub_err_garbage(ch);
            }
            break;
        case RS_GETLINE:
            if (ch == '}') {
                /* start escape sequence */
                gdbserver_state.state = RS_GETLINE_ESC;
                gdbserver_state.line_sum += ch;
            } else if (ch == '*') {
                /* start run length encoding sequence */
                gdbserver_state.state = RS_GETLINE_RLE;
                gdbserver_state.line_sum += ch;
            } else if (ch == '#') {
                /* end of command, start of checksum*/
                gdbserver_state.state = RS_CHKSUM1;
            } else if (gdbserver_state.line_buf_index >= sizeof(gdbserver_state.line_buf) - 1) {
                trace_gdbstub_err_overrun();
                gdbserver_state.state = RS_IDLE;
            } else {
                /* unescaped command character */
                gdbserver_state.line_buf[gdbserver_state.line_buf_index++] = ch;
                gdbserver_state.line_sum += ch;
            }
            break;
        case RS_GETLINE_ESC:
            if (ch == '#') {
                /* unexpected end of command in escape sequence */
                gdbserver_state.state = RS_CHKSUM1;
            } else if (gdbserver_state.line_buf_index >= sizeof(gdbserver_state.line_buf) - 1) {
                /* command buffer overrun */
                trace_gdbstub_err_overrun();
                gdbserver_state.state = RS_IDLE;
            } else {
                /* parse escaped character and leave escape state */
                gdbserver_state.line_buf[gdbserver_state.line_buf_index++] = ch ^ 0x20;
                gdbserver_state.line_sum += ch;
                gdbserver_state.state = RS_GETLINE;
            }
            break;
        case RS_GETLINE_RLE:
            /*
             * Run-length encoding is explained in "Debugging with GDB /
             * Appendix E GDB Remote Serial Protocol / Overview".
             */
            if (ch < ' ' || ch == '#' || ch == '$' || ch > 126) {
                /* invalid RLE count encoding */
                trace_gdbstub_err_invalid_repeat(ch);
                gdbserver_state.state = RS_GETLINE;
            } else {
                /* decode repeat length */
                int repeat = ch - ' ' + 3;
                if (gdbserver_state.line_buf_index + repeat >= sizeof(gdbserver_state.line_buf) - 1) {
                    /* that many repeats would overrun the command buffer */
                    trace_gdbstub_err_overrun();
                    gdbserver_state.state = RS_IDLE;
                } else if (gdbserver_state.line_buf_index < 1) {
                    /* got a repeat but we have nothing to repeat */
                    trace_gdbstub_err_invalid_rle();
                    gdbserver_state.state = RS_GETLINE;
                } else {
                    /* repeat the last character */
                    memset(gdbserver_state.line_buf + gdbserver_state.line_buf_index,
                           gdbserver_state.line_buf[gdbserver_state.line_buf_index - 1], repeat);
                    gdbserver_state.line_buf_index += repeat;
                    gdbserver_state.line_sum += ch;
                    gdbserver_state.state = RS_GETLINE;
                }
            }
            break;
        case RS_CHKSUM1:
            /* get high hex digit of checksum */
            if (!isxdigit(ch)) {
                trace_gdbstub_err_checksum_invalid(ch);
                gdbserver_state.state = RS_GETLINE;
                break;
            }
            gdbserver_state.line_buf[gdbserver_state.line_buf_index] = '\0';
            gdbserver_state.line_csum = fromhex(ch) << 4;
            gdbserver_state.state = RS_CHKSUM2;
            break;
        case RS_CHKSUM2:
            /* get low hex digit of checksum */
            if (!isxdigit(ch)) {
                trace_gdbstub_err_checksum_invalid(ch);
                gdbserver_state.state = RS_GETLINE;
                break;
            }
            gdbserver_state.line_csum |= fromhex(ch);

            if (gdbserver_state.line_csum != (gdbserver_state.line_sum & 0xff)) {
                trace_gdbstub_err_checksum_incorrect(gdbserver_state.line_sum, gdbserver_state.line_csum);
                /* send NAK reply */
                reply = '-';
                gdb_put_buffer(&reply, 1);
                gdbserver_state.state = RS_IDLE;
            } else {
                /* send ACK reply */
                reply = '+';
                gdb_put_buffer(&reply, 1);
                gdbserver_state.state = gdb_handle_packet(gdbserver_state.line_buf);
            }
            break;
        default:
            abort();
        }
    }
}

/*
 * Create the process that will contain all the "orphan" CPUs (that are not
 * part of a CPU cluster). Note that if this process contains no CPUs, it won't
 * be attachable and thus will be invisible to the user.
 */
void gdb_create_default_process(GDBState *s)
{
    GDBProcess *process;
    int pid;

#ifdef CONFIG_USER_ONLY
    assert(gdbserver_state.process_num == 0);
    pid = getpid();
#else
    if (gdbserver_state.process_num) {
        pid = s->processes[s->process_num - 1].pid;
    } else {
        pid = 0;
    }
    /* We need an available PID slot for this process */
    assert(pid < UINT32_MAX);
    pid++;
#endif

    s->processes = g_renew(GDBProcess, s->processes, ++s->process_num);
    process = &s->processes[s->process_num - 1];
    process->pid = pid;
    process->attached = false;
    process->target_xml = NULL;
}

