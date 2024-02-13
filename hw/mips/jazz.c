/*
 * QEMU MIPS Jazz support
 *
 * Copyright (c) 2007-2008 Hervé Poussineau
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
#include "qemu/datadir.h"
#include "hw/clock.h"
#include "hw/mips/mips.h"
#include "hw/intc/i8259.h"
#include "hw/dma/i8257.h"
#include "hw/char/serial.h"
#include "hw/char/parallel.h"
#include "hw/isa/isa.h"
#include "hw/block/fdc.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "net/net.h"
#include "hw/scsi/esp.h"
#include "hw/loader.h"
#include "hw/rtc/mc146818rtc.h"
#include "hw/timer/i8254.h"
#include "hw/display/vga.h"
#include "hw/display/bochs-vbe.h"
#include "hw/audio/pcspk.h"
#include "hw/input/i8042.h"
#include "hw/sysbus.h"
#include "sysemu/qtest.h"
#include "sysemu/reset.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/help_option.h"
#ifdef CONFIG_TCG
#include "hw/core/tcg-cpu-ops.h"
#endif /* CONFIG_TCG */
#include "cpu.h"

enum jazz_model_e {
    JAZZ_MAGNUM,
    JAZZ_PICA61,
};

#if TARGET_BIG_ENDIAN
#define BIOS_FILENAME "mips_bios.bin"
#else
#define BIOS_FILENAME "mipsel_bios.bin"
#endif

static void main_cpu_reset(void *opaque)
{
    MIPSCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
}

static uint64_t rtc_read(void *opaque, hwaddr addr, unsigned size)
{
    uint8_t val;
    address_space_read(&address_space_memory, 0x90000071,
                       MEMTXATTRS_UNSPECIFIED, &val, 1);
    return val;
}

static void rtc_write(void *opaque, hwaddr addr,
                      uint64_t val, unsigned size)
{
    uint8_t buf = val & 0xff;
    address_space_write(&address_space_memory, 0x90000071,
                        MEMTXATTRS_UNSPECIFIED, &buf, 1);
}

static const MemoryRegionOps rtc_ops = {
    .read = rtc_read,
    .write = rtc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static uint64_t dma_dummy_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    /*
     * Nothing to do. That is only to ensure that
     * the current DMA acknowledge cycle is completed.
     */
    return 0xff;
}

static void dma_dummy_write(void *opaque, hwaddr addr,
                            uint64_t val, unsigned size)
{
    /*
     * Nothing to do. That is only to ensure that
     * the current DMA acknowledge cycle is completed.
     */
}

