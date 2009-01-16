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
#include "hw.h"
#include "ppc.h"
#include "ppc405.h"
#include "nvram.h"
#include "flash.h"
#include "sysemu.h"
#include "block.h"
#include "boards.h"
#include "qemu-log.h"

#define BIOS_FILENAME "ppc405_rom.bin"
#undef BIOS_SIZE
#define BIOS_SIZE (2048 * 1024)

#define KERNEL_LOAD_ADDR 0x00000000
#define INITRD_LOAD_ADDR 0x01800000

#define USE_FLASH_BIOS

#define DEBUG_BOARD_INIT

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

static uint32_t ref405ep_fpga_readb (void *opaque, target_phys_addr_t addr)
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
                                  target_phys_addr_t addr, uint32_t value)
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

static uint32_t ref405ep_fpga_readw (void *opaque, target_phys_addr_t addr)
{
    uint32_t ret;

    ret = ref405ep_fpga_readb(opaque, addr) << 8;
    ret |= ref405ep_fpga_readb(opaque, addr + 1);

    return ret;
}

static void ref405ep_fpga_writew (void *opaque,
                                  target_phys_addr_t addr, uint32_t value)
{
    ref405ep_fpga_writeb(opaque, addr, (value >> 8) & 0xFF);
    ref405ep_fpga_writeb(opaque, addr + 1, value & 0xFF);
}

static uint32_t ref405ep_fpga_readl (void *opaque, target_phys_addr_t addr)
{
    uint32_t ret;

    ret = ref405ep_fpga_readb(opaque, addr) << 24;
    ret |= ref405ep_fpga_readb(opaque, addr + 1) << 16;
    ret |= ref405ep_fpga_readb(opaque, addr + 2) << 8;
    ret |= ref405ep_fpga_readb(opaque, addr + 3);

    return ret;
}

static void ref405ep_fpga_writel (void *opaque,
                                  target_phys_addr_t addr, uint32_t value)
{
    ref405ep_fpga_writeb(opaque, addr, (value >> 24) & 0xFF);
    ref405ep_fpga_writeb(opaque, addr + 1, (value >> 16) & 0xFF);
    ref405ep_fpga_writeb(opaque, addr + 2, (value >> 8) & 0xFF);
    ref405ep_fpga_writeb(opaque, addr + 3, value & 0xFF);
}

static CPUReadMemoryFunc *ref405ep_fpga_read[] = {
    &ref405ep_fpga_readb,
    &ref405ep_fpga_readw,
    &ref405ep_fpga_readl,
};

static CPUWriteMemoryFunc *ref405ep_fpga_write[] = {
    &ref405ep_fpga_writeb,
    &ref405ep_fpga_writew,
    &ref405ep_fpga_writel,
};

static void ref405ep_fpga_reset (void *opaque)
{
    ref405ep_fpga_t *fpga;

    fpga = opaque;
    fpga->reg0 = 0x00;
    fpga->reg1 = 0x0F;
}

static void ref405ep_fpga_init (uint32_t base)
{
    ref405ep_fpga_t *fpga;
    int fpga_memory;

    fpga = qemu_mallocz(sizeof(ref405ep_fpga_t));
    if (fpga != NULL) {
        fpga_memory = cpu_register_io_memory(0, ref405ep_fpga_read,
                                             ref405ep_fpga_write, fpga);
        cpu_register_physical_memory(base, 0x00000100, fpga_memory);
        ref405ep_fpga_reset(fpga);
        qemu_register_reset(&ref405ep_fpga_reset, fpga);
    }
}

