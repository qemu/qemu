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
#ifdef CONFIG_LINUX
#include <sys/syscall.h>
#endif


typedef struct RCUCloseFILE {
    struct rcu_head rcu;
    FILE *fd;
} RCUCloseFILE;

/* Mutex covering the other global_* variables. */
static QemuMutex global_mutex;
static char *global_filename;
static FILE *global_file;
static __thread FILE *thread_file;
static __thread Notifier qemu_log_thread_cleanup_notifier;

int qemu_loglevel;
static bool log_per_thread;
static GArray *debug_regions;

/* Returns true if qemu_log() will really write somewhere. */
bool qemu_log_enabled(void)
{
    return log_per_thread || qatomic_read(&global_file) != NULL;
}

/* Returns true if qemu_log() will write somewhere other than stderr. */
bool qemu_log_separate(void)
{
    if (log_per_thread) {
        return true;
    } else {
        FILE *logfile = qatomic_read(&global_file);
        return logfile && logfile != stderr;
    }
}

static int log_thread_id(void)
{
#ifdef CONFIG_GETTID
    return gettid();
#elif defined(SYS_gettid)
    return syscall(SYS_gettid);
#else
    static int counter;
    return qatomic_fetch_inc(&counter);
#endif
}

static void qemu_log_thread_cleanup(Notifier *n, void *unused)
{
    if (thread_file != stderr) {
        fclose(thread_file);
        thread_file = NULL;
    }
}

/* Lock/unlock output. */

static FILE *qemu_log_trylock_with_err(Error **errp)
{
    FILE *logfile;

    logfile = thread_file;
    if (!logfile) {
        if (log_per_thread) {
            g_autofree char *filename
                = g_strdup_printf(global_filename, log_thread_id());
            logfile = fopen(filename, "w");
            if (!logfile) {
                error_setg_errno(errp, errno,
                                 "Error opening logfile %s for thread %d",
                                 filename, log_thread_id());
                return NULL;
            }
            thread_file = logfile;
            qemu_log_thread_cleanup_notifier.notify = qemu_log_thread_cleanup;
            qemu_thread_atexit_add(&qemu_log_thread_cleanup_notifier);
        } else {
            rcu_read_lock();
            /*
             * FIXME: typeof_strip_qual, as used by qatomic_rcu_read,
             * does not work with pointers to undefined structures,
             * such as we have with struct _IO_FILE and musl libc.
             * Since all we want is a read of a pointer, cast to void**,
             * which does work with typeof_strip_qual.
             */
            logfile = qatomic_rcu_read((void **)&global_file);
            if (!logfile) {
                rcu_read_unlock();
                return NULL;
            }
        }
    }

    qemu_flockfile(logfile);
    return logfile;
}

FILE *qemu_log_trylock(void)
{
    return qemu_log_trylock_with_err(NULL);
}

