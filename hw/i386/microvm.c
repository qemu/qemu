/*
 * Copyright (c) 2018 Intel Corporation
 * Copyright (c) 2019 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qapi/qapi-visit-common.h"
#include "sysemu/sysemu.h"
#include "sysemu/cpus.h"
#include "sysemu/numa.h"
#include "sysemu/reset.h"

#include "hw/loader.h"
#include "hw/irq.h"
#include "hw/kvm/clock.h"
#include "hw/i386/microvm.h"
#include "hw/i386/x86.h"
#include "hw/i386/pc.h"
#include "target/i386/cpu.h"
#include "hw/timer/i8254.h"
#include "hw/rtc/mc146818rtc.h"
#include "hw/char/serial.h"
#include "hw/i386/topology.h"
#include "hw/i386/e820_memory_layout.h"
#include "hw/i386/fw_cfg.h"
#include "hw/virtio/virtio-mmio.h"

#include "cpu.h"
#include "elf.h"
#include "kvm_i386.h"
#include "hw/xen/start_info.h"

#define MICROVM_BIOS_FILENAME "bios-microvm.bin"

static void microvm_set_rtc(MicrovmMachineState *mms, ISADevice *s)
{
    X86MachineState *x86ms = X86_MACHINE(mms);
    int val;

    val = MIN(x86ms->below_4g_mem_size / KiB, 640);
    rtc_set_memory(s, 0x15, val);
    rtc_set_memory(s, 0x16, val >> 8);
    /* extended memory (next 64MiB) */
    if (x86ms->below_4g_mem_size > 1 * MiB) {
        val = (x86ms->below_4g_mem_size - 1 * MiB) / KiB;
    } else {
        val = 0;
    }
    if (val > 65535) {
        val = 65535;
    }
    rtc_set_memory(s, 0x17, val);
    rtc_set_memory(s, 0x18, val >> 8);
    rtc_set_memory(s, 0x30, val);
    rtc_set_memory(s, 0x31, val >> 8);
    /* memory between 16MiB and 4GiB */
    if (x86ms->below_4g_mem_size > 16 * MiB) {
        val = (x86ms->below_4g_mem_size - 16 * MiB) / (64 * KiB);
    } else {
        val = 0;
    }
    if (val > 65535) {
        val = 65535;
    }
    rtc_set_memory(s, 0x34, val);
    rtc_set_memory(s, 0x35, val >> 8);
    /* memory above 4GiB */
    val = x86ms->above_4g_mem_size / 65536;
    rtc_set_memory(s, 0x5b, val);
    rtc_set_memory(s, 0x5c, val >> 8);
    rtc_set_memory(s, 0x5d, val >> 16);
}

static void microvm_gsi_handler(void *opaque, int n, int level)
{
    GSIState *s = opaque;

    qemu_set_irq(s->ioapic_irq[n], level);
}

