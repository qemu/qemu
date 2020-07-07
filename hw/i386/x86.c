/*
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2019 Red Hat, Inc.
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
#include "qemu/option.h"
#include "qemu/cutils.h"
#include "qemu/units.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qapi-visit-common.h"
#include "qapi/visitor.h"
#include "sysemu/qtest.h"
#include "sysemu/numa.h"
#include "sysemu/replay.h"
#include "sysemu/sysemu.h"
#include "trace.h"

#include "hw/i386/x86.h"
#include "target/i386/cpu.h"
#include "hw/i386/topology.h"
#include "hw/i386/fw_cfg.h"
#include "hw/intc/i8259.h"

#include "hw/acpi/cpu_hotplug.h"
#include "hw/irq.h"
#include "hw/nmi.h"
#include "hw/loader.h"
#include "multiboot.h"
#include "elf.h"
#include "standard-headers/asm-x86/bootparam.h"
#include "config-devices.h"
#include "kvm_i386.h"

#define BIOS_FILENAME "bios.bin"

/* Physical Address of PVH entry point read from kernel ELF NOTE */
static size_t pvh_start_addr;

inline void init_topo_info(X86CPUTopoInfo *topo_info,
                           const X86MachineState *x86ms)
{
    MachineState *ms = MACHINE(x86ms);

    topo_info->nodes_per_pkg = ms->numa_state->num_nodes / ms->smp.sockets;
    topo_info->dies_per_pkg = x86ms->smp_dies;
    topo_info->cores_per_die = ms->smp.cores;
    topo_info->threads_per_core = ms->smp.threads;
}

/*
 * Set up with the new EPYC topology handlers
 *
 * AMD uses different apic id encoding for EPYC based cpus. Override
 * the default topo handlers with EPYC encoding handlers.
 */
static void x86_set_epyc_topo_handlers(MachineState *machine)
{
    X86MachineState *x86ms = X86_MACHINE(machine);

    x86ms->apicid_from_cpu_idx = x86_apicid_from_cpu_idx_epyc;
    x86ms->topo_ids_from_apicid = x86_topo_ids_from_apicid_epyc;
    x86ms->apicid_from_topo_ids = x86_apicid_from_topo_ids_epyc;
    x86ms->apicid_pkg_offset = apicid_pkg_offset_epyc;
}

/*
 * Calculates initial APIC ID for a specific CPU index
 *
 * Currently we need to be able to calculate the APIC ID from the CPU index
 * alone (without requiring a CPU object), as the QEMU<->Seabios interfaces have
 * no concept of "CPU index", and the NUMA tables on fw_cfg need the APIC ID of
 * all CPUs up to max_cpus.
 */
uint32_t x86_cpu_apic_id_from_index(X86MachineState *x86ms,
                                    unsigned int cpu_index)
{
    X86MachineClass *x86mc = X86_MACHINE_GET_CLASS(x86ms);
    X86CPUTopoInfo topo_info;
    uint32_t correct_id;
    static bool warned;

    init_topo_info(&topo_info, x86ms);

    correct_id = x86ms->apicid_from_cpu_idx(&topo_info, cpu_index);
    if (x86mc->compat_apic_id_mode) {
        if (cpu_index != correct_id && !warned && !qtest_enabled()) {
            error_report("APIC IDs set in compatibility mode, "
                         "CPU topology won't match the configuration");
            warned = true;
        }
        return cpu_index;
    } else {
        return correct_id;
    }
}


void x86_cpu_new(X86MachineState *x86ms, int64_t apic_id, Error **errp)
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

    /* Check for apicid encoding */
    if (cpu_x86_use_epyc_apic_id_encoding(ms->cpu_type)) {
        x86_set_epyc_topo_handlers(ms);
    }

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
    possible_cpus = mc->possible_cpu_arch_ids(ms);

    for (i = 0; i < ms->possible_cpus->len; i++) {
        ms->possible_cpus->cpus[i].arch_id =
            x86_cpu_apic_id_from_index(x86ms, i);
    }

    for (i = 0; i < ms->smp.cpus; i++) {
        x86_cpu_new(x86ms, possible_cpus->cpus[i].arch_id, &error_fatal);
    }
}

