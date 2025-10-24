/*
 * Model of Xilinx Virtex5 ML507 PPC-440 refdesign.
 *
 * Copyright (c) 2010 Edgar E. Iglesias.
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
#include "qemu/datadir.h"
#include "qemu/units.h"
#include "exec/page-protection.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "hw/char/serial-mm.h"
#include "hw/block/flash.h"
#include "system/system.h"
#include "system/reset.h"
#include "hw/boards.h"
#include "system/device_tree.h"
#include "hw/loader.h"
#include "elf.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/option.h"

#include "hw/intc/ppc-uic.h"
#include "hw/ppc/ppc.h"
#include "hw/ppc/ppc4xx.h"
#include "hw/qdev-properties.h"

#include <libfdt.h>

#define EPAPR_MAGIC    (0x45504150)
#define FLASH_SIZE     (16 * MiB)

#define INTC_BASEADDR       0x81800000
#define UART16550_BASEADDR  0x83e01003
#define TIMER_BASEADDR      0x83c00000
#define PFLASH_BASEADDR     0xfc000000

#define TIMER_IRQ           3
#define UART16550_IRQ       9

static struct boot_info
{
    uint32_t bootstrap_pc;
    uint32_t cmdline;
    uint32_t fdt;
    uint32_t ima_size;
    void *vfdt;
} boot_info;

static PowerPCCPU *ppc440_init_xilinx(const char *cpu_type, uint32_t sysclk)
{
    PowerPCCPU *cpu;
    CPUPPCState *env;
    DeviceState *uicdev;
    SysBusDevice *uicsbd;

    cpu = POWERPC_CPU(cpu_create(cpu_type));
    env = &cpu->env;

    ppc_booke_timers_init(cpu, sysclk, 0/* no flags */);

    ppc_dcr_init(env, NULL, NULL);

    /* interrupt controller */
    uicdev = qdev_new(TYPE_PPC_UIC);
    ppc4xx_dcr_realize(PPC4xx_DCR_DEVICE(uicdev), cpu, &error_fatal);
    object_unref(OBJECT(uicdev));
    uicsbd = SYS_BUS_DEVICE(uicdev);
    sysbus_connect_irq(uicsbd, PPCUIC_OUTPUT_INT,
                       qdev_get_gpio_in(DEVICE(cpu), PPC40x_INPUT_INT));
    sysbus_connect_irq(uicsbd, PPCUIC_OUTPUT_CINT,
                       qdev_get_gpio_in(DEVICE(cpu), PPC40x_INPUT_CINT));

    /* This board doesn't wire anything up to the inputs of the UIC. */
    return cpu;
}

static void main_cpu_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;
    struct boot_info *bi = env->load_info;

    cpu_reset(CPU(cpu));
    /* Linux Kernel Parameters (passing device tree):
       *   r3: pointer to the fdt
       *   r4: 0
       *   r5: 0
       *   r6: epapr magic
       *   r7: size of IMA in bytes
       *   r8: 0
       *   r9: 0
    */
    env->gpr[1] = (16 * MiB) - 8;
    /* Provide a device-tree.  */
    env->gpr[3] = bi->fdt;
    env->nip = bi->bootstrap_pc;

    /* Create a mapping spanning the 32bit addr space. */
    booke_set_tlb(&env->tlb.tlbe[0], 0, 0, 1U << 31);
    booke_set_tlb(&env->tlb.tlbe[1], 0x80000000, 0x80000000, 1U << 31);
    env->gpr[6] = EPAPR_MAGIC;
    env->gpr[7] = bi->ima_size;
}

#define BINARY_DEVICE_TREE_FILE "virtex-ml507.dtb"
static int xilinx_load_device_tree(MachineState *machine,
                                   hwaddr addr,
                                   hwaddr initrd_base,
                                   hwaddr initrd_size)
{
    char *path;
    int fdt_size;
    void *fdt = NULL;
    int r;
    const char *dtb_filename;

    dtb_filename = machine->dtb;
    if (dtb_filename) {
        fdt = load_device_tree(dtb_filename, &fdt_size);
        if (!fdt) {
            error_report("Error while loading device tree file '%s'",
                dtb_filename);
        }
    } else {
        /* Try the local "ppc.dtb" override.  */
        fdt = load_device_tree("ppc.dtb", &fdt_size);
        if (!fdt) {
            path = qemu_find_file(QEMU_FILE_TYPE_DTB, BINARY_DEVICE_TREE_FILE);
            if (path) {
                fdt = load_device_tree(path, &fdt_size);
                g_free(path);
            }
        }
    }
    if (!fdt) {
        return 0;
    }

    r = qemu_fdt_setprop_cell(fdt, "/chosen", "linux,initrd-start",
                              initrd_base);
    if (r < 0) {
        error_report("couldn't set /chosen/linux,initrd-start");
    }

    r = qemu_fdt_setprop_cell(fdt, "/chosen", "linux,initrd-end",
                              (initrd_base + initrd_size));
    if (r < 0) {
        error_report("couldn't set /chosen/linux,initrd-end");
    }

    r = qemu_fdt_setprop_string(fdt, "/chosen", "bootargs",
                                machine->kernel_cmdline);
    if (r < 0)
        fprintf(stderr, "couldn't set /chosen/bootargs\n");
    cpu_physical_memory_write(addr, fdt, fdt_size);

    /* Set machine->fdt for 'dumpdtb' QMP/HMP command */
    machine->fdt = fdt;

    return fdt_size;
}