void qemu_log_unlock(FILE *logfile)
{
    if (logfile) {
        fflush(logfile);
        qemu_funlockfile(logfile);
        if (!log_per_thread) {
            rcu_read_unlock();
        }
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

static void rcu_close_file(RCUCloseFILE *r)
{
    fclose(r->fd);
    g_free(r);
}

/**
 * valid_filename_template:
 *
 * Validate the filename template.  Require %d if per_thread, allow it
 * otherwise; require no other % within the template.
 */

typedef enum {
    vft_error,
    vft_stderr,
    vft_strdup,
    vft_pid_printf,
} ValidFilenameTemplateResult;

static ValidFilenameTemplateResult
valid_filename_template(const char *filename, bool per_thread, Error **errp)
{
    if (filename) {
        char *pidstr = strstr(filename, "%");

        if (pidstr) {
            /* We only accept one %d, no other format strings */
            if (pidstr[1] != 'd' || strchr(pidstr + 2, '%')) {
                error_setg(errp, "Bad logfile template: %s", filename);
                return 0;
            }
            return per_thread ? vft_strdup : vft_pid_printf;
        }
    }
    if (per_thread) {
        error_setg(errp, "Filename template with '%%d' required for 'tid'");
        return vft_error;
    }
    return filename ? vft_strdup : vft_stderr;
}

/* enable or disable low levels log */
static bool qemu_set_log_internal(const char *filename, bool changed_name,
                                  int log_flags, Error **errp)
{
    bool need_to_open_file;
    bool daemonized;
    bool per_thread;
    FILE *logfile;

    QEMU_LOCK_GUARD(&global_mutex);
    logfile = global_file;

    /* The per-thread flag is immutable. */
    if (log_per_thread) {
        log_flags |= LOG_PER_THREAD;
    } else {
        if (global_filename) {
            log_flags &= ~LOG_PER_THREAD;
        }
    }

    per_thread = log_flags & LOG_PER_THREAD;

    if (changed_name) {
        char *newname = NULL;

        /*
         * Once threads start opening their own log files, we have no
         * easy mechanism to tell them all to close and re-open.
         * There seems little cause to do so either -- this option
         * will most often be used at user-only startup.
         */
        if (log_per_thread) {
            error_setg(errp, "Cannot change log filename after setting 'tid'");
            return false;
        }

        switch (valid_filename_template(filename, per_thread, errp)) {
        case vft_error:
            return false;
        case vft_stderr:
            break;
        case vft_strdup:
            newname = g_strdup(filename);
            break;
        case vft_pid_printf:
            newname = g_strdup_printf(filename, getpid());
            break;
        }

        g_free(global_filename);
        global_filename = newname;
        filename = newname;
    } else {
        filename = global_filename;
        if (per_thread &&
            valid_filename_template(filename, true, errp) == vft_error) {
            return false;
        }
    }

    /* Once the per-thread flag is set, it cannot be unset. */
    if (per_thread) {
        log_per_thread = true;
    }
    /* The flag itself is not relevant for need_to_open_file. */
    log_flags &= ~LOG_PER_THREAD;
#ifdef CONFIG_TRACE_LOG
    log_flags |= LOG_TRACE;
#endif
    qemu_loglevel = log_flags;

    daemonized = is_daemonized();
    need_to_open_file = false;
    if (!daemonized) {
        /*
         * If not daemonized we only log if qemu_loglevel is set, either to
         * stderr or to a file (if there is a filename).
         * If per-thread, open the file for each thread in qemu_log_trylock().
         */
        need_to_open_file = qemu_loglevel && !log_per_thread;
    } else {
        /*
         * If we are daemonized, we will only log if there is a filename.
         */
        need_to_open_file = filename != NULL;
    }

    if (logfile) {
        fflush(logfile);
        if (changed_name && logfile != stderr) {
            RCUCloseFILE *r = g_new0(RCUCloseFILE, 1);
            r->fd = logfile;
            qatomic_rcu_set(&global_file, NULL);
            call_rcu(r, rcu_close_file, rcu);
            logfile = NULL;
        }
    }

    if (log_per_thread && daemonized) {
        logfile = thread_file;
    }

    if (!logfile && need_to_open_file) {
        if (filename) {
            if (log_per_thread) {
                logfile = qemu_log_trylock_with_err(errp);
                if (!logfile) {
                    return false;
                }
                qemu_log_unlock(logfile);
            } else {
                logfile = fopen(filename, "w");
                if (!logfile) {
                    error_setg_errno(errp, errno, "Error opening logfile %s",
                                     filename);
                    return false;
                }
            }
            /* In case we are a daemon redirect stderr to logfile */
            if (daemonized) {
                dup2(fileno(logfile), STDERR_FILENO);
                fclose(logfile);
                /*
                 * This will skip closing logfile in rcu_close_file()
                 * or qemu_log_thread_cleanup().
                 */
                logfile = stderr;
            }
        } else {
            /* Default to stderr if no log file specified */
            assert(!daemonized);
            logfile = stderr;
        }

        if (log_per_thread && daemonized) {
            thread_file = logfile;
        } else {
            qatomic_rcu_set(&global_file, logfile);
        }
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
    { LOG_PER_THREAD, "tid",
      "open a separate log file per thread; filename must contain '%d'" },
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
