/*
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2019, 2024 Red Hat, Inc.
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
#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "qemu/units.h"
#include "qemu/datadir.h"
#include "qapi/error.h"
#include "system/numa.h"
#include "system/system.h"
#include "system/xen.h"
#include "trace.h"

#include "hw/i386/x86.h"
#include "target/i386/cpu.h"
#include "hw/rtc/mc146818rtc.h"
#include "target/i386/sev.h"

#include "hw/acpi/cpu_hotplug.h"
#include "hw/irq.h"
#include "hw/loader.h"
#include "multiboot.h"
#include "elf.h"
#include "standard-headers/asm-x86/bootparam.h"
#include CONFIG_DEVICES
#include "kvm/kvm_i386.h"

#ifdef CONFIG_XEN_EMU
#include "hw/xen/xen.h"
#include "hw/i386/kvm/xen_evtchn.h"
#endif

/* Physical Address of PVH entry point read from kernel ELF NOTE */
static size_t pvh_start_addr;

static void x86_cpu_new(X86MachineState *x86ms, int64_t apic_id, Error **errp)
{
    Object *cpu = object_new(MACHINE(x86ms)->cpu_type);

    if (!object_property_set_uint(cpu, "apic-id", apic_id, errp)) {
        goto out;
    }
    qdev_realize(DEVICE(cpu), NULL, errp);

out:
    object_unref(cpu);
}

void x86_cpus_init(X86MachineState *x86ms, int default_cpu_version)
{
    int i;
    const CPUArchIdList *possible_cpus;
    MachineState *ms = MACHINE(x86ms);
    MachineClass *mc = MACHINE_GET_CLASS(x86ms);

    x86_cpu_set_default_version(default_cpu_version);

    /*
     * Calculates the limit to CPU APIC ID values
     *
     * Limit for the APIC ID value, so that all
     * CPU APIC IDs are < x86ms->apic_id_limit.
     *
     * This is used for FW_CFG_MAX_CPUS. See comments on fw_cfg_arch_create().
     */
    x86ms->apic_id_limit = x86_cpu_apic_id_from_index(x86ms,
                                                      ms->smp.max_cpus - 1) + 1;

    /*
     * Can we support APIC ID 255 or higher?  With KVM, that requires
     * both in-kernel lapic and X2APIC userspace API.
     *
     * kvm_enabled() must go first to ensure that kvm_* references are
     * not emitted for the linker to consume (kvm_enabled() is
     * a literal `0` in configurations where kvm_* aren't defined)
     */
    if (kvm_enabled() && x86ms->apic_id_limit > 255 &&
        kvm_irqchip_in_kernel() && !kvm_enable_x2apic()) {
        error_report("current -smp configuration requires kernel "
                     "irqchip and X2APIC API support.");
        exit(EXIT_FAILURE);
    }

    if (kvm_enabled()) {
        kvm_set_max_apic_id(x86ms->apic_id_limit);
    }

    if (!kvm_irqchip_in_kernel()) {
        apic_set_max_apic_id(x86ms->apic_id_limit);
    }

    possible_cpus = mc->possible_cpu_arch_ids(ms);
    for (i = 0; i < ms->smp.cpus; i++) {
        x86_cpu_new(x86ms, possible_cpus->cpus[i].arch_id, &error_fatal);
    }
}

void x86_rtc_set_cpus_count(ISADevice *s, uint16_t cpus_count)
{
    MC146818RtcState *rtc = MC146818_RTC(s);

    if (cpus_count > 0xff) {
        /*
         * If the number of CPUs can't be represented in 8 bits, the
         * BIOS must use "FW_CFG_NB_CPUS". Set RTC field to 0 just
         * to make old BIOSes fail more predictably.
         */
        mc146818rtc_set_cmos_data(rtc, 0x5f, 0);
    } else {
        mc146818rtc_set_cmos_data(rtc, 0x5f, cpus_count - 1);
    }
}

static int x86_apic_cmp(const void *a, const void *b)
{
   CPUArchId *apic_a = (CPUArchId *)a;
   CPUArchId *apic_b = (CPUArchId *)b;

   return apic_a->arch_id - apic_b->arch_id;
}

/*
 * returns pointer to CPUArchId descriptor that matches CPU's apic_id
 * in ms->possible_cpus->cpus, if ms->possible_cpus->cpus has no
 * entry corresponding to CPU's apic_id returns NULL.
 */
static CPUArchId *x86_find_cpu_slot(MachineState *ms, uint32_t id, int *idx)
{
    CPUArchId apic_id, *found_cpu;

    apic_id.arch_id = id;
    found_cpu = bsearch(&apic_id, ms->possible_cpus->cpus,
        ms->possible_cpus->len, sizeof(*ms->possible_cpus->cpus),
        x86_apic_cmp);
    if (found_cpu && idx) {
        *idx = found_cpu - ms->possible_cpus->cpus;
    }
    return found_cpu;
}

