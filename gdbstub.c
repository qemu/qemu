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
 * SPDX-License-Identifier: LGPL-2.0+
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/ctype.h"
#include "qemu/cutils.h"
#include "qemu/module.h"
#include "trace-root.h"
#ifdef CONFIG_USER_ONLY
#include "qemu.h"
#else
#include "monitor/monitor.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "sysemu/sysemu.h"
#include "exec/gdbstub.h"
#include "hw/cpu/cluster.h"
#include "hw/boards.h"
#endif

#define MAX_PACKET_LENGTH 4096

#include "qemu/sockets.h"
#include "sysemu/hw_accel.h"
#include "sysemu/kvm.h"
#include "sysemu/runstate.h"
#include "hw/semihosting/semihost.h"
#include "exec/exec-all.h"

#ifdef CONFIG_USER_ONLY
#define GDB_ATTACHED "0"
#else
#define GDB_ATTACHED "1"
#endif

#ifndef CONFIG_USER_ONLY
static int phy_memory_mode;
#endif

static inline int target_memory_rw_debug(CPUState *cpu, target_ulong addr,
                                         uint8_t *buf, int len, bool is_write)
{
    CPUClass *cc;

#ifndef CONFIG_USER_ONLY
    if (phy_memory_mode) {
        if (is_write) {
            cpu_physical_memory_write(addr, buf, len);
        } else {
            cpu_physical_memory_read(addr, buf, len);
        }
        return 0;
    }
#endif

    cc = CPU_GET_CLASS(cpu);
    if (cc->memory_rw_debug) {
        return cc->memory_rw_debug(cpu, addr, buf, len, is_write);
    }
    return cpu_memory_rw_debug(cpu, addr, buf, len, is_write);
}

/* Return the GDB index for a given vCPU state.
 *
 * For user mode this is simply the thread id. In system mode GDB
 * numbers CPUs from 1 as 0 is reserved as an "any cpu" index.
 */
static inline int cpu_gdb_index(CPUState *cpu)
{
#if defined(CONFIG_USER_ONLY)
    TaskState *ts = (TaskState *) cpu->opaque;
    return ts->ts_tid;
#else
    return cpu->cpu_index + 1;
#endif
}

enum {
    GDB_SIGNAL_0 = 0,
    GDB_SIGNAL_INT = 2,
    GDB_SIGNAL_QUIT = 3,
    GDB_SIGNAL_TRAP = 5,
    GDB_SIGNAL_ABRT = 6,
    GDB_SIGNAL_ALRM = 14,
    GDB_SIGNAL_IO = 23,
    GDB_SIGNAL_XCPU = 24,
    GDB_SIGNAL_UNKNOWN = 143
};

#ifdef CONFIG_USER_ONLY

/* Map target signal numbers to GDB protocol signal numbers and vice
 * versa.  For user emulation's currently supported systems, we can
 * assume most signals are defined.
 */

static int gdb_signal_table[] = {
    0,
    TARGET_SIGHUP,
    TARGET_SIGINT,
    TARGET_SIGQUIT,
    TARGET_SIGILL,
    TARGET_SIGTRAP,
    TARGET_SIGABRT,
    -1, /* SIGEMT */
    TARGET_SIGFPE,
    TARGET_SIGKILL,
    TARGET_SIGBUS,
    TARGET_SIGSEGV,
    TARGET_SIGSYS,
    TARGET_SIGPIPE,
    TARGET_SIGALRM,
    TARGET_SIGTERM,
    TARGET_SIGURG,
    TARGET_SIGSTOP,
    TARGET_SIGTSTP,
    TARGET_SIGCONT,
    TARGET_SIGCHLD,
    TARGET_SIGTTIN,
    TARGET_SIGTTOU,
    TARGET_SIGIO,
    TARGET_SIGXCPU,
    TARGET_SIGXFSZ,
    TARGET_SIGVTALRM,
    TARGET_SIGPROF,
    TARGET_SIGWINCH,
    -1, /* SIGLOST */
    TARGET_SIGUSR1,
    TARGET_SIGUSR2,
#ifdef TARGET_SIGPWR
    TARGET_SIGPWR,
#else
    -1,
#endif
    -1, /* SIGPOLL */
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
#ifdef __SIGRTMIN
    __SIGRTMIN + 1,
    __SIGRTMIN + 2,
    __SIGRTMIN + 3,
    __SIGRTMIN + 4,
    __SIGRTMIN + 5,
    __SIGRTMIN + 6,
    __SIGRTMIN + 7,
    __SIGRTMIN + 8,
    __SIGRTMIN + 9,
    __SIGRTMIN + 10,
    __SIGRTMIN + 11,
    __SIGRTMIN + 12,
    __SIGRTMIN + 13,
    __SIGRTMIN + 14,
    __SIGRTMIN + 15,
    __SIGRTMIN + 16,
    __SIGRTMIN + 17,
    __SIGRTMIN + 18,
    __SIGRTMIN + 19,
    __SIGRTMIN + 20,
    __SIGRTMIN + 21,
    __SIGRTMIN + 22,
    __SIGRTMIN + 23,
    __SIGRTMIN + 24,
    __SIGRTMIN + 25,
    __SIGRTMIN + 26,
    __SIGRTMIN + 27,
    __SIGRTMIN + 28,
    __SIGRTMIN + 29,
    __SIGRTMIN + 30,
    __SIGRTMIN + 31,
    -1, /* SIGCANCEL */
    __SIGRTMIN,
    __SIGRTMIN + 32,
    __SIGRTMIN + 33,
    __SIGRTMIN + 34,
    __SIGRTMIN + 35,
    __SIGRTMIN + 36,
    __SIGRTMIN + 37,
    __SIGRTMIN + 38,
    __SIGRTMIN + 39,
    __SIGRTMIN + 40,
    __SIGRTMIN + 41,
    __SIGRTMIN + 42,
    __SIGRTMIN + 43,
    __SIGRTMIN + 44,
    __SIGRTMIN + 45,
    __SIGRTMIN + 46,
    __SIGRTMIN + 47,
    __SIGRTMIN + 48,
    __SIGRTMIN + 49,
    __SIGRTMIN + 50,
    __SIGRTMIN + 51,
    __SIGRTMIN + 52,
    __SIGRTMIN + 53,
    __SIGRTMIN + 54,
    __SIGRTMIN + 55,
    __SIGRTMIN + 56,
    __SIGRTMIN + 57,
    __SIGRTMIN + 58,
    __SIGRTMIN + 59,
    __SIGRTMIN + 60,
    __SIGRTMIN + 61,
    __SIGRTMIN + 62,
    __SIGRTMIN + 63,
    __SIGRTMIN + 64,
    __SIGRTMIN + 65,
    __SIGRTMIN + 66,
    __SIGRTMIN + 67,
    __SIGRTMIN + 68,
    __SIGRTMIN + 69,
    __SIGRTMIN + 70,
    __SIGRTMIN + 71,
    __SIGRTMIN + 72,
    __SIGRTMIN + 73,
    __SIGRTMIN + 74,
    __SIGRTMIN + 75,
    __SIGRTMIN + 76,
    __SIGRTMIN + 77,
    __SIGRTMIN + 78,
    __SIGRTMIN + 79,
    __SIGRTMIN + 80,
    __SIGRTMIN + 81,
    __SIGRTMIN + 82,
    __SIGRTMIN + 83,
    __SIGRTMIN + 84,
    __SIGRTMIN + 85,
    __SIGRTMIN + 86,
    __SIGRTMIN + 87,
    __SIGRTMIN + 88,
    __SIGRTMIN + 89,
    __SIGRTMIN + 90,
    __SIGRTMIN + 91,
    __SIGRTMIN + 92,
    __SIGRTMIN + 93,
    __SIGRTMIN + 94,
    __SIGRTMIN + 95,
    -1, /* SIGINFO */
    -1, /* UNKNOWN */
    -1, /* DEFAULT */
    -1,
    -1,
    -1,
    -1,
    -1,
    -1
#endif
};
#else
/* In system mode we only need SIGINT and SIGTRAP; other signals
   are not yet supported.  */

enum {
    TARGET_SIGINT = 2,
    TARGET_SIGTRAP = 5
};

static int gdb_signal_table[] = {
    -1,
    -1,
    TARGET_SIGINT,
    -1,
    -1,
    TARGET_SIGTRAP
};
#endif

#ifdef CONFIG_USER_ONLY
static int target_signal_to_gdb (int sig)
{
    int i;
    for (i = 0; i < ARRAY_SIZE (gdb_signal_table); i++)
        if (gdb_signal_table[i] == sig)
            return i;
    return GDB_SIGNAL_UNKNOWN;
}
#endif

static int gdb_signal_to_target (int sig)
{
    if (sig < ARRAY_SIZE (gdb_signal_table))
        return gdb_signal_table[sig];
    else
        return -1;
}

typedef struct GDBRegisterState {
    int base_reg;
    int num_regs;
    gdb_reg_cb get_reg;
    gdb_reg_cb set_reg;
    const char *xml;
    struct GDBRegisterState *next;
} GDBRegisterState;

typedef struct GDBProcess {
    uint32_t pid;
    bool attached;

    char target_xml[1024];
} GDBProcess;

enum RSState {
    RS_INACTIVE,
    RS_IDLE,
    RS_GETLINE,
    RS_GETLINE_ESC,
    RS_GETLINE_RLE,
    RS_CHKSUM1,
    RS_CHKSUM2,
};
typedef struct GDBState {
    CPUState *c_cpu; /* current CPU for step/continue ops */
    CPUState *g_cpu; /* current CPU for other ops */
    CPUState *query_cpu; /* for q{f|s}ThreadInfo */
    enum RSState state; /* parsing state */
    char line_buf[MAX_PACKET_LENGTH];
    int line_buf_index;
    int line_sum; /* running checksum */
    int line_csum; /* checksum at the end of the packet */
    uint8_t last_packet[MAX_PACKET_LENGTH + 4];
    int last_packet_len;
    int signal;
#ifdef CONFIG_USER_ONLY
    int fd;
    int running_state;
#else
    CharBackend chr;
    Chardev *mon_chr;
#endif
    bool multiprocess;
    GDBProcess *processes;
    int process_num;
    char syscall_buf[256];
    gdb_syscall_complete_cb current_syscall_cb;
} GDBState;

/* By default use no IRQs and no timers while single stepping so as to
 * make single stepping like an ICE HW step.
 */
static int sstep_flags = SSTEP_ENABLE|SSTEP_NOIRQ|SSTEP_NOTIMER;

static GDBState *gdbserver_state;

bool gdb_has_xml;

#ifdef CONFIG_USER_ONLY
/* XXX: This is not thread safe.  Do we care?  */
static int gdbserver_fd = -1;

static int get_char(GDBState *s)
{
    uint8_t ch;
    int ret;

    for(;;) {
        ret = qemu_recv(s->fd, &ch, 1, 0);
        if (ret < 0) {
            if (errno == ECONNRESET)
                s->fd = -1;
            if (errno != EINTR)
                return -1;
        } else if (ret == 0) {
            close(s->fd);
            s->fd = -1;
            return -1;
        } else {
            break;
        }
    }
    return ch;
}
#endif

