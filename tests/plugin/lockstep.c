/*
 * Lockstep Execution Plugin
 *
 * Allows you to execute two QEMU instances in lockstep and report
 * when their execution diverges. This is mainly useful for developers
 * who want to see where a change to TCG code generation has
 * introduced a subtle and hard to find bug.
 *
 * Caveats:
 *   - single-threaded linux-user apps only with non-deterministic syscalls
 *   - no MTTCG enabled system emulation (icount may help)
 *
 * While icount makes things more deterministic it doesn't mean a
 * particular run may execute the exact same sequence of blocks. An
 * asynchronous event (for example X11 graphics update) may cause a
 * block to end early and a new partial block to start. This means
 * serial only test cases are a better bet. -d nochain may also help.
 *
 * This code is not thread safe!
 *
 * Copyright (c) 2020 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <glib.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <errno.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

/* saved so we can uninstall later */
static qemu_plugin_id_t our_id;

static unsigned long bb_count;
static unsigned long insn_count;

/* Information about a translated block */
typedef struct {
    uint64_t pc;
    uint64_t insns;
} BlockInfo;

/* Information about an execution state in the log */
typedef struct {
    BlockInfo *block;
    unsigned long insn_count;
    unsigned long block_count;
} ExecInfo;

/* The execution state we compare */
typedef struct {
    uint64_t pc;
    unsigned long insn_count;
} ExecState;

typedef struct {
    GSList *log_pos;
    int distance;
} DivergeState;

/* list of translated block info */
static GSList *blocks;

/* execution log and points of divergence */
static GSList *log, *divergence_log;

static int socket_fd;
static char *path_to_unlink;

static bool verbose;

static void plugin_cleanup(qemu_plugin_id_t id)
{
    /* Free our block data */
    g_slist_free_full(blocks, &g_free);
    g_slist_free_full(log, &g_free);
    g_slist_free(divergence_log);

    close(socket_fd);
    if (path_to_unlink) {
        unlink(path_to_unlink);
    }
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    g_autoptr(GString) out = g_string_new("No divergence :-)\n");
    g_string_append_printf(out, "Executed %ld/%d blocks\n",
                           bb_count, g_slist_length(log));
    g_string_append_printf(out, "Executed ~%ld instructions\n", insn_count);
    qemu_plugin_outs(out->str);

    plugin_cleanup(id);
}

static void report_divergance(ExecState *us, ExecState *them)
{
    DivergeState divrec = { log, 0 };
    g_autoptr(GString) out = g_string_new("");
    bool diverged = false;

    /*
     * If we have diverged before did we get back on track or are we
     * totally loosing it?
     */
    if (divergence_log) {
        DivergeState *last = (DivergeState *) divergence_log->data;
        GSList *entry;

        for (entry = log; g_slist_next(entry); entry = g_slist_next(entry)) {
            if (entry == last->log_pos) {
                break;
            }
            divrec.distance++;
        }

        /*
         * If the last two records are so close it is likely we will
         * not recover synchronisation with the other end.
         */
        if (divrec.distance == 1 && last->distance == 1) {
            diverged = true;
        }
    }
    divergence_log = g_slist_prepend(divergence_log,
                                     g_memdup(&divrec, sizeof(divrec)));

    /* Output short log entry of going out of sync... */
    if (verbose || divrec.distance == 1 || diverged) {
        g_string_printf(out, "@ %#016lx vs %#016lx (%d/%d since last)\n",
                        us->pc, them->pc, g_slist_length(divergence_log),
                        divrec.distance);
        qemu_plugin_outs(out->str);
    }

    if (diverged) {
        int i;
        GSList *entry;

        g_string_printf(out, "Δ insn_count @ %#016lx (%ld) vs %#016lx (%ld)\n",
                        us->pc, us->insn_count, them->pc, them->insn_count);

        for (entry = log, i = 0;
             g_slist_next(entry) && i < 5;
             entry = g_slist_next(entry), i++) {
            ExecInfo *prev = (ExecInfo *) entry->data;
            g_string_append_printf(out,
                                   "  previously @ %#016lx/%ld (%ld insns)\n",
                                   prev->block->pc, prev->block->insns,
                                   prev->insn_count);
        }
        qemu_plugin_outs(out->str);
        qemu_plugin_outs("too much divergence... giving up.");
        qemu_plugin_uninstall(our_id, plugin_cleanup);
    }
}