void x86_cpu_plug(HotplugHandler *hotplug_dev,
                  DeviceState *dev, Error **errp)
{
    CPUArchId *found_cpu;
    Error *local_err = NULL;
    X86CPU *cpu = X86_CPU(dev);
    X86MachineState *x86ms = X86_MACHINE(hotplug_dev);

    if (x86ms->acpi_dev) {
        hotplug_handler_plug(x86ms->acpi_dev, dev, &local_err);
        if (local_err) {
            goto out;
        }
    }

    /* increment the number of CPUs */
    x86ms->boot_cpus++;
    if (x86ms->rtc) {
        x86_rtc_set_cpus_count(x86ms->rtc, x86ms->boot_cpus);
    }
    if (x86ms->fw_cfg) {
        fw_cfg_modify_i16(x86ms->fw_cfg, FW_CFG_NB_CPUS, x86ms->boot_cpus);
    }

    found_cpu = x86_find_cpu_slot(MACHINE(x86ms), cpu->apic_id, NULL);
    found_cpu->cpu = CPU(dev);
out:
    error_propagate(errp, local_err);
}

void x86_cpu_unplug_request_cb(HotplugHandler *hotplug_dev,
                               DeviceState *dev, Error **errp)
{
    int idx = -1;
    X86CPU *cpu = X86_CPU(dev);
    X86MachineState *x86ms = X86_MACHINE(hotplug_dev);

    if (!x86ms->acpi_dev) {
        error_setg(errp, "CPU hot unplug not supported without ACPI");
        return;
    }

    x86_find_cpu_slot(MACHINE(x86ms), cpu->apic_id, &idx);
    assert(idx != -1);
    if (idx == 0) {
        error_setg(errp, "Boot CPU is unpluggable");
        return;
    }

    hotplug_handler_unplug_request(x86ms->acpi_dev, dev,
                                   errp);
}

void x86_cpu_unplug_cb(HotplugHandler *hotplug_dev,
                       DeviceState *dev, Error **errp)
{
    CPUArchId *found_cpu;
    Error *local_err = NULL;
    X86CPU *cpu = X86_CPU(dev);
    X86MachineState *x86ms = X86_MACHINE(hotplug_dev);

    hotplug_handler_unplug(x86ms->acpi_dev, dev, &local_err);
    if (local_err) {
        goto out;
    }

    found_cpu = x86_find_cpu_slot(MACHINE(x86ms), cpu->apic_id, NULL);
    found_cpu->cpu = NULL;
    qdev_unrealize(dev);

    /* decrement the number of CPUs */
    x86ms->boot_cpus--;
    /* Update the number of CPUs in CMOS */
    x86_rtc_set_cpus_count(x86ms->rtc, x86ms->boot_cpus);
    fw_cfg_modify_i16(x86ms->fw_cfg, FW_CFG_NB_CPUS, x86ms->boot_cpus);
 out:
    error_propagate(errp, local_err);
}

