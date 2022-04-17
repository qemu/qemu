#ifndef QEMU_EXEC_LOG_H
#define QEMU_EXEC_LOG_H

#include "qemu/log.h"
#include "hw/core/cpu.h"
#include "disas/disas.h"

/* cpu_dump_state() logging functions: */
/**
 * log_cpu_state:
 * @cpu: The CPU whose state is to be logged.
 * @flags: Flags what to log.
 *
 * Logs the output of cpu_dump_state().
 */
static inline void log_cpu_state(CPUState *cpu, int flags)
{
    QemuLogFile *logfile;

    if (qemu_log_enabled()) {
        rcu_read_lock();
        logfile = qatomic_rcu_read(&qemu_logfile);
        if (logfile) {
            cpu_dump_state(cpu, logfile->fd, flags);
        }
        rcu_read_unlock();
    }
}

/**
 * log_cpu_state_mask:
 * @mask: Mask when to log.
 * @cpu: The CPU whose state is to be logged.
 * @flags: Flags what to log.
 *
 * Logs the output of cpu_dump_state() if loglevel includes @mask.
 */
static inline void log_cpu_state_mask(int mask, CPUState *cpu, int flags)
{
    if (qemu_loglevel & mask) {
        log_cpu_state(cpu, flags);
    }
}

#ifdef NEED_CPU_H
#if defined(CONFIG_USER_ONLY)
/* page_dump() output to the log file: */
static inline void log_page_dump(const char *operation)
{
    FILE *logfile = qemu_log_trylock();
    if (logfile) {
        fprintf(logfile, "page layout changed following %s\n", operation);
        page_dump(logfile);
    }
    qemu_log_unlock(logfile);
}
#endif
#endif

#endif
