/*
 * QEMU PowerPC 405 evaluation boards emulation
 *
 * Copyright (c) 2007 Jocelyn Mayer
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
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/ppc/ppc.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "ppc405.h"
#include "hw/rtc/m48t59.h"
#include "hw/block/flash.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"
#include "sysemu/reset.h"
#include "sysemu/block-backend.h"
#include "hw/boards.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/loader.h"
#include "exec/address-spaces.h"
#include "qemu/cutils.h"

#define BIOS_FILENAME "ppc405_rom.bin"
#define BIOS_SIZE (2 * MiB)

#define KERNEL_LOAD_ADDR 0x00000000
#define INITRD_LOAD_ADDR 0x01800000

#define USE_FLASH_BIOS

/*****************************************************************************/
/* PPC405EP reference board (IBM) */
/* Standalone board with:
 * - PowerPC 405EP CPU
 * - SDRAM (0x00000000)
 * - Flash (0xFFF80000)
 * - SRAM  (0xFFF00000)
 * - NVRAM (0xF0000000)
 * - FPGA  (0xF0300000)
 */
typedef struct ref405ep_fpga_t ref405ep_fpga_t;
struct ref405ep_fpga_t {
    uint8_t reg0;
    uint8_t reg1;
};

static uint64_t ref405ep_fpga_readb(void *opaque, hwaddr addr, unsigned size)
{
    ref405ep_fpga_t *fpga;
    uint32_t ret;

    fpga = opaque;
    switch (addr) {
    case 0x0:
        ret = fpga->reg0;
        break;
    case 0x1:
        ret = fpga->reg1;
        break;
    default:
        ret = 0;
        break;
    }

    return ret;
}

