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
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/char/serial.h"
#include "hw/i386/apic.h"
#include "hw/block/fdc.h"
#include "hw/ide.h"
#include "hw/pci/pci.h"
#include "monitor/monitor.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/timer/hpet.h"
#include "hw/i386/smbios.h"
#include "hw/loader.h"
#include "elf.h"
#include "multiboot.h"
#include "hw/timer/mc146818rtc.h"
#include "hw/timer/i8254.h"
#include "hw/audio/pcspk.h"
#include "hw/pci/msi.h"
#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "kvm_i386.h"
#include "hw/xen/xen.h"
#include "sysemu/blockdev.h"
#include "hw/block/block.h"
#include "ui/qemu-spice.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "sysemu/arch_init.h"
#include "qemu/bitmap.h"
#include "qemu/config-file.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/cpu_hotplug.h"
#include "hw/cpu/icc_bus.h"
#include "hw/boards.h"
#include "hw/pci/pci_host.h"
#include "acpi-build.h"

/* debug PC/ISA interrupts */
//#define DEBUG_IRQ

#ifdef DEBUG_IRQ
#define DPRINTF(fmt, ...)                                       \
    do { printf("CPUIRQ: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...)
#endif

/* Leave a chunk of memory at the top of RAM for the BIOS ACPI tables.  */
#define ACPI_DATA_SIZE       0x10000
#define BIOS_CFG_IOPORT 0x510
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

/* SMM support */

static cpu_set_smm_t smm_set;
static void *smm_arg;

void cpu_smm_register(cpu_set_smm_t callback, void *arg)
{
    assert(smm_set == NULL);
    assert(smm_arg == NULL);
    smm_set = callback;
    smm_arg = arg;
}

void cpu_smm_update(CPUX86State *env)
{
    if (smm_set && smm_arg && CPU(x86_env_get_cpu(env)) == first_cpu) {
        smm_set(!!(env->hflags & HF_SMM_MASK), smm_arg);
    }
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

static int cmos_get_fd_drive_type(FDriveType fd0)
{
    int val;

    switch (fd0) {
    case FDRIVE_DRV_144:
        /* 1.44 Mb 3"5 drive */
        val = 4;
        break;
    case FDRIVE_DRV_288:
        /* 2.88 Mb 3"5 drive */
        val = 5;
        break;
    case FDRIVE_DRV_120:
        /* 1.2 Mb 5"5 drive */
        val = 2;
        break;
    case FDRIVE_DRV_NONE:
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

static int set_boot_dev(ISADevice *s, const char *boot_device)
{
#define PC_MAX_BOOT_DEVICES 3
    int nbds, bds[3] = { 0, };
    int i;

    nbds = strlen(boot_device);
    if (nbds > PC_MAX_BOOT_DEVICES) {
        error_report("Too many boot devices for PC");
        return(1);
    }
    for (i = 0; i < nbds; i++) {
        bds[i] = boot_device2nibble(boot_device[i]);
        if (bds[i] == 0) {
            error_report("Invalid boot device for PC: '%c'",
                         boot_device[i]);
            return(1);
        }
    }
    rtc_set_memory(s, 0x3d, (bds[1] << 4) | bds[0]);
    rtc_set_memory(s, 0x38, (bds[2] << 4) | (fd_bootchk ? 0x0 : 0x1));
    return(0);
}

static int pc_boot_set(void *opaque, const char *boot_device)
{
    return set_boot_dev(opaque, boot_device);
}

typedef struct pc_cmos_init_late_arg {
    ISADevice *rtc_state;
    BusState *idebus[2];
} pc_cmos_init_late_arg;

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

    qemu_unregister_reset(pc_cmos_init_late, opaque);
}

typedef struct RTCCPUHotplugArg {
    Notifier cpu_added_notifier;
    ISADevice *rtc_state;
} RTCCPUHotplugArg;

static void rtc_notify_cpu_added(Notifier *notifier, void *data)
{
    RTCCPUHotplugArg *arg = container_of(notifier, RTCCPUHotplugArg,
                                         cpu_added_notifier);
    ISADevice *s = arg->rtc_state;

    /* increment the number of CPUs */
    rtc_set_memory(s, 0x5f, rtc_get_memory(s, 0x5f) + 1);
}

void pc_cmos_init(ram_addr_t ram_size, ram_addr_t above_4g_mem_size,
                  const char *boot_device,
                  ISADevice *floppy, BusState *idebus0, BusState *idebus1,
                  ISADevice *s)
{
    int val, nb, i;
    FDriveType fd_type[2] = { FDRIVE_DRV_NONE, FDRIVE_DRV_NONE };
    static pc_cmos_init_late_arg arg;
    static RTCCPUHotplugArg cpu_hotplug_cb;

    /* various important CMOS locations needed by PC/Bochs bios */

    /* memory size */
    /* base memory (first MiB) */
    val = MIN(ram_size / 1024, 640);
    rtc_set_memory(s, 0x15, val);
    rtc_set_memory(s, 0x16, val >> 8);
    /* extended memory (next 64MiB) */
    if (ram_size > 1024 * 1024) {
        val = (ram_size - 1024 * 1024) / 1024;
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
    if (ram_size > 16 * 1024 * 1024) {
        val = (ram_size - 16 * 1024 * 1024) / 65536;
    } else {
        val = 0;
    }
    if (val > 65535)
        val = 65535;
    rtc_set_memory(s, 0x34, val);
    rtc_set_memory(s, 0x35, val >> 8);
    /* memory above 4GiB */
    val = above_4g_mem_size / 65536;
    rtc_set_memory(s, 0x5b, val);
    rtc_set_memory(s, 0x5c, val >> 8);
    rtc_set_memory(s, 0x5d, val >> 16);

    /* set the number of CPU */
    rtc_set_memory(s, 0x5f, smp_cpus - 1);
    /* init CPU hotplug notifier */
    cpu_hotplug_cb.rtc_state = s;
    cpu_hotplug_cb.cpu_added_notifier.notify = rtc_notify_cpu_added;
    qemu_register_cpu_added_notifier(&cpu_hotplug_cb.cpu_added_notifier);

    if (set_boot_dev(s, boot_device)) {
        exit(1);
    }

    /* floppy type */
    if (floppy) {
        for (i = 0; i < 2; i++) {
            fd_type[i] = isa_fdc_get_drive_type(floppy, i);
        }
    }
    val = (cmos_get_fd_drive_type(fd_type[0]) << 4) |
        cmos_get_fd_drive_type(fd_type[1]);
    rtc_set_memory(s, 0x10, val);

    val = 0;
    nb = 0;
    if (fd_type[0] < FDRIVE_DRV_NONE) {
        nb++;
    }
    if (fd_type[1] < FDRIVE_DRV_NONE) {
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
    val |= 0x02; /* FPU is there */
    val |= 0x04; /* PS/2 mouse installed */
    rtc_set_memory(s, REG_EQUIPMENT_BYTE, val);

    /* hard drives */
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

    DPRINTF("port92: write 0x%02x\n", val);
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
    e820_table = g_realloc(e820_table,
                           sizeof(struct e820_entry) * (e820_entries+1));
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

/* Calculates the limit to CPU APIC ID values
 *
 * This function returns the limit for the APIC ID value, so that all
 * CPU APIC IDs are < pc_apic_id_limit().
 *
 * This is used for FW_CFG_MAX_CPUS. See comments on bochs_bios_init().
 */
static unsigned int pc_apic_id_limit(unsigned int max_cpus)
{
    return x86_cpu_apic_id_from_index(max_cpus - 1) + 1;
}

static FWCfgState *bochs_bios_init(void)
{
    FWCfgState *fw_cfg;
    uint8_t *smbios_tables, *smbios_anchor;
    size_t smbios_tables_len, smbios_anchor_len;
    uint64_t *numa_fw_cfg;
    int i, j;
    unsigned int apic_id_limit = pc_apic_id_limit(max_cpus);

    fw_cfg = fw_cfg_init(BIOS_CFG_IOPORT, BIOS_CFG_IOPORT + 1, 0, 0);
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
    fw_cfg_add_i16(fw_cfg, FW_CFG_MAX_CPUS, (uint16_t)apic_id_limit);
    fw_cfg_add_i32(fw_cfg, FW_CFG_ID, 1);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, (uint64_t)ram_size);
    fw_cfg_add_bytes(fw_cfg, FW_CFG_ACPI_TABLES,
                     acpi_tables, acpi_tables_len);
    fw_cfg_add_i32(fw_cfg, FW_CFG_IRQ0_OVERRIDE, kvm_allows_irq0_override());

    smbios_tables = smbios_get_table_legacy(&smbios_tables_len);
    if (smbios_tables) {
        fw_cfg_add_bytes(fw_cfg, FW_CFG_SMBIOS_ENTRIES,
                         smbios_tables, smbios_tables_len);
    }

    smbios_get_tables(&smbios_tables, &smbios_tables_len,
                      &smbios_anchor, &smbios_anchor_len);
    if (smbios_anchor) {
        fw_cfg_add_file(fw_cfg, "etc/smbios/smbios-tables",
                        smbios_tables, smbios_tables_len);
        fw_cfg_add_file(fw_cfg, "etc/smbios/smbios-anchor",
                        smbios_anchor, smbios_anchor_len);
    }

    fw_cfg_add_bytes(fw_cfg, FW_CFG_E820_TABLE,
                     &e820_reserve, sizeof(e820_reserve));
    fw_cfg_add_file(fw_cfg, "etc/e820", e820_table,
                    sizeof(struct e820_entry) * e820_entries);

    fw_cfg_add_bytes(fw_cfg, FW_CFG_HPET, &hpet_cfg, sizeof(hpet_cfg));
    /* allocate memory for the NUMA channel: one (64bit) word for the number
     * of nodes, one word for each VCPU->node and one word for each node to
     * hold the amount of memory.
     */
    numa_fw_cfg = g_new0(uint64_t, 1 + apic_id_limit + nb_numa_nodes);
    numa_fw_cfg[0] = cpu_to_le64(nb_numa_nodes);
    for (i = 0; i < max_cpus; i++) {
        unsigned int apic_id = x86_cpu_apic_id_from_index(i);
        assert(apic_id < apic_id_limit);
        for (j = 0; j < nb_numa_nodes; j++) {
            if (test_bit(i, node_cpumask[j])) {
                numa_fw_cfg[apic_id + 1] = cpu_to_le64(j);
                break;
            }
        }
    }
    for (i = 0; i < nb_numa_nodes; i++) {
        numa_fw_cfg[apic_id_limit + 1 + i] = cpu_to_le64(node_mem[i]);
    }
    fw_cfg_add_bytes(fw_cfg, FW_CFG_NUMA, numa_fw_cfg,
                     (1 + apic_id_limit + nb_numa_nodes) *
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

static void load_linux(FWCfgState *fw_cfg,
                       const char *kernel_filename,
                       const char *initrd_filename,
                       const char *kernel_cmdline,
                       hwaddr max_ram_size)
{
    uint16_t protocol;
    int setup_size, kernel_size, initrd_size = 0, cmdline_size;
    uint32_t initrd_max;
    uint8_t header[8192], *setup, *kernel, *initrd_data;
    hwaddr real_addr, prot_addr, cmdline_addr, initrd_addr = 0;
    FILE *f;
    char *vmode;

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

    if (initrd_max >= max_ram_size-ACPI_DATA_SIZE)
    	initrd_max = max_ram_size-ACPI_DATA_SIZE-1;

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
                          DeviceState *icc_bridge, Error **errp)
{
    X86CPU *cpu;
    Error *local_err = NULL;

    cpu = cpu_x86_create(cpu_model, icc_bridge, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return NULL;
    }

    object_property_set_int(OBJECT(cpu), apic_id, "apic-id", &local_err);
    object_property_set_bool(OBJECT(cpu), true, "realized", &local_err);

    if (local_err) {
        error_propagate(errp, local_err);
        object_unref(OBJECT(cpu));
        cpu = NULL;
    }
    return cpu;
}

static const char *current_cpu_model;

void pc_hot_add_cpu(const int64_t id, Error **errp)
{
    DeviceState *icc_bridge;
    int64_t apic_id = x86_cpu_apic_id_from_index(id);

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

    icc_bridge = DEVICE(object_resolve_path_type("icc-bridge",
                                                 TYPE_ICC_BRIDGE, NULL));
    pc_new_cpu(current_cpu_model, apic_id, icc_bridge, errp);
}

void pc_cpus_init(const char *cpu_model, DeviceState *icc_bridge)
{
    int i;
    X86CPU *cpu = NULL;
    Error *error = NULL;
    unsigned long apic_id_limit;

    /* init CPUs */
    if (cpu_model == NULL) {
#ifdef TARGET_X86_64
        cpu_model = "qemu64";
#else
        cpu_model = "qemu32";
#endif
    }
    current_cpu_model = cpu_model;

    apic_id_limit = pc_apic_id_limit(max_cpus);
    if (apic_id_limit > ACPI_CPU_HOTPLUG_ID_LIMIT) {
        error_report("max_cpus is too large. APIC ID of last CPU is %lu",
                     apic_id_limit - 1);
        exit(1);
    }

    for (i = 0; i < smp_cpus; i++) {
        cpu = pc_new_cpu(cpu_model, x86_cpu_apic_id_from_index(i),
                         icc_bridge, &error);
        if (error) {
            error_report("%s", error_get_pretty(error));
            error_free(error);
            exit(1);
        }
    }

    /* map APIC MMIO area if CPU has APIC */
    if (cpu && cpu->apic_state) {
        /* XXX: what if the base changes? */
        sysbus_mmio_map_overlap(SYS_BUS_DEVICE(icc_bridge), 0,
                                APIC_DEFAULT_ADDRESS, 0x1000);
    }

    /* tell smbios about cpuid version and features */
    smbios_set_cpuid(cpu->env.cpuid_version, cpu->env.features[FEAT_1_EDX]);
}

/* pci-info ROM file. Little endian format */
typedef struct PcRomPciInfo {
    uint64_t w32_min;
    uint64_t w32_max;
    uint64_t w64_min;
    uint64_t w64_max;
} PcRomPciInfo;

static void pc_fw_cfg_guest_info(PcGuestInfo *guest_info)
{
    PcRomPciInfo *info;
    Object *pci_info;
    bool ambiguous = false;

    if (!guest_info->has_pci_info || !guest_info->fw_cfg) {
        return;
    }
    pci_info = object_resolve_path_type("", TYPE_PCI_HOST_BRIDGE, &ambiguous);
    g_assert(!ambiguous);
    if (!pci_info) {
        return;
    }

    info = g_malloc(sizeof *info);
    info->w32_min = cpu_to_le64(object_property_get_int(pci_info,
                                PCI_HOST_PROP_PCI_HOLE_START, NULL));
    info->w32_max = cpu_to_le64(object_property_get_int(pci_info,
                                PCI_HOST_PROP_PCI_HOLE_END, NULL));
    info->w64_min = cpu_to_le64(object_property_get_int(pci_info,
                                PCI_HOST_PROP_PCI_HOLE64_START, NULL));
    info->w64_max = cpu_to_le64(object_property_get_int(pci_info,
                                PCI_HOST_PROP_PCI_HOLE64_END, NULL));
    /* Pass PCI hole info to guest via a side channel.
     * Required so guest PCI enumeration does the right thing. */
    fw_cfg_add_file(guest_info->fw_cfg, "etc/pci-info", info, sizeof *info);
}

typedef struct PcGuestInfoState {
    PcGuestInfo info;
    Notifier machine_done;
} PcGuestInfoState;

static
void pc_guest_info_machine_done(Notifier *notifier, void *data)
{
    PcGuestInfoState *guest_info_state = container_of(notifier,
                                                      PcGuestInfoState,
                                                      machine_done);
    pc_fw_cfg_guest_info(&guest_info_state->info);
    acpi_setup(&guest_info_state->info);
}

PcGuestInfo *pc_guest_info_init(ram_addr_t below_4g_mem_size,
                                ram_addr_t above_4g_mem_size)
{
    PcGuestInfoState *guest_info_state = g_malloc0(sizeof *guest_info_state);
    PcGuestInfo *guest_info = &guest_info_state->info;
    int i, j;

    guest_info->ram_size_below_4g = below_4g_mem_size;
    guest_info->ram_size = below_4g_mem_size + above_4g_mem_size;
    guest_info->apic_id_limit = pc_apic_id_limit(max_cpus);
    guest_info->apic_xrupt_override = kvm_allows_irq0_override();
    guest_info->numa_nodes = nb_numa_nodes;
    guest_info->node_mem = g_memdup(node_mem, guest_info->numa_nodes *
                                    sizeof *guest_info->node_mem);
    guest_info->node_cpu = g_malloc0(guest_info->apic_id_limit *
                                     sizeof *guest_info->node_cpu);

    for (i = 0; i < max_cpus; i++) {
        unsigned int apic_id = x86_cpu_apic_id_from_index(i);
        assert(apic_id < guest_info->apic_id_limit);
        for (j = 0; j < nb_numa_nodes; j++) {
            if (test_bit(i, node_cpumask[j])) {
                guest_info->node_cpu[apic_id] = j;
                break;
            }
        }
    }

    guest_info_state->machine_done.notify = pc_guest_info_machine_done;
    qemu_add_machine_init_done_notifier(&guest_info_state->machine_done);
    return guest_info;
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
        char *arg;
        QemuOpts *opts;
        Error *err = NULL;

        arg = g_strdup_printf("file=%s", filename);

        /* creates a deep copy of "arg" */
        opts = qemu_opts_parse(qemu_find_opts("acpi"), arg, 0);
        g_assert(opts != NULL);

        acpi_table_add_builtin(opts, &err);
        if (err) {
            error_report("WARNING: failed to load %s: %s", filename,
                         error_get_pretty(err));
            error_free(err);
        }
        g_free(arg);
        g_free(filename);
    }
}

FWCfgState *pc_memory_init(MemoryRegion *system_memory,
                           const char *kernel_filename,
                           const char *kernel_cmdline,
                           const char *initrd_filename,
                           ram_addr_t below_4g_mem_size,
                           ram_addr_t above_4g_mem_size,
                           MemoryRegion *rom_memory,
                           MemoryRegion **ram_memory,
                           PcGuestInfo *guest_info)
{
    int linux_boot, i;
    MemoryRegion *ram, *option_rom_mr;
    MemoryRegion *ram_below_4g, *ram_above_4g;
    FWCfgState *fw_cfg;
    ram_addr_t ram_size = below_4g_mem_size + above_4g_mem_size;
    MachineState *machine = MACHINE(qdev_get_machine());
    PCMachineState *pcms = PC_MACHINE(machine);

    linux_boot = (kernel_filename != NULL);

    /* Allocate RAM.  We allocate it as a single memory region and use
     * aliases to address portions of it, mostly for backwards compatibility
     * with older qemus that used qemu_ram_alloc().
     */
    ram = g_malloc(sizeof(*ram));
    memory_region_init_ram(ram, NULL, "pc.ram", ram_size);
    vmstate_register_ram_global(ram);
    *ram_memory = ram;
    ram_below_4g = g_malloc(sizeof(*ram_below_4g));
    memory_region_init_alias(ram_below_4g, NULL, "ram-below-4g", ram,
                             0, below_4g_mem_size);
    memory_region_add_subregion(system_memory, 0, ram_below_4g);
    e820_add_entry(0, below_4g_mem_size, E820_RAM);
    if (above_4g_mem_size > 0) {
        ram_above_4g = g_malloc(sizeof(*ram_above_4g));
        memory_region_init_alias(ram_above_4g, NULL, "ram-above-4g", ram,
                                 below_4g_mem_size, above_4g_mem_size);
        memory_region_add_subregion(system_memory, 0x100000000ULL,
                                    ram_above_4g);
        e820_add_entry(0x100000000ULL, above_4g_mem_size, E820_RAM);
    }

    /* initialize hotplug memory address space */
    if (ram_size < machine->maxram_size) {
        ram_addr_t hotplug_mem_size =
            machine->maxram_size - ram_size;

        pcms->hotplug_memory_base =
            ROUND_UP(0x100000000ULL + above_4g_mem_size, 1ULL << 30);

        if ((pcms->hotplug_memory_base + hotplug_mem_size) <
            hotplug_mem_size) {
            error_report("unsupported amount of maximum memory: " RAM_ADDR_FMT,
                         machine->maxram_size);
            exit(EXIT_FAILURE);
        }

        memory_region_init(&pcms->hotplug_memory, OBJECT(pcms),
                           "hotplug-memory", hotplug_mem_size);
        memory_region_add_subregion(system_memory, pcms->hotplug_memory_base,
                                    &pcms->hotplug_memory);
    }

    /* Initialize PC system firmware */
    pc_system_firmware_init(rom_memory, guest_info->isapc_ram_fw);

    option_rom_mr = g_malloc(sizeof(*option_rom_mr));
    memory_region_init_ram(option_rom_mr, NULL, "pc.rom", PC_ROM_SIZE);
    vmstate_register_ram_global(option_rom_mr);
    memory_region_add_subregion_overlap(rom_memory,
                                        PC_ROM_MIN_VGA,
                                        option_rom_mr,
                                        1);

    fw_cfg = bochs_bios_init();
    rom_set_fw(fw_cfg);

    if (linux_boot) {
        load_linux(fw_cfg, kernel_filename, initrd_filename, kernel_cmdline, below_4g_mem_size);
    }

    for (i = 0; i < nb_option_roms; i++) {
        rom_add_option(option_rom[i].name, option_rom[i].bootindex);
    }
    guest_info->fw_cfg = fw_cfg;
    return fw_cfg;
}

qemu_irq *pc_allocate_cpu_irq(void)
{
    return qemu_allocate_irqs(pic_irq_request, NULL, 1);
}

DeviceState *pc_vga_init(ISABus *isa_bus, PCIBus *pci_bus)
{
    DeviceState *dev = NULL;

    if (pci_bus) {
        PCIDevice *pcidev = pci_vga_init(pci_bus);
        dev = pcidev ? &pcidev->qdev : NULL;
    } else if (isa_bus) {
        ISADevice *isadev = isa_vga_init(isa_bus);
        dev = isadev ? DEVICE(isadev) : NULL;
    }
    return dev;
}

static void cpu_request_exit(void *opaque, int irq, int level)
{
    CPUState *cpu = current_cpu;

    if (cpu && level) {
        cpu_exit(cpu);
    }
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
                          ISADevice **floppy,
                          bool no_vmport,
                          uint32 hpet_irqs)
{
    int i;
    DriveInfo *fd[MAX_FD];
    DeviceState *hpet = NULL;
    int pit_isa_irq = 0;
    qemu_irq pit_alt_irq = NULL;
    qemu_irq rtc_irq = NULL;
    qemu_irq *a20_line;
    ISADevice *i8042, *port92, *vmmouse, *pit = NULL;
    qemu_irq *cpu_exit_irq;
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
        if (kvm_irqchip_in_kernel()) {
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

    for(i = 0; i < MAX_SERIAL_PORTS; i++) {
        if (serial_hds[i]) {
            serial_isa_init(isa_bus, i, serial_hds[i]);
        }
    }

    for(i = 0; i < MAX_PARALLEL_PORTS; i++) {
        if (parallel_hds[i]) {
            parallel_init(isa_bus, i, parallel_hds[i]);
        }
    }

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

    cpu_exit_irq = qemu_allocate_irqs(cpu_request_exit, NULL, 1);
    DMA_init(0, cpu_exit_irq);

    for(i = 0; i < MAX_FD; i++) {
        fd[i] = drive_get(IF_FLOPPY, 0, i);
    }
    *floppy = fdctrl_init_isa(isa_bus, fd);
}

void pc_nic_init(ISABus *isa_bus, PCIBus *pci_bus)
{
    int i;

    for (i = 0; i < nb_nics; i++) {
        NICInfo *nd = &nd_table[i];

        if (!pci_bus || (nd->model && strcmp(nd->model, "ne2k_isa") == 0)) {
            pc_init_ne2k_isa(isa_bus, nd);
        } else {
            pci_nic_init_nofail(nd, pci_bus, "e1000", NULL);
        }
    }
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

    if (kvm_irqchip_in_kernel()) {
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

static void pc_generic_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    QEMUMachine *qm = data;

    mc->name = qm->name;
    mc->alias = qm->alias;
    mc->desc = qm->desc;
    mc->init = qm->init;
    mc->reset = qm->reset;
    mc->hot_add_cpu = qm->hot_add_cpu;
    mc->kvm_type = qm->kvm_type;
    mc->block_default_type = qm->block_default_type;
    mc->max_cpus = qm->max_cpus;
    mc->no_serial = qm->no_serial;
    mc->no_parallel = qm->no_parallel;
    mc->use_virtcon = qm->use_virtcon;
    mc->use_sclp = qm->use_sclp;
    mc->no_floppy = qm->no_floppy;
    mc->no_cdrom = qm->no_cdrom;
    mc->no_sdcard = qm->no_sdcard;
    mc->is_default = qm->is_default;
    mc->default_machine_opts = qm->default_machine_opts;
    mc->default_boot_order = qm->default_boot_order;
    mc->compat_props = qm->compat_props;
    mc->hw_version = qm->hw_version;
}

void qemu_register_pc_machine(QEMUMachine *m)
{
    char *name = g_strconcat(m->name, TYPE_MACHINE_SUFFIX, NULL);
    TypeInfo ti = {
        .name       = name,
        .parent     = TYPE_PC_MACHINE,
        .class_init = pc_generic_machine_class_init,
        .class_data = (void *)m,
    };

    type_register(&ti);
    g_free(name);
}

static const TypeInfo pc_machine_info = {
    .name = TYPE_PC_MACHINE,
    .parent = TYPE_MACHINE,
    .abstract = true,
    .instance_size = sizeof(PCMachineState),
    .class_size = sizeof(PCMachineClass),
};

static void pc_machine_register_types(void)
{
    type_register_static(&pc_machine_info);
}

type_init(pc_machine_register_types)
