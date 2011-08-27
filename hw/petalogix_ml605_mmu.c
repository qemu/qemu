/*
 * Model of Petalogix linux reference design targeting Xilinx Spartan ml605
 * board.
 *
 * Copyright (c) 2011 Michal Simek <monstr@monstr.eu>
 * Copyright (c) 2011 PetaLogix
 * Copyright (c) 2009 Edgar E. Iglesias.
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

#include "sysbus.h"
#include "hw.h"
#include "net.h"
#include "flash.h"
#include "sysemu.h"
#include "devices.h"
#include "boards.h"
#include "device_tree.h"
#include "xilinx.h"
#include "loader.h"
#include "elf.h"
#include "blockdev.h"
#include "pc.h"

#include "microblaze_pic_cpu.h"
#include "xilinx_axidma.h"

#define LMB_BRAM_SIZE  (128 * 1024)
#define FLASH_SIZE     (32 * 1024 * 1024)

static struct
{
    uint32_t bootstrap_pc;
    uint32_t cmdline;
    uint32_t fdt;
} boot_info;

static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;

    cpu_reset(env);
    env->regs[5] = boot_info.cmdline;
    env->regs[7] = boot_info.fdt;
    env->sregs[SR_PC] = boot_info.bootstrap_pc;
    env->pvr.regs[10] = 0x0e000000; /* virtex 6 */
    /* setup pvr to match kernel setting */
    env->pvr.regs[5] |= PVR5_DCACHE_WRITEBACK_MASK;
    env->pvr.regs[0] |= PVR0_USE_FPU_MASK | PVR0_ENDI;
    env->pvr.regs[0] = (env->pvr.regs[0] & ~PVR0_VERSION_MASK) | (0x14 << 8);
    env->pvr.regs[2] ^= PVR2_USE_FPU2_MASK;
    env->pvr.regs[4] = 0xc56b8000;
    env->pvr.regs[5] = 0xc56be000;
}

#define BINARY_DEVICE_TREE_FILE "petalogix-ml605.dtb"
static int petalogix_load_device_tree(target_phys_addr_t addr,
                                      uint32_t ramsize,
                                      target_phys_addr_t initrd_base,
                                      target_phys_addr_t initrd_size,
                                      const char *kernel_cmdline)
{
    char *path;
    int fdt_size;
#ifdef CONFIG_FDT
    void *fdt;
    int r;

    /* Try the local "mb.dtb" override.  */
    fdt = load_device_tree("mb.dtb", &fdt_size);
    if (!fdt) {
        path = qemu_find_file(QEMU_FILE_TYPE_BIOS, BINARY_DEVICE_TREE_FILE);
        if (path) {
            fdt = load_device_tree(path, &fdt_size);
            g_free(path);
        }
        if (!fdt) {
            return 0;
        }
    }

    r = qemu_devtree_setprop_string(fdt, "/chosen", "bootargs", kernel_cmdline);
    if (r < 0) {
        fprintf(stderr, "couldn't set /chosen/bootargs\n");
    }
    cpu_physical_memory_write(addr, (void *)fdt, fdt_size);
#else
    /* We lack libfdt so we cannot manipulate the fdt. Just pass on the blob
       to the kernel.  */
    fdt_size = load_image_targphys("mb.dtb", addr, 0x10000);
    if (fdt_size < 0) {
        path = qemu_find_file(QEMU_FILE_TYPE_BIOS, BINARY_DEVICE_TREE_FILE);
        if (path) {
            fdt_size = load_image_targphys(path, addr, 0x10000);
            g_free(path);
        }
    }

    if (kernel_cmdline) {
        fprintf(stderr,
                "Warning: missing libfdt, cannot pass cmdline to kernel!\n");
    }
#endif
    return fdt_size;
}

static uint64_t translate_kernel_address(void *opaque, uint64_t addr)
{
    return addr - 0x30000000LL;
}

#define MEMORY_BASEADDR 0x50000000
#define FLASH_BASEADDR 0x86000000
#define INTC_BASEADDR 0x81800000
#define TIMER_BASEADDR 0x83c00000
#define UART16550_BASEADDR 0x83e00000
#define AXIENET_BASEADDR 0x82780000
#define AXIDMA_BASEADDR 0x84600000