static void ref405ep_fpga_writeb(void *opaque, hwaddr addr, uint64_t value,
                                 unsigned size)
{
    ref405ep_fpga_t *fpga;

    fpga = opaque;
    switch (addr) {
    case 0x0:
        /* Read only */
        break;
    case 0x1:
        fpga->reg1 = value;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps ref405ep_fpga_ops = {
    .read = ref405ep_fpga_readb,
    .write = ref405ep_fpga_writeb,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void ref405ep_fpga_reset (void *opaque)
{
    ref405ep_fpga_t *fpga;

    fpga = opaque;
    fpga->reg0 = 0x00;
    fpga->reg1 = 0x0F;
}

static void ref405ep_fpga_init(MemoryRegion *sysmem, uint32_t base)
{
    ref405ep_fpga_t *fpga;
    MemoryRegion *fpga_memory = g_new(MemoryRegion, 1);

    fpga = g_malloc0(sizeof(ref405ep_fpga_t));
    memory_region_init_io(fpga_memory, NULL, &ref405ep_fpga_ops, fpga,
                          "fpga", 0x00000100);
    memory_region_add_subregion(sysmem, base, fpga_memory);
    qemu_register_reset(&ref405ep_fpga_reset, fpga);
}

static void ref405ep_init(MachineState *machine)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    const char *bios_name = machine->firmware ?: BIOS_FILENAME;
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *initrd_filename = machine->initrd_filename;
    char *filename;
    ppc4xx_bd_info_t bd;
    CPUPPCState *env;
    DeviceState *dev;
    SysBusDevice *s;
    qemu_irq *pic;
    MemoryRegion *bios;
    MemoryRegion *sram = g_new(MemoryRegion, 1);
    ram_addr_t bdloc;
    MemoryRegion *ram_memories = g_new(MemoryRegion, 2);
    hwaddr ram_bases[2], ram_sizes[2];
    target_ulong sram_size;
    long bios_size;
    //int phy_addr = 0;
    //static int phy_addr = 1;
    target_ulong kernel_base, initrd_base;
    long kernel_size, initrd_size;
    int linux_boot;
    int len;
    DriveInfo *dinfo;
    MemoryRegion *sysmem = get_system_memory();

    if (machine->ram_size != mc->default_ram_size) {
        char *sz = size_to_str(mc->default_ram_size);
        error_report("Invalid RAM size, should be %s", sz);
        g_free(sz);
        exit(EXIT_FAILURE);
    }

    /* XXX: fix this */
    memory_region_init_alias(&ram_memories[0], NULL, "ef405ep.ram.alias",
                             machine->ram, 0, machine->ram_size);
    ram_bases[0] = 0;
    ram_sizes[0] = machine->ram_size;
    memory_region_init(&ram_memories[1], NULL, "ef405ep.ram1", 0);
    ram_bases[1] = 0x00000000;
    ram_sizes[1] = 0x00000000;
    env = ppc405ep_init(sysmem, ram_memories, ram_bases, ram_sizes,
                        33333333, &pic, kernel_filename == NULL ? 0 : 1);
    /* allocate SRAM */
    sram_size = 512 * KiB;
    memory_region_init_ram(sram, NULL, "ef405ep.sram", sram_size,
                           &error_fatal);
    memory_region_add_subregion(sysmem, 0xFFF00000, sram);
    /* allocate and load BIOS */
#ifdef USE_FLASH_BIOS
    dinfo = drive_get(IF_PFLASH, 0, 0);
    if (dinfo) {
        bios_size = 8 * MiB;
        pflash_cfi02_register((uint32_t)(-bios_size),
                              "ef405ep.bios", bios_size,
                              blk_by_legacy_dinfo(dinfo),
                              64 * KiB, 1,
                              2, 0x0001, 0x22DA, 0x0000, 0x0000, 0x555, 0x2AA,
                              1);
    } else
#endif
    {
        bios = g_new(MemoryRegion, 1);
        memory_region_init_rom(bios, NULL, "ef405ep.bios", BIOS_SIZE,
                               &error_fatal);

        filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
        if (filename) {
            bios_size = load_image_size(filename,
                                        memory_region_get_ram_ptr(bios),
                                        BIOS_SIZE);
            g_free(filename);
            if (bios_size < 0) {
                error_report("Could not load PowerPC BIOS '%s'", bios_name);
                exit(1);
            }
            bios_size = (bios_size + 0xfff) & ~0xfff;
            memory_region_add_subregion(sysmem, (uint32_t)(-bios_size), bios);
        } else if (!qtest_enabled() || kernel_filename != NULL) {
            error_report("Could not load PowerPC BIOS '%s'", bios_name);
            exit(1);
        } else {
            /* Avoid an uninitialized variable warning */
            bios_size = -1;
        }
    }
    /* Register FPGA */
    ref405ep_fpga_init(sysmem, 0xF0300000);
    /* Register NVRAM */
    dev = qdev_new("sysbus-m48t08");
    qdev_prop_set_int32(dev, "base-year", 1968);
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, 0xF0000000);
    /* Load kernel */
    linux_boot = (kernel_filename != NULL);
    if (linux_boot) {
        memset(&bd, 0, sizeof(bd));
        bd.bi_memstart = 0x00000000;
        bd.bi_memsize = machine->ram_size;
        bd.bi_flashstart = -bios_size;
        bd.bi_flashsize = -bios_size;
        bd.bi_flashoffset = 0;
        bd.bi_sramstart = 0xFFF00000;
        bd.bi_sramsize = sram_size;
        bd.bi_bootflags = 0;
        bd.bi_intfreq = 133333333;
        bd.bi_busfreq = 33333333;
        bd.bi_baudrate = 115200;
        bd.bi_s_version[0] = 'Q';
        bd.bi_s_version[1] = 'M';
        bd.bi_s_version[2] = 'U';
        bd.bi_s_version[3] = '\0';
        bd.bi_r_version[0] = 'Q';
        bd.bi_r_version[1] = 'E';
        bd.bi_r_version[2] = 'M';
        bd.bi_r_version[3] = 'U';
        bd.bi_r_version[4] = '\0';
        bd.bi_procfreq = 133333333;
        bd.bi_plb_busfreq = 33333333;
        bd.bi_pci_busfreq = 33333333;
        bd.bi_opbfreq = 33333333;
        bdloc = ppc405_set_bootinfo(env, &bd, 0x00000001);
        env->gpr[3] = bdloc;
        kernel_base = KERNEL_LOAD_ADDR;
        /* now we can load the kernel */
        kernel_size = load_image_targphys(kernel_filename, kernel_base,
                                          machine->ram_size - kernel_base);
        if (kernel_size < 0) {
            error_report("could not load kernel '%s'", kernel_filename);
            exit(1);
        }
        printf("Load kernel size %ld at " TARGET_FMT_lx,
               kernel_size, kernel_base);
        /* load initrd */
        if (initrd_filename) {
            initrd_base = INITRD_LOAD_ADDR;
            initrd_size = load_image_targphys(initrd_filename, initrd_base,
                                              machine->ram_size - initrd_base);
            if (initrd_size < 0) {
                error_report("could not load initial ram disk '%s'",
                             initrd_filename);
                exit(1);
            }
        } else {
            initrd_base = 0;
            initrd_size = 0;
        }
        env->gpr[4] = initrd_base;
        env->gpr[5] = initrd_size;
        if (kernel_cmdline != NULL) {
            len = strlen(kernel_cmdline);
            bdloc -= ((len + 255) & ~255);
            cpu_physical_memory_write(bdloc, kernel_cmdline, len + 1);
            env->gpr[6] = bdloc;
            env->gpr[7] = bdloc + len;
        } else {
            env->gpr[6] = 0;
            env->gpr[7] = 0;
        }
        env->nip = KERNEL_LOAD_ADDR;
    } else {
        kernel_base = 0;
        kernel_size = 0;
        initrd_base = 0;
        initrd_size = 0;
        bdloc = 0;
    }
}

static void ref405ep_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "ref405ep";
    mc->init = ref405ep_init;
    mc->default_ram_size = 0x08000000;
    mc->default_ram_id = "ef405ep.ram";
}

