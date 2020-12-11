/*
 * Microblaze kernel loader
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
#include "qemu-common.h"
#include "qemu/datadir.h"
#include "cpu.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "sysemu/device_tree.h"
#include "sysemu/reset.h"
#include "sysemu/sysemu.h"
#include "hw/loader.h"
#include "elf.h"
#include "qemu/cutils.h"

#include "boot.h"

static struct
{
    void (*machine_cpu_reset)(MicroBlazeCPU *);
    uint32_t bootstrap_pc;
    uint32_t cmdline;
    uint32_t initrd_start;
    uint32_t initrd_end;
    uint32_t fdt;
} boot_info;

static void main_cpu_reset(void *opaque)
{
    MicroBlazeCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    CPUMBState *env = &cpu->env;

    cpu_reset(cs);
    env->regs[5] = boot_info.cmdline;
    env->regs[6] = boot_info.initrd_start;
    env->regs[7] = boot_info.fdt;
    cpu_set_pc(cs, boot_info.bootstrap_pc);
    if (boot_info.machine_cpu_reset) {
        boot_info.machine_cpu_reset(cpu);
    }
}

static int microblaze_load_dtb(hwaddr addr,
                               uint32_t ramsize,
                               uint32_t initrd_start,
                               uint32_t initrd_end,
                               const char *kernel_cmdline,
                               const char *dtb_filename)
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

    if (initrd_start) {
        qemu_fdt_setprop_cell(fdt, "/chosen", "linux,initrd-start",
                              initrd_start);

        qemu_fdt_setprop_cell(fdt, "/chosen", "linux,initrd-end",
                              initrd_end);
    }

    cpu_physical_memory_write(addr, fdt, fdt_size);
    g_free(fdt);
    return fdt_size;
}

static uint64_t translate_kernel_address(void *opaque, uint64_t addr)
{
    return addr - 0x30000000LL;
}

void microblaze_load_kernel(MicroBlazeCPU *cpu, hwaddr ddr_base,
                            uint32_t ramsize,
                            const char *initrd_filename,
                            const char *dtb_filename,
                            void (*machine_cpu_reset)(MicroBlazeCPU *))
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
    if (!dtb_arg && dtb_filename) {
        filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, dtb_filename);
    }

    boot_info.machine_cpu_reset = machine_cpu_reset;
    qemu_register_reset(main_cpu_reset, cpu);

    if (kernel_filename) {
        int kernel_size;
        uint64_t entry, high;
        uint32_t base32;
        int big_endian = 0;

#ifdef TARGET_WORDS_BIGENDIAN
        big_endian = 1;
#endif

        /* Boots a kernel elf binary.  */
        kernel_size = load_elf(kernel_filename, NULL, NULL, NULL,
                               &entry, NULL, &high, NULL,
                               big_endian, EM_MICROBLAZE, 0, 0);
        base32 = entry;
        if (base32 == 0xc0000000) {
            kernel_size = load_elf(kernel_filename, NULL,
                                   translate_kernel_address, NULL,
                                   &entry, NULL, NULL, NULL,
                                   big_endian, EM_MICROBLAZE, 0, 0);
        }
        /* Always boot into physical ram.  */
        boot_info.bootstrap_pc = (uint32_t)entry;

        /* If it wasn't an ELF image, try an u-boot image.  */
        if (kernel_size < 0) {
            hwaddr uentry, loadaddr = LOAD_UIMAGE_LOADADDR_INVALID;

            kernel_size = load_uimage(kernel_filename, &uentry, &loadaddr, 0,
                                      NULL, NULL);
            boot_info.bootstrap_pc = uentry;
            high = (loadaddr + kernel_size + 3) & ~3;
        }

        /* Not an ELF image nor an u-boot image, try a RAW image.  */
        if (kernel_size < 0) {
            kernel_size = load_image_targphys(kernel_filename, ddr_base,
                                              ramsize);
            boot_info.bootstrap_pc = ddr_base;
            high = (ddr_base + kernel_size + 3) & ~3;
        }

        if (initrd_filename) {
            int initrd_size;
            uint32_t initrd_offset;

            high = ROUND_UP(high + kernel_size, 4);
            boot_info.initrd_start = high;
            initrd_offset = boot_info.initrd_start - ddr_base;

            initrd_size = load_ramdisk(initrd_filename,
                                       boot_info.initrd_start,
                                       ramsize - initrd_offset);
            if (initrd_size < 0) {
                initrd_size = load_image_targphys(initrd_filename,
                                                  boot_info.initrd_start,
                                                  ramsize - initrd_offset);
            }
            if (initrd_size < 0) {
                error_report("could not load initrd '%s'",
                             initrd_filename);
                exit(EXIT_FAILURE);
            }
            boot_info.initrd_end = boot_info.initrd_start + initrd_size;
            high = ROUND_UP(high + initrd_size, 4);
        }

        boot_info.cmdline = high + 4096;
        if (kernel_cmdline && strlen(kernel_cmdline)) {
            pstrcpy_targphys("cmdline", boot_info.cmdline, 256, kernel_cmdline);
        }
        /* Provide a device-tree.  */
        boot_info.fdt = boot_info.cmdline + 4096;
        microblaze_load_dtb(boot_info.fdt, ramsize,
                            boot_info.initrd_start,
                            boot_info.initrd_end,
                            kernel_cmdline,
                            /* Preference a -dtb argument */
                            dtb_arg ? dtb_arg : filename);
    }
    g_free(filename);
}
