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
#include "trace/trace-root.h"
#include "exec/gdbstub.h"
#ifdef CONFIG_USER_ONLY
#include "qemu.h"
#else
#include "monitor/monitor.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "hw/cpu/cluster.h"
#include "hw/boards.h"
#endif

#define MAX_PACKET_LENGTH 4096

#include "qemu/sockets.h"
#include "sysemu/hw_accel.h"
#include "sysemu/kvm.h"
#include "sysemu/runstate.h"
#include "semihosting/semihost.h"
#include "exec/exec-all.h"
#include "sysemu/replay.h"

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
    return ts ? ts->ts_tid : -1;
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
    gdb_get_reg_cb get_reg;
    gdb_set_reg_cb set_reg;
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
    bool init;       /* have we been initialised? */
    CPUState *c_cpu; /* current CPU for step/continue ops */
    CPUState *g_cpu; /* current CPU for other ops */
    CPUState *query_cpu; /* for q{f|s}ThreadInfo */
    enum RSState state; /* parsing state */
    char line_buf[MAX_PACKET_LENGTH];
    int line_buf_index;
    int line_sum; /* running checksum */
    int line_csum; /* checksum at the end of the packet */
    GByteArray *last_packet;
    int signal;
#ifdef CONFIG_USER_ONLY
    int fd;
    char *socket_path;
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
    GString *str_buf;
    GByteArray *mem_buf;
    int sstep_flags;
    int supported_sstep_flags;
} GDBState;

static GDBState gdbserver_state;

static void init_gdbserver_state(void)
{
    g_assert(!gdbserver_state.init);
    memset(&gdbserver_state, 0, sizeof(GDBState));
    gdbserver_state.init = true;
    gdbserver_state.str_buf = g_string_new(NULL);
    gdbserver_state.mem_buf = g_byte_array_sized_new(MAX_PACKET_LENGTH);
    gdbserver_state.last_packet = g_byte_array_sized_new(MAX_PACKET_LENGTH + 4);

    /*
     * In replay mode all events will come from the log and can't be
     * suppressed otherwise we would break determinism. However as those
     * events are tied to the number of executed instructions we won't see
     * them occurring every time we single step.
     */
    if (replay_mode != REPLAY_MODE_NONE) {
        gdbserver_state.supported_sstep_flags = SSTEP_ENABLE;
    } else if (kvm_enabled()) {
        gdbserver_state.supported_sstep_flags = kvm_get_supported_sstep_flags();
    } else {
        gdbserver_state.supported_sstep_flags =
            SSTEP_ENABLE | SSTEP_NOIRQ | SSTEP_NOTIMER;
    }

    /*
     * By default use no IRQs and no timers while single stepping so as to
     * make single stepping like an ICE HW step.
     */
    gdbserver_state.sstep_flags = SSTEP_ENABLE | SSTEP_NOIRQ | SSTEP_NOTIMER;
    gdbserver_state.sstep_flags &= gdbserver_state.supported_sstep_flags;

}

#ifndef CONFIG_USER_ONLY
static void reset_gdbserver_state(void)
{
    g_free(gdbserver_state.processes);
    gdbserver_state.processes = NULL;
    gdbserver_state.process_num = 0;
}
#endif

bool gdb_has_xml;

#ifdef CONFIG_USER_ONLY

static int get_char(void)
{
    uint8_t ch;
    int ret;

    for(;;) {
        ret = qemu_recv(gdbserver_state.fd, &ch, 1, 0);
        if (ret < 0) {
            if (errno == ECONNRESET)
                gdbserver_state.fd = -1;
            if (errno != EINTR)
                return -1;
        } else if (ret == 0) {
            close(gdbserver_state.fd);
            gdbserver_state.fd = -1;
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
        gdb_syscall_mode = gdbserver_state.init ?
            GDB_SYS_ENABLED : GDB_SYS_DISABLED;
    }
    return gdb_syscall_mode == GDB_SYS_ENABLED;
}

static bool stub_can_reverse(void)
{
#ifdef CONFIG_USER_ONLY
    return false;
#else
    return replay_mode == REPLAY_MODE_PLAY;
#endif
}

/* Resume execution.  */
static inline void gdb_continue(void)
{

#ifdef CONFIG_USER_ONLY
    gdbserver_state.running_state = 1;
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
static int gdb_continue_partial(char *newstates)
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
            cpu_single_step(cpu, gdbserver_state.sstep_flags);
        }
    }
    gdbserver_state.running_state = 1;
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
                cpu_single_step(cpu, gdbserver_state.sstep_flags);
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

static void put_buffer(const uint8_t *buf, int len)
{
#ifdef CONFIG_USER_ONLY
    int ret;

    while (len > 0) {
        ret = send(gdbserver_state.fd, buf, len, 0);
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
    qemu_chr_fe_write_all(&gdbserver_state.chr, buf, len);
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
static void memtohex(GString *buf, const uint8_t *mem, int len)
{
    int i, c;
    for(i = 0; i < len; i++) {
        c = mem[i];
        g_string_append_c(buf, tohex(c >> 4));
        g_string_append_c(buf, tohex(c & 0xf));
    }
    g_string_append_c(buf, '\0');
}

static void hextomem(GByteArray *mem, const char *buf, int len)
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
static int put_packet_binary(const char *buf, int len, bool dump)
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

        put_buffer(gdbserver_state.last_packet->data,
                   gdbserver_state.last_packet->len);

#ifdef CONFIG_USER_ONLY
        i = get_char();
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
static int put_packet(const char *buf)
{
    trace_gdbstub_io_reply(buf);

    return put_packet_binary(buf, strlen(buf), false);
}

static void put_strbuf(void)
{
    put_packet(gdbserver_state.str_buf->str);
}

/* Encode data using the encoding for 'x' packets.  */
static void memtox(GString *buf, const char *mem, int len)
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
    /* TODO: In user mode, we should use the task state PID */
    if (cpu->cluster_index == UNASSIGNED_CLUSTER_INDEX) {
        /* Return the default process' PID */
        int index = gdbserver_state.process_num - 1;
        return gdbserver_state.processes[index].pid;
    }
    return cpu->cluster_index + 1;
}

static GDBProcess *gdb_get_process(uint32_t pid)
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
        if (cpu_gdb_index(cpu) == thread_id) {
            return cpu;
        }
    }

    return NULL;
}