static enum {
    GDB_SYS_UNKNOWN,
    GDB_SYS_ENABLED,
    GDB_SYS_DISABLED,
} gdb_syscall_mode;

/* Decide if either remote gdb syscalls or native file IO should be used. */
int use_gdb_syscalls(void)
{
    SemihostingTarget target = semihosting_get_target();
    if (target == SEMIHOSTING_TARGET_NATIVE) {
        /* -semihosting-config target=native */
        return false;
    } else if (target == SEMIHOSTING_TARGET_GDB) {
        /* -semihosting-config target=gdb */
        return true;
    }

    /* -semihosting-config target=auto */
    /* On the first call check if gdb is connected and remember. */
    if (gdb_syscall_mode == GDB_SYS_UNKNOWN) {
        gdb_syscall_mode = (gdbserver_state ? GDB_SYS_ENABLED
                                            : GDB_SYS_DISABLED);
    }
    return gdb_syscall_mode == GDB_SYS_ENABLED;
}

/* Resume execution.  */
static inline void gdb_continue(GDBState *s)
{

#ifdef CONFIG_USER_ONLY
    s->running_state = 1;
    trace_gdbstub_op_continue();
#else
    if (!runstate_needs_reset()) {
        trace_gdbstub_op_continue();
        vm_start();
    }
#endif
}

/*
 * Resume execution, per CPU actions. For user-mode emulation it's
 * equivalent to gdb_continue.
 */
static int gdb_continue_partial(GDBState *s, char *newstates)
{
    CPUState *cpu;
    int res = 0;
#ifdef CONFIG_USER_ONLY
    /*
     * This is not exactly accurate, but it's an improvement compared to the
     * previous situation, where only one CPU would be single-stepped.
     */
    CPU_FOREACH(cpu) {
        if (newstates[cpu->cpu_index] == 's') {
            trace_gdbstub_op_stepping(cpu->cpu_index);
            cpu_single_step(cpu, sstep_flags);
        }
    }
    s->running_state = 1;
#else
    int flag = 0;

    if (!runstate_needs_reset()) {
        if (vm_prepare_start()) {
            return 0;
        }

        CPU_FOREACH(cpu) {
            switch (newstates[cpu->cpu_index]) {
            case 0:
            case 1:
                break; /* nothing to do here */
            case 's':
                trace_gdbstub_op_stepping(cpu->cpu_index);
                cpu_single_step(cpu, sstep_flags);
                cpu_resume(cpu);
                flag = 1;
                break;
            case 'c':
                trace_gdbstub_op_continue_cpu(cpu->cpu_index);
                cpu_resume(cpu);
                flag = 1;
                break;
            default:
                res = -1;
                break;
            }
        }
    }
    if (flag) {
        qemu_clock_enable(QEMU_CLOCK_VIRTUAL, true);
    }
#endif
    return res;
}

static void put_buffer(GDBState *s, const uint8_t *buf, int len)
{
#ifdef CONFIG_USER_ONLY
    int ret;

    while (len > 0) {
        ret = send(s->fd, buf, len, 0);
        if (ret < 0) {
            if (errno != EINTR)
                return;
        } else {
            buf += ret;
            len -= ret;
        }
    }
#else
    /* XXX this blocks entire thread. Rewrite to use
     * qemu_chr_fe_write and background I/O callbacks */
    qemu_chr_fe_write_all(&s->chr, buf, len);
#endif
}

static inline int fromhex(int v)
{
    if (v >= '0' && v <= '9')
        return v - '0';
    else if (v >= 'A' && v <= 'F')
        return v - 'A' + 10;
    else if (v >= 'a' && v <= 'f')
        return v - 'a' + 10;
    else
        return 0;
}

static inline int tohex(int v)
{
    if (v < 10)
        return v + '0';
    else
        return v - 10 + 'a';
}

/* writes 2*len+1 bytes in buf */
static void memtohex(char *buf, const uint8_t *mem, int len)
{
    int i, c;
    char *q;
    q = buf;
    for(i = 0; i < len; i++) {
        c = mem[i];
        *q++ = tohex(c >> 4);
        *q++ = tohex(c & 0xf);
    }
    *q = '\0';
}

static void hextomem(uint8_t *mem, const char *buf, int len)
{
    int i;

    for(i = 0; i < len; i++) {
        mem[i] = (fromhex(buf[0]) << 4) | fromhex(buf[1]);
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
static int put_packet_binary(GDBState *s, const char *buf, int len, bool dump)
{
    int csum, i;
    uint8_t *p;

    if (dump && trace_event_get_state_backends(TRACE_GDBSTUB_IO_BINARYREPLY)) {
        hexdump(buf, len, trace_gdbstub_io_binaryreply);
    }

    for(;;) {
        p = s->last_packet;
        *(p++) = '$';
        memcpy(p, buf, len);
        p += len;
        csum = 0;
        for(i = 0; i < len; i++) {
            csum += buf[i];
        }
        *(p++) = '#';
        *(p++) = tohex((csum >> 4) & 0xf);
        *(p++) = tohex((csum) & 0xf);

        s->last_packet_len = p - s->last_packet;
        put_buffer(s, (uint8_t *)s->last_packet, s->last_packet_len);

#ifdef CONFIG_USER_ONLY
        i = get_char(s);
        if (i < 0)
            return -1;
        if (i == '+')
            break;
#else
        break;
#endif
    }
    return 0;
}

/* return -1 if error, 0 if OK */
static int put_packet(GDBState *s, const char *buf)
{
    trace_gdbstub_io_reply(buf);

    return put_packet_binary(s, buf, strlen(buf), false);
}

/* Encode data using the encoding for 'x' packets.  */
static int memtox(char *buf, const char *mem, int len)
{
    char *p = buf;
    char c;

    while (len--) {
        c = *(mem++);
        switch (c) {
        case '#': case '$': case '*': case '}':
            *(p++) = '}';
            *(p++) = c ^ 0x20;
            break;
        default:
            *(p++) = c;
            break;
        }
    }
    return p - buf;
}

static uint32_t gdb_get_cpu_pid(const GDBState *s, CPUState *cpu)
{
    /* TODO: In user mode, we should use the task state PID */
    if (cpu->cluster_index == UNASSIGNED_CLUSTER_INDEX) {
        /* Return the default process' PID */
        return s->processes[s->process_num - 1].pid;
    }
    return cpu->cluster_index + 1;
}

static GDBProcess *gdb_get_process(const GDBState *s, uint32_t pid)
{
    int i;

    if (!pid) {
        /* 0 means any process, we take the first one */
        return &s->processes[0];
    }

    for (i = 0; i < s->process_num; i++) {
        if (s->processes[i].pid == pid) {
            return &s->processes[i];
        }
    }

    return NULL;
}

static GDBProcess *gdb_get_cpu_process(const GDBState *s, CPUState *cpu)
{
    return gdb_get_process(s, gdb_get_cpu_pid(s, cpu));
}

static CPUState *find_cpu(uint32_t thread_id)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        if (cpu_gdb_index(cpu) == thread_id) {
            return cpu;
        }
    }

    return NULL;
}

static CPUState *get_first_cpu_in_process(const GDBState *s,
                                          GDBProcess *process)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        if (gdb_get_cpu_pid(s, cpu) == process->pid) {
            return cpu;
        }
    }

    return NULL;
}

static CPUState *gdb_next_cpu_in_process(const GDBState *s, CPUState *cpu)
{
    uint32_t pid = gdb_get_cpu_pid(s, cpu);
    cpu = CPU_NEXT(cpu);

    while (cpu) {
        if (gdb_get_cpu_pid(s, cpu) == pid) {
            break;
        }

        cpu = CPU_NEXT(cpu);
    }

    return cpu;
}

/* Return the cpu following @cpu, while ignoring unattached processes. */
static CPUState *gdb_next_attached_cpu(const GDBState *s, CPUState *cpu)
{
    cpu = CPU_NEXT(cpu);

    while (cpu) {
        if (gdb_get_cpu_process(s, cpu)->attached) {
            break;
        }

        cpu = CPU_NEXT(cpu);
    }

    return cpu;
}

/* Return the first attached cpu */
static CPUState *gdb_first_attached_cpu(const GDBState *s)
{
    CPUState *cpu = first_cpu;
    GDBProcess *process = gdb_get_cpu_process(s, cpu);

    if (!process->attached) {
        return gdb_next_attached_cpu(s, cpu);
    }

    return cpu;
}

static CPUState *gdb_get_cpu(const GDBState *s, uint32_t pid, uint32_t tid)
{
    GDBProcess *process;
    CPUState *cpu;

    if (!pid && !tid) {
        /* 0 means any process/thread, we take the first attached one */
        return gdb_first_attached_cpu(s);
    } else if (pid && !tid) {
        /* any thread in a specific process */
        process = gdb_get_process(s, pid);

        if (process == NULL) {
            return NULL;
        }

        if (!process->attached) {
            return NULL;
        }

        return get_first_cpu_in_process(s, process);
    } else {
        /* a specific thread */
        cpu = find_cpu(tid);

        if (cpu == NULL) {
            return NULL;
        }

        process = gdb_get_cpu_process(s, cpu);

        if (pid && process->pid != pid) {
            return NULL;
        }

        if (!process->attached) {
            return NULL;
        }

        return cpu;
    }
}

static const char *get_feature_xml(const GDBState *s, const char *p,
                                   const char **newp, GDBProcess *process)
{
    size_t len;
    int i;
    const char *name;
    CPUState *cpu = get_first_cpu_in_process(s, process);
    CPUClass *cc = CPU_GET_CLASS(cpu);

    len = 0;
    while (p[len] && p[len] != ':')
        len++;
    *newp = p + len;

    name = NULL;
    if (strncmp(p, "target.xml", len) == 0) {
        char *buf = process->target_xml;
        const size_t buf_sz = sizeof(process->target_xml);

        /* Generate the XML description for this CPU.  */
        if (!buf[0]) {
            GDBRegisterState *r;

            pstrcat(buf, buf_sz,
                    "<?xml version=\"1.0\"?>"
                    "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
                    "<target>");
            if (cc->gdb_arch_name) {
                gchar *arch = cc->gdb_arch_name(cpu);
                pstrcat(buf, buf_sz, "<architecture>");
                pstrcat(buf, buf_sz, arch);
                pstrcat(buf, buf_sz, "</architecture>");
                g_free(arch);
            }
            pstrcat(buf, buf_sz, "<xi:include href=\"");
            pstrcat(buf, buf_sz, cc->gdb_core_xml_file);
            pstrcat(buf, buf_sz, "\"/>");
            for (r = cpu->gdb_regs; r; r = r->next) {
                pstrcat(buf, buf_sz, "<xi:include href=\"");
                pstrcat(buf, buf_sz, r->xml);
                pstrcat(buf, buf_sz, "\"/>");
            }
            pstrcat(buf, buf_sz, "</target>");
        }
        return buf;
    }
    if (cc->gdb_get_dynamic_xml) {
        char *xmlname = g_strndup(p, len);
        const char *xml = cc->gdb_get_dynamic_xml(cpu, xmlname);

        g_free(xmlname);
        if (xml) {
            return xml;
        }
    }
    for (i = 0; ; i++) {
        name = xml_builtin[i][0];
        if (!name || (strncmp(name, p, len) == 0 && strlen(name) == len))
            break;
    }
    return name ? xml_builtin[i][1] : NULL;
}

