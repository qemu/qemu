/*
 * WHPX platform APIC support
 *
 * Copyright (c) 2011 Siemens AG
 *
 * Authors:
 *  Jan Kiszka          <jan.kiszka@siemens.com>
 *  John Starks         <jostarks@microsoft.com>
 *
 * This work is licensed under the terms of the GNU GPL version 2.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/i386/apic_internal.h"
#include "hw/i386/apic-msidef.h"
#include "hw/pci/msi.h"
#include "sysemu/hw_accel.h"
#include "sysemu/whpx.h"
#include "whpx-internal.h"

struct whpx_lapic_state {
    struct {
        uint32_t data;
        uint32_t padding[3];
    } fields[256];
};

static void whpx_put_apic_state(APICCommonState *s,
                                struct whpx_lapic_state *kapic)
{
    int i;

    memset(kapic, 0, sizeof(*kapic));
    kapic->fields[0x2].data = s->id << 24;
    kapic->fields[0x3].data = s->version | ((APIC_LVT_NB - 1) << 16);
    kapic->fields[0x8].data = s->tpr;
    kapic->fields[0xd].data = s->log_dest << 24;
    kapic->fields[0xe].data = s->dest_mode << 28 | 0x0fffffff;
    kapic->fields[0xf].data = s->spurious_vec;
    for (i = 0; i < 8; i++) {
        kapic->fields[0x10 + i].data = s->isr[i];
        kapic->fields[0x18 + i].data = s->tmr[i];
        kapic->fields[0x20 + i].data = s->irr[i];
    }

    kapic->fields[0x28].data = s->esr;
    kapic->fields[0x30].data = s->icr[0];
    kapic->fields[0x31].data = s->icr[1];
    for (i = 0; i < APIC_LVT_NB; i++) {
        kapic->fields[0x32 + i].data = s->lvt[i];
    }

    kapic->fields[0x38].data = s->initial_count;
    kapic->fields[0x3e].data = s->divide_conf;
}

static void whpx_get_apic_state(APICCommonState *s,
                                struct whpx_lapic_state *kapic)
{
    int i, v;

    s->id = kapic->fields[0x2].data >> 24;
    s->tpr = kapic->fields[0x8].data;
    s->arb_id = kapic->fields[0x9].data;
    s->log_dest = kapic->fields[0xd].data >> 24;
    s->dest_mode = kapic->fields[0xe].data >> 28;
    s->spurious_vec = kapic->fields[0xf].data;
    for (i = 0; i < 8; i++) {
        s->isr[i] = kapic->fields[0x10 + i].data;
        s->tmr[i] = kapic->fields[0x18 + i].data;
        s->irr[i] = kapic->fields[0x20 + i].data;
    }

    s->esr = kapic->fields[0x28].data;
    s->icr[0] = kapic->fields[0x30].data;
    s->icr[1] = kapic->fields[0x31].data;
    for (i = 0; i < APIC_LVT_NB; i++) {
        s->lvt[i] = kapic->fields[0x32 + i].data;
    }

    s->initial_count = kapic->fields[0x38].data;
    s->divide_conf = kapic->fields[0x3e].data;

    v = (s->divide_conf & 3) | ((s->divide_conf >> 1) & 4);
    s->count_shift = (v + 1) & 7;

    s->initial_count_load_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    apic_next_timer(s, s->initial_count_load_time);
}

static void whpx_apic_set_base(APICCommonState *s, uint64_t val)
{
    s->apicbase = val;
}

static void whpx_put_apic_base(CPUState *cpu, uint64_t val)
{
    HRESULT hr;
    WHV_REGISTER_VALUE reg_value = {.Reg64 = val};
    WHV_REGISTER_NAME reg_name = WHvX64RegisterApicBase;

    hr = whp_dispatch.WHvSetVirtualProcessorRegisters(
             whpx_global.partition,
             cpu->cpu_index,
             &reg_name, 1,
             &reg_value);

    if (FAILED(hr)) {
        error_report("WHPX: Failed to set MSR APIC base, hr=%08lx", hr);
    }
}

static void whpx_apic_set_tpr(APICCommonState *s, uint8_t val)
{
    s->tpr = val;
}

static uint8_t whpx_apic_get_tpr(APICCommonState *s)
{
    return s->tpr;
}

static void whpx_apic_vapic_base_update(APICCommonState *s)
{
    /* not implemented yet */
}

static void whpx_apic_put(CPUState *cs, run_on_cpu_data data)
{
    APICCommonState *s = data.host_ptr;
    struct whpx_lapic_state kapic;
    HRESULT hr;

    whpx_put_apic_base(CPU(s->cpu), s->apicbase);
    whpx_put_apic_state(s, &kapic);

    hr = whp_dispatch.WHvSetVirtualProcessorInterruptControllerState2(
        whpx_global.partition,
        cs->cpu_index,
        &kapic,
        sizeof(kapic));
    if (FAILED(hr)) {
        fprintf(stderr,
            "WHvSetVirtualProcessorInterruptControllerState failed: %08lx\n",
             hr);

        abort();
    }
}

