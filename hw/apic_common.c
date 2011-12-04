/*
 *  APIC support - common bits of emulated and KVM kernel model
 *
 *  Copyright (c) 2004-2005 Fabrice Bellard
 *  Copyright (c) 2011      Jan Kiszka, Siemens AG
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */
#include "apic.h"
#include "apic_internal.h"
#include "trace.h"

static int apic_irq_delivered;

void cpu_set_apic_base(DeviceState *d, uint64_t val)
{
    APICCommonState *s = DO_UPCAST(APICCommonState, busdev.qdev, d);
    APICCommonInfo *info;

    trace_cpu_set_apic_base(val);

    if (s) {
        info = DO_UPCAST(APICCommonInfo, busdev.qdev, qdev_get_info(&s->busdev.qdev));
        info->set_base(s, val);
    }
}

uint64_t cpu_get_apic_base(DeviceState *d)
{
    APICCommonState *s = DO_UPCAST(APICCommonState, busdev.qdev, d);

    trace_cpu_get_apic_base(s ? (uint64_t)s->apicbase : 0);

    return s ? s->apicbase : 0;
}

void cpu_set_apic_tpr(DeviceState *d, uint8_t val)
{
    APICCommonState *s = DO_UPCAST(APICCommonState, busdev.qdev, d);
    APICCommonInfo *info;

    if (s) {
        info = DO_UPCAST(APICCommonInfo, busdev.qdev, qdev_get_info(&s->busdev.qdev));
        info->set_tpr(s, val);
    }
}

uint8_t cpu_get_apic_tpr(DeviceState *d)
{
    APICCommonState *s = DO_UPCAST(APICCommonState, busdev.qdev, d);

    return s ? s->tpr >> 4 : 0;
}

void apic_report_irq_delivered(int delivered)
{
    apic_irq_delivered += delivered;

    trace_apic_report_irq_delivered(apic_irq_delivered);
}

void apic_reset_irq_delivered(void)
{
    trace_apic_reset_irq_delivered(apic_irq_delivered);

    apic_irq_delivered = 0;
}

int apic_get_irq_delivered(void)
{
    trace_apic_get_irq_delivered(apic_irq_delivered);

    return apic_irq_delivered;
}

void apic_deliver_nmi(DeviceState *d)
{
    APICCommonState *s = DO_UPCAST(APICCommonState, busdev.qdev, d);
    APICCommonInfo *info;

    info = DO_UPCAST(APICCommonInfo, busdev.qdev, qdev_get_info(&s->busdev.qdev));
    info->external_nmi(s);
}

bool apic_next_timer(APICCommonState *s, int64_t current_time)
{
    int64_t d;

    /* We need to store the timer state separately to support APIC
     * implementations that maintain a non-QEMU timer, e.g. inside the
     * host kernel. This open-coded state allows us to migrate between
     * both models. */
    s->timer_expiry = -1;

    if (s->lvt[APIC_LVT_TIMER] & APIC_LVT_MASKED) {
        return false;
    }

    d = (current_time - s->initial_count_load_time) >> s->count_shift;

    if (s->lvt[APIC_LVT_TIMER] & APIC_LVT_TIMER_PERIODIC) {
        if (!s->initial_count) {
            return false;
        }
        d = ((d / ((uint64_t)s->initial_count + 1)) + 1) *
            ((uint64_t)s->initial_count + 1);
    } else {
        if (d >= s->initial_count) {
            return false;
        }
        d = (uint64_t)s->initial_count + 1;
    }
    s->next_time = s->initial_count_load_time + (d << s->count_shift);
    s->timer_expiry = s->next_time;
    return true;
}

void apic_init_reset(DeviceState *d)
{
    APICCommonState *s = DO_UPCAST(APICCommonState, busdev.qdev, d);
    int i;

    if (!s) {
        return;
    }
    s->tpr = 0;
    s->spurious_vec = 0xff;
    s->log_dest = 0;
    s->dest_mode = 0xf;
    memset(s->isr, 0, sizeof(s->isr));
    memset(s->tmr, 0, sizeof(s->tmr));
    memset(s->irr, 0, sizeof(s->irr));
    for (i = 0; i < APIC_LVT_NB; i++) {
        s->lvt[i] = APIC_LVT_MASKED;
    }
    s->esr = 0;
    memset(s->icr, 0, sizeof(s->icr));
    s->divide_conf = 0;
    s->count_shift = 0;
    s->initial_count = 0;
    s->initial_count_load_time = 0;
    s->next_time = 0;
    s->wait_for_sipi = 1;

    if (s->timer) {
        qemu_del_timer(s->timer);
    }
    s->timer_expiry = -1;
}

static void apic_reset_common(DeviceState *d)
{
    APICCommonState *s = DO_UPCAST(APICCommonState, busdev.qdev, d);
    bool bsp;

    bsp = cpu_is_bsp(s->cpu_env);
    s->apicbase = 0xfee00000 |
        (bsp ? MSR_IA32_APICBASE_BSP : 0) | MSR_IA32_APICBASE_ENABLE;

    apic_init_reset(d);

    if (bsp) {
        /*
         * LINT0 delivery mode on CPU #0 is set to ExtInt at initialization
         * time typically by BIOS, so PIC interrupt can be delivered to the
         * processor when local APIC is enabled.
         */
        s->lvt[APIC_LVT_LINT0] = 0x700;
    }
}

