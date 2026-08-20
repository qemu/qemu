/*
 * MSHV in-kernel APIC support
 *
 * Copyright Microsoft, Corp. 2026
 *
 * Authors: Magnus Kulke <magnuskulke@microsoft.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/memalign.h"
#include "qemu/error-report.h"
#include "hw/i386/apic_internal.h"
#include "hw/i386/apic-msidef.h"
#include "hw/pci/msi.h"
#include "migration/vmstate.h"
#include "qemu/typedefs.h"
#include "system/hw_accel.h"
#include "system/mshv.h"
#include "system/mshv_int.h"

typedef struct hv_local_interrupt_controller_state
               hv_local_interrupt_controller_state;

#define TYPE_MSHV_APIC "mshv-apic"
OBJECT_DECLARE_SIMPLE_TYPE(MshvAPICState, MSHV_APIC)

struct MshvAPICState {
    APICCommonState parent_obj;

    uint32_t apic_version;
    uint32_t apic_lvt_cmci;
    uint32_t apic_error_status;
    uint32_t apic_counter_value;
    uint32_t apic_remote_read;
};

static int get_lapic(int cpu_fd,
                     struct hv_local_interrupt_controller_state *state)
{
    int ret;
    size_t size = 4096;
    /* buffer aligned to 4k, as *state requires that */
    void *buffer = qemu_memalign(size, size);
    struct mshv_get_set_vp_state mshv_state = { 0 };

    mshv_state.buf_ptr = (uint64_t) buffer;
    mshv_state.buf_sz = size;
    mshv_state.type = MSHV_VP_STATE_LAPIC;

    ret = mshv_get_vp_state(cpu_fd, &mshv_state);
    if (ret == 0) {
        memcpy(state, buffer, sizeof(*state));
    }
    qemu_vfree(buffer);
    if (ret < 0) {
        error_report("failed to get lapic");
        return -1;
    }

    return 0;
}

static int set_lapic(int cpu_fd,
                     const struct hv_local_interrupt_controller_state *state)
{
    int ret;
    size_t size = 4096;
    /* buffer aligned to 4k, as *state requires that */
    void *buffer = qemu_memalign(size, size);
    struct mshv_get_set_vp_state mshv_state = { 0 };

    if (!state) {
        error_report("lapic state is NULL");
        return -1;
    }
    memcpy(buffer, state, sizeof(*state));

    mshv_state.buf_ptr = (uint64_t) buffer;
    mshv_state.buf_sz = size;
    mshv_state.type = MSHV_VP_STATE_LAPIC;

    ret = mshv_set_vp_state(cpu_fd, &mshv_state);
    qemu_vfree(buffer);
    if (ret < 0) {
        error_report("failed to set lapic: %s", strerror(errno));
        return -1;
    }

    return 0;
}

static void populate_apic_state(CPUState *cpu,
                                const hv_local_interrupt_controller_state *hv)
{
    X86CPU *x86cpu = X86_CPU(cpu);
    MshvAPICState *ms = MSHV_APIC(x86cpu->apic_state);
    APICCommonState *s = &ms->parent_obj;
    size_t i;

    /*
     * x2APIC:
     * - APIC ID is the full 32-bit initial_apic_id
     * - LDR is read-only, architecturally derived from the ID
     * - DFR does not exist in x2APIC mode
     */
    if (is_x2apic_mode(s)) {
        s->initial_apic_id = hv->apic_id;
    } else {
        s->id        = hv->apic_id  >> 24;
        s->log_dest  = hv->apic_ldr >> 24;
        s->dest_mode = hv->apic_dfr >> 28;
    }
    ms->apic_version = hv->apic_version;
    s->spurious_vec = hv->apic_spurious;
    for (i = 0; i < 8; i++) {
        s->isr[i] = hv->apic_isr[i];
        s->tmr[i] = hv->apic_tmr[i];
        s->irr[i] = hv->apic_irr[i];
    }
    s->esr = hv->apic_esr;
    s->icr[1] = hv->apic_icr_high;
    s->icr[0] = hv->apic_icr_low;

    s->lvt[APIC_LVT_TIMER]   = hv->apic_lvt_timer;
    s->lvt[APIC_LVT_THERMAL] = hv->apic_lvt_thermal;
    s->lvt[APIC_LVT_PERFORM] = hv->apic_lvt_perfmon;
    s->lvt[APIC_LVT_LINT0]   = hv->apic_lvt_lint0;
    s->lvt[APIC_LVT_LINT1]   = hv->apic_lvt_lint1;
    s->lvt[APIC_LVT_ERROR]   = hv->apic_lvt_error;
    ms->apic_lvt_cmci        = hv->apic_lvt_cmci;

    ms->apic_error_status  = hv->apic_error_status;
    s->initial_count       = hv->apic_initial_count;
    ms->apic_counter_value = hv->apic_counter_value;
    s->divide_conf         = hv->apic_divide_configuration;
    ms->apic_remote_read   = hv->apic_remote_read;
}

