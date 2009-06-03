/*
 * Model of Petalogix linux reference design targeting Xilinx Spartan 3ADSP-1800
 * boards.
 *
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

#define LMB_BRAM_SIZE  (128 * 1024)
#define FLASH_SIZE     (16 * 1024 * 1024)

static uint32_t bootstrap_pc;

static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    cpu_reset(env);
    env->sregs[SR_PC] = bootstrap_pc;
}

#define BINARY_DEVICE_TREE_FILE "petalogix-s3adsp1800.dtb"
static int petalogix_load_device_tree(target_phys_addr_t addr,
                                      uint32_t ramsize,
                                      target_phys_addr_t initrd_base,
                                      target_phys_addr_t initrd_size,
                                      const char *kernel_cmdline)
{
#ifdef HAVE_FDT
    void *fdt;
    int r;
#endif
    char *path;
    int fdt_size;

#ifdef HAVE_FDT
    /* Try the local "mb.dtb" override.  */
    fdt = load_device_tree("mb.dtb", &fdt_size);
    if (!fdt) {
        path = qemu_find_file(QEMU_FILE_TYPE_BIOS, BINARY_DEVICE_TREE_FILE);
        if (path) {
            fdt = load_device_tree(path, &fdt_size);
            qemu_free(path);
        }
        if (!fdt)
            return 0;
    }

    r = qemu_devtree_setprop_string(fdt, "/chosen", "bootargs", kernel_cmdline);
    if (r < 0)
        fprintf(stderr, "couldn't set /chosen/bootargs\n");
    cpu_physical_memory_write (addr, (void *)fdt, fdt_size);
#else
    /* We lack libfdt so we cannot manipulate the fdt. Just pass on the blob
       to the kernel.  */
    fdt_size = load_image_targphys("mb.dtb", addr, 0x10000);
    if (fdt_size < 0) {
        path = qemu_find_file(QEMU_FILE_TYPE_BIOS, BINARY_DEVICE_TREE_FILE);
        if (path) {
            fdt_size = load_image_targphys(path, addr, 0x10000);
	    qemu_free(path);
        }
    }

    if (kernel_cmdline) {
        fprintf(stderr,
                "Warning: missing libfdt, cannot pass cmdline to kernel!\n");
    }
#endif
    return fdt_size;
}

static void
petalogix_s3adsp1800_init(ram_addr_t ram_size,
                          const char *boot_device,
                          const char *kernel_filename,
                          const char *kernel_cmdline,
                          const char *initrd_filename, const char *cpu_model)
{
    DeviceState *dev;
    CPUState *env;
    int kernel_size;
    int i;
    target_phys_addr_t ddr_base = 0x90000000;
    ram_addr_t phys_lmb_bram;
    ram_addr_t phys_ram;
    ram_addr_t phys_flash;
    qemu_irq irq[32], *cpu_irq;

    /* init CPUs */
    if (cpu_model == NULL) {
        cpu_model = "microblaze";
    }
    env = cpu_init(cpu_model);

    env->pvr.regs[10] = 0x0c000000; /* spartan 3a dsp family.  */
    qemu_register_reset(main_cpu_reset, 0, env);

    /* Attach emulated BRAM through the LMB.  */
    phys_lmb_bram = qemu_ram_alloc(LMB_BRAM_SIZE);
    cpu_register_physical_memory(0x00000000, LMB_BRAM_SIZE,
                                 phys_lmb_bram | IO_MEM_RAM);

    phys_ram = qemu_ram_alloc(ram_size);
    cpu_register_physical_memory(ddr_base, ram_size, phys_ram | IO_MEM_RAM);

    phys_flash = qemu_ram_alloc(FLASH_SIZE);
    i = drive_get_index(IF_PFLASH, 0, 0);
    pflash_cfi02_register(0xa0000000, phys_flash,
                          i != -1 ? drives_table[i].bdrv : NULL, (64 * 1024),
                          FLASH_SIZE >> 16,
                          1, 1, 0x0000, 0x0000, 0x0000, 0x0000,
                          0x555, 0x2aa);

    cpu_irq = microblaze_pic_init_cpu(env);
    dev = xilinx_intc_create(0x81800000, cpu_irq[0], 2);
    for (i = 0; i < 32; i++) {
        irq[i] = qdev_get_gpio_in(dev, i);
    }

    sysbus_create_simple("xilinx,uartlite", 0x84000000, irq[3]);
    /* 2 timers at irq 2 @ 62 Mhz.  */
    xilinx_timer_create(0x83c00000, irq[0], 2, 62 * 1000000);
    xilinx_ethlite_create(&nd_table[0], 0x81000000, irq[1], 0, 0);

    if (kernel_filename) {
        uint64_t entry, low, high;
        int kcmdline_len;
        uint32_t base32;

        /* Boots a kernel elf binary.  */
        kernel_size = load_elf(kernel_filename, 0,
                               &entry, &low, &high);
        base32 = entry;
        if (base32 == 0xc0000000) {
            kernel_size = load_elf(kernel_filename, -0x30000000LL,
                                   &entry, NULL, NULL);
        }
        /* Always boot into physical ram.  */
        bootstrap_pc = ddr_base + (entry & 0x0fffffff);
        if (kernel_size < 0) {
            /* If we failed loading ELF's try a raw image.  */
            kernel_size = load_image_targphys(kernel_filename, ddr_base,
                                              ram_size);
            bootstrap_pc = ddr_base;
        }

        env->regs[5] = ddr_base + kernel_size;
        if (kernel_cmdline && (kcmdline_len = strlen(kernel_cmdline))) {
            pstrcpy_targphys(env->regs[5], 256, kernel_cmdline);
        }
        env->regs[6] = 0;
        /* Provide a device-tree.  */
        env->regs[7] = ddr_base + kernel_size + 256;
        petalogix_load_device_tree(env->regs[7], ram_size,
                                   env->regs[6], 0,
                                   kernel_cmdline);
    }

    env->sregs[SR_PC] = bootstrap_pc;
}

static QEMUMachine petalogix_s3adsp1800_machine = {
    .name = "petalogix-s3adsp1800",
    .desc = "Petalogix linux refdesign for xilinx Spartan 3ADSP1800",
    .init = petalogix_s3adsp1800_init,
    .is_default = 1
};

static void petalogix_s3adsp1800_machine_init(void)
{
    qemu_register_machine(&petalogix_s3adsp1800_machine);
}

machine_init(petalogix_s3adsp1800_machine_init);
