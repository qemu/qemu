/*
 * Logging support
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/range.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "trace/control.h"
#include "qemu/thread.h"
#include "qemu/lockable.h"
#include "qemu/rcu.h"


typedef struct QemuLogFile {
    struct rcu_head rcu;
    FILE *fd;
} QemuLogFile;

/* Mutex covering the other global_* variables. */
static QemuMutex global_mutex;
static char *global_filename;
static QemuLogFile *global_file;

int qemu_loglevel;
static int log_append = 0;
static GArray *debug_regions;

/* Returns true if qemu_log() will really write somewhere. */
bool qemu_log_enabled(void)
{
    return global_file != NULL;
}

/* Returns true if qemu_log() will write somewhere other than stderr. */
bool qemu_log_separate(void)
{
    QemuLogFile *logfile;
    bool res = false;

    rcu_read_lock();
    logfile = qatomic_rcu_read(&global_file);
    if (logfile && logfile->fd != stderr) {
        res = true;
    }
    rcu_read_unlock();
    return res;
}

/* Lock/unlock output. */

FILE *qemu_log_trylock(void)
{
    QemuLogFile *logfile;

    rcu_read_lock();
    logfile = qatomic_rcu_read(&global_file);
    if (logfile) {
        qemu_flockfile(logfile->fd);
        return logfile->fd;
    } else {
        rcu_read_unlock();
        return NULL;
    }
}

void qemu_log_unlock(FILE *fd)
{
    if (fd) {
        fflush(fd);
        qemu_funlockfile(fd);
        rcu_read_unlock();
    }
}

void qemu_log(const char *fmt, ...)
{
    FILE *f = qemu_log_trylock();
    if (f) {
        va_list ap;

        va_start(ap, fmt);
        vfprintf(f, fmt, ap);
        va_end(ap);
        qemu_log_unlock(f);
    }
}

static void __attribute__((__constructor__)) startup(void)
{
    qemu_mutex_init(&global_mutex);
}

static void qemu_logfile_free(QemuLogFile *logfile)
{
    g_assert(logfile);

    if (logfile->fd != stderr) {
        fclose(logfile->fd);
    }
    g_free(logfile);
}

/* enable or disable low levels log */
static bool qemu_set_log_internal(const char *filename, bool changed_name,
                                  int log_flags, Error **errp)
{
    bool need_to_open_file;
    QemuLogFile *logfile;

    QEMU_LOCK_GUARD(&global_mutex);
    logfile = global_file;

    if (changed_name) {
        char *newname = NULL;

        /*
         * Allow the user to include %d in their logfile which will be
         * substituted with the current PID. This is useful for debugging many
         * nested linux-user tasks but will result in lots of logs.
         *
         * filename may be NULL. In that case, log output is sent to stderr
         */
        if (filename) {
            char *pidstr = strstr(filename, "%");

            if (pidstr) {
                /* We only accept one %d, no other format strings */
                if (pidstr[1] != 'd' || strchr(pidstr + 2, '%')) {
                    error_setg(errp, "Bad logfile format: %s", filename);
                    return false;
                }
                newname = g_strdup_printf(filename, getpid());
            } else {
                newname = g_strdup(filename);
            }
        }

        g_free(global_filename);
        global_filename = newname;
        filename = newname;

        if (logfile) {
            qatomic_rcu_set(&global_file, NULL);
            call_rcu(logfile, qemu_logfile_free, rcu);
            logfile = NULL;
        }
    } else {
        filename = global_filename;
    }

#ifdef CONFIG_TRACE_LOG
    log_flags |= LOG_TRACE;
#endif
    qemu_loglevel = log_flags;

    /*
     * In all cases we only log if qemu_loglevel is set.
     * Also:
     *   If not daemonized we will always log either to stderr
     *     or to a file (if there is a filename).
     *   If we are daemonized, we will only log if there is a filename.
     */
    need_to_open_file = log_flags && (!is_daemonized() || filename);

    if (logfile && !need_to_open_file) {
        qatomic_rcu_set(&global_file, NULL);
        call_rcu(logfile, qemu_logfile_free, rcu);
        return true;
    }
    if (!logfile && need_to_open_file) {
        FILE *fd;

        if (filename) {
            fd = fopen(filename, log_append ? "a" : "w");
            if (!fd) {
                error_setg_errno(errp, errno, "Error opening logfile %s",
                                 filename);
                return false;
            }
            /* In case we are a daemon redirect stderr to logfile */
            if (is_daemonized()) {
                dup2(fileno(fd), STDERR_FILENO);
                fclose(fd);
                /* This will skip closing logfile in qemu_logfile_free. */
                fd = stderr;
            }
        } else {
            /* Default to stderr if no log file specified */
            assert(!is_daemonized());
            fd = stderr;
        }

        log_append = 1;

        logfile = g_new0(QemuLogFile, 1);
        logfile->fd = fd;
        qatomic_rcu_set(&global_file, logfile);
    }
    return true;
}

bool qemu_set_log(int log_flags, Error **errp)
{
    return qemu_set_log_internal(NULL, false, log_flags, errp);
}

bool qemu_set_log_filename(const char *filename, Error **errp)
{
    return qemu_set_log_internal(filename, true, qemu_loglevel, errp);
}

bool qemu_set_log_filename_flags(const char *name, int flags, Error **errp)
{
    return qemu_set_log_internal(name, true, flags, errp);
}

/* Returns true if addr is in our debug filter or no filter defined
 */
bool qemu_log_in_addr_range(uint64_t addr)
{
    if (debug_regions) {
        int i = 0;
        for (i = 0; i < debug_regions->len; i++) {
            Range *range = &g_array_index(debug_regions, Range, i);
            if (range_contains(range, addr)) {
                return true;
            }
        }
        return false;
    } else {
        return true;
    }
}