void whpx_apic_get(DeviceState *dev)
{
    APICCommonState *s = APIC_COMMON(dev);
    CPUState *cpu = CPU(s->cpu);
    struct whpx_lapic_state kapic;

    HRESULT hr = whp_dispatch.WHvGetVirtualProcessorInterruptControllerState2(
        whpx_global.partition,
        cpu->cpu_index,
        &kapic,
        sizeof(kapic),
        NULL);
    if (FAILED(hr)) {
        fprintf(stderr,
            "WHvSetVirtualProcessorInterruptControllerState failed: %08lx\n",
            hr);

        abort();
    }

    whpx_get_apic_state(s, &kapic);
}

static void whpx_apic_post_load(APICCommonState *s)
{
    run_on_cpu(CPU(s->cpu), whpx_apic_put, RUN_ON_CPU_HOST_PTR(s));
}

static void whpx_apic_external_nmi(APICCommonState *s)
{
}

static void whpx_send_msi(MSIMessage *msg)
{
    uint64_t addr = msg->address;
    uint32_t data = msg->data;
    uint8_t dest = (addr & MSI_ADDR_DEST_ID_MASK) >> MSI_ADDR_DEST_ID_SHIFT;
    uint8_t vector = (data & MSI_DATA_VECTOR_MASK) >> MSI_DATA_VECTOR_SHIFT;
    uint8_t dest_mode = (addr >> MSI_ADDR_DEST_MODE_SHIFT) & 0x1;
    uint8_t trigger_mode = (data >> MSI_DATA_TRIGGER_SHIFT) & 0x1;
    uint8_t delivery = (data >> MSI_DATA_DELIVERY_MODE_SHIFT) & 0x7;

    WHV_INTERRUPT_CONTROL interrupt = {
        /* Values correspond to delivery modes */
        .Type = delivery,
        .DestinationMode = dest_mode ?
            WHvX64InterruptDestinationModeLogical :
            WHvX64InterruptDestinationModePhysical,

        .TriggerMode = trigger_mode ?
            WHvX64InterruptTriggerModeLevel : WHvX64InterruptTriggerModeEdge,
        .Reserved = 0,
        .Vector = vector,
        .Destination = dest,
    };
    HRESULT hr = whp_dispatch.WHvRequestInterrupt(whpx_global.partition,
                     &interrupt, sizeof(interrupt));
    if (FAILED(hr)) {
        fprintf(stderr, "whpx: injection failed, MSI (%llx, %x) delivery: %d, "
                "dest_mode: %d, trigger mode: %d, vector: %d, lost (%08lx)\n",
                addr, data, delivery, dest_mode, trigger_mode, vector, hr);
    }
}

static uint64_t whpx_apic_mem_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    return ~(uint64_t)0;
}

static void whpx_apic_mem_write(void *opaque, hwaddr addr,
                                uint64_t data, unsigned size)
{
    MSIMessage msg = { .address = addr, .data = data };
    whpx_send_msi(&msg);
}

static const MemoryRegionOps whpx_apic_io_ops = {
    .read = whpx_apic_mem_read,
    .write = whpx_apic_mem_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void whpx_apic_reset(APICCommonState *s)
{
    /* Not used by WHPX. */
    s->wait_for_sipi = 0;

    run_on_cpu(CPU(s->cpu), whpx_apic_put, RUN_ON_CPU_HOST_PTR(s));
}

static void whpx_apic_realize(DeviceState *dev, Error **errp)
{
    APICCommonState *s = APIC_COMMON(dev);

    memory_region_init_io(&s->io_memory, OBJECT(s), &whpx_apic_io_ops, s,
                          "whpx-apic-msi", APIC_SPACE_SIZE);

    msi_nonbroken = true;
}

static void whpx_apic_class_init(ObjectClass *klass, void *data)
{
    APICCommonClass *k = APIC_COMMON_CLASS(klass);

    k->realize = whpx_apic_realize;
    k->reset = whpx_apic_reset;
    k->set_base = whpx_apic_set_base;
    k->set_tpr = whpx_apic_set_tpr;
    k->get_tpr = whpx_apic_get_tpr;
    k->post_load = whpx_apic_post_load;
    k->vapic_base_update = whpx_apic_vapic_base_update;
    k->external_nmi = whpx_apic_external_nmi;
    k->send_msi = whpx_send_msi;
}

static const TypeInfo whpx_apic_info = {
    .name = "whpx-apic",
    .parent = TYPE_APIC_COMMON,
    .instance_size = sizeof(APICCommonState),
    .class_init = whpx_apic_class_init,
};

static void whpx_apic_register_types(void)
{
    type_register_static(&whpx_apic_info);
}

type_init(whpx_apic_register_types)
