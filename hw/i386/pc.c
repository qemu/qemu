/*
 * QEMU PC System Emulator
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
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
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/char/serial.h"
#include "hw/i386/apic.h"
#include "hw/i386/topology.h"
#include "sysemu/cpus.h"
#include "hw/block/fdc.h"
#include "hw/ide.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/timer/hpet.h"
#include "hw/smbios/smbios.h"
#include "hw/loader.h"
#include "elf.h"
#include "multiboot.h"
#include "hw/timer/mc146818rtc.h"
#include "hw/timer/i8254.h"
#include "hw/audio/pcspk.h"
#include "hw/pci/msi.h"
#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "sysemu/numa.h"
#include "sysemu/kvm.h"
#include "sysemu/qtest.h"
#include "kvm_i386.h"
#include "hw/xen/xen.h"
#include "sysemu/block-backend.h"
#include "hw/block/block.h"
#include "ui/qemu-spice.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "sysemu/arch_init.h"
#include "qemu/bitmap.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/cpu_hotplug.h"
#include "hw/boards.h"
#include "hw/pci/pci_host.h"
#include "acpi-build.h"
#include "hw/mem/pc-dimm.h"
#include "qapi/visitor.h"
#include "qapi-visit.h"
#include "qom/cpu.h"
#include "hw/nmi.h"

/* debug PC/ISA interrupts */
//#define DEBUG_IRQ

#ifdef DEBUG_IRQ
#define DPRINTF(fmt, ...)                                       \
    do { printf("CPUIRQ: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...)
#endif

#define FW_CFG_ACPI_TABLES (FW_CFG_ARCH_LOCAL + 0)
#define FW_CFG_SMBIOS_ENTRIES (FW_CFG_ARCH_LOCAL + 1)
#define FW_CFG_IRQ0_OVERRIDE (FW_CFG_ARCH_LOCAL + 2)
#define FW_CFG_E820_TABLE (FW_CFG_ARCH_LOCAL + 3)
#define FW_CFG_HPET (FW_CFG_ARCH_LOCAL + 4)

#define E820_NR_ENTRIES		16

struct e820_entry {
    uint64_t address;
    uint64_t length;
    uint32_t type;
} QEMU_PACKED __attribute((__aligned__(4)));

struct e820_table {
    uint32_t count;
    struct e820_entry entry[E820_NR_ENTRIES];
} QEMU_PACKED __attribute((__aligned__(4)));

static struct e820_table e820_reserve;
static struct e820_entry *e820_table;
static unsigned e820_entries;
struct hpet_fw_config hpet_cfg = {.count = UINT8_MAX};

void gsi_handler(void *opaque, int n, int level)
{
    GSIState *s = opaque;

    DPRINTF("pc: %s GSI %d\n", level ? "raising" : "lowering", n);
    if (n < ISA_NUM_IRQS) {
        qemu_set_irq(s->i8259_irq[n], level);
    }
    qemu_set_irq(s->ioapic_irq[n], level);
}

static void ioport80_write(void *opaque, hwaddr addr, uint64_t data,
                           unsigned size)
{
}

static uint64_t ioport80_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0xffffffffffffffffULL;
}

/* MSDOS compatibility mode FPU exception support */
static qemu_irq ferr_irq;

void pc_register_ferr_irq(qemu_irq irq)
{
    ferr_irq = irq;
}

/* XXX: add IGNNE support */
void cpu_set_ferr(CPUX86State *s)
{
    qemu_irq_raise(ferr_irq);
}

static void ioportF0_write(void *opaque, hwaddr addr, uint64_t data,
                           unsigned size)
{
    qemu_irq_lower(ferr_irq);
}

static uint64_t ioportF0_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0xffffffffffffffffULL;
}

/* TSC handling */
uint64_t cpu_get_tsc(CPUX86State *env)
{
    return cpu_get_ticks();
}

/* IRQ handling */
int cpu_get_pic_interrupt(CPUX86State *env)
{
    X86CPU *cpu = x86_env_get_cpu(env);
    int intno;

    intno = apic_get_interrupt(cpu->apic_state);
    if (intno >= 0) {
        return intno;
    }
    /* read the irq from the PIC */
    if (!apic_accept_pic_intr(cpu->apic_state)) {
        return -1;
    }

    intno = pic_read_irq(isa_pic);
    return intno;
}

