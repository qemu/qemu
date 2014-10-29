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

#include "hw/hw.h"
#include "hw/mips/mips.h"
#include "hw/mips/cpudevs.h"
#include "hw/i386/pc.h"
#include "hw/char/serial.h"
#include "hw/isa/isa.h"
#include "hw/block/fdc.h"
#include "sysemu/sysemu.h"
#include "sysemu/arch_init.h"
#include "hw/boards.h"
#include "net/net.h"
#include "hw/scsi/esp.h"
#include "hw/mips/bios.h"
#include "hw/loader.h"
#include "hw/timer/mc146818rtc.h"
#include "hw/timer/i8254.h"
#include "hw/audio/pcspk.h"
#include "sysemu/block-backend.h"
#include "hw/sysbus.h"
#include "exec/address-spaces.h"
#include "sysemu/qtest.h"
#include "qemu/error-report.h"

enum jazz_model_e
{
    JAZZ_MAGNUM,
    JAZZ_PICA61,
};

static void main_cpu_reset(void *opaque)
{
    MIPSCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
}

static uint64_t rtc_read(void *opaque, hwaddr addr, unsigned size)
{
    return cpu_inw(0x71);
}

static void rtc_write(void *opaque, hwaddr addr,
                      uint64_t val, unsigned size)
{
    cpu_outw(0x71, val & 0xff);
}

static const MemoryRegionOps rtc_ops = {
    .read = rtc_read,
    .write = rtc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static uint64_t dma_dummy_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    /* Nothing to do. That is only to ensure that
     * the current DMA acknowledge cycle is completed. */
    return 0xff;
}

static void dma_dummy_write(void *opaque, hwaddr addr,
                            uint64_t val, unsigned size)
{
    /* Nothing to do. That is only to ensure that
     * the current DMA acknowledge cycle is completed. */
}