CpuInstanceProperties
x86_cpu_index_to_props(MachineState *ms, unsigned cpu_index)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    const CPUArchIdList *possible_cpus = mc->possible_cpu_arch_ids(ms);

    assert(cpu_index < possible_cpus->len);
    return possible_cpus->cpus[cpu_index].props;
}

int64_t x86_get_default_cpu_node_id(const MachineState *ms, int idx)
{
   X86CPUTopoIDs topo_ids;
   X86MachineState *x86ms = X86_MACHINE(ms);
   X86CPUTopoInfo topo_info;

   init_topo_info(&topo_info, x86ms);

   assert(idx < ms->possible_cpus->len);
   x86_topo_ids_from_idx(&topo_info, idx, &topo_ids);
   return topo_ids.pkg_id % ms->numa_state->num_nodes;
}

const CPUArchIdList *x86_possible_cpu_arch_ids(MachineState *ms)
{
    X86MachineState *x86ms = X86_MACHINE(ms);
    unsigned int max_cpus = ms->smp.max_cpus;
    X86CPUTopoInfo topo_info;
    int i;

    if (ms->possible_cpus) {
        /*
         * make sure that max_cpus hasn't changed since the first use, i.e.
         * -smp hasn't been parsed after it
         */
        assert(ms->possible_cpus->len == max_cpus);
        return ms->possible_cpus;
    }

    ms->possible_cpus = g_malloc0(sizeof(CPUArchIdList) +
                                  sizeof(CPUArchId) * max_cpus);
    ms->possible_cpus->len = max_cpus;

    init_topo_info(&topo_info, x86ms);

    for (i = 0; i < ms->possible_cpus->len; i++) {
        X86CPUTopoIDs topo_ids;

        ms->possible_cpus->cpus[i].type = ms->cpu_type;
        ms->possible_cpus->cpus[i].vcpus_count = 1;
        x86_topo_ids_from_idx(&topo_info, i, &topo_ids);
        ms->possible_cpus->cpus[i].props.has_socket_id = true;
        ms->possible_cpus->cpus[i].props.socket_id = topo_ids.pkg_id;
        if (x86ms->smp_dies > 1) {
            ms->possible_cpus->cpus[i].props.has_die_id = true;
            ms->possible_cpus->cpus[i].props.die_id = topo_ids.die_id;
        }
        ms->possible_cpus->cpus[i].props.has_core_id = true;
        ms->possible_cpus->cpus[i].props.core_id = topo_ids.core_id;
        ms->possible_cpus->cpus[i].props.has_thread_id = true;
        ms->possible_cpus->cpus[i].props.thread_id = topo_ids.smt_id;
    }
    return ms->possible_cpus;
}

