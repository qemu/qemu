/*
 * Semihosting Console Support
 *
 * Copyright (c) 2015 Imagination Technologies
 * Copyright (c) 2019 Linaro Ltd
 *
 * This provides support for outputting to a semihosting console.
 *
 * While most semihosting implementations support reading and writing
 * to arbitrary file descriptors we treat the console as something
 * specifically for debugging interaction. This means messages can be
 * re-directed to gdb (if currently being used to debug) or even
 * re-directed elsewhere.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "hw/semihosting/console.h"
#include "exec/gdbstub.h"
#include "qemu/log.h"

int qemu_semihosting_log_out(const char *s, int len)
{
    return write(STDERR_FILENO, s, len);
}

/*
 * A re-implementation of lock_user_string that we can use locally
 * instead of relying on softmmu-semi. Hopefully we can deprecate that
 * in time. We either copy len bytes if specified or until we find a NULL.
 */
static GString *copy_user_string(CPUArchState *env, target_ulong addr, int len)
{
    CPUState *cpu = ENV_GET_CPU(env);
    GString *s = g_string_sized_new(len ? len : 128);
    uint8_t c;
    bool done;

    do {
        if (cpu_memory_rw_debug(cpu, addr++, &c, 1, 0) == 0) {
            s = g_string_append_c(s, c);
            done = len ? s->len == len : c == 0;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: passed inaccessible address " TARGET_FMT_lx,
                          __func__, addr);
            done = true;
        }
    } while (!done);

    return s;
}

static void semihosting_cb(CPUState *cs, target_ulong ret, target_ulong err)
{
    if (ret == (target_ulong) -1) {
        qemu_log("%s: gdb console output failed ("TARGET_FMT_ld")",
                 __func__, err);
    }
}

int qemu_semihosting_console_out(CPUArchState *env, target_ulong addr, int len)
{
    GString *s = copy_user_string(env, addr, len);
    int out = s->len;

    if (use_gdb_syscalls()) {
        gdb_do_syscall(semihosting_cb, "write,2,%x,%x", addr, s->len);
    } else {
        out = qemu_semihosting_log_out(s->str, s->len);
    }

    g_string_free(s, true);
    return out;
}
