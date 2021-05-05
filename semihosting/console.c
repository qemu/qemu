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
#include "semihosting/semihost.h"
#include "semihosting/console.h"
#include "exec/gdbstub.h"
#include "exec/exec-all.h"
#include "qemu/log.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "qemu/fifo8.h"

int qemu_semihosting_log_out(const char *s, int len)
{
    Chardev *chardev = semihosting_get_chardev();
    if (chardev) {
        return qemu_chr_write_all(chardev, (uint8_t *) s, len);
    } else {
        return write(STDERR_FILENO, s, len);
    }
}

/*
 * A re-implementation of lock_user_string that we can use locally
 * instead of relying on softmmu-semi. Hopefully we can deprecate that
 * in time. Copy string until we find a 0 or address error.
 */
static GString *copy_user_string(CPUArchState *env, target_ulong addr)
{
    CPUState *cpu = env_cpu(env);
    GString *s = g_string_sized_new(128);
    uint8_t c;

    do {
        if (cpu_memory_rw_debug(cpu, addr++, &c, 1, 0) == 0) {
            if (c) {
                s = g_string_append_c(s, c);
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: passed inaccessible address " TARGET_FMT_lx,
                          __func__, addr);
            break;
        }
    } while (c!=0);

    return s;
}

static void semihosting_cb(CPUState *cs, target_ulong ret, target_ulong err)
{
    if (ret == (target_ulong) -1) {
        qemu_log("%s: gdb console output failed ("TARGET_FMT_ld")",
                 __func__, err);
    }
}

int qemu_semihosting_console_outs(CPUArchState *env, target_ulong addr)
{
    GString *s = copy_user_string(env, addr);
    int out = s->len;

    if (use_gdb_syscalls()) {
        gdb_do_syscall(semihosting_cb, "write,2,%x,%x", addr, s->len);
    } else {
        out = qemu_semihosting_log_out(s->str, s->len);
    }

    g_string_free(s, true);
    return out;
}

void qemu_semihosting_console_outc(CPUArchState *env, target_ulong addr)
{
    CPUState *cpu = env_cpu(env);
    uint8_t c;

    if (cpu_memory_rw_debug(cpu, addr, &c, 1, 0) == 0) {
        if (use_gdb_syscalls()) {
            gdb_do_syscall(semihosting_cb, "write,2,%x,%x", addr, 1);
        } else {
            qemu_semihosting_log_out((const char *) &c, 1);
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: passed inaccessible address " TARGET_FMT_lx,
                      __func__, addr);
    }
}

#define FIFO_SIZE   1024

/* Access to this structure is protected by the BQL */
typedef struct SemihostingConsole {
    CharBackend         backend;
    GSList              *sleeping_cpus;
    bool                got;
    Fifo8               fifo;
} SemihostingConsole;

static SemihostingConsole console;

static int console_can_read(void *opaque)
{
    SemihostingConsole *c = opaque;
    int ret;
    g_assert(qemu_mutex_iothread_locked());
    ret = (int) fifo8_num_free(&c->fifo);
    return ret;
}

static void console_wake_up(gpointer data, gpointer user_data)
{
    CPUState *cs = (CPUState *) data;
    /* cpu_handle_halt won't know we have work so just unbung here */
    cs->halted = 0;
    qemu_cpu_kick(cs);
}

static void console_read(void *opaque, const uint8_t *buf, int size)
{
    SemihostingConsole *c = opaque;
    g_assert(qemu_mutex_iothread_locked());
    while (size-- && !fifo8_is_full(&c->fifo)) {
        fifo8_push(&c->fifo, *buf++);
    }
    g_slist_foreach(c->sleeping_cpus, console_wake_up, NULL);
    c->sleeping_cpus = NULL;
}

target_ulong qemu_semihosting_console_inc(CPUArchState *env)
{
    uint8_t ch;
    SemihostingConsole *c = &console;
    g_assert(qemu_mutex_iothread_locked());
    g_assert(current_cpu);
    if (fifo8_is_empty(&c->fifo)) {
        c->sleeping_cpus = g_slist_prepend(c->sleeping_cpus, current_cpu);
        current_cpu->halted = 1;
        current_cpu->exception_index = EXCP_HALTED;
        cpu_loop_exit(current_cpu);
        /* never returns */
    }
    ch = fifo8_pop(&c->fifo);
    return (target_ulong) ch;
}

void qemu_semihosting_console_init(void)
{
    Chardev *chr = semihosting_get_chardev();

    if  (chr) {
        fifo8_create(&console.fifo, FIFO_SIZE);
        qemu_chr_fe_init(&console.backend, chr, &error_abort);
        qemu_chr_fe_set_handlers(&console.backend,
                                 console_can_read,
                                 console_read,
                                 NULL, NULL, &console,
                                 NULL, true);
    }
}