static void x86_nmi(NMIState *n, int cpu_index, Error **errp)
{
    /* cpu index isn't used */
    CPUState *cs;

    CPU_FOREACH(cs) {
        X86CPU *cpu = X86_CPU(cs);

        if (!cpu->apic_state) {
            cpu_interrupt(cs, CPU_INTERRUPT_NMI);
        } else {
            apic_deliver_nmi(cpu->apic_state);
        }
    }
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

/* TSC handling */
uint64_t cpu_get_tsc(CPUX86State *env)
{
    return cpu_get_ticks();
}

/* IRQ handling */
static void pic_irq_request(void *opaque, int irq, int level)
{
    CPUState *cs = first_cpu;
    X86CPU *cpu = X86_CPU(cs);

    trace_x86_pic_interrupt(irq, level);
    if (cpu->apic_state && !kvm_irqchip_in_kernel()) {
        CPU_FOREACH(cs) {
            cpu = X86_CPU(cs);
            if (apic_accept_pic_intr(cpu->apic_state)) {
                apic_deliver_pic_intr(cpu->apic_state, level);
            }
        }
    } else {
        if (level) {
            cpu_interrupt(cs, CPU_INTERRUPT_HARD);
        } else {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
        }
    }
}

qemu_irq x86_allocate_cpu_irq(void)
{
    return qemu_allocate_irq(pic_irq_request, NULL, 0);
}

int cpu_get_pic_interrupt(CPUX86State *env)
{
    X86CPU *cpu = env_archcpu(env);
    int intno;

    if (!kvm_irqchip_in_kernel()) {
        intno = apic_get_interrupt(cpu->apic_state);
        if (intno >= 0) {
            return intno;
        }
        /* read the irq from the PIC */
        if (!apic_accept_pic_intr(cpu->apic_state)) {
            return -1;
        }
    }

    intno = pic_read_irq(isa_pic);
    return intno;
}

DeviceState *cpu_get_current_apic(void)
{
    if (current_cpu) {
        X86CPU *cpu = X86_CPU(current_cpu);
        return cpu->apic_state;
    } else {
        return NULL;
    }
}

void gsi_handler(void *opaque, int n, int level)
{
    GSIState *s = opaque;

    trace_x86_gsi_interrupt(n, level);
    if (n < ISA_NUM_IRQS) {
        /* Under KVM, Kernel will forward to both PIC and IOAPIC */
        qemu_set_irq(s->i8259_irq[n], level);
    }
    qemu_set_irq(s->ioapic_irq[n], level);
}

void ioapic_init_gsi(GSIState *gsi_state, const char *parent_name)
{
    DeviceState *dev;
    SysBusDevice *d;
    unsigned int i;

    assert(parent_name);
    if (kvm_ioapic_in_kernel()) {
        dev = qdev_new(TYPE_KVM_IOAPIC);
    } else {
        dev = qdev_new(TYPE_IOAPIC);
    }
    object_property_add_child(object_resolve_path(parent_name, NULL),
                              "ioapic", OBJECT(dev));
    d = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(d, &error_fatal);
    sysbus_mmio_map(d, 0, IO_APIC_DEFAULT_ADDRESS);

    for (i = 0; i < IOAPIC_NUM_PINS; i++) {
        gsi_state->ioapic_irq[i] = qdev_get_gpio_in(dev, i);
    }
}

struct setup_data {
    uint64_t next;
    uint32_t type;
    uint32_t len;
    uint8_t data[];
} __attribute__((packed));


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
    } else {
        struct elf32_note *nhdr32 = (struct elf32_note *)arg1;
        uint32_t nhdr_size32 = sizeof(struct elf32_note);
        uint32_t phdr_align = *(uint32_t *)arg2;
        uint32_t nhdr_namesz = nhdr32->n_namesz;

        elf_note_data_addr =
            ((void *)nhdr32) + nhdr_size32 +
            QEMU_ALIGN_UP(nhdr_namesz, phdr_align);
    }

    pvh_start_addr = *elf_note_data_addr;

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

    if (ldl_p(header) != 0x464c457f) {
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
                           &elf_low, &elf_high, NULL, 0, I386_ELF_MACHINE,
                           0, 0);

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
                    bool pvh_enabled,
                    bool linuxboot_dma_enabled)
{
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

    /* kernel protocol version */
    if (ldl_p(header + 0x202) == 0x53726448) {
        protocol = lduw_p(header + 0x206);
    } else {
        /*
         * This could be a multiboot kernel. If it is, let's stop treating it
         * like a Linux kernel.
         * Note: some multiboot images could be in the ELF format (the same of
         * PVH), so we try multiboot first since we check the multiboot magic
         * header before to load it.
         */
        if (load_multiboot(fw_cfg, f, kernel_filename, initrd_filename,
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

            fw_cfg_add_i32(fw_cfg, FW_CFG_SETUP_SIZE, sizeof(header));
            fw_cfg_add_bytes(fw_cfg, FW_CFG_SETUP_DATA,
                             header, sizeof(header));

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
        lduw_p(header + 0x236) & XLF_CAN_BE_LOADED_ABOVE_4G) {
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
        initrd_max = ldl_p(header + 0x22c);
    } else {
        initrd_max = 0x37ffffff;
    }

    if (initrd_max >= x86ms->below_4g_mem_size - acpi_data_size) {
        initrd_max = x86ms->below_4g_mem_size - acpi_data_size - 1;
    }

    fw_cfg_add_i32(fw_cfg, FW_CFG_CMDLINE_ADDR, cmdline_addr);
    fw_cfg_add_i32(fw_cfg, FW_CFG_CMDLINE_SIZE, strlen(kernel_cmdline) + 1);
    fw_cfg_add_string(fw_cfg, FW_CFG_CMDLINE_DATA, kernel_cmdline);

    if (protocol >= 0x202) {
        stl_p(header + 0x228, cmdline_addr);
    } else {
        stw_p(header + 0x20, 0xA33F);
        stw_p(header + 0x22, cmdline_addr - real_addr);
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
        stw_p(header + 0x1fa, video_mode);
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
        stw_p(header + 0x224, cmdline_addr - real_addr - 0x200);
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

        stl_p(header + 0x218, initrd_addr);
        stl_p(header + 0x21c, initrd_size);
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
    kernel_size -= setup_size;

    setup  = g_malloc(setup_size);
    kernel = g_malloc(kernel_size);
    fseek(f, 0, SEEK_SET);
    if (fread(setup, 1, setup_size, f) != setup_size) {
        fprintf(stderr, "fread() failed\n");
        exit(1);
    }
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

        stq_p(header + 0x250, prot_addr + setup_data_offset);

        setup_data = (struct setup_data *)(kernel + setup_data_offset);
        setup_data->next = 0;
        setup_data->type = cpu_to_le32(SETUP_DTB);
        setup_data->len = cpu_to_le32(dtb_size);

        load_image_size(dtb_filename, setup_data->data, dtb_size);
    }

    memcpy(setup, header, MIN(sizeof(header), setup_size));

    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_ADDR, prot_addr);
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_SIZE, kernel_size);
    fw_cfg_add_bytes(fw_cfg, FW_CFG_KERNEL_DATA, kernel, kernel_size);

    fw_cfg_add_i32(fw_cfg, FW_CFG_SETUP_ADDR, real_addr);
    fw_cfg_add_i32(fw_cfg, FW_CFG_SETUP_SIZE, setup_size);
    fw_cfg_add_bytes(fw_cfg, FW_CFG_SETUP_DATA, setup, setup_size);

    option_rom[nb_option_roms].bootindex = 0;
    option_rom[nb_option_roms].name = "linuxboot.bin";
    if (linuxboot_dma_enabled && fw_cfg_dma_enabled(fw_cfg)) {
        option_rom[nb_option_roms].name = "linuxboot_dma.bin";
    }
    nb_option_roms++;
}