static CPUState *get_first_cpu_in_process(GDBProcess *process)
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
static CPUState *gdb_first_attached_cpu(void)
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

        return get_first_cpu_in_process(process);
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
    size_t len;
    int i;
    const char *name;
    CPUState *cpu = get_first_cpu_in_process(process);
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

static int gdb_read_register(CPUState *cpu, GByteArray *buf, int reg)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);
    CPUArchState *env = cpu->env_ptr;
    GDBRegisterState *r;

    if (reg < cc->gdb_num_core_regs) {
        return cc->gdb_read_register(cpu, buf, reg);
    }

    for (r = cpu->gdb_regs; r; r = r->next) {
        if (r->base_reg <= reg && reg < r->base_reg + r->num_regs) {
            return r->get_reg(env, buf, reg - r->base_reg);
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
                              gdb_get_reg_cb get_reg, gdb_set_reg_cb set_reg,
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
        return kvm_insert_breakpoint(gdbserver_state.c_cpu, addr, len, type);
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
        return kvm_remove_breakpoint(gdbserver_state.c_cpu, addr, len, type);
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

static void gdb_process_breakpoint_remove_all(GDBProcess *p)
{
    CPUState *cpu = get_first_cpu_in_process(p);

    while (cpu) {
        gdb_cpu_breakpoint_remove_all(cpu);
        cpu = gdb_next_cpu_in_process(cpu);
    }
}

static void gdb_breakpoint_remove_all(void)
{
    CPUState *cpu;

    if (kvm_enabled()) {
        kvm_remove_all_breakpoints(gdbserver_state.c_cpu);
        return;
    }

    CPU_FOREACH(cpu) {
        gdb_cpu_breakpoint_remove_all(cpu);
    }
}

static void gdb_set_cpu_pc(target_ulong pc)
{
    CPUState *cpu = gdbserver_state.c_cpu;

    cpu_synchronize_state(cpu);
    cpu_set_pc(cpu, pc);
}

static void gdb_append_thread_id(CPUState *cpu, GString *buf)
{
    if (gdbserver_state.multiprocess) {
        g_string_append_printf(buf, "p%02x.%02x",
                               gdb_get_cpu_pid(cpu), cpu_gdb_index(cpu));
    } else {
        g_string_append_printf(buf, "%02x", cpu_gdb_index(cpu));
    }
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
static int gdb_handle_vcont(const char *p)
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
            res = qemu_strtoul(p, &p, 16, &tmp);
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
            cpu = gdb_first_attached_cpu();
            while (cpu) {
                if (newstates[cpu->cpu_index] == 1) {
                    newstates[cpu->cpu_index] = cur_action;
                }

                cpu = gdb_next_attached_cpu(cpu);
            }
            break;

        case GDB_ALL_THREADS:
            process = gdb_get_process(pid);

            if (!process->attached) {
                res = -EINVAL;
                goto out;
            }

            cpu = get_first_cpu_in_process(process);
            while (cpu) {
                if (newstates[cpu->cpu_index] == 1) {
                    newstates[cpu->cpu_index] = cur_action;
                }

                cpu = gdb_next_cpu_in_process(cpu);
            }
            break;

        case GDB_ONE_THREAD:
            cpu = gdb_get_cpu(pid, tid);

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
    gdbserver_state.signal = signal;
    gdb_continue_partial(newstates);

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

#define get_param(p, i)    (&g_array_index(p, GdbCmdVariant, i))

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

typedef void (*GdbCmdHandler)(GArray *params, void *user_ctx);

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

static int process_string_cmd(void *user_ctx, const char *data,
                              const GdbCmdParseEntry *cmds, int num_cmds)
{
    int i;
    g_autoptr(GArray) params = g_array_new(false, true, sizeof(GdbCmdVariant));

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
            if (cmd_parse_params(&data[strlen(cmd->cmd)],
                                 cmd->schema, params)) {
                return -1;
            }
        }

        cmd->handler(params, user_ctx);
        return 0;
    }

    return -1;
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
    if (process_string_cmd(NULL, data, cmd, 1)) {
        put_packet("");
    }
}

static void handle_detach(GArray *params, void *user_ctx)
{
    GDBProcess *process;
    uint32_t pid = 1;

    if (gdbserver_state.multiprocess) {
        if (!params->len) {
            put_packet("E22");
            return;
        }

        pid = get_param(params, 0)->val_ul;
    }

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
        gdb_syscall_mode = GDB_SYS_DISABLED;
        gdb_continue();
    }
    put_packet("OK");
}