static void pic_irq_request(void *opaque, int irq, int level)
{
    CPUState *cs = first_cpu;
    X86CPU *cpu = X86_CPU(cs);

    DPRINTF("pic_irqs: %s irq %d\n", level? "raise" : "lower", irq);
    if (cpu->apic_state) {
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

/* PC cmos mappings */

#define REG_EQUIPMENT_BYTE          0x14

int cmos_get_fd_drive_type(FloppyDriveType fd0)
{
    int val;

    switch (fd0) {
    case FLOPPY_DRIVE_TYPE_144:
        /* 1.44 Mb 3"5 drive */
        val = 4;
        break;
    case FLOPPY_DRIVE_TYPE_288:
        /* 2.88 Mb 3"5 drive */
        val = 5;
        break;
    case FLOPPY_DRIVE_TYPE_120:
        /* 1.2 Mb 5"5 drive */
        val = 2;
        break;
    case FLOPPY_DRIVE_TYPE_NONE:
    default:
        val = 0;
        break;
    }
    return val;
}

static void cmos_init_hd(ISADevice *s, int type_ofs, int info_ofs,
                         int16_t cylinders, int8_t heads, int8_t sectors)
{
    rtc_set_memory(s, type_ofs, 47);
    rtc_set_memory(s, info_ofs, cylinders);
    rtc_set_memory(s, info_ofs + 1, cylinders >> 8);
    rtc_set_memory(s, info_ofs + 2, heads);
    rtc_set_memory(s, info_ofs + 3, 0xff);
    rtc_set_memory(s, info_ofs + 4, 0xff);
    rtc_set_memory(s, info_ofs + 5, 0xc0 | ((heads > 8) << 3));
    rtc_set_memory(s, info_ofs + 6, cylinders);
    rtc_set_memory(s, info_ofs + 7, cylinders >> 8);
    rtc_set_memory(s, info_ofs + 8, sectors);
}

/* convert boot_device letter to something recognizable by the bios */
static int boot_device2nibble(char boot_device)
{
    switch(boot_device) {
    case 'a':
    case 'b':
        return 0x01; /* floppy boot */
    case 'c':
        return 0x02; /* hard drive boot */
    case 'd':
        return 0x03; /* CD-ROM boot */
    case 'n':
        return 0x04; /* Network boot */
    }
    return 0;
}

static void set_boot_dev(ISADevice *s, const char *boot_device, Error **errp)
{
#define PC_MAX_BOOT_DEVICES 3
    int nbds, bds[3] = { 0, };
    int i;

    nbds = strlen(boot_device);
    if (nbds > PC_MAX_BOOT_DEVICES) {
        error_setg(errp, "Too many boot devices for PC");
        return;
    }
    for (i = 0; i < nbds; i++) {
        bds[i] = boot_device2nibble(boot_device[i]);
        if (bds[i] == 0) {
            error_setg(errp, "Invalid boot device for PC: '%c'",
                       boot_device[i]);
            return;
        }
    }
    rtc_set_memory(s, 0x3d, (bds[1] << 4) | bds[0]);
    rtc_set_memory(s, 0x38, (bds[2] << 4) | (fd_bootchk ? 0x0 : 0x1));
}

static void pc_boot_set(void *opaque, const char *boot_device, Error **errp)
{
    set_boot_dev(opaque, boot_device, errp);
}

static void pc_cmos_init_floppy(ISADevice *rtc_state, ISADevice *floppy)
{
    int val, nb, i;
    FloppyDriveType fd_type[2] = { FLOPPY_DRIVE_TYPE_NONE,
                                   FLOPPY_DRIVE_TYPE_NONE };

    /* floppy type */
    if (floppy) {
        for (i = 0; i < 2; i++) {
            fd_type[i] = isa_fdc_get_drive_type(floppy, i);
        }
    }
    val = (cmos_get_fd_drive_type(fd_type[0]) << 4) |
        cmos_get_fd_drive_type(fd_type[1]);
    rtc_set_memory(rtc_state, 0x10, val);

    val = rtc_get_memory(rtc_state, REG_EQUIPMENT_BYTE);
    nb = 0;
    if (fd_type[0] != FLOPPY_DRIVE_TYPE_NONE) {
        nb++;
    }
    if (fd_type[1] != FLOPPY_DRIVE_TYPE_NONE) {
        nb++;
    }
    switch (nb) {
    case 0:
        break;
    case 1:
        val |= 0x01; /* 1 drive, ready for boot */
        break;
    case 2:
        val |= 0x41; /* 2 drives, ready for boot */
        break;
    }
    rtc_set_memory(rtc_state, REG_EQUIPMENT_BYTE, val);
}

typedef struct pc_cmos_init_late_arg {
    ISADevice *rtc_state;
    BusState *idebus[2];
} pc_cmos_init_late_arg;

typedef struct check_fdc_state {
    ISADevice *floppy;
    bool multiple;
} CheckFdcState;

static int check_fdc(Object *obj, void *opaque)
{
    CheckFdcState *state = opaque;
    Object *fdc;
    uint32_t iobase;
    Error *local_err = NULL;

    fdc = object_dynamic_cast(obj, TYPE_ISA_FDC);
    if (!fdc) {
        return 0;
    }

    iobase = object_property_get_int(obj, "iobase", &local_err);
    if (local_err || iobase != 0x3f0) {
        error_free(local_err);
        return 0;
    }

    if (state->floppy) {
        state->multiple = true;
    } else {
        state->floppy = ISA_DEVICE(obj);
    }
    return 0;
}

static const char * const fdc_container_path[] = {
    "/unattached", "/peripheral", "/peripheral-anon"
};

/*
 * Locate the FDC at IO address 0x3f0, in order to configure the CMOS registers
 * and ACPI objects.
 */
ISADevice *pc_find_fdc0(void)
{
    int i;
    Object *container;
    CheckFdcState state = { 0 };

    for (i = 0; i < ARRAY_SIZE(fdc_container_path); i++) {
        container = container_get(qdev_get_machine(), fdc_container_path[i]);
        object_child_foreach(container, check_fdc, &state);
    }

    if (state.multiple) {
        error_report("warning: multiple floppy disk controllers with "
                     "iobase=0x3f0 have been found");
        error_printf("the one being picked for CMOS setup might not reflect "
                     "your intent");
    }

    return state.floppy;
}

static void pc_cmos_init_late(void *opaque)
{
    pc_cmos_init_late_arg *arg = opaque;
    ISADevice *s = arg->rtc_state;
    int16_t cylinders;
    int8_t heads, sectors;
    int val;
    int i, trans;

    val = 0;
    if (ide_get_geometry(arg->idebus[0], 0,
                         &cylinders, &heads, &sectors) >= 0) {
        cmos_init_hd(s, 0x19, 0x1b, cylinders, heads, sectors);
        val |= 0xf0;
    }
    if (ide_get_geometry(arg->idebus[0], 1,
                         &cylinders, &heads, &sectors) >= 0) {
        cmos_init_hd(s, 0x1a, 0x24, cylinders, heads, sectors);
        val |= 0x0f;
    }
    rtc_set_memory(s, 0x12, val);

    val = 0;
    for (i = 0; i < 4; i++) {
        /* NOTE: ide_get_geometry() returns the physical
           geometry.  It is always such that: 1 <= sects <= 63, 1
           <= heads <= 16, 1 <= cylinders <= 16383. The BIOS
           geometry can be different if a translation is done. */
        if (ide_get_geometry(arg->idebus[i / 2], i % 2,
                             &cylinders, &heads, &sectors) >= 0) {
            trans = ide_get_bios_chs_trans(arg->idebus[i / 2], i % 2) - 1;
            assert((trans & ~3) == 0);
            val |= trans << (i * 2);
        }
    }
    rtc_set_memory(s, 0x39, val);

    pc_cmos_init_floppy(s, pc_find_fdc0());

    qemu_unregister_reset(pc_cmos_init_late, opaque);
}

void pc_cmos_init(PCMachineState *pcms,
                  BusState *idebus0, BusState *idebus1,
                  ISADevice *s)
{
    int val;
    static pc_cmos_init_late_arg arg;

    /* various important CMOS locations needed by PC/Bochs bios */

    /* memory size */
    /* base memory (first MiB) */
    val = MIN(pcms->below_4g_mem_size / 1024, 640);
    rtc_set_memory(s, 0x15, val);
    rtc_set_memory(s, 0x16, val >> 8);
    /* extended memory (next 64MiB) */
    if (pcms->below_4g_mem_size > 1024 * 1024) {
        val = (pcms->below_4g_mem_size - 1024 * 1024) / 1024;
    } else {
        val = 0;
    }
    if (val > 65535)
        val = 65535;
    rtc_set_memory(s, 0x17, val);
    rtc_set_memory(s, 0x18, val >> 8);
    rtc_set_memory(s, 0x30, val);
    rtc_set_memory(s, 0x31, val >> 8);
    /* memory between 16MiB and 4GiB */
    if (pcms->below_4g_mem_size > 16 * 1024 * 1024) {
        val = (pcms->below_4g_mem_size - 16 * 1024 * 1024) / 65536;
    } else {
        val = 0;
    }
    if (val > 65535)
        val = 65535;
    rtc_set_memory(s, 0x34, val);
    rtc_set_memory(s, 0x35, val >> 8);
    /* memory above 4GiB */
    val = pcms->above_4g_mem_size / 65536;
    rtc_set_memory(s, 0x5b, val);
    rtc_set_memory(s, 0x5c, val >> 8);
    rtc_set_memory(s, 0x5d, val >> 16);

    /* set the number of CPU */
    rtc_set_memory(s, 0x5f, smp_cpus - 1);

    object_property_add_link(OBJECT(pcms), "rtc_state",
                             TYPE_ISA_DEVICE,
                             (Object **)&pcms->rtc,
                             object_property_allow_set_link,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE, &error_abort);
    object_property_set_link(OBJECT(pcms), OBJECT(s),
                             "rtc_state", &error_abort);

    set_boot_dev(s, MACHINE(pcms)->boot_order, &error_fatal);

    val = 0;
    val |= 0x02; /* FPU is there */
    val |= 0x04; /* PS/2 mouse installed */
    rtc_set_memory(s, REG_EQUIPMENT_BYTE, val);

    /* hard drives and FDC */
    arg.rtc_state = s;
    arg.idebus[0] = idebus0;
    arg.idebus[1] = idebus1;
    qemu_register_reset(pc_cmos_init_late, &arg);
}

#define TYPE_PORT92 "port92"
#define PORT92(obj) OBJECT_CHECK(Port92State, (obj), TYPE_PORT92)

/* port 92 stuff: could be split off */
typedef struct Port92State {
    ISADevice parent_obj;

    MemoryRegion io;
    uint8_t outport;
    qemu_irq *a20_out;
} Port92State;

static void port92_write(void *opaque, hwaddr addr, uint64_t val,
                         unsigned size)
{
    Port92State *s = opaque;
    int oldval = s->outport;

    DPRINTF("port92: write 0x%02" PRIx64 "\n", val);
    s->outport = val;
    qemu_set_irq(*s->a20_out, (val >> 1) & 1);
    if ((val & 1) && !(oldval & 1)) {
        qemu_system_reset_request();
    }
}

static uint64_t port92_read(void *opaque, hwaddr addr,
                            unsigned size)
{
    Port92State *s = opaque;
    uint32_t ret;

    ret = s->outport;
    DPRINTF("port92: read 0x%02x\n", ret);
    return ret;
}

static void port92_init(ISADevice *dev, qemu_irq *a20_out)
{
    Port92State *s = PORT92(dev);

    s->a20_out = a20_out;
}

static const VMStateDescription vmstate_port92_isa = {
    .name = "port92",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(outport, Port92State),
        VMSTATE_END_OF_LIST()
    }
};

static void port92_reset(DeviceState *d)
{
    Port92State *s = PORT92(d);

    s->outport &= ~1;
}

static const MemoryRegionOps port92_ops = {
    .read = port92_read,
    .write = port92_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void port92_initfn(Object *obj)
{
    Port92State *s = PORT92(obj);

    memory_region_init_io(&s->io, OBJECT(s), &port92_ops, s, "port92", 1);

    s->outport = 0;
}

static void port92_realizefn(DeviceState *dev, Error **errp)
{
    ISADevice *isadev = ISA_DEVICE(dev);
    Port92State *s = PORT92(dev);

    isa_register_ioport(isadev, &s->io, 0x92);
}

static void port92_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = port92_realizefn;
    dc->reset = port92_reset;
    dc->vmsd = &vmstate_port92_isa;
    /*
     * Reason: unlike ordinary ISA devices, this one needs additional
     * wiring: its A20 output line needs to be wired up by
     * port92_init().
     */
    dc->cannot_instantiate_with_device_add_yet = true;
}

static const TypeInfo port92_info = {
    .name          = TYPE_PORT92,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(Port92State),
    .instance_init = port92_initfn,
    .class_init    = port92_class_initfn,
};

static void port92_register_types(void)
{
    type_register_static(&port92_info);
}

type_init(port92_register_types)

static void handle_a20_line_change(void *opaque, int irq, int level)
{
    X86CPU *cpu = opaque;

    /* XXX: send to all CPUs ? */
    /* XXX: add logic to handle multiple A20 line sources */
    x86_cpu_set_a20(cpu, level);
}

int e820_add_entry(uint64_t address, uint64_t length, uint32_t type)
{
    int index = le32_to_cpu(e820_reserve.count);
    struct e820_entry *entry;

    if (type != E820_RAM) {
        /* old FW_CFG_E820_TABLE entry -- reservations only */
        if (index >= E820_NR_ENTRIES) {
            return -EBUSY;
        }
        entry = &e820_reserve.entry[index++];

        entry->address = cpu_to_le64(address);
        entry->length = cpu_to_le64(length);
        entry->type = cpu_to_le32(type);

        e820_reserve.count = cpu_to_le32(index);
    }

    /* new "etc/e820" file -- include ram too */
    e820_table = g_renew(struct e820_entry, e820_table, e820_entries + 1);
    e820_table[e820_entries].address = cpu_to_le64(address);
    e820_table[e820_entries].length = cpu_to_le64(length);
    e820_table[e820_entries].type = cpu_to_le32(type);
    e820_entries++;

    return e820_entries;
}

int e820_get_num_entries(void)
{
    return e820_entries;
}

bool e820_get_entry(int idx, uint32_t type, uint64_t *address, uint64_t *length)
{
    if (idx < e820_entries && e820_table[idx].type == cpu_to_le32(type)) {
        *address = le64_to_cpu(e820_table[idx].address);
        *length = le64_to_cpu(e820_table[idx].length);
        return true;
    }
    return false;
}

/* Enables contiguous-apic-ID mode, for compatibility */
static bool compat_apic_id_mode;

void enable_compat_apic_id_mode(void)
{
    compat_apic_id_mode = true;
}

/* Calculates initial APIC ID for a specific CPU index
 *
 * Currently we need to be able to calculate the APIC ID from the CPU index
 * alone (without requiring a CPU object), as the QEMU<->Seabios interfaces have
 * no concept of "CPU index", and the NUMA tables on fw_cfg need the APIC ID of
 * all CPUs up to max_cpus.
 */
static uint32_t x86_cpu_apic_id_from_index(unsigned int cpu_index)
{
    uint32_t correct_id;
    static bool warned;

    correct_id = x86_apicid_from_cpu_idx(smp_cores, smp_threads, cpu_index);
    if (compat_apic_id_mode) {
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

static void pc_build_smbios(FWCfgState *fw_cfg)
{
    uint8_t *smbios_tables, *smbios_anchor;
    size_t smbios_tables_len, smbios_anchor_len;
    struct smbios_phys_mem_area *mem_array;
    unsigned i, array_count;

    smbios_tables = smbios_get_table_legacy(&smbios_tables_len);
    if (smbios_tables) {
        fw_cfg_add_bytes(fw_cfg, FW_CFG_SMBIOS_ENTRIES,
                         smbios_tables, smbios_tables_len);
    }

    /* build the array of physical mem area from e820 table */
    mem_array = g_malloc0(sizeof(*mem_array) * e820_get_num_entries());
    for (i = 0, array_count = 0; i < e820_get_num_entries(); i++) {
        uint64_t addr, len;

        if (e820_get_entry(i, E820_RAM, &addr, &len)) {
            mem_array[array_count].address = addr;
            mem_array[array_count].length = len;
            array_count++;
        }
    }
    smbios_get_tables(mem_array, array_count,
                      &smbios_tables, &smbios_tables_len,
                      &smbios_anchor, &smbios_anchor_len);
    g_free(mem_array);

    if (smbios_anchor) {
        fw_cfg_add_file(fw_cfg, "etc/smbios/smbios-tables",
                        smbios_tables, smbios_tables_len);
        fw_cfg_add_file(fw_cfg, "etc/smbios/smbios-anchor",
                        smbios_anchor, smbios_anchor_len);
    }
}

static FWCfgState *bochs_bios_init(AddressSpace *as, PCMachineState *pcms)
{
    FWCfgState *fw_cfg;
    uint64_t *numa_fw_cfg;
    int i, j;

    fw_cfg = fw_cfg_init_io_dma(FW_CFG_IO_BASE, FW_CFG_IO_BASE + 4, as);

    /* FW_CFG_MAX_CPUS is a bit confusing/problematic on x86:
     *
     * SeaBIOS needs FW_CFG_MAX_CPUS for CPU hotplug, but the CPU hotplug
     * QEMU<->SeaBIOS interface is not based on the "CPU index", but on the APIC
     * ID of hotplugged CPUs[1]. This means that FW_CFG_MAX_CPUS is not the
     * "maximum number of CPUs", but the "limit to the APIC ID values SeaBIOS
     * may see".
     *
     * So, this means we must not use max_cpus, here, but the maximum possible
     * APIC ID value, plus one.
     *
     * [1] The only kind of "CPU identifier" used between SeaBIOS and QEMU is
     *     the APIC ID, not the "CPU index"
     */
    fw_cfg_add_i16(fw_cfg, FW_CFG_MAX_CPUS, (uint16_t)pcms->apic_id_limit);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, (uint64_t)ram_size);
    fw_cfg_add_bytes(fw_cfg, FW_CFG_ACPI_TABLES,
                     acpi_tables, acpi_tables_len);
    fw_cfg_add_i32(fw_cfg, FW_CFG_IRQ0_OVERRIDE, kvm_allows_irq0_override());

    fw_cfg_add_bytes(fw_cfg, FW_CFG_E820_TABLE,
                     &e820_reserve, sizeof(e820_reserve));
    fw_cfg_add_file(fw_cfg, "etc/e820", e820_table,
                    sizeof(struct e820_entry) * e820_entries);

    fw_cfg_add_bytes(fw_cfg, FW_CFG_HPET, &hpet_cfg, sizeof(hpet_cfg));
    /* allocate memory for the NUMA channel: one (64bit) word for the number
     * of nodes, one word for each VCPU->node and one word for each node to
     * hold the amount of memory.
     */
    numa_fw_cfg = g_new0(uint64_t, 1 + pcms->apic_id_limit + nb_numa_nodes);
    numa_fw_cfg[0] = cpu_to_le64(nb_numa_nodes);
    for (i = 0; i < max_cpus; i++) {
        unsigned int apic_id = x86_cpu_apic_id_from_index(i);
        assert(apic_id < pcms->apic_id_limit);
        for (j = 0; j < nb_numa_nodes; j++) {
            if (test_bit(i, numa_info[j].node_cpu)) {
                numa_fw_cfg[apic_id + 1] = cpu_to_le64(j);
                break;
            }
        }
    }
    for (i = 0; i < nb_numa_nodes; i++) {
        numa_fw_cfg[pcms->apic_id_limit + 1 + i] =
            cpu_to_le64(numa_info[i].node_mem);
    }
    fw_cfg_add_bytes(fw_cfg, FW_CFG_NUMA, numa_fw_cfg,
                     (1 + pcms->apic_id_limit + nb_numa_nodes) *
                     sizeof(*numa_fw_cfg));

    return fw_cfg;
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

static void load_linux(PCMachineState *pcms,
                       FWCfgState *fw_cfg)
{
    uint16_t protocol;
    int setup_size, kernel_size, initrd_size = 0, cmdline_size;
    uint32_t initrd_max;
    uint8_t header[8192], *setup, *kernel, *initrd_data;
    hwaddr real_addr, prot_addr, cmdline_addr, initrd_addr = 0;
    FILE *f;
    char *vmode;
    MachineState *machine = MACHINE(pcms);
    PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);
    const char *kernel_filename = machine->kernel_filename;
    const char *initrd_filename = machine->initrd_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;

    /* Align to 16 bytes as a paranoia measure */
    cmdline_size = (strlen(kernel_cmdline)+16) & ~15;

    /* load the kernel header */
    f = fopen(kernel_filename, "rb");
    if (!f || !(kernel_size = get_file_size(f)) ||
        fread(header, 1, MIN(ARRAY_SIZE(header), kernel_size), f) !=
        MIN(ARRAY_SIZE(header), kernel_size)) {
        fprintf(stderr, "qemu: could not load kernel '%s': %s\n",
                kernel_filename, strerror(errno));
        exit(1);
    }

    /* kernel protocol version */
#if 0
    fprintf(stderr, "header magic: %#x\n", ldl_p(header+0x202));
#endif
    if (ldl_p(header+0x202) == 0x53726448) {
        protocol = lduw_p(header+0x206);
    } else {
        /* This looks like a multiboot kernel. If it is, let's stop
           treating it like a Linux kernel. */
        if (load_multiboot(fw_cfg, f, kernel_filename, initrd_filename,
                           kernel_cmdline, kernel_size, header)) {
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

#if 0
    fprintf(stderr,
            "qemu: real_addr     = 0x" TARGET_FMT_plx "\n"
            "qemu: cmdline_addr  = 0x" TARGET_FMT_plx "\n"
            "qemu: prot_addr     = 0x" TARGET_FMT_plx "\n",
            real_addr,
            cmdline_addr,
            prot_addr);
#endif

    /* highest address for loading the initrd */
    if (protocol >= 0x203) {
        initrd_max = ldl_p(header+0x22c);
    } else {
        initrd_max = 0x37ffffff;
    }

    if (initrd_max >= pcms->below_4g_mem_size - pcmc->acpi_data_size) {
        initrd_max = pcms->below_4g_mem_size - pcmc->acpi_data_size - 1;
    }

    fw_cfg_add_i32(fw_cfg, FW_CFG_CMDLINE_ADDR, cmdline_addr);
    fw_cfg_add_i32(fw_cfg, FW_CFG_CMDLINE_SIZE, strlen(kernel_cmdline)+1);
    fw_cfg_add_string(fw_cfg, FW_CFG_CMDLINE_DATA, kernel_cmdline);

    if (protocol >= 0x202) {
        stl_p(header+0x228, cmdline_addr);
    } else {
        stw_p(header+0x20, 0xA33F);
        stw_p(header+0x22, cmdline_addr-real_addr);
    }

    /* handle vga= parameter */
    vmode = strstr(kernel_cmdline, "vga=");
    if (vmode) {
        unsigned int video_mode;
        /* skip "vga=" */
        vmode += 4;
        if (!strncmp(vmode, "normal", 6)) {
            video_mode = 0xffff;
        } else if (!strncmp(vmode, "ext", 3)) {
            video_mode = 0xfffe;
        } else if (!strncmp(vmode, "ask", 3)) {
            video_mode = 0xfffd;
        } else {
            video_mode = strtol(vmode, NULL, 0);
        }
        stw_p(header+0x1fa, video_mode);
    }

    /* loader type */
    /* High nybble = B reserved for QEMU; low nybble is revision number.
       If this code is substantially changed, you may want to consider
       incrementing the revision. */
    if (protocol >= 0x200) {
        header[0x210] = 0xB0;
    }
    /* heap */
    if (protocol >= 0x201) {
        header[0x211] |= 0x80;	/* CAN_USE_HEAP */
        stw_p(header+0x224, cmdline_addr-real_addr-0x200);
    }

    /* load initrd */
    if (initrd_filename) {
        if (protocol < 0x200) {
            fprintf(stderr, "qemu: linux kernel too old to load a ram disk\n");
            exit(1);
        }

        initrd_size = get_image_size(initrd_filename);
        if (initrd_size < 0) {
            fprintf(stderr, "qemu: error reading initrd %s: %s\n",
                    initrd_filename, strerror(errno));
            exit(1);
        }

        initrd_addr = (initrd_max-initrd_size) & ~4095;

        initrd_data = g_malloc(initrd_size);
        load_image(initrd_filename, initrd_data);

        fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_ADDR, initrd_addr);
        fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_SIZE, initrd_size);
        fw_cfg_add_bytes(fw_cfg, FW_CFG_INITRD_DATA, initrd_data, initrd_size);

        stl_p(header+0x218, initrd_addr);
        stl_p(header+0x21c, initrd_size);
    }

    /* load kernel and setup */
    setup_size = header[0x1f1];
    if (setup_size == 0) {
        setup_size = 4;
    }
    setup_size = (setup_size+1)*512;
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
    memcpy(setup, header, MIN(sizeof(header), setup_size));

    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_ADDR, prot_addr);
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_SIZE, kernel_size);
    fw_cfg_add_bytes(fw_cfg, FW_CFG_KERNEL_DATA, kernel, kernel_size);

    fw_cfg_add_i32(fw_cfg, FW_CFG_SETUP_ADDR, real_addr);
    fw_cfg_add_i32(fw_cfg, FW_CFG_SETUP_SIZE, setup_size);
    fw_cfg_add_bytes(fw_cfg, FW_CFG_SETUP_DATA, setup, setup_size);

    option_rom[nb_option_roms].name = "linuxboot.bin";
    option_rom[nb_option_roms].bootindex = 0;
    nb_option_roms++;
}

