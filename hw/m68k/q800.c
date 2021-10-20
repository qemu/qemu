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
#include "qemu-common.h"
#include "qemu/datadir.h"
#include "sysemu/sysemu.h"
#include "cpu.h"
#include "hw/boards.h"
#include "hw/or-irq.h"
#include "hw/nmi.h"
#include "elf.h"
#include "hw/loader.h"
#include "ui/console.h"
#include "hw/char/escc.h"
#include "hw/sysbus.h"
#include "hw/scsi/esp.h"
#include "standard-headers/asm-m68k/bootinfo.h"
#include "standard-headers/asm-m68k/bootinfo-mac.h"
#include "bootinfo.h"
#include "hw/misc/mac_via.h"
#include "hw/input/adb.h"
#include "hw/nubus/mac-nubus-bridge.h"
#include "hw/display/macfb.h"
#include "hw/block/swim.h"
#include "net/net.h"
#include "qapi/error.h"
#include "sysemu/qtest.h"
#include "sysemu/runstate.h"
#include "sysemu/reset.h"
#include "migration/vmstate.h"

#define MACROM_ADDR     0x40800000
#define MACROM_SIZE     0x00100000

#define MACROM_FILENAME "MacROM.bin"

#define IO_BASE               0x50000000
#define IO_SLICE              0x00040000
#define IO_SIZE               0x04000000

#define VIA_BASE              (IO_BASE + 0x00000)
#define SONIC_PROM_BASE       (IO_BASE + 0x08000)
#define SONIC_BASE            (IO_BASE + 0x0a000)
#define SCC_BASE              (IO_BASE + 0x0c020)
#define ESP_BASE              (IO_BASE + 0x10000)
#define ESP_PDMA              (IO_BASE + 0x10100)
#define ASC_BASE              (IO_BASE + 0x14000)
#define SWIM_BASE             (IO_BASE + 0x1E000)

#define SONIC_PROM_SIZE       0x1000

/*
 * the video base, whereas it a Nubus address,
 * is needed by the kernel to have early display and
 * thus provided by the bootloader
 */
#define VIDEO_BASE            0xf9000000

#define MAC_CLOCK  3686418

/*
 * Slot 0x9 is reserved for use by the in-built framebuffer whilst only
 * slots 0xc, 0xd and 0xe physically exist on the Quadra 800
 */
#define Q800_NUBUS_SLOTS_AVAILABLE    (BIT(0x9) | BIT(0xc) | BIT(0xd) | \
                                       BIT(0xe))

/*
 * The GLUE (General Logic Unit) is an Apple custom integrated circuit chip
 * that performs a variety of functions (RAM management, clock generation, ...).
 * The GLUE chip receives interrupt requests from various devices,
 * assign priority to each, and asserts one or more interrupt line to the
 * CPU.
 */

#define TYPE_GLUE "q800-glue"
OBJECT_DECLARE_SIMPLE_TYPE(GLUEState, GLUE)

struct GLUEState {
    SysBusDevice parent_obj;
    M68kCPU *cpu;
    uint8_t ipr;
    uint8_t auxmode;
    qemu_irq irqs[1];
    QEMUTimer *nmi_release;
};

#define GLUE_IRQ_IN_VIA1       0
#define GLUE_IRQ_IN_VIA2       1
#define GLUE_IRQ_IN_SONIC      2
#define GLUE_IRQ_IN_ESCC       3
#define GLUE_IRQ_IN_NMI        4

#define GLUE_IRQ_NUBUS_9       0

/*
 * The GLUE logic on the Quadra 800 supports 2 different IRQ routing modes
 * controlled from the VIA1 auxmode GPIO (port B bit 6) which are documented
 * in NetBSD as follows:
 *
 * A/UX mode (Linux, NetBSD, auxmode GPIO low)
 *
 *   Level 0:        Spurious: ignored
 *   Level 1:        Software
 *   Level 2:        VIA2 (except ethernet, sound)
 *   Level 3:        Ethernet
 *   Level 4:        Serial (SCC)
 *   Level 5:        Sound
 *   Level 6:        VIA1
 *   Level 7:        NMIs: parity errors, RESET button, YANCC error
 *
 * Classic mode (default: used by MacOS, A/UX 3.0.1, auxmode GPIO high)
 *
 *   Level 0:        Spurious: ignored
 *   Level 1:        VIA1 (clock, ADB)
 *   Level 2:        VIA2 (NuBus, SCSI)
 *   Level 3:
 *   Level 4:        Serial (SCC)
 *   Level 5:
 *   Level 6:
 *   Level 7:        Non-maskable: parity errors, RESET button
 *
 * Note that despite references to A/UX mode in Linux and NetBSD, at least
 * A/UX 3.0.1 still uses Classic mode.
 */

