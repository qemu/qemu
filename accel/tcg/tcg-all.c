/*
 * QEMU System Emulator, accelerator interfaces
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2014 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "sysemu/accel.h"
#include "sysemu/tcg.h"
#include "qom/object.h"
#include "cpu.h"
#include "sysemu/cpus.h"
#include "qemu/main-loop.h"
#include "tcg/tcg.h"
#include "include/qapi/error.h"
#include "include/qemu/error-report.h"
#include "include/hw/boards.h"
#include "qemu/option.h"

unsigned long tcg_tb_size;

/* mask must never be zero, except for A20 change call */
static void tcg_handle_interrupt(CPUState *cpu, int mask)
{
    int old_mask;
    g_assert(qemu_mutex_iothread_locked());

    old_mask = cpu->interrupt_request;
    cpu->interrupt_request |= mask;

    /*
     * If called from iothread context, wake the target cpu in
     * case its halted.
     */
    if (!qemu_cpu_is_self(cpu)) {
        qemu_cpu_kick(cpu);
    } else {
        atomic_set(&cpu_neg(cpu)->icount_decr.u16.high, -1);
        if (use_icount &&
            !cpu->can_do_io
            && (mask & ~old_mask) != 0) {
            cpu_abort(cpu, "Raised interrupt while not in I/O function");
        }
    }
}

/*
 * We default to false if we know other options have been enabled
 * which are currently incompatible with MTTCG. Otherwise when each
 * guest (target) has been updated to support:
 *   - atomic instructions
 *   - memory ordering primitives (barriers)
 * they can set the appropriate CONFIG flags in ${target}-softmmu.mak
 *
 * Once a guest architecture has been converted to the new primitives
 * there are two remaining limitations to check.
 *
 * - The guest can't be oversized (e.g. 64 bit guest on 32 bit host)
 * - The host must have a stronger memory order than the guest
 *
 * It may be possible in future to support strong guests on weak hosts
 * but that will require tagging all load/stores in a guest with their
 * implicit memory order requirements which would likely slow things
 * down a lot.
 */

static bool check_tcg_memory_orders_compatible(void)
{
#if defined(TCG_GUEST_DEFAULT_MO) && defined(TCG_TARGET_DEFAULT_MO)
    return (TCG_GUEST_DEFAULT_MO & ~TCG_TARGET_DEFAULT_MO) == 0;
#else
    return false;
#endif
}

static bool default_mttcg_enabled(void)
{
    if (use_icount || TCG_OVERSIZED_GUEST) {
        return false;
    } else {
#ifdef TARGET_SUPPORTS_MTTCG
        return check_tcg_memory_orders_compatible();
#else
        return false;
#endif
    }
}

static void tcg_accel_instance_init(Object *obj)
{
    mttcg_enabled = default_mttcg_enabled();
}

static int tcg_init(MachineState *ms)
{
    tcg_exec_init(tcg_tb_size * 1024 * 1024);
    cpu_interrupt_handler = tcg_handle_interrupt;
    return 0;
}

void qemu_tcg_configure(QemuOpts *opts, Error **errp)
{
    const char *t = qemu_opt_get(opts, "thread");
    if (!t) {
        return;
    }
    if (strcmp(t, "multi") == 0) {
        if (TCG_OVERSIZED_GUEST) {
            error_setg(errp, "No MTTCG when guest word size > hosts");
        } else if (use_icount) {
            error_setg(errp, "No MTTCG when icount is enabled");
        } else {
#ifndef TARGET_SUPPORTS_MTTCG
            warn_report("Guest not yet converted to MTTCG - "
                        "you may get unexpected results");
#endif
            if (!check_tcg_memory_orders_compatible()) {
                warn_report("Guest expects a stronger memory ordering "
                            "than the host provides");
                error_printf("This may cause strange/hard to debug errors\n");
            }
            mttcg_enabled = true;
        }
    } else if (strcmp(t, "single") == 0) {
        mttcg_enabled = false;
    } else {
        error_setg(errp, "Invalid 'thread' setting %s", t);
    }
}

static void tcg_accel_class_init(ObjectClass *oc, void *data)
{
    AccelClass *ac = ACCEL_CLASS(oc);
    ac->name = "tcg";
    ac->init_machine = tcg_init;
    ac->allowed = &tcg_allowed;
}

#define TYPE_TCG_ACCEL ACCEL_CLASS_NAME("tcg")

static const TypeInfo tcg_accel_type = {
    .name = TYPE_TCG_ACCEL,
    .parent = TYPE_ACCEL,
    .instance_init = tcg_accel_instance_init,
    .class_init = tcg_accel_class_init,
};

static void register_accel_types(void)
{
    type_register_static(&tcg_accel_type);
}

type_init(register_accel_types);
