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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/mips/mips.h"
#include "hw/mips/cpudevs.h"
#include "hw/char/serial.h"
#include "hw/isa/isa.h"
#include "net/net.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/mips/bios.h"
#include "hw/loader.h"
#include "elf.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"
#include "sysemu/qtest.h"
#include "sysemu/reset.h"

static struct _loaderparams {
    int ram_size;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *initrd_filename;
} loaderparams;

typedef struct ResetData {
    MIPSCPU *cpu;
    uint64_t vector;
} ResetData;

static int64_t load_kernel(void)
{
    int64_t entry, kernel_high, initrd_size;
    long kernel_size;
    ram_addr_t initrd_offset;
    int big_endian;

#ifdef TARGET_WORDS_BIGENDIAN
    big_endian = 1;
#else
    big_endian = 0;
#endif

    kernel_size = load_elf(loaderparams.kernel_filename, NULL,
                           cpu_mips_kseg0_to_phys, NULL,
                           (uint64_t *)&entry, NULL,
                           (uint64_t *)&kernel_high, NULL, big_endian,
                           EM_MIPS, 1, 0);
    if (kernel_size >= 0) {
        if ((entry & ~0x7fffffffULL) == 0x80000000) {
            entry = (int32_t)entry;
        }
    } else {
        error_report("could not load kernel '%s': %s",
                     loaderparams.kernel_filename,
                     load_elf_strerror(kernel_size));
        exit(1);
    }

    /* load initrd */
    initrd_size = 0;
    initrd_offset = 0;
    if (loaderparams.initrd_filename) {
        initrd_size = get_image_size(loaderparams.initrd_filename);
        if (initrd_size > 0) {
            initrd_offset = (kernel_high + ~INITRD_PAGE_MASK) &
                            INITRD_PAGE_MASK;
            if (initrd_offset + initrd_size > loaderparams.ram_size) {
                error_report("memory too small for initial ram disk '%s'",
                             loaderparams.initrd_filename);
                exit(1);
            }
            initrd_size = load_image_targphys(loaderparams.initrd_filename,
                initrd_offset, loaderparams.ram_size - initrd_offset);
        }
        if (initrd_size == (target_ulong) -1) {
            error_report("could not load initial ram disk '%s'",
                         loaderparams.initrd_filename);
            exit(1);
        }
    }
    return entry;
}

static void main_cpu_reset(void *opaque)
{
    ResetData *s = (ResetData *)opaque;
    CPUMIPSState *env = &s->cpu->env;

    cpu_reset(CPU(s->cpu));
    env->active_tc.PC = s->vector & ~(target_ulong)1;
    if (s->vector & 1) {
        env->hflags |= MIPS_HFLAG_M16;
    }
}

static void mipsnet_init(int base, qemu_irq irq, NICInfo *nd)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new("mipsnet");
    qdev_set_nic_properties(dev, nd);

    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_connect_irq(s, 0, irq);
    memory_region_add_subregion(get_system_io(),
                                base,
                                sysbus_mmio_get_region(s, 0));
}

static void
mips_mipssim_init(MachineState *machine)
{
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *initrd_filename = machine->initrd_filename;
    char *filename;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *isa = g_new(MemoryRegion, 1);
    MemoryRegion *bios = g_new(MemoryRegion, 1);
    MIPSCPU *cpu;
    CPUMIPSState *env;
    ResetData *reset_info;
    int bios_size;

    /* Init CPUs. */
    cpu = MIPS_CPU(cpu_create(machine->cpu_type));
    env = &cpu->env;

    reset_info = g_malloc0(sizeof(ResetData));
    reset_info->cpu = cpu;
    reset_info->vector = env->active_tc.PC;
    qemu_register_reset(main_cpu_reset, reset_info);

    /* Allocate RAM. */
    memory_region_init_rom(bios, NULL, "mips_mipssim.bios", BIOS_SIZE,
                           &error_fatal);

    memory_region_add_subregion(address_space_mem, 0, machine->ram);

    /* Map the BIOS / boot exception handler. */
    memory_region_add_subregion(address_space_mem, 0x1fc00000LL, bios);
    /* Load a BIOS / boot exception handler image. */
    if (bios_name == NULL) {
        bios_name = BIOS_FILENAME;
    }
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    if (filename) {
        bios_size = load_image_targphys(filename, 0x1fc00000LL, BIOS_SIZE);
        g_free(filename);
    } else {
        bios_size = -1;
    }
    if ((bios_size < 0 || bios_size > BIOS_SIZE) &&
        !kernel_filename && !qtest_enabled()) {
        /* Bail out if we have neither a kernel image nor boot vector code. */
        error_report("Could not load MIPS bios '%s', and no "
                     "-kernel argument was specified", bios_name);
        exit(1);
    } else {
        /* We have a boot vector start address. */
        env->active_tc.PC = (target_long)(int32_t)0xbfc00000;
    }

    if (kernel_filename) {
        loaderparams.ram_size = machine->ram_size;
        loaderparams.kernel_filename = kernel_filename;
        loaderparams.kernel_cmdline = kernel_cmdline;
        loaderparams.initrd_filename = initrd_filename;
        reset_info->vector = load_kernel();
    }

    /* Init CPU internal devices. */
    cpu_mips_irq_init_cpu(cpu);
    cpu_mips_clock_init(cpu);

    /* Register 64 KB of ISA IO space at 0x1fd00000. */
    memory_region_init_alias(isa, NULL, "isa_mmio",
                             get_system_io(), 0, 0x00010000);
    memory_region_add_subregion(get_system_memory(), 0x1fd00000, isa);

    /*
     * A single 16450 sits at offset 0x3f8. It is attached to
     * MIPS CPU INT2, which is interrupt 4.
     */
    if (serial_hd(0)) {
        DeviceState *dev = qdev_new(TYPE_SERIAL_MM);

        qdev_prop_set_chr(dev, "chardev", serial_hd(0));
        qdev_prop_set_uint8(dev, "regshift", 0);
        qdev_prop_set_uint8(dev, "endianness", DEVICE_LITTLE_ENDIAN);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, env->irq[4]);
        sysbus_add_io(SYS_BUS_DEVICE(dev), 0x3f8,
                      sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0));
    }

    if (nd_table[0].used)
        /* MIPSnet uses the MIPS CPU INT0, which is interrupt 2. */
        mipsnet_init(0x4200, env->irq[2], &nd_table[0]);
}

static void mips_mipssim_machine_init(MachineClass *mc)
{
    mc->desc = "MIPS MIPSsim platform";
    mc->init = mips_mipssim_init;
#ifdef TARGET_MIPS64
    mc->default_cpu_type = MIPS_CPU_TYPE_NAME("5Kf");
#else
    mc->default_cpu_type = MIPS_CPU_TYPE_NAME("24Kf");
#endif
    mc->default_ram_id = "mips_mipssim.ram";
}

DEFINE_MACHINE("mipssim", mips_mipssim_machine_init)