static void microvm_devices_init(MicrovmMachineState *mms)
{
    X86MachineState *x86ms = X86_MACHINE(mms);
    ISABus *isa_bus;
    ISADevice *rtc_state;
    GSIState *gsi_state;
    int i;

    /* Core components */

    gsi_state = g_malloc0(sizeof(*gsi_state));
    if (mms->pic == ON_OFF_AUTO_ON || mms->pic == ON_OFF_AUTO_AUTO) {
        x86ms->gsi = qemu_allocate_irqs(gsi_handler, gsi_state, GSI_NUM_PINS);
    } else {
        x86ms->gsi = qemu_allocate_irqs(microvm_gsi_handler,
                                        gsi_state, GSI_NUM_PINS);
    }

    isa_bus = isa_bus_new(NULL, get_system_memory(), get_system_io(),
                          &error_abort);
    isa_bus_irqs(isa_bus, x86ms->gsi);

    ioapic_init_gsi(gsi_state, "machine");

    kvmclock_create();

    for (i = 0; i < VIRTIO_NUM_TRANSPORTS; i++) {
        sysbus_create_simple("virtio-mmio",
                             VIRTIO_MMIO_BASE + i * 512,
                             x86ms->gsi[VIRTIO_IRQ_BASE + i]);
    }

    /* Optional and legacy devices */

    if (mms->pic == ON_OFF_AUTO_ON || mms->pic == ON_OFF_AUTO_AUTO) {
        qemu_irq *i8259;

        i8259 = i8259_init(isa_bus, pc_allocate_cpu_irq());
        for (i = 0; i < ISA_NUM_IRQS; i++) {
            gsi_state->i8259_irq[i] = i8259[i];
        }
        g_free(i8259);
    }

    if (mms->pit == ON_OFF_AUTO_ON || mms->pit == ON_OFF_AUTO_AUTO) {
        if (kvm_pit_in_kernel()) {
            kvm_pit_init(isa_bus, 0x40);
        } else {
            i8254_pit_init(isa_bus, 0x40, 0, NULL);
        }
    }

    if (mms->rtc == ON_OFF_AUTO_ON ||
        (mms->rtc == ON_OFF_AUTO_AUTO && !kvm_enabled())) {
        rtc_state = mc146818_rtc_init(isa_bus, 2000, NULL);
        microvm_set_rtc(mms, rtc_state);
    }

    if (mms->isa_serial) {
        serial_hds_isa_init(isa_bus, 0, 1);
    }

    if (bios_name == NULL) {
        bios_name = MICROVM_BIOS_FILENAME;
    }
    x86_bios_rom_init(get_system_memory(), true);
}

static void microvm_memory_init(MicrovmMachineState *mms)
{
    MachineState *machine = MACHINE(mms);
    X86MachineState *x86ms = X86_MACHINE(mms);
    MemoryRegion *ram, *ram_below_4g, *ram_above_4g;
    MemoryRegion *system_memory = get_system_memory();
    FWCfgState *fw_cfg;
    ram_addr_t lowmem;
    int i;

    /*
     * Check whether RAM fits below 4G (leaving 1/2 GByte for IO memory
     * and 256 Mbytes for PCI Express Enhanced Configuration Access Mapping
     * also known as MMCFG).
     * If it doesn't, we need to split it in chunks below and above 4G.
     * In any case, try to make sure that guest addresses aligned at
     * 1G boundaries get mapped to host addresses aligned at 1G boundaries.
     */
    if (machine->ram_size >= 0xb0000000) {
        lowmem = 0x80000000;
    } else {
        lowmem = 0xb0000000;
    }

    /*
     * Handle the machine opt max-ram-below-4g.  It is basically doing
     * min(qemu limit, user limit).
     */
    if (!x86ms->max_ram_below_4g) {
        x86ms->max_ram_below_4g = 4 * GiB;
    }
    if (lowmem > x86ms->max_ram_below_4g) {
        lowmem = x86ms->max_ram_below_4g;
        if (machine->ram_size - lowmem > lowmem &&
            lowmem & (1 * GiB - 1)) {
            warn_report("There is possibly poor performance as the ram size "
                        " (0x%" PRIx64 ") is more then twice the size of"
                        " max-ram-below-4g (%"PRIu64") and"
                        " max-ram-below-4g is not a multiple of 1G.",
                        (uint64_t)machine->ram_size, x86ms->max_ram_below_4g);
        }
    }

    if (machine->ram_size > lowmem) {
        x86ms->above_4g_mem_size = machine->ram_size - lowmem;
        x86ms->below_4g_mem_size = lowmem;
    } else {
        x86ms->above_4g_mem_size = 0;
        x86ms->below_4g_mem_size = machine->ram_size;
    }

    ram = g_malloc(sizeof(*ram));
    memory_region_allocate_system_memory(ram, NULL, "microvm.ram",
                                         machine->ram_size);

    ram_below_4g = g_malloc(sizeof(*ram_below_4g));
    memory_region_init_alias(ram_below_4g, NULL, "ram-below-4g", ram,
                             0, x86ms->below_4g_mem_size);
    memory_region_add_subregion(system_memory, 0, ram_below_4g);

    e820_add_entry(0, x86ms->below_4g_mem_size, E820_RAM);

    if (x86ms->above_4g_mem_size > 0) {
        ram_above_4g = g_malloc(sizeof(*ram_above_4g));
        memory_region_init_alias(ram_above_4g, NULL, "ram-above-4g", ram,
                                 x86ms->below_4g_mem_size,
                                 x86ms->above_4g_mem_size);
        memory_region_add_subregion(system_memory, 0x100000000ULL,
                                    ram_above_4g);
        e820_add_entry(0x100000000ULL, x86ms->above_4g_mem_size, E820_RAM);
    }

    fw_cfg = fw_cfg_init_io_dma(FW_CFG_IO_BASE, FW_CFG_IO_BASE + 4,
                                &address_space_memory);

    fw_cfg_add_i16(fw_cfg, FW_CFG_NB_CPUS, machine->smp.cpus);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MAX_CPUS, machine->smp.max_cpus);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, (uint64_t)machine->ram_size);
    fw_cfg_add_i32(fw_cfg, FW_CFG_IRQ0_OVERRIDE, kvm_allows_irq0_override());
    fw_cfg_add_bytes(fw_cfg, FW_CFG_E820_TABLE,
                     &e820_reserve, sizeof(e820_reserve));
    fw_cfg_add_file(fw_cfg, "etc/e820", e820_table,
                    sizeof(struct e820_entry) * e820_get_num_entries());

    rom_set_fw(fw_cfg);

    if (machine->kernel_filename != NULL) {
        x86_load_linux(x86ms, fw_cfg, 0, true, true);
    }

    if (mms->option_roms) {
        for (i = 0; i < nb_option_roms; i++) {
            rom_add_option(option_rom[i].name, option_rom[i].bootindex);
        }
    }

    x86ms->fw_cfg = fw_cfg;
    x86ms->ioapic_as = &address_space_memory;
}

