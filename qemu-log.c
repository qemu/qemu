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

#include "qemu-common.h"
#include "qemu-log.h"

#ifdef WIN32
static const char *logfilename = "qemu.log";
#else
static const char *logfilename = "/tmp/qemu.log";
#endif
FILE *qemu_logfile;
int qemu_loglevel;
static int log_append = 0;

void qemu_log(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    if (qemu_logfile) {
        vfprintf(qemu_logfile, fmt, ap);
    }
    va_end(ap);
}

void qemu_log_mask(int mask, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    if ((qemu_loglevel & mask) && qemu_logfile) {
        vfprintf(qemu_logfile, fmt, ap);
    }
    va_end(ap);
}

/* enable or disable low levels log */
void qemu_set_log(int log_flags, bool use_own_buffers)
{
    qemu_loglevel = log_flags;
    if (qemu_loglevel && !qemu_logfile) {
        qemu_logfile = fopen(logfilename, log_append ? "a" : "w");
        if (!qemu_logfile) {
            perror(logfilename);
            _exit(1);
        }
        /* must avoid mmap() usage of glibc by setting a buffer "by hand" */
        if (use_own_buffers) {
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
    if (!qemu_loglevel && qemu_logfile) {
        fclose(qemu_logfile);
        qemu_logfile = NULL;
    }
}

void cpu_set_log_filename(const char *filename)
{
    logfilename = strdup(filename);
    if (qemu_logfile) {
        fclose(qemu_logfile);
        qemu_logfile = NULL;
    }
    cpu_set_log(qemu_loglevel);
}

const CPULogItem cpu_log_items[] = {
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
      "show CPU state before block translation" },
    { CPU_LOG_PCALL, "pcall",
      "x86 only: show protected mode far calls/returns/exceptions" },
    { CPU_LOG_RESET, "cpu_reset",
      "x86 only: show CPU state before CPU resets" },
    { CPU_LOG_IOPORT, "ioport",
      "show all i/o ports accesses" },
    { LOG_UNIMP, "unimp",
      "log unimplemented functionality" },
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
int cpu_str_to_log_mask(const char *str)
{
    const CPULogItem *item;
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
            for (item = cpu_log_items; item->mask != 0; item++) {
                mask |= item->mask;
            }
        } else {
            for (item = cpu_log_items; item->mask != 0; item++) {
                if (cmp1(p, p1 - p, item->name)) {
                    goto found;
                }
            }
            return 0;
        }
    found:
        mask |= item->mask;
        if (*p1 != ',') {
            break;
        }
        p = p1 + 1;
    }
    return mask;
}
