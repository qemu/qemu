/*
 * QEMU Motorla 680x0 Macintosh hardware System Emulator
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
#include "qemu/units.h"
#include "qemu/datadir.h"
#include "qemu/guest-random.h"
#include "sysemu/sysemu.h"
#include "cpu.h"
#include "hw/boards.h"
#include "hw/or-irq.h"
#include "elf.h"
#include "hw/loader.h"
#include "ui/console.h"
#include "hw/char/escc.h"
#include "hw/sysbus.h"
#include "hw/scsi/esp.h"
#include "standard-headers/asm-m68k/bootinfo.h"
#include "standard-headers/asm-m68k/bootinfo-mac.h"
#include "bootinfo.h"
#include "hw/m68k/q800.h"
#include "hw/m68k/q800-glue.h"
#include "hw/misc/mac_via.h"
#include "hw/misc/djmemc.h"
#include "hw/misc/iosb.h"
#include "hw/input/adb.h"
#include "hw/audio/asc.h"
#include "hw/nubus/mac-nubus-bridge.h"
#include "hw/display/macfb.h"
#include "hw/block/swim.h"
#include "net/net.h"
#include "net/util.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "sysemu/qtest.h"
#include "sysemu/runstate.h"
#include "sysemu/reset.h"
#include "migration/vmstate.h"

#define MACROM_ADDR     0x40800000
#define MACROM_SIZE     0x00100000

#define MACROM_FILENAME "MacROM.bin"

#define IO_BASE               0x50000000
#define IO_SLICE              0x00040000
#define IO_SLICE_MASK         (IO_SLICE - 1)
#define IO_SIZE               0x04000000

#define VIA_BASE              (IO_BASE + 0x00000)
#define SONIC_PROM_BASE       (IO_BASE + 0x08000)
#define SONIC_BASE            (IO_BASE + 0x0a000)
#define SCC_BASE              (IO_BASE + 0x0c020)
#define DJMEMC_BASE           (IO_BASE + 0x0e000)
#define ESP_BASE              (IO_BASE + 0x10000)
#define ESP_PDMA              (IO_BASE + 0x10100)
#define ASC_BASE              (IO_BASE + 0x14000)
#define IOSB_BASE             (IO_BASE + 0x18000)
#define SWIM_BASE             (IO_BASE + 0x1E000)

#define SONIC_PROM_SIZE       0x1000

/*
 * the video base, whereas it a Nubus address,
 * is needed by the kernel to have early display and
 * thus provided by the bootloader
 */
#define VIDEO_BASE            0xf9000000

#define MAC_CLOCK  3686418

/* Size of whole RAM area */
#define RAM_SIZE              0x40000000

/*
 * Slot 0x9 is reserved for use by the in-built framebuffer whilst only
 * slots 0xc, 0xd and 0xe physically exist on the Quadra 800
 */
#define Q800_NUBUS_SLOTS_AVAILABLE    (BIT(0x9) | BIT(0xc) | BIT(0xd) | \
                                       BIT(0xe))

/* Quadra 800 machine ID */
#define Q800_MACHINE_ID    0xa55a2bad


static void main_cpu_reset(void *opaque)
{
    M68kCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);

    cpu_reset(cs);
    cpu->env.aregs[7] = ldl_phys(cs->as, 0);
    cpu->env.pc = ldl_phys(cs->as, 4);
}

static void rerandomize_rng_seed(void *opaque)
{
    struct bi_record *rng_seed = opaque;
    qemu_guest_getrandom_nofail((void *)rng_seed->data + 2,
                                be16_to_cpu(*(uint16_t *)rng_seed->data));
}