void x86_cpu_pre_plug(HotplugHandler *hotplug_dev,
                      DeviceState *dev, Error **errp)
{
    int idx;
    CPUState *cs;
    CPUArchId *cpu_slot;
    X86CPUTopoIDs topo_ids;
    X86CPU *cpu = X86_CPU(dev);
    CPUX86State *env = &cpu->env;
    MachineState *ms = MACHINE(hotplug_dev);
    X86MachineState *x86ms = X86_MACHINE(hotplug_dev);
    X86CPUTopoInfo *topo_info = &env->topo_info;

    if (!object_dynamic_cast(OBJECT(cpu), ms->cpu_type)) {
        error_setg(errp, "Invalid CPU type, expected cpu type: '%s'",
                   ms->cpu_type);
        return;
    }

    if (x86ms->acpi_dev) {
        Error *local_err = NULL;

        hotplug_handler_pre_plug(HOTPLUG_HANDLER(x86ms->acpi_dev), dev,
                                 &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }

    init_topo_info(topo_info, x86ms);

    if (ms->smp.modules > 1) {
        set_bit(CPU_TOPOLOGY_LEVEL_MODULE, env->avail_cpu_topo);
    }

    if (ms->smp.dies > 1) {
        set_bit(CPU_TOPOLOGY_LEVEL_DIE, env->avail_cpu_topo);
    }

    /*
     * If APIC ID is not set,
     * set it based on socket/die/module/core/thread properties.
     */
    if (cpu->apic_id == UNASSIGNED_APIC_ID) {
        /*
         * die-id was optional in QEMU 4.0 and older, so keep it optional
         * if there's only one die per socket.
         */
        if (cpu->die_id < 0 && ms->smp.dies == 1) {
            cpu->die_id = 0;
        }

        /*
         * module-id was optional in QEMU 9.0 and older, so keep it optional
         * if there's only one module per die.
         */
        if (cpu->module_id < 0 && ms->smp.modules == 1) {
            cpu->module_id = 0;
        }

        if (cpu->socket_id < 0) {
            error_setg(errp, "CPU socket-id is not set");
            return;
        } else if (cpu->socket_id > ms->smp.sockets - 1) {
            error_setg(errp, "Invalid CPU socket-id: %u must be in range 0:%u",
                       cpu->socket_id, ms->smp.sockets - 1);
            return;
        }
        if (cpu->die_id < 0) {
            error_setg(errp, "CPU die-id is not set");
            return;
        } else if (cpu->die_id > ms->smp.dies - 1) {
            error_setg(errp, "Invalid CPU die-id: %u must be in range 0:%u",
                       cpu->die_id, ms->smp.dies - 1);
            return;
        }
        if (cpu->module_id < 0) {
            error_setg(errp, "CPU module-id is not set");
            return;
        } else if (cpu->module_id > ms->smp.modules - 1) {
            error_setg(errp, "Invalid CPU module-id: %u must be in range 0:%u",
                       cpu->module_id, ms->smp.modules - 1);
            return;
        }
        if (cpu->core_id < 0) {
            error_setg(errp, "CPU core-id is not set");
            return;
        } else if (cpu->core_id > (ms->smp.cores - 1)) {
            error_setg(errp, "Invalid CPU core-id: %u must be in range 0:%u",
                       cpu->core_id, ms->smp.cores - 1);
            return;
        }
        if (cpu->thread_id < 0) {
            error_setg(errp, "CPU thread-id is not set");
            return;
        } else if (cpu->thread_id > (ms->smp.threads - 1)) {
            error_setg(errp, "Invalid CPU thread-id: %u must be in range 0:%u",
                       cpu->thread_id, ms->smp.threads - 1);
            return;
        }

        topo_ids.pkg_id = cpu->socket_id;
        topo_ids.die_id = cpu->die_id;
        topo_ids.module_id = cpu->module_id;
        topo_ids.core_id = cpu->core_id;
        topo_ids.smt_id = cpu->thread_id;
        cpu->apic_id = x86_apicid_from_topo_ids(topo_info, &topo_ids);
    }

    cpu_slot = x86_find_cpu_slot(MACHINE(x86ms), cpu->apic_id, &idx);
    if (!cpu_slot) {
        x86_topo_ids_from_apicid(cpu->apic_id, topo_info, &topo_ids);

        error_setg(errp,
            "Invalid CPU [socket: %u, die: %u, module: %u, core: %u, thread: %u]"
            " with APIC ID %" PRIu32 ", valid index range 0:%d",
            topo_ids.pkg_id, topo_ids.die_id, topo_ids.module_id,
            topo_ids.core_id, topo_ids.smt_id, cpu->apic_id,
            ms->possible_cpus->len - 1);
        return;
    }

    if (cpu_slot->cpu) {
        error_setg(errp, "CPU[%d] with APIC ID %" PRIu32 " exists",
                   idx, cpu->apic_id);
        return;
    }

    /* if 'address' properties socket-id/core-id/thread-id are not set, set them
     * so that machine_query_hotpluggable_cpus would show correct values
     */
    /* TODO: move socket_id/core_id/thread_id checks into x86_cpu_realizefn()
     * once -smp refactoring is complete and there will be CPU private
     * CPUState::nr_cores and CPUState::nr_threads fields instead of globals */
    x86_topo_ids_from_apicid(cpu->apic_id, topo_info, &topo_ids);
    if (cpu->socket_id != -1 && cpu->socket_id != topo_ids.pkg_id) {
        error_setg(errp, "property socket-id: %u doesn't match set apic-id:"
            " 0x%x (socket-id: %u)", cpu->socket_id, cpu->apic_id,
            topo_ids.pkg_id);
        return;
    }
    cpu->socket_id = topo_ids.pkg_id;

    if (cpu->die_id != -1 && cpu->die_id != topo_ids.die_id) {
        error_setg(errp, "property die-id: %u doesn't match set apic-id:"
            " 0x%x (die-id: %u)", cpu->die_id, cpu->apic_id, topo_ids.die_id);
        return;
    }
    cpu->die_id = topo_ids.die_id;

    if (cpu->module_id != -1 && cpu->module_id != topo_ids.module_id) {
        error_setg(errp, "property module-id: %u doesn't match set apic-id:"
            " 0x%x (module-id: %u)", cpu->module_id, cpu->apic_id,
            topo_ids.module_id);
        return;
    }
    cpu->module_id = topo_ids.module_id;

    if (cpu->core_id != -1 && cpu->core_id != topo_ids.core_id) {
        error_setg(errp, "property core-id: %u doesn't match set apic-id:"
            " 0x%x (core-id: %u)", cpu->core_id, cpu->apic_id,
            topo_ids.core_id);
        return;
    }
    cpu->core_id = topo_ids.core_id;

    if (cpu->thread_id != -1 && cpu->thread_id != topo_ids.smt_id) {
        error_setg(errp, "property thread-id: %u doesn't match set apic-id:"
            " 0x%x (thread-id: %u)", cpu->thread_id, cpu->apic_id,
            topo_ids.smt_id);
        return;
    }
    cpu->thread_id = topo_ids.smt_id;

    /*
    * kvm_enabled() must go first to ensure that kvm_* references are
    * not emitted for the linker to consume (kvm_enabled() is
    * a literal `0` in configurations where kvm_* aren't defined)
    */
    if (kvm_enabled() && hyperv_feat_enabled(cpu, HYPERV_FEAT_VPINDEX) &&
        !kvm_hv_vpindex_settable()) {
        error_setg(errp, "kernel doesn't allow setting HyperV VP_INDEX");
        return;
    }

    cs = CPU(cpu);
    cs->cpu_index = idx;

    numa_cpu_pre_plug(cpu_slot, dev, errp);
}

static long get_file_size(FILE *f)
{
    long where, size;

    /* XXX: on Unix systems, using fstat() probably makes more sense */

    where = ftell(f);
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, where, SEEK_SET);

    return size;
}