static uint32_t set_apic_delivery_mode(uint32_t reg, uint32_t mode)
{
    return ((reg) & ~0x700) | ((mode) << 8);
}

int mshv_init_lint(CPUState *cpu)
{
    uint32_t *lvt_lint0, *lvt_lint1;
    int cpu_fd = mshv_vcpufd(cpu);
    int ret;
    struct hv_local_interrupt_controller_state lapic_state = { 0 };

    ret = get_lapic(cpu_fd, &lapic_state);
    if (ret < 0) {
        return ret;
    }

    lvt_lint0 = &lapic_state.apic_lvt_lint0;
    *lvt_lint0 = set_apic_delivery_mode(*lvt_lint0, APIC_DM_EXTINT);

    lvt_lint1 = &lapic_state.apic_lvt_lint1;
    *lvt_lint1 = set_apic_delivery_mode(*lvt_lint1, APIC_DM_NMI);

    /* TODO: should we skip setting lapic if the values are the same? */

    ret = set_lapic(cpu_fd, &lapic_state);
    if (ret < 0) {
        return -1;
    }

    populate_apic_state(cpu, &lapic_state);

    return 0;
}

static void populate_hv_lapic_state(hv_local_interrupt_controller_state *hv,
                                    const CPUState *cpu)
{
    uint32_t x2apic_id;
    X86CPU *x86cpu = X86_CPU(cpu);
    MshvAPICState *ms = MSHV_APIC(x86cpu->apic_state);
    APICCommonState *s = &ms->parent_obj;
    size_t i;

    /*
     * x2APIC:
     * - APIC ID is the full 32-bit initial_apic_id
     * - LDR is read-only, architecturally derived from the ID
     * - DFR does not exist in x2APIC mode
     */
    if (is_x2apic_mode(s)) {
        x2apic_id = s->initial_apic_id;

        hv->apic_id  = x2apic_id;
        hv->apic_ldr = ((x2apic_id >> 4) << 16) | (1 << (x2apic_id & 0xf));
        hv->apic_dfr = 0;
    } else {
        hv->apic_id  = s->id << 24;
        hv->apic_ldr = s->log_dest << 24;
        hv->apic_dfr = s->dest_mode << 28 | 0x0fffffff;
    }
    hv->apic_version = ms->apic_version;
    hv->apic_spurious = s->spurious_vec;
    for (i = 0; i < 8; i++) {
        hv->apic_isr[i] = s->isr[i];
        hv->apic_tmr[i] = s->tmr[i];
        hv->apic_irr[i] = s->irr[i];
    }
    hv->apic_esr      = s->esr;
    hv->apic_icr_high = s->icr[1];
    hv->apic_icr_low  = s->icr[0];

    hv->apic_lvt_timer   = s->lvt[APIC_LVT_TIMER];
    hv->apic_lvt_thermal = s->lvt[APIC_LVT_THERMAL];
    hv->apic_lvt_perfmon = s->lvt[APIC_LVT_PERFORM];
    hv->apic_lvt_lint0   = s->lvt[APIC_LVT_LINT0];
    hv->apic_lvt_lint1   = s->lvt[APIC_LVT_LINT1];
    hv->apic_lvt_error   = s->lvt[APIC_LVT_ERROR];
    hv->apic_lvt_cmci    = ms->apic_lvt_cmci;

    hv->apic_error_status         = ms->apic_error_status;
    hv->apic_initial_count        = s->initial_count;
    hv->apic_counter_value        = ms->apic_counter_value;
    hv->apic_divide_configuration = s->divide_conf;
    hv->apic_remote_read          = ms->apic_remote_read;
}

int mshv_set_lapic(const CPUState *cpu)
{
    int cpu_fd = mshv_vcpufd(cpu);
    struct hv_local_interrupt_controller_state lapic_state = { 0 };

    populate_hv_lapic_state(&lapic_state, cpu);

    return set_lapic(cpu_fd, &lapic_state);
}

int mshv_get_lapic(CPUState *cpu)
{
    int cpu_fd = mshv_vcpufd(cpu);
    int ret;
    struct hv_local_interrupt_controller_state lapic_state = { 0 };

    ret = get_lapic(cpu_fd, &lapic_state);
    if (ret < 0) {
        return -1;
    }

    populate_apic_state(cpu, &lapic_state);

    return 0;
}

static int mshv_apic_set_base(APICCommonState *s, uint64_t val)
{
    s->apicbase = val;

    return 0;
}

static void mshv_apic_set_tpr(APICCommonState *s, uint8_t val)
{
    s->tpr = (val & APIC_PR_SUB_CLASS) << APIC_PR_CLASS_SHIFT;
}