/* This function is only used for old state version 1 and 2 */
static int apic_load_old(QEMUFile *f, void *opaque, int version_id)
{
    APICCommonState *s = opaque;
    int i;

    if (version_id > 2) {
        return -EINVAL;
    }

    /* XXX: what if the base changes? (registered memory regions) */
    qemu_get_be32s(f, &s->apicbase);
    qemu_get_8s(f, &s->id);
    qemu_get_8s(f, &s->arb_id);
    qemu_get_8s(f, &s->tpr);
    qemu_get_be32s(f, &s->spurious_vec);
    qemu_get_8s(f, &s->log_dest);
    qemu_get_8s(f, &s->dest_mode);
    for (i = 0; i < 8; i++) {
        qemu_get_be32s(f, &s->isr[i]);
        qemu_get_be32s(f, &s->tmr[i]);
        qemu_get_be32s(f, &s->irr[i]);
    }
    for (i = 0; i < APIC_LVT_NB; i++) {
        qemu_get_be32s(f, &s->lvt[i]);
    }
    qemu_get_be32s(f, &s->esr);
    qemu_get_be32s(f, &s->icr[0]);
    qemu_get_be32s(f, &s->icr[1]);
    qemu_get_be32s(f, &s->divide_conf);
    s->count_shift = qemu_get_be32(f);
    qemu_get_be32s(f, &s->initial_count);
    s->initial_count_load_time = qemu_get_be64(f);
    s->next_time = qemu_get_be64(f);

    if (version_id >= 2) {
        qemu_get_timer(f, s->timer);
    }
    return 0;
}

static int apic_init_common(SysBusDevice *dev)
{
    APICCommonState *s = FROM_SYSBUS(APICCommonState, dev);
    APICCommonInfo *info;
    static int apic_no;

    if (apic_no >= MAX_APICS) {
        return -1;
    }
    s->idx = apic_no++;

    info = DO_UPCAST(APICCommonInfo, busdev.qdev, qdev_get_info(&s->busdev.qdev));
    info->init(s);

    sysbus_init_mmio(&s->busdev, &s->io_memory);
    return 0;
}

static int apic_dispatch_post_load(void *opaque, int version_id)
{
    APICCommonState *s = opaque;
    APICCommonInfo *info =
        DO_UPCAST(APICCommonInfo, busdev.qdev, qdev_get_info(&s->busdev.qdev));

    if (info->post_load) {
        info->post_load(s);
    }
    return 0;
}

static const VMStateDescription vmstate_apic_common = {
    .name = "apic",
    .version_id = 3,
    .minimum_version_id = 3,
    .minimum_version_id_old = 1,
    .load_state_old = apic_load_old,
    .post_load = apic_dispatch_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(apicbase, APICCommonState),
        VMSTATE_UINT8(id, APICCommonState),
        VMSTATE_UINT8(arb_id, APICCommonState),
        VMSTATE_UINT8(tpr, APICCommonState),
        VMSTATE_UINT32(spurious_vec, APICCommonState),
        VMSTATE_UINT8(log_dest, APICCommonState),
        VMSTATE_UINT8(dest_mode, APICCommonState),
        VMSTATE_UINT32_ARRAY(isr, APICCommonState, 8),
        VMSTATE_UINT32_ARRAY(tmr, APICCommonState, 8),
        VMSTATE_UINT32_ARRAY(irr, APICCommonState, 8),
        VMSTATE_UINT32_ARRAY(lvt, APICCommonState, APIC_LVT_NB),
        VMSTATE_UINT32(esr, APICCommonState),
        VMSTATE_UINT32_ARRAY(icr, APICCommonState, 2),
        VMSTATE_UINT32(divide_conf, APICCommonState),
        VMSTATE_INT32(count_shift, APICCommonState),
        VMSTATE_UINT32(initial_count, APICCommonState),
        VMSTATE_INT64(initial_count_load_time, APICCommonState),
        VMSTATE_INT64(next_time, APICCommonState),
        VMSTATE_INT64(timer_expiry,
                      APICCommonState), /* open-coded timer state */
        VMSTATE_END_OF_LIST()
    }
};

static Property apic_properties_common[] = {
    DEFINE_PROP_UINT8("id", APICCommonState, id, -1),
    DEFINE_PROP_PTR("cpu_env", APICCommonState, cpu_env),
    DEFINE_PROP_END_OF_LIST(),
};


void apic_qdev_register(APICCommonInfo *info)
{
    info->busdev.init = apic_init_common;
    info->busdev.qdev.size = sizeof(APICCommonState),
    info->busdev.qdev.vmsd = &vmstate_apic_common;
    info->busdev.qdev.reset = apic_reset_common;
    info->busdev.qdev.no_user = 1;
    info->busdev.qdev.props = apic_properties_common;
    sysbus_register_withprop(&info->busdev);
}