void gsi_handler(void *opaque, int n, int level)
{
    GSIState *s = opaque;
    bool bypass_ioapic = false;

    trace_x86_gsi_interrupt(n, level);

#ifdef CONFIG_XEN_EMU
    /*
     * Xen delivers the GSI to the Legacy PIC (not that Legacy PIC
     * routing actually works properly under Xen). And then to
     * *either* the PIRQ handling or the I/OAPIC depending on whether
     * the former wants it.
     *
     * Additionally, this hook allows the Xen event channel GSI to
     * work around QEMU's lack of support for shared level interrupts,
     * by keeping track of the externally driven state of the pin and
     * implementing a logical OR with the state of the evtchn GSI.
     */
    if (xen_mode == XEN_EMULATE) {
        bypass_ioapic = xen_evtchn_set_gsi(n, &level);
    }
#endif

    switch (n) {
    case 0 ... ISA_NUM_IRQS - 1:
        if (s->i8259_irq[n]) {
            /* Under KVM, Kernel will forward to both PIC and IOAPIC */
            qemu_set_irq(s->i8259_irq[n], level);
        }
        /* fall through */
    case ISA_NUM_IRQS ... IOAPIC_NUM_PINS - 1:
        if (!bypass_ioapic) {
            qemu_set_irq(s->ioapic_irq[n], level);
        }
        break;
    case IO_APIC_SECONDARY_IRQBASE
        ... IO_APIC_SECONDARY_IRQBASE + IOAPIC_NUM_PINS - 1:
        qemu_set_irq(s->ioapic2_irq[n - IO_APIC_SECONDARY_IRQBASE], level);
        break;
    }
}

void ioapic_init_gsi(GSIState *gsi_state, Object *parent)
{
    DeviceState *dev;
    SysBusDevice *d;
    unsigned int i;

    assert(parent);
    if (kvm_ioapic_in_kernel()) {
        dev = qdev_new(TYPE_KVM_IOAPIC);
    } else {
        dev = qdev_new(TYPE_IOAPIC);
    }
    object_property_add_child(parent, "ioapic", OBJECT(dev));
    d = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(d, &error_fatal);
    sysbus_mmio_map(d, 0, IO_APIC_DEFAULT_ADDRESS);

    for (i = 0; i < IOAPIC_NUM_PINS; i++) {
        gsi_state->ioapic_irq[i] = qdev_get_gpio_in(dev, i);
    }
}

DeviceState *ioapic_init_secondary(GSIState *gsi_state)
{
    DeviceState *dev;
    SysBusDevice *d;
    unsigned int i;

    dev = qdev_new(TYPE_IOAPIC);
    d = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(d, &error_fatal);
    sysbus_mmio_map(d, 0, IO_APIC_SECONDARY_ADDRESS);

    for (i = 0; i < IOAPIC_NUM_PINS; i++) {
        gsi_state->ioapic2_irq[i] = qdev_get_gpio_in(dev, i);
    }
    return dev;
}

/*
 * The entry point into the kernel for PVH boot is different from
 * the native entry point.  The PVH entry is defined by the x86/HVM
 * direct boot ABI and is available in an ELFNOTE in the kernel binary.
 *
 * This function is passed to load_elf() when it is called from
 * load_elfboot() which then additionally checks for an ELF Note of
 * type XEN_ELFNOTE_PHYS32_ENTRY and passes it to this function to
 * parse the PVH entry address from the ELF Note.
 *
 * Due to trickery in elf_opts.h, load_elf() is actually available as
 * load_elf32() or load_elf64() and this routine needs to be able
 * to deal with being called as 32 or 64 bit.
 *
 * The address of the PVH entry point is saved to the 'pvh_start_addr'
 * global variable.  (although the entry point is 32-bit, the kernel
 * binary can be either 32-bit or 64-bit).
 */
