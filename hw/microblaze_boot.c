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

#include "qemu-option.h"
#include "qemu-config.h"
#include "qemu-common.h"
#include "device_tree.h"
#include "loader.h"
#include "elf.h"

#include "microblaze_boot.h"

static struct
{
    void (*machine_cpu_reset)(CPUMBState *);
    uint32_t bootstrap_pc;
    uint32_t cmdline;
    uint32_t fdt;
} boot_info;

static void main_cpu_reset(void *opaque)
{
    CPUMBState *env = opaque;

    cpu_state_reset(env);
    env->regs[5] = boot_info.cmdline;
    env->regs[7] = boot_info.fdt;
    env->sregs[SR_PC] = boot_info.bootstrap_pc;
    if (boot_info.machine_cpu_reset) {
        boot_info.machine_cpu_reset(env);
    }
}

static int microblaze_load_dtb(target_phys_addr_t addr,
                                      uint32_t ramsize,
                                      const char *kernel_cmdline,
                                      const char *dtb_filename)
{
    int fdt_size;
#ifdef CONFIG_FDT
    void *fdt = NULL;
    int r;

    if (dtb_filename) {
        fdt = load_device_tree(dtb_filename, &fdt_size);
    }
    if (!fdt) {
        return 0;
    }

    if (kernel_cmdline) {
        r = qemu_devtree_setprop_string(fdt, "/chosen", "bootargs",
                                                        kernel_cmdline);
        if (r < 0) {
            fprintf(stderr, "couldn't set /chosen/bootargs\n");
        }
    }

    cpu_physical_memory_write(addr, (void *)fdt, fdt_size);
#else
    /* We lack libfdt so we cannot manipulate the fdt. Just pass on the blob
       to the kernel.  */
    if (dtb_filename) {
        fdt_size = load_image_targphys(dtb_filename, addr, 0x10000);
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

void microblaze_load_kernel(CPUMBState *env, target_phys_addr_t ddr_base,
                            uint32_t ramsize, const char *dtb_filename,
                                  void (*machine_cpu_reset)(CPUMBState *))
{

    QemuOpts *machine_opts;
    const char *kernel_filename = NULL;
    const char *kernel_cmdline = NULL;

    machine_opts = qemu_opts_find(qemu_find_opts("machine"), 0);
    if (machine_opts) {
        const char *dtb_arg;
        kernel_filename = qemu_opt_get(machine_opts, "kernel");
        kernel_cmdline = qemu_opt_get(machine_opts, "append");
        dtb_arg = qemu_opt_get(machine_opts, "dtb");
        if (dtb_arg) { /* Preference a -dtb argument */
            dtb_filename = dtb_arg;
        } else { /* default to pcbios dtb as passed by machine_init */
            dtb_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, dtb_filename);
        }
    }

    boot_info.machine_cpu_reset = machine_cpu_reset;
    qemu_register_reset(main_cpu_reset, env);

    if (kernel_filename) {
        int kernel_size;
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
        microblaze_load_dtb(boot_info.fdt, ram_size, kernel_cmdline,
                                                     dtb_filename);
    }

}
