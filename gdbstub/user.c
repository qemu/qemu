/*
 * gdbstub user-mode helper routines.
 *
 * We know for user-mode we are using TCG so we can call stuff directly.
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/cutils.h"
#include "qemu/sockets.h"
#include "qapi/error.h"
#include "exec/hwaddr.h"
#include "exec/gdbstub.h"
#include "gdbstub/commands.h"
#include "gdbstub/syscalls.h"
#include "gdbstub/user.h"
#include "gdbstub/enums.h"
#include "hw/core/cpu.h"
#include "user/signal.h"
#include "trace.h"
#include "internals.h"

#define GDB_NR_SYSCALLS 1024
typedef unsigned long GDBSyscallsMask[BITS_TO_LONGS(GDB_NR_SYSCALLS)];

/*
 * Forked child talks to its parent in order to let GDB enforce the
 * follow-fork-mode. This happens inside a start_exclusive() section, so that
 * the other threads, which may be forking too, do not interfere. The
 * implementation relies on GDB not sending $vCont until it has detached
 * either from the parent (follow-fork-mode child) or from the child
 * (follow-fork-mode parent).
 *
 * The parent and the child share the GDB socket; at any given time only one
 * of them is allowed to use it, as is reflected in the respective fork_state.
 * This is negotiated via the fork_sockets pair as a reaction to $Hg.
 *
 * Below is a short summary of the possible state transitions:
 *
 *     ENABLED                     : Terminal state.
 *     DISABLED                    : Terminal state.
 *     ACTIVE                      : Parent initial state.
 *     INACTIVE                    : Child initial state.
 *     ACTIVE       -> DEACTIVATING: On $Hg.
 *     ACTIVE       -> ENABLING    : On $D.
 *     ACTIVE       -> DISABLING   : On $D.
 *     ACTIVE       -> DISABLED    : On communication error.
 *     DEACTIVATING -> INACTIVE    : On gdb_read_byte() return.
 *     DEACTIVATING -> DISABLED    : On communication error.
 *     INACTIVE     -> ACTIVE      : On $Hg in the peer.
 *     INACTIVE     -> ENABLE      : On $D in the peer.
 *     INACTIVE     -> DISABLE     : On $D in the peer.
 *     INACTIVE     -> DISABLED    : On communication error.
 *     ENABLING     -> ENABLED     : On gdb_read_byte() return.
 *     ENABLING     -> DISABLED    : On communication error.
 *     DISABLING    -> DISABLED    : On gdb_read_byte() return.
 */
enum GDBForkState {
    /* Fully owning the GDB socket. */
    GDB_FORK_ENABLED,
    /* Working with the GDB socket; the peer is inactive. */
    GDB_FORK_ACTIVE,
    /* Handing off the GDB socket to the peer. */
    GDB_FORK_DEACTIVATING,
    /* The peer is working with the GDB socket. */
    GDB_FORK_INACTIVE,
    /* Asking the peer to close its GDB socket fd. */
    GDB_FORK_ENABLING,
    /* Asking the peer to take over, closing our GDB socket fd. */
    GDB_FORK_DISABLING,
    /* The peer has taken over, our GDB socket fd is closed. */
    GDB_FORK_DISABLED,
};

enum GDBForkMessage {
    GDB_FORK_ACTIVATE = 'a',
    GDB_FORK_ENABLE = 'e',
    GDB_FORK_DISABLE = 'd',
};

/* User-mode specific state */
typedef struct {
    int fd;
    char *socket_path;
    int running_state;
    /*
     * Store syscalls mask without memory allocation in order to avoid
     * implementing synchronization.
     */
    bool catch_all_syscalls;
    GDBSyscallsMask catch_syscalls_mask;
    bool fork_events;
    enum GDBForkState fork_state;
    int fork_sockets[2];
    pid_t fork_peer_pid, fork_peer_tid;
    uint8_t siginfo[MAX_SIGINFO_LENGTH];
    unsigned long siginfo_len;
} GDBUserState;

static GDBUserState gdbserver_user_state;

int gdb_get_char(void)
{
    uint8_t ch;
    int ret;

    for (;;) {
        ret = recv(gdbserver_user_state.fd, &ch, 1, 0);
        if (ret < 0) {
            if (errno == ECONNRESET) {
                gdbserver_user_state.fd = -1;
            }
            if (errno != EINTR) {
                return -1;
            }
        } else if (ret == 0) {
            close(gdbserver_user_state.fd);
            gdbserver_user_state.fd = -1;
            return -1;
        } else {
            break;
        }
    }
    return ch;
}