static void handle_thread_alive(GArray *params, void *user_ctx)
{
    CPUState *cpu;

    if (!params->len) {
        put_packet("E22");
        return;
    }

    if (get_param(params, 0)->thread_id.kind == GDB_READ_THREAD_ERR) {
        put_packet("E22");
        return;
    }

    cpu = gdb_get_cpu(get_param(params, 0)->thread_id.pid,
                      get_param(params, 0)->thread_id.tid);
    if (!cpu) {
        put_packet("E22");
        return;
    }

    put_packet("OK");
}

static void handle_continue(GArray *params, void *user_ctx)
{
    if (params->len) {
        gdb_set_cpu_pc(get_param(params, 0)->val_ull);
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
        signal = get_param(params, 0)->val_ul;
    }

    gdbserver_state.signal = gdb_signal_to_target(signal);
    if (gdbserver_state.signal == -1) {
        gdbserver_state.signal = 0;
    }
    gdb_continue();
}

static void handle_set_thread(GArray *params, void *user_ctx)
{
    CPUState *cpu;

    if (params->len != 2) {
        put_packet("E22");
        return;
    }

    if (get_param(params, 1)->thread_id.kind == GDB_READ_THREAD_ERR) {
        put_packet("E22");
        return;
    }

    if (get_param(params, 1)->thread_id.kind != GDB_ONE_THREAD) {
        put_packet("OK");
        return;
    }

    cpu = gdb_get_cpu(get_param(params, 1)->thread_id.pid,
                      get_param(params, 1)->thread_id.tid);
    if (!cpu) {
        put_packet("E22");
        return;
    }

    /*
     * Note: This command is deprecated and modern gdb's will be using the
     *       vCont command instead.
     */
    switch (get_param(params, 0)->opcode) {
    case 'c':
        gdbserver_state.c_cpu = cpu;
        put_packet("OK");
        break;
    case 'g':
        gdbserver_state.g_cpu = cpu;
        put_packet("OK");
        break;
    default:
        put_packet("E22");
        break;
    }
}

static void handle_insert_bp(GArray *params, void *user_ctx)
{
    int res;

    if (params->len != 3) {
        put_packet("E22");
        return;
    }

    res = gdb_breakpoint_insert(get_param(params, 0)->val_ul,
                                get_param(params, 1)->val_ull,
                                get_param(params, 2)->val_ull);
    if (res >= 0) {
        put_packet("OK");
        return;
    } else if (res == -ENOSYS) {
        put_packet("");
        return;
    }

    put_packet("E22");
}

static void handle_remove_bp(GArray *params, void *user_ctx)
{
    int res;

    if (params->len != 3) {
        put_packet("E22");
        return;
    }

    res = gdb_breakpoint_remove(get_param(params, 0)->val_ul,
                                get_param(params, 1)->val_ull,
                                get_param(params, 2)->val_ull);
    if (res >= 0) {
        put_packet("OK");
        return;
    } else if (res == -ENOSYS) {
        put_packet("");
        return;
    }

    put_packet("E22");
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

    if (!gdb_has_xml) {
        put_packet("");
        return;
    }

    if (params->len != 2) {
        put_packet("E22");
        return;
    }

    reg_size = strlen(get_param(params, 1)->data) / 2;
    hextomem(gdbserver_state.mem_buf, get_param(params, 1)->data, reg_size);
    gdb_write_register(gdbserver_state.g_cpu, gdbserver_state.mem_buf->data,
                       get_param(params, 0)->val_ull);
    put_packet("OK");
}

static void handle_get_reg(GArray *params, void *user_ctx)
{
    int reg_size;

    if (!gdb_has_xml) {
        put_packet("");
        return;
    }

    if (!params->len) {
        put_packet("E14");
        return;
    }

    reg_size = gdb_read_register(gdbserver_state.g_cpu,
                                 gdbserver_state.mem_buf,
                                 get_param(params, 0)->val_ull);
    if (!reg_size) {
        put_packet("E14");
        return;
    } else {
        g_byte_array_set_size(gdbserver_state.mem_buf, reg_size);
    }

    memtohex(gdbserver_state.str_buf, gdbserver_state.mem_buf->data, reg_size);
    put_strbuf();
}

static void handle_write_mem(GArray *params, void *user_ctx)
{
    if (params->len != 3) {
        put_packet("E22");
        return;
    }

    /* hextomem() reads 2*len bytes */
    if (get_param(params, 1)->val_ull >
        strlen(get_param(params, 2)->data) / 2) {
        put_packet("E22");
        return;
    }

    hextomem(gdbserver_state.mem_buf, get_param(params, 2)->data,
             get_param(params, 1)->val_ull);
    if (target_memory_rw_debug(gdbserver_state.g_cpu,
                               get_param(params, 0)->val_ull,
                               gdbserver_state.mem_buf->data,
                               gdbserver_state.mem_buf->len, true)) {
        put_packet("E14");
        return;
    }

    put_packet("OK");
}

static void handle_read_mem(GArray *params, void *user_ctx)
{
    if (params->len != 2) {
        put_packet("E22");
        return;
    }

    /* memtohex() doubles the required space */
    if (get_param(params, 1)->val_ull > MAX_PACKET_LENGTH / 2) {
        put_packet("E22");
        return;
    }

    g_byte_array_set_size(gdbserver_state.mem_buf,
                          get_param(params, 1)->val_ull);

    if (target_memory_rw_debug(gdbserver_state.g_cpu,
                               get_param(params, 0)->val_ull,
                               gdbserver_state.mem_buf->data,
                               gdbserver_state.mem_buf->len, false)) {
        put_packet("E14");
        return;
    }

    memtohex(gdbserver_state.str_buf, gdbserver_state.mem_buf->data,
             gdbserver_state.mem_buf->len);
    put_strbuf();
}

