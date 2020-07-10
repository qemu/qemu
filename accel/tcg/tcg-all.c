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
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/boards.h"
#include "qapi/qapi-builtin-visit.h"

typedef struct TCGState {
    AccelState parent_obj;

    bool mttcg_enabled;
    unsigned long tb_size;
} TCGState;

#define TYPE_TCG_ACCEL ACCEL_CLASS_NAME("tcg")

#define TCG_STATE(obj) \
        OBJECT_CHECK(TCGState, (obj), TYPE_TCG_ACCEL)

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
    TCGState *s = TCG_STATE(obj);

    s->mttcg_enabled = default_mttcg_enabled();
}

static int tcg_init(MachineState *ms)
{
    TCGState *s = TCG_STATE(current_accel());

    tcg_exec_init(s->tb_size * 1024 * 1024);
    cpu_interrupt_handler = tcg_handle_interrupt;
    mttcg_enabled = s->mttcg_enabled;
    return 0;
}

static char *tcg_get_thread(Object *obj, Error **errp)
{
    TCGState *s = TCG_STATE(obj);

    return g_strdup(s->mttcg_enabled ? "multi" : "single");
}

static void tcg_set_thread(Object *obj, const char *value, Error **errp)
{
    TCGState *s = TCG_STATE(obj);

    if (strcmp(value, "multi") == 0) {
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
            s->mttcg_enabled = true;
        }
    } else if (strcmp(value, "single") == 0) {
        s->mttcg_enabled = false;
    } else {
        error_setg(errp, "Invalid 'thread' setting %s", value);
    }
}

static void tcg_get_tb_size(Object *obj, Visitor *v,
                            const char *name, void *opaque,
                            Error **errp)
{
    TCGState *s = TCG_STATE(obj);
    uint32_t value = s->tb_size;

    visit_type_uint32(v, name, &value, errp);
}

static void tcg_set_tb_size(Object *obj, Visitor *v,
                            const char *name, void *opaque,
                            Error **errp)
{
    TCGState *s = TCG_STATE(obj);
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }

    s->tb_size = value;
}

static void tcg_accel_class_init(ObjectClass *oc, void *data)
{
    AccelClass *ac = ACCEL_CLASS(oc);
    ac->name = "tcg";
    ac->init_machine = tcg_init;
    ac->allowed = &tcg_allowed;

    object_class_property_add_str(oc, "thread",
                                  tcg_get_thread,
                                  tcg_set_thread);

    object_class_property_add(oc, "tb-size", "int",
        tcg_get_tb_size, tcg_set_tb_size,
        NULL, NULL);
    object_class_property_set_description(oc, "tb-size",
        "TCG translation block cache size");

}

static const TypeInfo tcg_accel_type = {
    .name = TYPE_TCG_ACCEL,
    .parent = TYPE_ACCEL,
    .instance_init = tcg_accel_instance_init,
    .class_init = tcg_accel_class_init,
    .instance_size = sizeof(TCGState),
};

static void register_accel_types(void)
{
    type_register_static(&tcg_accel_type);
}

type_init(register_accel_types);