static int gdb_read_register(CPUState *cpu, uint8_t *mem_buf, int reg)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);
    CPUArchState *env = cpu->env_ptr;
    GDBRegisterState *r;

    if (reg < cc->gdb_num_core_regs) {
        return cc->gdb_read_register(cpu, mem_buf, reg);
    }

    for (r = cpu->gdb_regs; r; r = r->next) {
        if (r->base_reg <= reg && reg < r->base_reg + r->num_regs) {
            return r->get_reg(env, mem_buf, reg - r->base_reg);
        }
    }
    return 0;
}

static int gdb_write_register(CPUState *cpu, uint8_t *mem_buf, int reg)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);
    CPUArchState *env = cpu->env_ptr;
    GDBRegisterState *r;

    if (reg < cc->gdb_num_core_regs) {
        return cc->gdb_write_register(cpu, mem_buf, reg);
    }

    for (r = cpu->gdb_regs; r; r = r->next) {
        if (r->base_reg <= reg && reg < r->base_reg + r->num_regs) {
            return r->set_reg(env, mem_buf, reg - r->base_reg);
        }
    }
    return 0;
}

/* Register a supplemental set of CPU registers.  If g_pos is nonzero it
   specifies the first register number and these registers are included in
   a standard "g" packet.  Direction is relative to gdb, i.e. get_reg is
   gdb reading a CPU register, and set_reg is gdb modifying a CPU register.
 */

void gdb_register_coprocessor(CPUState *cpu,
                              gdb_reg_cb get_reg, gdb_reg_cb set_reg,
                              int num_regs, const char *xml, int g_pos)
{
    GDBRegisterState *s;
    GDBRegisterState **p;

    p = &cpu->gdb_regs;
    while (*p) {
        /* Check for duplicates.  */
        if (strcmp((*p)->xml, xml) == 0)
            return;
        p = &(*p)->next;
    }

    s = g_new0(GDBRegisterState, 1);
    s->base_reg = cpu->gdb_num_regs;
    s->num_regs = num_regs;
    s->get_reg = get_reg;
    s->set_reg = set_reg;
    s->xml = xml;

    /* Add to end of list.  */
    cpu->gdb_num_regs += num_regs;
    *p = s;
    if (g_pos) {
        if (g_pos != s->base_reg) {
            error_report("Error: Bad gdb register numbering for '%s', "
                         "expected %d got %d", xml, g_pos, s->base_reg);
        } else {
            cpu->gdb_num_g_regs = cpu->gdb_num_regs;
        }
    }
}

#ifndef CONFIG_USER_ONLY
/* Translate GDB watchpoint type to a flags value for cpu_watchpoint_* */
static inline int xlat_gdb_type(CPUState *cpu, int gdbtype)
{
    static const int xlat[] = {
        [GDB_WATCHPOINT_WRITE]  = BP_GDB | BP_MEM_WRITE,
        [GDB_WATCHPOINT_READ]   = BP_GDB | BP_MEM_READ,
        [GDB_WATCHPOINT_ACCESS] = BP_GDB | BP_MEM_ACCESS,
    };

    CPUClass *cc = CPU_GET_CLASS(cpu);
    int cputype = xlat[gdbtype];

    if (cc->gdb_stop_before_watchpoint) {
        cputype |= BP_STOP_BEFORE_ACCESS;
    }
    return cputype;
}
#endif

static int gdb_breakpoint_insert(int type, target_ulong addr, target_ulong len)
{
    CPUState *cpu;
    int err = 0;

    if (kvm_enabled()) {
        return kvm_insert_breakpoint(gdbserver_state->c_cpu, addr, len, type);
    }

    switch (type) {
    case GDB_BREAKPOINT_SW:
    case GDB_BREAKPOINT_HW:
        CPU_FOREACH(cpu) {
            err = cpu_breakpoint_insert(cpu, addr, BP_GDB, NULL);
            if (err) {
                break;
            }
        }
        return err;
#ifndef CONFIG_USER_ONLY
    case GDB_WATCHPOINT_WRITE:
    case GDB_WATCHPOINT_READ:
    case GDB_WATCHPOINT_ACCESS:
        CPU_FOREACH(cpu) {
            err = cpu_watchpoint_insert(cpu, addr, len,
                                        xlat_gdb_type(cpu, type), NULL);
            if (err) {
                break;
            }
        }
        return err;
#endif
    default:
        return -ENOSYS;
    }
}

static int gdb_breakpoint_remove(int type, target_ulong addr, target_ulong len)
{
    CPUState *cpu;
    int err = 0;

    if (kvm_enabled()) {
        return kvm_remove_breakpoint(gdbserver_state->c_cpu, addr, len, type);
    }

    switch (type) {
    case GDB_BREAKPOINT_SW:
    case GDB_BREAKPOINT_HW:
        CPU_FOREACH(cpu) {
            err = cpu_breakpoint_remove(cpu, addr, BP_GDB);
            if (err) {
                break;
            }
        }
        return err;
#ifndef CONFIG_USER_ONLY
    case GDB_WATCHPOINT_WRITE:
    case GDB_WATCHPOINT_READ:
    case GDB_WATCHPOINT_ACCESS:
        CPU_FOREACH(cpu) {
            err = cpu_watchpoint_remove(cpu, addr, len,
                                        xlat_gdb_type(cpu, type));
            if (err)
                break;
        }
        return err;
#endif
    default:
        return -ENOSYS;
    }
}

static inline void gdb_cpu_breakpoint_remove_all(CPUState *cpu)
{
    cpu_breakpoint_remove_all(cpu, BP_GDB);
#ifndef CONFIG_USER_ONLY
    cpu_watchpoint_remove_all(cpu, BP_GDB);
#endif
}

static void gdb_process_breakpoint_remove_all(const GDBState *s, GDBProcess *p)
{
    CPUState *cpu = get_first_cpu_in_process(s, p);

    while (cpu) {
        gdb_cpu_breakpoint_remove_all(cpu);
        cpu = gdb_next_cpu_in_process(s, cpu);
    }
}

static void gdb_breakpoint_remove_all(void)
{
    CPUState *cpu;

    if (kvm_enabled()) {
        kvm_remove_all_breakpoints(gdbserver_state->c_cpu);
        return;
    }

    CPU_FOREACH(cpu) {
        gdb_cpu_breakpoint_remove_all(cpu);
    }
}

static void gdb_set_cpu_pc(GDBState *s, target_ulong pc)
{
    CPUState *cpu = s->c_cpu;

    cpu_synchronize_state(cpu);
    cpu_set_pc(cpu, pc);
}

static char *gdb_fmt_thread_id(const GDBState *s, CPUState *cpu,
                           char *buf, size_t buf_size)
{
    if (s->multiprocess) {
        snprintf(buf, buf_size, "p%02x.%02x",
                 gdb_get_cpu_pid(s, cpu), cpu_gdb_index(cpu));
    } else {
        snprintf(buf, buf_size, "%02x", cpu_gdb_index(cpu));
    }

    return buf;
}

typedef enum GDBThreadIdKind {
    GDB_ONE_THREAD = 0,
    GDB_ALL_THREADS,     /* One process, all threads */
    GDB_ALL_PROCESSES,
    GDB_READ_THREAD_ERR
} GDBThreadIdKind;

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
        p = 1;
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
static int gdb_handle_vcont(GDBState *s, const char *p)
{
    int res, signal = 0;
    char cur_action;
    char *newstates;
    unsigned long tmp;
    uint32_t pid, tid;
    GDBProcess *process;
    CPUState *cpu;
    GDBThreadIdKind kind;
#ifdef CONFIG_USER_ONLY
    int max_cpus = 1; /* global variable max_cpus exists only in system mode */

    CPU_FOREACH(cpu) {
        max_cpus = max_cpus <= cpu->cpu_index ? cpu->cpu_index + 1 : max_cpus;
    }
#else
    MachineState *ms = MACHINE(qdev_get_machine());
    unsigned int max_cpus = ms->smp.max_cpus;
#endif
    /* uninitialised CPUs stay 0 */
    newstates = g_new0(char, max_cpus);

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
    while (*p) {
        if (*p++ != ';') {
            res = -ENOTSUP;
            goto out;
        }

        cur_action = *p++;
        if (cur_action == 'C' || cur_action == 'S') {
            cur_action = qemu_tolower(cur_action);
            res = qemu_strtoul(p + 1, &p, 16, &tmp);
            if (res) {
                goto out;
            }
            signal = gdb_signal_to_target(tmp);
        } else if (cur_action != 'c' && cur_action != 's') {
            /* unknown/invalid/unsupported command */
            res = -ENOTSUP;
            goto out;
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
            res = -ENOTSUP;
            goto out;
        }

        switch (kind) {
        case GDB_READ_THREAD_ERR:
            res = -EINVAL;
            goto out;

        case GDB_ALL_PROCESSES:
            cpu = gdb_first_attached_cpu(s);
            while (cpu) {
                if (newstates[cpu->cpu_index] == 1) {
                    newstates[cpu->cpu_index] = cur_action;
                }

                cpu = gdb_next_attached_cpu(s, cpu);
            }
            break;

        case GDB_ALL_THREADS:
            process = gdb_get_process(s, pid);

            if (!process->attached) {
                res = -EINVAL;
                goto out;
            }

            cpu = get_first_cpu_in_process(s, process);
            while (cpu) {
                if (newstates[cpu->cpu_index] == 1) {
                    newstates[cpu->cpu_index] = cur_action;
                }

                cpu = gdb_next_cpu_in_process(s, cpu);
            }
            break;

        case GDB_ONE_THREAD:
            cpu = gdb_get_cpu(s, pid, tid);

            /* invalid CPU/thread specified */
            if (!cpu) {
                res = -EINVAL;
                goto out;
            }

            /* only use if no previous match occourred */
            if (newstates[cpu->cpu_index] == 1) {
                newstates[cpu->cpu_index] = cur_action;
            }
            break;
        }
    }
    s->signal = signal;
    gdb_continue_partial(s, newstates);

out:
    g_free(newstates);

    return res;
}

