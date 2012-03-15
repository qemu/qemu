/*
 * QEMU/mipssim emulation
 *
 * Emulates a very simple machine model similar to the one used by the
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
#include "mips_cpudevs.h"
#include "pc.h"
#include "isa.h"
#include "net.h"
#include "sysemu.h"
#include "boards.h"
#include "mips-bios.h"
#include "loader.h"
#include "elf.h"
#include "sysbus.h"
#include "exec-memory.h"

static struct _loaderparams {
    int ram_size;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *initrd_filename;
} loaderparams;

typedef struct ResetData {
    CPUMIPSState *env;
    uint64_t vector;
} ResetData;

static int64_t load_kernel(void)
{
    int64_t entry, kernel_high;
    long kernel_size;
    long initrd_size;
    ram_addr_t initrd_offset;
    int big_endian;

#ifdef TARGET_WORDS_BIGENDIAN
    big_endian = 1;
#else
    big_endian = 0;
#endif

    kernel_size = load_elf(loaderparams.kernel_filename, cpu_mips_kseg0_to_phys,
                           NULL, (uint64_t *)&entry, NULL,
                           (uint64_t *)&kernel_high, big_endian,
                           ELF_MACHINE, 1);
    if (kernel_size >= 0) {
        if ((entry & ~0x7fffffffULL) == 0x80000000)
            entry = (int32_t)entry;
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
            initrd_size = load_image_targphys(loaderparams.initrd_filename,
                initrd_offset, loaderparams.ram_size - initrd_offset);
        }
        if (initrd_size == (target_ulong) -1) {
            fprintf(stderr, "qemu: could not load initial ram disk '%s'\n",
                    loaderparams.initrd_filename);
            exit(1);
        }
    }
    return entry;
}

static void main_cpu_reset(void *opaque)
{
    ResetData *s = (ResetData *)opaque;
    CPUMIPSState *env = s->env;

    cpu_state_reset(env);
    env->active_tc.PC = s->vector & ~(target_ulong)1;
    if (s->vector & 1) {
        env->hflags |= MIPS_HFLAG_M16;
    }
}

static void mipsnet_init(int base, qemu_irq irq, NICInfo *nd)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_create(NULL, "mipsnet");
    qdev_set_nic_properties(dev, nd);
    qdev_init_nofail(dev);

    s = sysbus_from_qdev(dev);
    sysbus_connect_irq(s, 0, irq);
    memory_region_add_subregion(get_system_io(),
                                base,
                                sysbus_mmio_get_region(s, 0));
}

static void
mips_mipssim_init (ram_addr_t ram_size,
                   const char *boot_device,
                   const char *kernel_filename, const char *kernel_cmdline,
                   const char *initrd_filename, const char *cpu_model)
{
    char *filename;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    MemoryRegion *bios = g_new(MemoryRegion, 1);
    CPUMIPSState *env;
    ResetData *reset_info;
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
    reset_info = g_malloc0(sizeof(ResetData));
    reset_info->env = env;
    reset_info->vector = env->active_tc.PC;
    qemu_register_reset(main_cpu_reset, reset_info);

    /* Allocate RAM. */
    memory_region_init_ram(ram, "mips_mipssim.ram", ram_size);
    vmstate_register_ram_global(ram);
    memory_region_init_ram(bios, "mips_mipssim.bios", BIOS_SIZE);
    vmstate_register_ram_global(bios);
    memory_region_set_readonly(bios, true);

    memory_region_add_subregion(address_space_mem, 0, ram);

    /* Map the BIOS / boot exception handler. */
    memory_region_add_subregion(address_space_mem, 0x1fc00000LL, bios);
    /* Load a BIOS / boot exception handler image. */
    if (bios_name == NULL)
        bios_name = BIOS_FILENAME;
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    if (filename) {
        bios_size = load_image_targphys(filename, 0x1fc00000LL, BIOS_SIZE);
        g_free(filename);
    } else {
        bios_size = -1;
    }
    if ((bios_size < 0 || bios_size > BIOS_SIZE) && !kernel_filename) {
        /* Bail out if we have neither a kernel image nor boot vector code. */
        fprintf(stderr,
                "qemu: Could not load MIPS bios '%s', and no -kernel argument was specified\n",
                filename);
        exit(1);
    } else {
        /* We have a boot vector start address. */
        env->active_tc.PC = (target_long)(int32_t)0xbfc00000;
    }

    if (kernel_filename) {
        loaderparams.ram_size = ram_size;
        loaderparams.kernel_filename = kernel_filename;
        loaderparams.kernel_cmdline = kernel_cmdline;
        loaderparams.initrd_filename = initrd_filename;
        reset_info->vector = load_kernel();
    }

    /* Init CPU internal devices. */
    cpu_mips_irq_init_cpu(env);
    cpu_mips_clock_init(env);

    /* Register 64 KB of ISA IO space at 0x1fd00000. */
    isa_mmio_init(0x1fd00000, 0x00010000);

    /* A single 16450 sits at offset 0x3f8. It is attached to
       MIPS CPU INT2, which is interrupt 4. */
    if (serial_hds[0])
        serial_init(0x3f8, env->irq[4], 115200, serial_hds[0]);

    if (nd_table[0].vlan)
        /* MIPSnet uses the MIPS CPU INT0, which is interrupt 2. */
        mipsnet_init(0x4200, env->irq[2], &nd_table[0]);
}

static QEMUMachine mips_mipssim_machine = {
    .name = "mipssim",
    .desc = "MIPS MIPSsim platform",
    .init = mips_mipssim_init,
};

static void mips_mipssim_machine_init(void)
{
    qemu_register_machine(&mips_mipssim_machine);
}

machine_init(mips_mipssim_machine_init);
