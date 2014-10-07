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

#include "hw/sysbus.h"
#include "hw/hw.h"
#include "hw/char/serial.h"
#include "hw/block/flash.h"
#include "sysemu/sysemu.h"
#include "hw/devices.h"
#include "hw/boards.h"
#include "sysemu/device_tree.h"
#include "hw/loader.h"
#include "elf.h"
#include "qemu/log.h"
#include "exec/address-spaces.h"

#include "hw/ppc/ppc.h"
#include "hw/ppc/ppc4xx.h"
#include "ppc405.h"

#include "sysemu/block-backend.h"
#include "qapi/qmp/qerror.h"

#define EPAPR_MAGIC    (0x45504150)
#define FLASH_SIZE     (16 * 1024 * 1024)

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

/* Create reset TLB entries for BookE, spanning the 32bit addr space.  */
static void mmubooke_create_initial_mapping(CPUPPCState *env,
                                     target_ulong va,
                                     hwaddr pa)
{
    ppcemb_tlb_t *tlb = &env->tlb.tlbe[0];

    tlb->attr = 0;
    tlb->prot = PAGE_VALID | ((PAGE_READ | PAGE_WRITE | PAGE_EXEC) << 4);
    tlb->size = 1U << 31; /* up to 0x80000000  */
    tlb->EPN = va & TARGET_PAGE_MASK;
    tlb->RPN = pa & TARGET_PAGE_MASK;
    tlb->PID = 0;

    tlb = &env->tlb.tlbe[1];
    tlb->attr = 0;
    tlb->prot = PAGE_VALID | ((PAGE_READ | PAGE_WRITE | PAGE_EXEC) << 4);
    tlb->size = 1U << 31; /* up to 0xffffffff  */
    tlb->EPN = 0x80000000 & TARGET_PAGE_MASK;
    tlb->RPN = 0x80000000 & TARGET_PAGE_MASK;
    tlb->PID = 0;
}

static PowerPCCPU *ppc440_init_xilinx(ram_addr_t *ram_size,
                                      int do_init,
                                      const char *cpu_model,
                                      uint32_t sysclk)
{
    PowerPCCPU *cpu;
    CPUPPCState *env;
    qemu_irq *irqs;

    cpu = cpu_ppc_init(cpu_model);
    if (cpu == NULL) {
        fprintf(stderr, "Unable to initialize CPU!\n");
        exit(1);
    }
    env = &cpu->env;

    ppc_booke_timers_init(cpu, sysclk, 0/* no flags */);

    ppc_dcr_init(env, NULL, NULL);

    /* interrupt controller */
    irqs = g_malloc0(sizeof(qemu_irq) * PPCUIC_OUTPUT_NB);
    irqs[PPCUIC_OUTPUT_INT] = ((qemu_irq *)env->irq_inputs)[PPC40x_INPUT_INT];
    irqs[PPCUIC_OUTPUT_CINT] = ((qemu_irq *)env->irq_inputs)[PPC40x_INPUT_CINT];
    ppcuic_init(env, irqs, 0x0C0, 0, 1);
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
    env->gpr[1] = (16<<20) - 8;
    /* Provide a device-tree.  */
    env->gpr[3] = bi->fdt;
    env->nip = bi->bootstrap_pc;

    /* Create a mapping for the kernel.  */
    mmubooke_create_initial_mapping(env, 0, 0);
    env->gpr[6] = tswap32(EPAPR_MAGIC);
    env->gpr[7] = bi->ima_size;
}

#define BINARY_DEVICE_TREE_FILE "virtex-ml507.dtb"
static int xilinx_load_device_tree(hwaddr addr,
                                      uint32_t ramsize,
                                      hwaddr initrd_base,
                                      hwaddr initrd_size,
                                      const char *kernel_cmdline)
{
    char *path;
    int fdt_size;
    void *fdt = NULL;
    int r;
    const char *dtb_filename;

    dtb_filename = qemu_opt_get(qemu_get_machine_opts(), "dtb");
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
            path = qemu_find_file(QEMU_FILE_TYPE_BIOS, BINARY_DEVICE_TREE_FILE);
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

    r = qemu_fdt_setprop_string(fdt, "/chosen", "bootargs", kernel_cmdline);
    if (r < 0)
        fprintf(stderr, "couldn't set /chosen/bootargs\n");
    cpu_physical_memory_write(addr, fdt, fdt_size);
    return fdt_size;
}