#define NE2000_NB_MAX 6

static const int ne2000_io[NE2000_NB_MAX] = { 0x300, 0x320, 0x340, 0x360,
                                              0x280, 0x380 };
static const int ne2000_irq[NE2000_NB_MAX] = { 9, 10, 11, 3, 4, 5 };

void pc_init_ne2k_isa(ISABus *bus, NICInfo *nd)
{
    static int nb_ne2k = 0;

    if (nb_ne2k == NE2000_NB_MAX)
        return;
    isa_ne2000_init(bus, ne2000_io[nb_ne2k],
                    ne2000_irq[nb_ne2k], nd);
    nb_ne2k++;
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

void pc_acpi_smi_interrupt(void *opaque, int irq, int level)
{
    X86CPU *cpu = opaque;

    if (level) {
        cpu_interrupt(CPU(cpu), CPU_INTERRUPT_SMI);
    }
}

static X86CPU *pc_new_cpu(const char *cpu_model, int64_t apic_id,
                          Error **errp)
{
    X86CPU *cpu = NULL;
    Error *local_err = NULL;

    cpu = cpu_x86_create(cpu_model, &local_err);
    if (local_err != NULL) {
        goto out;
    }

    object_property_set_int(OBJECT(cpu), apic_id, "apic-id", &local_err);
    object_property_set_bool(OBJECT(cpu), true, "realized", &local_err);

out:
    if (local_err) {
        error_propagate(errp, local_err);
        object_unref(OBJECT(cpu));
        cpu = NULL;
    }
    return cpu;
}

void pc_hot_add_cpu(const int64_t id, Error **errp)
{
    X86CPU *cpu;
    MachineState *machine = MACHINE(qdev_get_machine());
    int64_t apic_id = x86_cpu_apic_id_from_index(id);
    Error *local_err = NULL;

    if (id < 0) {
        error_setg(errp, "Invalid CPU id: %" PRIi64, id);
        return;
    }

    if (cpu_exists(apic_id)) {
        error_setg(errp, "Unable to add CPU: %" PRIi64
                   ", it already exists", id);
        return;
    }

    if (id >= max_cpus) {
        error_setg(errp, "Unable to add CPU: %" PRIi64
                   ", max allowed: %d", id, max_cpus - 1);
        return;
    }

    if (apic_id >= ACPI_CPU_HOTPLUG_ID_LIMIT) {
        error_setg(errp, "Unable to add CPU: %" PRIi64
                   ", resulting APIC ID (%" PRIi64 ") is too large",
                   id, apic_id);
        return;
    }

    cpu = pc_new_cpu(machine->cpu_model, apic_id, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    object_unref(OBJECT(cpu));
}

void pc_cpus_init(PCMachineState *pcms)
{
    int i;
    X86CPU *cpu = NULL;
    MachineState *machine = MACHINE(pcms);

    /* init CPUs */
    if (machine->cpu_model == NULL) {
#ifdef TARGET_X86_64
        machine->cpu_model = "qemu64";
#else
        machine->cpu_model = "qemu32";
#endif
    }

    /* Calculates the limit to CPU APIC ID values
     *
     * Limit for the APIC ID value, so that all
     * CPU APIC IDs are < pcms->apic_id_limit.
     *
     * This is used for FW_CFG_MAX_CPUS. See comments on bochs_bios_init().
     */
    pcms->apic_id_limit = x86_cpu_apic_id_from_index(max_cpus - 1) + 1;
    if (pcms->apic_id_limit > ACPI_CPU_HOTPLUG_ID_LIMIT) {
        error_report("max_cpus is too large. APIC ID of last CPU is %u",
                     pcms->apic_id_limit - 1);
        exit(1);
    }

    pcms->possible_cpus = g_malloc0(sizeof(CPUArchIdList) +
                                    sizeof(CPUArchId) * max_cpus);
    for (i = 0; i < max_cpus; i++) {
        pcms->possible_cpus->cpus[i].arch_id = x86_cpu_apic_id_from_index(i);
        pcms->possible_cpus->len++;
        if (i < smp_cpus) {
            cpu = pc_new_cpu(machine->cpu_model, x86_cpu_apic_id_from_index(i),
                             &error_fatal);
            pcms->possible_cpus->cpus[i].cpu = CPU(cpu);
            object_unref(OBJECT(cpu));
        }
    }

    /* tell smbios about cpuid version and features */
    smbios_set_cpuid(cpu->env.cpuid_version, cpu->env.features[FEAT_1_EDX]);
}

static
void pc_machine_done(Notifier *notifier, void *data)
{
    PCMachineState *pcms = container_of(notifier,
                                        PCMachineState, machine_done);
    PCIBus *bus = pcms->bus;

    if (bus) {
        int extra_hosts = 0;

        QLIST_FOREACH(bus, &bus->child, sibling) {
            /* look for expander root buses */
            if (pci_bus_is_root(bus)) {
                extra_hosts++;
            }
        }
        if (extra_hosts && pcms->fw_cfg) {
            uint64_t *val = g_malloc(sizeof(*val));
            *val = cpu_to_le64(extra_hosts);
            fw_cfg_add_file(pcms->fw_cfg,
                    "etc/extra-pci-roots", val, sizeof(*val));
        }
    }

    acpi_setup();
    if (pcms->fw_cfg) {
        pc_build_smbios(pcms->fw_cfg);
    }
}

void pc_guest_info_init(PCMachineState *pcms)
{
    int i, j;

    pcms->apic_xrupt_override = kvm_allows_irq0_override();
    pcms->numa_nodes = nb_numa_nodes;
    pcms->node_mem = g_malloc0(pcms->numa_nodes *
                                    sizeof *pcms->node_mem);
    for (i = 0; i < nb_numa_nodes; i++) {
        pcms->node_mem[i] = numa_info[i].node_mem;
    }

    pcms->node_cpu = g_malloc0(pcms->apic_id_limit *
                                     sizeof *pcms->node_cpu);

    for (i = 0; i < max_cpus; i++) {
        unsigned int apic_id = x86_cpu_apic_id_from_index(i);
        assert(apic_id < pcms->apic_id_limit);
        for (j = 0; j < nb_numa_nodes; j++) {
            if (test_bit(i, numa_info[j].node_cpu)) {
                pcms->node_cpu[apic_id] = j;
                break;
            }
        }
    }

    pcms->machine_done.notify = pc_machine_done;
    qemu_add_machine_init_done_notifier(&pcms->machine_done);
}

/* setup pci memory address space mapping into system address space */
void pc_pci_as_mapping_init(Object *owner, MemoryRegion *system_memory,
                            MemoryRegion *pci_address_space)
{
    /* Set to lower priority than RAM */
    memory_region_add_subregion_overlap(system_memory, 0x0,
                                        pci_address_space, -1);
}

void pc_acpi_init(const char *default_dsdt)
{
    char *filename;

    if (acpi_tables != NULL) {
        /* manually set via -acpitable, leave it alone */
        return;
    }

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, default_dsdt);
    if (filename == NULL) {
        fprintf(stderr, "WARNING: failed to find %s\n", default_dsdt);
    } else {
        QemuOpts *opts = qemu_opts_create(qemu_find_opts("acpi"), NULL, 0,
                                          &error_abort);
        Error *err = NULL;

        qemu_opt_set(opts, "file", filename, &error_abort);

        acpi_table_add_builtin(opts, &err);
        if (err) {
            error_reportf_err(err, "WARNING: failed to load %s: ",
                              filename);
        }
        g_free(filename);
    }
}

void xen_load_linux(PCMachineState *pcms)
{
    int i;
    FWCfgState *fw_cfg;

    assert(MACHINE(pcms)->kernel_filename != NULL);

    fw_cfg = fw_cfg_init_io(FW_CFG_IO_BASE);
    rom_set_fw(fw_cfg);

    load_linux(pcms, fw_cfg);
    for (i = 0; i < nb_option_roms; i++) {
        assert(!strcmp(option_rom[i].name, "linuxboot.bin") ||
               !strcmp(option_rom[i].name, "multiboot.bin"));
        rom_add_option(option_rom[i].name, option_rom[i].bootindex);
    }
    pcms->fw_cfg = fw_cfg;
}

void pc_memory_init(PCMachineState *pcms,
                    MemoryRegion *system_memory,
                    MemoryRegion *rom_memory,
                    MemoryRegion **ram_memory)
{
    int linux_boot, i;
    MemoryRegion *ram, *option_rom_mr;
    MemoryRegion *ram_below_4g, *ram_above_4g;
    FWCfgState *fw_cfg;
    MachineState *machine = MACHINE(pcms);
    PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);

    assert(machine->ram_size == pcms->below_4g_mem_size +
                                pcms->above_4g_mem_size);

    linux_boot = (machine->kernel_filename != NULL);

    /* Allocate RAM.  We allocate it as a single memory region and use
     * aliases to address portions of it, mostly for backwards compatibility
     * with older qemus that used qemu_ram_alloc().
     */
    ram = g_malloc(sizeof(*ram));
    memory_region_allocate_system_memory(ram, NULL, "pc.ram",
                                         machine->ram_size);
    *ram_memory = ram;
    ram_below_4g = g_malloc(sizeof(*ram_below_4g));
    memory_region_init_alias(ram_below_4g, NULL, "ram-below-4g", ram,
                             0, pcms->below_4g_mem_size);
    memory_region_add_subregion(system_memory, 0, ram_below_4g);
    e820_add_entry(0, pcms->below_4g_mem_size, E820_RAM);
    if (pcms->above_4g_mem_size > 0) {
        ram_above_4g = g_malloc(sizeof(*ram_above_4g));
        memory_region_init_alias(ram_above_4g, NULL, "ram-above-4g", ram,
                                 pcms->below_4g_mem_size,
                                 pcms->above_4g_mem_size);
        memory_region_add_subregion(system_memory, 0x100000000ULL,
                                    ram_above_4g);
        e820_add_entry(0x100000000ULL, pcms->above_4g_mem_size, E820_RAM);
    }

    if (!pcmc->has_reserved_memory &&
        (machine->ram_slots ||
         (machine->maxram_size > machine->ram_size))) {
        MachineClass *mc = MACHINE_GET_CLASS(machine);

        error_report("\"-memory 'slots|maxmem'\" is not supported by: %s",
                     mc->name);
        exit(EXIT_FAILURE);
    }

    /* initialize hotplug memory address space */
    if (pcmc->has_reserved_memory &&
        (machine->ram_size < machine->maxram_size)) {
        ram_addr_t hotplug_mem_size =
            machine->maxram_size - machine->ram_size;

        if (machine->ram_slots > ACPI_MAX_RAM_SLOTS) {
            error_report("unsupported amount of memory slots: %"PRIu64,
                         machine->ram_slots);
            exit(EXIT_FAILURE);
        }

        if (QEMU_ALIGN_UP(machine->maxram_size,
                          TARGET_PAGE_SIZE) != machine->maxram_size) {
            error_report("maximum memory size must by aligned to multiple of "
                         "%d bytes", TARGET_PAGE_SIZE);
            exit(EXIT_FAILURE);
        }

        pcms->hotplug_memory.base =
            ROUND_UP(0x100000000ULL + pcms->above_4g_mem_size, 1ULL << 30);

        if (pcmc->enforce_aligned_dimm) {
            /* size hotplug region assuming 1G page max alignment per slot */
            hotplug_mem_size += (1ULL << 30) * machine->ram_slots;
        }

        if ((pcms->hotplug_memory.base + hotplug_mem_size) <
            hotplug_mem_size) {
            error_report("unsupported amount of maximum memory: " RAM_ADDR_FMT,
                         machine->maxram_size);
            exit(EXIT_FAILURE);
        }

        memory_region_init(&pcms->hotplug_memory.mr, OBJECT(pcms),
                           "hotplug-memory", hotplug_mem_size);
        memory_region_add_subregion(system_memory, pcms->hotplug_memory.base,
                                    &pcms->hotplug_memory.mr);
    }

    /* Initialize PC system firmware */
    pc_system_firmware_init(rom_memory, !pcmc->pci_enabled);

    option_rom_mr = g_malloc(sizeof(*option_rom_mr));
    memory_region_init_ram(option_rom_mr, NULL, "pc.rom", PC_ROM_SIZE,
                           &error_fatal);
    vmstate_register_ram_global(option_rom_mr);
    memory_region_add_subregion_overlap(rom_memory,
                                        PC_ROM_MIN_VGA,
                                        option_rom_mr,
                                        1);

    fw_cfg = bochs_bios_init(&address_space_memory, pcms);

    rom_set_fw(fw_cfg);

    if (pcmc->has_reserved_memory && pcms->hotplug_memory.base) {
        uint64_t *val = g_malloc(sizeof(*val));
        PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);
        uint64_t res_mem_end = pcms->hotplug_memory.base;

        if (!pcmc->broken_reserved_end) {
            res_mem_end += memory_region_size(&pcms->hotplug_memory.mr);
        }
        *val = cpu_to_le64(ROUND_UP(res_mem_end, 0x1ULL << 30));
        fw_cfg_add_file(fw_cfg, "etc/reserved-memory-end", val, sizeof(*val));
    }

    if (linux_boot) {
        load_linux(pcms, fw_cfg);
    }

    for (i = 0; i < nb_option_roms; i++) {
        rom_add_option(option_rom[i].name, option_rom[i].bootindex);
    }
    pcms->fw_cfg = fw_cfg;
}