static const MemoryRegionOps dma_dummy_ops = {
    .read = dma_dummy_read,
    .write = dma_dummy_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void mips_jazz_init_net(IOMMUMemoryRegion *rc4030_dma_mr,
                               DeviceState *rc4030, MemoryRegion *dp8393x_prom)
{
    DeviceState *dev;
    SysBusDevice *sysbus;
    int checksum, i;
    uint8_t *prom;
    NICInfo *nd;

    nd = qemu_find_nic_info("dp8393x", true, "dp82932");
    if (!nd) {
        return;
    }

    dev = qdev_new("dp8393x");
    qdev_set_nic_properties(dev, nd);
    qdev_prop_set_uint8(dev, "it_shift", 2);
    qdev_prop_set_bit(dev, "big_endian", TARGET_BIG_ENDIAN);
    object_property_set_link(OBJECT(dev), "dma_mr",
                             OBJECT(rc4030_dma_mr), &error_abort);
    sysbus = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(sysbus, &error_fatal);
    sysbus_mmio_map(sysbus, 0, 0x80001000);
    sysbus_connect_irq(sysbus, 0, qdev_get_gpio_in(rc4030, 4));

    /* Add MAC address with valid checksum to PROM */
    prom = memory_region_get_ram_ptr(dp8393x_prom);
    checksum = 0;
    for (i = 0; i < 6; i++) {
        prom[i] = nd->macaddr.a[i];
        checksum += prom[i];
        if (checksum > 0xff) {
            checksum = (checksum + 1) & 0xff;
        }
    }
    prom[7] = 0xff - checksum;
}

#define BIOS_SIZE (4 * MiB)

#define MAGNUM_BIOS_SIZE_MAX 0x7e000
#define MAGNUM_BIOS_SIZE                                                       \
        (BIOS_SIZE < MAGNUM_BIOS_SIZE_MAX ? BIOS_SIZE : MAGNUM_BIOS_SIZE_MAX)

#define SONIC_PROM_SIZE 0x1000

static void mips_jazz_init(MachineState *machine,
                           enum jazz_model_e jazz_model)
{
    MemoryRegion *address_space = get_system_memory();
    char *filename;
    int bios_size, n;
    Clock *cpuclk;
    MIPSCPU *cpu;
    MIPSCPUClass *mcc;
    CPUMIPSState *env;
    qemu_irq *i8259;
    rc4030_dma *dmas;
    IOMMUMemoryRegion *rc4030_dma_mr;
    MemoryRegion *isa_mem = g_new(MemoryRegion, 1);
    MemoryRegion *isa_io = g_new(MemoryRegion, 1);
    MemoryRegion *rtc = g_new(MemoryRegion, 1);
    MemoryRegion *dma_dummy = g_new(MemoryRegion, 1);
    MemoryRegion *dp8393x_prom = g_new(MemoryRegion, 1);
    DeviceState *dev, *rc4030;
    MMIOKBDState *i8042;
    SysBusDevice *sysbus;
    ISABus *isa_bus;
    ISADevice *pit;
    ISADevice *pcspk;
    DriveInfo *fds[MAX_FD];
    MemoryRegion *bios = g_new(MemoryRegion, 1);
    MemoryRegion *bios2 = g_new(MemoryRegion, 1);
    SysBusESPState *sysbus_esp;
    ESPState *esp;
    static const struct {
        unsigned freq_hz;
        unsigned pll_mult;
    } ext_clk[] = {
        [JAZZ_MAGNUM] = {50000000, 2},
        [JAZZ_PICA61] = {33333333, 4},
    };

    if (machine->ram_size > 256 * MiB) {
        error_report("RAM size more than 256Mb is not supported");
        exit(EXIT_FAILURE);
    }

    cpuclk = clock_new(OBJECT(machine), "cpu-refclk");
    clock_set_hz(cpuclk, ext_clk[jazz_model].freq_hz
                         * ext_clk[jazz_model].pll_mult);

    /* init CPUs */
    cpu = mips_cpu_create_with_clock(machine->cpu_type, cpuclk);
    env = &cpu->env;
    qemu_register_reset(main_cpu_reset, cpu);

    /*
     * Chipset returns 0 in invalid reads and do not raise data exceptions.
     * However, we can't simply add a global memory region to catch
     * everything, as this would make all accesses including instruction
     * accesses be ignored and not raise exceptions.
     *
     * NOTE: this behaviour of raising exceptions for bad instruction
     * fetches but not bad data accesses was added in commit 54e755588cf1e9
     * to restore behaviour broken by c658b94f6e8c206, but it is not clear
     * whether the real hardware behaves this way. It is possible that
     * real hardware ignores bad instruction fetches as well -- if so then
     * we could replace this hijacking of CPU methods with a simple global
     * memory region that catches all memory accesses, as we do on Malta.
     */
    mcc = MIPS_CPU_GET_CLASS(cpu);
    mcc->no_data_aborts = true;

    /* allocate RAM */
    memory_region_add_subregion(address_space, 0, machine->ram);

    memory_region_init_rom(bios, NULL, "mips_jazz.bios", MAGNUM_BIOS_SIZE,
                           &error_fatal);
    memory_region_init_alias(bios2, NULL, "mips_jazz.bios", bios,
                             0, MAGNUM_BIOS_SIZE);
    memory_region_add_subregion(address_space, 0x1fc00000LL, bios);
    memory_region_add_subregion(address_space, 0xfff00000LL, bios2);

    /* load the BIOS image. */
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, machine->firmware ?: BIOS_FILENAME);
    if (filename) {
        bios_size = load_image_targphys(filename, 0xfff00000LL,
                                        MAGNUM_BIOS_SIZE);
        g_free(filename);
    } else {
        bios_size = -1;
    }
    if ((bios_size < 0 || bios_size > MAGNUM_BIOS_SIZE)
        && machine->firmware && !qtest_enabled()) {
        error_report("Could not load MIPS bios '%s'", machine->firmware);
        exit(1);
    }

    /* Init CPU internal devices */
    cpu_mips_irq_init_cpu(cpu);
    cpu_mips_clock_init(cpu);

    /* Chipset */
    rc4030 = rc4030_init(&dmas, &rc4030_dma_mr);
    sysbus = SYS_BUS_DEVICE(rc4030);
    sysbus_connect_irq(sysbus, 0, env->irq[6]);
    sysbus_connect_irq(sysbus, 1, env->irq[3]);
    memory_region_add_subregion(address_space, 0x80000000,
                                sysbus_mmio_get_region(sysbus, 0));
    memory_region_add_subregion(address_space, 0xf0000000,
                                sysbus_mmio_get_region(sysbus, 1));
    memory_region_init_io(dma_dummy, NULL, &dma_dummy_ops,
                          NULL, "dummy_dma", 0x1000);
    memory_region_add_subregion(address_space, 0x8000d000, dma_dummy);

    memory_region_init_rom(dp8393x_prom, NULL, "dp8393x-jazz.prom",
                           SONIC_PROM_SIZE, &error_fatal);
    memory_region_add_subregion(address_space, 0x8000b000, dp8393x_prom);

    /* ISA bus: IO space at 0x90000000, mem space at 0x91000000 */
    memory_region_init(isa_io, NULL, "isa-io", 0x00010000);
    memory_region_init(isa_mem, NULL, "isa-mem", 0x01000000);
    memory_region_add_subregion(address_space, 0x90000000, isa_io);
    memory_region_add_subregion(address_space, 0x91000000, isa_mem);
    isa_bus = isa_bus_new(NULL, isa_mem, isa_io, &error_abort);

    /* ISA devices */
    i8259 = i8259_init(isa_bus, env->irq[4]);
    isa_bus_register_input_irqs(isa_bus, i8259);
    i8257_dma_init(OBJECT(rc4030), isa_bus, 0);
    pit = i8254_pit_init(isa_bus, 0x40, 0, NULL);
    pcspk = isa_new(TYPE_PC_SPEAKER);
    object_property_set_link(OBJECT(pcspk), "pit", OBJECT(pit), &error_fatal);
    isa_realize_and_unref(pcspk, isa_bus, &error_fatal);

    /* Video card */
    switch (jazz_model) {
    case JAZZ_MAGNUM:
        dev = qdev_new("sysbus-g364");
        sysbus = SYS_BUS_DEVICE(dev);
        sysbus_realize_and_unref(sysbus, &error_fatal);
        sysbus_mmio_map(sysbus, 0, 0x60080000);
        sysbus_mmio_map(sysbus, 1, 0x40000000);
        sysbus_connect_irq(sysbus, 0, qdev_get_gpio_in(rc4030, 3));
        {
            /* Simple ROM, so user doesn't have to provide one */
            MemoryRegion *rom_mr = g_new(MemoryRegion, 1);
            memory_region_init_rom(rom_mr, NULL, "g364fb.rom", 0x80000,
                                   &error_fatal);
            uint8_t *rom = memory_region_get_ram_ptr(rom_mr);
            memory_region_add_subregion(address_space, 0x60000000, rom_mr);
            rom[0] = 0x10; /* Mips G364 */
        }
        break;
    case JAZZ_PICA61:
        dev = qdev_new(TYPE_VGA_MMIO);
        qdev_prop_set_uint8(dev, "it_shift", 0);
        sysbus = SYS_BUS_DEVICE(dev);
        sysbus_realize_and_unref(sysbus, &error_fatal);
        sysbus_mmio_map(sysbus, 0, 0x60000000);
        sysbus_mmio_map(sysbus, 1, 0x400a0000);
        sysbus_mmio_map(sysbus, 2, VBE_DISPI_LFB_PHYSICAL_ADDRESS);
        break;
    default:
        break;
    }

    /* Network controller */
    mips_jazz_init_net(rc4030_dma_mr, rc4030, dp8393x_prom);

    /* SCSI adapter */
    dev = qdev_new(TYPE_SYSBUS_ESP);
    sysbus_esp = SYSBUS_ESP(dev);
    esp = &sysbus_esp->esp;
    esp->dma_memory_read = rc4030_dma_read;
    esp->dma_memory_write = rc4030_dma_write;
    esp->dma_opaque = dmas[0];
    sysbus_esp->it_shift = 0;
    /* XXX for now until rc4030 has been changed to use DMA enable signal */
    esp->dma_enabled = 1;

    sysbus = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(sysbus, &error_fatal);
    sysbus_connect_irq(sysbus, 0, qdev_get_gpio_in(rc4030, 5));
    sysbus_mmio_map(sysbus, 0, 0x80002000);

    scsi_bus_legacy_handle_cmdline(&esp->bus);

    /* Floppy */
    for (n = 0; n < MAX_FD; n++) {
        fds[n] = drive_get(IF_FLOPPY, 0, n);
    }
    /* FIXME: we should enable DMA with a custom IsaDma device */
    fdctrl_init_sysbus(qdev_get_gpio_in(rc4030, 1), 0x80003000, fds);

    /* Real time clock */
    mc146818_rtc_init(isa_bus, 1980, NULL);
    memory_region_init_io(rtc, NULL, &rtc_ops, NULL, "rtc", 0x1000);
    memory_region_add_subregion(address_space, 0x80004000, rtc);

    /* Keyboard (i8042) */
    i8042 = I8042_MMIO(qdev_new(TYPE_I8042_MMIO));
    qdev_prop_set_uint64(DEVICE(i8042), "mask", 1);
    qdev_prop_set_uint32(DEVICE(i8042), "size", 0x1000);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(i8042), &error_fatal);

    qdev_connect_gpio_out(DEVICE(i8042), I8042_KBD_IRQ,
                          qdev_get_gpio_in(rc4030, 6));
    qdev_connect_gpio_out(DEVICE(i8042), I8042_MOUSE_IRQ,
                          qdev_get_gpio_in(rc4030, 7));

    memory_region_add_subregion(address_space, 0x80005000,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(i8042),
                                                       0));

    /* Serial ports */
    serial_mm_init(address_space, 0x80006000, 0,
                   qdev_get_gpio_in(rc4030, 8), 8000000 / 16,
                   serial_hd(0), DEVICE_NATIVE_ENDIAN);
    serial_mm_init(address_space, 0x80007000, 0,
                   qdev_get_gpio_in(rc4030, 9), 8000000 / 16,
                   serial_hd(1), DEVICE_NATIVE_ENDIAN);

    /* Parallel port */
    if (parallel_hds[0])
        parallel_mm_init(address_space, 0x80008000, 0,
                         qdev_get_gpio_in(rc4030, 0), parallel_hds[0]);

    /* FIXME: missing Jazz sound at 0x8000c000, rc4030[2] */

    /* NVRAM */
    dev = qdev_new("ds1225y");
    sysbus = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(sysbus, &error_fatal);
    sysbus_mmio_map(sysbus, 0, 0x80009000);

    /* LED indicator */
    sysbus_create_simple("jazz-led", 0x8000f000, NULL);

    g_free(dmas);
}

