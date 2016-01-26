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
#include "hw/hw.h"
#include "hw/ppc/ppc.h"
#include "ppc405.h"
#include "hw/timer/m48t59.h"
#include "hw/block/flash.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"
#include "sysemu/block-backend.h"
#include "hw/boards.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/loader.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "exec/address-spaces.h"

#define BIOS_FILENAME "ppc405_rom.bin"
#define BIOS_SIZE (2048 * 1024)

#define KERNEL_LOAD_ADDR 0x00000000
#define INITRD_LOAD_ADDR 0x01800000

#define USE_FLASH_BIOS

//#define DEBUG_BOARD_INIT

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

static uint32_t ref405ep_fpga_readb (void *opaque, hwaddr addr)
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

static void ref405ep_fpga_writeb (void *opaque,
                                  hwaddr addr, uint32_t value)
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

static uint32_t ref405ep_fpga_readw (void *opaque, hwaddr addr)
{
    uint32_t ret;

    ret = ref405ep_fpga_readb(opaque, addr) << 8;
    ret |= ref405ep_fpga_readb(opaque, addr + 1);

    return ret;
}

static void ref405ep_fpga_writew (void *opaque,
                                  hwaddr addr, uint32_t value)
{
    ref405ep_fpga_writeb(opaque, addr, (value >> 8) & 0xFF);
    ref405ep_fpga_writeb(opaque, addr + 1, value & 0xFF);
}

static uint32_t ref405ep_fpga_readl (void *opaque, hwaddr addr)
{
    uint32_t ret;

    ret = ref405ep_fpga_readb(opaque, addr) << 24;
    ret |= ref405ep_fpga_readb(opaque, addr + 1) << 16;
    ret |= ref405ep_fpga_readb(opaque, addr + 2) << 8;
    ret |= ref405ep_fpga_readb(opaque, addr + 3);

    return ret;
}

static void ref405ep_fpga_writel (void *opaque,
                                  hwaddr addr, uint32_t value)
{
    ref405ep_fpga_writeb(opaque, addr, (value >> 24) & 0xFF);
    ref405ep_fpga_writeb(opaque, addr + 1, (value >> 16) & 0xFF);
    ref405ep_fpga_writeb(opaque, addr + 2, (value >> 8) & 0xFF);
    ref405ep_fpga_writeb(opaque, addr + 3, value & 0xFF);
}