static void virtex_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *cpu_model = machine->cpu_model;
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    hwaddr initrd_base = 0;
    int initrd_size = 0;
    MemoryRegion *address_space_mem = get_system_memory();
    DeviceState *dev;
    PowerPCCPU *cpu;
    CPUPPCState *env;
    hwaddr ram_base = 0;
    DriveInfo *dinfo;
    MemoryRegion *phys_ram = g_new(MemoryRegion, 1);
    qemu_irq irq[32], *cpu_irq;
    int kernel_size;
    int i;

    /* init CPUs */
    if (cpu_model == NULL) {
        cpu_model = "440-Xilinx";
    }

    cpu = ppc440_init_xilinx(&ram_size, 1, cpu_model, 400000000);
    env = &cpu->env;
    qemu_register_reset(main_cpu_reset, cpu);

    memory_region_allocate_system_memory(phys_ram, NULL, "ram", ram_size);
    memory_region_add_subregion(address_space_mem, ram_base, phys_ram);

    dinfo = drive_get(IF_PFLASH, 0, 0);
    pflash_cfi01_register(PFLASH_BASEADDR, NULL, "virtex.flash", FLASH_SIZE,
                          dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                          (64 * 1024), FLASH_SIZE >> 16,
                          1, 0x89, 0x18, 0x0000, 0x0, 1);

    cpu_irq = (qemu_irq *) &env->irq_inputs[PPC40x_INPUT_INT];
    dev = qdev_create(NULL, "xlnx.xps-intc");
    qdev_prop_set_uint32(dev, "kind-of-intr", 0);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, INTC_BASEADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, cpu_irq[0]);
    for (i = 0; i < 32; i++) {
        irq[i] = qdev_get_gpio_in(dev, i);
    }

    serial_mm_init(address_space_mem, UART16550_BASEADDR, 2, irq[UART16550_IRQ],
                   115200, serial_hds[0], DEVICE_LITTLE_ENDIAN);

    /* 2 timers at irq 2 @ 62 Mhz.  */
    dev = qdev_create(NULL, "xlnx.xps-timer");
    qdev_prop_set_uint32(dev, "one-timer-only", 0);
    qdev_prop_set_uint32(dev, "clock-frequency", 62 * 1000000);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, TIMER_BASEADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[TIMER_IRQ]);

    if (kernel_filename) {
        uint64_t entry, low, high;
        hwaddr boot_offset;

        /* Boots a kernel elf binary.  */
        kernel_size = load_elf(kernel_filename, NULL, NULL,
                               &entry, &low, &high, 1, ELF_MACHINE, 0);
        boot_info.bootstrap_pc = entry & 0x00ffffff;

        if (kernel_size < 0) {
            boot_offset = 0x1200000;
            /* If we failed loading ELF's try a raw image.  */
            kernel_size = load_image_targphys(kernel_filename,
                                              boot_offset,
                                              ram_size);
            boot_info.bootstrap_pc = boot_offset;
            high = boot_info.bootstrap_pc + kernel_size + 8192;
        }

        boot_info.ima_size = kernel_size;

        /* Load initrd. */
        if (machine->initrd_filename) {
            initrd_base = high = ROUND_UP(high, 4);
            initrd_size = load_image_targphys(machine->initrd_filename,
                                              high, ram_size - high);

            if (initrd_size < 0) {
                error_report("couldn't load ram disk '%s'",
                             machine->initrd_filename);
                exit(1);
            }
            high = ROUND_UP(high + initrd_size, 4);
        }

        /* Provide a device-tree.  */
        boot_info.fdt = high + (8192 * 2);
        boot_info.fdt &= ~8191;

        xilinx_load_device_tree(boot_info.fdt, ram_size,
                                initrd_base, initrd_size,
                                kernel_cmdline);
    }
    env->load_info = &boot_info;
}

static QEMUMachine virtex_machine = {
    .name = "virtex-ml507",
    .desc = "Xilinx Virtex ML507 reference design",
    .init = virtex_init,
};

static void virtex_machine_init(void)
{
    qemu_register_machine(&virtex_machine);
}

machine_init(virtex_machine_init);
