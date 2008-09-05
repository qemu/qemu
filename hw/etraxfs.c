/*
 * QEMU ETRAX System Emulator
 *
 * Copyright (c) 2007 Edgar E. Iglesias, Axis Communications AB.
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
#include <time.h>
#include <sys/time.h>
#include "hw.h"
#include "net.h"
#include "flash.h"
#include "sysemu.h"
#include "devices.h"
#include "boards.h"

#include "etraxfs.h"

#define FLASH_SIZE 0x2000000
#define INTMEM_SIZE (128 * 1024)

static uint32_t bootstrap_pc;

static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    cpu_reset(env);

    env->pregs[PR_CCS] &= ~I_FLAG;
    env->pc = bootstrap_pc;
}

static
void bareetraxfs_init (ram_addr_t ram_size, int vga_ram_size,
                       const char *boot_device, DisplayState *ds,
                       const char *kernel_filename, const char *kernel_cmdline,
                       const char *initrd_filename, const char *cpu_model)
{
    CPUState *env;
    struct etraxfs_pic *pic;
    void *etraxfs_dmac;
    struct etraxfs_dma_client *eth[2] = {NULL, NULL};
    int kernel_size;
    int i;
    ram_addr_t phys_ram;
    ram_addr_t phys_flash;
    ram_addr_t phys_intmem;

    /* init CPUs */
    if (cpu_model == NULL) {
        cpu_model = "crisv32";
    }
    env = cpu_init(cpu_model);
    qemu_register_reset(main_cpu_reset, env);

    /* allocate RAM */
    phys_ram = qemu_ram_alloc(ram_size);
    cpu_register_physical_memory(0x40000000, ram_size, phys_ram | IO_MEM_RAM);

    /* The ETRAX-FS has 128Kb on chip ram, the docs refer to it as the 
       internal memory.  */
    phys_intmem = qemu_ram_alloc(INTMEM_SIZE);
    cpu_register_physical_memory(0x38000000, INTMEM_SIZE,
                                 phys_intmem | IO_MEM_RAM);


    phys_flash = qemu_ram_alloc(FLASH_SIZE);
    i = drive_get_index(IF_PFLASH, 0, 0);
    pflash_cfi02_register(0x0, phys_flash,
                          drives_table[i].bdrv, (64 * 1024),
                          FLASH_SIZE >> 16,
                          1, 2, 0x0000, 0x0000, 0x0000, 0x0000,
                          0x555, 0x2aa);
    pic = etraxfs_pic_init(env, 0x3001c000);
    etraxfs_dmac = etraxfs_dmac_init(env, 0x30000000, 10);
    for (i = 0; i < 10; i++) {
        /* On ETRAX, odd numbered channels are inputs.  */
        etraxfs_dmac_connect(etraxfs_dmac, i, pic->irq + 7 + i, i & 1);
    }

    /* Add the two ethernet blocks.  */
    eth[0] = etraxfs_eth_init(&nd_table[0], env, pic->irq + 25, 0x30034000);
    if (nb_nics > 1)
        eth[1] = etraxfs_eth_init(&nd_table[1], env, pic->irq + 26, 0x30036000);

    /* The DMA Connector block is missing, hardwire things for now.  */
    etraxfs_dmac_connect_client(etraxfs_dmac, 0, eth[0]);
    etraxfs_dmac_connect_client(etraxfs_dmac, 1, eth[0] + 1);
    if (eth[1]) {
        etraxfs_dmac_connect_client(etraxfs_dmac, 6, eth[1]);
        etraxfs_dmac_connect_client(etraxfs_dmac, 7, eth[1] + 1);
    }

    /* 2 timers.  */
    etraxfs_timer_init(env, pic->irq + 0x1b, pic->nmi + 1, 0x3001e000);
    etraxfs_timer_init(env, pic->irq + 0x1b, pic->nmi + 1, 0x3005e000);

    for (i = 0; i < 4; i++) {
        if (serial_hds[i]) {
            etraxfs_ser_init(env, pic->irq + 0x14 + i,
                             serial_hds[i], 0x30026000 + i * 0x2000);
        }
    }

    if (kernel_filename) {
        uint64_t entry;
        /* Boots a kernel elf binary, os/linux-2.6/vmlinux from the axis 
           devboard SDK.  */
        kernel_size = load_elf(kernel_filename, 0,
                               &entry, NULL, NULL);
        bootstrap_pc = entry;
        if (kernel_size < 0) {
            /* Takes a kimage from the axis devboard SDK.  */
            kernel_size = load_image(kernel_filename, phys_ram_base + 0x4000);
            bootstrap_pc = 0x40004000;
            /* magic for boot.  */
            env->regs[8] = 0x56902387;
            env->regs[9] = 0x40004000 + kernel_size;
        }
    }
    env->pc = bootstrap_pc;

    printf ("pc =%x\n", env->pc);
    printf ("ram size =%ld\n", ram_size);
}

QEMUMachine bareetraxfs_machine = {
    "bareetraxfs",
    "Bare ETRAX FS board",
    bareetraxfs_init,
    0x8000000,
};
