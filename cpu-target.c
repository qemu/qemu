/*
 * Target-specific parts of the CPU object
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
#include "cpu.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/qemu-print.h"
#include "system/accel-ops.h"
#include "system/cpus.h"
#include "exec/cpu-common.h"
#include "exec/tswap.h"
#include "exec/replay-core.h"
#include "exec/log.h"
#include "accel/accel-cpu-target.h"
#include "trace/trace-root.h"

/* Validate correct placement of CPUArchState. */
QEMU_BUILD_BUG_ON(offsetof(ArchCPU, parent_obj) != 0);
QEMU_BUILD_BUG_ON(offsetof(ArchCPU, env) != sizeof(CPUState));

char *cpu_model_from_type(const char *typename)
{
    const char *suffix = "-" CPU_RESOLVING_TYPE;

    if (!object_class_by_name(typename)) {
        return NULL;
    }

    if (g_str_has_suffix(typename, suffix)) {
        return g_strndup(typename, strlen(typename) - strlen(suffix));
    }

    return g_strdup(typename);
}

const char *parse_cpu_option(const char *cpu_option)
{
    ObjectClass *oc;
    CPUClass *cc;
    gchar **model_pieces;
    const char *cpu_type;

    model_pieces = g_strsplit(cpu_option, ",", 2);
    if (!model_pieces[0]) {
        error_report("-cpu option cannot be empty");
        exit(1);
    }

    oc = cpu_class_by_name(CPU_RESOLVING_TYPE, model_pieces[0]);
    if (oc == NULL) {
        error_report("unable to find CPU model '%s'", model_pieces[0]);
        g_strfreev(model_pieces);
        exit(EXIT_FAILURE);
    }

    cpu_type = object_class_get_name(oc);
    cc = CPU_CLASS(oc);
    cc->parse_features(cpu_type, model_pieces[1], &error_fatal);
    g_strfreev(model_pieces);
    return cpu_type;
}

#ifndef cpu_list
static void cpu_list_entry(gpointer data, gpointer user_data)
{
    CPUClass *cc = CPU_CLASS(OBJECT_CLASS(data));
    const char *typename = object_class_get_name(OBJECT_CLASS(data));
    g_autofree char *model = cpu_model_from_type(typename);

    if (cc->deprecation_note) {
        qemu_printf("  %s (deprecated)\n", model);
    } else {
        qemu_printf("  %s\n", model);
    }
}

static void cpu_list(void)
{
    GSList *list;

    list = object_class_get_list_sorted(TYPE_CPU, false);
    qemu_printf("Available CPUs:\n");
    g_slist_foreach(list, cpu_list_entry, NULL);
    g_slist_free(list);
}
#endif

void list_cpus(void)
{
    cpu_list();
}

/* enable or disable single step mode. EXCP_DEBUG is returned by the
   CPU loop after each instruction */
void cpu_single_step(CPUState *cpu, int enabled)
{
    if (cpu->singlestep_enabled != enabled) {
        cpu->singlestep_enabled = enabled;

#if !defined(CONFIG_USER_ONLY)
        const AccelOpsClass *ops = cpus_get_accel();
        if (ops->update_guest_debug) {
            ops->update_guest_debug(cpu);
        }
#endif

        trace_breakpoint_singlestep(cpu->cpu_index, enabled);
    }
}

void cpu_abort(CPUState *cpu, const char *fmt, ...)
{
    va_list ap;
    va_list ap2;

    va_start(ap, fmt);
    va_copy(ap2, ap);
    fprintf(stderr, "qemu: fatal: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    cpu_dump_state(cpu, stderr, CPU_DUMP_FPU | CPU_DUMP_CCOP);
    if (qemu_log_separate()) {
        FILE *logfile = qemu_log_trylock();
        if (logfile) {
            fprintf(logfile, "qemu: fatal: ");
            vfprintf(logfile, fmt, ap2);
            fprintf(logfile, "\n");
            cpu_dump_state(cpu, logfile, CPU_DUMP_FPU | CPU_DUMP_CCOP);
            qemu_log_unlock(logfile);
        }
    }
    va_end(ap2);
    va_end(ap);
    replay_finish();
#if defined(CONFIG_USER_ONLY)
    {
        struct sigaction act;
        sigfillset(&act.sa_mask);
        act.sa_handler = SIG_DFL;
        act.sa_flags = 0;
        sigaction(SIGABRT, &act, NULL);
    }
#endif
    abort();
}

#undef target_words_bigendian
bool target_words_bigendian(void)
{
    return TARGET_BIG_ENDIAN;
}

const char *target_name(void)
{
    return TARGET_NAME;
}
