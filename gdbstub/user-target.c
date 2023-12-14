/*
 * Target specific user-mode handling
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: LGPL-2.0+
 */

#include "qemu/osdep.h"
#include "exec/gdbstub.h"
#include "qemu.h"
#include "internals.h"
#ifdef CONFIG_LINUX
#include "linux-user/loader.h"
#include "linux-user/qemu.h"
#endif

/*
 * Map target signal numbers to GDB protocol signal numbers and vice
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

int gdb_signal_to_target(int sig)
{
    if (sig < ARRAY_SIZE(gdb_signal_table)) {
        return gdb_signal_table[sig];
    } else {
        return -1;
    }
}

int gdb_target_signal_to_gdb(int sig)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(gdb_signal_table); i++) {
        if (gdb_signal_table[i] == sig) {
            return i;
        }
    }
    return GDB_SIGNAL_UNKNOWN;
}

int gdb_get_cpu_index(CPUState *cpu)
{
    TaskState *ts = (TaskState *) cpu->opaque;
    return ts ? ts->ts_tid : -1;
}

/*
 * User-mode specific command helpers
 */

void gdb_handle_query_offsets(GArray *params, void *user_ctx)
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
    gdb_put_strbuf();
}

#if defined(CONFIG_LINUX)
/* Partial user only duplicate of helper in gdbstub.c */
static inline int target_memory_rw_debug(CPUState *cpu, target_ulong addr,
                                         uint8_t *buf, int len, bool is_write)
{
    CPUClass *cc;
    cc = CPU_GET_CLASS(cpu);
    if (cc->memory_rw_debug) {
        return cc->memory_rw_debug(cpu, addr, buf, len, is_write);
    }
    return cpu_memory_rw_debug(cpu, addr, buf, len, is_write);
}

void gdb_handle_query_xfer_auxv(GArray *params, void *user_ctx)
{
    TaskState *ts;
    unsigned long offset, len, saved_auxv, auxv_len;

    if (params->len < 2) {
        gdb_put_packet("E22");
        return;
    }

    offset = get_param(params, 0)->val_ul;
    len = get_param(params, 1)->val_ul;
    ts = gdbserver_state.c_cpu->opaque;
    saved_auxv = ts->info->saved_auxv;
    auxv_len = ts->info->auxv_len;

    if (offset >= auxv_len) {
        gdb_put_packet("E00");
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
        gdb_put_packet("E14");
        return;
    }

    gdb_memtox(gdbserver_state.str_buf,
           (const char *)gdbserver_state.mem_buf->data, len);
    gdb_put_packet_binary(gdbserver_state.str_buf->str,
                      gdbserver_state.str_buf->len, true);
}
#endif

static const char *get_filename_param(GArray *params, int i)
{
    const char *hex_filename = get_param(params, i)->data;
    gdb_hextomem(gdbserver_state.mem_buf, hex_filename,
                 strlen(hex_filename) / 2);
    g_byte_array_append(gdbserver_state.mem_buf, (const guint8 *)"", 1);
    return (const char *)gdbserver_state.mem_buf->data;
}

static void hostio_reply_with_data(const void *buf, size_t n)
{
    g_string_printf(gdbserver_state.str_buf, "F%zx;", n);
    gdb_memtox(gdbserver_state.str_buf, buf, n);
    gdb_put_packet_binary(gdbserver_state.str_buf->str,
                          gdbserver_state.str_buf->len, true);
}

void gdb_handle_v_file_open(GArray *params, void *user_ctx)
{
    const char *filename = get_filename_param(params, 0);
    uint64_t flags = get_param(params, 1)->val_ull;
    uint64_t mode = get_param(params, 2)->val_ull;

#ifdef CONFIG_LINUX
    int fd = do_guest_openat(cpu_env(gdbserver_state.g_cpu), 0, filename,
                             flags, mode, false);
#else
    int fd = open(filename, flags, mode);
#endif
    if (fd < 0) {
        g_string_printf(gdbserver_state.str_buf, "F-1,%d", errno);
    } else {
        g_string_printf(gdbserver_state.str_buf, "F%d", fd);
    }
    gdb_put_strbuf();
}

void gdb_handle_v_file_close(GArray *params, void *user_ctx)
{
    int fd = get_param(params, 0)->val_ul;

    if (close(fd) == -1) {
        g_string_printf(gdbserver_state.str_buf, "F-1,%d", errno);
        gdb_put_strbuf();
        return;
    }

    gdb_put_packet("F00");
}

void gdb_handle_v_file_pread(GArray *params, void *user_ctx)
{
    int fd = get_param(params, 0)->val_ul;
    size_t count = get_param(params, 1)->val_ull;
    off_t offset = get_param(params, 2)->val_ull;

    size_t bufsiz = MIN(count, BUFSIZ);
    g_autofree char *buf = g_try_malloc(bufsiz);
    if (buf == NULL) {
        gdb_put_packet("E12");
        return;
    }

    ssize_t n = pread(fd, buf, bufsiz, offset);
    if (n < 0) {
        g_string_printf(gdbserver_state.str_buf, "F-1,%d", errno);
        gdb_put_strbuf();
        return;
    }
    hostio_reply_with_data(buf, n);
}

void gdb_handle_v_file_readlink(GArray *params, void *user_ctx)
{
    const char *filename = get_filename_param(params, 0);

    g_autofree char *buf = g_try_malloc(BUFSIZ);
    if (buf == NULL) {
        gdb_put_packet("E12");
        return;
    }

#ifdef CONFIG_LINUX
    ssize_t n = do_guest_readlink(filename, buf, BUFSIZ);
#else
    ssize_t n = readlink(filename, buf, BUFSIZ);
#endif
    if (n < 0) {
        g_string_printf(gdbserver_state.str_buf, "F-1,%d", errno);
        gdb_put_strbuf();
        return;
    }
    hostio_reply_with_data(buf, n);
}

void gdb_handle_query_xfer_exec_file(GArray *params, void *user_ctx)
{
    uint32_t pid = get_param(params, 0)->val_ul;
    uint32_t offset = get_param(params, 1)->val_ul;
    uint32_t length = get_param(params, 2)->val_ul;

    GDBProcess *process = gdb_get_process(pid);
    if (!process) {
        gdb_put_packet("E00");
        return;
    }

    CPUState *cpu = gdb_get_first_cpu_in_process(process);
    if (!cpu) {
        gdb_put_packet("E00");
        return;
    }

    TaskState *ts = cpu->opaque;
    if (!ts || !ts->bprm || !ts->bprm->filename) {
        gdb_put_packet("E00");
        return;
    }

    size_t total_length = strlen(ts->bprm->filename);
    if (offset > total_length) {
        gdb_put_packet("E00");
        return;
    }
    if (offset + length > total_length) {
        length = total_length - offset;
    }

    g_string_printf(gdbserver_state.str_buf, "l%.*s", length,
                    ts->bprm->filename + offset);
    gdb_put_strbuf();
}