static void handle_write_all_regs(GArray *params, void *user_ctx)
{
    target_ulong addr, len;
    uint8_t *registers;
    int reg_size;

    if (!params->len) {
        return;
    }

    cpu_synchronize_state(gdbserver_state.g_cpu);
    len = strlen(get_param(params, 0)->data) / 2;
    hextomem(gdbserver_state.mem_buf, get_param(params, 0)->data, len);
    registers = gdbserver_state.mem_buf->data;
    for (addr = 0; addr < gdbserver_state.g_cpu->gdb_num_g_regs && len > 0;
         addr++) {
        reg_size = gdb_write_register(gdbserver_state.g_cpu, registers, addr);
        len -= reg_size;
        registers += reg_size;
    }
    put_packet("OK");
}

static void handle_read_all_regs(GArray *params, void *user_ctx)
{
    target_ulong addr, len;

    cpu_synchronize_state(gdbserver_state.g_cpu);
    g_byte_array_set_size(gdbserver_state.mem_buf, 0);
    len = 0;
    for (addr = 0; addr < gdbserver_state.g_cpu->gdb_num_g_regs; addr++) {
        len += gdb_read_register(gdbserver_state.g_cpu,
                                 gdbserver_state.mem_buf,
                                 addr);
    }
    g_assert(len == gdbserver_state.mem_buf->len);

    memtohex(gdbserver_state.str_buf, gdbserver_state.mem_buf->data, len);
    put_strbuf();
}

static void handle_file_io(GArray *params, void *user_ctx)
{
    if (params->len >= 1 && gdbserver_state.current_syscall_cb) {
        target_ulong ret, err;

        ret = (target_ulong)get_param(params, 0)->val_ull;
        if (params->len >= 2) {
            err = (target_ulong)get_param(params, 1)->val_ull;
        } else {
            err = 0;
        }
        gdbserver_state.current_syscall_cb(gdbserver_state.c_cpu, ret, err);
        gdbserver_state.current_syscall_cb = NULL;
    }

    if (params->len >= 3 && get_param(params, 2)->opcode == (uint8_t)'C') {
        put_packet("T02");
        return;
    }

    gdb_continue();
}

static void handle_step(GArray *params, void *user_ctx)
{
    if (params->len) {
        gdb_set_cpu_pc((target_ulong)get_param(params, 0)->val_ull);
    }

    cpu_single_step(gdbserver_state.c_cpu, gdbserver_state.sstep_flags);
    gdb_continue();
}

static void handle_backward(GArray *params, void *user_ctx)
{
    if (!stub_can_reverse()) {
        put_packet("E22");
    }
    if (params->len == 1) {
        switch (get_param(params, 0)->opcode) {
        case 's':
            if (replay_reverse_step()) {
                gdb_continue();
            } else {
                put_packet("E14");
            }
            return;
        case 'c':
            if (replay_reverse_continue()) {
                gdb_continue();
            } else {
                put_packet("E14");
            }
            return;
        }
    }

    /* Default invalid command */
    put_packet("");
}

static void handle_v_cont_query(GArray *params, void *user_ctx)
{
    put_packet("vCont;c;C;s;S");
}