typedef union GdbCmdVariant {
    const char *data;
    uint8_t opcode;
    unsigned long val_ul;
    unsigned long long val_ull;
    struct {
        GDBThreadIdKind kind;
        uint32_t pid;
        uint32_t tid;
    } thread_id;
} GdbCmdVariant;

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
                            GdbCmdVariant *params, int *num_params)
{
    int curr_param;
    const char *curr_schema, *curr_data;

    *num_params = 0;

    if (!schema) {
        return 0;
    }

    curr_schema = schema;
    curr_param = 0;
    curr_data = data;
    while (curr_schema[0] && curr_schema[1] && *curr_data) {
        switch (curr_schema[0]) {
        case 'l':
            if (qemu_strtoul(curr_data, &curr_data, 16,
                             &params[curr_param].val_ul)) {
                return -EINVAL;
            }
            curr_param++;
            curr_data = cmd_next_param(curr_data, curr_schema[1]);
            break;
        case 'L':
            if (qemu_strtou64(curr_data, &curr_data, 16,
                              (uint64_t *)&params[curr_param].val_ull)) {
                return -EINVAL;
            }
            curr_param++;
            curr_data = cmd_next_param(curr_data, curr_schema[1]);
            break;
        case 's':
            params[curr_param].data = curr_data;
            curr_param++;
            curr_data = cmd_next_param(curr_data, curr_schema[1]);
            break;
        case 'o':
            params[curr_param].opcode = *(uint8_t *)curr_data;
            curr_param++;
            curr_data = cmd_next_param(curr_data, curr_schema[1]);
            break;
        case 't':
            params[curr_param].thread_id.kind =
                read_thread_id(curr_data, &curr_data,
                               &params[curr_param].thread_id.pid,
                               &params[curr_param].thread_id.tid);
            curr_param++;
            curr_data = cmd_next_param(curr_data, curr_schema[1]);
            break;
        case '?':
            curr_data = cmd_next_param(curr_data, curr_schema[1]);
            break;
        default:
            return -EINVAL;
        }
        curr_schema += 2;
    }

    *num_params = curr_param;
    return 0;
}

typedef struct GdbCmdContext {
    GDBState *s;
    GdbCmdVariant *params;
    int num_params;
    uint8_t mem_buf[MAX_PACKET_LENGTH];
    char str_buf[MAX_PACKET_LENGTH + 1];
} GdbCmdContext;

typedef void (*GdbCmdHandler)(GdbCmdContext *gdb_ctx, void *user_ctx);

/*
 * cmd_startswith -> cmd is compared using startswith
 *
 *
 * schema definitions:
 * Each schema parameter entry consists of 2 chars,
 * the first char represents the parameter type handling
 * the second char represents the delimiter for the next parameter
 *
 * Currently supported schema types:
 * 'l' -> unsigned long (stored in .val_ul)
 * 'L' -> unsigned long long (stored in .val_ull)
 * 's' -> string (stored in .data)
 * 'o' -> single char (stored in .opcode)
 * 't' -> thread id (stored in .thread_id)
 * '?' -> skip according to delimiter
 *
 * Currently supported delimiters:
 * '?' -> Stop at any delimiter (",;:=\0")
 * '0' -> Stop at "\0"
 * '.' -> Skip 1 char unless reached "\0"
 * Any other value is treated as the delimiter value itself
 */
typedef struct GdbCmdParseEntry {
    GdbCmdHandler handler;
    const char *cmd;
    bool cmd_startswith;
    const char *schema;
} GdbCmdParseEntry;

static inline int startswith(const char *string, const char *pattern)
{
  return !strncmp(string, pattern, strlen(pattern));
}

static int process_string_cmd(GDBState *s, void *user_ctx, const char *data,
                              const GdbCmdParseEntry *cmds, int num_cmds)
{
    int i, schema_len, max_num_params = 0;
    GdbCmdContext gdb_ctx;

    if (!cmds) {
        return -1;
    }

    for (i = 0; i < num_cmds; i++) {
        const GdbCmdParseEntry *cmd = &cmds[i];
        g_assert(cmd->handler && cmd->cmd);

        if ((cmd->cmd_startswith && !startswith(data, cmd->cmd)) ||
            (!cmd->cmd_startswith && strcmp(cmd->cmd, data))) {
            continue;
        }

        if (cmd->schema) {
            schema_len = strlen(cmd->schema);
            if (schema_len % 2) {
                return -2;
            }

            max_num_params = schema_len / 2;
        }

        gdb_ctx.params =
            (GdbCmdVariant *)alloca(sizeof(*gdb_ctx.params) * max_num_params);
        memset(gdb_ctx.params, 0, sizeof(*gdb_ctx.params) * max_num_params);

        if (cmd_parse_params(&data[strlen(cmd->cmd)], cmd->schema,
                             gdb_ctx.params, &gdb_ctx.num_params)) {
            return -1;
        }

        gdb_ctx.s = s;
        cmd->handler(&gdb_ctx, user_ctx);
        return 0;
    }

    return -1;
}

static void run_cmd_parser(GDBState *s, const char *data,
                           const GdbCmdParseEntry *cmd)
{
    if (!data) {
        return;
    }

    /* In case there was an error during the command parsing we must
    * send a NULL packet to indicate the command is not supported */
    if (process_string_cmd(s, NULL, data, cmd, 1)) {
        put_packet(s, "");
    }
}

static void handle_detach(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    GDBProcess *process;
    GDBState *s = gdb_ctx->s;
    uint32_t pid = 1;

    if (s->multiprocess) {
        if (!gdb_ctx->num_params) {
            put_packet(s, "E22");
            return;
        }

        pid = gdb_ctx->params[0].val_ul;
    }

    process = gdb_get_process(s, pid);
    gdb_process_breakpoint_remove_all(s, process);
    process->attached = false;

    if (pid == gdb_get_cpu_pid(s, s->c_cpu)) {
        s->c_cpu = gdb_first_attached_cpu(s);
    }

    if (pid == gdb_get_cpu_pid(s, s->g_cpu)) {
        s->g_cpu = gdb_first_attached_cpu(s);
    }

    if (!s->c_cpu) {
        /* No more process attached */
        gdb_syscall_mode = GDB_SYS_DISABLED;
        gdb_continue(s);
    }
    put_packet(s, "OK");
}

static void handle_thread_alive(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    CPUState *cpu;

    if (!gdb_ctx->num_params) {
        put_packet(gdb_ctx->s, "E22");
        return;
    }

    if (gdb_ctx->params[0].thread_id.kind == GDB_READ_THREAD_ERR) {
        put_packet(gdb_ctx->s, "E22");
        return;
    }

    cpu = gdb_get_cpu(gdb_ctx->s, gdb_ctx->params[0].thread_id.pid,
                      gdb_ctx->params[0].thread_id.tid);
    if (!cpu) {
        put_packet(gdb_ctx->s, "E22");
        return;
    }

    put_packet(gdb_ctx->s, "OK");
}

static void handle_continue(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    if (gdb_ctx->num_params) {
        gdb_set_cpu_pc(gdb_ctx->s, gdb_ctx->params[0].val_ull);
    }

    gdb_ctx->s->signal = 0;
    gdb_continue(gdb_ctx->s);
}

static void handle_cont_with_sig(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    unsigned long signal = 0;

    /*
     * Note: C sig;[addr] is currently unsupported and we simply
     *       omit the addr parameter
     */
    if (gdb_ctx->num_params) {
        signal = gdb_ctx->params[0].val_ul;
    }

    gdb_ctx->s->signal = gdb_signal_to_target(signal);
    if (gdb_ctx->s->signal == -1) {
        gdb_ctx->s->signal = 0;
    }
    gdb_continue(gdb_ctx->s);
}

static void handle_set_thread(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    CPUState *cpu;

    if (gdb_ctx->num_params != 2) {
        put_packet(gdb_ctx->s, "E22");
        return;
    }

    if (gdb_ctx->params[1].thread_id.kind == GDB_READ_THREAD_ERR) {
        put_packet(gdb_ctx->s, "E22");
        return;
    }

    if (gdb_ctx->params[1].thread_id.kind != GDB_ONE_THREAD) {
        put_packet(gdb_ctx->s, "OK");
        return;
    }

    cpu = gdb_get_cpu(gdb_ctx->s, gdb_ctx->params[1].thread_id.pid,
                      gdb_ctx->params[1].thread_id.tid);
    if (!cpu) {
        put_packet(gdb_ctx->s, "E22");
        return;
    }

    /*
     * Note: This command is deprecated and modern gdb's will be using the
     *       vCont command instead.
     */
    switch (gdb_ctx->params[0].opcode) {
    case 'c':
        gdb_ctx->s->c_cpu = cpu;
        put_packet(gdb_ctx->s, "OK");
        break;
    case 'g':
        gdb_ctx->s->g_cpu = cpu;
        put_packet(gdb_ctx->s, "OK");
        break;
    default:
        put_packet(gdb_ctx->s, "E22");
        break;
    }
}

static void handle_insert_bp(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    int res;

    if (gdb_ctx->num_params != 3) {
        put_packet(gdb_ctx->s, "E22");
        return;
    }

    res = gdb_breakpoint_insert(gdb_ctx->params[0].val_ul,
                                gdb_ctx->params[1].val_ull,
                                gdb_ctx->params[2].val_ull);
    if (res >= 0) {
        put_packet(gdb_ctx->s, "OK");
        return;
    } else if (res == -ENOSYS) {
        put_packet(gdb_ctx->s, "");
        return;
    }

    put_packet(gdb_ctx->s, "E22");
}

