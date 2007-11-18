/*
 * QEMU/mipssim emulation
 *
 * Emulates a very simple machine model similiar to the one use by the
 * proprietary MIPS emulator.
 * 
 * Copyright (c) 2007 Thiemo Seufer
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
#include "pc.h"
#include "isa.h"
#include "net.h"
#include "sysemu.h"
#include "boards.h"

#ifdef TARGET_WORDS_BIGENDIAN
#define BIOS_FILENAME "mips_bios.bin"
#else
#define BIOS_FILENAME "mipsel_bios.bin"
#endif

#ifdef TARGET_MIPS64
#define PHYS_TO_VIRT(x) ((x) | ~0x7fffffffULL)
#else
#define PHYS_TO_VIRT(x) ((x) | ~0x7fffffffU)
#endif

#define VIRT_TO_PHYS_ADDEND (-((int64_t)(int32_t)0x80000000))

static struct _loaderparams {
    int ram_size;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *initrd_filename;
} loaderparams;

static void load_kernel (CPUState *env)
{
    int64_t entry, kernel_low, kernel_high;
    long kernel_size;
    long initrd_size;
    ram_addr_t initrd_offset;

    kernel_size = load_elf(loaderparams.kernel_filename, VIRT_TO_PHYS_ADDEND,
                           &entry, &kernel_low, &kernel_high);
    if (kernel_size >= 0) {
        if ((entry & ~0x7fffffffULL) == 0x80000000)
            entry = (int32_t)entry;
        env->PC[env->current_tc] = entry;
    } else {
        fprintf(stderr, "qemu: could not load kernel '%s'\n",
                loaderparams.kernel_filename);
        exit(1);
    }

    /* load initrd */
    initrd_size = 0;
    initrd_offset = 0;
    if (loaderparams.initrd_filename) {
        initrd_size = get_image_size (loaderparams.initrd_filename);
        if (initrd_size > 0) {
            initrd_offset = (kernel_high + ~TARGET_PAGE_MASK) & TARGET_PAGE_MASK;
            if (initrd_offset + initrd_size > loaderparams.ram_size) {
                fprintf(stderr,
                        "qemu: memory too small for initial ram disk '%s'\n",
                        loaderparams.initrd_filename);
                exit(1);
            }
            initrd_size = load_image(loaderparams.initrd_filename,
                                     phys_ram_base + initrd_offset);
        }
        if (initrd_size == (target_ulong) -1) {
            fprintf(stderr, "qemu: could not load initial ram disk '%s'\n",
                    loaderparams.initrd_filename);
            exit(1);
        }
    }
}

static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    cpu_reset(env);

    if (loaderparams.kernel_filename)
        load_kernel (env);
}

static void
mips_mipssim_init (int ram_size, int vga_ram_size,
                   const char *boot_device, DisplayState *ds,
                   const char *kernel_filename, const char *kernel_cmdline,
                   const char *initrd_filename, const char *cpu_model)
{
    char buf[1024];
    unsigned long bios_offset;
    CPUState *env;
    int bios_size;

    /* Init CPUs. */
    if (cpu_model == NULL) {
#ifdef TARGET_MIPS64
        cpu_model = "5Kf";
#else
        cpu_model = "24Kf";
#endif
    }
    env = cpu_init(cpu_model);
    if (!env) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }
    register_savevm("cpu", 0, 3, cpu_save, cpu_load, env);
    qemu_register_reset(main_cpu_reset, env);

    /* Allocate RAM. */
    cpu_register_physical_memory(0, ram_size, IO_MEM_RAM);

    /* Load a BIOS / boot exception handler image. */
    bios_offset = ram_size + vga_ram_size;
    if (bios_name == NULL)
        bios_name = BIOS_FILENAME;
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, bios_name);
    bios_size = load_image(buf, phys_ram_base + bios_offset);
    if ((bios_size < 0 || bios_size > BIOS_SIZE) && !kernel_filename) {
        /* Bail out if we have neither a kernel image nor boot vector code. */
        fprintf(stderr,
                "qemu: Could not load MIPS bios '%s', and no -kernel argument was specified\n",
                buf);
        exit(1);
    } else {
        /* Map the BIOS / boot exception handler. */
        cpu_register_physical_memory(0x1fc00000LL,
                                     bios_size, bios_offset | IO_MEM_ROM);
        /* We have a boot vector start address. */
        env->PC[env->current_tc] = (target_long)(int32_t)0xbfc00000;
    }

    if (kernel_filename) {
        loaderparams.ram_size = ram_size;
        loaderparams.kernel_filename = kernel_filename;
        loaderparams.kernel_cmdline = kernel_cmdline;
        loaderparams.initrd_filename = initrd_filename;
        load_kernel(env);
    }

    /* Init CPU internal devices. */
    cpu_mips_irq_init_cpu(env);
    cpu_mips_clock_init(env);
    cpu_mips_irqctrl_init();

    /* Register 64 KB of ISA IO space at 0x1fd00000. */
    isa_mmio_init(0x1fd00000, 0x00010000);

    /* A single 16450 sits at offset 0x3f8. It is attached to
       MIPS CPU INT2, which is interrupt 4. */
    if (serial_hds[0])
        serial_init(0x3f8, env->irq[4], serial_hds[0]);

    if (nd_table[0].vlan) {
        if (nd_table[0].model == NULL
            || strcmp(nd_table[0].model, "mipsnet") == 0) {
            /* MIPSnet uses the MIPS CPU INT0, which is interrupt 2. */
            mipsnet_init(0x4200, env->irq[2], &nd_table[0]);
        } else if (strcmp(nd_table[0].model, "?") == 0) {
            fprintf(stderr, "qemu: Supported NICs: mipsnet\n");
            exit (1);
        } else {
            fprintf(stderr, "qemu: Unsupported NIC: %s\n", nd_table[0].model);
            exit (1);
        }
    }
}

QEMUMachine mips_mipssim_machine = {
    "mipssim",
    "MIPS MIPSsim platform",
    mips_mipssim_init,
};