static void virtex_init(MachineState *machine)
{
    const char *kernel_filename = machine->kernel_filename;
    hwaddr initrd_base = 0;
    int initrd_size = 0;
    MemoryRegion *address_space_mem = get_system_memory();
    DeviceState *dev;
    PowerPCCPU *cpu;
    CPUPPCState *env;
    hwaddr ram_base = 0;
    DriveInfo *dinfo;
    qemu_irq irq[32], cpu_irq;
    int kernel_size;
    int i;

    /* init CPUs */
    cpu = ppc440_init_xilinx(machine->cpu_type, 400000000);
    env = &cpu->env;

    if (env->mmu_model != POWERPC_MMU_BOOKE) {
        error_report("MMU model %i not supported by this machine",
                     env->mmu_model);
        exit(1);
    }

    qemu_register_reset(main_cpu_reset, cpu);

    memory_region_add_subregion(address_space_mem, ram_base, machine->ram);

    dinfo = drive_get(IF_PFLASH, 0, 0);
    pflash_cfi01_register(PFLASH_BASEADDR, "virtex.flash", FLASH_SIZE,
                          dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                          64 * KiB, 1, 0x89, 0x18, 0x0000, 0x0, 1);

    cpu_irq = qdev_get_gpio_in(DEVICE(cpu), PPC40x_INPUT_INT);
    dev = qdev_new("xlnx.xps-intc");
    qdev_prop_set_enum(dev, "endianness", ENDIAN_MODE_BIG);
    qdev_prop_set_uint32(dev, "kind-of-intr", 0);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, INTC_BASEADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, cpu_irq);
    for (i = 0; i < 32; i++) {
        irq[i] = qdev_get_gpio_in(dev, i);
    }

    serial_mm_init(address_space_mem, UART16550_BASEADDR, 2, irq[UART16550_IRQ],
                   115200, serial_hd(0), DEVICE_LITTLE_ENDIAN);

    /* 2 timers at irq 2 @ 62 Mhz.  */
    dev = qdev_new("xlnx.xps-timer");
    qdev_prop_set_enum(dev, "endianness", ENDIAN_MODE_BIG);
    qdev_prop_set_uint32(dev, "one-timer-only", 0);
    qdev_prop_set_uint32(dev, "clock-frequency", 62 * 1000000);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, TIMER_BASEADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[TIMER_IRQ]);

    if (kernel_filename) {
        uint64_t entry, high;
        hwaddr boot_offset;

        /* Boots a kernel elf binary.  */
        kernel_size = load_elf(kernel_filename, NULL, NULL, NULL,
                               &entry, NULL, &high, NULL,
                               ELFDATA2MSB, PPC_ELF_MACHINE, 0, 0);
        boot_info.bootstrap_pc = entry & 0x00ffffff;

        if (kernel_size < 0) {
            boot_offset = 0x1200000;
            /* If we failed loading ELF's try a raw image.  */
            kernel_size = load_image_targphys(kernel_filename,
                                              boot_offset,
                                              machine->ram_size, &error_fatal);
            boot_info.bootstrap_pc = boot_offset;
            high = boot_info.bootstrap_pc + kernel_size + 8192;
        }

        boot_info.ima_size = kernel_size;

        /* Load initrd. */
        if (machine->initrd_filename) {
            initrd_base = high = ROUND_UP(high, 4);
            initrd_size = load_image_targphys(machine->initrd_filename,
                                              high, machine->ram_size - high,
                                              &error_fatal);
            high = ROUND_UP(high + initrd_size, 4);
        }

        /* Provide a device-tree.  */
        boot_info.fdt = high + (8192 * 2);
        boot_info.fdt &= ~8191;

        xilinx_load_device_tree(machine, boot_info.fdt,
                                initrd_base, initrd_size);
    }
    env->load_info = &boot_info;
}

static void virtex_machine_init(MachineClass *mc)
{
    mc->desc = "Xilinx Virtex ML507 reference design";
    mc->init = virtex_init;
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("440-xilinx");
    mc->default_ram_id = "ram";
}

DEFINE_MACHINE("virtex-ml507", virtex_machine_init)