static void handle_remove_bp(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    int res;

    if (gdb_ctx->num_params != 3) {
        put_packet(gdb_ctx->s, "E22");
        return;
    }

    res = gdb_breakpoint_remove(gdb_ctx->params[0].val_ul,
                                gdb_ctx->params[1].val_ull,
                                gdb_ctx->params[2].val_ull);
    if (res >= 0) {
        put_packet(gdb_ctx->s, "OK");
        return;
    } else if (res == -ENOSYS) {
        put_packet(gdb_ctx->s, "");
        return;
    }

    put_packet(gdb_ctx->s, "E22");
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

static void handle_set_reg(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    int reg_size;

    if (!gdb_has_xml) {
        put_packet(gdb_ctx->s, "");
        return;
    }

    if (gdb_ctx->num_params != 2) {
        put_packet(gdb_ctx->s, "E22");
        return;
    }

    reg_size = strlen(gdb_ctx->params[1].data) / 2;
    hextomem(gdb_ctx->mem_buf, gdb_ctx->params[1].data, reg_size);
    gdb_write_register(gdb_ctx->s->g_cpu, gdb_ctx->mem_buf,
                       gdb_ctx->params[0].val_ull);
    put_packet(gdb_ctx->s, "OK");
}

static void handle_get_reg(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    int reg_size;

    if (!gdb_has_xml) {
        put_packet(gdb_ctx->s, "");
        return;
    }

    if (!gdb_ctx->num_params) {
        put_packet(gdb_ctx->s, "E14");
        return;
    }

    reg_size = gdb_read_register(gdb_ctx->s->g_cpu, gdb_ctx->mem_buf,
                                 gdb_ctx->params[0].val_ull);
    if (!reg_size) {
        put_packet(gdb_ctx->s, "E14");
        return;
    }

    memtohex(gdb_ctx->str_buf, gdb_ctx->mem_buf, reg_size);
    put_packet(gdb_ctx->s, gdb_ctx->str_buf);
}

static void handle_write_mem(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    if (gdb_ctx->num_params != 3) {
        put_packet(gdb_ctx->s, "E22");
        return;
    }

    /* hextomem() reads 2*len bytes */
    if (gdb_ctx->params[1].val_ull > strlen(gdb_ctx->params[2].data) / 2) {
        put_packet(gdb_ctx->s, "E22");
        return;
    }

    hextomem(gdb_ctx->mem_buf, gdb_ctx->params[2].data,
             gdb_ctx->params[1].val_ull);
    if (target_memory_rw_debug(gdb_ctx->s->g_cpu, gdb_ctx->params[0].val_ull,
                               gdb_ctx->mem_buf,
                               gdb_ctx->params[1].val_ull, true)) {
        put_packet(gdb_ctx->s, "E14");
        return;
    }

    put_packet(gdb_ctx->s, "OK");
}

static void handle_read_mem(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    if (gdb_ctx->num_params != 2) {
        put_packet(gdb_ctx->s, "E22");
        return;
    }

    /* memtohex() doubles the required space */
    if (gdb_ctx->params[1].val_ull > MAX_PACKET_LENGTH / 2) {
        put_packet(gdb_ctx->s, "E22");
        return;
    }

    if (target_memory_rw_debug(gdb_ctx->s->g_cpu, gdb_ctx->params[0].val_ull,
                               gdb_ctx->mem_buf,
                               gdb_ctx->params[1].val_ull, false)) {
        put_packet(gdb_ctx->s, "E14");
        return;
    }

    memtohex(gdb_ctx->str_buf, gdb_ctx->mem_buf, gdb_ctx->params[1].val_ull);
    put_packet(gdb_ctx->s, gdb_ctx->str_buf);
}

static void handle_write_all_regs(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    target_ulong addr, len;
    uint8_t *registers;
    int reg_size;

    if (!gdb_ctx->num_params) {
        return;
    }

    cpu_synchronize_state(gdb_ctx->s->g_cpu);
    registers = gdb_ctx->mem_buf;
    len = strlen(gdb_ctx->params[0].data) / 2;
    hextomem(registers, gdb_ctx->params[0].data, len);
    for (addr = 0; addr < gdb_ctx->s->g_cpu->gdb_num_g_regs && len > 0;
         addr++) {
        reg_size = gdb_write_register(gdb_ctx->s->g_cpu, registers, addr);
        len -= reg_size;
        registers += reg_size;
    }
    put_packet(gdb_ctx->s, "OK");
}

static void handle_read_all_regs(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    target_ulong addr, len;

    cpu_synchronize_state(gdb_ctx->s->g_cpu);
    len = 0;
    for (addr = 0; addr < gdb_ctx->s->g_cpu->gdb_num_g_regs; addr++) {
        len += gdb_read_register(gdb_ctx->s->g_cpu, gdb_ctx->mem_buf + len,
                                 addr);
    }

    memtohex(gdb_ctx->str_buf, gdb_ctx->mem_buf, len);
    put_packet(gdb_ctx->s, gdb_ctx->str_buf);
}

static void handle_file_io(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    if (gdb_ctx->num_params >= 1 && gdb_ctx->s->current_syscall_cb) {
        target_ulong ret, err;

        ret = (target_ulong)gdb_ctx->params[0].val_ull;
        if (gdb_ctx->num_params >= 2) {
            err = (target_ulong)gdb_ctx->params[1].val_ull;
        } else {
            err = 0;
        }
        gdb_ctx->s->current_syscall_cb(gdb_ctx->s->c_cpu, ret, err);
        gdb_ctx->s->current_syscall_cb = NULL;
    }

    if (gdb_ctx->num_params >= 3 && gdb_ctx->params[2].opcode == (uint8_t)'C') {
        put_packet(gdb_ctx->s, "T02");
        return;
    }

    gdb_continue(gdb_ctx->s);
}

static void handle_step(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    if (gdb_ctx->num_params) {
        gdb_set_cpu_pc(gdb_ctx->s, (target_ulong)gdb_ctx->params[0].val_ull);
    }

    cpu_single_step(gdb_ctx->s->c_cpu, sstep_flags);
    gdb_continue(gdb_ctx->s);
}

static void handle_v_cont_query(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    put_packet(gdb_ctx->s, "vCont;c;C;s;S");
}

static void handle_v_cont(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    int res;

    if (!gdb_ctx->num_params) {
        return;
    }

    res = gdb_handle_vcont(gdb_ctx->s, gdb_ctx->params[0].data);
    if ((res == -EINVAL) || (res == -ERANGE)) {
        put_packet(gdb_ctx->s, "E22");
    } else if (res) {
        put_packet(gdb_ctx->s, "");
    }
}

static void handle_v_attach(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    GDBProcess *process;
    CPUState *cpu;
    char thread_id[16];

    pstrcpy(gdb_ctx->str_buf, sizeof(gdb_ctx->str_buf), "E22");
    if (!gdb_ctx->num_params) {
        goto cleanup;
    }

    process = gdb_get_process(gdb_ctx->s, gdb_ctx->params[0].val_ul);
    if (!process) {
        goto cleanup;
    }

    cpu = get_first_cpu_in_process(gdb_ctx->s, process);
    if (!cpu) {
        goto cleanup;
    }

    process->attached = true;
    gdb_ctx->s->g_cpu = cpu;
    gdb_ctx->s->c_cpu = cpu;

    gdb_fmt_thread_id(gdb_ctx->s, cpu, thread_id, sizeof(thread_id));
    snprintf(gdb_ctx->str_buf, sizeof(gdb_ctx->str_buf), "T%02xthread:%s;",
             GDB_SIGNAL_TRAP, thread_id);
cleanup:
    put_packet(gdb_ctx->s, gdb_ctx->str_buf);
}

static void handle_v_kill(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    /* Kill the target */
    put_packet(gdb_ctx->s, "OK");
    error_report("QEMU: Terminated via GDBstub");
    exit(0);
}

static GdbCmdParseEntry gdb_v_commands_table[] = {
    /* Order is important if has same prefix */
    {
        .handler = handle_v_cont_query,
        .cmd = "Cont?",
        .cmd_startswith = 1
    },
    {
        .handler = handle_v_cont,
        .cmd = "Cont",
        .cmd_startswith = 1,
        .schema = "s0"
    },
    {
        .handler = handle_v_attach,
        .cmd = "Attach;",
        .cmd_startswith = 1,
        .schema = "l0"
    },
    {
        .handler = handle_v_kill,
        .cmd = "Kill;",
        .cmd_startswith = 1
    },
};

static void handle_v_commands(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    if (!gdb_ctx->num_params) {
        return;
    }

    if (process_string_cmd(gdb_ctx->s, NULL, gdb_ctx->params[0].data,
                           gdb_v_commands_table,
                           ARRAY_SIZE(gdb_v_commands_table))) {
        put_packet(gdb_ctx->s, "");
    }
}

static void handle_query_qemu_sstepbits(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    snprintf(gdb_ctx->str_buf, sizeof(gdb_ctx->str_buf),
             "ENABLE=%x,NOIRQ=%x,NOTIMER=%x", SSTEP_ENABLE,
             SSTEP_NOIRQ, SSTEP_NOTIMER);
    put_packet(gdb_ctx->s, gdb_ctx->str_buf);
}

static void handle_set_qemu_sstep(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    if (!gdb_ctx->num_params) {
        return;
    }

    sstep_flags = gdb_ctx->params[0].val_ul;
    put_packet(gdb_ctx->s, "OK");
}

static void handle_query_qemu_sstep(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    snprintf(gdb_ctx->str_buf, sizeof(gdb_ctx->str_buf), "0x%x", sstep_flags);
    put_packet(gdb_ctx->s, gdb_ctx->str_buf);
}

static void handle_query_curr_tid(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    CPUState *cpu;
    GDBProcess *process;
    char thread_id[16];

    /*
     * "Current thread" remains vague in the spec, so always return
     * the first thread of the current process (gdb returns the
     * first thread).
     */
    process = gdb_get_cpu_process(gdb_ctx->s, gdb_ctx->s->g_cpu);
    cpu = get_first_cpu_in_process(gdb_ctx->s, process);
    gdb_fmt_thread_id(gdb_ctx->s, cpu, thread_id, sizeof(thread_id));
    snprintf(gdb_ctx->str_buf, sizeof(gdb_ctx->str_buf), "QC%s", thread_id);
    put_packet(gdb_ctx->s, gdb_ctx->str_buf);
}

static void handle_query_threads(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    char thread_id[16];

    if (!gdb_ctx->s->query_cpu) {
        put_packet(gdb_ctx->s, "l");
        return;
    }

    gdb_fmt_thread_id(gdb_ctx->s, gdb_ctx->s->query_cpu, thread_id,
                      sizeof(thread_id));
    snprintf(gdb_ctx->str_buf, sizeof(gdb_ctx->str_buf), "m%s", thread_id);
    put_packet(gdb_ctx->s, gdb_ctx->str_buf);
    gdb_ctx->s->query_cpu =
        gdb_next_attached_cpu(gdb_ctx->s, gdb_ctx->s->query_cpu);
}

static void handle_query_first_threads(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    gdb_ctx->s->query_cpu = gdb_first_attached_cpu(gdb_ctx->s);
    handle_query_threads(gdb_ctx, user_ctx);
}

static void handle_query_thread_extra(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    CPUState *cpu;
    int len;

    if (!gdb_ctx->num_params ||
        gdb_ctx->params[0].thread_id.kind == GDB_READ_THREAD_ERR) {
        put_packet(gdb_ctx->s, "E22");
        return;
    }

    cpu = gdb_get_cpu(gdb_ctx->s, gdb_ctx->params[0].thread_id.pid,
                      gdb_ctx->params[0].thread_id.tid);
    if (!cpu) {
        return;
    }

    cpu_synchronize_state(cpu);

    if (gdb_ctx->s->multiprocess && (gdb_ctx->s->process_num > 1)) {
        /* Print the CPU model and name in multiprocess mode */
        ObjectClass *oc = object_get_class(OBJECT(cpu));
        const char *cpu_model = object_class_get_name(oc);
        char *cpu_name = object_get_canonical_path_component(OBJECT(cpu));
        len = snprintf((char *)gdb_ctx->mem_buf, sizeof(gdb_ctx->str_buf) / 2,
                       "%s %s [%s]", cpu_model, cpu_name,
                       cpu->halted ? "halted " : "running");
        g_free(cpu_name);
    } else {
        /* memtohex() doubles the required space */
        len = snprintf((char *)gdb_ctx->mem_buf, sizeof(gdb_ctx->str_buf) / 2,
                        "CPU#%d [%s]", cpu->cpu_index,
                        cpu->halted ? "halted " : "running");
    }
    trace_gdbstub_op_extra_info((char *)gdb_ctx->mem_buf);
    memtohex(gdb_ctx->str_buf, gdb_ctx->mem_buf, len);
    put_packet(gdb_ctx->s, gdb_ctx->str_buf);
}

#ifdef CONFIG_USER_ONLY
static void handle_query_offsets(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    TaskState *ts;

    ts = gdb_ctx->s->c_cpu->opaque;
    snprintf(gdb_ctx->str_buf, sizeof(gdb_ctx->str_buf),
             "Text=" TARGET_ABI_FMT_lx ";Data=" TARGET_ABI_FMT_lx
             ";Bss=" TARGET_ABI_FMT_lx,
             ts->info->code_offset,
             ts->info->data_offset,
             ts->info->data_offset);
    put_packet(gdb_ctx->s, gdb_ctx->str_buf);
}
#else
static void handle_query_rcmd(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    int len;

    if (!gdb_ctx->num_params) {
        put_packet(gdb_ctx->s, "E22");
        return;
    }

    len = strlen(gdb_ctx->params[0].data);
    if (len % 2) {
        put_packet(gdb_ctx->s, "E01");
        return;
    }

    len = len / 2;
    hextomem(gdb_ctx->mem_buf, gdb_ctx->params[0].data, len);
    gdb_ctx->mem_buf[len++] = 0;
    qemu_chr_be_write(gdb_ctx->s->mon_chr, gdb_ctx->mem_buf, len);
    put_packet(gdb_ctx->s, "OK");

}
#endif

static void handle_query_supported(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    CPUClass *cc;

    snprintf(gdb_ctx->str_buf, sizeof(gdb_ctx->str_buf), "PacketSize=%x",
             MAX_PACKET_LENGTH);
    cc = CPU_GET_CLASS(first_cpu);
    if (cc->gdb_core_xml_file) {
        pstrcat(gdb_ctx->str_buf, sizeof(gdb_ctx->str_buf),
                ";qXfer:features:read+");
    }

    if (gdb_ctx->num_params &&
        strstr(gdb_ctx->params[0].data, "multiprocess+")) {
        gdb_ctx->s->multiprocess = true;
    }

    pstrcat(gdb_ctx->str_buf, sizeof(gdb_ctx->str_buf), ";multiprocess+");
    put_packet(gdb_ctx->s, gdb_ctx->str_buf);
}

static void handle_query_xfer_features(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    GDBProcess *process;
    CPUClass *cc;
    unsigned long len, total_len, addr;
    const char *xml;
    const char *p;

    if (gdb_ctx->num_params < 3) {
        put_packet(gdb_ctx->s, "E22");
        return;
    }

    process = gdb_get_cpu_process(gdb_ctx->s, gdb_ctx->s->g_cpu);
    cc = CPU_GET_CLASS(gdb_ctx->s->g_cpu);
    if (!cc->gdb_core_xml_file) {
        put_packet(gdb_ctx->s, "");
        return;
    }

    gdb_has_xml = true;
    p = gdb_ctx->params[0].data;
    xml = get_feature_xml(gdb_ctx->s, p, &p, process);
    if (!xml) {
        put_packet(gdb_ctx->s, "E00");
        return;
    }

    addr = gdb_ctx->params[1].val_ul;
    len = gdb_ctx->params[2].val_ul;
    total_len = strlen(xml);
    if (addr > total_len) {
        put_packet(gdb_ctx->s, "E00");
        return;
    }

    if (len > (MAX_PACKET_LENGTH - 5) / 2) {
        len = (MAX_PACKET_LENGTH - 5) / 2;
    }

    if (len < total_len - addr) {
        gdb_ctx->str_buf[0] = 'm';
        len = memtox(gdb_ctx->str_buf + 1, xml + addr, len);
    } else {
        gdb_ctx->str_buf[0] = 'l';
        len = memtox(gdb_ctx->str_buf + 1, xml + addr, total_len - addr);
    }

    put_packet_binary(gdb_ctx->s, gdb_ctx->str_buf, len + 1, true);
}

static void handle_query_attached(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    put_packet(gdb_ctx->s, GDB_ATTACHED);
}

static void handle_query_qemu_supported(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    snprintf(gdb_ctx->str_buf, sizeof(gdb_ctx->str_buf), "sstepbits;sstep");
#ifndef CONFIG_USER_ONLY
    pstrcat(gdb_ctx->str_buf, sizeof(gdb_ctx->str_buf), ";PhyMemMode");
#endif
    put_packet(gdb_ctx->s, gdb_ctx->str_buf);
}

#ifndef CONFIG_USER_ONLY
static void handle_query_qemu_phy_mem_mode(GdbCmdContext *gdb_ctx,
                                           void *user_ctx)
{
    snprintf(gdb_ctx->str_buf, sizeof(gdb_ctx->str_buf), "%d", phy_memory_mode);
    put_packet(gdb_ctx->s, gdb_ctx->str_buf);
}

static void handle_set_qemu_phy_mem_mode(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    if (!gdb_ctx->num_params) {
        put_packet(gdb_ctx->s, "E22");
        return;
    }

    if (!gdb_ctx->params[0].val_ul) {
        phy_memory_mode = 0;
    } else {
        phy_memory_mode = 1;
    }
    put_packet(gdb_ctx->s, "OK");
}
#endif

static GdbCmdParseEntry gdb_gen_query_set_common_table[] = {
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
        .cmd_startswith = 1,
        .schema = "l0"
    },
};

