/*
 * GDB Syscall Handling
 *
 * GDB can execute syscalls on the guests behalf, currently used by
 * the various semihosting extensions. As this interfaces with a guest
 * ABI we need to build it per-guest (although in reality its a 32 or
 * 64 bit target_ulong that is the only difference).
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2023 Linaro Ltd
 *
 * SPDX-License-Identifier: LGPL-2.0+
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "cpu.h"
#include "semihosting/semihost.h"
#include "sysemu/runstate.h"
#include "gdbstub/user.h"
#include "gdbstub/syscalls.h"
#include "trace.h"
#include "internals.h"

/* Syscall specific state */
typedef struct {
    char syscall_buf[256];
    gdb_syscall_complete_cb current_syscall_cb;
} GDBSyscallState;

static GDBSyscallState gdbserver_syscall_state;

/*
 * Return true if there is a GDB currently connected to the stub
 * and attached to a CPU
 */
static bool gdb_attached(void)
{
    return gdbserver_state.init && gdbserver_state.c_cpu;
}

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
        gdb_syscall_mode = gdb_attached() ? GDB_SYS_ENABLED : GDB_SYS_DISABLED;
    }
    return gdb_syscall_mode == GDB_SYS_ENABLED;
}

/* called when the stub detaches */
void gdb_disable_syscalls(void)
{
    gdb_syscall_mode = GDB_SYS_DISABLED;
}

void gdb_syscall_reset(void)
{
    gdbserver_syscall_state.current_syscall_cb = NULL;
}

bool gdb_handled_syscall(void)
{
    if (gdbserver_syscall_state.current_syscall_cb) {
        gdb_put_packet(gdbserver_syscall_state.syscall_buf);
        return true;
    }

    return false;
}

/*
 * Send a gdb syscall request.
 *  This accepts limited printf-style format specifiers, specifically:
 *   %x  - target_ulong argument printed in hex.
 *   %lx - 64-bit argument printed in hex.
 *   %s  - string pointer (target_ulong) and length (int) pair.
 */
void gdb_do_syscallv(gdb_syscall_complete_cb cb, const char *fmt, va_list va)
{
    char *p;
    char *p_end;
    target_ulong addr;
    uint64_t i64;

    if (!gdb_attached()) {
        return;
    }

    gdbserver_syscall_state.current_syscall_cb = cb;
#ifndef CONFIG_USER_ONLY
    vm_stop(RUN_STATE_DEBUG);
#endif
    p = &gdbserver_syscall_state.syscall_buf[0];
    p_end = &gdbserver_syscall_state.syscall_buf[sizeof(gdbserver_syscall_state.syscall_buf)];
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
                if (*(fmt++) != 'x') {
                    goto bad_format;
                }
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
    gdb_put_packet(gdbserver_syscall_state.syscall_buf);
    /*
     * Return control to gdb for it to process the syscall request.
     * Since the protocol requires that gdb hands control back to us
     * using a "here are the results" F packet, we don't need to check
     * gdb_handlesig's return value (which is the signal to deliver if
     * execution was resumed via a continue packet).
     */
    gdb_handlesig(gdbserver_state.c_cpu, 0);
#else
    /*
     * In this case wait to send the syscall packet until notification that
     * the CPU has stopped.  This must be done because if the packet is sent
     * now the reply from the syscall request could be received while the CPU
     * is still in the running state, which can cause packets to be dropped
     * and state transition 'T' packets to be sent while the syscall is still
     * being processed.
     */
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

/*
 * GDB Command Handlers
 */

void gdb_handle_file_io(GArray *params, void *user_ctx)
{
    if (params->len >= 1 && gdbserver_syscall_state.current_syscall_cb) {
        uint64_t ret;
        int err;

        ret = get_param(params, 0)->val_ull;
        if (params->len >= 2) {
            err = get_param(params, 1)->val_ull;
        } else {
            err = 0;
        }

        /* Convert GDB error numbers back to host error numbers. */
#define E(X)  case GDB_E##X: err = E##X; break
        switch (err) {
        case 0:
            break;
        E(PERM);
        E(NOENT);
        E(INTR);
        E(BADF);
        E(ACCES);
        E(FAULT);
        E(BUSY);
        E(EXIST);
        E(NODEV);
        E(NOTDIR);
        E(ISDIR);
        E(INVAL);
        E(NFILE);
        E(MFILE);
        E(FBIG);
        E(NOSPC);
        E(SPIPE);
        E(ROFS);
        E(NAMETOOLONG);
        default:
            err = EINVAL;
            break;
        }
#undef E

        gdbserver_syscall_state.current_syscall_cb(gdbserver_state.c_cpu,
                                                   ret, err);
        gdbserver_syscall_state.current_syscall_cb = NULL;
    }

    if (params->len >= 3 && get_param(params, 2)->opcode == (uint8_t)'C') {
        gdb_put_packet("T02");
        return;
    }

    gdb_continue();
}