static uint8_t fake_mac_rom[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

    /* offset: 0xa - mac_reset */

    /* via2[vDirB] |= VIA2B_vPower */
    0x20, 0x7C, 0x50, 0xF0, 0x24, 0x00, /* moveal VIA2_BASE+vDirB,%a0 */
    0x10, 0x10,                         /* moveb %a0@,%d0 */
    0x00, 0x00, 0x00, 0x04,             /* orib #4,%d0 */
    0x10, 0x80,                         /* moveb %d0,%a0@ */

    /* via2[vBufB] &= ~VIA2B_vPower */
    0x20, 0x7C, 0x50, 0xF0, 0x20, 0x00, /* moveal VIA2_BASE+vBufB,%a0 */
    0x10, 0x10,                         /* moveb %a0@,%d0 */
    0x02, 0x00, 0xFF, 0xFB,             /* andib #-5,%d0 */
    0x10, 0x80,                         /* moveb %d0,%a0@ */

    /* while (true) ; */
    0x60, 0xFE                          /* bras [self] */
};

static MemTxResult macio_alias_read(void *opaque, hwaddr addr, uint64_t *data,
                                    unsigned size, MemTxAttrs attrs)
{
    MemTxResult r;
    uint32_t val;

    addr &= IO_SLICE_MASK;
    addr |= IO_BASE;

    switch (size) {
    case 4:
        val = address_space_ldl_be(&address_space_memory, addr, attrs, &r);
        break;
    case 2:
        val = address_space_lduw_be(&address_space_memory, addr, attrs, &r);
        break;
    case 1:
        val = address_space_ldub(&address_space_memory, addr, attrs, &r);
        break;
    default:
        g_assert_not_reached();
    }

    *data = val;
    return r;
}

static MemTxResult macio_alias_write(void *opaque, hwaddr addr, uint64_t value,
                                     unsigned size, MemTxAttrs attrs)
{
    MemTxResult r;

    addr &= IO_SLICE_MASK;
    addr |= IO_BASE;

    switch (size) {
    case 4:
        address_space_stl_be(&address_space_memory, addr, value, attrs, &r);
        break;
    case 2:
        address_space_stw_be(&address_space_memory, addr, value, attrs, &r);
        break;
    case 1:
        address_space_stb(&address_space_memory, addr, value, attrs, &r);
        break;
    default:
        g_assert_not_reached();
    }

    return r;
}

