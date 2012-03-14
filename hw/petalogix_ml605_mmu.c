/*
 * Model of Petalogix linux reference design targeting Xilinx Spartan ml605
 * board.
 *
 * Copyright (c) 2011 Michal Simek <monstr@monstr.eu>
 * Copyright (c) 2011 PetaLogix
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
#include "xilinx.h"
#include "blockdev.h"
#include "pc.h"
#include "exec-memory.h"

#include "microblaze_boot.h"
#include "microblaze_pic_cpu.h"
#include "xilinx_axidma.h"

#define LMB_BRAM_SIZE  (128 * 1024)
#define FLASH_SIZE     (32 * 1024 * 1024)

#define BINARY_DEVICE_TREE_FILE "petalogix-ml605.dtb"

#define MEMORY_BASEADDR 0x50000000
#define FLASH_BASEADDR 0x86000000
#define INTC_BASEADDR 0x81800000
#define TIMER_BASEADDR 0x83c00000
#define UART16550_BASEADDR 0x83e00000
#define AXIENET_BASEADDR 0x82780000
#define AXIDMA_BASEADDR 0x84600000

static void machine_cpu_reset(CPUMBState *env)
{
    env->pvr.regs[10] = 0x0e000000; /* virtex 6 */
    /* setup pvr to match kernel setting */
    env->pvr.regs[5] |= PVR5_DCACHE_WRITEBACK_MASK;
    env->pvr.regs[0] |= PVR0_USE_FPU_MASK | PVR0_ENDI;
    env->pvr.regs[0] = (env->pvr.regs[0] & ~PVR0_VERSION_MASK) | (0x14 << 8);
    env->pvr.regs[2] ^= PVR2_USE_FPU2_MASK;
    env->pvr.regs[4] = 0xc56b8000;
    env->pvr.regs[5] = 0xc56be000;
}

static void
petalogix_ml605_init(ram_addr_t ram_size,
                          const char *boot_device,
                          const char *kernel_filename,
                          const char *kernel_cmdline,
                          const char *initrd_filename, const char *cpu_model)
{
    MemoryRegion *address_space_mem = get_system_memory();
    DeviceState *dev;
    CPUMBState *env;
    DriveInfo *dinfo;
    int i;
    target_phys_addr_t ddr_base = MEMORY_BASEADDR;
    MemoryRegion *phys_lmb_bram = g_new(MemoryRegion, 1);
    MemoryRegion *phys_ram = g_new(MemoryRegion, 1);
    qemu_irq irq[32], *cpu_irq;

    /* init CPUs */
    if (cpu_model == NULL) {
        cpu_model = "microblaze";
    }
    env = cpu_init(cpu_model);

    /* Attach emulated BRAM through the LMB.  */
    memory_region_init_ram(phys_lmb_bram, "petalogix_ml605.lmb_bram",
                           LMB_BRAM_SIZE);
    vmstate_register_ram_global(phys_lmb_bram);
    memory_region_add_subregion(address_space_mem, 0x00000000, phys_lmb_bram);

    memory_region_init_ram(phys_ram, "petalogix_ml605.ram", ram_size);
    vmstate_register_ram_global(phys_ram);
    memory_region_add_subregion(address_space_mem, ddr_base, phys_ram);

    dinfo = drive_get(IF_PFLASH, 0, 0);
    /* 5th parameter 2 means bank-width
     * 10th paremeter 0 means little-endian */
    pflash_cfi01_register(FLASH_BASEADDR,
                          NULL, "petalogix_ml605.flash", FLASH_SIZE,
                          dinfo ? dinfo->bdrv : NULL, (64 * 1024),
                          FLASH_SIZE >> 16,
                          2, 0x89, 0x18, 0x0000, 0x0, 0);


    cpu_irq = microblaze_pic_init_cpu(env);
    dev = xilinx_intc_create(INTC_BASEADDR, cpu_irq[0], 4);
    for (i = 0; i < 32; i++) {
        irq[i] = qdev_get_gpio_in(dev, i);
    }

    serial_mm_init(address_space_mem, UART16550_BASEADDR + 0x1000, 2,
                   irq[5], 115200, serial_hds[0], DEVICE_LITTLE_ENDIAN);

    /* 2 timers at irq 2 @ 100 Mhz.  */
    xilinx_timer_create(TIMER_BASEADDR, irq[2], 2, 100 * 1000000);

    /* axi ethernet and dma initialization. TODO: Dynamically connect them.  */
    {
        static struct XilinxDMAConnection dmach;

        xilinx_axiethernet_create(&dmach, &nd_table[0], 0x82780000,
                                  irq[3], 0x1000, 0x1000);
        xilinx_axiethernetdma_create(&dmach, 0x84600000,
                                     irq[1], irq[0], 100 * 1000000);
    }

    microblaze_load_kernel(env, ddr_base, ram_size, BINARY_DEVICE_TREE_FILE,
                                                            machine_cpu_reset);

}

static QEMUMachine petalogix_ml605_machine = {
    .name = "petalogix-ml605",
    .desc = "PetaLogix linux refdesign for xilinx ml605 little endian",
    .init = petalogix_ml605_init,
    .is_default = 0
};

static void petalogix_ml605_machine_init(void)
{
    qemu_register_machine(&petalogix_ml605_machine);
}

machine_init(petalogix_ml605_machine_init);