static uint8_t mshv_apic_get_tpr(APICCommonState *s)
{
    return s->tpr >> APIC_PR_CLASS_SHIFT;
}

static void mshv_apic_external_nmi(APICCommonState *s)
{
}

static void mshv_apic_vapic_base_update(APICCommonState *s)
{
}

static void mshv_send_msi(MSIMessage *msi)
{
    uint64_t addr;
    uint32_t data, dest;
    uint8_t vector, dest_mode, trigger_mode, delivery;

    addr         = msi->address;
    data         = msi->data;
    dest         = (addr & MSI_ADDR_DEST_ID_MASK) >> MSI_ADDR_DEST_ID_SHIFT |
                   (addr >> 32);
    vector       = (data & MSI_DATA_VECTOR_MASK) >> MSI_DATA_VECTOR_SHIFT;
    dest_mode    = (addr >> MSI_ADDR_DEST_MODE_SHIFT) & 0x1;
    trigger_mode = (data >> MSI_DATA_TRIGGER_SHIFT) & 0x1;
    delivery     = (data >> MSI_DATA_DELIVERY_MODE_SHIFT) &
                   MSI_DATA_DELIVERY_MODE_MASK;

    /*
     * Vector 0 is not a valid interrupt vector (0-15 are reserved for CPU
     * exceptions). This can trigger during machine reset, if hpet_reset()
     * forces the PIT to pulse GSI 2 before IOAPIC's own reset has masked its
     * redirection entries.
     */
    if (vector == 0) {
        return;
    }

    mshv_request_interrupt(mshv_state, delivery, vector, dest, dest_mode,
                           trigger_mode);
}

static uint64_t mshv_apic_mem_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    return UINT64_MAX;
}

static void mshv_apic_mem_write(void *opaque, hwaddr addr,
                                uint64_t data, unsigned size)
{
    MSIMessage msg = { .address = addr, .data = data };

    mshv_send_msi(&msg);
}

static const MemoryRegionOps mshv_apic_io_ops = {
    .read = mshv_apic_mem_read,
    .write = mshv_apic_mem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void mshv_apic_reset(APICCommonState *s)
{
    s->wait_for_sipi = 0;
}

static const VMStateDescription vmstate_mshv_apic = {
    .name = "mshv-apic",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(apic_version, MshvAPICState),
        VMSTATE_UINT32(apic_lvt_cmci, MshvAPICState),
        VMSTATE_UINT32(apic_error_status, MshvAPICState),
        VMSTATE_UINT32(apic_counter_value, MshvAPICState),
        VMSTATE_UINT32(apic_remote_read, MshvAPICState),
        VMSTATE_END_OF_LIST()
    }
};

static void mshv_apic_realize(DeviceState *dev, Error **errp)
{
    APICCommonState *s = APIC_COMMON(dev);
    MshvAPICState *ms = MSHV_APIC(dev);

    memory_region_init_io(&s->io_memory, OBJECT(s), &mshv_apic_io_ops, s,
                          "mshv-apic-msi", APIC_SPACE_SIZE);

    msi_nonbroken = true;

    /*
     * We register this state explicity, rather than going via dc->vmsd.
     * The auto-wiring would register the state with
     * instance_id == VMSTATE_INSTANCE_ID_ANY, which for the APIC doesn't
     * work, b/c the ID carries semantic meaning for restoring the state
     * on the destination (which vcpu it belongs to).
     */
    vmstate_register_with_alias_id(NULL,
                                   s->initial_apic_id, &vmstate_mshv_apic, ms,
                                   -1, 0, NULL);
}

static void mshv_apic_unrealize(DeviceState *dev)
{
    MshvAPICState *ms = MSHV_APIC(dev);

    vmstate_unregister(NULL, &vmstate_mshv_apic, ms);
}

static void mshv_apic_class_init(ObjectClass *klass, const void *data)
{
    APICCommonClass *k = APIC_COMMON_CLASS(klass);

    k->realize = mshv_apic_realize;
    k->unrealize = mshv_apic_unrealize;
    k->reset = mshv_apic_reset;
    k->set_base = mshv_apic_set_base;
    k->set_tpr = mshv_apic_set_tpr;
    k->get_tpr = mshv_apic_get_tpr;
    k->external_nmi = mshv_apic_external_nmi;
    k->vapic_base_update = mshv_apic_vapic_base_update;
    k->send_msi = mshv_send_msi;
}

static const TypeInfo mshv_apic_info = {
    .name = TYPE_MSHV_APIC,
    .parent = TYPE_APIC_COMMON,
    .instance_size = sizeof(MshvAPICState),
    .class_init = mshv_apic_class_init,
};

static void mshv_apic_register_types(void)
{
    type_register_static(&mshv_apic_info);
}

type_init(mshv_apic_register_types)