static const TypeInfo ref405ep_type = {
    .name = MACHINE_TYPE_NAME("ref405ep"),
    .parent = TYPE_MACHINE,
    .class_init = ref405ep_class_init,
};

/*****************************************************************************/
/* AMCC Taihu evaluation board */
/* - PowerPC 405EP processor
 * - SDRAM               128 MB at 0x00000000
 * - Boot flash          2 MB   at 0xFFE00000
 * - Application flash   32 MB  at 0xFC000000
 * - 2 serial ports
 * - 2 ethernet PHY
 * - 1 USB 1.1 device    0x50000000
 * - 1 LCD display       0x50100000
 * - 1 CPLD              0x50100000
 * - 1 I2C EEPROM
 * - 1 I2C thermal sensor
 * - a set of LEDs
 * - bit-bang SPI port using GPIOs
 * - 1 EBC interface connector 0 0x50200000
 * - 1 cardbus controller + expansion slot.
 * - 1 PCI expansion slot.
 */
typedef struct taihu_cpld_t taihu_cpld_t;
struct taihu_cpld_t {
    uint8_t reg0;
    uint8_t reg1;
};

static uint64_t taihu_cpld_read(void *opaque, hwaddr addr, unsigned size)
{
    taihu_cpld_t *cpld;
    uint32_t ret;

    cpld = opaque;
    switch (addr) {
    case 0x0:
        ret = cpld->reg0;
        break;
    case 0x1:
        ret = cpld->reg1;
        break;
    default:
        ret = 0;
        break;
    }

    return ret;
}