static uint64_t read_pvh_start_addr(void *arg1, void *arg2, bool is64)
{
    size_t *elf_note_data_addr;

    /* Check if ELF Note header passed in is valid */
    if (arg1 == NULL) {
        return 0;
    }

    if (is64) {
        struct elf64_note *nhdr64 = (struct elf64_note *)arg1;
        uint64_t nhdr_size64 = sizeof(struct elf64_note);
        uint64_t phdr_align = *(uint64_t *)arg2;
        uint64_t nhdr_namesz = nhdr64->n_namesz;

        elf_note_data_addr =
            ((void *)nhdr64) + nhdr_size64 +
            QEMU_ALIGN_UP(nhdr_namesz, phdr_align);

        pvh_start_addr = *elf_note_data_addr;
    } else {
        struct elf32_note *nhdr32 = (struct elf32_note *)arg1;
        uint32_t nhdr_size32 = sizeof(struct elf32_note);
        uint32_t phdr_align = *(uint32_t *)arg2;
        uint32_t nhdr_namesz = nhdr32->n_namesz;

        elf_note_data_addr =
            ((void *)nhdr32) + nhdr_size32 +
            QEMU_ALIGN_UP(nhdr_namesz, phdr_align);

        pvh_start_addr = *(uint32_t *)elf_note_data_addr;
    }

    return pvh_start_addr;
}

static bool load_elfboot(const char *kernel_filename,
                         int kernel_file_size,
                         uint8_t *header,
                         size_t pvh_xen_start_addr,
                         FWCfgState *fw_cfg)
{
    uint32_t flags = 0;
    uint32_t mh_load_addr = 0;
    uint32_t elf_kernel_size = 0;
    uint64_t elf_entry;
    uint64_t elf_low, elf_high;
    int kernel_size;

    if (ldl_le_p(header) != 0x464c457f) {
        return false; /* no elfboot */
    }

    bool elf_is64 = header[EI_CLASS] == ELFCLASS64;
    flags = elf_is64 ?
        ((Elf64_Ehdr *)header)->e_flags : ((Elf32_Ehdr *)header)->e_flags;

    if (flags & 0x00010004) { /* LOAD_ELF_HEADER_HAS_ADDR */
        error_report("elfboot unsupported flags = %x", flags);
        exit(1);
    }

    uint64_t elf_note_type = XEN_ELFNOTE_PHYS32_ENTRY;
    kernel_size = load_elf(kernel_filename, read_pvh_start_addr,
                           NULL, &elf_note_type, &elf_entry,
                           &elf_low, &elf_high, NULL,
                           ELFDATA2LSB, I386_ELF_MACHINE, 0, 0);

    if (kernel_size < 0) {
        error_report("Error while loading elf kernel");
        exit(1);
    }
    mh_load_addr = elf_low;
    elf_kernel_size = elf_high - elf_low;

    if (pvh_start_addr == 0) {
        error_report("Error loading uncompressed kernel without PVH ELF Note");
        exit(1);
    }
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_ENTRY, pvh_start_addr);
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_ADDR, mh_load_addr);
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_SIZE, elf_kernel_size);

    return true;
}