qemu_irq pc_allocate_cpu_irq(void)
{
    return qemu_allocate_irq(pic_irq_request, NULL, 0);
}

DeviceState *pc_vga_init(ISABus *isa_bus, PCIBus *pci_bus)
{
    DeviceState *dev = NULL;

    rom_set_order_override(FW_CFG_ORDER_OVERRIDE_VGA);
    if (pci_bus) {
        PCIDevice *pcidev = pci_vga_init(pci_bus);
        dev = pcidev ? &pcidev->qdev : NULL;
    } else if (isa_bus) {
        ISADevice *isadev = isa_vga_init(isa_bus);
        dev = isadev ? DEVICE(isadev) : NULL;
    }
    rom_reset_order_override();
    return dev;
}

static const MemoryRegionOps ioport80_io_ops = {
    .write = ioport80_write,
    .read = ioport80_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static const MemoryRegionOps ioportF0_io_ops = {
    .write = ioportF0_write,
    .read = ioportF0_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

void pc_basic_device_init(ISABus *isa_bus, qemu_irq *gsi,
                          ISADevice **rtc_state,
                          bool create_fdctrl,
                          bool no_vmport,
                          uint32_t hpet_irqs)
{
    int i;
    DriveInfo *fd[MAX_FD];
    DeviceState *hpet = NULL;
    int pit_isa_irq = 0;
    qemu_irq pit_alt_irq = NULL;
    qemu_irq rtc_irq = NULL;
    qemu_irq *a20_line;
    ISADevice *i8042, *port92, *vmmouse, *pit = NULL;
    MemoryRegion *ioport80_io = g_new(MemoryRegion, 1);
    MemoryRegion *ioportF0_io = g_new(MemoryRegion, 1);

    memory_region_init_io(ioport80_io, NULL, &ioport80_io_ops, NULL, "ioport80", 1);
    memory_region_add_subregion(isa_bus->address_space_io, 0x80, ioport80_io);

    memory_region_init_io(ioportF0_io, NULL, &ioportF0_io_ops, NULL, "ioportF0", 1);
    memory_region_add_subregion(isa_bus->address_space_io, 0xf0, ioportF0_io);

    /*
     * Check if an HPET shall be created.
     *
     * Without KVM_CAP_PIT_STATE2, we cannot switch off the in-kernel PIT
     * when the HPET wants to take over. Thus we have to disable the latter.
     */
    if (!no_hpet && (!kvm_irqchip_in_kernel() || kvm_has_pit_state2())) {
        /* In order to set property, here not using sysbus_try_create_simple */
        hpet = qdev_try_create(NULL, TYPE_HPET);
        if (hpet) {
            /* For pc-piix-*, hpet's intcap is always IRQ2. For pc-q35-1.7
             * and earlier, use IRQ2 for compat. Otherwise, use IRQ16~23,
             * IRQ8 and IRQ2.
             */
            uint8_t compat = object_property_get_int(OBJECT(hpet),
                    HPET_INTCAP, NULL);
            if (!compat) {
                qdev_prop_set_uint32(hpet, HPET_INTCAP, hpet_irqs);
            }
            qdev_init_nofail(hpet);
            sysbus_mmio_map(SYS_BUS_DEVICE(hpet), 0, HPET_BASE);

            for (i = 0; i < GSI_NUM_PINS; i++) {
                sysbus_connect_irq(SYS_BUS_DEVICE(hpet), i, gsi[i]);
            }
            pit_isa_irq = -1;
            pit_alt_irq = qdev_get_gpio_in(hpet, HPET_LEGACY_PIT_INT);
            rtc_irq = qdev_get_gpio_in(hpet, HPET_LEGACY_RTC_INT);
        }
    }
    *rtc_state = rtc_init(isa_bus, 2000, rtc_irq);

    qemu_register_boot_set(pc_boot_set, *rtc_state);

    if (!xen_enabled()) {
        if (kvm_pit_in_kernel()) {
            pit = kvm_pit_init(isa_bus, 0x40);
        } else {
            pit = pit_init(isa_bus, 0x40, pit_isa_irq, pit_alt_irq);
        }
        if (hpet) {
            /* connect PIT to output control line of the HPET */
            qdev_connect_gpio_out(hpet, 0, qdev_get_gpio_in(DEVICE(pit), 0));
        }
        pcspk_init(isa_bus, pit);
    }

    serial_hds_isa_init(isa_bus, MAX_SERIAL_PORTS);
    parallel_hds_isa_init(isa_bus, MAX_PARALLEL_PORTS);

    a20_line = qemu_allocate_irqs(handle_a20_line_change, first_cpu, 2);
    i8042 = isa_create_simple(isa_bus, "i8042");
    i8042_setup_a20_line(i8042, &a20_line[0]);
    if (!no_vmport) {
        vmport_init(isa_bus);
        vmmouse = isa_try_create(isa_bus, "vmmouse");
    } else {
        vmmouse = NULL;
    }
    if (vmmouse) {
        DeviceState *dev = DEVICE(vmmouse);
        qdev_prop_set_ptr(dev, "ps2_mouse", i8042);
        qdev_init_nofail(dev);
    }
    port92 = isa_create_simple(isa_bus, "port92");
    port92_init(port92, &a20_line[1]);

    DMA_init(isa_bus, 0);

    for(i = 0; i < MAX_FD; i++) {
        fd[i] = drive_get(IF_FLOPPY, 0, i);
        create_fdctrl |= !!fd[i];
    }
    if (create_fdctrl) {
        fdctrl_init_isa(isa_bus, fd);
    }
}

void pc_nic_init(ISABus *isa_bus, PCIBus *pci_bus)
{
    int i;

    rom_set_order_override(FW_CFG_ORDER_OVERRIDE_NIC);
    for (i = 0; i < nb_nics; i++) {
        NICInfo *nd = &nd_table[i];

        if (!pci_bus || (nd->model && strcmp(nd->model, "ne2k_isa") == 0)) {
            pc_init_ne2k_isa(isa_bus, nd);
        } else {
            pci_nic_init_nofail(nd, pci_bus, "e1000", NULL);
        }
    }
    rom_reset_order_override();
}

void pc_pci_device_init(PCIBus *pci_bus)
{
    int max_bus;
    int bus;

    max_bus = drive_get_max_bus(IF_SCSI);
    for (bus = 0; bus <= max_bus; bus++) {
        pci_create_simple(pci_bus, -1, "lsi53c895a");
    }
}

void ioapic_init_gsi(GSIState *gsi_state, const char *parent_name)
{
    DeviceState *dev;
    SysBusDevice *d;
    unsigned int i;

    if (kvm_ioapic_in_kernel()) {
        dev = qdev_create(NULL, "kvm-ioapic");
    } else {
        dev = qdev_create(NULL, "ioapic");
    }
    if (parent_name) {
        object_property_add_child(object_resolve_path(parent_name, NULL),
                                  "ioapic", OBJECT(dev), NULL);
    }
    qdev_init_nofail(dev);
    d = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(d, 0, IO_APIC_DEFAULT_ADDRESS);

    for (i = 0; i < IOAPIC_NUM_PINS; i++) {
        gsi_state->ioapic_irq[i] = qdev_get_gpio_in(dev, i);
    }
}

static void pc_dimm_plug(HotplugHandler *hotplug_dev,
                         DeviceState *dev, Error **errp)
{
    HotplugHandlerClass *hhc;
    Error *local_err = NULL;
    PCMachineState *pcms = PC_MACHINE(hotplug_dev);
    PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);
    PCDIMMDevice *dimm = PC_DIMM(dev);
    PCDIMMDeviceClass *ddc = PC_DIMM_GET_CLASS(dimm);
    MemoryRegion *mr = ddc->get_memory_region(dimm);
    uint64_t align = TARGET_PAGE_SIZE;

    if (memory_region_get_alignment(mr) && pcmc->enforce_aligned_dimm) {
        align = memory_region_get_alignment(mr);
    }

    if (!pcms->acpi_dev) {
        error_setg(&local_err,
                   "memory hotplug is not enabled: missing acpi device");
        goto out;
    }

    pc_dimm_memory_plug(dev, &pcms->hotplug_memory, mr, align, &local_err);
    if (local_err) {
        goto out;
    }

    hhc = HOTPLUG_HANDLER_GET_CLASS(pcms->acpi_dev);
    hhc->plug(HOTPLUG_HANDLER(pcms->acpi_dev), dev, &error_abort);
out:
    error_propagate(errp, local_err);
}

static void pc_dimm_unplug_request(HotplugHandler *hotplug_dev,
                                   DeviceState *dev, Error **errp)
{
    HotplugHandlerClass *hhc;
    Error *local_err = NULL;
    PCMachineState *pcms = PC_MACHINE(hotplug_dev);

    if (!pcms->acpi_dev) {
        error_setg(&local_err,
                   "memory hotplug is not enabled: missing acpi device");
        goto out;
    }

    hhc = HOTPLUG_HANDLER_GET_CLASS(pcms->acpi_dev);
    hhc->unplug_request(HOTPLUG_HANDLER(pcms->acpi_dev), dev, &local_err);

out:
    error_propagate(errp, local_err);
}

static void pc_dimm_unplug(HotplugHandler *hotplug_dev,
                           DeviceState *dev, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(hotplug_dev);
    PCDIMMDevice *dimm = PC_DIMM(dev);
    PCDIMMDeviceClass *ddc = PC_DIMM_GET_CLASS(dimm);
    MemoryRegion *mr = ddc->get_memory_region(dimm);
    HotplugHandlerClass *hhc;
    Error *local_err = NULL;

    hhc = HOTPLUG_HANDLER_GET_CLASS(pcms->acpi_dev);
    hhc->unplug(HOTPLUG_HANDLER(pcms->acpi_dev), dev, &local_err);

    if (local_err) {
        goto out;
    }

    pc_dimm_memory_unplug(dev, &pcms->hotplug_memory, mr);
    object_unparent(OBJECT(dev));

 out:
    error_propagate(errp, local_err);
}

static int pc_apic_cmp(const void *a, const void *b)
{
   CPUArchId *apic_a = (CPUArchId *)a;
   CPUArchId *apic_b = (CPUArchId *)b;

   return apic_a->arch_id - apic_b->arch_id;
}

static void pc_cpu_plug(HotplugHandler *hotplug_dev,
                        DeviceState *dev, Error **errp)
{
    CPUClass *cc = CPU_GET_CLASS(dev);
    CPUArchId apic_id, *found_cpu;
    HotplugHandlerClass *hhc;
    Error *local_err = NULL;
    PCMachineState *pcms = PC_MACHINE(hotplug_dev);

    if (!dev->hotplugged) {
        goto out;
    }

    if (!pcms->acpi_dev) {
        error_setg(&local_err,
                   "cpu hotplug is not enabled: missing acpi device");
        goto out;
    }

    hhc = HOTPLUG_HANDLER_GET_CLASS(pcms->acpi_dev);
    hhc->plug(HOTPLUG_HANDLER(pcms->acpi_dev), dev, &local_err);
    if (local_err) {
        goto out;
    }

    /* increment the number of CPUs */
    rtc_set_memory(pcms->rtc, 0x5f, rtc_get_memory(pcms->rtc, 0x5f) + 1);

    apic_id.arch_id = cc->get_arch_id(CPU(dev));
    found_cpu = bsearch(&apic_id, pcms->possible_cpus->cpus,
        pcms->possible_cpus->len, sizeof(*pcms->possible_cpus->cpus),
        pc_apic_cmp);
    assert(found_cpu);
    found_cpu->cpu = CPU(dev);
out:
    error_propagate(errp, local_err);
}

static void pc_machine_device_plug_cb(HotplugHandler *hotplug_dev,
                                      DeviceState *dev, Error **errp)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        pc_dimm_plug(hotplug_dev, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_CPU)) {
        pc_cpu_plug(hotplug_dev, dev, errp);
    }
}

