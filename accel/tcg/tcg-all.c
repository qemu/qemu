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
#include "sysemu/tcg.h"
#include "sysemu/replay.h"
#include "sysemu/cpu-timers.h"
#include "tcg/tcg.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/accel.h"
#include "qapi/qapi-builtin-visit.h"
#include "qemu/units.h"
#if !defined(CONFIG_USER_ONLY)
#include "hw/boards.h"
#endif
#include "internal.h"

struct TCGState {
    AccelState parent_obj;

    bool mttcg_enabled;
    int splitwx_enabled;
    unsigned long tb_size;
};
typedef struct TCGState TCGState;

#define TYPE_TCG_ACCEL ACCEL_CLASS_NAME("tcg")

DECLARE_INSTANCE_CHECKER(TCGState, TCG_STATE,
                         TYPE_TCG_ACCEL)

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
    if (icount_enabled() || TCG_OVERSIZED_GUEST) {
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

    /* If debugging enabled, default "auto on", otherwise off. */
#if defined(CONFIG_DEBUG_TCG) && !defined(CONFIG_USER_ONLY)
    s->splitwx_enabled = -1;
#else
    s->splitwx_enabled = 0;
#endif
}

bool mttcg_enabled;

static int tcg_init_machine(MachineState *ms)
{
    TCGState *s = TCG_STATE(current_accel());
#ifdef CONFIG_USER_ONLY
    unsigned max_cpus = 1;
#else
    unsigned max_cpus = ms->smp.max_cpus;
#endif

    tcg_allowed = true;
    mttcg_enabled = s->mttcg_enabled;

    page_init();
    tb_htable_init();
    tcg_init(s->tb_size * MiB, s->splitwx_enabled, max_cpus);

#if defined(CONFIG_SOFTMMU)
    /*
     * There's no guest base to take into account, so go ahead and
     * initialize the prologue now.
     */
    tcg_prologue_init(tcg_ctx);
#endif

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
        } else if (icount_enabled()) {
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

static bool tcg_get_splitwx(Object *obj, Error **errp)
{
    TCGState *s = TCG_STATE(obj);
    return s->splitwx_enabled;
}

static void tcg_set_splitwx(Object *obj, bool value, Error **errp)
{
    TCGState *s = TCG_STATE(obj);
    s->splitwx_enabled = value;
}

static int tcg_gdbstub_supported_sstep_flags(void)
{
    /*
     * In replay mode all events will come from the log and can't be
     * suppressed otherwise we would break determinism. However as those
     * events are tied to the number of executed instructions we won't see
     * them occurring every time we single step.
     */
    if (replay_mode != REPLAY_MODE_NONE) {
        return SSTEP_ENABLE;
    } else {
        return SSTEP_ENABLE | SSTEP_NOIRQ | SSTEP_NOTIMER;
    }
}

static void tcg_accel_class_init(ObjectClass *oc, void *data)
{
    AccelClass *ac = ACCEL_CLASS(oc);
    ac->name = "tcg";
    ac->init_machine = tcg_init_machine;
    ac->allowed = &tcg_allowed;
    ac->gdbstub_supported_sstep_flags = tcg_gdbstub_supported_sstep_flags;

    object_class_property_add_str(oc, "thread",
                                  tcg_get_thread,
                                  tcg_set_thread);

    object_class_property_add(oc, "tb-size", "int",
        tcg_get_tb_size, tcg_set_tb_size,
        NULL, NULL);
    object_class_property_set_description(oc, "tb-size",
        "TCG translation block cache size");

    object_class_property_add_bool(oc, "split-wx",
        tcg_get_splitwx, tcg_set_splitwx);
    object_class_property_set_description(oc, "split-wx",
        "Map jit pages into separate RW and RX regions");
}

static const TypeInfo tcg_accel_type = {
    .name = TYPE_TCG_ACCEL,
    .parent = TYPE_ACCEL,
    .instance_init = tcg_accel_instance_init,
    .class_init = tcg_accel_class_init,
    .instance_size = sizeof(TCGState),
};
module_obj(TYPE_TCG_ACCEL);

static void register_accel_types(void)
{
    type_register_static(&tcg_accel_type);
}

type_init(register_accel_types);