static void handle_v_cont(GArray *params, void *user_ctx)
{
    int res;

    if (!params->len) {
        return;
    }

    res = gdb_handle_vcont(get_param(params, 0)->data);
    if ((res == -EINVAL) || (res == -ERANGE)) {
        put_packet("E22");
    } else if (res) {
        put_packet("");
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

    process = gdb_get_process(get_param(params, 0)->val_ul);
    if (!process) {
        goto cleanup;
    }

    cpu = get_first_cpu_in_process(process);
    if (!cpu) {
        goto cleanup;
    }

    process->attached = true;
    gdbserver_state.g_cpu = cpu;
    gdbserver_state.c_cpu = cpu;

    g_string_printf(gdbserver_state.str_buf, "T%02xthread:", GDB_SIGNAL_TRAP);
    gdb_append_thread_id(cpu, gdbserver_state.str_buf);
    g_string_append_c(gdbserver_state.str_buf, ';');
cleanup:
    put_strbuf();
}

static void handle_v_kill(GArray *params, void *user_ctx)
{
    /* Kill the target */
    put_packet("OK");
    error_report("QEMU: Terminated via GDBstub");
    gdb_exit(0);
    exit(0);
}

static const GdbCmdParseEntry gdb_v_commands_table[] = {
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

static void handle_v_commands(GArray *params, void *user_ctx)
{
    if (!params->len) {
        return;
    }

    if (process_string_cmd(NULL, get_param(params, 0)->data,
                           gdb_v_commands_table,
                           ARRAY_SIZE(gdb_v_commands_table))) {
        put_packet("");
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

    put_strbuf();
}

static void handle_set_qemu_sstep(GArray *params, void *user_ctx)
{
    int new_sstep_flags;

    if (!params->len) {
        return;
    }

    new_sstep_flags = get_param(params, 0)->val_ul;

    if (new_sstep_flags  & ~gdbserver_state.supported_sstep_flags) {
        put_packet("E22");
        return;
    }

    gdbserver_state.sstep_flags = new_sstep_flags;
    put_packet("OK");
}

static void handle_query_qemu_sstep(GArray *params, void *user_ctx)
{
    g_string_printf(gdbserver_state.str_buf, "0x%x",
                    gdbserver_state.sstep_flags);
    put_strbuf();
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
    cpu = get_first_cpu_in_process(process);
    g_string_assign(gdbserver_state.str_buf, "QC");
    gdb_append_thread_id(cpu, gdbserver_state.str_buf);
    put_strbuf();
}

static void handle_query_threads(GArray *params, void *user_ctx)
{
    if (!gdbserver_state.query_cpu) {
        put_packet("l");
        return;
    }

    g_string_assign(gdbserver_state.str_buf, "m");
    gdb_append_thread_id(gdbserver_state.query_cpu, gdbserver_state.str_buf);
    put_strbuf();
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
        get_param(params, 0)->thread_id.kind == GDB_READ_THREAD_ERR) {
        put_packet("E22");
        return;
    }

    cpu = gdb_get_cpu(get_param(params, 0)->thread_id.pid,
                      get_param(params, 0)->thread_id.tid);
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
    memtohex(gdbserver_state.str_buf, (uint8_t *)rs->str, rs->len);
    put_strbuf();
}

#ifdef CONFIG_USER_ONLY
static void handle_query_offsets(GArray *params, void *user_ctx)
{
    TaskState *ts;

    ts = gdbserver_state.c_cpu->opaque;
    g_string_printf(gdbserver_state.str_buf,
                    "Text=" TARGET_ABI_FMT_lx
                    ";Data=" TARGET_ABI_FMT_lx
                    ";Bss=" TARGET_ABI_FMT_lx,
                    ts->info->code_offset,
                    ts->info->data_offset,
                    ts->info->data_offset);
    put_strbuf();
}
#else
static void handle_query_rcmd(GArray *params, void *user_ctx)
{
    const guint8 zero = 0;
    int len;

    if (!params->len) {
        put_packet("E22");
        return;
    }

    len = strlen(get_param(params, 0)->data);
    if (len % 2) {
        put_packet("E01");
        return;
    }

    g_assert(gdbserver_state.mem_buf->len == 0);
    len = len / 2;
    hextomem(gdbserver_state.mem_buf, get_param(params, 0)->data, len);
    g_byte_array_append(gdbserver_state.mem_buf, &zero, 1);
    qemu_chr_be_write(gdbserver_state.mon_chr, gdbserver_state.mem_buf->data,
                      gdbserver_state.mem_buf->len);
    put_packet("OK");
}
#endif

static void handle_query_supported(GArray *params, void *user_ctx)
{
    CPUClass *cc;

    g_string_printf(gdbserver_state.str_buf, "PacketSize=%x", MAX_PACKET_LENGTH);
    cc = CPU_GET_CLASS(first_cpu);
    if (cc->gdb_core_xml_file) {
        g_string_append(gdbserver_state.str_buf, ";qXfer:features:read+");
    }

    if (stub_can_reverse()) {
        g_string_append(gdbserver_state.str_buf,
            ";ReverseStep+;ReverseContinue+");
    }

#ifdef CONFIG_USER_ONLY
    if (gdbserver_state.c_cpu->opaque) {
        g_string_append(gdbserver_state.str_buf, ";qXfer:auxv:read+");
    }
#endif

    if (params->len &&
        strstr(get_param(params, 0)->data, "multiprocess+")) {
        gdbserver_state.multiprocess = true;
    }

    g_string_append(gdbserver_state.str_buf, ";vContSupported+;multiprocess+");
    put_strbuf();
}

static void handle_query_xfer_features(GArray *params, void *user_ctx)
{
    GDBProcess *process;
    CPUClass *cc;
    unsigned long len, total_len, addr;
    const char *xml;
    const char *p;

    if (params->len < 3) {
        put_packet("E22");
        return;
    }

    process = gdb_get_cpu_process(gdbserver_state.g_cpu);
    cc = CPU_GET_CLASS(gdbserver_state.g_cpu);
    if (!cc->gdb_core_xml_file) {
        put_packet("");
        return;
    }

    gdb_has_xml = true;
    p = get_param(params, 0)->data;
    xml = get_feature_xml(p, &p, process);
    if (!xml) {
        put_packet("E00");
        return;
    }

    addr = get_param(params, 1)->val_ul;
    len = get_param(params, 2)->val_ul;
    total_len = strlen(xml);
    if (addr > total_len) {
        put_packet("E00");
        return;
    }

    if (len > (MAX_PACKET_LENGTH - 5) / 2) {
        len = (MAX_PACKET_LENGTH - 5) / 2;
    }

    if (len < total_len - addr) {
        g_string_assign(gdbserver_state.str_buf, "m");
        memtox(gdbserver_state.str_buf, xml + addr, len);
    } else {
        g_string_assign(gdbserver_state.str_buf, "l");
        memtox(gdbserver_state.str_buf, xml + addr, total_len - addr);
    }

    put_packet_binary(gdbserver_state.str_buf->str,
                      gdbserver_state.str_buf->len, true);
}

#if defined(CONFIG_USER_ONLY) && defined(CONFIG_LINUX_USER)
static void handle_query_xfer_auxv(GArray *params, void *user_ctx)
{
    TaskState *ts;
    unsigned long offset, len, saved_auxv, auxv_len;

    if (params->len < 2) {
        put_packet("E22");
        return;
    }

    offset = get_param(params, 0)->val_ul;
    len = get_param(params, 1)->val_ul;
    ts = gdbserver_state.c_cpu->opaque;
    saved_auxv = ts->info->saved_auxv;
    auxv_len = ts->info->auxv_len;

    if (offset >= auxv_len) {
        put_packet("E00");
        return;
    }

    if (len > (MAX_PACKET_LENGTH - 5) / 2) {
        len = (MAX_PACKET_LENGTH - 5) / 2;
    }

    if (len < auxv_len - offset) {
        g_string_assign(gdbserver_state.str_buf, "m");
    } else {
        g_string_assign(gdbserver_state.str_buf, "l");
        len = auxv_len - offset;
    }

    g_byte_array_set_size(gdbserver_state.mem_buf, len);
    if (target_memory_rw_debug(gdbserver_state.g_cpu, saved_auxv + offset,
                               gdbserver_state.mem_buf->data, len, false)) {
        put_packet("E14");
        return;
    }

    memtox(gdbserver_state.str_buf,
           (const char *)gdbserver_state.mem_buf->data, len);
    put_packet_binary(gdbserver_state.str_buf->str,
                      gdbserver_state.str_buf->len, true);
}
#endif

static void handle_query_attached(GArray *params, void *user_ctx)
{
    put_packet(GDB_ATTACHED);
}

static void handle_query_qemu_supported(GArray *params, void *user_ctx)
{
    g_string_printf(gdbserver_state.str_buf, "sstepbits;sstep");
#ifndef CONFIG_USER_ONLY
    g_string_append(gdbserver_state.str_buf, ";PhyMemMode");
#endif
    put_strbuf();
}

#ifndef CONFIG_USER_ONLY
static void handle_query_qemu_phy_mem_mode(GArray *params,
                                           void *user_ctx)
{
    g_string_printf(gdbserver_state.str_buf, "%d", phy_memory_mode);
    put_strbuf();
}

static void handle_set_qemu_phy_mem_mode(GArray *params, void *user_ctx)
{
    if (!params->len) {
        put_packet("E22");
        return;
    }

    if (!get_param(params, 0)->val_ul) {
        phy_memory_mode = 0;
    } else {
        phy_memory_mode = 1;
    }
    put_packet("OK");
}
#endif

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
        .cmd_startswith = 1,
        .schema = "l0"
    },
};

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
#if defined(CONFIG_USER_ONLY) && defined(CONFIG_LINUX_USER)
    {
        .handler = handle_query_xfer_auxv,
        .cmd = "Xfer:auxv:read::",
        .cmd_startswith = 1,
        .schema = "l,l0"
    },