static void pc_machine_device_unplug_request_cb(HotplugHandler *hotplug_dev,
                                                DeviceState *dev, Error **errp)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        pc_dimm_unplug_request(hotplug_dev, dev, errp);
    } else {
        error_setg(errp, "acpi: device unplug request for not supported device"
                   " type: %s", object_get_typename(OBJECT(dev)));
    }
}

static void pc_machine_device_unplug_cb(HotplugHandler *hotplug_dev,
                                        DeviceState *dev, Error **errp)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        pc_dimm_unplug(hotplug_dev, dev, errp);
    } else {
        error_setg(errp, "acpi: device unplug for not supported device"
                   " type: %s", object_get_typename(OBJECT(dev)));
    }
}

static HotplugHandler *pc_get_hotpug_handler(MachineState *machine,
                                             DeviceState *dev)
{
    PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(machine);

    if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM) ||
        object_dynamic_cast(OBJECT(dev), TYPE_CPU)) {
        return HOTPLUG_HANDLER(machine);
    }

    return pcmc->get_hotplug_handler ?
        pcmc->get_hotplug_handler(machine, dev) : NULL;
}

static void
pc_machine_get_hotplug_memory_region_size(Object *obj, Visitor *v,
                                          const char *name, void *opaque,
                                          Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);
    int64_t value = memory_region_size(&pcms->hotplug_memory.mr);

    visit_type_int(v, name, &value, errp);
}