bool gdb_got_immediate_ack(void)
{
    int i;

    i = gdb_get_char();
    if (i < 0) {
        /* no response, continue anyway */
        return true;
    }

    if (i == '+') {
        /* received correctly, continue */
        return true;
    }

    /* anything else, including '-' then try again */
    return false;
}

void gdb_put_buffer(const uint8_t *buf, int len)
{
    int ret;

    while (len > 0) {
        ret = send(gdbserver_user_state.fd, buf, len, 0);
        if (ret < 0) {
            if (errno != EINTR) {
                return;
            }
        } else {
            buf += ret;
            len -= ret;
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
    if (gdbserver_user_state.socket_path) {
        unlink(gdbserver_user_state.socket_path);
    }
    if (gdbserver_user_state.fd < 0) {
        return;
    }

    trace_gdbstub_op_exiting((uint8_t)code);

    if (gdbserver_state.allow_stop_reply) {
        snprintf(buf, sizeof(buf), "W%02x", (uint8_t)code);
        gdb_put_packet(buf);
        gdbserver_state.allow_stop_reply = false;
    }

}

void gdb_qemu_exit(int code)
{
    exit(code);
}

int gdb_handlesig(CPUState *cpu, int sig, const char *reason, void *siginfo,
                  int siginfo_len)
{
    char buf[256];
    int n;

    if (!gdbserver_state.init || gdbserver_user_state.fd < 0) {
        return sig;
    }

    if (siginfo) {
        /*
         * Save target-specific siginfo.
         *
         * siginfo size, i.e. siginfo_len, is asserted at compile-time to fit in
         * gdbserver_user_state.siginfo, usually in the source file calling
         * gdb_handlesig. See, for instance, {linux,bsd}-user/signal.c.
         */
        memcpy(gdbserver_user_state.siginfo, siginfo, siginfo_len);
        gdbserver_user_state.siginfo_len = siginfo_len;
    }

    /* disable single step if it was enabled */
    cpu_single_step(cpu, 0);

    if (sig != 0) {
        gdb_set_stop_cpu(cpu);
        if (gdbserver_state.allow_stop_reply) {
            g_string_printf(gdbserver_state.str_buf,
                            "T%02xthread:", gdb_target_signal_to_gdb(sig));
            gdb_append_thread_id(cpu, gdbserver_state.str_buf);
            g_string_append_c(gdbserver_state.str_buf, ';');
            if (reason) {
                g_string_append(gdbserver_state.str_buf, reason);
            }
            gdb_put_strbuf();
            gdbserver_state.allow_stop_reply = false;
        }
    }
    /*
     * gdb_put_packet() might have detected that the peer terminated the
     * connection.
     */
    if (gdbserver_user_state.fd < 0) {
        return sig;
    }

    sig = 0;
    gdbserver_state.state = RS_IDLE;
    gdbserver_user_state.running_state = 0;
    while (gdbserver_user_state.running_state == 0) {
        n = read(gdbserver_user_state.fd, buf, 256);
        if (n > 0) {
            int i;

            for (i = 0; i < n; i++) {
                gdb_read_byte(buf[i]);
            }
        } else {
            /*
             * XXX: Connection closed.  Should probably wait for another
             * connection before continuing.
             */
            if (n == 0) {
                close(gdbserver_user_state.fd);
            }
            gdbserver_user_state.fd = -1;
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

    if (!gdbserver_state.init || gdbserver_user_state.fd < 0 ||
        !gdbserver_state.allow_stop_reply) {
        return;
    }

    snprintf(buf, sizeof(buf), "X%02x", gdb_target_signal_to_gdb(sig));
    gdb_put_packet(buf);
    gdbserver_state.allow_stop_reply = false;
}

static void gdb_accept_init(int fd)
{
    gdb_init_gdbserver_state();
    gdb_create_default_process(&gdbserver_state);
    gdbserver_state.processes[0].attached = true;
    gdbserver_state.c_cpu = gdb_first_attached_cpu();
    gdbserver_state.g_cpu = gdbserver_state.c_cpu;
    gdbserver_user_state.fd = fd;
}

static bool gdb_accept_socket(int gdb_fd)
{
    int fd;

    for (;;) {
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

static int gdbserver_open_socket(const char *path, Error **errp)
{
    g_autoptr(GString) buf = g_string_new("");
    const char *pid_placeholder;

    pid_placeholder = strstr(path, "%d");
    if (pid_placeholder != NULL) {
        g_string_append_len(buf, path, pid_placeholder - path);
        g_string_append_printf(buf, "%d", qemu_get_thread_id());
        g_string_append(buf, pid_placeholder + 2);
        path = buf->str;
    }

    return unix_listen(path, errp);
}

static bool gdb_accept_tcp(int gdb_fd)
{
    struct sockaddr_in sockaddr = {};
    socklen_t len;
    int fd;

    for (;;) {
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

static int gdbserver_open_port(int port, Error **errp)
{
    struct sockaddr_in sockaddr;
    int fd, ret;

    fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        error_setg_errno(errp, errno, "Failed to create socket");
        return -1;
    }
    qemu_set_cloexec(fd);

    socket_set_fast_reuse(fd);

    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(port);
    sockaddr.sin_addr.s_addr = 0;
    ret = bind(fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
    if (ret < 0) {
        error_setg_errno(errp, errno, "Failed to bind socket");
        close(fd);
        return -1;
    }
    ret = listen(fd, 1);
    if (ret < 0) {
        error_setg_errno(errp, errno, "Failed to listen to socket");
        close(fd);
        return -1;
    }

    return fd;
}

static bool gdbserver_accept(int port, int gdb_fd, const char *path)
{
    bool ret;

    if (port > 0) {
        ret = gdb_accept_tcp(gdb_fd);
    } else {
        ret = gdb_accept_socket(gdb_fd);
        if (ret) {
            gdbserver_user_state.socket_path = g_strdup(path);
        }
    }

    if (!ret) {
        close(gdb_fd);
    }

    return ret;
}

struct {
    int port;
    int gdb_fd;
    char *path;
} gdbserver_args;

static void do_gdb_handlesig(CPUState *cs, run_on_cpu_data arg)
{
    int sig;

    sig = target_to_host_signal(gdb_handlesig(cs, 0, NULL, NULL, 0));
    if (sig >= 1 && sig < NSIG) {
        qemu_kill_thread(gdb_get_cpu_index(cs), sig);
    }
}

static void *gdbserver_accept_thread(void *arg)
{
    if (gdbserver_accept(gdbserver_args.port, gdbserver_args.gdb_fd,
                         gdbserver_args.path)) {
        CPUState *cs = first_cpu;

        async_safe_run_on_cpu(cs, do_gdb_handlesig, RUN_ON_CPU_NULL);
        qemu_kill_thread(gdb_get_cpu_index(cs), host_interrupt_signal);
    }

    g_free(gdbserver_args.path);
    gdbserver_args.path = NULL;

    return NULL;
}

#define USAGE "\nUsage: -g {port|path}[,suspend={y|n}]"

bool gdbserver_start(const char *args, Error **errp)
{
    g_auto(GStrv) argv = g_strsplit(args, ",", 0);
    const char *port_or_path = NULL;
    bool suspend = true;
    int gdb_fd, port;
    GStrv arg;

    for (arg = argv; *arg; arg++) {
        g_auto(GStrv) tokens = g_strsplit(*arg, "=", 2);

        if (g_strcmp0(tokens[0], "suspend") == 0) {
            if (tokens[1] == NULL) {
                error_setg(errp,
                           "gdbstub: missing \"suspend\" option value" USAGE);
                return false;
            } else if (!qapi_bool_parse(tokens[0], tokens[1],
                                        &suspend, errp)) {
                return false;
            }
        } else {
            if (port_or_path) {
                error_setg(errp, "gdbstub: unknown option \"%s\"" USAGE, *arg);
                return false;
            }
            port_or_path = *arg;
        }
    }
    if (!port_or_path) {
        error_setg(errp, "gdbstub: port or path not specified" USAGE);
        return false;
    }

    port = g_ascii_strtoull(port_or_path, NULL, 10);
    if (port > 0) {
        gdb_fd = gdbserver_open_port(port, errp);
    } else {
        gdb_fd = gdbserver_open_socket(port_or_path, errp);
    }
    if (gdb_fd < 0) {
        return false;
    }

    if (suspend) {
        if (gdbserver_accept(port, gdb_fd, port_or_path)) {
            gdb_handlesig(first_cpu, 0, NULL, NULL, 0);
            return true;
        } else {
            error_setg(errp, "gdbstub: failed to accept connection");
            return false;
        }
    } else {
        QemuThread thread;

        gdbserver_args.port = port;
        gdbserver_args.gdb_fd = gdb_fd;
        gdbserver_args.path = g_strdup(port_or_path);
        qemu_thread_create(&thread, "gdb-accept",
                           &gdbserver_accept_thread, NULL,
                           QEMU_THREAD_DETACHED);
        return true;
    }
}

void gdbserver_fork_start(void)
{
    if (!gdbserver_state.init || gdbserver_user_state.fd < 0) {
        return;
    }
    if (!gdbserver_user_state.fork_events ||
            qemu_socketpair(AF_UNIX, SOCK_STREAM, 0,
                            gdbserver_user_state.fork_sockets) < 0) {
        gdbserver_user_state.fork_state = GDB_FORK_DISABLED;
        return;
    }
    gdbserver_user_state.fork_state = GDB_FORK_INACTIVE;
    gdbserver_user_state.fork_peer_pid = getpid();
    gdbserver_user_state.fork_peer_tid = qemu_get_thread_id();
}

static void disable_gdbstub(CPUState *thread_cpu)
{
    CPUState *cpu;

    close(gdbserver_user_state.fd);
    gdbserver_user_state.fd = -1;
    CPU_FOREACH(cpu) {
        cpu_breakpoint_remove_all(cpu, BP_GDB);
        /* no cpu_watchpoint_remove_all for user-mode */
        cpu_single_step(cpu, 0);
    }
}

void gdbserver_fork_end(CPUState *cpu, pid_t pid)
{
    char b;
    int fd;

    if (!gdbserver_state.init || gdbserver_user_state.fd < 0) {
        return;
    }

    if (pid == -1) {
        if (gdbserver_user_state.fork_state != GDB_FORK_DISABLED) {
            g_assert(gdbserver_user_state.fork_state == GDB_FORK_INACTIVE);
            close(gdbserver_user_state.fork_sockets[0]);
            close(gdbserver_user_state.fork_sockets[1]);
        }
        return;
    }

    if (gdbserver_user_state.fork_state == GDB_FORK_DISABLED) {
        if (pid == 0) {
            disable_gdbstub(cpu);
        }
        return;
    }

    if (pid == 0) {
        close(gdbserver_user_state.fork_sockets[0]);
        fd = gdbserver_user_state.fork_sockets[1];
        g_assert(gdbserver_state.process_num == 1);
        g_assert(gdbserver_state.processes[0].pid ==
                     gdbserver_user_state.fork_peer_pid);
        g_assert(gdbserver_state.processes[0].attached);
        gdbserver_state.processes[0].pid = getpid();
    } else {
        close(gdbserver_user_state.fork_sockets[1]);
        fd = gdbserver_user_state.fork_sockets[0];
        gdbserver_user_state.fork_state = GDB_FORK_ACTIVE;
        gdbserver_user_state.fork_peer_pid = pid;
        gdbserver_user_state.fork_peer_tid = pid;

        if (!gdbserver_state.allow_stop_reply) {
            goto fail;
        }
        g_string_printf(gdbserver_state.str_buf,
                        "T%02xfork:p%02x.%02x;thread:p%02x.%02x;",
                        gdb_target_signal_to_gdb(gdb_target_sigtrap()),
                        pid, pid, (int)getpid(), qemu_get_thread_id());
        gdb_put_strbuf();
    }

    gdbserver_state.state = RS_IDLE;
    gdbserver_state.allow_stop_reply = false;
    gdbserver_user_state.running_state = 0;
    for (;;) {
        switch (gdbserver_user_state.fork_state) {
        case GDB_FORK_ENABLED:
            if (gdbserver_user_state.running_state) {
                close(fd);
                return;
            }
            QEMU_FALLTHROUGH;
        case GDB_FORK_ACTIVE:
            if (read(gdbserver_user_state.fd, &b, 1) != 1) {
                goto fail;
            }
            gdb_read_byte(b);
            break;
        case GDB_FORK_DEACTIVATING:
            b = GDB_FORK_ACTIVATE;
            if (write(fd, &b, 1) != 1) {
                goto fail;
            }
            gdbserver_user_state.fork_state = GDB_FORK_INACTIVE;
            break;
        case GDB_FORK_INACTIVE:
            if (read(fd, &b, 1) != 1) {
                goto fail;
            }
            switch (b) {
            case GDB_FORK_ACTIVATE:
                gdbserver_user_state.fork_state = GDB_FORK_ACTIVE;
                break;
            case GDB_FORK_ENABLE:
                gdbserver_user_state.fork_state = GDB_FORK_ENABLED;
                break;
            case GDB_FORK_DISABLE:
                gdbserver_user_state.fork_state = GDB_FORK_DISABLED;
                break;
            default:
                g_assert_not_reached();
            }
            break;
        case GDB_FORK_ENABLING:
            b = GDB_FORK_DISABLE;
            if (write(fd, &b, 1) != 1) {
                goto fail;
            }
            gdbserver_user_state.fork_state = GDB_FORK_ENABLED;
            break;
        case GDB_FORK_DISABLING:
            b = GDB_FORK_ENABLE;
            if (write(fd, &b, 1) != 1) {
                goto fail;
            }
            gdbserver_user_state.fork_state = GDB_FORK_DISABLED;
            break;
        case GDB_FORK_DISABLED:
            close(fd);
            disable_gdbstub(cpu);
            return;
        default:
            g_assert_not_reached();
        }
    }

fail:
    close(fd);
    if (pid == 0) {
        disable_gdbstub(cpu);
    }
}

void gdb_handle_query_supported_user(const char *gdb_supported)
{
    if (strstr(gdb_supported, "fork-events+")) {
        gdbserver_user_state.fork_events = true;
    }
    g_string_append(gdbserver_state.str_buf, ";fork-events+");
}

bool gdb_handle_set_thread_user(uint32_t pid, uint32_t tid)
{
    if (gdbserver_user_state.fork_state == GDB_FORK_ACTIVE &&
            pid == gdbserver_user_state.fork_peer_pid &&
            tid == gdbserver_user_state.fork_peer_tid) {
        gdbserver_user_state.fork_state = GDB_FORK_DEACTIVATING;
        gdb_put_packet("OK");
        return true;
    }
    return false;
}

bool gdb_handle_detach_user(uint32_t pid)
{
    bool enable;

    if (gdbserver_user_state.fork_state == GDB_FORK_ACTIVE) {
        enable = pid == gdbserver_user_state.fork_peer_pid;
        if (enable || pid == getpid()) {
            gdbserver_user_state.fork_state = enable ? GDB_FORK_ENABLING :
                                                       GDB_FORK_DISABLING;
            gdb_put_packet("OK");
            return true;
        }
    }
    return false;
}

/*
 * Execution state helpers
 */

void gdb_handle_query_attached(GArray *params, void *user_ctx)
{
    gdb_put_packet("0");
}

void gdb_continue(void)
{
    gdbserver_user_state.running_state = 1;
    trace_gdbstub_op_continue();
}

/*
 * Resume execution, for user-mode emulation it's equivalent to
 * gdb_continue.
 */
int gdb_continue_partial(char *newstates)
{
    CPUState *cpu;
    int res = 0;
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
    gdbserver_user_state.running_state = 1;
    return res;
}

/*
 * Memory access helpers
 */
int gdb_target_memory_rw_debug(CPUState *cpu, hwaddr addr,
                               uint8_t *buf, int len, bool is_write)
{
    if (cpu->cc->memory_rw_debug) {
        return cpu->cc->memory_rw_debug(cpu, addr, buf, len, is_write);
    }
    return cpu_memory_rw_debug(cpu, addr, buf, len, is_write);
}

/*
 * cpu helpers
 */

unsigned int gdb_get_max_cpus(void)
{
    CPUState *cpu;
    unsigned int max_cpus = 1;

    CPU_FOREACH(cpu) {
        max_cpus = max_cpus <= cpu->cpu_index ? cpu->cpu_index + 1 : max_cpus;
    }

    return max_cpus;
}

/* replay not supported for user-mode */
bool gdb_can_reverse(void)
{
    return false;
}

/*
 * Break/Watch point helpers
 */

bool gdb_supports_guest_debug(void)
{
    /* user-mode == TCG == supported */
    return true;
}

int gdb_breakpoint_insert(CPUState *cs, int type, vaddr addr, vaddr len)
{
    CPUState *cpu;
    int err = 0;

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
    default:
        /* user-mode doesn't support watchpoints */
        return -ENOSYS;
    }
}

int gdb_breakpoint_remove(CPUState *cs, int type, vaddr addr, vaddr len)
{
    CPUState *cpu;
    int err = 0;

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
    default:
        /* user-mode doesn't support watchpoints */
        return -ENOSYS;
    }
}

void gdb_breakpoint_remove_all(CPUState *cs)
{
    cpu_breakpoint_remove_all(cs, BP_GDB);
}

/*
 * For user-mode syscall support we send the system call immediately
 * and then return control to gdb for it to process the syscall request.
 * Since the protocol requires that gdb hands control back to us
 * using a "here are the results" F packet, we don't need to check
 * gdb_handlesig's return value (which is the signal to deliver if
 * execution was resumed via a continue packet).
 */
void gdb_syscall_handling(const char *syscall_packet)
{
    gdb_put_packet(syscall_packet);
    gdb_handlesig(gdbserver_state.c_cpu, 0, NULL, NULL, 0);
}

static bool should_catch_syscall(int num)
{
    if (gdbserver_user_state.catch_all_syscalls) {
        return true;
    }
    if (num < 0 || num >= GDB_NR_SYSCALLS) {
        return false;
    }
    return test_bit(num, gdbserver_user_state.catch_syscalls_mask);
}

void gdb_syscall_entry(CPUState *cs, int num)
{
    if (should_catch_syscall(num)) {
        g_autofree char *reason = g_strdup_printf("syscall_entry:%x;", num);
        gdb_handlesig(cs, gdb_target_sigtrap(), reason, NULL, 0);
    }
}

void gdb_syscall_return(CPUState *cs, int num)
{
    if (should_catch_syscall(num)) {
        g_autofree char *reason = g_strdup_printf("syscall_return:%x;", num);
        gdb_handlesig(cs, gdb_target_sigtrap(), reason, NULL, 0);
    }
}

void gdb_handle_set_catch_syscalls(GArray *params, void *user_ctx)
{
    const char *param = gdb_get_cmd_param(params, 0)->data;
    GDBSyscallsMask catch_syscalls_mask;
    bool catch_all_syscalls;
    unsigned int num;
    const char *p;

    /* "0" means not catching any syscalls. */
    if (strcmp(param, "0") == 0) {
        gdbserver_user_state.catch_all_syscalls = false;
        memset(gdbserver_user_state.catch_syscalls_mask, 0,
               sizeof(gdbserver_user_state.catch_syscalls_mask));
        gdb_put_packet("OK");
        return;
    }

    /* "1" means catching all syscalls. */
    if (strcmp(param, "1") == 0) {
        gdbserver_user_state.catch_all_syscalls = true;
        gdb_put_packet("OK");
        return;
    }

    /*
     * "1;..." means catching only the specified syscalls.
     * The syscall list must not be empty.
     */
    if (param[0] == '1' && param[1] == ';') {
        catch_all_syscalls = false;
        memset(catch_syscalls_mask, 0, sizeof(catch_syscalls_mask));
        for (p = &param[2];; p++) {
            if (qemu_strtoui(p, &p, 16, &num) || (*p && *p != ';')) {
                goto err;
            }
            if (num >= GDB_NR_SYSCALLS) {
                /*
                 * Fall back to reporting all syscalls. Reporting extra
                 * syscalls is inefficient, but the spec explicitly allows it.
                 * Keep parsing in case there is a syntax error ahead.
                 */
                catch_all_syscalls = true;
            } else {
                set_bit(num, catch_syscalls_mask);
            }
            if (!*p) {
                break;
            }
        }
        gdbserver_user_state.catch_all_syscalls = catch_all_syscalls;
        if (!catch_all_syscalls) {
            memcpy(gdbserver_user_state.catch_syscalls_mask,
                   catch_syscalls_mask, sizeof(catch_syscalls_mask));
        }
        gdb_put_packet("OK");
        return;
    }

err:
    gdb_put_packet("E00");
}

void gdb_handle_query_xfer_siginfo(GArray *params, void *user_ctx)
{
    unsigned long offset, len;
    uint8_t *siginfo_offset;

    offset = gdb_get_cmd_param(params, 0)->val_ul;
    len = gdb_get_cmd_param(params, 1)->val_ul;

    if (offset + len > gdbserver_user_state.siginfo_len) {
        /* Invalid offset and/or requested length. */
        gdb_put_packet("E01");
        return;
    }

    siginfo_offset = (uint8_t *)gdbserver_user_state.siginfo + offset;

    /* Reply */
    g_string_assign(gdbserver_state.str_buf, "l");
    gdb_memtox(gdbserver_state.str_buf, (const char *)siginfo_offset, len);
    gdb_put_packet_binary(gdbserver_state.str_buf->str,
                          gdbserver_state.str_buf->len, true);
}