void x86_bios_rom_init(MemoryRegion *rom_memory, bool isapc_ram_fw)
{
    char *filename;
    MemoryRegion *bios, *isa_bios;
    int bios_size, isa_bios_size;
    int ret;

    /* BIOS load */
    if (bios_name == NULL) {
        bios_name = BIOS_FILENAME;
    }
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
    bios = g_malloc(sizeof(*bios));
    memory_region_init_ram(bios, NULL, "pc.bios", bios_size, &error_fatal);
    if (!isapc_ram_fw) {
        memory_region_set_readonly(bios, true);
    }
    ret = rom_add_file_fixed(bios_name, (uint32_t)(-bios_size), -1);
    if (ret != 0) {
    bios_error:
        fprintf(stderr, "qemu: could not load PC BIOS '%s'\n", bios_name);
        exit(1);
    }
    g_free(filename);

    /* map the last 128KB of the BIOS in ISA space */
    isa_bios_size = MIN(bios_size, 128 * KiB);
    isa_bios = g_malloc(sizeof(*isa_bios));
    memory_region_init_alias(isa_bios, NULL, "isa-bios", bios,
                             bios_size - isa_bios_size, isa_bios_size);
    memory_region_add_subregion_overlap(rom_memory,
                                        0x100000 - isa_bios_size,
                                        isa_bios,
                                        1);
    if (!isapc_ram_fw) {
        memory_region_set_readonly(isa_bios, true);
    }

    /* map all the bios at the top of memory */
    memory_region_add_subregion(rom_memory,
                                (uint32_t)(-bios_size),
                                bios);
}