static void pc_machine_get_max_ram_below_4g(Object *obj, Visitor *v,
                                            const char *name, void *opaque,
                                            Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);
    uint64_t value = pcms->max_ram_below_4g;

    visit_type_size(v, name, &value, errp);
}

static void pc_machine_set_max_ram_below_4g(Object *obj, Visitor *v,
                                            const char *name, void *opaque,
                                            Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);
    Error *error = NULL;
    uint64_t value;

    visit_type_size(v, name, &value, &error);
    if (error) {
        error_propagate(errp, error);
        return;
    }
    if (value > (1ULL << 32)) {
        error_setg(&error,
                   "Machine option 'max-ram-below-4g=%"PRIu64
                   "' expects size less than or equal to 4G", value);
        error_propagate(errp, error);
        return;
    }

    if (value < (1ULL << 20)) {
        error_report("Warning: small max_ram_below_4g(%"PRIu64
                     ") less than 1M.  BIOS may not work..",
                     value);
    }

    pcms->max_ram_below_4g = value;
}

static void pc_machine_get_vmport(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);
    OnOffAuto vmport = pcms->vmport;

    visit_type_OnOffAuto(v, name, &vmport, errp);
}

static void pc_machine_set_vmport(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &pcms->vmport, errp);
}

bool pc_machine_is_smm_enabled(PCMachineState *pcms)
{
    bool smm_available = false;

    if (pcms->smm == ON_OFF_AUTO_OFF) {
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

    if (pcms->smm == ON_OFF_AUTO_ON) {
        error_report("System Management Mode not supported by this hypervisor.");
        exit(1);
    }
    return false;
}

static void pc_machine_get_smm(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);
    OnOffAuto smm = pcms->smm;

    visit_type_OnOffAuto(v, name, &smm, errp);
}