static
void mips_magnum_init(MachineState *machine)
{
    mips_jazz_init(machine, JAZZ_MAGNUM);
}

static
void mips_pica61_init(MachineState *machine)
{
    mips_jazz_init(machine, JAZZ_PICA61);
}

static void mips_magnum_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "MIPS Magnum";
    mc->init = mips_magnum_init;
    mc->block_default_type = IF_SCSI;
    mc->default_cpu_type = MIPS_CPU_TYPE_NAME("R4000");
    mc->default_ram_id = "mips_jazz.ram";
}

static const TypeInfo mips_magnum_type = {
    .name = MACHINE_TYPE_NAME("magnum"),
    .parent = TYPE_MACHINE,
    .class_init = mips_magnum_class_init,
};

static void mips_pica61_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Acer Pica 61";
    mc->init = mips_pica61_init;
    mc->block_default_type = IF_SCSI;
    mc->default_cpu_type = MIPS_CPU_TYPE_NAME("R4000");
    mc->default_ram_id = "mips_jazz.ram";
}

static const TypeInfo mips_pica61_type = {
    .name = MACHINE_TYPE_NAME("pica61"),
    .parent = TYPE_MACHINE,
    .class_init = mips_pica61_class_init,
};

static void mips_jazz_machine_init(void)
{
    type_register_static(&mips_magnum_type);
    type_register_static(&mips_pica61_type);
}

type_init(mips_jazz_machine_init)