#endif
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

static const GdbCmdParseEntry gdb_gen_set_table[] = {
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

static void handle_gen_query(GArray *params, void *user_ctx)
{
    if (!params->len) {
        return;
    }

    if (!process_string_cmd(NULL, get_param(params, 0)->data,
                            gdb_gen_query_set_common_table,
                            ARRAY_SIZE(gdb_gen_query_set_common_table))) {
        return;
    }

    if (process_string_cmd(NULL, get_param(params, 0)->data,
                           gdb_gen_query_table,
                           ARRAY_SIZE(gdb_gen_query_table))) {
        put_packet("");
    }
}

static void handle_gen_set(GArray *params, void *user_ctx)
{
    if (!params->len) {
        return;
    }

    if (!process_string_cmd(NULL, get_param(params, 0)->data,
                            gdb_gen_query_set_common_table,
                            ARRAY_SIZE(gdb_gen_query_set_common_table))) {
        return;
    }

    if (process_string_cmd(NULL, get_param(params, 0)->data,
                           gdb_gen_set_table,
                           ARRAY_SIZE(gdb_gen_set_table))) {
        put_packet("");
    }
}

static void handle_target_halt(GArray *params, void *user_ctx)
{
    g_string_printf(gdbserver_state.str_buf, "T%02xthread:", GDB_SIGNAL_TRAP);
    gdb_append_thread_id(gdbserver_state.c_cpu, gdbserver_state.str_buf);
    g_string_append_c(gdbserver_state.str_buf, ';');
    put_strbuf();
    /*
     * Remove all the breakpoints when this query is issued,
     * because gdb is doing an initial connect and the state
     * should be cleaned up.
     */
    gdb_breakpoint_remove_all();
}

static int gdb_handle_packet(const char *line_buf)
{
    const GdbCmdParseEntry *cmd_parser = NULL;

    trace_gdbstub_io_command(line_buf);

    switch (line_buf[0]) {
    case '!':
        put_packet("OK");
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
        gdb_exit(0);
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
    case 'b':
        {
            static const GdbCmdParseEntry backward_cmd_desc = {
                .handler = handle_backward,
                .cmd = "b",
                .cmd_startswith = 1,
                .schema = "o0"
            };
            cmd_parser = &backward_cmd_desc;
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
        put_packet("");
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

#ifndef CONFIG_USER_ONLY
static void gdb_vm_state_change(void *opaque, bool running, RunState state)
{
    CPUState *cpu = gdbserver_state.c_cpu;
    g_autoptr(GString) buf = g_string_new(NULL);
    g_autoptr(GString) tid = g_string_new(NULL);
    const char *type;
    int ret;

    if (running || gdbserver_state.state == RS_INACTIVE) {
        return;
    }
    /* Is there a GDB syscall waiting to be sent?  */
    if (gdbserver_state.current_syscall_cb) {
        put_packet(gdbserver_state.syscall_buf);
        return;
    }

    if (cpu == NULL) {
        /* No process attached */
        return;
    }

    gdb_append_thread_id(cpu, tid);

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
            g_string_printf(buf, "T%02xthread:%s;%swatch:" TARGET_FMT_lx ";",
                            GDB_SIGNAL_TRAP, tid->str, type,
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
    g_string_printf(buf, "T%02xthread:%s;", ret, tid->str);

send_packet:
    put_packet(buf->str);

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

    if (!gdbserver_state.init) {
        return;
    }

    gdbserver_state.current_syscall_cb = cb;
#ifndef CONFIG_USER_ONLY
    vm_stop(RUN_STATE_DEBUG);
#endif
    p = &gdbserver_state.syscall_buf[0];
    p_end = &gdbserver_state.syscall_buf[sizeof(gdbserver_state.syscall_buf)];
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
    put_packet(gdbserver_state.syscall_buf);
    /* Return control to gdb for it to process the syscall request.
     * Since the protocol requires that gdb hands control back to us
     * using a "here are the results" F packet, we don't need to check
     * gdb_handlesig's return value (which is the signal to deliver if
     * execution was resumed via a continue packet).
     */
    gdb_handlesig(gdbserver_state.c_cpu, 0);
#else
    /* In this case wait to send the syscall packet until notification that
       the CPU has stopped.  This must be done because if the packet is sent
       now the reply from the syscall request could be received while the CPU
       is still in the running state, which can cause packets to be dropped
       and state transition 'T' packets to be sent while the syscall is still
       being processed.  */
    qemu_cpu_kick(gdbserver_state.c_cpu);
#endif
}

void gdb_do_syscall(gdb_syscall_complete_cb cb, const char *fmt, ...)
{
    va_list va;

    va_start(va, fmt);
    gdb_do_syscallv(cb, fmt, va);
    va_end(va);
}

static void gdb_read_byte(uint8_t ch)
{
    uint8_t reply;

#ifndef CONFIG_USER_ONLY
    if (gdbserver_state.last_packet->len) {
        /* Waiting for a response to the last packet.  If we see the start
           of a new command then abandon the previous response.  */
        if (ch == '-') {
            trace_gdbstub_err_got_nack();
            put_buffer(gdbserver_state.last_packet->data,
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
        /* when the CPU is running, we cannot do anything except stop
           it when receiving a char */
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
                put_buffer(&reply, 1);
                gdbserver_state.state = RS_IDLE;
            } else {
                /* send ACK reply */
                reply = '+';
                put_buffer(&reply, 1);
                gdbserver_state.state = gdb_handle_packet(gdbserver_state.line_buf);
            }
            break;
        default:
            abort();
        }
    }
}

/* Tell the remote gdb that the process has exited.  */
void gdb_exit(int code)
{
  char buf[4];

  if (!gdbserver_state.init) {
      return;
  }
#ifdef CONFIG_USER_ONLY
  if (gdbserver_state.socket_path) {
      unlink(gdbserver_state.socket_path);
  }
  if (gdbserver_state.fd < 0) {
      return;
  }
#endif

  trace_gdbstub_op_exiting((uint8_t)code);

  snprintf(buf, sizeof(buf), "W%02x", (uint8_t)code);
  put_packet(buf);

#ifndef CONFIG_USER_ONLY
  qemu_chr_fe_deinit(&gdbserver_state.chr, true);
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

    if (gdbserver_state.process_num) {
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
    char buf[256];
    int n;

    if (!gdbserver_state.init || gdbserver_state.fd < 0) {
        return sig;
    }

    /* disable single step if it was enabled */
    cpu_single_step(cpu, 0);
    tb_flush(cpu);

    if (sig != 0) {
        gdb_set_stop_cpu(cpu);
        g_string_printf(gdbserver_state.str_buf,
                        "T%02xthread:", target_signal_to_gdb(sig));
        gdb_append_thread_id(cpu, gdbserver_state.str_buf);
        g_string_append_c(gdbserver_state.str_buf, ';');
        put_strbuf();
    }
    /* put_packet() might have detected that the peer terminated the
       connection.  */
    if (gdbserver_state.fd < 0) {
        return sig;
    }

    sig = 0;
    gdbserver_state.state = RS_IDLE;
    gdbserver_state.running_state = 0;
    while (gdbserver_state.running_state == 0) {
        n = read(gdbserver_state.fd, buf, 256);
        if (n > 0) {
            int i;

            for (i = 0; i < n; i++) {
                gdb_read_byte(buf[i]);
            }
        } else {
            /* XXX: Connection closed.  Should probably wait for another
               connection before continuing.  */
            if (n == 0) {
                close(gdbserver_state.fd);
            }
            gdbserver_state.fd = -1;
            return sig;
        }
    }
    sig = gdbserver_state.signal;
    gdbserver_state.signal = 0;
    return sig;
}

/* Tell the remote gdb that the process has exited due to SIG.  */
void gdb_signalled(CPUArchState *env, int sig)
{
    char buf[4];

    if (!gdbserver_state.init || gdbserver_state.fd < 0) {
        return;
    }

    snprintf(buf, sizeof(buf), "X%02x", target_signal_to_gdb(sig));
    put_packet(buf);
}

static void gdb_accept_init(int fd)
{
    init_gdbserver_state();
    create_default_process(&gdbserver_state);
    gdbserver_state.processes[0].attached = true;
    gdbserver_state.c_cpu = gdb_first_attached_cpu();
    gdbserver_state.g_cpu = gdbserver_state.c_cpu;
    gdbserver_state.fd = fd;
    gdb_has_xml = false;
}

static bool gdb_accept_socket(int gdb_fd)
{
    int fd;

    for(;;) {
        fd = accept(gdb_fd, NULL, NULL);
        if (fd < 0 && errno != EINTR) {
            perror("accept socket");
            return false;
        } else if (fd >= 0) {
            qemu_set_cloexec(fd);
            break;
        }
    }

    gdb_accept_init(fd);
    return true;
}

static int gdbserver_open_socket(const char *path)
{
    struct sockaddr_un sockaddr = {};
    int fd, ret;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("create socket");
        return -1;
    }

    sockaddr.sun_family = AF_UNIX;
    pstrcpy(sockaddr.sun_path, sizeof(sockaddr.sun_path) - 1, path);
    ret = bind(fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
    if (ret < 0) {
        perror("bind socket");
        close(fd);
        return -1;
    }
    ret = listen(fd, 1);
    if (ret < 0) {
        perror("listen socket");
        close(fd);
        return -1;
    }

    return fd;
}

static bool gdb_accept_tcp(int gdb_fd)
{
    struct sockaddr_in sockaddr = {};
    socklen_t len;
    int fd;

    for(;;) {
        len = sizeof(sockaddr);
        fd = accept(gdb_fd, (struct sockaddr *)&sockaddr, &len);
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

    gdb_accept_init(fd);
    return true;
}

static int gdbserver_open_port(int port)
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

int gdbserver_start(const char *port_or_path)
{
    int port = g_ascii_strtoull(port_or_path, NULL, 10);
    int gdb_fd;

    if (port > 0) {
        gdb_fd = gdbserver_open_port(port);
    } else {
        gdb_fd = gdbserver_open_socket(port_or_path);
    }

    if (gdb_fd < 0) {
        return -1;
    }

    if (port > 0 && gdb_accept_tcp(gdb_fd)) {
        return 0;
    } else if (gdb_accept_socket(gdb_fd)) {
        gdbserver_state.socket_path = g_strdup(port_or_path);
        return 0;
    }

    /* gone wrong */
    close(gdb_fd);
    return -1;
}

/* Disable gdb stub for child processes.  */
void gdbserver_fork(CPUState *cpu)
{
    if (!gdbserver_state.init || gdbserver_state.fd < 0) {
        return;
    }
    close(gdbserver_state.fd);
    gdbserver_state.fd = -1;
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
        gdb_read_byte(buf[i]);
    }
}

static void gdb_chr_event(void *opaque, QEMUChrEvent event)
{
    int i;
    GDBState *s = (GDBState *) opaque;

    switch (event) {
    case CHR_EVENT_OPENED:
        /* Start with first process attached, others detached */
        for (i = 0; i < s->process_num; i++) {
            s->processes[i].attached = !i;
        }

        s->c_cpu = gdb_first_attached_cpu();
        s->g_cpu = s->c_cpu;

        vm_stop(RUN_STATE_PAUSED);
        replay_gdb_attached();
        gdb_has_xml = false;
        break;
    default:
        break;
    }
}

static int gdb_monitor_write(Chardev *chr, const uint8_t *buf, int len)
{
    g_autoptr(GString) hex_buf = g_string_new("O");
    memtohex(hex_buf, buf, len);
    put_packet(hex_buf->str);
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

    if (gdbserver_state.processes) {
        /* Sort by PID */
        qsort(gdbserver_state.processes, gdbserver_state.process_num, sizeof(gdbserver_state.processes[0]), pid_order);
    }

    create_default_process(s);
}

int gdbserver_start(const char *device)
{
    trace_gdbstub_op_start(device);

    char gdbstub_device_name[128];
    Chardev *chr = NULL;
    Chardev *mon_chr;

    if (!first_cpu) {
        error_report("gdbstub: meaningless to attach gdb to a "
                     "machine without any CPU.");
        return -1;
    }

    if (kvm_enabled() && !kvm_supports_guest_debug()) {
        error_report("gdbstub: KVM doesn't support guest debugging");
        return -1;
    }

    if (!device)
        return -1;
    if (strcmp(device, "none") != 0) {
        if (strstart(device, "tcp:", NULL)) {
            /* enforce required TCP attributes */
            snprintf(gdbstub_device_name, sizeof(gdbstub_device_name),
                     "%s,wait=off,nodelay=on,server=on", device);
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

    if (!gdbserver_state.init) {
        init_gdbserver_state();

        qemu_add_vm_change_state_handler(gdb_vm_state_change, NULL);

        /* Initialize a monitor terminal for gdb */
        mon_chr = qemu_chardev_new(NULL, TYPE_CHARDEV_GDB,
                                   NULL, NULL, &error_abort);
        monitor_init_hmp(mon_chr, false, &error_abort);
    } else {
        qemu_chr_fe_deinit(&gdbserver_state.chr, true);
        mon_chr = gdbserver_state.mon_chr;
        reset_gdbserver_state();
    }

    create_processes(&gdbserver_state);

    if (chr) {
        qemu_chr_fe_init(&gdbserver_state.chr, chr, &error_abort);
        qemu_chr_fe_set_handlers(&gdbserver_state.chr, gdb_chr_can_receive,
                                 gdb_chr_receive, gdb_chr_event,
                                 NULL, &gdbserver_state, NULL, true);
    }
    gdbserver_state.state = chr ? RS_IDLE : RS_INACTIVE;
    gdbserver_state.mon_chr = mon_chr;
    gdbserver_state.current_syscall_cb = NULL;

    return 0;
}

static void register_types(void)
{
    type_register_static(&char_gdb_type_info);
}

type_init(register_types);
#endif