static void vcpu_tb_exec(unsigned int cpu_index, void *udata)
{
    BlockInfo *bi = (BlockInfo *) udata;
    ExecState us, them;
    ssize_t bytes;
    ExecInfo *exec;

    us.pc = bi->pc;
    us.insn_count = insn_count;

    /*
     * Write our current position to the other end. If we fail the
     * other end has probably died and we should shut down gracefully.
     */
    bytes = write(socket_fd, &us, sizeof(ExecState));
    if (bytes < sizeof(ExecState)) {
        qemu_plugin_outs(bytes < 0 ?
                         "problem writing to socket" :
                         "wrote less than expected to socket");
        qemu_plugin_uninstall(our_id, plugin_cleanup);
        return;
    }

    /*
     * Now read where our peer has reached. Again a failure probably
     * indicates the other end died and we should close down cleanly.
     */
    bytes = read(socket_fd, &them, sizeof(ExecState));
    if (bytes < sizeof(ExecState)) {
        qemu_plugin_outs(bytes < 0 ?
                         "problem reading from socket" :
                         "read less than expected");
        qemu_plugin_uninstall(our_id, plugin_cleanup);
        return;
    }

    /*
     * Compare and report if we have diverged.
     */
    if (us.pc != them.pc) {
        report_divergance(&us, &them);
    }

    /*
     * Assume this block will execute fully and record it
     * in the execution log.
     */
    insn_count += bi->insns;
    bb_count++;
    exec = g_new0(ExecInfo, 1);
    exec->block = bi;
    exec->insn_count = insn_count;
    exec->block_count = bb_count;
    log = g_slist_prepend(log, exec);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    BlockInfo *bi = g_new0(BlockInfo, 1);
    bi->pc = qemu_plugin_tb_vaddr(tb);
    bi->insns = qemu_plugin_tb_n_insns(tb);

    /* save a reference so we can free later */
    blocks = g_slist_prepend(blocks, bi);
    qemu_plugin_register_vcpu_tb_exec_cb(tb, vcpu_tb_exec,
                                         QEMU_PLUGIN_CB_NO_REGS, (void *)bi);
}


/*
 * Instead of encoding master/slave status into what is essentially
 * two peers we shall just take the simple approach of checking for
 * the existence of the pipe and assuming if it's not there we are the
 * first process.
 */
static bool setup_socket(const char *path)
{
    struct sockaddr_un sockaddr;
    int fd;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("create socket");
        return false;
    }

    sockaddr.sun_family = AF_UNIX;
    g_strlcpy(sockaddr.sun_path, path, sizeof(sockaddr.sun_path) - 1);
    if (bind(fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) < 0) {
        perror("bind socket");
        close(fd);
        return false;
    }

    /* remember to clean-up */
    path_to_unlink = g_strdup(path);

    if (listen(fd, 1) < 0) {
        perror("listen socket");
        close(fd);
        return false;
    }

    socket_fd = accept(fd, NULL, NULL);
    if (socket_fd < 0 && errno != EINTR) {
        perror("accept socket");
        return false;
    }

    qemu_plugin_outs("setup_socket::ready\n");

    return true;
}

static bool connect_socket(const char *path)
{
    int fd;
    struct sockaddr_un sockaddr;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("create socket");
        return false;
    }

    sockaddr.sun_family = AF_UNIX;
    g_strlcpy(sockaddr.sun_path, path, sizeof(sockaddr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) < 0) {
        perror("failed to connect");
        return false;
    }

    qemu_plugin_outs("connect_socket::ready\n");

    socket_fd = fd;
    return true;
}

static bool setup_unix_socket(const char *path)
{
    if (g_file_test(path, G_FILE_TEST_EXISTS)) {
        return connect_socket(path);
    } else {
        return setup_socket(path);
    }
}


QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    int i;

    if (!argc || !argv[0]) {
        qemu_plugin_outs("Need a socket path to talk to other instance.");
        return -1;
    }

    for (i = 0; i < argc; i++) {
        char *p = argv[i];
        if (strcmp(p, "verbose") == 0) {
            verbose = true;
        } else if (!setup_unix_socket(argv[0])) {
            qemu_plugin_outs("Failed to setup socket for communications.");
            return -1;
        }
    }

    our_id = id;

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
