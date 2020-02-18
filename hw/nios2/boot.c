/*
 * Nios2 kernel loader
 *
 * Copyright (c) 2016 Marek Vasut <marek.vasut@gmail.com>
 *
 * Based on microblaze kernel loader
 *
 * Copyright (c) 2012 Peter Crosthwaite <peter.crosthwaite@petalogix.com>
 * Copyright (c) 2012 PetaLogix
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

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu-common.h"
#include "cpu.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "sysemu/device_tree.h"
#include "sysemu/reset.h"
#include "sysemu/sysemu.h"
#include "hw/loader.h"
#include "elf.h"

#include "boot.h"

#define NIOS2_MAGIC    0x534f494e

static struct nios2_boot_info {
    void (*machine_cpu_reset)(Nios2CPU *);
    uint32_t bootstrap_pc;
    uint32_t cmdline;
    uint32_t initrd_start;
    uint32_t initrd_end;
    uint32_t fdt;
} boot_info;

static void main_cpu_reset(void *opaque)
{
    Nios2CPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    CPUNios2State *env = &cpu->env;

    cpu_reset(CPU(cpu));

    env->regs[R_ARG0] = NIOS2_MAGIC;
    env->regs[R_ARG1] = boot_info.initrd_start;
    env->regs[R_ARG2] = boot_info.fdt;
    env->regs[R_ARG3] = boot_info.cmdline;

    cpu_set_pc(cs, boot_info.bootstrap_pc);
    if (boot_info.machine_cpu_reset) {
        boot_info.machine_cpu_reset(cpu);
    }
}

static uint64_t translate_kernel_address(void *opaque, uint64_t addr)
{
    return addr - 0xc0000000LL;
}

static int nios2_load_dtb(struct nios2_boot_info bi, const uint32_t ramsize,
                          const char *kernel_cmdline, const char *dtb_filename)
{
    int fdt_size;
    void *fdt = NULL;
    int r;

    if (dtb_filename) {
        fdt = load_device_tree(dtb_filename, &fdt_size);
    }
    if (!fdt) {
        return 0;
    }

    if (kernel_cmdline) {
        r = qemu_fdt_setprop_string(fdt, "/chosen", "bootargs",
                                    kernel_cmdline);
        if (r < 0) {
            fprintf(stderr, "couldn't set /chosen/bootargs\n");
        }
    }

    if (bi.initrd_start) {
        qemu_fdt_setprop_cell(fdt, "/chosen", "linux,initrd-start",
                              translate_kernel_address(NULL, bi.initrd_start));

        qemu_fdt_setprop_cell(fdt, "/chosen", "linux,initrd-end",
                              translate_kernel_address(NULL, bi.initrd_end));
    }

    cpu_physical_memory_write(bi.fdt, fdt, fdt_size);
    g_free(fdt);
    return fdt_size;
}

void nios2_load_kernel(Nios2CPU *cpu, hwaddr ddr_base,
                            uint32_t ramsize,
                            const char *initrd_filename,
                            const char *dtb_filename,
                            void (*machine_cpu_reset)(Nios2CPU *))
{
    QemuOpts *machine_opts;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *dtb_arg;
    char *filename = NULL;

    machine_opts = qemu_get_machine_opts();
    kernel_filename = qemu_opt_get(machine_opts, "kernel");
    kernel_cmdline = qemu_opt_get(machine_opts, "append");
    dtb_arg = qemu_opt_get(machine_opts, "dtb");
    /* default to pcbios dtb as passed by machine_init */
    if (!dtb_arg) {
        filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, dtb_filename);
    }

    boot_info.machine_cpu_reset = machine_cpu_reset;
    qemu_register_reset(main_cpu_reset, cpu);

    if (kernel_filename) {
        int kernel_size, fdt_size;
        uint64_t entry, low, high;
        int big_endian = 0;

#ifdef TARGET_WORDS_BIGENDIAN
        big_endian = 1;
#endif

        /* Boots a kernel elf binary. */
        kernel_size = load_elf(kernel_filename, NULL, NULL, NULL,
                               &entry, &low, &high, NULL,
                               big_endian, EM_ALTERA_NIOS2, 0, 0);
        if ((uint32_t)entry == 0xc0000000) {
            /*
             * The Nios II processor reference guide documents that the
             * kernel is placed at virtual memory address 0xc0000000,
             * and we've got something that points there.  Reload it
             * and adjust the entry to get the address in physical RAM.
             */
            kernel_size = load_elf(kernel_filename, NULL,
                                   translate_kernel_address, NULL,
                                   &entry, NULL, NULL, NULL,
                                   big_endian, EM_ALTERA_NIOS2, 0, 0);
            boot_info.bootstrap_pc = ddr_base + 0xc0000000 +
                (entry & 0x07ffffff);
        } else {
            /* Use the entry point in the ELF image.  */
            boot_info.bootstrap_pc = (uint32_t)entry;
        }

        /* If it wasn't an ELF image, try an u-boot image. */
        if (kernel_size < 0) {
            hwaddr uentry, loadaddr = LOAD_UIMAGE_LOADADDR_INVALID;

            kernel_size = load_uimage(kernel_filename, &uentry, &loadaddr, 0,
                                      NULL, NULL);
            boot_info.bootstrap_pc = uentry;
            high = loadaddr + kernel_size;
        }

        /* Not an ELF image nor an u-boot image, try a RAW image. */
        if (kernel_size < 0) {
            kernel_size = load_image_targphys(kernel_filename, ddr_base,
                                              ram_size);
            boot_info.bootstrap_pc = ddr_base;
            high = ddr_base + kernel_size;
        }

        high = ROUND_UP(high, 1 * MiB);

        /* If initrd is available, it goes after the kernel, aligned to 1M. */
        if (initrd_filename) {
            int initrd_size;
            uint32_t initrd_offset;

            boot_info.initrd_start = high;
            initrd_offset = boot_info.initrd_start - ddr_base;

            initrd_size = load_ramdisk(initrd_filename,
                                       boot_info.initrd_start,
                                       ram_size - initrd_offset);
            if (initrd_size < 0) {
                initrd_size = load_image_targphys(initrd_filename,
                                                  boot_info.initrd_start,
                                                  ram_size - initrd_offset);
            }
            if (initrd_size < 0) {
                error_report("could not load initrd '%s'",
                             initrd_filename);
                exit(EXIT_FAILURE);
            }
            high += initrd_size;
        }
        high = ROUND_UP(high, 4);
        boot_info.initrd_end = high;

        /* Device tree must be placed right after initrd (if available) */
        boot_info.fdt = high;
        fdt_size = nios2_load_dtb(boot_info, ram_size, kernel_cmdline,
                                  /* Preference a -dtb argument */
                                  dtb_arg ? dtb_arg : filename);
        high += fdt_size;

        /* Kernel command is at the end, 4k aligned. */
        boot_info.cmdline = ROUND_UP(high, 4 * KiB);
        if (kernel_cmdline && strlen(kernel_cmdline)) {
            pstrcpy_targphys("cmdline", boot_info.cmdline, 256, kernel_cmdline);
        }
    }
    g_free(filename);
}
