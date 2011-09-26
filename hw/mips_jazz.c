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

#include "hw.h"
#include "mips.h"
#include "mips_cpudevs.h"
#include "pc.h"
#include "isa.h"
#include "fdc.h"
#include "sysemu.h"
#include "arch_init.h"
#include "boards.h"
#include "net.h"
#include "esp.h"
#include "mips-bios.h"
#include "loader.h"
#include "mc146818rtc.h"
#include "blockdev.h"
#include "sysbus.h"
#include "exec-memory.h"

enum jazz_model_e
{
    JAZZ_MAGNUM,
    JAZZ_PICA61,
};

static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    cpu_reset(env);
}

static uint64_t rtc_read(void *opaque, target_phys_addr_t addr, unsigned size)
{
    return cpu_inw(0x71);
}

static void rtc_write(void *opaque, target_phys_addr_t addr,
                      uint64_t val, unsigned size)
{
    cpu_outw(0x71, val & 0xff);
}

static const MemoryRegionOps rtc_ops = {
    .read = rtc_read,
    .write = rtc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static uint64_t dma_dummy_read(void *opaque, target_phys_addr_t addr,
                               unsigned size)
{
    /* Nothing to do. That is only to ensure that
     * the current DMA acknowledge cycle is completed. */
    return 0xff;
}

static void dma_dummy_write(void *opaque, target_phys_addr_t addr,
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
    CPUState *env = cpu_single_env;

    if (env && level) {
        cpu_exit(env);
    }
}

static void mips_jazz_init(MemoryRegion *address_space,
                           MemoryRegion *address_space_io,
                           ram_addr_t ram_size,
                           const char *cpu_model,
                           enum jazz_model_e jazz_model)
{
    char *filename;
    int bios_size, n;
    CPUState *env;
    qemu_irq *rc4030, *i8259;
    rc4030_dma *dmas;
    void* rc4030_opaque;
    MemoryRegion *rtc = g_new(MemoryRegion, 1);
    MemoryRegion *i8042 = g_new(MemoryRegion, 1);
    MemoryRegion *dma_dummy = g_new(MemoryRegion, 1);
    NICInfo *nd;
    DeviceState *dev;
    SysBusDevice *sysbus;
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
    env = cpu_init(cpu_model);
    if (!env) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }
    qemu_register_reset(main_cpu_reset, env);

    /* allocate RAM */
    memory_region_init_ram(ram, NULL, "mips_jazz.ram", ram_size);
    memory_region_add_subregion(address_space, 0, ram);

    memory_region_init_ram(bios, NULL, "mips_jazz.bios", MAGNUM_BIOS_SIZE);
    memory_region_set_readonly(bios, true);
    memory_region_init_alias(bios2, "mips_jazz.bios", bios,
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
    if (bios_size < 0 || bios_size > MAGNUM_BIOS_SIZE) {
        fprintf(stderr, "qemu: Could not load MIPS bios '%s'\n",
                bios_name);
        exit(1);
    }

    /* Init CPU internal devices */
    cpu_mips_irq_init_cpu(env);
    cpu_mips_clock_init(env);

    /* Chipset */
    rc4030_opaque = rc4030_init(env->irq[6], env->irq[3], &rc4030, &dmas);
    memory_region_init_io(dma_dummy, &dma_dummy_ops, NULL, "dummy_dma", 0x1000);
    memory_region_add_subregion(address_space, 0x8000d000, dma_dummy);

    /* ISA devices */
    isa_bus_new(NULL, address_space_io);
    i8259 = i8259_init(env->irq[4]);
    isa_bus_irqs(i8259);
    cpu_exit_irq = qemu_allocate_irqs(cpu_request_exit, NULL, 1);
    DMA_init(0, cpu_exit_irq);
    pit = pit_init(0x40, 0);
    pcspk_init(pit);

    /* ISA IO space at 0x90000000 */
    isa_mmio_init(0x90000000, 0x01000000);
    isa_mem_base = 0x11000000;

    /* Video card */
    switch (jazz_model) {
    case JAZZ_MAGNUM:
        dev = qdev_create(NULL, "sysbus-g364");
        qdev_init_nofail(dev);
        sysbus = sysbus_from_qdev(dev);
        sysbus_mmio_map(sysbus, 0, 0x60080000);
        sysbus_mmio_map(sysbus, 1, 0x40000000);
        sysbus_connect_irq(sysbus, 0, rc4030[3]);
        {
            /* Simple ROM, so user doesn't have to provide one */
            MemoryRegion *rom_mr = g_new(MemoryRegion, 1);
            memory_region_init_ram(rom_mr, NULL, "g364fb.rom", 0x80000);
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
            dp83932_init(nd, 0x80001000, 2, rc4030[4],
                         rc4030_opaque, rc4030_dma_memory_rw);
            break;
        } else if (strcmp(nd->model, "?") == 0) {
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
    rtc_init(1980, NULL);
    memory_region_init_io(rtc, &rtc_ops, NULL, "rtc", 0x1000);
    memory_region_add_subregion(address_space, 0x80004000, rtc);

    /* Keyboard (i8042) */
    i8042_mm_init(rc4030[6], rc4030[7], i8042, 0x1000, 0x1);
    memory_region_add_subregion(address_space, 0x80005000, i8042);

    /* Serial ports */
    if (serial_hds[0]) {
#ifdef TARGET_WORDS_BIGENDIAN
        serial_mm_init(0x80006000, 0, rc4030[8], 8000000/16, serial_hds[0], 1, 1);
#else
        serial_mm_init(0x80006000, 0, rc4030[8], 8000000/16, serial_hds[0], 1, 0);
#endif
    }
    if (serial_hds[1]) {
#ifdef TARGET_WORDS_BIGENDIAN
        serial_mm_init(0x80007000, 0, rc4030[9], 8000000/16, serial_hds[1], 1, 1);
#else
        serial_mm_init(0x80007000, 0, rc4030[9], 8000000/16, serial_hds[1], 1, 0);
#endif
    }

    /* Parallel port */
    if (parallel_hds[0])
        parallel_mm_init(0x80008000, 0, rc4030[0], parallel_hds[0]);

    /* Sound card */
    /* FIXME: missing Jazz sound at 0x8000c000, rc4030[2] */
    audio_init(i8259, NULL);

    /* NVRAM */
    dev = qdev_create(NULL, "ds1225y");
    qdev_init_nofail(dev);
    sysbus = sysbus_from_qdev(dev);
    sysbus_mmio_map(sysbus, 0, 0x80009000);

    /* LED indicator */
    jazz_led_init(0x8000f000);
}

static
void mips_magnum_init (ram_addr_t ram_size,
                       const char *boot_device,
                       const char *kernel_filename, const char *kernel_cmdline,
                       const char *initrd_filename, const char *cpu_model)
{
        mips_jazz_init(get_system_memory(), get_system_io(),
                       ram_size, cpu_model, JAZZ_MAGNUM);
}

static
void mips_pica61_init (ram_addr_t ram_size,
                       const char *boot_device,
                       const char *kernel_filename, const char *kernel_cmdline,
                       const char *initrd_filename, const char *cpu_model)
{
    mips_jazz_init(get_system_memory(), get_system_io(),
                   ram_size, cpu_model, JAZZ_PICA61);
}

static QEMUMachine mips_magnum_machine = {
    .name = "magnum",
    .desc = "MIPS Magnum",
    .init = mips_magnum_init,
    .use_scsi = 1,
};

static QEMUMachine mips_pica61_machine = {
    .name = "pica61",
    .desc = "Acer Pica 61",
    .init = mips_pica61_init,
    .use_scsi = 1,
};

static void mips_jazz_machine_init(void)
{
    qemu_register_machine(&mips_magnum_machine);
    qemu_register_machine(&mips_pica61_machine);
}

machine_init(mips_jazz_machine_init);