static void pc_machine_set_smm(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &pcms->smm, errp);
}

static bool pc_machine_get_nvdimm(Object *obj, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    return pcms->acpi_nvdimm_state.is_enabled;
}

static void pc_machine_set_nvdimm(Object *obj, bool value, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    pcms->acpi_nvdimm_state.is_enabled = value;
}

static void pc_machine_initfn(Object *obj)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    object_property_add(obj, PC_MACHINE_MEMHP_REGION_SIZE, "int",
                        pc_machine_get_hotplug_memory_region_size,
                        NULL, NULL, NULL, &error_abort);

    pcms->max_ram_below_4g = 0xe0000000; /* 3.5G */
    object_property_add(obj, PC_MACHINE_MAX_RAM_BELOW_4G, "size",
                        pc_machine_get_max_ram_below_4g,
                        pc_machine_set_max_ram_below_4g,
                        NULL, NULL, &error_abort);
    object_property_set_description(obj, PC_MACHINE_MAX_RAM_BELOW_4G,
                                    "Maximum ram below the 4G boundary (32bit boundary)",
                                    &error_abort);

    pcms->smm = ON_OFF_AUTO_AUTO;
    object_property_add(obj, PC_MACHINE_SMM, "OnOffAuto",
                        pc_machine_get_smm,
                        pc_machine_set_smm,
                        NULL, NULL, &error_abort);
    object_property_set_description(obj, PC_MACHINE_SMM,
                                    "Enable SMM (pc & q35)",
                                    &error_abort);

    pcms->vmport = ON_OFF_AUTO_AUTO;
    object_property_add(obj, PC_MACHINE_VMPORT, "OnOffAuto",
                        pc_machine_get_vmport,
                        pc_machine_set_vmport,
                        NULL, NULL, &error_abort);
    object_property_set_description(obj, PC_MACHINE_VMPORT,
                                    "Enable vmport (pc & q35)",
                                    &error_abort);

    /* nvdimm is disabled on default. */
    pcms->acpi_nvdimm_state.is_enabled = false;
    object_property_add_bool(obj, PC_MACHINE_NVDIMM, pc_machine_get_nvdimm,
                             pc_machine_set_nvdimm, &error_abort);
}