static const MemoryRegionOps dma_dummy_ops = {
    .read = dma_dummy_read,
    .write = dma_dummy_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

#define MAGNUM_BIOS_SIZE_MAX 0x7e000
#define MAGNUM_BIOS_SIZE (BIOS_SIZE < MAGNUM_BIOS_SIZE_MAX ? BIOS_SIZE : MAGNUM_BIOS_SIZE_MAX)

static void cpu_request_exit(void *opaque, int irq, int level)
{
    CPUState *cpu = current_cpu;

    if (cpu && level) {
        cpu_exit(cpu);
    }
}

static CPUUnassignedAccess real_do_unassigned_access;
static void mips_jazz_do_unassigned_access(CPUState *cpu, hwaddr addr,
                                           bool is_write, bool is_exec,
                                           int opaque, unsigned size)
{
    if (!is_exec) {
        /* ignore invalid access (ie do not raise exception) */
        return;
    }
    (*real_do_unassigned_access)(cpu, addr, is_write, is_exec, opaque, size);
}

static void mips_jazz_init(MemoryRegion *address_space,
                           MemoryRegion *address_space_io,
                           ram_addr_t ram_size,
                           const char *cpu_model,
                           enum jazz_model_e jazz_model)
{
    char *filename;
    int bios_size, n;
    MIPSCPU *cpu;
    CPUClass *cc;
    CPUMIPSState *env;
    qemu_irq *rc4030, *i8259;
    rc4030_dma *dmas;
    void* rc4030_opaque;
    MemoryRegion *isa = g_new(MemoryRegion, 1);
    MemoryRegion *rtc = g_new(MemoryRegion, 1);
    MemoryRegion *i8042 = g_new(MemoryRegion, 1);
    MemoryRegion *dma_dummy = g_new(MemoryRegion, 1);
    NICInfo *nd;
    DeviceState *dev;
    SysBusDevice *sysbus;
    ISABus *isa_bus;
    ISADevice *pit;
    DriveInfo *fds[MAX_FD];
    qemu_irq esp_reset, dma_enable;
    qemu_irq *cpu_exit_irq;
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    MemoryRegion *bios = g_new(MemoryRegion, 1);
    MemoryRegion *bios2 = g_new(MemoryRegion, 1);

    /* init CPUs */
    if (cpu_model == NULL) {
#ifdef TARGET_MIPS64
        cpu_model = "R4000";
#else
        /* FIXME: All wrong, this maybe should be R3000 for the older JAZZs. */
        cpu_model = "24Kf";
#endif
    }
    cpu = cpu_mips_init(cpu_model);
    if (cpu == NULL) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }
    env = &cpu->env;
    qemu_register_reset(main_cpu_reset, cpu);

    /* Chipset returns 0 in invalid reads and do not raise data exceptions.
     * However, we can't simply add a global memory region to catch
     * everything, as memory core directly call unassigned_mem_read/write
     * on some invalid accesses, which call do_unassigned_access on the
     * CPU, which raise an exception.
     * Handle that case by hijacking the do_unassigned_access method on
     * the CPU, and do not raise exceptions for data access. */
    cc = CPU_GET_CLASS(cpu);
    real_do_unassigned_access = cc->do_unassigned_access;
    cc->do_unassigned_access = mips_jazz_do_unassigned_access;

    /* allocate RAM */
    memory_region_init_ram(ram, NULL, "mips_jazz.ram", ram_size, &error_abort);
    vmstate_register_ram_global(ram);
    memory_region_add_subregion(address_space, 0, ram);

    memory_region_init_ram(bios, NULL, "mips_jazz.bios", MAGNUM_BIOS_SIZE,
                           &error_abort);
    vmstate_register_ram_global(bios);
    memory_region_set_readonly(bios, true);
    memory_region_init_alias(bios2, NULL, "mips_jazz.bios", bios,
                             0, MAGNUM_BIOS_SIZE);
    memory_region_add_subregion(address_space, 0x1fc00000LL, bios);
    memory_region_add_subregion(address_space, 0xfff00000LL, bios2);

    /* load the BIOS image. */
    if (bios_name == NULL)
        bios_name = BIOS_FILENAME;
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    if (filename) {
        bios_size = load_image_targphys(filename, 0xfff00000LL,
                                        MAGNUM_BIOS_SIZE);
        g_free(filename);
    } else {
        bios_size = -1;
    }
    if ((bios_size < 0 || bios_size > MAGNUM_BIOS_SIZE) && !qtest_enabled()) {
        error_report("Could not load MIPS bios '%s'", bios_name);
        exit(1);
    }

    /* Init CPU internal devices */
    cpu_mips_irq_init_cpu(env);
    cpu_mips_clock_init(env);

    /* Chipset */
    rc4030_opaque = rc4030_init(env->irq[6], env->irq[3], &rc4030, &dmas,
                                address_space);
    memory_region_init_io(dma_dummy, NULL, &dma_dummy_ops, NULL, "dummy_dma", 0x1000);
    memory_region_add_subregion(address_space, 0x8000d000, dma_dummy);

    /* ISA devices */
    isa_bus = isa_bus_new(NULL, address_space_io);
    i8259 = i8259_init(isa_bus, env->irq[4]);
    isa_bus_irqs(isa_bus, i8259);
    cpu_exit_irq = qemu_allocate_irqs(cpu_request_exit, NULL, 1);
    DMA_init(0, cpu_exit_irq);
    pit = pit_init(isa_bus, 0x40, 0, NULL);
    pcspk_init(isa_bus, pit);

    /* ISA IO space at 0x90000000 */
    memory_region_init_alias(isa, NULL, "isa_mmio",
                             get_system_io(), 0, 0x01000000);
    memory_region_add_subregion(address_space, 0x90000000, isa);
    isa_mem_base = 0x11000000;

    /* Video card */
    switch (jazz_model) {
    case JAZZ_MAGNUM:
        dev = qdev_create(NULL, "sysbus-g364");
        qdev_init_nofail(dev);
        sysbus = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(sysbus, 0, 0x60080000);
        sysbus_mmio_map(sysbus, 1, 0x40000000);
        sysbus_connect_irq(sysbus, 0, rc4030[3]);
        {
            /* Simple ROM, so user doesn't have to provide one */
            MemoryRegion *rom_mr = g_new(MemoryRegion, 1);
            memory_region_init_ram(rom_mr, NULL, "g364fb.rom", 0x80000,
                                   &error_abort);
            vmstate_register_ram_global(rom_mr);
            memory_region_set_readonly(rom_mr, true);
            uint8_t *rom = memory_region_get_ram_ptr(rom_mr);
            memory_region_add_subregion(address_space, 0x60000000, rom_mr);
            rom[0] = 0x10; /* Mips G364 */
        }
        break;
    case JAZZ_PICA61:
        isa_vga_mm_init(0x40000000, 0x60000000, 0, get_system_memory());
        break;
    default:
        break;
    }

    /* Network controller */
    for (n = 0; n < nb_nics; n++) {
        nd = &nd_table[n];
        if (!nd->model)
            nd->model = g_strdup("dp83932");
        if (strcmp(nd->model, "dp83932") == 0) {
            dp83932_init(nd, 0x80001000, 2, get_system_memory(), rc4030[4],
                         rc4030_opaque, rc4030_dma_memory_rw);
            break;
        } else if (is_help_option(nd->model)) {
            fprintf(stderr, "qemu: Supported NICs: dp83932\n");
            exit(1);
        } else {
            fprintf(stderr, "qemu: Unsupported NIC: %s\n", nd->model);
            exit(1);
        }
    }

    /* SCSI adapter */
    esp_init(0x80002000, 0,
             rc4030_dma_read, rc4030_dma_write, dmas[0],
             rc4030[5], &esp_reset, &dma_enable);

    /* Floppy */
    if (drive_get_max_bus(IF_FLOPPY) >= MAX_FD) {
        fprintf(stderr, "qemu: too many floppy drives\n");
        exit(1);
    }
    for (n = 0; n < MAX_FD; n++) {
        fds[n] = drive_get(IF_FLOPPY, 0, n);
    }
    fdctrl_init_sysbus(rc4030[1], 0, 0x80003000, fds);

    /* Real time clock */
    rtc_init(isa_bus, 1980, NULL);
    memory_region_init_io(rtc, NULL, &rtc_ops, NULL, "rtc", 0x1000);
    memory_region_add_subregion(address_space, 0x80004000, rtc);

    /* Keyboard (i8042) */
    i8042_mm_init(rc4030[6], rc4030[7], i8042, 0x1000, 0x1);
    memory_region_add_subregion(address_space, 0x80005000, i8042);

    /* Serial ports */
    if (serial_hds[0]) {
        serial_mm_init(address_space, 0x80006000, 0, rc4030[8], 8000000/16,
                       serial_hds[0], DEVICE_NATIVE_ENDIAN);
    }
    if (serial_hds[1]) {
        serial_mm_init(address_space, 0x80007000, 0, rc4030[9], 8000000/16,
                       serial_hds[1], DEVICE_NATIVE_ENDIAN);
    }

    /* Parallel port */
    if (parallel_hds[0])
        parallel_mm_init(address_space, 0x80008000, 0, rc4030[0],
                         parallel_hds[0]);

    /* FIXME: missing Jazz sound at 0x8000c000, rc4030[2] */

    /* NVRAM */
    dev = qdev_create(NULL, "ds1225y");
    qdev_init_nofail(dev);
    sysbus = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(sysbus, 0, 0x80009000);

    /* LED indicator */
    sysbus_create_simple("jazz-led", 0x8000f000, NULL);
}

static
void mips_magnum_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *cpu_model = machine->cpu_model;
        mips_jazz_init(get_system_memory(), get_system_io(),
                       ram_size, cpu_model, JAZZ_MAGNUM);
}

static
void mips_pica61_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *cpu_model = machine->cpu_model;
    mips_jazz_init(get_system_memory(), get_system_io(),
                   ram_size, cpu_model, JAZZ_PICA61);
}

static QEMUMachine mips_magnum_machine = {
    .name = "magnum",
    .desc = "MIPS Magnum",
    .init = mips_magnum_init,
    .block_default_type = IF_SCSI,
};

static QEMUMachine mips_pica61_machine = {
    .name = "pica61",
    .desc = "Acer Pica 61",
    .init = mips_pica61_init,
    .block_default_type = IF_SCSI,
};

static void mips_jazz_machine_init(void)
{
    qemu_register_machine(&mips_magnum_machine);
    qemu_register_machine(&mips_pica61_machine);
}

machine_init(mips_jazz_machine_init);
