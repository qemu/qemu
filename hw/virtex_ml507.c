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

#include "sysbus.h"
#include "hw.h"
#include "pc.h"
#include "net.h"
#include "flash.h"
#include "sysemu.h"
#include "devices.h"
#include "boards.h"
#include "device_tree.h"
#include "loader.h"
#include "elf.h"
#include "qemu-log.h"
#include "exec-memory.h"

#include "ppc.h"
#include "ppc4xx.h"
#include "ppc440.h"
#include "ppc405.h"

#include "blockdev.h"
#include "xilinx.h"

#define EPAPR_MAGIC    (0x45504150)
#define FLASH_SIZE     (16 * 1024 * 1024)

static struct boot_info
{
    uint32_t bootstrap_pc;
    uint32_t cmdline;
    uint32_t fdt;
    uint32_t ima_size;
    void *vfdt;
} boot_info;

/* Create reset TLB entries for BookE, spanning the 32bit addr space.  */
static void mmubooke_create_initial_mapping(CPUState *env,
                                     target_ulong va,
                                     target_phys_addr_t pa)
{
    ppcemb_tlb_t *tlb = &env->tlb.tlbe[0];

    tlb->attr = 0;
    tlb->prot = PAGE_VALID | ((PAGE_READ | PAGE_WRITE | PAGE_EXEC) << 4);
    tlb->size = 1 << 31; /* up to 0x80000000  */
    tlb->EPN = va & TARGET_PAGE_MASK;
    tlb->RPN = pa & TARGET_PAGE_MASK;
    tlb->PID = 0;

    tlb = &env->tlb.tlbe[1];
    tlb->attr = 0;
    tlb->prot = PAGE_VALID | ((PAGE_READ | PAGE_WRITE | PAGE_EXEC) << 4);
    tlb->size = 1 << 31; /* up to 0xffffffff  */
    tlb->EPN = 0x80000000 & TARGET_PAGE_MASK;
    tlb->RPN = 0x80000000 & TARGET_PAGE_MASK;
    tlb->PID = 0;
}

static CPUState *ppc440_init_xilinx(ram_addr_t *ram_size,
                                    int do_init,
                                    const char *cpu_model,
                                    uint32_t sysclk)
{
    CPUState *env;
    qemu_irq *irqs;

    env = cpu_init(cpu_model);
    if (!env) {
        fprintf(stderr, "Unable to initialize CPU!\n");
        exit(1);
    }

    ppc_booke_timers_init(env, sysclk, 0/* no flags */);

    ppc_dcr_init(env, NULL, NULL);

    /* interrupt controller */
    irqs = g_malloc0(sizeof(qemu_irq) * PPCUIC_OUTPUT_NB);
    irqs[PPCUIC_OUTPUT_INT] = ((qemu_irq *)env->irq_inputs)[PPC40x_INPUT_INT];
    irqs[PPCUIC_OUTPUT_CINT] = ((qemu_irq *)env->irq_inputs)[PPC40x_INPUT_CINT];
    ppcuic_init(env, irqs, 0x0C0, 0, 1);
    return env;
}

static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    struct boot_info *bi = env->load_info;

    cpu_reset(env);
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
static int xilinx_load_device_tree(target_phys_addr_t addr,
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

    /* Try the local "ppc.dtb" override.  */
    fdt = load_device_tree("ppc.dtb", &fdt_size);
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
    if (r < 0)
        fprintf(stderr, "couldn't set /chosen/bootargs\n");
    cpu_physical_memory_write (addr, (void *)fdt, fdt_size);
#else
    /* We lack libfdt so we cannot manipulate the fdt. Just pass on the blob
       to the kernel.  */
    fdt_size = load_image_targphys("ppc.dtb", addr, 0x10000);
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

static void virtex_init(ram_addr_t ram_size,
                        const char *boot_device,
                        const char *kernel_filename,
                        const char *kernel_cmdline,
                        const char *initrd_filename, const char *cpu_model)
{
    MemoryRegion *address_space_mem = get_system_memory();
    DeviceState *dev;
    CPUState *env;
    target_phys_addr_t ram_base = 0;
    DriveInfo *dinfo;
    ram_addr_t phys_ram;
    qemu_irq irq[32], *cpu_irq;
    int kernel_size;
    int i;

    /* init CPUs */
    if (cpu_model == NULL) {
        cpu_model = "440-Xilinx";
    }

    env = ppc440_init_xilinx(&ram_size, 1, cpu_model, 400000000);
    qemu_register_reset(main_cpu_reset, env);

    phys_ram = qemu_ram_alloc(NULL, "ram", ram_size);
    cpu_register_physical_memory(ram_base, ram_size, phys_ram | IO_MEM_RAM);

    dinfo = drive_get(IF_PFLASH, 0, 0);
    pflash_cfi01_register(0xfc000000, NULL, "virtex.flash", FLASH_SIZE,
                          dinfo ? dinfo->bdrv : NULL, (64 * 1024),
                          FLASH_SIZE >> 16,
                          1, 0x89, 0x18, 0x0000, 0x0, 1);

    cpu_irq = (qemu_irq *) &env->irq_inputs[PPC40x_INPUT_INT];
    dev = xilinx_intc_create(0x81800000, cpu_irq[0], 0);
    for (i = 0; i < 32; i++) {
        irq[i] = qdev_get_gpio_in(dev, i);
    }

    serial_mm_init(address_space_mem, 0x83e01003ULL, 2, irq[9], 115200,
                   serial_hds[0], DEVICE_LITTLE_ENDIAN);

    /* 2 timers at irq 2 @ 62 Mhz.  */
    xilinx_timer_create(0x83c00000, irq[3], 2, 62 * 1000000);

    if (kernel_filename) {
        uint64_t entry, low, high;
        target_phys_addr_t boot_offset;

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

        /* Provide a device-tree.  */
        boot_info.fdt = high + (8192 * 2);
        boot_info.fdt &= ~8191;
        xilinx_load_device_tree(boot_info.fdt, ram_size, 0, 0, kernel_cmdline);
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