static GdbCmdParseEntry gdb_gen_query_table[] = {
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
        .cmd_startswith = 1,
        .schema = "t0"
    },
#ifdef CONFIG_USER_ONLY
    {
        .handler = handle_query_offsets,
        .cmd = "Offsets",
    },
#else
    {
        .handler = handle_query_rcmd,
        .cmd = "Rcmd,",
        .cmd_startswith = 1,
        .schema = "s0"
    },
#endif
    {
        .handler = handle_query_supported,
        .cmd = "Supported:",
        .cmd_startswith = 1,
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
        .cmd_startswith = 1,
        .schema = "s:l,l0"
    },
    {
        .handler = handle_query_attached,
        .cmd = "Attached:",
        .cmd_startswith = 1
    },
    {
        .handler = handle_query_attached,
        .cmd = "Attached",
    },
    {
        .handler = handle_query_qemu_supported,
        .cmd = "qemu.Supported",
    },
#ifndef CONFIG_USER_ONLY
    {
        .handler = handle_query_qemu_phy_mem_mode,
        .cmd = "qemu.PhyMemMode",
    },
#endif
};

static GdbCmdParseEntry gdb_gen_set_table[] = {
    /* Order is important if has same prefix */
    {
        .handler = handle_set_qemu_sstep,
        .cmd = "qemu.sstep:",
        .cmd_startswith = 1,
        .schema = "l0"
    },
#ifndef CONFIG_USER_ONLY
    {
        .handler = handle_set_qemu_phy_mem_mode,
        .cmd = "qemu.PhyMemMode:",
        .cmd_startswith = 1,
        .schema = "l0"
    },
#endif
};

static void handle_gen_query(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    if (!gdb_ctx->num_params) {
        return;
    }

    if (!process_string_cmd(gdb_ctx->s, NULL, gdb_ctx->params[0].data,
                            gdb_gen_query_set_common_table,
                            ARRAY_SIZE(gdb_gen_query_set_common_table))) {
        return;
    }

    if (process_string_cmd(gdb_ctx->s, NULL, gdb_ctx->params[0].data,
                           gdb_gen_query_table,
                           ARRAY_SIZE(gdb_gen_query_table))) {
        put_packet(gdb_ctx->s, "");
    }
}

static void handle_gen_set(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    if (!gdb_ctx->num_params) {
        return;
    }

    if (!process_string_cmd(gdb_ctx->s, NULL, gdb_ctx->params[0].data,
                            gdb_gen_query_set_common_table,
                            ARRAY_SIZE(gdb_gen_query_set_common_table))) {
        return;
    }

    if (process_string_cmd(gdb_ctx->s, NULL, gdb_ctx->params[0].data,
                           gdb_gen_set_table,
                           ARRAY_SIZE(gdb_gen_set_table))) {
        put_packet(gdb_ctx->s, "");
    }
}

static void handle_target_halt(GdbCmdContext *gdb_ctx, void *user_ctx)
{
    char thread_id[16];

    gdb_fmt_thread_id(gdb_ctx->s, gdb_ctx->s->c_cpu, thread_id,
                      sizeof(thread_id));
    snprintf(gdb_ctx->str_buf, sizeof(gdb_ctx->str_buf), "T%02xthread:%s;",
             GDB_SIGNAL_TRAP, thread_id);
    put_packet(gdb_ctx->s, gdb_ctx->str_buf);
    /*
     * Remove all the breakpoints when this query is issued,
     * because gdb is doing an initial connect and the state
     * should be cleaned up.
     */
    gdb_breakpoint_remove_all();
}

static int gdb_handle_packet(GDBState *s, const char *line_buf)
{
    const GdbCmdParseEntry *cmd_parser = NULL;

    trace_gdbstub_io_command(line_buf);

    switch (line_buf[0]) {
    case '!':
        put_packet(s, "OK");
        break;
    case '?':
        {
            static const GdbCmdParseEntry target_halted_cmd_desc = {
                .handler = handle_target_halt,
                .cmd = "?",
                .cmd_startswith = 1
            };
            cmd_parser = &target_halted_cmd_desc;
        }
        break;
    case 'c':
        {
            static const GdbCmdParseEntry continue_cmd_desc = {
                .handler = handle_continue,
                .cmd = "c",
                .cmd_startswith = 1,
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
                .cmd_startswith = 1,
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
                .cmd_startswith = 1,
                .schema = "s0"
            };
            cmd_parser = &v_cmd_desc;
        }
        break;
    case 'k':
        /* Kill the target */
        error_report("QEMU: Terminated via GDBstub");
        exit(0);
    case 'D':
        {
            static const GdbCmdParseEntry detach_cmd_desc = {
                .handler = handle_detach,
                .cmd = "D",
                .cmd_startswith = 1,
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
                .cmd_startswith = 1,
                .schema = "L0"
            };
            cmd_parser = &step_cmd_desc;
        }
        break;
    case 'F':
        {
            static const GdbCmdParseEntry file_io_cmd_desc = {
                .handler = handle_file_io,
                .cmd = "F",
                .cmd_startswith = 1,
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
                .cmd_startswith = 1
            };
            cmd_parser = &read_all_regs_cmd_desc;
        }
        break;
    case 'G':
        {
            static const GdbCmdParseEntry write_all_regs_cmd_desc = {
                .handler = handle_write_all_regs,
                .cmd = "G",
                .cmd_startswith = 1,
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
                .cmd_startswith = 1,
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
                .cmd_startswith = 1,
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
                .cmd_startswith = 1,
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
                .cmd_startswith = 1,
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
                .cmd_startswith = 1,
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
                .cmd_startswith = 1,
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
                .cmd_startswith = 1,
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
                .cmd_startswith = 1,
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
                .cmd_startswith = 1,
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
                .cmd_startswith = 1,
                .schema = "s0"
            };
            cmd_parser = &gen_set_cmd_desc;
        }
        break;
    default:
        /* put empty packet */
        put_packet(s, "");
        break;
    }

    if (cmd_parser) {
        run_cmd_parser(s, line_buf, cmd_parser);
    }

    return RS_IDLE;
}

void gdb_set_stop_cpu(CPUState *cpu)
{
    GDBProcess *p = gdb_get_cpu_process(gdbserver_state, cpu);

    if (!p->attached) {
        /*
         * Having a stop CPU corresponding to a process that is not attached
         * confuses GDB. So we ignore the request.
         */
        return;
    }

    gdbserver_state->c_cpu = cpu;
    gdbserver_state->g_cpu = cpu;
}