static gchar *microvm_get_mmio_cmdline(gchar *name)
{
    gchar *cmdline;
    gchar *separator;
    long int index;
    int ret;

    separator = g_strrstr(name, ".");
    if (!separator) {
        return NULL;
    }

    if (qemu_strtol(separator + 1, NULL, 10, &index) != 0) {
        return NULL;
    }

    cmdline = g_malloc0(VIRTIO_CMDLINE_MAXLEN);
    ret = g_snprintf(cmdline, VIRTIO_CMDLINE_MAXLEN,
                     " virtio_mmio.device=512@0x%lx:%ld",
                     VIRTIO_MMIO_BASE + index * 512,
                     VIRTIO_IRQ_BASE + index);
    if (ret < 0 || ret >= VIRTIO_CMDLINE_MAXLEN) {
        g_free(cmdline);
        return NULL;
    }

    return cmdline;
}

static void microvm_fix_kernel_cmdline(MachineState *machine)
{
    X86MachineState *x86ms = X86_MACHINE(machine);
    BusState *bus;
    BusChild *kid;
    char *cmdline;

    /*
     * Find MMIO transports with attached devices, and add them to the kernel
     * command line.
     *
     * Yes, this is a hack, but one that heavily improves the UX without
     * introducing any significant issues.
     */
    cmdline = g_strdup(machine->kernel_cmdline);
    bus = sysbus_get_default();
    QTAILQ_FOREACH(kid, &bus->children, sibling) {
        DeviceState *dev = kid->child;
        ObjectClass *class = object_get_class(OBJECT(dev));

        if (class == object_class_by_name(TYPE_VIRTIO_MMIO)) {
            VirtIOMMIOProxy *mmio = VIRTIO_MMIO(OBJECT(dev));
            VirtioBusState *mmio_virtio_bus = &mmio->bus;
            BusState *mmio_bus = &mmio_virtio_bus->parent_obj;

            if (!QTAILQ_EMPTY(&mmio_bus->children)) {
                gchar *mmio_cmdline = microvm_get_mmio_cmdline(mmio_bus->name);
                if (mmio_cmdline) {
                    char *newcmd = g_strjoin(NULL, cmdline, mmio_cmdline, NULL);
                    g_free(mmio_cmdline);
                    g_free(cmdline);
                    cmdline = newcmd;
                }
            }
        }
    }

    fw_cfg_modify_i32(x86ms->fw_cfg, FW_CFG_CMDLINE_SIZE, strlen(cmdline) + 1);
    fw_cfg_modify_string(x86ms->fw_cfg, FW_CFG_CMDLINE_DATA, cmdline);

    g_free(cmdline);
}