static void ref405ep_init (ram_addr_t ram_size, int vga_ram_size,
                           const char *boot_device,
                           const char *kernel_filename,
                           const char *kernel_cmdline,
                           const char *initrd_filename,
                           const char *cpu_model)
{
    char buf[1024];
    ppc4xx_bd_info_t bd;
    CPUPPCState *env;
    qemu_irq *pic;
    ram_addr_t sram_offset, bios_offset, bdloc;
    target_phys_addr_t ram_bases[2], ram_sizes[2];
    target_ulong sram_size, bios_size;
    //int phy_addr = 0;
    //static int phy_addr = 1;
    target_ulong kernel_base, kernel_size, initrd_base, initrd_size;
    int linux_boot;
    int fl_idx, fl_sectors, len;
    int ppc_boot_device = boot_device[0];
    int index;

    /* XXX: fix this */
    ram_bases[0] = 0x00000000;
    ram_sizes[0] = 0x08000000;
    ram_bases[1] = 0x00000000;
    ram_sizes[1] = 0x00000000;
    ram_size = 128 * 1024 * 1024;
#ifdef DEBUG_BOARD_INIT
    printf("%s: register cpu\n", __func__);
#endif
    env = ppc405ep_init(ram_bases, ram_sizes, 33333333, &pic, &sram_offset,
                        kernel_filename == NULL ? 0 : 1);
    /* allocate SRAM */
#ifdef DEBUG_BOARD_INIT
    printf("%s: register SRAM at offset %08lx\n", __func__, sram_offset);
#endif
    sram_size = 512 * 1024;
    cpu_register_physical_memory(0xFFF00000, sram_size,
                                 sram_offset | IO_MEM_RAM);
    /* allocate and load BIOS */
#ifdef DEBUG_BOARD_INIT
    printf("%s: register BIOS\n", __func__);
#endif
    bios_offset = sram_offset + sram_size;
    fl_idx = 0;
#ifdef USE_FLASH_BIOS
    index = drive_get_index(IF_PFLASH, 0, fl_idx);
    if (index != -1) {
        bios_size = bdrv_getlength(drives_table[index].bdrv);
        fl_sectors = (bios_size + 65535) >> 16;
#ifdef DEBUG_BOARD_INIT
        printf("Register parallel flash %d size " ADDRX " at offset %08lx "
               " addr " ADDRX " '%s' %d\n",
               fl_idx, bios_size, bios_offset, -bios_size,
               bdrv_get_device_name(drives_table[index].bdrv), fl_sectors);
#endif
        pflash_cfi02_register((uint32_t)(-bios_size), bios_offset,
                              drives_table[index].bdrv, 65536, fl_sectors, 1,
                              2, 0x0001, 0x22DA, 0x0000, 0x0000, 0x555, 0x2AA);
        fl_idx++;
    } else
#endif
    {
#ifdef DEBUG_BOARD_INIT
        printf("Load BIOS from file\n");
#endif
        if (bios_name == NULL)
            bios_name = BIOS_FILENAME;
        snprintf(buf, sizeof(buf), "%s/%s", bios_dir, bios_name);
        bios_size = load_image(buf, phys_ram_base + bios_offset);
        if (bios_size < 0 || bios_size > BIOS_SIZE) {
            fprintf(stderr, "qemu: could not load PowerPC bios '%s'\n", buf);
            exit(1);
        }
        bios_size = (bios_size + 0xfff) & ~0xfff;
        cpu_register_physical_memory((uint32_t)(-bios_size),
                                     bios_size, bios_offset | IO_MEM_ROM);
    }
    bios_offset += bios_size;
    /* Register FPGA */
#ifdef DEBUG_BOARD_INIT
    printf("%s: register FPGA\n", __func__);
#endif
    ref405ep_fpga_init(0xF0300000);
    /* Register NVRAM */
#ifdef DEBUG_BOARD_INIT
    printf("%s: register NVRAM\n", __func__);
#endif
    m48t59_init(NULL, 0xF0000000, 0, 8192, 8);
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
        kernel_size = load_image(kernel_filename, phys_ram_base + kernel_base);
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }
        printf("Load kernel size " TARGET_FMT_ld " at " TARGET_FMT_lx
               " %02x %02x %02x %02x\n", kernel_size, kernel_base,
               *(char *)(phys_ram_base + kernel_base),
               *(char *)(phys_ram_base + kernel_base + 1),
               *(char *)(phys_ram_base + kernel_base + 2),
               *(char *)(phys_ram_base + kernel_base + 3));
        /* load initrd */
        if (initrd_filename) {
            initrd_base = INITRD_LOAD_ADDR;
            initrd_size = load_image(initrd_filename,
                                     phys_ram_base + initrd_base);
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
        ppc_boot_device = 'm';
        if (kernel_cmdline != NULL) {
            len = strlen(kernel_cmdline);
            bdloc -= ((len + 255) & ~255);
            memcpy(phys_ram_base + bdloc, kernel_cmdline, len + 1);
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
    printf("%s: Done\n", __func__);
#endif
    printf("bdloc %016lx %s\n",
           (unsigned long)bdloc, (char *)(phys_ram_base + bdloc));
}

QEMUMachine ref405ep_machine = {
    .name = "ref405ep",
    .desc = "ref405ep",
    .init = ref405ep_init,
    .ram_require = (128 * 1024 * 1024 + 4096 + 512 * 1024 + BIOS_SIZE) | RAMSIZE_FIXED,
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

static uint32_t taihu_cpld_readb (void *opaque, target_phys_addr_t addr)
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

static void taihu_cpld_writeb (void *opaque,
                               target_phys_addr_t addr, uint32_t value)
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

static uint32_t taihu_cpld_readw (void *opaque, target_phys_addr_t addr)
{
    uint32_t ret;

    ret = taihu_cpld_readb(opaque, addr) << 8;
    ret |= taihu_cpld_readb(opaque, addr + 1);

    return ret;
}

static void taihu_cpld_writew (void *opaque,
                               target_phys_addr_t addr, uint32_t value)
{
    taihu_cpld_writeb(opaque, addr, (value >> 8) & 0xFF);
    taihu_cpld_writeb(opaque, addr + 1, value & 0xFF);
}

static uint32_t taihu_cpld_readl (void *opaque, target_phys_addr_t addr)
{
    uint32_t ret;

    ret = taihu_cpld_readb(opaque, addr) << 24;
    ret |= taihu_cpld_readb(opaque, addr + 1) << 16;
    ret |= taihu_cpld_readb(opaque, addr + 2) << 8;
    ret |= taihu_cpld_readb(opaque, addr + 3);

    return ret;
}

static void taihu_cpld_writel (void *opaque,
                               target_phys_addr_t addr, uint32_t value)
{
    taihu_cpld_writel(opaque, addr, (value >> 24) & 0xFF);
    taihu_cpld_writel(opaque, addr + 1, (value >> 16) & 0xFF);
    taihu_cpld_writel(opaque, addr + 2, (value >> 8) & 0xFF);
    taihu_cpld_writeb(opaque, addr + 3, value & 0xFF);
}

static CPUReadMemoryFunc *taihu_cpld_read[] = {
    &taihu_cpld_readb,
    &taihu_cpld_readw,
    &taihu_cpld_readl,
};

static CPUWriteMemoryFunc *taihu_cpld_write[] = {
    &taihu_cpld_writeb,
    &taihu_cpld_writew,
    &taihu_cpld_writel,
};

static void taihu_cpld_reset (void *opaque)
{
    taihu_cpld_t *cpld;

    cpld = opaque;
    cpld->reg0 = 0x01;
    cpld->reg1 = 0x80;
}

static void taihu_cpld_init (uint32_t base)
{
    taihu_cpld_t *cpld;
    int cpld_memory;

    cpld = qemu_mallocz(sizeof(taihu_cpld_t));
    if (cpld != NULL) {
        cpld_memory = cpu_register_io_memory(0, taihu_cpld_read,
                                             taihu_cpld_write, cpld);
        cpu_register_physical_memory(base, 0x00000100, cpld_memory);
        taihu_cpld_reset(cpld);
        qemu_register_reset(&taihu_cpld_reset, cpld);
    }
}

static void taihu_405ep_init(ram_addr_t ram_size, int vga_ram_size,
                             const char *boot_device,
                             const char *kernel_filename,
                             const char *kernel_cmdline,
                             const char *initrd_filename,
                             const char *cpu_model)
{
    char buf[1024];
    CPUPPCState *env;
    qemu_irq *pic;
    ram_addr_t bios_offset;
    target_phys_addr_t ram_bases[2], ram_sizes[2];
    target_ulong bios_size;
    target_ulong kernel_base, kernel_size, initrd_base, initrd_size;
    int linux_boot;
    int fl_idx, fl_sectors;
    int ppc_boot_device = boot_device[0];
    int index;

    /* RAM is soldered to the board so the size cannot be changed */
    ram_bases[0] = 0x00000000;
    ram_sizes[0] = 0x04000000;
    ram_bases[1] = 0x04000000;
    ram_sizes[1] = 0x04000000;
#ifdef DEBUG_BOARD_INIT
    printf("%s: register cpu\n", __func__);
#endif
    env = ppc405ep_init(ram_bases, ram_sizes, 33333333, &pic, &bios_offset,
                        kernel_filename == NULL ? 0 : 1);
    /* allocate and load BIOS */
#ifdef DEBUG_BOARD_INIT
    printf("%s: register BIOS\n", __func__);
#endif
    fl_idx = 0;
#if defined(USE_FLASH_BIOS)
    index = drive_get_index(IF_PFLASH, 0, fl_idx);
    if (index != -1) {
        bios_size = bdrv_getlength(drives_table[index].bdrv);
        /* XXX: should check that size is 2MB */
        //        bios_size = 2 * 1024 * 1024;
        fl_sectors = (bios_size + 65535) >> 16;
#ifdef DEBUG_BOARD_INIT
        printf("Register parallel flash %d size " ADDRX " at offset %08lx "
               " addr " ADDRX " '%s' %d\n",
               fl_idx, bios_size, bios_offset, -bios_size,
               bdrv_get_device_name(drives_table[index].bdrv), fl_sectors);
#endif
        pflash_cfi02_register((uint32_t)(-bios_size), bios_offset,
                              drives_table[index].bdrv, 65536, fl_sectors, 1,
                              4, 0x0001, 0x22DA, 0x0000, 0x0000, 0x555, 0x2AA);
        fl_idx++;
    } else
#endif
    {
#ifdef DEBUG_BOARD_INIT
        printf("Load BIOS from file\n");
#endif
        if (bios_name == NULL)
            bios_name = BIOS_FILENAME;
        snprintf(buf, sizeof(buf), "%s/%s", bios_dir, bios_name);
        bios_size = load_image(buf, phys_ram_base + bios_offset);
        if (bios_size < 0 || bios_size > BIOS_SIZE) {
            fprintf(stderr, "qemu: could not load PowerPC bios '%s'\n", buf);
            exit(1);
        }
        bios_size = (bios_size + 0xfff) & ~0xfff;
        cpu_register_physical_memory((uint32_t)(-bios_size),
                                     bios_size, bios_offset | IO_MEM_ROM);
    }
    bios_offset += bios_size;
    /* Register Linux flash */
    index = drive_get_index(IF_PFLASH, 0, fl_idx);
    if (index != -1) {
        bios_size = bdrv_getlength(drives_table[index].bdrv);
        /* XXX: should check that size is 32MB */
        bios_size = 32 * 1024 * 1024;
        fl_sectors = (bios_size + 65535) >> 16;
#ifdef DEBUG_BOARD_INIT
        printf("Register parallel flash %d size " ADDRX " at offset %08lx "
               " addr " ADDRX " '%s'\n",
               fl_idx, bios_size, bios_offset, (target_ulong)0xfc000000,
               bdrv_get_device_name(drives_table[index].bdrv));
#endif
        pflash_cfi02_register(0xfc000000, bios_offset,
                              drives_table[index].bdrv, 65536, fl_sectors, 1,
                              4, 0x0001, 0x22DA, 0x0000, 0x0000, 0x555, 0x2AA);
        fl_idx++;
    }
    /* Register CLPD & LCD display */
#ifdef DEBUG_BOARD_INIT
    printf("%s: register CPLD\n", __func__);
#endif
    taihu_cpld_init(0x50100000);
    /* Load kernel */
    linux_boot = (kernel_filename != NULL);
    if (linux_boot) {
#ifdef DEBUG_BOARD_INIT
        printf("%s: load kernel\n", __func__);
#endif
        kernel_base = KERNEL_LOAD_ADDR;
        /* now we can load the kernel */
        kernel_size = load_image(kernel_filename, phys_ram_base + kernel_base);
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }
        /* load initrd */
        if (initrd_filename) {
            initrd_base = INITRD_LOAD_ADDR;
            initrd_size = load_image(initrd_filename,
                                     phys_ram_base + initrd_base);
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
        ppc_boot_device = 'm';
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

QEMUMachine taihu_machine = {
    "taihu",
    "taihu",
    taihu_405ep_init,
    (128 * 1024 * 1024 + 4096 + BIOS_SIZE + 32 * 1024 * 1024) | RAMSIZE_FIXED,
};