static void
petalogix_ml605_init(ram_addr_t ram_size,
                          const char *boot_device,
                          const char *kernel_filename,
                          const char *kernel_cmdline,
                          const char *initrd_filename, const char *cpu_model)
{
    DeviceState *dev;
    CPUState *env;
    int kernel_size;
    DriveInfo *dinfo;
    int i;
    target_phys_addr_t ddr_base = MEMORY_BASEADDR;
    ram_addr_t phys_lmb_bram;
    ram_addr_t phys_ram;
    ram_addr_t phys_flash;
    qemu_irq irq[32], *cpu_irq;

    /* init CPUs */
    if (cpu_model == NULL) {
        cpu_model = "microblaze";
    }
    env = cpu_init(cpu_model);

    qemu_register_reset(main_cpu_reset, env);

    /* Attach emulated BRAM through the LMB.  */
    phys_lmb_bram = qemu_ram_alloc(NULL, "petalogix_ml605.lmb_bram",
                                   LMB_BRAM_SIZE);
    cpu_register_physical_memory(0x00000000, LMB_BRAM_SIZE,
                                 phys_lmb_bram | IO_MEM_RAM);

    phys_ram = qemu_ram_alloc(NULL, "petalogix_ml605.ram", ram_size);
    cpu_register_physical_memory(ddr_base, ram_size, phys_ram | IO_MEM_RAM);

    phys_flash = qemu_ram_alloc(NULL, "petalogix_ml605.flash", FLASH_SIZE);
    dinfo = drive_get(IF_PFLASH, 0, 0);
    /* 5th parameter 2 means bank-width
     * 10th paremeter 0 means little-endian */
    pflash_cfi01_register(FLASH_BASEADDR, phys_flash,
                          dinfo ? dinfo->bdrv : NULL, (64 * 1024),
                          FLASH_SIZE >> 16,
                          2, 0x89, 0x18, 0x0000, 0x0, 0);


    cpu_irq = microblaze_pic_init_cpu(env);
    dev = xilinx_intc_create(INTC_BASEADDR, cpu_irq[0], 4);
    for (i = 0; i < 32; i++) {
        irq[i] = qdev_get_gpio_in(dev, i);
    }

    serial_mm_init(UART16550_BASEADDR + 0x1000, 2, irq[5], 115200,
                   serial_hds[0], 1, 0);

    /* 2 timers at irq 2 @ 100 Mhz.  */
    xilinx_timer_create(TIMER_BASEADDR, irq[2], 2, 100 * 1000000);

    /* axi ethernet and dma initialization. TODO: Dynamically connect them.  */
    {
        static struct XilinxDMAConnection dmach;

        xilinx_axiethernet_create(&dmach, &nd_table[0], 0x82780000,
                                  irq[3], 0x1000, 0x1000);
        xilinx_axiethernetdma_create(&dmach, 0x84600000,
                                     irq[1], irq[0], 100 * 1000000);
    }

    if (kernel_filename) {
        uint64_t entry, low, high;
        uint32_t base32;
        int big_endian = 0;

#ifdef TARGET_WORDS_BIGENDIAN
        big_endian = 1;
#endif

        /* Boots a kernel elf binary.  */
        kernel_size = load_elf(kernel_filename, NULL, NULL,
                               &entry, &low, &high,
                               big_endian, ELF_MACHINE, 0);
        base32 = entry;
        if (base32 == 0xc0000000) {
            kernel_size = load_elf(kernel_filename, translate_kernel_address,
                                   NULL, &entry, NULL, NULL,
                                   big_endian, ELF_MACHINE, 0);
        }
        /* Always boot into physical ram.  */
        boot_info.bootstrap_pc = ddr_base + (entry & 0x0fffffff);

        /* If it wasn't an ELF image, try an u-boot image.  */
        if (kernel_size < 0) {
            target_phys_addr_t uentry, loadaddr;

            kernel_size = load_uimage(kernel_filename, &uentry, &loadaddr, 0);
            boot_info.bootstrap_pc = uentry;
            high = (loadaddr + kernel_size + 3) & ~3;
        }

        /* Not an ELF image nor an u-boot image, try a RAW image.  */
        if (kernel_size < 0) {
            kernel_size = load_image_targphys(kernel_filename, ddr_base,
                                              ram_size);
            boot_info.bootstrap_pc = ddr_base;
            high = (ddr_base + kernel_size + 3) & ~3;
        }

        boot_info.cmdline = high + 4096;
        if (kernel_cmdline && strlen(kernel_cmdline)) {
            pstrcpy_targphys("cmdline", boot_info.cmdline, 256, kernel_cmdline);
        }
        /* Provide a device-tree.  */
        boot_info.fdt = boot_info.cmdline + 4096;
        petalogix_load_device_tree(boot_info.fdt, ram_size,
                                   0, 0,
                                   kernel_cmdline);
    }
}

static QEMUMachine petalogix_ml605_machine = {
    .name = "petalogix-ml605",
    .desc = "PetaLogix linux refdesign for xilinx ml605 little endian",
    .init = petalogix_ml605_init,
    .is_default = 0
};

static void petalogix_ml605_machine_init(void)
{
    qemu_register_machine(&petalogix_ml605_machine);
}

machine_init(petalogix_ml605_machine_init);