static void GLUE_set_irq(void *opaque, int irq, int level)
{
    GLUEState *s = opaque;
    int i;

    if (s->auxmode) {
        /* Classic mode */
        switch (irq) {
        case GLUE_IRQ_IN_VIA1:
            irq = 0;
            break;

        case GLUE_IRQ_IN_VIA2:
            irq = 1;
            break;

        case GLUE_IRQ_IN_SONIC:
            /* Route to VIA2 instead */
            qemu_set_irq(s->irqs[GLUE_IRQ_NUBUS_9], level);
            return;

        case GLUE_IRQ_IN_ESCC:
            irq = 3;
            break;

        case GLUE_IRQ_IN_NMI:
            irq = 6;
            break;

        default:
            g_assert_not_reached();
        }
    } else {
        /* A/UX mode */
        switch (irq) {
        case GLUE_IRQ_IN_VIA1:
            irq = 5;
            break;

        case GLUE_IRQ_IN_VIA2:
            irq = 1;
            break;

        case GLUE_IRQ_IN_SONIC:
            irq = 2;
            break;

        case GLUE_IRQ_IN_ESCC:
            irq = 3;
            break;

        case GLUE_IRQ_IN_NMI:
            irq = 6;
            break;

        default:
            g_assert_not_reached();
        }
    }

    if (level) {
        s->ipr |= 1 << irq;
    } else {
        s->ipr &= ~(1 << irq);
    }

    for (i = 7; i >= 0; i--) {
        if ((s->ipr >> i) & 1) {
            m68k_set_irq_level(s->cpu, i + 1, i + 25);
            return;
        }
    }
    m68k_set_irq_level(s->cpu, 0, 0);
}

static void glue_auxmode_set_irq(void *opaque, int irq, int level)
{
    GLUEState *s = GLUE(opaque);

    s->auxmode = level;
}

static void glue_nmi(NMIState *n, int cpu_index, Error **errp)
{
    GLUEState *s = GLUE(n);

    /* Hold NMI active for 100ms */
    GLUE_set_irq(s, GLUE_IRQ_IN_NMI, 1);
    timer_mod(s->nmi_release, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);
}

static void glue_nmi_release(void *opaque)
{
    GLUEState *s = GLUE(opaque);

    GLUE_set_irq(s, GLUE_IRQ_IN_NMI, 0);
}

static void glue_reset(DeviceState *dev)
{
    GLUEState *s = GLUE(dev);

    s->ipr = 0;
    s->auxmode = 0;

    timer_del(s->nmi_release);
}

static const VMStateDescription vmstate_glue = {
    .name = "q800-glue",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(ipr, GLUEState),
        VMSTATE_UINT8(auxmode, GLUEState),
        VMSTATE_TIMER_PTR(nmi_release, GLUEState),
        VMSTATE_END_OF_LIST(),
    },
};

/*
 * If the m68k CPU implemented its inbound irq lines as GPIO lines
 * rather than via the m68k_set_irq_level() function we would not need
 * this cpu link property and could instead provide outbound IRQ lines
 * that the board could wire up to the CPU.
 */
static Property glue_properties[] = {
    DEFINE_PROP_LINK("cpu", GLUEState, cpu, TYPE_M68K_CPU, M68kCPU *),
    DEFINE_PROP_END_OF_LIST(),
};

static void glue_finalize(Object *obj)
{
    GLUEState *s = GLUE(obj);

    timer_free(s->nmi_release);
}