static void taihu_cpld_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size)
{
    taihu_cpld_t *cpld;

    cpld = opaque;
    switch (addr) {
    case 0x0:
        /* Read only */
        break;
    case 0x1:
        cpld->reg1 = value;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps taihu_cpld_ops = {
    .read = taihu_cpld_read,
    .write = taihu_cpld_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void taihu_cpld_reset (void *opaque)
{
    taihu_cpld_t *cpld;

    cpld = opaque;
    cpld->reg0 = 0x01;
    cpld->reg1 = 0x80;
}

static void taihu_cpld_init(MemoryRegion *sysmem, uint32_t base)
{
    taihu_cpld_t *cpld;
    MemoryRegion *cpld_memory = g_new(MemoryRegion, 1);

    cpld = g_malloc0(sizeof(taihu_cpld_t));
    memory_region_init_io(cpld_memory, NULL, &taihu_cpld_ops, cpld, "cpld", 0x100);
    memory_region_add_subregion(sysmem, base, cpld_memory);
    qemu_register_reset(&taihu_cpld_reset, cpld);
}

static void taihu_405ep_init(MachineState *machine)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    const char *bios_name = machine->firmware ?: BIOS_FILENAME;
    const char *kernel_filename = machine->kernel_filename;
    const char *initrd_filename = machine->initrd_filename;
    char *filename;
    qemu_irq *pic;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *bios;
    MemoryRegion *ram_memories = g_new(MemoryRegion, 2);
    hwaddr ram_bases[2], ram_sizes[2];
    long bios_size;
    target_ulong kernel_base, initrd_base;
    long kernel_size, initrd_size;
    int linux_boot;
    int fl_idx;
    DriveInfo *dinfo;

    if (machine->ram_size != mc->default_ram_size) {
        char *sz = size_to_str(mc->default_ram_size);
        error_report("Invalid RAM size, should be %s", sz);
        g_free(sz);
        exit(EXIT_FAILURE);
    }

    ram_bases[0] = 0;
    ram_sizes[0] = 0x04000000;
    memory_region_init_alias(&ram_memories[0], NULL,
                             "taihu_405ep.ram-0", machine->ram, ram_bases[0],
                             ram_sizes[0]);
    ram_bases[1] = 0x04000000;
    ram_sizes[1] = 0x04000000;
    memory_region_init_alias(&ram_memories[1], NULL,
                             "taihu_405ep.ram-1", machine->ram, ram_bases[1],
                             ram_sizes[1]);
    ppc405ep_init(sysmem, ram_memories, ram_bases, ram_sizes,
                  33333333, &pic, kernel_filename == NULL ? 0 : 1);
    /* allocate and load BIOS */
    fl_idx = 0;
#if defined(USE_FLASH_BIOS)
    dinfo = drive_get(IF_PFLASH, 0, fl_idx);
    if (dinfo) {
        bios_size = 2 * MiB;
        pflash_cfi02_register(0xFFE00000,
                              "taihu_405ep.bios", bios_size,
                              blk_by_legacy_dinfo(dinfo),
                              64 * KiB, 1,
                              4, 0x0001, 0x22DA, 0x0000, 0x0000, 0x555, 0x2AA,
                              1);
        fl_idx++;
    } else
#endif
    {
        bios = g_new(MemoryRegion, 1);
        memory_region_init_rom(bios, NULL, "taihu_405ep.bios", BIOS_SIZE,
                               &error_fatal);
        filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
        if (filename) {
            bios_size = load_image_size(filename,
                                        memory_region_get_ram_ptr(bios),
                                        BIOS_SIZE);
            g_free(filename);
            if (bios_size < 0) {
                error_report("Could not load PowerPC BIOS '%s'", bios_name);
                exit(1);
            }
            bios_size = (bios_size + 0xfff) & ~0xfff;
            memory_region_add_subregion(sysmem, (uint32_t)(-bios_size), bios);
        } else if (!qtest_enabled()) {
            error_report("Could not load PowerPC BIOS '%s'", bios_name);
            exit(1);
        }
    }
    /* Register Linux flash */
    dinfo = drive_get(IF_PFLASH, 0, fl_idx);
    if (dinfo) {
        bios_size = 32 * MiB;
        pflash_cfi02_register(0xfc000000, "taihu_405ep.flash", bios_size,
                              blk_by_legacy_dinfo(dinfo),
                              64 * KiB, 1,
                              4, 0x0001, 0x22DA, 0x0000, 0x0000, 0x555, 0x2AA,
                              1);
        fl_idx++;
    }
    /* Register CLPD & LCD display */
    taihu_cpld_init(sysmem, 0x50100000);
    /* Load kernel */
    linux_boot = (kernel_filename != NULL);
    if (linux_boot) {
        kernel_base = KERNEL_LOAD_ADDR;
        /* now we can load the kernel */
        kernel_size = load_image_targphys(kernel_filename, kernel_base,
                                          machine->ram_size - kernel_base);
        if (kernel_size < 0) {
            error_report("could not load kernel '%s'", kernel_filename);
            exit(1);
        }
        /* load initrd */
        if (initrd_filename) {
            initrd_base = INITRD_LOAD_ADDR;
            initrd_size = load_image_targphys(initrd_filename, initrd_base,
                                              machine->ram_size - initrd_base);
            if (initrd_size < 0) {
                error_report("could not load initial ram disk '%s'",
                             initrd_filename);
                exit(1);
            }
        } else {
            initrd_base = 0;
            initrd_size = 0;
        }
    } else {
        kernel_base = 0;
        kernel_size = 0;
        initrd_base = 0;
        initrd_size = 0;
    }
}

static void taihu_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "taihu";
    mc->init = taihu_405ep_init;
    mc->default_ram_size = 0x08000000;
    mc->default_ram_id = "taihu_405ep.ram";
}

static const TypeInfo taihu_type = {
    .name = MACHINE_TYPE_NAME("taihu"),
    .parent = TYPE_MACHINE,
    .class_init = taihu_class_init,
};

static void ppc405_machine_init(void)
{
    type_register_static(&ref405ep_type);
    type_register_static(&taihu_type);
}

type_init(ppc405_machine_init)