void x86_load_linux(X86MachineState *x86ms,
                    FWCfgState *fw_cfg,
                    int acpi_data_size,
                    bool pvh_enabled)
{
    bool linuxboot_dma_enabled = X86_MACHINE_GET_CLASS(x86ms)->fwcfg_dma_enabled;
    uint16_t protocol;
    int setup_size, kernel_size, cmdline_size;
    int dtb_size, setup_data_offset;
    uint32_t initrd_max;
    uint8_t header[8192], *setup, *kernel;
    hwaddr real_addr, prot_addr, cmdline_addr, initrd_addr = 0;
    FILE *f;
    char *vmode;
    MachineState *machine = MACHINE(x86ms);
    struct setup_data *setup_data;
    const char *kernel_filename = machine->kernel_filename;
    const char *initrd_filename = machine->initrd_filename;
    const char *dtb_filename = machine->dtb;
    const char *kernel_cmdline = machine->kernel_cmdline;
    SevKernelLoaderContext sev_load_ctx = {};

    /* Align to 16 bytes as a paranoia measure */
    cmdline_size = (strlen(kernel_cmdline) + 16) & ~15;

    /* load the kernel header */
    f = fopen(kernel_filename, "rb");
    if (!f) {
        fprintf(stderr, "qemu: could not open kernel file '%s': %s\n",
                kernel_filename, strerror(errno));
        exit(1);
    }

    kernel_size = get_file_size(f);
    if (!kernel_size ||
        fread(header, 1, MIN(ARRAY_SIZE(header), kernel_size), f) !=
        MIN(ARRAY_SIZE(header), kernel_size)) {
        fprintf(stderr, "qemu: could not load kernel '%s': %s\n",
                kernel_filename, strerror(errno));
        exit(1);
    }

    /*
     * kernel protocol version.
     * Please see https://www.kernel.org/doc/Documentation/x86/boot.txt
     */
    if (ldl_le_p(header + 0x202) == 0x53726448) /* Magic signature "HdrS" */ {
        protocol = lduw_le_p(header + 0x206);
    } else {
        /*
         * This could be a multiboot kernel. If it is, let's stop treating it
         * like a Linux kernel.
         * Note: some multiboot images could be in the ELF format (the same of
         * PVH), so we try multiboot first since we check the multiboot magic
         * header before to load it.
         */
        if (load_multiboot(x86ms, fw_cfg, f, kernel_filename, initrd_filename,
                           kernel_cmdline, kernel_size, header)) {
            return;
        }
        /*
         * Check if the file is an uncompressed kernel file (ELF) and load it,
         * saving the PVH entry point used by the x86/HVM direct boot ABI.
         * If load_elfboot() is successful, populate the fw_cfg info.
         */
        if (pvh_enabled &&
            load_elfboot(kernel_filename, kernel_size,
                         header, pvh_start_addr, fw_cfg)) {
            fclose(f);

            fw_cfg_add_i32(fw_cfg, FW_CFG_CMDLINE_SIZE,
                strlen(kernel_cmdline) + 1);
            fw_cfg_add_string(fw_cfg, FW_CFG_CMDLINE_DATA, kernel_cmdline);

            setup = g_memdup2(header, sizeof(header));

            fw_cfg_add_i32(fw_cfg, FW_CFG_SETUP_SIZE, sizeof(header));
            fw_cfg_add_bytes(fw_cfg, FW_CFG_SETUP_DATA,
                             setup, sizeof(header));

            /* load initrd */
            if (initrd_filename) {
                GMappedFile *mapped_file;
                gsize initrd_size;
                gchar *initrd_data;
                GError *gerr = NULL;

                mapped_file = g_mapped_file_new(initrd_filename, false, &gerr);
                if (!mapped_file) {
                    fprintf(stderr, "qemu: error reading initrd %s: %s\n",
                            initrd_filename, gerr->message);
                    exit(1);
                }
                x86ms->initrd_mapped_file = mapped_file;

                initrd_data = g_mapped_file_get_contents(mapped_file);
                initrd_size = g_mapped_file_get_length(mapped_file);
                initrd_max = x86ms->below_4g_mem_size - acpi_data_size - 1;
                if (initrd_size >= initrd_max) {
                    fprintf(stderr, "qemu: initrd is too large, cannot support."
                            "(max: %"PRIu32", need %"PRId64")\n",
                            initrd_max, (uint64_t)initrd_size);
                    exit(1);
                }

                initrd_addr = (initrd_max - initrd_size) & ~4095;

                fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_ADDR, initrd_addr);
                fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_SIZE, initrd_size);
                fw_cfg_add_bytes(fw_cfg, FW_CFG_INITRD_DATA, initrd_data,
                                 initrd_size);
            }

            option_rom[nb_option_roms].bootindex = 0;
            option_rom[nb_option_roms].name = "pvh.bin";
            nb_option_roms++;

            return;
        }
        protocol = 0;
    }

    if (protocol < 0x200 || !(header[0x211] & 0x01)) {
        /* Low kernel */
        real_addr    = 0x90000;
        cmdline_addr = 0x9a000 - cmdline_size;
        prot_addr    = 0x10000;
    } else if (protocol < 0x202) {
        /* High but ancient kernel */
        real_addr    = 0x90000;
        cmdline_addr = 0x9a000 - cmdline_size;
        prot_addr    = 0x100000;
    } else {
        /* High and recent kernel */
        real_addr    = 0x10000;
        cmdline_addr = 0x20000;
        prot_addr    = 0x100000;
    }

    /* highest address for loading the initrd */
    if (protocol >= 0x20c &&
        lduw_le_p(header + 0x236) & XLF_CAN_BE_LOADED_ABOVE_4G) {
        /*
         * Linux has supported initrd up to 4 GB for a very long time (2007,
         * long before XLF_CAN_BE_LOADED_ABOVE_4G which was added in 2013),
         * though it only sets initrd_max to 2 GB to "work around bootloader
         * bugs". Luckily, QEMU firmware(which does something like bootloader)
         * has supported this.
         *
         * It's believed that if XLF_CAN_BE_LOADED_ABOVE_4G is set, initrd can
         * be loaded into any address.
         *
         * In addition, initrd_max is uint32_t simply because QEMU doesn't
         * support the 64-bit boot protocol (specifically the ext_ramdisk_image
         * field).
         *
         * Therefore here just limit initrd_max to UINT32_MAX simply as well.
         */
        initrd_max = UINT32_MAX;
    } else if (protocol >= 0x203) {
        initrd_max = ldl_le_p(header + 0x22c);
    } else {
        initrd_max = 0x37ffffff;
    }

    if (initrd_max >= x86ms->below_4g_mem_size - acpi_data_size) {
        initrd_max = x86ms->below_4g_mem_size - acpi_data_size - 1;
    }

    fw_cfg_add_i32(fw_cfg, FW_CFG_CMDLINE_ADDR, cmdline_addr);
    fw_cfg_add_i32(fw_cfg, FW_CFG_CMDLINE_SIZE, strlen(kernel_cmdline) + 1);
    fw_cfg_add_string(fw_cfg, FW_CFG_CMDLINE_DATA, kernel_cmdline);
    sev_load_ctx.cmdline_data = (char *)kernel_cmdline;
    sev_load_ctx.cmdline_size = strlen(kernel_cmdline) + 1;

    if (protocol >= 0x202) {
        stl_le_p(header + 0x228, cmdline_addr);
    } else {
        stw_le_p(header + 0x20, 0xA33F);
        stw_le_p(header + 0x22, cmdline_addr - real_addr);
    }

    /* handle vga= parameter */
    vmode = strstr(kernel_cmdline, "vga=");
    if (vmode) {
        unsigned int video_mode;
        const char *end;
        int ret;
        /* skip "vga=" */
        vmode += 4;
        if (!strncmp(vmode, "normal", 6)) {
            video_mode = 0xffff;
        } else if (!strncmp(vmode, "ext", 3)) {
            video_mode = 0xfffe;
        } else if (!strncmp(vmode, "ask", 3)) {
            video_mode = 0xfffd;
        } else {
            ret = qemu_strtoui(vmode, &end, 0, &video_mode);
            if (ret != 0 || (*end && *end != ' ')) {
                fprintf(stderr, "qemu: invalid 'vga=' kernel parameter.\n");
                exit(1);
            }
        }
        stw_le_p(header + 0x1fa, video_mode);
    }

    /* loader type */
    /*
     * High nybble = B reserved for QEMU; low nybble is revision number.
     * If this code is substantially changed, you may want to consider
     * incrementing the revision.
     */
    if (protocol >= 0x200) {
        header[0x210] = 0xB0;
    }
    /* heap */
    if (protocol >= 0x201) {
        header[0x211] |= 0x80; /* CAN_USE_HEAP */
        stw_le_p(header + 0x224, cmdline_addr - real_addr - 0x200);
    }

    /* load initrd */
    if (initrd_filename) {
        GMappedFile *mapped_file;
        gsize initrd_size;
        gchar *initrd_data;
        GError *gerr = NULL;

        if (protocol < 0x200) {
            fprintf(stderr, "qemu: linux kernel too old to load a ram disk\n");
            exit(1);
        }

        mapped_file = g_mapped_file_new(initrd_filename, false, &gerr);
        if (!mapped_file) {
            fprintf(stderr, "qemu: error reading initrd %s: %s\n",
                    initrd_filename, gerr->message);
            exit(1);
        }
        x86ms->initrd_mapped_file = mapped_file;

        initrd_data = g_mapped_file_get_contents(mapped_file);
        initrd_size = g_mapped_file_get_length(mapped_file);
        if (initrd_size >= initrd_max) {
            fprintf(stderr, "qemu: initrd is too large, cannot support."
                    "(max: %"PRIu32", need %"PRId64")\n",
                    initrd_max, (uint64_t)initrd_size);
            exit(1);
        }

        initrd_addr = (initrd_max - initrd_size) & ~4095;

        fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_ADDR, initrd_addr);
        fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_SIZE, initrd_size);
        fw_cfg_add_bytes(fw_cfg, FW_CFG_INITRD_DATA, initrd_data, initrd_size);
        sev_load_ctx.initrd_data = initrd_data;
        sev_load_ctx.initrd_size = initrd_size;

        stl_le_p(header + 0x218, initrd_addr);
        stl_le_p(header + 0x21c, initrd_size);
    }

    /* load kernel and setup */
    setup_size = header[0x1f1];
    if (setup_size == 0) {
        setup_size = 4;
    }
    setup_size = (setup_size + 1) * 512;
    if (setup_size > kernel_size) {
        fprintf(stderr, "qemu: invalid kernel header\n");
        exit(1);
    }

    setup  = g_malloc(setup_size);
    kernel = g_malloc(kernel_size);
    fseek(f, 0, SEEK_SET);
    if (fread(setup, 1, setup_size, f) != setup_size) {
        fprintf(stderr, "fread() failed\n");
        exit(1);
    }
    fseek(f, 0, SEEK_SET);
    if (fread(kernel, 1, kernel_size, f) != kernel_size) {
        fprintf(stderr, "fread() failed\n");
        exit(1);
    }
    fclose(f);

    /* append dtb to kernel */
    if (dtb_filename) {
        if (protocol < 0x209) {
            fprintf(stderr, "qemu: Linux kernel too old to load a dtb\n");
            exit(1);
        }

        dtb_size = get_image_size(dtb_filename);
        if (dtb_size <= 0) {
            fprintf(stderr, "qemu: error reading dtb %s: %s\n",
                    dtb_filename, strerror(errno));
            exit(1);
        }

        setup_data_offset = QEMU_ALIGN_UP(kernel_size, 16);
        kernel_size = setup_data_offset + sizeof(struct setup_data) + dtb_size;
        kernel = g_realloc(kernel, kernel_size);

        stq_le_p(header + 0x250, prot_addr + setup_data_offset);

        setup_data = (struct setup_data *)(kernel + setup_data_offset);
        setup_data->next = 0;
        setup_data->type = cpu_to_le32(SETUP_DTB);
        setup_data->len = cpu_to_le32(dtb_size);

        load_image_size(dtb_filename, setup_data->data, dtb_size);
    }

    /*
     * If we're starting an encrypted VM, it will be OVMF based, which uses the
     * efi stub for booting and doesn't require any values to be placed in the
     * kernel header.  We therefore don't update the header so the hash of the
     * kernel on the other side of the fw_cfg interface matches the hash of the
     * file the user passed in.
     */
    if (!sev_enabled() && protocol > 0) {
        memcpy(setup, header, MIN(sizeof(header), setup_size));
    }

    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_ADDR, prot_addr);
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_SIZE, kernel_size - setup_size);
    fw_cfg_add_bytes(fw_cfg, FW_CFG_KERNEL_DATA,
                     kernel + setup_size, kernel_size - setup_size);
    sev_load_ctx.kernel_data = (char *)kernel + setup_size;
    sev_load_ctx.kernel_size = kernel_size - setup_size;

    fw_cfg_add_i32(fw_cfg, FW_CFG_SETUP_ADDR, real_addr);
    fw_cfg_add_i32(fw_cfg, FW_CFG_SETUP_SIZE, setup_size);
    fw_cfg_add_bytes(fw_cfg, FW_CFG_SETUP_DATA, setup, setup_size);
    sev_load_ctx.setup_data = (char *)setup;
    sev_load_ctx.setup_size = setup_size;

    /* kernel without setup header patches */
    fw_cfg_add_file(fw_cfg, "etc/boot/kernel", kernel, kernel_size);

    if (machine->shim_filename) {
        GMappedFile *mapped_file;
        GError *gerr = NULL;

        mapped_file = g_mapped_file_new(machine->shim_filename, false, &gerr);
        if (!mapped_file) {
            fprintf(stderr, "qemu: error reading shim %s: %s\n",
                    machine->shim_filename, gerr->message);
            exit(1);
        }

        fw_cfg_add_file(fw_cfg, "etc/boot/shim",
                        g_mapped_file_get_contents(mapped_file),
                        g_mapped_file_get_length(mapped_file));
    }

    if (sev_enabled()) {
        sev_add_kernel_loader_hashes(&sev_load_ctx, &error_fatal);
    }

    option_rom[nb_option_roms].bootindex = 0;
    option_rom[nb_option_roms].name = "linuxboot.bin";
    if (linuxboot_dma_enabled && fw_cfg_dma_enabled(fw_cfg)) {
        option_rom[nb_option_roms].name = "linuxboot_dma.bin";
    }
    nb_option_roms++;
}