bool x86_machine_is_smm_enabled(X86MachineState *x86ms)
{
    bool smm_available = false;

    if (x86ms->smm == ON_OFF_AUTO_OFF) {
        return false;
    }

    if (tcg_enabled() || qtest_enabled()) {
        smm_available = true;
    } else if (kvm_enabled()) {
        smm_available = kvm_has_smm();
    }

    if (smm_available) {
        return true;
    }

    if (x86ms->smm == ON_OFF_AUTO_ON) {
        error_report("System Management Mode not supported by this hypervisor.");
        exit(1);
    }
    return false;
}

static void x86_machine_get_smm(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    X86MachineState *x86ms = X86_MACHINE(obj);
    OnOffAuto smm = x86ms->smm;

    visit_type_OnOffAuto(v, name, &smm, errp);
}

static void x86_machine_set_smm(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    X86MachineState *x86ms = X86_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &x86ms->smm, errp);
}

bool x86_machine_is_acpi_enabled(X86MachineState *x86ms)
{
    if (x86ms->acpi == ON_OFF_AUTO_OFF) {
        return false;
    }
    return true;
}

static void x86_machine_get_acpi(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    X86MachineState *x86ms = X86_MACHINE(obj);
    OnOffAuto acpi = x86ms->acpi;

    visit_type_OnOffAuto(v, name, &acpi, errp);
}

static void x86_machine_set_acpi(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    X86MachineState *x86ms = X86_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &x86ms->acpi, errp);
}

static void x86_machine_initfn(Object *obj)
{
    X86MachineState *x86ms = X86_MACHINE(obj);

    x86ms->smm = ON_OFF_AUTO_AUTO;
    x86ms->acpi = ON_OFF_AUTO_AUTO;
    x86ms->smp_dies = 1;

    x86ms->apicid_from_cpu_idx = x86_apicid_from_cpu_idx;
    x86ms->topo_ids_from_apicid = x86_topo_ids_from_apicid;
    x86ms->apicid_from_topo_ids = x86_apicid_from_topo_ids;
    x86ms->apicid_pkg_offset = apicid_pkg_offset;
}

static void x86_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    X86MachineClass *x86mc = X86_MACHINE_CLASS(oc);
    NMIClass *nc = NMI_CLASS(oc);

    mc->cpu_index_to_instance_props = x86_cpu_index_to_props;
    mc->get_default_cpu_node_id = x86_get_default_cpu_node_id;
    mc->possible_cpu_arch_ids = x86_possible_cpu_arch_ids;
    x86mc->compat_apic_id_mode = false;
    x86mc->save_tsc_khz = true;
    nc->nmi_monitor_handler = x86_nmi;

    object_class_property_add(oc, X86_MACHINE_SMM, "OnOffAuto",
        x86_machine_get_smm, x86_machine_set_smm,
        NULL, NULL);
    object_class_property_set_description(oc, X86_MACHINE_SMM,
        "Enable SMM");

    object_class_property_add(oc, X86_MACHINE_ACPI, "OnOffAuto",
        x86_machine_get_acpi, x86_machine_set_acpi,
        NULL, NULL);
    object_class_property_set_description(oc, X86_MACHINE_ACPI,
        "Enable ACPI");
}

static const TypeInfo x86_machine_info = {
    .name = TYPE_X86_MACHINE,
    .parent = TYPE_MACHINE,
    .abstract = true,
    .instance_size = sizeof(X86MachineState),
    .instance_init = x86_machine_initfn,
    .class_size = sizeof(X86MachineClass),
    .class_init = x86_machine_class_init,
    .interfaces = (InterfaceInfo[]) {
         { TYPE_NMI },
         { }
    },
};

static void x86_machine_register_types(void)
{
    type_register_static(&x86_machine_info);
}

type_init(x86_machine_register_types)