static void microvm_machine_state_init(MachineState *machine)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(machine);
    X86MachineState *x86ms = X86_MACHINE(machine);
    Error *local_err = NULL;

    microvm_memory_init(mms);

    x86_cpus_init(x86ms, CPU_VERSION_LATEST);
    if (local_err) {
        error_report_err(local_err);
        exit(1);
    }

    microvm_devices_init(mms);
}

static void microvm_machine_reset(MachineState *machine)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(machine);
    CPUState *cs;
    X86CPU *cpu;

    if (machine->kernel_filename != NULL &&
        mms->auto_kernel_cmdline && !mms->kernel_cmdline_fixed) {
        microvm_fix_kernel_cmdline(machine);
        mms->kernel_cmdline_fixed = true;
    }

    qemu_devices_reset();

    CPU_FOREACH(cs) {
        cpu = X86_CPU(cs);

        if (cpu->apic_state) {
            device_reset(cpu->apic_state);
        }
    }
}

static void microvm_machine_get_pic(Object *obj, Visitor *v, const char *name,
                                    void *opaque, Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);
    OnOffAuto pic = mms->pic;

    visit_type_OnOffAuto(v, name, &pic, errp);
}

static void microvm_machine_set_pic(Object *obj, Visitor *v, const char *name,
                                    void *opaque, Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &mms->pic, errp);
}

static void microvm_machine_get_pit(Object *obj, Visitor *v, const char *name,
                                    void *opaque, Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);
    OnOffAuto pit = mms->pit;

    visit_type_OnOffAuto(v, name, &pit, errp);
}

static void microvm_machine_set_pit(Object *obj, Visitor *v, const char *name,
                                    void *opaque, Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &mms->pit, errp);
}

static void microvm_machine_get_rtc(Object *obj, Visitor *v, const char *name,
                                    void *opaque, Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);
    OnOffAuto rtc = mms->rtc;

    visit_type_OnOffAuto(v, name, &rtc, errp);
}

static void microvm_machine_set_rtc(Object *obj, Visitor *v, const char *name,
                                    void *opaque, Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &mms->rtc, errp);
}

static bool microvm_machine_get_isa_serial(Object *obj, Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);

    return mms->isa_serial;
}

static void microvm_machine_set_isa_serial(Object *obj, bool value,
                                           Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);

    mms->isa_serial = value;
}

static bool microvm_machine_get_option_roms(Object *obj, Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);

    return mms->option_roms;
}

static void microvm_machine_set_option_roms(Object *obj, bool value,
                                            Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);

    mms->option_roms = value;
}

static bool microvm_machine_get_auto_kernel_cmdline(Object *obj, Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);

    return mms->auto_kernel_cmdline;
}

static void microvm_machine_set_auto_kernel_cmdline(Object *obj, bool value,
                                                    Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);

    mms->auto_kernel_cmdline = value;
}