#ifndef CONFIG_USER_ONLY
static void gdb_vm_state_change(void *opaque, int running, RunState state)
{
    GDBState *s = gdbserver_state;
    CPUState *cpu = s->c_cpu;
    char buf[256];
    char thread_id[16];
    const char *type;
    int ret;

    if (running || s->state == RS_INACTIVE) {
        return;
    }
    /* Is there a GDB syscall waiting to be sent?  */
    if (s->current_syscall_cb) {
        put_packet(s, s->syscall_buf);
        return;
    }

    if (cpu == NULL) {
        /* No process attached */
        return;
    }

    gdb_fmt_thread_id(s, cpu, thread_id, sizeof(thread_id));

    switch (state) {
    case RUN_STATE_DEBUG:
        if (cpu->watchpoint_hit) {
            switch (cpu->watchpoint_hit->flags & BP_MEM_ACCESS) {
            case BP_MEM_READ:
                type = "r";
                break;
            case BP_MEM_ACCESS:
                type = "a";
                break;
            default:
                type = "";
                break;
            }
            trace_gdbstub_hit_watchpoint(type, cpu_gdb_index(cpu),
                    (target_ulong)cpu->watchpoint_hit->vaddr);
            snprintf(buf, sizeof(buf),
                     "T%02xthread:%s;%swatch:" TARGET_FMT_lx ";",
                     GDB_SIGNAL_TRAP, thread_id, type,
                     (target_ulong)cpu->watchpoint_hit->vaddr);
            cpu->watchpoint_hit = NULL;
            goto send_packet;
        } else {
            trace_gdbstub_hit_break();
        }
        tb_flush(cpu);
        ret = GDB_SIGNAL_TRAP;
        break;
    case RUN_STATE_PAUSED:
        trace_gdbstub_hit_paused();
        ret = GDB_SIGNAL_INT;
        break;
    case RUN_STATE_SHUTDOWN:
        trace_gdbstub_hit_shutdown();
        ret = GDB_SIGNAL_QUIT;
        break;
    case RUN_STATE_IO_ERROR:
        trace_gdbstub_hit_io_error();
        ret = GDB_SIGNAL_IO;
        break;
    case RUN_STATE_WATCHDOG:
        trace_gdbstub_hit_watchdog();
        ret = GDB_SIGNAL_ALRM;
        break;
    case RUN_STATE_INTERNAL_ERROR:
        trace_gdbstub_hit_internal_error();
        ret = GDB_SIGNAL_ABRT;
        break;
    case RUN_STATE_SAVE_VM:
    case RUN_STATE_RESTORE_VM:
        return;
    case RUN_STATE_FINISH_MIGRATE:
        ret = GDB_SIGNAL_XCPU;
        break;
    default:
        trace_gdbstub_hit_unknown(state);
        ret = GDB_SIGNAL_UNKNOWN;
        break;
    }
    gdb_set_stop_cpu(cpu);
    snprintf(buf, sizeof(buf), "T%02xthread:%s;", ret, thread_id);

send_packet:
    put_packet(s, buf);

    /* disable single step if it was enabled */
    cpu_single_step(cpu, 0);
}
#endif

/* Send a gdb syscall request.
   This accepts limited printf-style format specifiers, specifically:
    %x  - target_ulong argument printed in hex.
    %lx - 64-bit argument printed in hex.
    %s  - string pointer (target_ulong) and length (int) pair.  */
void gdb_do_syscallv(gdb_syscall_complete_cb cb, const char *fmt, va_list va)
{
    char *p;
    char *p_end;
    target_ulong addr;
    uint64_t i64;
    GDBState *s;

    s = gdbserver_state;
    if (!s)
        return;
    s->current_syscall_cb = cb;
#ifndef CONFIG_USER_ONLY
    vm_stop(RUN_STATE_DEBUG);
#endif
    p = s->syscall_buf;
    p_end = &s->syscall_buf[sizeof(s->syscall_buf)];
    *(p++) = 'F';
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt++) {
            case 'x':
                addr = va_arg(va, target_ulong);
                p += snprintf(p, p_end - p, TARGET_FMT_lx, addr);
                break;
            case 'l':
                if (*(fmt++) != 'x')
                    goto bad_format;
                i64 = va_arg(va, uint64_t);
                p += snprintf(p, p_end - p, "%" PRIx64, i64);
                break;
            case 's':
                addr = va_arg(va, target_ulong);
                p += snprintf(p, p_end - p, TARGET_FMT_lx "/%x",
                              addr, va_arg(va, int));
                break;
            default:
            bad_format:
                error_report("gdbstub: Bad syscall format string '%s'",
                             fmt - 1);
                break;
            }
        } else {
            *(p++) = *(fmt++);
        }
    }
    *p = 0;
#ifdef CONFIG_USER_ONLY
    put_packet(s, s->syscall_buf);
    /* Return control to gdb for it to process the syscall request.
     * Since the protocol requires that gdb hands control back to us
     * using a "here are the results" F packet, we don't need to check
     * gdb_handlesig's return value (which is the signal to deliver if
     * execution was resumed via a continue packet).
     */
    gdb_handlesig(s->c_cpu, 0);
#else
    /* In this case wait to send the syscall packet until notification that
       the CPU has stopped.  This must be done because if the packet is sent
       now the reply from the syscall request could be received while the CPU
       is still in the running state, which can cause packets to be dropped
       and state transition 'T' packets to be sent while the syscall is still
       being processed.  */
    qemu_cpu_kick(s->c_cpu);
#endif
}

void gdb_do_syscall(gdb_syscall_complete_cb cb, const char *fmt, ...)
{
    va_list va;

    va_start(va, fmt);
    gdb_do_syscallv(cb, fmt, va);
    va_end(va);
}

static void gdb_read_byte(GDBState *s, uint8_t ch)
{
    uint8_t reply;

#ifndef CONFIG_USER_ONLY
    if (s->last_packet_len) {
        /* Waiting for a response to the last packet.  If we see the start
           of a new command then abandon the previous response.  */
        if (ch == '-') {
            trace_gdbstub_err_got_nack();
            put_buffer(s, (uint8_t *)s->last_packet, s->last_packet_len);
        } else if (ch == '+') {
            trace_gdbstub_io_got_ack();
        } else {
            trace_gdbstub_io_got_unexpected(ch);
        }

        if (ch == '+' || ch == '$')
            s->last_packet_len = 0;
        if (ch != '$')
            return;
    }
    if (runstate_is_running()) {
        /* when the CPU is running, we cannot do anything except stop
           it when receiving a char */
        vm_stop(RUN_STATE_PAUSED);
    } else
#endif
    {
        switch(s->state) {
        case RS_IDLE:
            if (ch == '$') {
                /* start of command packet */
                s->line_buf_index = 0;
                s->line_sum = 0;
                s->state = RS_GETLINE;
            } else {
                trace_gdbstub_err_garbage(ch);
            }
            break;
        case RS_GETLINE:
            if (ch == '}') {
                /* start escape sequence */
                s->state = RS_GETLINE_ESC;
                s->line_sum += ch;
            } else if (ch == '*') {
                /* start run length encoding sequence */
                s->state = RS_GETLINE_RLE;
                s->line_sum += ch;
            } else if (ch == '#') {
                /* end of command, start of checksum*/
                s->state = RS_CHKSUM1;
            } else if (s->line_buf_index >= sizeof(s->line_buf) - 1) {
                trace_gdbstub_err_overrun();
                s->state = RS_IDLE;
            } else {
                /* unescaped command character */
                s->line_buf[s->line_buf_index++] = ch;
                s->line_sum += ch;
            }
            break;
        case RS_GETLINE_ESC:
            if (ch == '#') {
                /* unexpected end of command in escape sequence */
                s->state = RS_CHKSUM1;
            } else if (s->line_buf_index >= sizeof(s->line_buf) - 1) {
                /* command buffer overrun */
                trace_gdbstub_err_overrun();
                s->state = RS_IDLE;
            } else {
                /* parse escaped character and leave escape state */
                s->line_buf[s->line_buf_index++] = ch ^ 0x20;
                s->line_sum += ch;
                s->state = RS_GETLINE;
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
                s->state = RS_GETLINE;
            } else {
                /* decode repeat length */
                int repeat = ch - ' ' + 3;
                if (s->line_buf_index + repeat >= sizeof(s->line_buf) - 1) {
                    /* that many repeats would overrun the command buffer */
                    trace_gdbstub_err_overrun();
                    s->state = RS_IDLE;
                } else if (s->line_buf_index < 1) {
                    /* got a repeat but we have nothing to repeat */
                    trace_gdbstub_err_invalid_rle();
                    s->state = RS_GETLINE;
                } else {
                    /* repeat the last character */
                    memset(s->line_buf + s->line_buf_index,
                           s->line_buf[s->line_buf_index - 1], repeat);
                    s->line_buf_index += repeat;
                    s->line_sum += ch;
                    s->state = RS_GETLINE;
                }
            }
            break;
        case RS_CHKSUM1:
            /* get high hex digit of checksum */
            if (!isxdigit(ch)) {
                trace_gdbstub_err_checksum_invalid(ch);
                s->state = RS_GETLINE;
                break;
            }
            s->line_buf[s->line_buf_index] = '\0';
            s->line_csum = fromhex(ch) << 4;
            s->state = RS_CHKSUM2;
            break;
        case RS_CHKSUM2:
            /* get low hex digit of checksum */
            if (!isxdigit(ch)) {
                trace_gdbstub_err_checksum_invalid(ch);
                s->state = RS_GETLINE;
                break;
            }
            s->line_csum |= fromhex(ch);

            if (s->line_csum != (s->line_sum & 0xff)) {
                trace_gdbstub_err_checksum_incorrect(s->line_sum, s->line_csum);
                /* send NAK reply */
                reply = '-';
                put_buffer(s, &reply, 1);
                s->state = RS_IDLE;
            } else {
                /* send ACK reply */
                reply = '+';
                put_buffer(s, &reply, 1);
                s->state = gdb_handle_packet(s, s->line_buf);
            }
            break;
        default:
            abort();
        }
    }
}

/* Tell the remote gdb that the process has exited.  */
void gdb_exit(CPUArchState *env, int code)
{
  GDBState *s;
  char buf[4];

  s = gdbserver_state;
  if (!s) {
      return;
  }
#ifdef CONFIG_USER_ONLY
  if (gdbserver_fd < 0 || s->fd < 0) {
      return;
  }
#endif

  trace_gdbstub_op_exiting((uint8_t)code);

  snprintf(buf, sizeof(buf), "W%02x", (uint8_t)code);
  put_packet(s, buf);

#ifndef CONFIG_USER_ONLY
  qemu_chr_fe_deinit(&s->chr, true);
#endif
}

/*
 * Create the process that will contain all the "orphan" CPUs (that are not
 * part of a CPU cluster). Note that if this process contains no CPUs, it won't
 * be attachable and thus will be invisible to the user.
 */