void x86_isa_bios_init(MemoryRegion *isa_bios, MemoryRegion *isa_memory,
                       MemoryRegion *bios, bool read_only)
{
    uint64_t bios_size = memory_region_size(bios);
    uint64_t isa_bios_size = MIN(bios_size, 128 * KiB);

    memory_region_init_alias(isa_bios, NULL, "isa-bios", bios,
                             bios_size - isa_bios_size, isa_bios_size);
    memory_region_add_subregion_overlap(isa_memory, 1 * MiB - isa_bios_size,
                                        isa_bios, 1);
    memory_region_set_readonly(isa_bios, read_only);
}

void x86_bios_rom_init(X86MachineState *x86ms, const char *default_firmware,
                       MemoryRegion *rom_memory, bool isapc_ram_fw)
{
    const char *bios_name;
    char *filename;
    int bios_size;
    ssize_t ret;

    /* BIOS load */
    bios_name = MACHINE(x86ms)->firmware ?: default_firmware;
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    if (filename) {
        bios_size = get_image_size(filename);
    } else {
        bios_size = -1;
    }
    if (bios_size <= 0 ||
        (bios_size % 65536) != 0) {
        goto bios_error;
    }
    if (machine_require_guest_memfd(MACHINE(x86ms))) {
        memory_region_init_ram_guest_memfd(&x86ms->bios, NULL, "pc.bios",
                                           bios_size, &error_fatal);
    } else {
        memory_region_init_ram(&x86ms->bios, NULL, "pc.bios",
                               bios_size, &error_fatal);
    }
    if (sev_enabled()) {
        /*
         * The concept of a "reset" simply doesn't exist for
         * confidential computing guests, we have to destroy and
         * re-launch them instead.  So there is no need to register
         * the firmware as rom to properly re-initialize on reset.
         * Just go for a straight file load instead.
         */
        void *ptr = memory_region_get_ram_ptr(&x86ms->bios);
        load_image_size(filename, ptr, bios_size);
        x86_firmware_configure(0x100000000ULL - bios_size, ptr, bios_size);
    } else {
        memory_region_set_readonly(&x86ms->bios, !isapc_ram_fw);
        ret = rom_add_file_fixed(bios_name, (uint32_t)(-bios_size), -1);
        if (ret != 0) {
            goto bios_error;
        }
    }
    g_free(filename);

    if (!machine_require_guest_memfd(MACHINE(x86ms))) {
        /* map the last 128KB of the BIOS in ISA space */
        x86_isa_bios_init(&x86ms->isa_bios, rom_memory, &x86ms->bios,
                          !isapc_ram_fw);
    }

    /* map all the bios at the top of memory */
    memory_region_add_subregion(rom_memory,
                                (uint32_t)(-bios_size),
                                &x86ms->bios);
    return;

bios_error:
    fprintf(stderr, "qemu: could not load PC BIOS '%s'\n", bios_name);
    exit(1);
}
