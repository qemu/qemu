/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  QEMU TCG monitor
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
 */

#include "qemu/osdep.h"
#include "qemu/accel.h"
#include "qapi/error.h"
#include "qapi/type-helpers.h"
#include "qapi/qapi-commands-machine.h"
#include "monitor/monitor.h"
#include "sysemu/cpus.h"
#include "sysemu/cpu-timers.h"
#include "sysemu/tcg.h"
#include "tcg/tcg.h"
#include "internal.h"


static void dump_drift_info(GString *buf)
{
    if (!icount_enabled()) {
        return;
    }

    g_string_append_printf(buf, "Host - Guest clock  %"PRIi64" ms\n",
                           (cpu_get_clock() - icount_get()) / SCALE_MS);
    if (icount_align_option) {
        g_string_append_printf(buf, "Max guest delay     %"PRIi64" ms\n",
                               -max_delay / SCALE_MS);
        g_string_append_printf(buf, "Max guest advance   %"PRIi64" ms\n",
                               max_advance / SCALE_MS);
    } else {
        g_string_append_printf(buf, "Max guest delay     NA\n");
        g_string_append_printf(buf, "Max guest advance   NA\n");
    }
}

static void dump_accel_info(GString *buf)
{
    AccelState *accel = current_accel();
    bool one_insn_per_tb = object_property_get_bool(OBJECT(accel),
                                                    "one-insn-per-tb",
                                                    &error_fatal);

    g_string_append_printf(buf, "Accelerator settings:\n");
    g_string_append_printf(buf, "one-insn-per-tb: %s\n\n",
                           one_insn_per_tb ? "on" : "off");
}

HumanReadableText *qmp_x_query_jit(Error **errp)
{
    g_autoptr(GString) buf = g_string_new("");

    if (!tcg_enabled()) {
        error_setg(errp, "JIT information is only available with accel=tcg");
        return NULL;
    }

    dump_accel_info(buf);
    dump_exec_info(buf);
    dump_drift_info(buf);

    return human_readable_text_from_str(buf);
}

HumanReadableText *qmp_x_query_opcount(Error **errp)
{
    g_autoptr(GString) buf = g_string_new("");

    if (!tcg_enabled()) {
        error_setg(errp,
                   "Opcode count information is only available with accel=tcg");
        return NULL;
    }

    tcg_dump_op_count(buf);

    return human_readable_text_from_str(buf);
}

static void hmp_tcg_register(void)
{
    monitor_register_hmp_info_hrt("jit", qmp_x_query_jit);
    monitor_register_hmp_info_hrt("opcount", qmp_x_query_opcount);
}

type_init(hmp_tcg_register);