static void create_default_process(GDBState *s)
{
    GDBProcess *process;
    int max_pid = 0;

    if (s->process_num) {
        max_pid = s->processes[s->process_num - 1].pid;
    }

    s->processes = g_renew(GDBProcess, s->processes, ++s->process_num);
    process = &s->processes[s->process_num - 1];

    /* We need an available PID slot for this process */
    assert(max_pid < UINT32_MAX);

    process->pid = max_pid + 1;
    process->attached = false;
    process->target_xml[0] = '\0';
}

#ifdef CONFIG_USER_ONLY
int
gdb_handlesig(CPUState *cpu, int sig)
{
    GDBState *s;
    char buf[256];
    int n;

    s = gdbserver_state;
    if (gdbserver_fd < 0 || s->fd < 0) {
        return sig;
    }

    /* disable single step if it was enabled */
    cpu_single_step(cpu, 0);
    tb_flush(cpu);

    if (sig != 0) {
        snprintf(buf, sizeof(buf), "S%02x", target_signal_to_gdb(sig));
        put_packet(s, buf);
    }
    /* put_packet() might have detected that the peer terminated the
       connection.  */
    if (s->fd < 0) {
        return sig;
    }

    sig = 0;
    s->state = RS_IDLE;
    s->running_state = 0;
    while (s->running_state == 0) {
        n = read(s->fd, buf, 256);
        if (n > 0) {
            int i;

            for (i = 0; i < n; i++) {
                gdb_read_byte(s, buf[i]);
            }
        } else {
            /* XXX: Connection closed.  Should probably wait for another
               connection before continuing.  */
            if (n == 0) {
                close(s->fd);
            }
            s->fd = -1;
            return sig;
        }
    }
    sig = s->signal;
    s->signal = 0;
    return sig;
}

/* Tell the remote gdb that the process has exited due to SIG.  */
void gdb_signalled(CPUArchState *env, int sig)
{
    GDBState *s;
    char buf[4];

    s = gdbserver_state;
    if (gdbserver_fd < 0 || s->fd < 0) {
        return;
    }

    snprintf(buf, sizeof(buf), "X%02x", target_signal_to_gdb(sig));
    put_packet(s, buf);
}

static bool gdb_accept(void)
{
    GDBState *s;
    struct sockaddr_in sockaddr;
    socklen_t len;
    int fd;

    for(;;) {
        len = sizeof(sockaddr);
        fd = accept(gdbserver_fd, (struct sockaddr *)&sockaddr, &len);
        if (fd < 0 && errno != EINTR) {
            perror("accept");
            return false;
        } else if (fd >= 0) {
            qemu_set_cloexec(fd);
            break;
        }
    }

    /* set short latency */
    if (socket_set_nodelay(fd)) {
        perror("setsockopt");
        close(fd);
        return false;
    }

    s = g_malloc0(sizeof(GDBState));
    create_default_process(s);
    s->processes[0].attached = true;
    s->c_cpu = gdb_first_attached_cpu(s);
    s->g_cpu = s->c_cpu;
    s->fd = fd;
    gdb_has_xml = false;

    gdbserver_state = s;
    return true;
}

static int gdbserver_open(int port)
{
    struct sockaddr_in sockaddr;
    int fd, ret;

    fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    qemu_set_cloexec(fd);

    socket_set_fast_reuse(fd);

    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(port);
    sockaddr.sin_addr.s_addr = 0;
    ret = bind(fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
    if (ret < 0) {
        perror("bind");
        close(fd);
        return -1;
    }
    ret = listen(fd, 1);
    if (ret < 0) {
        perror("listen");
        close(fd);
        return -1;
    }
    return fd;
}

int gdbserver_start(int port)
{
    gdbserver_fd = gdbserver_open(port);
    if (gdbserver_fd < 0)
        return -1;
    /* accept connections */
    if (!gdb_accept()) {
        close(gdbserver_fd);
        gdbserver_fd = -1;
        return -1;
    }
    return 0;
}

/* Disable gdb stub for child processes.  */
void gdbserver_fork(CPUState *cpu)
{
    GDBState *s = gdbserver_state;

    if (gdbserver_fd < 0 || s->fd < 0) {
        return;
    }
    close(s->fd);
    s->fd = -1;
    cpu_breakpoint_remove_all(cpu, BP_GDB);
    cpu_watchpoint_remove_all(cpu, BP_GDB);
}
#else
static int gdb_chr_can_receive(void *opaque)
{
  /* We can handle an arbitrarily large amount of data.
   Pick the maximum packet size, which is as good as anything.  */
  return MAX_PACKET_LENGTH;
}

static void gdb_chr_receive(void *opaque, const uint8_t *buf, int size)
{
    int i;

    for (i = 0; i < size; i++) {
        gdb_read_byte(gdbserver_state, buf[i]);
    }
}

static void gdb_chr_event(void *opaque, int event)
{
    int i;
    GDBState *s = (GDBState *) opaque;

    switch (event) {
    case CHR_EVENT_OPENED:
        /* Start with first process attached, others detached */
        for (i = 0; i < s->process_num; i++) {
            s->processes[i].attached = !i;
        }

        s->c_cpu = gdb_first_attached_cpu(s);
        s->g_cpu = s->c_cpu;

        vm_stop(RUN_STATE_PAUSED);
        gdb_has_xml = false;
        break;
    default:
        break;
    }
}

static void gdb_monitor_output(GDBState *s, const char *msg, int len)
{
    char buf[MAX_PACKET_LENGTH];

    buf[0] = 'O';
    if (len > (MAX_PACKET_LENGTH/2) - 1)
        len = (MAX_PACKET_LENGTH/2) - 1;
    memtohex(buf + 1, (uint8_t *)msg, len);
    put_packet(s, buf);
}

static int gdb_monitor_write(Chardev *chr, const uint8_t *buf, int len)
{
    const char *p = (const char *)buf;
    int max_sz;

    max_sz = (sizeof(gdbserver_state->last_packet) - 2) / 2;
    for (;;) {
        if (len <= max_sz) {
            gdb_monitor_output(gdbserver_state, p, len);
            break;
        }
        gdb_monitor_output(gdbserver_state, p, max_sz);
        p += max_sz;
        len -= max_sz;
    }
    return len;
}

#ifndef _WIN32
static void gdb_sigterm_handler(int signal)
{
    if (runstate_is_running()) {
        vm_stop(RUN_STATE_PAUSED);
    }
}
#endif

static void gdb_monitor_open(Chardev *chr, ChardevBackend *backend,
                             bool *be_opened, Error **errp)
{
    *be_opened = false;
}

static void char_gdb_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->internal = true;
    cc->open = gdb_monitor_open;
    cc->chr_write = gdb_monitor_write;
}

#define TYPE_CHARDEV_GDB "chardev-gdb"

static const TypeInfo char_gdb_type_info = {
    .name = TYPE_CHARDEV_GDB,
    .parent = TYPE_CHARDEV,
    .class_init = char_gdb_class_init,
};

static int find_cpu_clusters(Object *child, void *opaque)
{
    if (object_dynamic_cast(child, TYPE_CPU_CLUSTER)) {
        GDBState *s = (GDBState *) opaque;
        CPUClusterState *cluster = CPU_CLUSTER(child);
        GDBProcess *process;

        s->processes = g_renew(GDBProcess, s->processes, ++s->process_num);

        process = &s->processes[s->process_num - 1];

        /*
         * GDB process IDs -1 and 0 are reserved. To avoid subtle errors at
         * runtime, we enforce here that the machine does not use a cluster ID
         * that would lead to PID 0.
         */
        assert(cluster->cluster_id != UINT32_MAX);
        process->pid = cluster->cluster_id + 1;
        process->attached = false;
        process->target_xml[0] = '\0';

        return 0;
    }

    return object_child_foreach(child, find_cpu_clusters, opaque);
}

static int pid_order(const void *a, const void *b)
{
    GDBProcess *pa = (GDBProcess *) a;
    GDBProcess *pb = (GDBProcess *) b;

    if (pa->pid < pb->pid) {
        return -1;
    } else if (pa->pid > pb->pid) {
        return 1;
    } else {
        return 0;
    }
}

static void create_processes(GDBState *s)
{
    object_child_foreach(object_get_root(), find_cpu_clusters, s);

    if (s->processes) {
        /* Sort by PID */
        qsort(s->processes, s->process_num, sizeof(s->processes[0]), pid_order);
    }

    create_default_process(s);
}

static void cleanup_processes(GDBState *s)
{
    g_free(s->processes);
    s->process_num = 0;
    s->processes = NULL;
}

int gdbserver_start(const char *device)
{
    trace_gdbstub_op_start(device);

    GDBState *s;
    char gdbstub_device_name[128];
    Chardev *chr = NULL;
    Chardev *mon_chr;

    if (!first_cpu) {
        error_report("gdbstub: meaningless to attach gdb to a "
                     "machine without any CPU.");
        return -1;
    }

    if (!device)
        return -1;
    if (strcmp(device, "none") != 0) {
        if (strstart(device, "tcp:", NULL)) {
            /* enforce required TCP attributes */
            snprintf(gdbstub_device_name, sizeof(gdbstub_device_name),
                     "%s,nowait,nodelay,server", device);
            device = gdbstub_device_name;
        }
#ifndef _WIN32
        else if (strcmp(device, "stdio") == 0) {
            struct sigaction act;

            memset(&act, 0, sizeof(act));
            act.sa_handler = gdb_sigterm_handler;
            sigaction(SIGINT, &act, NULL);
        }
#endif
        /*
         * FIXME: it's a bit weird to allow using a mux chardev here
         * and implicitly setup a monitor. We may want to break this.
         */
        chr = qemu_chr_new_noreplay("gdb", device, true, NULL);
        if (!chr)
            return -1;
    }

    s = gdbserver_state;
    if (!s) {
        s = g_malloc0(sizeof(GDBState));
        gdbserver_state = s;

        qemu_add_vm_change_state_handler(gdb_vm_state_change, NULL);

        /* Initialize a monitor terminal for gdb */
        mon_chr = qemu_chardev_new(NULL, TYPE_CHARDEV_GDB,
                                   NULL, NULL, &error_abort);
        monitor_init_hmp(mon_chr, false);
    } else {
        qemu_chr_fe_deinit(&s->chr, true);
        mon_chr = s->mon_chr;
        cleanup_processes(s);
        memset(s, 0, sizeof(GDBState));
        s->mon_chr = mon_chr;
    }

    create_processes(s);

    if (chr) {
        qemu_chr_fe_init(&s->chr, chr, &error_abort);
        qemu_chr_fe_set_handlers(&s->chr, gdb_chr_can_receive, gdb_chr_receive,
                                 gdb_chr_event, NULL, s, NULL, true);
    }
    s->state = chr ? RS_IDLE : RS_INACTIVE;
    s->mon_chr = mon_chr;
    s->current_syscall_cb = NULL;

    return 0;
}

void gdbserver_cleanup(void)
{
    if (gdbserver_state) {
        put_packet(gdbserver_state, "W00");
    }
}

static void register_types(void)
{
    type_register_static(&char_gdb_type_info);
}

type_init(register_types);
#endif