static void microvm_machine_initfn(Object *obj)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);

    /* Configuration */
    mms->pic = ON_OFF_AUTO_AUTO;
    mms->pit = ON_OFF_AUTO_AUTO;
    mms->rtc = ON_OFF_AUTO_AUTO;
    mms->isa_serial = true;
    mms->option_roms = true;
    mms->auto_kernel_cmdline = true;

    /* State */
    mms->kernel_cmdline_fixed = false;
}

static void microvm_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = microvm_machine_state_init;

    mc->family = "microvm_i386";
    mc->desc = "microvm (i386)";
    mc->units_per_default_bus = 1;
    mc->no_floppy = 1;
    mc->max_cpus = 288;
    mc->has_hotpluggable_cpus = false;
    mc->auto_enable_numa_with_memhp = false;
    mc->default_cpu_type = TARGET_DEFAULT_CPU_TYPE;
    mc->nvdimm_supported = false;

    /* Avoid relying too much on kernel components */
    mc->default_kernel_irqchip_split = true;

    /* Machine class handlers */
    mc->reset = microvm_machine_reset;

    object_class_property_add(oc, MICROVM_MACHINE_PIC, "OnOffAuto",
                              microvm_machine_get_pic,
                              microvm_machine_set_pic,
                              NULL, NULL, &error_abort);
    object_class_property_set_description(oc, MICROVM_MACHINE_PIC,
        "Enable i8259 PIC", &error_abort);

    object_class_property_add(oc, MICROVM_MACHINE_PIT, "OnOffAuto",
                              microvm_machine_get_pit,
                              microvm_machine_set_pit,
                              NULL, NULL, &error_abort);
    object_class_property_set_description(oc, MICROVM_MACHINE_PIT,
        "Enable i8254 PIT", &error_abort);

    object_class_property_add(oc, MICROVM_MACHINE_RTC, "OnOffAuto",
                              microvm_machine_get_rtc,
                              microvm_machine_set_rtc,
                              NULL, NULL, &error_abort);
    object_class_property_set_description(oc, MICROVM_MACHINE_RTC,
        "Enable MC146818 RTC", &error_abort);

    object_class_property_add_bool(oc, MICROVM_MACHINE_ISA_SERIAL,
                                   microvm_machine_get_isa_serial,
                                   microvm_machine_set_isa_serial,
                                   &error_abort);
    object_class_property_set_description(oc, MICROVM_MACHINE_ISA_SERIAL,
        "Set off to disable the instantiation an ISA serial port",
        &error_abort);

    object_class_property_add_bool(oc, MICROVM_MACHINE_OPTION_ROMS,
                                   microvm_machine_get_option_roms,
                                   microvm_machine_set_option_roms,
                                   &error_abort);
    object_class_property_set_description(oc, MICROVM_MACHINE_OPTION_ROMS,
        "Set off to disable loading option ROMs", &error_abort);

    object_class_property_add_bool(oc, MICROVM_MACHINE_AUTO_KERNEL_CMDLINE,
                                   microvm_machine_get_auto_kernel_cmdline,
                                   microvm_machine_set_auto_kernel_cmdline,
                                   &error_abort);
    object_class_property_set_description(oc,
        MICROVM_MACHINE_AUTO_KERNEL_CMDLINE,
        "Set off to disable adding virtio-mmio devices to the kernel cmdline",
        &error_abort);
}

static const TypeInfo microvm_machine_info = {
    .name          = TYPE_MICROVM_MACHINE,
    .parent        = TYPE_X86_MACHINE,
    .instance_size = sizeof(MicrovmMachineState),
    .instance_init = microvm_machine_initfn,
    .class_size    = sizeof(MicrovmMachineClass),
    .class_init    = microvm_class_init,
    .interfaces = (InterfaceInfo[]) {
         { }
    },
};

static void microvm_machine_init(void)
{
    type_register_static(&microvm_machine_info);
}
type_init(microvm_machine_init);