static void glue_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    GLUEState *s = GLUE(dev);

    qdev_init_gpio_in(dev, GLUE_set_irq, 8);
    qdev_init_gpio_in_named(dev, glue_auxmode_set_irq, "auxmode", 1);

    qdev_init_gpio_out(dev, s->irqs, 1);

    /* NMI release timer */
    s->nmi_release = timer_new_ms(QEMU_CLOCK_VIRTUAL, glue_nmi_release, s);
}

static void glue_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    NMIClass *nc = NMI_CLASS(klass);

    dc->vmsd = &vmstate_glue;
    dc->reset = glue_reset;
    device_class_set_props(dc, glue_properties);
    nc->nmi_monitor_handler = glue_nmi;
}

static const TypeInfo glue_info = {
    .name = TYPE_GLUE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GLUEState),
    .instance_init = glue_init,
    .instance_finalize = glue_finalize,
    .class_init = glue_class_init,
    .interfaces = (InterfaceInfo[]) {
         { TYPE_NMI },
         { }
    },
};

static void main_cpu_reset(void *opaque)
{
    M68kCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);

    cpu_reset(cs);
    cpu->env.aregs[7] = ldl_phys(cs->as, 0);
    cpu->env.pc = ldl_phys(cs->as, 4);
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

static void q800_init(MachineState *machine)
{
    M68kCPU *cpu = NULL;
    int linux_boot;
    int32_t kernel_size;
    uint64_t elf_entry;
    char *filename;
    int bios_size;
    ram_addr_t initrd_base;
    int32_t initrd_size;
    MemoryRegion *rom;
    MemoryRegion *io;
    MemoryRegion *dp8393x_prom = g_new(MemoryRegion, 1);
    uint8_t *prom;
    const int io_slice_nb = (IO_SIZE / IO_SLICE) - 1;
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
    DeviceState *via1_dev, *via2_dev;
    DeviceState *escc_orgate;
    SysBusESPState *sysbus_esp;
    ESPState *esp;
    SysBusDevice *sysbus;
    BusState *adb_bus;
    NubusBus *nubus;
    DeviceState *glue;
    DriveInfo *dinfo;

    linux_boot = (kernel_filename != NULL);

    if (ram_size > 1 * GiB) {
        error_report("Too much memory for this machine: %" PRId64 " MiB, "
                     "maximum 1024 MiB", ram_size / MiB);
        exit(1);
    }

    /* init CPUs */
    cpu = M68K_CPU(cpu_create(machine->cpu_type));
    qemu_register_reset(main_cpu_reset, cpu);

    /* RAM */
    memory_region_add_subregion(get_system_memory(), 0, machine->ram);

    /*
     * Memory from IO_BASE to IO_BASE + IO_SLICE is repeated
     * from IO_BASE + IO_SLICE to IO_BASE + IO_SIZE
     */
    io = g_new(MemoryRegion, io_slice_nb);
    for (i = 0; i < io_slice_nb; i++) {
        char *name = g_strdup_printf("mac_m68k.io[%d]", i + 1);

        memory_region_init_alias(&io[i], NULL, name, get_system_memory(),
                                 IO_BASE, IO_SLICE);
        memory_region_add_subregion(get_system_memory(),
                                    IO_BASE + (i + 1) * IO_SLICE, &io[i]);
        g_free(name);
    }

    /* IRQ Glue */
    glue = qdev_new(TYPE_GLUE);
    object_property_set_link(OBJECT(glue), "cpu", OBJECT(cpu), &error_abort);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(glue), &error_fatal);

    /* VIA 1 */
    via1_dev = qdev_new(TYPE_MOS6522_Q800_VIA1);
    dinfo = drive_get(IF_MTD, 0, 0);
    if (dinfo) {
        qdev_prop_set_drive(via1_dev, "drive", blk_by_legacy_dinfo(dinfo));
    }
    sysbus = SYS_BUS_DEVICE(via1_dev);
    sysbus_realize_and_unref(sysbus, &error_fatal);
    sysbus_mmio_map(sysbus, 1, VIA_BASE);
    sysbus_connect_irq(sysbus, 0, qdev_get_gpio_in(glue, GLUE_IRQ_IN_VIA1));
    /* A/UX mode */
    qdev_connect_gpio_out(via1_dev, 0,
                          qdev_get_gpio_in_named(glue, "auxmode", 0));

    adb_bus = qdev_get_child_bus(via1_dev, "adb.0");
    dev = qdev_new(TYPE_ADB_KEYBOARD);
    qdev_realize_and_unref(dev, adb_bus, &error_fatal);
    dev = qdev_new(TYPE_ADB_MOUSE);
    qdev_realize_and_unref(dev, adb_bus, &error_fatal);

    /* VIA 2 */
    via2_dev = qdev_new(TYPE_MOS6522_Q800_VIA2);
    sysbus = SYS_BUS_DEVICE(via2_dev);
    sysbus_realize_and_unref(sysbus, &error_fatal);
    sysbus_mmio_map(sysbus, 1, VIA_BASE + VIA_SIZE);
    sysbus_connect_irq(sysbus, 0, qdev_get_gpio_in(glue, GLUE_IRQ_IN_VIA2));

    /* MACSONIC */

    if (nb_nics > 1) {
        error_report("q800 can only have one ethernet interface");
        exit(1);
    }

    qemu_check_nic_model(&nd_table[0], "dp83932");

    /*
     * MacSonic driver needs an Apple MAC address
     * Valid prefix are:
     * 00:05:02 Apple
     * 00:80:19 Dayna Communications, Inc.
     * 00:A0:40 Apple
     * 08:00:07 Apple
     * (Q800 use the last one)
     */
    nd_table[0].macaddr.a[0] = 0x08;
    nd_table[0].macaddr.a[1] = 0x00;
    nd_table[0].macaddr.a[2] = 0x07;

    dev = qdev_new("dp8393x");
    qdev_set_nic_properties(dev, &nd_table[0]);
    qdev_prop_set_uint8(dev, "it_shift", 2);
    qdev_prop_set_bit(dev, "big_endian", true);
    object_property_set_link(OBJECT(dev), "dma_mr",
                             OBJECT(get_system_memory()), &error_abort);
    sysbus = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(sysbus, &error_fatal);
    sysbus_mmio_map(sysbus, 0, SONIC_BASE);
    sysbus_connect_irq(sysbus, 0, qdev_get_gpio_in(glue, GLUE_IRQ_IN_SONIC));

    memory_region_init_rom(dp8393x_prom, NULL, "dp8393x-q800.prom",
                           SONIC_PROM_SIZE, &error_fatal);
    memory_region_add_subregion(get_system_memory(), SONIC_PROM_BASE,
                                dp8393x_prom);

    /* Add MAC address with valid checksum to PROM */
    prom = memory_region_get_ram_ptr(dp8393x_prom);
    checksum = 0;
    for (i = 0; i < 6; i++) {
        prom[i] = revbit8(nd_table[0].macaddr.a[i]);
        checksum ^= prom[i];
    }
    prom[7] = 0xff - checksum;

    /* SCC */

    dev = qdev_new(TYPE_ESCC);
    qdev_prop_set_uint32(dev, "disabled", 0);
    qdev_prop_set_uint32(dev, "frequency", MAC_CLOCK);
    qdev_prop_set_uint32(dev, "it_shift", 1);
    qdev_prop_set_bit(dev, "bit_swap", true);
    qdev_prop_set_chr(dev, "chrA", serial_hd(0));
    qdev_prop_set_chr(dev, "chrB", serial_hd(1));
    qdev_prop_set_uint32(dev, "chnBtype", 0);
    qdev_prop_set_uint32(dev, "chnAtype", 0);
    sysbus = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(sysbus, &error_fatal);

    /* Logically OR both its IRQs together */
    escc_orgate = DEVICE(object_new(TYPE_OR_IRQ));
    object_property_set_int(OBJECT(escc_orgate), "num-lines", 2, &error_fatal);
    qdev_realize_and_unref(escc_orgate, NULL, &error_fatal);
    sysbus_connect_irq(sysbus, 0, qdev_get_gpio_in(escc_orgate, 0));
    sysbus_connect_irq(sysbus, 1, qdev_get_gpio_in(escc_orgate, 1));
    qdev_connect_gpio_out(DEVICE(escc_orgate), 0,
                          qdev_get_gpio_in(glue, GLUE_IRQ_IN_ESCC));
    sysbus_mmio_map(sysbus, 0, SCC_BASE);

    /* SCSI */

    dev = qdev_new(TYPE_SYSBUS_ESP);
    sysbus_esp = SYSBUS_ESP(dev);
    esp = &sysbus_esp->esp;
    esp->dma_memory_read = NULL;
    esp->dma_memory_write = NULL;
    esp->dma_opaque = NULL;
    sysbus_esp->it_shift = 4;
    esp->dma_enabled = 1;

    sysbus = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(sysbus, &error_fatal);
    sysbus_connect_irq(sysbus, 0, qdev_get_gpio_in(via2_dev,
                                                   VIA2_IRQ_SCSI_BIT));
    sysbus_connect_irq(sysbus, 1, qdev_get_gpio_in(via2_dev,
                                                   VIA2_IRQ_SCSI_DATA_BIT));
    sysbus_mmio_map(sysbus, 0, ESP_BASE);
    sysbus_mmio_map(sysbus, 1, ESP_PDMA);

    scsi_bus_legacy_handle_cmdline(&esp->bus);

    /* SWIM floppy controller */

    dev = qdev_new(TYPE_SWIM);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, SWIM_BASE);

    /* NuBus */

    dev = qdev_new(TYPE_MAC_NUBUS_BRIDGE);
    qdev_prop_set_uint32(dev, "slot-available-mask",
                         Q800_NUBUS_SLOTS_AVAILABLE);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0,
                    MAC_NUBUS_FIRST_SLOT * NUBUS_SUPER_SLOT_SIZE);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, NUBUS_SLOT_BASE +
                    MAC_NUBUS_FIRST_SLOT * NUBUS_SLOT_SIZE);
    qdev_connect_gpio_out(dev, 9,
                          qdev_get_gpio_in_named(via2_dev, "nubus-irq",
                          VIA2_NUBUS_IRQ_INTVIDEO));
    for (i = 1; i < VIA2_NUBUS_IRQ_NB; i++) {
        qdev_connect_gpio_out(dev, 9 + i,
                              qdev_get_gpio_in_named(via2_dev, "nubus-irq",
                                                     VIA2_NUBUS_IRQ_9 + i));
    }

    /*
     * Since the framebuffer in slot 0x9 uses a separate IRQ, wire the unused
     * IRQ via GLUE for use by SONIC Ethernet in classic mode
     */
    qdev_connect_gpio_out(glue, GLUE_IRQ_NUBUS_9,
                          qdev_get_gpio_in_named(via2_dev, "nubus-irq",
                                                 VIA2_NUBUS_IRQ_9));

    nubus = &NUBUS_BRIDGE(dev)->bus;

    /* framebuffer in nubus slot #9 */

    dev = qdev_new(TYPE_NUBUS_MACFB);
    qdev_prop_set_uint32(dev, "slot", 9);
    qdev_prop_set_uint32(dev, "width", graphic_width);
    qdev_prop_set_uint32(dev, "height", graphic_height);
    qdev_prop_set_uint8(dev, "depth", graphic_depth);
    if (graphic_width == 1152 && graphic_height == 870) {
        qdev_prop_set_uint8(dev, "display", MACFB_DISPLAY_APPLE_21_COLOR);
    } else {
        qdev_prop_set_uint8(dev, "display", MACFB_DISPLAY_VGA);
    }
    qdev_realize_and_unref(dev, BUS(nubus), &error_fatal);

    macfb_mode = (NUBUS_MACFB(dev)->macfb).mode;

    cs = CPU(cpu);
    if (linux_boot) {
        uint64_t high;
        kernel_size = load_elf(kernel_filename, NULL, NULL, NULL,
                               &elf_entry, NULL, &high, NULL, 1,
                               EM_68K, 0, 0);
        if (kernel_size < 0) {
            error_report("could not load kernel '%s'", kernel_filename);
            exit(1);
        }
        stl_phys(cs->as, 4, elf_entry); /* reset initial PC */
        parameters_base = (high + 1) & ~1;

        BOOTINFO1(cs->as, parameters_base, BI_MACHTYPE, MACH_MAC);
        BOOTINFO1(cs->as, parameters_base, BI_FPUTYPE, FPU_68040);
        BOOTINFO1(cs->as, parameters_base, BI_MMUTYPE, MMU_68040);
        BOOTINFO1(cs->as, parameters_base, BI_CPUTYPE, CPU_68040);
        BOOTINFO1(cs->as, parameters_base, BI_MAC_CPUID, CPUB_68040);
        BOOTINFO1(cs->as, parameters_base, BI_MAC_MODEL, MAC_MODEL_Q800);
        BOOTINFO1(cs->as, parameters_base,
                  BI_MAC_MEMSIZE, ram_size >> 20); /* in MB */
        BOOTINFO2(cs->as, parameters_base, BI_MEMCHUNK, 0, ram_size);
        BOOTINFO1(cs->as, parameters_base, BI_MAC_VADDR,
                  VIDEO_BASE + macfb_mode->offset);
        BOOTINFO1(cs->as, parameters_base, BI_MAC_VDEPTH, graphic_depth);
        BOOTINFO1(cs->as, parameters_base, BI_MAC_VDIM,
                  (graphic_height << 16) | graphic_width);
        BOOTINFO1(cs->as, parameters_base, BI_MAC_VROW, macfb_mode->stride);
        BOOTINFO1(cs->as, parameters_base, BI_MAC_SCCBASE, SCC_BASE);

        rom = g_malloc(sizeof(*rom));
        memory_region_init_ram_ptr(rom, NULL, "m68k_fake_mac.rom",
                                   sizeof(fake_mac_rom), fake_mac_rom);
        memory_region_set_readonly(rom, true);
        memory_region_add_subregion(get_system_memory(), MACROM_ADDR, rom);

        if (kernel_cmdline) {
            BOOTINFOSTR(cs->as, parameters_base, BI_COMMAND_LINE,
                        kernel_cmdline);
        }

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
            BOOTINFO2(cs->as, parameters_base, BI_RAMDISK, initrd_base,
                      initrd_size);
        } else {
            initrd_base = 0;
            initrd_size = 0;
        }
        BOOTINFO0(cs->as, parameters_base, BI_LAST);
    } else {
        uint8_t *ptr;
        /* allocate and load BIOS */
        rom = g_malloc(sizeof(*rom));
        memory_region_init_rom(rom, NULL, "m68k_mac.rom", MACROM_SIZE,
                               &error_abort);
        filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
        memory_region_add_subregion(get_system_memory(), MACROM_ADDR, rom);

        /* Load MacROM binary */
        if (filename) {
            bios_size = load_image_targphys(filename, MACROM_ADDR, MACROM_SIZE);
            g_free(filename);
        } else {
            bios_size = -1;
        }

        /* Remove qtest_enabled() check once firmware files are in the tree */
        if (!qtest_enabled()) {
            if (bios_size < 0 || bios_size > MACROM_SIZE) {
                error_report("could not load MacROM '%s'", bios_name);
                exit(1);
            }

            ptr = rom_ptr(MACROM_ADDR, MACROM_SIZE);
            stl_phys(cs->as, 0, ldl_p(ptr));    /* reset initial SP */
            stl_phys(cs->as, 4,
                     MACROM_ADDR + ldl_p(ptr + 4)); /* reset initial PC */
        }
    }
}

static void q800_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    mc->desc = "Macintosh Quadra 800";
    mc->init = q800_init;
    mc->default_cpu_type = M68K_CPU_TYPE_NAME("m68040");
    mc->max_cpus = 1;
    mc->block_default_type = IF_SCSI;
    mc->default_ram_id = "m68k_mac.ram";
}

static const TypeInfo q800_machine_typeinfo = {
    .name       = MACHINE_TYPE_NAME("q800"),
    .parent     = TYPE_MACHINE,
    .class_init = q800_machine_class_init,
};

static void q800_machine_register_types(void)
{
    type_register_static(&q800_machine_typeinfo);
    type_register_static(&glue_info);
}

type_init(q800_machine_register_types)