static const MemoryRegionOps macio_alias_ops = {
    .read_with_attrs = macio_alias_read,
    .write_with_attrs = macio_alias_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static uint64_t machine_id_read(void *opaque, hwaddr addr, unsigned size)
{
    return Q800_MACHINE_ID;
}

static void machine_id_write(void *opaque, hwaddr addr, uint64_t val,
                             unsigned size)
{
    return;
}

static const MemoryRegionOps machine_id_ops = {
    .read = machine_id_read,
    .write = machine_id_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t ramio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0x0;
}

static void ramio_write(void *opaque, hwaddr addr, uint64_t val,
                        unsigned size)
{
    return;
}

static const MemoryRegionOps ramio_ops = {
    .read = ramio_read,
    .write = ramio_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void q800_machine_init(MachineState *machine)
{
    Q800MachineState *m = Q800_MACHINE(machine);
    int linux_boot;
    int32_t kernel_size;
    uint64_t elf_entry;
    char *filename;
    int bios_size;
    ram_addr_t initrd_base;
    int32_t initrd_size;
    uint8_t *prom;
    int i, checksum;
    MacFbMode *macfb_mode;
    ram_addr_t ram_size = machine->ram_size;
    const char *kernel_filename = machine->kernel_filename;
    const char *initrd_filename = machine->initrd_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *bios_name = machine->firmware ?: MACROM_FILENAME;
    hwaddr parameters_base;
    CPUState *cs;
    DeviceState *dev;
    SysBusESPState *sysbus_esp;
    ESPState *esp;
    SysBusDevice *sysbus;
    BusState *adb_bus;
    NubusBus *nubus;
    DriveInfo *dinfo;
    NICInfo *nd;
    MACAddr mac;
    uint8_t rng_seed[32];

    linux_boot = (kernel_filename != NULL);

    if (ram_size > 1 * GiB) {
        error_report("Too much memory for this machine: %" PRId64 " MiB, "
                     "maximum 1024 MiB", ram_size / MiB);
        exit(1);
    }

    /* init CPUs */
    object_initialize_child(OBJECT(machine), "cpu", &m->cpu, machine->cpu_type);
    qdev_realize(DEVICE(&m->cpu), NULL, &error_fatal);
    qemu_register_reset(main_cpu_reset, &m->cpu);

    /* RAM */
    memory_region_init_io(&m->ramio, OBJECT(machine), &ramio_ops, &m->ramio,
                          "ram", RAM_SIZE);
    memory_region_add_subregion(get_system_memory(), 0x0, &m->ramio);

    memory_region_add_subregion(&m->ramio, 0, machine->ram);

    /*
     * Create container for all IO devices
     */
    memory_region_init(&m->macio, OBJECT(machine), "mac-io", IO_SLICE);
    memory_region_add_subregion(get_system_memory(), IO_BASE, &m->macio);

    /*
     * Memory from IO_BASE to IO_BASE + IO_SLICE is repeated
     * from IO_BASE + IO_SLICE to IO_BASE + IO_SIZE
     */
    memory_region_init_io(&m->macio_alias, OBJECT(machine), &macio_alias_ops,
                          &m->macio, "mac-io.alias", IO_SIZE - IO_SLICE);
    memory_region_add_subregion(get_system_memory(), IO_BASE + IO_SLICE,
                                &m->macio_alias);

    memory_region_init_io(&m->machine_id, NULL, &machine_id_ops, NULL,
                          "Machine ID", 4);
    memory_region_add_subregion(get_system_memory(), 0x5ffffffc,
                                &m->machine_id);

    /* IRQ Glue */
    object_initialize_child(OBJECT(machine), "glue", &m->glue, TYPE_GLUE);
    object_property_set_link(OBJECT(&m->glue), "cpu", OBJECT(&m->cpu),
                             &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&m->glue), &error_fatal);

    /* djMEMC memory controller */
    object_initialize_child(OBJECT(machine), "djmemc", &m->djmemc,
                            TYPE_DJMEMC);
    sysbus = SYS_BUS_DEVICE(&m->djmemc);
    sysbus_realize_and_unref(sysbus, &error_fatal);
    memory_region_add_subregion(&m->macio, DJMEMC_BASE - IO_BASE,
                                sysbus_mmio_get_region(sysbus, 0));

    /* IOSB subsystem */
    object_initialize_child(OBJECT(machine), "iosb", &m->iosb, TYPE_IOSB);
    sysbus = SYS_BUS_DEVICE(&m->iosb);
    sysbus_realize_and_unref(sysbus, &error_fatal);
    memory_region_add_subregion(&m->macio, IOSB_BASE - IO_BASE,
                                sysbus_mmio_get_region(sysbus, 0));

    /* VIA 1 */
    object_initialize_child(OBJECT(machine), "via1", &m->via1,
                            TYPE_MOS6522_Q800_VIA1);
    dinfo = drive_get(IF_MTD, 0, 0);
    if (dinfo) {
        qdev_prop_set_drive(DEVICE(&m->via1), "drive",
                            blk_by_legacy_dinfo(dinfo));
    }
    sysbus = SYS_BUS_DEVICE(&m->via1);
    sysbus_realize(sysbus, &error_fatal);
    memory_region_add_subregion(&m->macio, VIA_BASE - IO_BASE,
                                sysbus_mmio_get_region(sysbus, 1));
    sysbus_connect_irq(sysbus, 0,
                       qdev_get_gpio_in(DEVICE(&m->glue), GLUE_IRQ_IN_VIA1));
    /* A/UX mode */
    qdev_connect_gpio_out(DEVICE(&m->via1), 0,
                          qdev_get_gpio_in_named(DEVICE(&m->glue),
                                                 "auxmode", 0));

    adb_bus = qdev_get_child_bus(DEVICE(&m->via1), "adb.0");
    dev = qdev_new(TYPE_ADB_KEYBOARD);
    qdev_realize_and_unref(dev, adb_bus, &error_fatal);
    dev = qdev_new(TYPE_ADB_MOUSE);
    qdev_realize_and_unref(dev, adb_bus, &error_fatal);

    /* VIA 2 */
    object_initialize_child(OBJECT(machine), "via2", &m->via2,
                            TYPE_MOS6522_Q800_VIA2);
    sysbus = SYS_BUS_DEVICE(&m->via2);
    sysbus_realize(sysbus, &error_fatal);
    memory_region_add_subregion(&m->macio, VIA_BASE - IO_BASE + VIA_SIZE,
                                sysbus_mmio_get_region(sysbus, 1));
    sysbus_connect_irq(sysbus, 0,
                       qdev_get_gpio_in(DEVICE(&m->glue), GLUE_IRQ_IN_VIA2));

    /* MACSONIC */

    /*
     * MacSonic driver needs an Apple MAC address
     * Valid prefix are:
     * 00:05:02 Apple
     * 00:80:19 Dayna Communications, Inc.
     * 00:A0:40 Apple
     * 08:00:07 Apple
     * (Q800 use the last one)
     */
    object_initialize_child(OBJECT(machine), "dp8393x", &m->dp8393x,
                            TYPE_DP8393X);
    dev = DEVICE(&m->dp8393x);
    nd = qemu_find_nic_info(TYPE_DP8393X, true, "dp83932");
    if (nd) {
        qdev_set_nic_properties(dev, nd);
        memcpy(mac.a, nd->macaddr.a, sizeof(mac.a));
    } else {
        qemu_macaddr_default_if_unset(&mac);
    }
    mac.a[0] = 0x08;
    mac.a[1] = 0x00;
    mac.a[2] = 0x07;
    qdev_prop_set_macaddr(dev, "mac", mac.a);

    qdev_prop_set_uint8(dev, "it_shift", 2);
    qdev_prop_set_bit(dev, "big_endian", true);
    object_property_set_link(OBJECT(dev), "dma_mr",
                             OBJECT(get_system_memory()), &error_abort);
    sysbus = SYS_BUS_DEVICE(dev);
    sysbus_realize(sysbus, &error_fatal);
    memory_region_add_subregion(&m->macio, SONIC_BASE - IO_BASE,
                                sysbus_mmio_get_region(sysbus, 0));
    sysbus_connect_irq(sysbus, 0,
                       qdev_get_gpio_in(DEVICE(&m->glue), GLUE_IRQ_IN_SONIC));

    memory_region_init_rom(&m->dp8393x_prom, NULL, "dp8393x-q800.prom",
                           SONIC_PROM_SIZE, &error_fatal);
    memory_region_add_subregion(get_system_memory(), SONIC_PROM_BASE,
                                &m->dp8393x_prom);

    /* Add MAC address with valid checksum to PROM */
    prom = memory_region_get_ram_ptr(&m->dp8393x_prom);
    checksum = 0;
    for (i = 0; i < 6; i++) {
        prom[i] = revbit8(mac.a[i]);
        checksum ^= prom[i];
    }
    prom[7] = 0xff - checksum;

    /* SCC */

    object_initialize_child(OBJECT(machine), "escc", &m->escc,
                            TYPE_ESCC);
    dev = DEVICE(&m->escc);
    qdev_prop_set_uint32(dev, "disabled", 0);
    qdev_prop_set_uint32(dev, "frequency", MAC_CLOCK);
    qdev_prop_set_uint32(dev, "it_shift", 1);
    qdev_prop_set_bit(dev, "bit_swap", true);
    qdev_prop_set_chr(dev, "chrA", serial_hd(0));
    qdev_prop_set_chr(dev, "chrB", serial_hd(1));
    qdev_prop_set_uint32(dev, "chnBtype", 0);
    qdev_prop_set_uint32(dev, "chnAtype", 0);
    sysbus = SYS_BUS_DEVICE(dev);
    sysbus_realize(sysbus, &error_fatal);

    /* Logically OR both its IRQs together */
    object_initialize_child(OBJECT(machine), "escc_orgate", &m->escc_orgate,
                            TYPE_OR_IRQ);
    object_property_set_int(OBJECT(&m->escc_orgate), "num-lines", 2,
                            &error_fatal);
    dev = DEVICE(&m->escc_orgate);
    qdev_realize(dev, NULL, &error_fatal);
    sysbus_connect_irq(sysbus, 0, qdev_get_gpio_in(dev, 0));
    sysbus_connect_irq(sysbus, 1, qdev_get_gpio_in(dev, 1));
    qdev_connect_gpio_out(dev, 0,
                          qdev_get_gpio_in(DEVICE(&m->glue),
                                           GLUE_IRQ_IN_ESCC));
    memory_region_add_subregion(&m->macio, SCC_BASE - IO_BASE,
                                sysbus_mmio_get_region(sysbus, 0));

    /* Create alias for NetBSD */
    memory_region_init_alias(&m->escc_alias, OBJECT(machine), "escc-alias",
                             sysbus_mmio_get_region(sysbus, 0), 0, 0x8);
    memory_region_add_subregion(&m->macio, SCC_BASE - IO_BASE - 0x20,
                                &m->escc_alias);

    /* SCSI */

    object_initialize_child(OBJECT(machine), "esp", &m->esp,
                            TYPE_SYSBUS_ESP);
    sysbus_esp = SYSBUS_ESP(&m->esp);
    esp = &sysbus_esp->esp;
    esp->dma_memory_read = NULL;
    esp->dma_memory_write = NULL;
    esp->dma_opaque = NULL;
    sysbus_esp->it_shift = 4;
    esp->dma_enabled = 1;

    sysbus = SYS_BUS_DEVICE(&m->esp);
    sysbus_realize(sysbus, &error_fatal);
    /* SCSI and SCSI data IRQs are negative edge triggered */
    sysbus_connect_irq(sysbus, 0,
                       qemu_irq_invert(
                           qdev_get_gpio_in(DEVICE(&m->via2),
                                                   VIA2_IRQ_SCSI_BIT)));
    sysbus_connect_irq(sysbus, 1,
                       qemu_irq_invert(
                           qdev_get_gpio_in(DEVICE(&m->via2),
                                                   VIA2_IRQ_SCSI_DATA_BIT)));
    memory_region_add_subregion(&m->macio, ESP_BASE - IO_BASE,
                                sysbus_mmio_get_region(sysbus, 0));
    memory_region_add_subregion(&m->macio, ESP_PDMA - IO_BASE,
                                sysbus_mmio_get_region(sysbus, 1));

    scsi_bus_legacy_handle_cmdline(&esp->bus);

    /* Apple Sound Chip */

    object_initialize_child(OBJECT(machine), "asc", &m->asc, TYPE_ASC);
    qdev_prop_set_uint8(DEVICE(&m->asc), "asctype", m->easc ? ASC_TYPE_EASC
                                                            : ASC_TYPE_ASC);
    if (machine->audiodev) {
        qdev_prop_set_string(DEVICE(&m->asc), "audiodev", machine->audiodev);
    }
    sysbus = SYS_BUS_DEVICE(&m->asc);
    sysbus_realize_and_unref(sysbus, &error_fatal);
    memory_region_add_subregion(&m->macio, ASC_BASE - IO_BASE,
                                sysbus_mmio_get_region(sysbus, 0));
    sysbus_connect_irq(sysbus, 0, qdev_get_gpio_in(DEVICE(&m->glue),
                                                   GLUE_IRQ_IN_ASC));

    /* Wire ASC IRQ via GLUE for use in classic mode */
    qdev_connect_gpio_out(DEVICE(&m->glue), GLUE_IRQ_ASC,
                          qdev_get_gpio_in(DEVICE(&m->via2),
                                           VIA2_IRQ_ASC_BIT));

    /* SWIM floppy controller */

    object_initialize_child(OBJECT(machine), "swim", &m->swim,
                            TYPE_SWIM);
    sysbus = SYS_BUS_DEVICE(&m->swim);
    sysbus_realize(sysbus, &error_fatal);
    memory_region_add_subregion(&m->macio, SWIM_BASE - IO_BASE,
                                sysbus_mmio_get_region(sysbus, 0));

    /* NuBus */

    object_initialize_child(OBJECT(machine), "mac-nubus-bridge",
                            &m->mac_nubus_bridge,
                            TYPE_MAC_NUBUS_BRIDGE);
    sysbus = SYS_BUS_DEVICE(&m->mac_nubus_bridge);
    dev = DEVICE(&m->mac_nubus_bridge);
    qdev_prop_set_uint32(DEVICE(&m->mac_nubus_bridge), "slot-available-mask",
                         Q800_NUBUS_SLOTS_AVAILABLE);
    sysbus_realize(sysbus, &error_fatal);
    memory_region_add_subregion(get_system_memory(),
                                MAC_NUBUS_FIRST_SLOT * NUBUS_SUPER_SLOT_SIZE,
                                sysbus_mmio_get_region(sysbus, 0));
    memory_region_add_subregion(get_system_memory(),
                                NUBUS_SLOT_BASE +
                                MAC_NUBUS_FIRST_SLOT * NUBUS_SLOT_SIZE,
                                sysbus_mmio_get_region(sysbus, 1));
    qdev_connect_gpio_out(dev, 9,
                          qdev_get_gpio_in_named(DEVICE(&m->via2), "nubus-irq",
                          VIA2_NUBUS_IRQ_INTVIDEO));
    for (i = 1; i < VIA2_NUBUS_IRQ_NB; i++) {
        qdev_connect_gpio_out(dev, 9 + i,
                              qdev_get_gpio_in_named(DEVICE(&m->via2),
                                                     "nubus-irq",
                                                     VIA2_NUBUS_IRQ_9 + i));
    }

    /*
     * Since the framebuffer in slot 0x9 uses a separate IRQ, wire the unused
     * IRQ via GLUE for use by SONIC Ethernet in classic mode
     */
    qdev_connect_gpio_out(DEVICE(&m->glue), GLUE_IRQ_NUBUS_9,
                          qdev_get_gpio_in_named(DEVICE(&m->via2), "nubus-irq",
                                                 VIA2_NUBUS_IRQ_9));

    nubus = NUBUS_BUS(qdev_get_child_bus(dev, "nubus-bus.0"));

    /* framebuffer in nubus slot #9 */

    object_initialize_child(OBJECT(machine), "macfb", &m->macfb,
                            TYPE_NUBUS_MACFB);
    dev = DEVICE(&m->macfb);
    qdev_prop_set_uint32(dev, "slot", 9);
    qdev_prop_set_uint32(dev, "width", graphic_width);
    qdev_prop_set_uint32(dev, "height", graphic_height);
    qdev_prop_set_uint8(dev, "depth", graphic_depth);
    if (graphic_width == 1152 && graphic_height == 870) {
        qdev_prop_set_uint8(dev, "display", MACFB_DISPLAY_APPLE_21_COLOR);
    } else {
        qdev_prop_set_uint8(dev, "display", MACFB_DISPLAY_VGA);
    }
    qdev_realize(dev, BUS(nubus), &error_fatal);

    macfb_mode = (NUBUS_MACFB(dev)->macfb).mode;

    cs = CPU(&m->cpu);
    if (linux_boot) {
        uint64_t high;
        void *param_blob, *param_ptr, *param_rng_seed;

        if (kernel_cmdline) {
            param_blob = g_malloc(strlen(kernel_cmdline) + 1024);
        } else {
            param_blob = g_malloc(1024);
        }

        kernel_size = load_elf(kernel_filename, NULL, NULL, NULL,
                               &elf_entry, NULL, &high, NULL, 1,
                               EM_68K, 0, 0);
        if (kernel_size < 0) {
            error_report("could not load kernel '%s'", kernel_filename);
            exit(1);
        }
        stl_phys(cs->as, 4, elf_entry); /* reset initial PC */
        parameters_base = (high + 1) & ~1;
        param_ptr = param_blob;

        BOOTINFO1(param_ptr, BI_MACHTYPE, MACH_MAC);
        BOOTINFO1(param_ptr, BI_FPUTYPE, FPU_68040);
        BOOTINFO1(param_ptr, BI_MMUTYPE, MMU_68040);
        BOOTINFO1(param_ptr, BI_CPUTYPE, CPU_68040);
        BOOTINFO1(param_ptr, BI_MAC_CPUID, CPUB_68040);
        BOOTINFO1(param_ptr, BI_MAC_MODEL, MAC_MODEL_Q800);
        BOOTINFO1(param_ptr,
                  BI_MAC_MEMSIZE, ram_size >> 20); /* in MB */
        BOOTINFO2(param_ptr, BI_MEMCHUNK, 0, ram_size);
        BOOTINFO1(param_ptr, BI_MAC_VADDR,
                  VIDEO_BASE + macfb_mode->offset);
        BOOTINFO1(param_ptr, BI_MAC_VDEPTH, graphic_depth);
        BOOTINFO1(param_ptr, BI_MAC_VDIM,
                  (graphic_height << 16) | graphic_width);
        BOOTINFO1(param_ptr, BI_MAC_VROW, macfb_mode->stride);
        BOOTINFO1(param_ptr, BI_MAC_SCCBASE, SCC_BASE);

        memory_region_init_ram_ptr(&m->rom, NULL, "m68k_fake_mac.rom",
                                   sizeof(fake_mac_rom), fake_mac_rom);
        memory_region_set_readonly(&m->rom, true);
        memory_region_add_subregion(get_system_memory(), MACROM_ADDR, &m->rom);

        if (kernel_cmdline) {
            BOOTINFOSTR(param_ptr, BI_COMMAND_LINE,
                        kernel_cmdline);
        }

        /* Pass seed to RNG. */
        param_rng_seed = param_ptr;
        qemu_guest_getrandom_nofail(rng_seed, sizeof(rng_seed));
        BOOTINFODATA(param_ptr, BI_RNG_SEED,
                     rng_seed, sizeof(rng_seed));

        /* load initrd */
        if (initrd_filename) {
            initrd_size = get_image_size(initrd_filename);
            if (initrd_size < 0) {
                error_report("could not load initial ram disk '%s'",
                             initrd_filename);
                exit(1);
            }

            initrd_base = (ram_size - initrd_size) & TARGET_PAGE_MASK;
            load_image_targphys(initrd_filename, initrd_base,
                                ram_size - initrd_base);
            BOOTINFO2(param_ptr, BI_RAMDISK, initrd_base,
                      initrd_size);
        } else {
            initrd_base = 0;
            initrd_size = 0;
        }
        BOOTINFO0(param_ptr, BI_LAST);
        rom_add_blob_fixed_as("bootinfo", param_blob, param_ptr - param_blob,
                              parameters_base, cs->as);
        qemu_register_reset_nosnapshotload(rerandomize_rng_seed,
                            rom_ptr_for_as(cs->as, parameters_base,
                                           param_ptr - param_blob) +
                            (param_rng_seed - param_blob));
        g_free(param_blob);
    } else {
        uint8_t *ptr;
        /* allocate and load BIOS */
        memory_region_init_rom(&m->rom, NULL, "m68k_mac.rom", MACROM_SIZE,
                               &error_abort);
        filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
        memory_region_add_subregion(get_system_memory(), MACROM_ADDR, &m->rom);

        memory_region_init_alias(&m->rom_alias, NULL, "m68k_mac.rom-alias",
                                 &m->rom, 0, MACROM_SIZE);
        memory_region_add_subregion(get_system_memory(), 0x40000000,
                                    &m->rom_alias);

        /* Load MacROM binary */
        if (filename) {
            bios_size = load_image_targphys(filename, MACROM_ADDR, MACROM_SIZE);
            g_free(filename);
        } else {
            bios_size = -1;
        }

        /* Remove qtest_enabled() check once firmware files are in the tree */
        if (!qtest_enabled()) {
            if (bios_size <= 0 || bios_size > MACROM_SIZE) {
                error_report("could not load MacROM '%s'", bios_name);
                exit(1);
            }

            ptr = rom_ptr(MACROM_ADDR, bios_size);
            assert(ptr != NULL);
            stl_phys(cs->as, 0, ldl_p(ptr));    /* reset initial SP */
            stl_phys(cs->as, 4,
                     MACROM_ADDR + ldl_p(ptr + 4)); /* reset initial PC */
        }
    }
}

static bool q800_get_easc(Object *obj, Error **errp)
{
    Q800MachineState *ms = Q800_MACHINE(obj);

    return ms->easc;
}

static void q800_set_easc(Object *obj, bool value, Error **errp)
{
    Q800MachineState *ms = Q800_MACHINE(obj);

    ms->easc = value;
}

static void q800_init(Object *obj)
{
    Q800MachineState *ms = Q800_MACHINE(obj);

    /* Default to EASC */
    ms->easc = true;
}

static GlobalProperty hw_compat_q800[] = {
    { "scsi-hd", "quirk_mode_page_vendor_specific_apple", "on" },
    { "scsi-hd", "vendor", " SEAGATE" },
    { "scsi-hd", "product", "          ST225N" },
    { "scsi-hd", "ver", "1.0 " },
    { "scsi-cd", "quirk_mode_page_apple_vendor", "on" },
    { "scsi-cd", "quirk_mode_sense_rom_use_dbd", "on" },
    { "scsi-cd", "quirk_mode_page_vendor_specific_apple", "on" },
    { "scsi-cd", "quirk_mode_page_truncated", "on" },
    { "scsi-cd", "vendor", "MATSHITA" },
    { "scsi-cd", "product", "CD-ROM CR-8005" },
    { "scsi-cd", "ver", "1.0k" },
};
static const size_t hw_compat_q800_len = G_N_ELEMENTS(hw_compat_q800);

static void q800_machine_class_init(ObjectClass *oc, void *data)
{
    static const char * const valid_cpu_types[] = {
        M68K_CPU_TYPE_NAME("m68040"),
        NULL
    };
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Macintosh Quadra 800";
    mc->init = q800_machine_init;
    mc->default_cpu_type = M68K_CPU_TYPE_NAME("m68040");
    mc->valid_cpu_types = valid_cpu_types;
    mc->max_cpus = 1;
    mc->block_default_type = IF_SCSI;
    mc->default_ram_id = "m68k_mac.ram";
    machine_add_audiodev_property(mc);
    compat_props_add(mc->compat_props, hw_compat_q800, hw_compat_q800_len);

    object_class_property_add_bool(oc, "easc", q800_get_easc, q800_set_easc);
    object_class_property_set_description(oc, "easc",
        "Set to off to use ASC rather than EASC");
}

static const TypeInfo q800_machine_typeinfo = {
    .name       = MACHINE_TYPE_NAME("q800"),
    .parent     = TYPE_MACHINE,
    .instance_init = q800_init,
    .instance_size = sizeof(Q800MachineState),
    .class_init = q800_machine_class_init,
};

static void q800_machine_register_types(void)
{
    type_register_static(&q800_machine_typeinfo);
}

type_init(q800_machine_register_types)