void qemu_set_dfilter_ranges(const char *filter_spec, Error **errp)
{
    gchar **ranges = g_strsplit(filter_spec, ",", 0);
    int i;

    if (debug_regions) {
        g_array_unref(debug_regions);
        debug_regions = NULL;
    }

    debug_regions = g_array_sized_new(FALSE, FALSE,
                                      sizeof(Range), g_strv_length(ranges));
    for (i = 0; ranges[i]; i++) {
        const char *r = ranges[i];
        const char *range_op, *r2, *e;
        uint64_t r1val, r2val, lob, upb;
        struct Range range;

        range_op = strstr(r, "-");
        r2 = range_op ? range_op + 1 : NULL;
        if (!range_op) {
            range_op = strstr(r, "+");
            r2 = range_op ? range_op + 1 : NULL;
        }
        if (!range_op) {
            range_op = strstr(r, "..");
            r2 = range_op ? range_op + 2 : NULL;
        }
        if (!range_op) {
            error_setg(errp, "Bad range specifier");
            goto out;
        }

        if (qemu_strtou64(r, &e, 0, &r1val)
            || e != range_op) {
            error_setg(errp, "Invalid number to the left of %.*s",
                       (int)(r2 - range_op), range_op);
            goto out;
        }
        if (qemu_strtou64(r2, NULL, 0, &r2val)) {
            error_setg(errp, "Invalid number to the right of %.*s",
                       (int)(r2 - range_op), range_op);
            goto out;
        }

        switch (*range_op) {
        case '+':
            lob = r1val;
            upb = r1val + r2val - 1;
            break;
        case '-':
            upb = r1val;
            lob = r1val - (r2val - 1);
            break;
        case '.':
            lob = r1val;
            upb = r2val;
            break;
        default:
            g_assert_not_reached();
        }
        if (lob > upb) {
            error_setg(errp, "Invalid range");
            goto out;
        }
        range_set_bounds(&range, lob, upb);
        g_array_append_val(debug_regions, range);
    }
out:
    g_strfreev(ranges);
}

const QEMULogItem qemu_log_items[] = {
    { CPU_LOG_TB_OUT_ASM, "out_asm",
      "show generated host assembly code for each compiled TB" },
    { CPU_LOG_TB_IN_ASM, "in_asm",
      "show target assembly code for each compiled TB" },
    { CPU_LOG_TB_OP, "op",
      "show micro ops for each compiled TB" },
    { CPU_LOG_TB_OP_OPT, "op_opt",
      "show micro ops after optimization" },
    { CPU_LOG_TB_OP_IND, "op_ind",
      "show micro ops before indirect lowering" },
    { CPU_LOG_INT, "int",
      "show interrupts/exceptions in short format" },
    { CPU_LOG_EXEC, "exec",
      "show trace before each executed TB (lots of logs)" },
    { CPU_LOG_TB_CPU, "cpu",
      "show CPU registers before entering a TB (lots of logs)" },
    { CPU_LOG_TB_FPU, "fpu",
      "include FPU registers in the 'cpu' logging" },
    { CPU_LOG_MMU, "mmu",
      "log MMU-related activities" },
    { CPU_LOG_PCALL, "pcall",
      "x86 only: show protected mode far calls/returns/exceptions" },
    { CPU_LOG_RESET, "cpu_reset",
      "show CPU state before CPU resets" },
    { LOG_UNIMP, "unimp",
      "log unimplemented functionality" },
    { LOG_GUEST_ERROR, "guest_errors",
      "log when the guest OS does something invalid (eg accessing a\n"
      "non-existent register)" },
    { CPU_LOG_PAGE, "page",
      "dump pages at beginning of user mode emulation" },
    { CPU_LOG_TB_NOCHAIN, "nochain",
      "do not chain compiled TBs so that \"exec\" and \"cpu\" show\n"
      "complete traces" },
#ifdef CONFIG_PLUGIN
    { CPU_LOG_PLUGIN, "plugin", "output from TCG plugins\n"},
#endif
    { LOG_STRACE, "strace",
      "log every user-mode syscall, its input, and its result" },
    { 0, NULL, NULL },
};

/* takes a comma separated list of log masks. Return 0 if error. */
int qemu_str_to_log_mask(const char *str)
{
    const QEMULogItem *item;
    int mask = 0;
    char **parts = g_strsplit(str, ",", 0);
    char **tmp;

    for (tmp = parts; tmp && *tmp; tmp++) {
        if (g_str_equal(*tmp, "all")) {
            for (item = qemu_log_items; item->mask != 0; item++) {
                mask |= item->mask;
            }
#ifdef CONFIG_TRACE_LOG
        } else if (g_str_has_prefix(*tmp, "trace:") && (*tmp)[6] != '\0') {
            trace_enable_events((*tmp) + 6);
            mask |= LOG_TRACE;
#endif
        } else {
            for (item = qemu_log_items; item->mask != 0; item++) {
                if (g_str_equal(*tmp, item->name)) {
                    goto found;
                }
            }
            goto error;
        found:
            mask |= item->mask;
        }
    }

    g_strfreev(parts);
    return mask;

 error:
    g_strfreev(parts);
    return 0;
}

void qemu_print_log_usage(FILE *f)
{
    const QEMULogItem *item;
    fprintf(f, "Log items (comma separated):\n");
    for (item = qemu_log_items; item->mask != 0; item++) {
        fprintf(f, "%-15s %s\n", item->name, item->help);
    }
#ifdef CONFIG_TRACE_LOG
    fprintf(f, "trace:PATTERN   enable trace events\n");
    fprintf(f, "\nUse \"-d trace:help\" to get a list of trace events.\n\n");
#endif
}