static const MemoryRegionOps ref405ep_fpga_ops = {
    .old_mmio = {
        .read = {
            ref405ep_fpga_readb, ref405ep_fpga_readw, ref405ep_fpga_readl,
        },
        .write = {
            ref405ep_fpga_writeb, ref405ep_fpga_writew, ref405ep_fpga_writel,
        },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
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
    ram_addr_t ram_size = machine->ram_size;
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *initrd_filename = machine->initrd_filename;
    char *filename;
    ppc4xx_bd_info_t bd;
    CPUPPCState *env;
    qemu_irq *pic;
    MemoryRegion *bios;
    MemoryRegion *sram = g_new(MemoryRegion, 1);
    ram_addr_t bdloc;
    MemoryRegion *ram_memories = g_malloc(2 * sizeof(*ram_memories));
    hwaddr ram_bases[2], ram_sizes[2];
    target_ulong sram_size;
    long bios_size;
    //int phy_addr = 0;
    //static int phy_addr = 1;
    target_ulong kernel_base, initrd_base;
    long kernel_size, initrd_size;
    int linux_boot;
    int fl_idx, fl_sectors, len;
    DriveInfo *dinfo;
    MemoryRegion *sysmem = get_system_memory();

    /* XXX: fix this */
    memory_region_allocate_system_memory(&ram_memories[0], NULL, "ef405ep.ram",
                                         0x08000000);
    ram_bases[0] = 0;
    ram_sizes[0] = 0x08000000;
    memory_region_init(&ram_memories[1], NULL, "ef405ep.ram1", 0);
    ram_bases[1] = 0x00000000;
    ram_sizes[1] = 0x00000000;
    ram_size = 128 * 1024 * 1024;
#ifdef DEBUG_BOARD_INIT
    printf("%s: register cpu\n", __func__);
#endif
    env = ppc405ep_init(sysmem, ram_memories, ram_bases, ram_sizes,
                        33333333, &pic, kernel_filename == NULL ? 0 : 1);
    /* allocate SRAM */
    sram_size = 512 * 1024;
    memory_region_init_ram(sram, NULL, "ef405ep.sram", sram_size,
                           &error_fatal);
    vmstate_register_ram_global(sram);
    memory_region_add_subregion(sysmem, 0xFFF00000, sram);
    /* allocate and load BIOS */
#ifdef DEBUG_BOARD_INIT
    printf("%s: register BIOS\n", __func__);
#endif
    fl_idx = 0;
#ifdef USE_FLASH_BIOS
    dinfo = drive_get(IF_PFLASH, 0, fl_idx);
    if (dinfo) {
        BlockBackend *blk = blk_by_legacy_dinfo(dinfo);

        bios_size = blk_getlength(blk);
        fl_sectors = (bios_size + 65535) >> 16;
#ifdef DEBUG_BOARD_INIT
        printf("Register parallel flash %d size %lx"
               " at addr %lx '%s' %d\n",
               fl_idx, bios_size, -bios_size,
               blk_name(blk), fl_sectors);
#endif
        pflash_cfi02_register((uint32_t)(-bios_size),
                              NULL, "ef405ep.bios", bios_size,
                              blk, 65536, fl_sectors, 1,
                              2, 0x0001, 0x22DA, 0x0000, 0x0000, 0x555, 0x2AA,
                              1);
        fl_idx++;
    } else
#endif
    {
#ifdef DEBUG_BOARD_INIT
        printf("Load BIOS from file\n");
#endif
        bios = g_new(MemoryRegion, 1);
        memory_region_init_ram(bios, NULL, "ef405ep.bios", BIOS_SIZE,
                               &error_fatal);
        vmstate_register_ram_global(bios);

        if (bios_name == NULL)
            bios_name = BIOS_FILENAME;
        filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
        if (filename) {
            bios_size = load_image(filename, memory_region_get_ram_ptr(bios));
            g_free(filename);
            if (bios_size < 0 || bios_size > BIOS_SIZE) {
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
        memory_region_set_readonly(bios, true);
    }
    /* Register FPGA */
#ifdef DEBUG_BOARD_INIT
    printf("%s: register FPGA\n", __func__);
#endif
    ref405ep_fpga_init(sysmem, 0xF0300000);
    /* Register NVRAM */
#ifdef DEBUG_BOARD_INIT
    printf("%s: register NVRAM\n", __func__);
#endif
    m48t59_init(NULL, 0xF0000000, 0, 8192, 1968, 8);
    /* Load kernel */
    linux_boot = (kernel_filename != NULL);
    if (linux_boot) {
#ifdef DEBUG_BOARD_INIT
        printf("%s: load kernel\n", __func__);
#endif
        memset(&bd, 0, sizeof(bd));
        bd.bi_memstart = 0x00000000;
        bd.bi_memsize = ram_size;
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
                                          ram_size - kernel_base);
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }
        printf("Load kernel size %ld at " TARGET_FMT_lx,
               kernel_size, kernel_base);
        /* load initrd */
        if (initrd_filename) {
            initrd_base = INITRD_LOAD_ADDR;
            initrd_size = load_image_targphys(initrd_filename, initrd_base,
                                              ram_size - initrd_base);
            if (initrd_size < 0) {
                fprintf(stderr, "qemu: could not load initial ram disk '%s'\n",
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
#ifdef DEBUG_BOARD_INIT
    printf("bdloc " RAM_ADDR_FMT "\n", bdloc);
    printf("%s: Done\n", __func__);
#endif
}

static void ref405ep_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "ref405ep";
    mc->init = ref405ep_init;
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
    ram_addr_t ram_size = machine->ram_size;
    const char *kernel_filename = machine->kernel_filename;
    const char *initrd_filename = machine->initrd_filename;
    char *filename;
    qemu_irq *pic;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *bios;
    MemoryRegion *ram_memories = g_malloc(2 * sizeof(*ram_memories));
    MemoryRegion *ram = g_malloc0(sizeof(*ram));
    hwaddr ram_bases[2], ram_sizes[2];
    long bios_size;
    target_ulong kernel_base, initrd_base;
    long kernel_size, initrd_size;
    int linux_boot;
    int fl_idx, fl_sectors;
    DriveInfo *dinfo;

    /* RAM is soldered to the board so the size cannot be changed */
    ram_size = 0x08000000;
    memory_region_allocate_system_memory(ram, NULL, "taihu_405ep.ram",
                                         ram_size);

    ram_bases[0] = 0;
    ram_sizes[0] = 0x04000000;
    memory_region_init_alias(&ram_memories[0], NULL,
                             "taihu_405ep.ram-0", ram, ram_bases[0],
                             ram_sizes[0]);
    ram_bases[1] = 0x04000000;
    ram_sizes[1] = 0x04000000;
    memory_region_init_alias(&ram_memories[1], NULL,
                             "taihu_405ep.ram-1", ram, ram_bases[1],
                             ram_sizes[1]);
#ifdef DEBUG_BOARD_INIT
    printf("%s: register cpu\n", __func__);
#endif
    ppc405ep_init(sysmem, ram_memories, ram_bases, ram_sizes,
                  33333333, &pic, kernel_filename == NULL ? 0 : 1);
    /* allocate and load BIOS */
#ifdef DEBUG_BOARD_INIT
    printf("%s: register BIOS\n", __func__);
#endif
    fl_idx = 0;
#if defined(USE_FLASH_BIOS)
    dinfo = drive_get(IF_PFLASH, 0, fl_idx);
    if (dinfo) {
        BlockBackend *blk = blk_by_legacy_dinfo(dinfo);

        bios_size = blk_getlength(blk);
        /* XXX: should check that size is 2MB */
        //        bios_size = 2 * 1024 * 1024;
        fl_sectors = (bios_size + 65535) >> 16;
#ifdef DEBUG_BOARD_INIT
        printf("Register parallel flash %d size %lx"
               " at addr %lx '%s' %d\n",
               fl_idx, bios_size, -bios_size,
               blk_name(blk), fl_sectors);
#endif
        pflash_cfi02_register((uint32_t)(-bios_size),
                              NULL, "taihu_405ep.bios", bios_size,
                              blk, 65536, fl_sectors, 1,
                              4, 0x0001, 0x22DA, 0x0000, 0x0000, 0x555, 0x2AA,
                              1);
        fl_idx++;
    } else
#endif
    {
#ifdef DEBUG_BOARD_INIT
        printf("Load BIOS from file\n");
#endif
        if (bios_name == NULL)
            bios_name = BIOS_FILENAME;
        bios = g_new(MemoryRegion, 1);
        memory_region_init_ram(bios, NULL, "taihu_405ep.bios", BIOS_SIZE,
                               &error_fatal);
        vmstate_register_ram_global(bios);
        filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
        if (filename) {
            bios_size = load_image(filename, memory_region_get_ram_ptr(bios));
            g_free(filename);
            if (bios_size < 0 || bios_size > BIOS_SIZE) {
                error_report("Could not load PowerPC BIOS '%s'", bios_name);
                exit(1);
            }
            bios_size = (bios_size + 0xfff) & ~0xfff;
            memory_region_add_subregion(sysmem, (uint32_t)(-bios_size), bios);
        } else if (!qtest_enabled()) {
            error_report("Could not load PowerPC BIOS '%s'", bios_name);
            exit(1);
        }
        memory_region_set_readonly(bios, true);
    }
    /* Register Linux flash */
    dinfo = drive_get(IF_PFLASH, 0, fl_idx);
    if (dinfo) {
        BlockBackend *blk = blk_by_legacy_dinfo(dinfo);

        bios_size = blk_getlength(blk);
        /* XXX: should check that size is 32MB */
        bios_size = 32 * 1024 * 1024;
        fl_sectors = (bios_size + 65535) >> 16;
#ifdef DEBUG_BOARD_INIT
        printf("Register parallel flash %d size %lx"
               " at addr " TARGET_FMT_lx " '%s'\n",
               fl_idx, bios_size, (target_ulong)0xfc000000,
               blk_name(blk));
#endif
        pflash_cfi02_register(0xfc000000, NULL, "taihu_405ep.flash", bios_size,
                              blk, 65536, fl_sectors, 1,
                              4, 0x0001, 0x22DA, 0x0000, 0x0000, 0x555, 0x2AA,
                              1);
        fl_idx++;
    }
    /* Register CLPD & LCD display */
#ifdef DEBUG_BOARD_INIT
    printf("%s: register CPLD\n", __func__);
#endif
    taihu_cpld_init(sysmem, 0x50100000);
    /* Load kernel */
    linux_boot = (kernel_filename != NULL);
    if (linux_boot) {
#ifdef DEBUG_BOARD_INIT
        printf("%s: load kernel\n", __func__);
#endif
        kernel_base = KERNEL_LOAD_ADDR;
        /* now we can load the kernel */
        kernel_size = load_image_targphys(kernel_filename, kernel_base,
                                          ram_size - kernel_base);
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }
        /* load initrd */
        if (initrd_filename) {
            initrd_base = INITRD_LOAD_ADDR;
            initrd_size = load_image_targphys(initrd_filename, initrd_base,
                                              ram_size - initrd_base);
            if (initrd_size < 0) {
                fprintf(stderr,
                        "qemu: could not load initial ram disk '%s'\n",
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
#ifdef DEBUG_BOARD_INIT
    printf("%s: Done\n", __func__);
#endif
}

static void taihu_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "taihu";
    mc->init = taihu_405ep_init;
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

machine_init(ppc405_machine_init)
