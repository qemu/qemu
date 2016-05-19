/*
 * Logging support
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/log.h"
#include "qemu/range.h"
#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "trace/control.h"

static char *logfilename;
FILE *qemu_logfile;
int qemu_loglevel;
static int log_append = 0;
static GArray *debug_regions;

void qemu_log(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    if (qemu_logfile) {
        vfprintf(qemu_logfile, fmt, ap);
    }
    va_end(ap);
}

static bool log_uses_own_buffers;

/* enable or disable low levels log */
void qemu_set_log(int log_flags)
{
    qemu_loglevel = log_flags;
#ifdef CONFIG_TRACE_LOG
    qemu_loglevel |= LOG_TRACE;
#endif
    if (!qemu_logfile &&
        (is_daemonized() ? logfilename != NULL : qemu_loglevel)) {
        if (logfilename) {
            qemu_logfile = fopen(logfilename, log_append ? "a" : "w");
            if (!qemu_logfile) {
                perror(logfilename);
                _exit(1);
            }
            /* In case we are a daemon redirect stderr to logfile */
            if (is_daemonized()) {
                dup2(fileno(qemu_logfile), STDERR_FILENO);
                fclose(qemu_logfile);
                /* This will skip closing logfile in qemu_log_close() */
                qemu_logfile = stderr;
            }
        } else {
            /* Default to stderr if no log file specified */
            assert(!is_daemonized());
            qemu_logfile = stderr;
        }
        /* must avoid mmap() usage of glibc by setting a buffer "by hand" */
        if (log_uses_own_buffers) {
            static char logfile_buf[4096];

            setvbuf(qemu_logfile, logfile_buf, _IOLBF, sizeof(logfile_buf));
        } else {
#if defined(_WIN32)
            /* Win32 doesn't support line-buffering, so use unbuffered output. */
            setvbuf(qemu_logfile, NULL, _IONBF, 0);
#else
            setvbuf(qemu_logfile, NULL, _IOLBF, 0);
#endif
            log_append = 1;
        }
    }
    if (qemu_logfile &&
        (is_daemonized() ? logfilename == NULL : !qemu_loglevel)) {
        qemu_log_close();
    }
}

void qemu_log_needs_buffers(void)
{
    log_uses_own_buffers = true;
}

/*
 * Allow the user to include %d in their logfile which will be
 * substituted with the current PID. This is useful for debugging many
 * nested linux-user tasks but will result in lots of logs.
 */
void qemu_set_log_filename(const char *filename)
{
    char *pidstr;
    g_free(logfilename);

    pidstr = strstr(filename, "%");
    if (pidstr) {
        /* We only accept one %d, no other format strings */
        if (pidstr[1] != 'd' || strchr(pidstr + 2, '%')) {
            error_report("Bad logfile format: %s", filename);
            logfilename = NULL;
        } else {
            logfilename = g_strdup_printf(filename, getpid());
        }
    } else {
        logfilename = g_strdup(filename);
    }
    qemu_log_close();
    qemu_set_log(qemu_loglevel);
}

/* Returns true if addr is in our debug filter or no filter defined
 */
bool qemu_log_in_addr_range(uint64_t addr)
{
    if (debug_regions) {
        int i = 0;
        for (i = 0; i < debug_regions->len; i++) {
            struct Range *range = &g_array_index(debug_regions, Range, i);
            if (addr >= range->begin && addr <= range->end) {
                return true;
            }
        }
        return false;
    } else {
        return true;
    }
}


void qemu_set_dfilter_ranges(const char *filter_spec)
{
    gchar **ranges = g_strsplit(filter_spec, ",", 0);
    if (ranges) {
        gchar **next = ranges;
        gchar *r = *next++;
        debug_regions = g_array_sized_new(FALSE, FALSE,
                                          sizeof(Range), g_strv_length(ranges));
        while (r) {
            char *range_op = strstr(r, "-");
            char *r2 = range_op ? range_op + 1 : NULL;
            if (!range_op) {
                range_op = strstr(r, "+");
                r2 = range_op ? range_op + 1 : NULL;
            }
            if (!range_op) {
                range_op = strstr(r, "..");
                r2 = range_op ? range_op + 2 : NULL;
            }
            if (range_op) {
                const char *e = NULL;
                uint64_t r1val, r2val;

                if ((qemu_strtoull(r, &e, 0, &r1val) == 0) &&
                    (qemu_strtoull(r2, NULL, 0, &r2val) == 0) &&
                    r2val > 0) {
                    struct Range range;

                    g_assert(e == range_op);

                    switch (*range_op) {
                    case '+':
                    {
                        range.begin = r1val;
                        range.end = r1val + (r2val - 1);
                        break;
                    }
                    case '-':
                    {
                        range.end = r1val;
                        range.begin = r1val - (r2val - 1);
                        break;
                    }
                    case '.':
                        range.begin = r1val;
                        range.end = r2val;
                        break;
                    default:
                        g_assert_not_reached();
                    }
                    g_array_append_val(debug_regions, range);

                } else {
                    g_error("Failed to parse range in: %s", r);
                }
            } else {
                g_error("Bad range specifier in: %s", r);
            }
            r = *next++;
        }
        g_strfreev(ranges);
    }
}

/* fflush() the log file */
void qemu_log_flush(void)
{
    fflush(qemu_logfile);
}

/* Close the log file */
void qemu_log_close(void)
{
    if (qemu_logfile) {
        if (qemu_logfile != stderr) {
            fclose(qemu_logfile);
        }
        qemu_logfile = NULL;
    }
}

const QEMULogItem qemu_log_items[] = {
    { CPU_LOG_TB_OUT_ASM, "out_asm",
      "show generated host assembly code for each compiled TB" },
    { CPU_LOG_TB_IN_ASM, "in_asm",
      "show target assembly code for each compiled TB" },
    { CPU_LOG_TB_OP, "op",
      "show micro ops for each compiled TB" },
    { CPU_LOG_TB_OP_OPT, "op_opt",
      "show micro ops (x86 only: before eflags optimization) and\n"
      "after liveness analysis" },
    { CPU_LOG_INT, "int",
      "show interrupts/exceptions in short format" },
    { CPU_LOG_EXEC, "exec",
      "show trace before each executed TB (lots of logs)" },
    { CPU_LOG_TB_CPU, "cpu",
      "show CPU registers before entering a TB (lots of logs)" },
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
    { 0, NULL, NULL },
};

static int cmp1(const char *s1, int n, const char *s2)
{
    if (strlen(s2) != n) {
        return 0;
    }
    return memcmp(s1, s2, n) == 0;
}

/* takes a comma separated list of log masks. Return 0 if error. */
int qemu_str_to_log_mask(const char *str)
{
    const QEMULogItem *item;
    int mask;
    const char *p, *p1;

    p = str;
    mask = 0;
    for (;;) {
        p1 = strchr(p, ',');
        if (!p1) {
            p1 = p + strlen(p);
        }
        if (cmp1(p,p1-p,"all")) {
            for (item = qemu_log_items; item->mask != 0; item++) {
                mask |= item->mask;
            }
#ifdef CONFIG_TRACE_LOG
        } else if (strncmp(p, "trace:", 6) == 0 && p + 6 != p1) {
            trace_enable_events(p + 6);
            mask |= LOG_TRACE;
#endif
        } else {
            for (item = qemu_log_items; item->mask != 0; item++) {
                if (cmp1(p, p1 - p, item->name)) {
                    goto found;
                }
            }
            return 0;
        found:
            mask |= item->mask;
        }
        if (*p1 != ',') {
            break;
        }
        p = p1 + 1;
    }
    return mask;
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
