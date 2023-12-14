/*
 * gdbstub user-mode helper routines.
 *
 * We know for user-mode we are using TCG so we can call stuff directly.
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: LGPL-2.0+
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/sockets.h"
#include "exec/hwaddr.h"
#include "exec/tb-flush.h"
#include "exec/gdbstub.h"
#include "gdbstub/syscalls.h"
#include "gdbstub/user.h"
#include "hw/core/cpu.h"
#include "trace.h"
#include "internals.h"

/* User-mode specific state */
typedef struct {
    int fd;
    char *socket_path;
    int running_state;
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

int gdb_handlesig(CPUState *cpu, int sig)
{
    char buf[256];
    int n;

    if (!gdbserver_state.init || gdbserver_user_state.fd < 0) {
        return sig;
    }

    /* disable single step if it was enabled */
    cpu_single_step(cpu, 0);
    tb_flush(cpu);

    if (sig != 0) {
        gdb_set_stop_cpu(cpu);
        if (gdbserver_state.allow_stop_reply) {
            g_string_printf(gdbserver_state.str_buf,
                            "T%02xthread:", gdb_target_signal_to_gdb(sig));
            gdb_append_thread_id(cpu, gdbserver_state.str_buf);
            g_string_append_c(gdbserver_state.str_buf, ';');
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
        gdbserver_user_state.socket_path = g_strdup(port_or_path);
        return 0;
    }

    /* gone wrong */
    close(gdb_fd);
    return -1;
}

/* Disable gdb stub for child processes.  */
void gdbserver_fork(CPUState *cpu)
{
    if (!gdbserver_state.init || gdbserver_user_state.fd < 0) {
        return;
    }
    close(gdbserver_user_state.fd);
    gdbserver_user_state.fd = -1;
    cpu_breakpoint_remove_all(cpu, BP_GDB);
    /* no cpu_watchpoint_remove_all for user-mode */
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
    CPUClass *cc;

    cc = CPU_GET_CLASS(cpu);
    if (cc->memory_rw_debug) {
        return cc->memory_rw_debug(cpu, addr, buf, len, is_write);
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
    gdb_handlesig(gdbserver_state.c_cpu, 0);
}