static void pc_machine_reset(void)
{
    CPUState *cs;
    X86CPU *cpu;

    qemu_devices_reset();

    /* Reset APIC after devices have been reset to cancel
     * any changes that qemu_devices_reset() might have done.
     */
    CPU_FOREACH(cs) {
        cpu = X86_CPU(cs);

        if (cpu->apic_state) {
            device_reset(cpu->apic_state);
        }
    }
}

static unsigned pc_cpu_index_to_socket_id(unsigned cpu_index)
{
    X86CPUTopoInfo topo;
    x86_topo_ids_from_idx(smp_cores, smp_threads, cpu_index,
                          &topo);
    return topo.pkg_id;
}

static CPUArchIdList *pc_possible_cpu_arch_ids(MachineState *machine)
{
    PCMachineState *pcms = PC_MACHINE(machine);
    int len = sizeof(CPUArchIdList) +
              sizeof(CPUArchId) * (pcms->possible_cpus->len);
    CPUArchIdList *list = g_malloc(len);

    memcpy(list, pcms->possible_cpus, len);
    return list;
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

static void pc_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    PCMachineClass *pcmc = PC_MACHINE_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);
    NMIClass *nc = NMI_CLASS(oc);

    pcmc->get_hotplug_handler = mc->get_hotplug_handler;
    pcmc->pci_enabled = true;
    pcmc->has_acpi_build = true;
    pcmc->rsdp_in_ram = true;
    pcmc->smbios_defaults = true;
    pcmc->smbios_uuid_encoded = true;
    pcmc->gigabyte_align = true;
    pcmc->has_reserved_memory = true;
    pcmc->kvmclock_enabled = true;
    pcmc->enforce_aligned_dimm = true;
    /* BIOS ACPI tables: 128K. Other BIOS datastructures: less than 4K reported
     * to be used at the moment, 32K should be enough for a while.  */
    pcmc->acpi_data_size = 0x20000 + 0x8000;
    pcmc->save_tsc_khz = true;
    mc->get_hotplug_handler = pc_get_hotpug_handler;
    mc->cpu_index_to_socket_id = pc_cpu_index_to_socket_id;
    mc->possible_cpu_arch_ids = pc_possible_cpu_arch_ids;
    mc->default_boot_order = "cad";
    mc->hot_add_cpu = pc_hot_add_cpu;
    mc->max_cpus = 255;
    mc->reset = pc_machine_reset;
    hc->plug = pc_machine_device_plug_cb;
    hc->unplug_request = pc_machine_device_unplug_request_cb;
    hc->unplug = pc_machine_device_unplug_cb;
    nc->nmi_monitor_handler = x86_nmi;
}

static const TypeInfo pc_machine_info = {
    .name = TYPE_PC_MACHINE,
    .parent = TYPE_MACHINE,
    .abstract = true,
    .instance_size = sizeof(PCMachineState),
    .instance_init = pc_machine_initfn,
    .class_size = sizeof(PCMachineClass),
    .class_init = pc_machine_class_init,
    .interfaces = (InterfaceInfo[]) {
         { TYPE_HOTPLUG_HANDLER },
         { TYPE_NMI },
         { }
    },
};

static void pc_machine_register_types(void)
{
    type_register_static(&pc_machine_info);
}

type_init(pc_machine_register_types)
